# 02 — AudioHal (ALSA)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý, không phải lời giải.*
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
**Ràng buộc:** đọc/ghi **theo từng period** (để AudioPipeline kiểm cờ dừng + FR-8 — guide 04); blocking; lỗi trả mã; RAII đóng `snd_pcm_t` ở dtor.

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

### 5.1 `AlsaAudioHal.hpp` — *skeleton*
```cpp
#pragma once
#include "IAudioHal.hpp"
#include <alsa/asoundlib.h>
#include <atomic>
namespace bbb::hal {
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
    snd_pcm_t* cap_ = nullptr;
    snd_pcm_t* play_ = nullptr;
    std::atomic<float> gain_{1.0f};
    // helper: setHwParams(pcm, fmt) dùng chung cho cả 2 stream
};
} // namespace bbb::hal
```

### 5.2 Thuật toán `setHwParams` (dùng chung)
```
snd_pcm_hw_params_alloca(&hp)
snd_pcm_hw_params_any(pcm, hp)
set_access(INTERLEAVED); set_format(S16_LE); set_channels(1)
set_rate_near(16000); set_period_size_near(period)
snd_pcm_hw_params(pcm, hp)        // commit
nếu bất kỳ bước lỗi → trả mã <0
```
> TODO(you): kiểm rate thực tế trả về có đúng 16000 không (rate_near có thể lệch) — log cảnh báo nếu lệch.

### 5.3 Thuật toán `readPeriod` / `writePeriod`
```
read:  n = snd_pcm_readi(cap_, dst, frames)
       nếu n == -EPIPE → snd_pcm_prepare(cap_); return 0   // overrun, bỏ period này
       nếu n <  0      → log; return n
       return n

write: áp gain: for i: src2[i] = clamp(src[i] * gain_)
       n = snd_pcm_writei(play_, src2, frames)
       nếu n == -EPIPE → snd_pcm_prepare(play_); return 0   // underrun
       return n
```
> TODO(you): buffer tạm cho gain — cấp phát ở đâu để khỏi malloc mỗi period? (gợi ý: member vector reserve sẵn). Đây là chỗ Claude soi hiệu năng.

### 5.4 `MockAudioHal.hpp` — *harness, gần đủ*
```cpp
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
```

---

## 6. ⚠️ Cạm bẫy ([troubleshooting.md §3](../troubleshooting.md))

- **`hw:` mở lỗi** vì card không hỗ trợ 16k → `plughw:`.
- **"Device or resource busy"** → process khác giữ card (PulseAudio?). `fuser /dev/snd/*`.
- **Quên xử lý `-EPIPE`** → app chết khi xrun đầu tiên. Phải `snd_pcm_prepare` recover.
- **malloc buffer gain mỗi period** → giật. Reserve sẵn.
- **Hardcode `hw:1`** → reboot đổi index. Dùng `CARD=` name.
- **`drain` vs `drop`** → `drain` phát hết buffer rồi mới dừng (đúng cho playback xong); `drop` cắt ngay (đúng cho interrupt SPEAKING).

---

## 7. ✅ Checkpoint review

**Xong khi:** thu→phát loopback nghe rõ; gain đổi âm lượng; chạy vài phút không chết vì xrun; MockAudioHal test (gain + capture) pass trên VM.

**Đưa Claude review:** `AlsaAudioHal.cpp`. Hỏi:
1. *"Xử lý `-EPIPE` của tôi có làm mất đồng bộ stream không?"*
2. *"Buffer gain của tôi có cấp phát lại mỗi period không?"*
3. *"Vòng đời `snd_pcm_t` có rò khi open fail giữa chừng không?"*
