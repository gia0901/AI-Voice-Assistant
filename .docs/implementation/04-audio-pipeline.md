# 04 — AudioPipeline (FR-8 + gain)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.*
>
> Nền: [../development/app_layer.md](../development/app_layer.md) · [../knowledge/threading_eventbus.md](../knowledge/threading_eventbus.md) · FR-1/FR-8/NFR-4

---

## 1. 🎯 Mục tiêu & vị trí

Middleware điều phối thu âm: chạy capture thread, dừng khi PTT nhả **hoặc** quá 15s (FR-8), gom buffer, đẩy `RecordingComplete`/`RecordingTimeout` lên bus; quản software gain (gọi xuống `IAudioHal`).

**File:** `middleware/audio_pipeline/AudioPipeline.hpp/.cpp`.
**Phụ thuộc:** `IAudioHal` (guide 02), `EventBus`+`Types` (guide 00).

---

## 2. 📜 Hợp đồng

```cpp
class AudioPipeline {
public:
    AudioPipeline(IAudioHal* hal, EventBus& bus, int maxSeconds = 15);
    void start();                 // gọi khi vào LISTENING — spawn capture thread
    void stop();                  // gọi khi PTT release — dừng thread, đẩy RecordingComplete
    void applyGain(float gain);   // Vol± → hal->setVolume
    // playback có thể tách lớp riêng hoặc gộp ở đây (xem Q3)
};
```
**Ràng buộc:** `start/stop` gọi từ main thread (FSM); capture chạy thread riêng; buffer **move** vào event; FR-8 timeout tự đẩy `RecordingTimeout` ngay cả khi PTT chưa nhả.

---

## 3. 🤔 Quyết định trước khi code

<details><summary><b>Q1.</b> Capture thread đang block trong <code>readPeriod</code>, dừng nó kiểu gì?</summary>

> `std::atomic<bool> running_`. Thread đọc **từng period** (không đọc một phát 15s), sau mỗi period kiểm `running_`. `stop()` set false → thread thoát sau period hiện tại. Đây là lý do HAL đọc theo period. ([app_layer.md](../development/app_layer.md))
</details>

<details><summary><b>Q2.</b> FR-8 timeout đo thế nào?</summary>

> Đếm tổng frame đã đọc; khi `tổng >= maxSeconds * rate` → tự dừng + đẩy `RecordingTimeout`. Không cần timer riêng — suy ra từ số frame (16000/s). Đơn giản hơn một std::timer.
</details>

<details><summary><b>Q3.</b> Playback nằm ở AudioPipeline hay lớp riêng?</summary>

> Tùy bạn: gộp (một lớp "audio io") hay tách `PlaybackPlayer`. Gợi ý tách nhẹ: capture và playback có vòng đời khác nhau (capture theo LISTENING, playback theo SPEAKING). *Quyết định và lý giải* — Claude sẽ review ranh giới.
</details>

<details><summary><b>Q4.</b> stop() có chờ thread join không?</summary>

> Có — `stop()` set cờ rồi `join()` để chắc chắn buffer hoàn tất trước khi đẩy event. Nhưng cẩn thận: đừng join trên main thread nếu thread đang block lâu trong readPeriod (period phải đủ ngắn để vòng lặp quay). Đây là điểm tinh tế Claude soi.
</details>

**⚖️ Bạn tự chốt:** maxSeconds (mặc định 15, configurable — NFR-5), gộp/tách playback.

---

## 4. 🔨 Trình tự dựng

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | skeleton + `applyGain` → hal | gain xuống HAL đúng (mock) |
| 2 | `start` spawn thread đọc period vào buffer | thu được buffer khi start→stop |
| 3 | `stop` set cờ + join + push `RecordingComplete{move buffer}` | event lên bus, buffer không copy |
| 4 | FR-8: đếm frame, tự push `RecordingTimeout` | giữ "PTT" > 15s (mock) → timeout |
| 5 | test với `MockAudioHal.fakeCapture` | logic kiểm trên VM, không cần board |

---

## 5. 🧩 Khung code tự điền

### 5.1 `AudioPipeline.hpp` — *skeleton*
```cpp
#pragma once
#include "IAudioHal.hpp"
#include "EventBus.hpp"
#include <thread>
#include <atomic>
namespace bbb {
class AudioPipeline {
public:
    AudioPipeline(hal::IAudioHal* hal, EventBus& bus, int maxSeconds = 15)
        : hal_(hal), bus_(bus), maxFrames_(maxSeconds * 16000) {}
    ~AudioPipeline() { stop(); }
    void start();
    void stop();
    void applyGain(float g) { hal_->setVolume(g); }
private:
    void captureLoop();          // thân chạy trên thread
    hal::IAudioHal* hal_;
    EventBus& bus_;
    size_t maxFrames_;
    std::thread th_;
    std::atomic<bool> running_{false};
};
} // namespace bbb
```

### 5.2 Thuật toán `start` / `captureLoop` / `stop`
```
start():
    nếu running_ → return            // tránh double start
    hal_->openCapture({16000,1})
    running_ = true
    th_ = std::thread(&AudioPipeline::captureLoop, this)

captureLoop():
    PcmBuffer buf; buf.reserve(maxFrames_)
    const size_t PERIOD = 1024
    int16_t tmp[PERIOD]
    while running_:
        n = hal_->readPeriod(tmp, PERIOD)
        nếu n > 0: buf.insert(end, tmp, tmp+n)
        nếu buf.size() >= maxFrames_:       // FR-8
            running_ = false
            hal_->closeCapture()
            bus_.push(RecordingTimeout{})
            return
    hal_->closeCapture()
    bus_.push(RecordingComplete{ std::move(buf) })   // PTT-release path

stop():
    nếu !running_ → return
    running_ = false
    nếu th_.joinable(): th_.join()
```
> TODO(you): có race giữa `stop()` (main) set running_=false và captureLoop tự set false do timeout? Cả hai cùng push event được không? (gợi ý: chỉ một path được push — dùng một cờ "đã kết thúc" hoặc để stop() không push, timeout không cho stop() join nhầm. Đây là **chỗ khó nhất** — hỏi Claude.)

### 5.3 Khung test
```cpp
TEST_CASE("FR-8: quá maxFrames thì phát RecordingTimeout") {
    MockAudioHal hal;
    hal.fakeCapture.assign(16 * 16000, 0);   // 16s dữ liệu > 15s
    EventBus bus; AudioPipeline pipe(&hal, bus, 15);
    pipe.start();
    // TODO(you): chờ thread chạy hết, pop bus, assert là RecordingTimeout
}
```

---

## 6. ⚠️ Cạm bẫy

- **Đọc một phát 15s** thay vì từng period → không dừng được giữa chừng (PTT nhả vô tác dụng).
- **Hai path cùng push event** (stop + timeout) → main nhận 2 event, FSM rối. Đảm bảo đúng 1.
- **Copy buffer** vào event → 480KB. `std::move`.
- **join() khi period quá dài** → main thread treo. Period đủ ngắn.
- **Quên reserve buffer** → realloc liên tục khi insert.

---

## 7. ✅ Checkpoint review

**Xong khi:** start→stop ra `RecordingComplete` với buffer đúng độ dài; giữ >15s ra `RecordingTimeout` (đúng 1 event); gain xuống HAL; test FR-8 pass trên VM.

**Đưa Claude review:** `AudioPipeline.cpp` (đặc biệt logic stop vs timeout). Hỏi:
1. *"Race giữa `stop()` và FR-8 self-stop của tôi có thể đẩy 2 event hoặc double-join không?"*
2. *"Buffer có được move (không copy) vào event không?"*
3. *"Period size + join có khiến main thread treo lâu không?"*
