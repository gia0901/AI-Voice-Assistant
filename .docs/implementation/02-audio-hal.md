# 02 — AudioHal (ALSA)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý, không phải lời giải.*
>
> **Phong cách mục 5:** mỗi hàm theo 3 lớp — **Bản chất** (bài toán & hướng giải) → **API toolbox** (tra cứu cục bộ) → **Pseudo** (chừa quyết định khó ở `TODO(you)`).
>
> Nền: [../knowledge/audio_alsa.md](../knowledge/audio_alsa.md) (đọc trước — period/xrun/plughw) · [../development/hal_layer.md](../development/hal_layer.md)

---

## 1. 🎯 Mục tiêu & vị trí

HAL audio: thu (capture) + phát (playback) PCM 16kHz/S16_LE/mono, + software gain (FR-6).

**File:** `hal/include/IAudioHal.hpp`, `hal/audio/AlsaAudioHal.hpp/.cpp`, `tests/hal/MockAudioHal.hpp`.
**Phụ thuộc:** `Types.hpp` (`PcmBuffer`, `AudioFormat`), libasound. **Tiền đề phần cứng:** USB audio đã chạy (`arecord`/`aplay` OK ở Week 1).

---

## 2. 📜 Hợp đồng

Trích [../architecture.md](../architecture.md) §2.1:
```cpp
struct AudioFormat { unsigned rate = 16000; unsigned channels = 1; }; // S16_LE

class IAudioHal {
public:
    virtual ~IAudioHal() = default;
    virtual int  openCapture(const AudioFormat&) = 0;
    virtual int  readPeriod(int16_t* dst, size_t frames) = 0;   // blocking, trả frames đọc / <0 lỗi
    virtual void closeCapture() = 0;
    virtual int  openPlayback(const AudioFormat&) = 0;
    virtual int  writePeriod(const int16_t* src, size_t frames) = 0;
    virtual void drain() = 0;
    virtual void closePlayback() = 0;
    virtual void setVolume(float gain) = 0;     // 0.0–1.0, nhân trước khi ghi
};
```
**Ràng buộc:** đọc/ghi **theo từng period** (để AudioPipeline kiểm cờ dừng + FR-8 — [04](04-audio-pipeline.md)); blocking; lỗi trả mã; RAII đóng `snd_pcm_t` ở dtor.

---

## 3. 🤔 Quyết định trước khi code

<details><summary><b>Q1.</b> <code>hw:</code> hay <code>plughw:</code>?</summary>

> `plughw:` — tự resample/convert, an toàn với USB adapter rẻ không hỗ trợ 16kHz native. Đánh đổi: thêm một lớp xử lý. Đo bằng `arecord` trước; nếu `hw` chạy 16k thì dùng `hw` cho nhẹ. ([audio_alsa.md §2](../knowledge/audio_alsa.md))
</details>

<details><summary><b>Q2.</b> Period size chọn sao?</summary>

> Nhỏ → độ trễ thấp nhưng dễ xrun; lớn → ngược lại. Bắt đầu một giá trị mặc định (vd 1024 frames), *đo xrun* rồi chỉnh. Đừng tối ưu trước khi đo.
</details>

<details><summary><b>Q3.</b> Software gain — clamp thế nào?</summary>

> `out = clamp(sample * gain, INT16_MIN, INT16_MAX)`. Gain > 1 dễ clip → méo. Áp gain ngay trước `writePeriod`. ([audio_alsa.md §5](../knowledge/audio_alsa.md))
</details>

<details><summary><b>Q4.</b> Tên device lấy từ đâu?</summary>

> Không hardcode `hw:1`. USB card đổi index khi reboot. Dùng `plughw:CARD=<name>,DEV=0` (tên ổn định) và đưa vào `config.json`. ([troubleshooting.md §3](../troubleshooting.md))
</details>

**⚖️ Bạn tự chốt:** device name, period size, `hw` vs `plughw`.

---

## 4. 🔨 Trình tự dựng

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | `IAudioHal.hpp` + `MockAudioHal` | compile; mock test (gain) đỏ |
| 2 | `openPlayback` + `writePeriod` + `drain` | phát được file PCM thu sẵn nghe rõ |
| 3 | `openCapture` + `readPeriod` | thu 3s → ghi ra file → nghe lại OK |
| 4 | `setVolume` software gain | đổi gain nghe to/nhỏ rõ |
| 5 | xrun recovery (`-EPIPE` → `snd_pcm_prepare`) | chạy lâu không chết vì xrun |

---

## 5. 🧩 Khung code tự điền

### 5.1 `hal/audio/AlsaAudioHal.hpp` — *skeleton*
```cpp
#pragma once
#include "IAudioHal.hpp"
#include <alsa/asoundlib.h>
#include <atomic>
#include <vector>
namespace bbb {
class AlsaAudioHal : public IAudioHal {
public:
    explicit AlsaAudioHal(std::string device) : device_(std::move(device)) {}
    ~AlsaAudioHal() override;                  // close cả 2 stream
    AlsaAudioHal(const AlsaAudioHal&) = delete;
    int  openCapture(const AudioFormat&) override;
    int  readPeriod(int16_t*, size_t) override;
    void closeCapture() override;
    int  openPlayback(const AudioFormat&) override;
    int  writePeriod(const int16_t*, size_t) override;
    void drain() override;
    void closePlayback() override;
    void setVolume(float g) override { gain_ = g; }    // atomic
private:
    std::string device_;
    snd_pcm_t* cap_  = nullptr;
    snd_pcm_t* play_ = nullptr;
    std::atomic<float> gain_{1.0f};
    std::vector<int16_t> gainBuf_;   // TODO(you): reserve sẵn, tránh malloc mỗi period
    // helper: setHwParams(pcm, fmt) dùng chung cho cả 2 stream
};
} // namespace bbb
```

---

### 5.2 `open*()` + `setHwParams()` (cấu hình stream)

**Bản chất.** Mở một PCM device là **đàm phán cấu hình với driver**: bạn *đề nghị* (rate/format/channels/period), driver trả về *cái gần nhất nó làm được*. Mấu chốt là `*_near`: bạn xin 16000 nhưng có thể nhận 16001 → phải **đọc lại giá trị thực** và quyết có chấp nhận không. `openCapture`/`openPlayback` chỉ khác nhau ở hướng stream; phần config dùng chung một helper → DRY.

**API toolbox** (libasound):

| Hàm | Công dụng | Gotcha |
|-----|-----------|--------|
| `snd_pcm_open(&pcm, device, STREAM_CAPTURE/PLAYBACK, 0)` | Mở device theo hướng | tên device từ config (Q4), không hardcode |
| `snd_pcm_hw_params_alloca(&hp)` | Cấp vùng tham số trên stack | macro alloca — không tự free, đừng dùng ngoài scope |
| `snd_pcm_hw_params_any(pcm, hp)` | Nạp cấu hình khả dĩ đầy đủ | gọi trước khi set gì |
| `..._set_access(.,.,INTERLEAVED)` / `..._set_format(.,.,S16_LE)` / `..._set_channels(.,.,1)` | Đặt access/format/kênh | mono = 1 |
| `..._set_rate_near` / `..._set_period_size_near` | Đặt rate/period "gần nhất" | trả giá trị *thực* qua tham chiếu — phải kiểm |
| `snd_pcm_hw_params(pcm, hp)` | **Commit** cấu hình | trước bước này chưa có gì hiệu lực |

**Pseudo:**
```
openX:  snd_pcm_open(&X_, device_, STREAM_X, 0);  lỗi → return <0
        setHwParams(X_, fmt);  lỗi → đóng X_; return <0;  return 0

setHwParams(pcm, fmt):
        hw_params_alloca(&hp); hw_params_any(pcm, hp)
        set_access(INTERLEAVED); set_format(S16_LE); set_channels(fmt.channels)
        set_rate_near(fmt.rate); set_period_size_near(period)
        hw_params(pcm, hp)                         // commit
        đọc lại rate thực → nếu lệch nhiều, log cảnh báo
        bất kỳ bước lỗi → return <0
```
> **TODO(you):** kiểm rate thực trả về có đúng 16000 không (`rate_near` có thể lệch); quyết ngưỡng "lệch bao nhiêu thì từ chối".

---

### 5.3 `readPeriod()` / `writePeriod()`

**Bản chất.** I/O audio chạy theo **nhịp period**: mỗi lời gọi xử lý đúng một period rồi trả về — để tầng trên (AudioPipeline) còn chen vào kiểm cờ dừng / timeout FR-8. Sự cố thường gặp là **xrun** (overrun khi đọc, underrun khi ghi): buffer phần cứng cạn/tràn vì CPU không kịp → ALSA trả `-EPIPE`, stream vào trạng thái lỗi và **phải `prepare` lại** mới chạy tiếp. Coi xrun là *bình thường, hồi phục được*, không phải lỗi chết. Gain áp **ngay trước khi ghi**, vào buffer tạm cấp phát sẵn (không malloc mỗi period).

**API toolbox** (libasound):

| Hàm | Công dụng | Gotcha |
|-----|-----------|--------|
| `snd_pcm_readi(pcm, dst, frames)` | Đọc interleaved, trả #frames | `-EPIPE` = overrun |
| `snd_pcm_writei(pcm, src, frames)` | Ghi interleaved, trả #frames | `-EPIPE` = underrun |
| `snd_pcm_prepare(pcm)` | Đưa stream lỗi về sẵn sàng | bắt buộc sau `-EPIPE` để chạy tiếp |

**Pseudo:**
```
read:   n = snd_pcm_readi(cap_, dst, frames)
        n == -EPIPE → snd_pcm_prepare(cap_); return 0     // overrun: bỏ period này
        n <  0      → log; return n
        return n

write:  for i: gainBuf_[i] = clamp(src[i] * gain_, INT16_MIN, INT16_MAX)
        n = snd_pcm_writei(play_, gainBuf_.data(), frames)
        n == -EPIPE → snd_pcm_prepare(play_); return 0     // underrun
        return n
```
> **TODO(you):** `gainBuf_` reserve ở đâu để khỏi malloc mỗi period? (gợi ý: resize 1 lần theo period trong `openPlayback`). Sau `-EPIPE` trả 0: tầng trên hiểu "không có dữ liệu lần này" có đúng không? Đây là chỗ Claude soi hiệu năng + đồng bộ.

---

### 5.4 `drain()` / `closePlayback()` / `closeCapture()` / `setVolume()`

**Bản chất.** Kết thúc playback có **hai ngữ nghĩa khác nhau**: `drain` = phát nốt buffer rồi mới dừng (đúng khi TTS nói xong tự nhiên); `drop` = cắt ngay (đúng khi PTT interrupt SPEAKING). Chọn nhầm = âm thanh bị cụt hoặc trễ. `setVolume` chỉ lưu gain (atomic) — nó là dữ liệu, không phải lời gọi ALSA.

**API toolbox** (libasound):

| Hàm | Công dụng | Gotcha |
|-----|-----------|--------|
| `snd_pcm_drain(pcm)` | Chờ phát hết buffer rồi dừng | blocking tới khi hết |
| `snd_pcm_drop(pcm)` | Vứt buffer, dừng ngay | dùng cho interrupt |
| `snd_pcm_close(pcm)` | Đóng & giải phóng handle | guard `NULL`; gán lại `nullptr` sau khi đóng |

**Pseudo:** `drain` → `snd_pcm_drain(play_)`. `closeX` → nếu `X_` khác null: `snd_pcm_close(X_); X_ = nullptr`. `setVolume` → `gain_ = clamp(g, 0, max)`.
> **TODO(you):** interrupt SPEAKING ([08](08-state-machine.md)) dùng `drain` hay `drop`? Khớp với hành vi state machine mong đợi.

---

### 5.5 `~AlsaAudioHal()`
**Bản chất.** RAII đối xứng: đóng cả hai stream nếu còn mở. An toàn `NULL`.
**Pseudo:** gọi `closeCapture()` + `closePlayback()` (đã guard null) trong dtor.

---

### 5.6 `tests/hal/MockAudioHal.hpp` — *harness, gần đủ*
```cpp
namespace bbb {
class MockAudioHal : public IAudioHal {
public:
    std::vector<int16_t> fakeCapture, playbackSink;
    float lastGain = 1.0f;
    int openCapture(const AudioFormat&) override { return 0; }
    int readPeriod(int16_t* d, size_t f) override {
        size_t n = std::min(f, fakeCapture.size());
        std::copy_n(fakeCapture.begin(), n, d);
        fakeCapture.erase(fakeCapture.begin(), fakeCapture.begin()+n);
        return (int)n;
    }
    void closeCapture() override {}
    int openPlayback(const AudioFormat&) override { return 0; }
    int writePeriod(const int16_t* s, size_t f) override {
        playbackSink.insert(playbackSink.end(), s, s+f); return (int)f; }
    void drain() override {} void closePlayback() override {}
    void setVolume(float g) override { lastGain = g; }
};
} // namespace bbb
```
> Test gợi ý: nạp `fakeCapture` → assert pipeline đọc đủ; ghi qua gain → assert `playbackSink` bị scale đúng.

---

## 6. ⚠️ Cạm bẫy ([troubleshooting.md §3](../troubleshooting.md))

- **`hw:` mở lỗi** vì card không hỗ trợ 16k → `plughw:`.
- **"Device or resource busy"** → process khác giữ card (PulseAudio?). `fuser /dev/snd/*`.
- **Quên xử lý `-EPIPE`** → app chết khi xrun đầu tiên. Phải `snd_pcm_prepare` recover.
- **malloc buffer gain mỗi period** → giật. Reserve sẵn.
- **Hardcode `hw:1`** → reboot đổi index. Dùng `CARD=` name.
- **`drain` vs `drop`** → `drain` phát hết rồi dừng (playback xong tự nhiên); `drop` cắt ngay (interrupt SPEAKING). Chọn nhầm = cụt tiếng hoặc trễ.

---

## 7. ✅ Checkpoint review

**Xong khi:** thu→phát loopback nghe rõ; gain đổi âm lượng; chạy vài phút không chết vì xrun; MockAudioHal test (gain + capture) pass trên VM.

**Đưa Claude review:** `AlsaAudioHal.cpp`. Hỏi:
1. *"Xử lý `-EPIPE` của tôi có làm mất đồng bộ stream không?"*
2. *"Buffer gain của tôi có cấp phát lại mỗi period không?"*
3. *"Vòng đời `snd_pcm_t` có rò khi open fail giữa chừng không?"*
