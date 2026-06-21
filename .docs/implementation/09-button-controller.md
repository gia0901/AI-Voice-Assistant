# 09 — ButtonController

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.* Component nhỏ — guide ngắn.
>
> Nền: [../development/app_layer.md](../development/app_layer.md) §4 · [01-gpio-hal.md](01-gpio-hal.md)

---

## 1. 🎯 Mục tiêu & vị trí

Chạy GPIO thread: gọi `IGpioHal::waitEvent`, dịch `GpioEvent` thô → *ý định ngữ nghĩa*, rồi định tuyến:
- PTT → đẩy `ButtonEvent` lên bus (FSM xử lý).
- Vol± → gọi thẳng `audio.applyGain(±step)` (**không** qua FSM, không đổi state).

**File:** `app/ButtonController.hpp/.cpp`.
**Phụ thuộc:** `IGpioHal` (01), `EventBus` (00), `AudioPipeline` (04).

---

## 2. 📜 Hợp đồng

```cpp
class ButtonController {
public:
    ButtonController(IGpioHal* gpio, EventBus& bus, AudioPipeline& audio, float volStep=0.1f);
    void start();   // spawn GPIO thread
    void stop();    // dừng thread (atomic + join)
};
```
**Ràng buộc:** GPIO thread blocking-wait (low CPU); PTT đi qua bus (tuần tự hóa về main); Vol± xử lý trực tiếp; dừng sạch khi shutdown.

---

## 3. 🤔 Quyết định trước khi code

<details><summary><b>Q1.</b> Vì sao Vol± không đi qua FSM/bus?</summary>

> Âm lượng **độc lập** với trạng thái hội thoại — chỉnh được ở bất kỳ state nào, không làm FSM rối thêm nhánh. Đẩy qua bus chỉ thêm độ trễ + event vô nghĩa với FSM. ([app_layer.md §4](../development/app_layer.md))
</details>

<details><summary><b>Q2.</b> Vì sao PTT phải đi qua bus mà không gọi thẳng FSM?</summary>

> GPIO chạy thread riêng; gọi thẳng `fsm.handle()` từ thread khác = race vào FSM/LVGL. Bus tuần tự hóa về main thread. ([threading_eventbus.md](../knowledge/threading_eventbus.md))
</details>

<details><summary><b>Q3.</b> Dừng thread đang block trong <code>waitEvent</code> ra sao?</summary>

> `waitEvent(timeoutMs)` có timeout → thread quay lại kiểm cờ `running_` định kỳ. `stop()` set false → thread thoát ở lần timeout kế. (Vì vậy timeout không nên quá dài.)
</details>

**⚖️ Bạn tự chốt:** `volStep` (gợi ý 0.1, hoặc bước phi tuyến theo tai người — audio_alsa §5), timeout của waitEvent.

---

## 4. 🔨 Trình tự dựng

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | skeleton + thread loop gọi waitEvent | nhấn nút → log đúng |
| 2 | PTT → `bus.push(ButtonEvent)` | FSM nhận, đổi state |
| 3 | Vol± → `audio.applyGain` clamp [0,1] | âm lượng đổi, state không đổi |
| 4 | `stop()` join sạch | shutdown không treo |

---

## 5. 🧩 Khung code tự điền

```cpp
#pragma once
#include "IGpioHal.hpp"
#include "EventBus.hpp"
#include "AudioPipeline.hpp"
#include <thread>
#include <atomic>
namespace bbb {
class ButtonController {
public:
    ButtonController(hal::IGpioHal* g, EventBus& bus, AudioPipeline& a, float step=0.1f)
        : gpio_(g), bus_(bus), audio_(a), step_(step) {}
    ~ButtonController(){ stop(); }
    void start();
    void stop();
private:
    void loop();
    hal::IGpioHal* gpio_; EventBus& bus_; AudioPipeline& audio_; float step_;
    float vol_ = 0.7f;
    std::thread th_; std::atomic<bool> running_{false};
};
} // namespace bbb
```
Thuật toán `loop`:
```
while running_:
    GpioEvent ev
    nếu !gpio_->waitEvent(ev, 200ms): continue       // timeout → kiểm cờ
    switch ev.id:
      Ptt:    bus_.push(ButtonEvent{ ev.id, ev.edge })   // để FSM lo
      VolUp:   nếu ev.edge==Press: vol_=clamp(vol_+step_,0,1); audio_.applyGain(vol_)
      VolDown: nếu ev.edge==Press: vol_=clamp(vol_-step_,0,1); audio_.applyGain(vol_)
```
> TODO(you): chỉ phản ứng Vol ở cạnh Press (không Release) để 1 lần nhấn = 1 bước. PTT đẩy **cả** Press và Release (FSM cần cả hai). Đây là chỗ dễ nhầm.

---

## 6. ⚠️ Cạm bẫy

- **Vol± đi qua bus/FSM** → thêm nhánh FSM vô ích, trễ. Xử lý trực tiếp.
- **Phản ứng Vol ở cả Press lẫn Release** → mỗi lần nhấn nhảy 2 bước.
- **Quên đẩy PTT Release** → FSM kẹt LISTENING tới FR-8.
- **timeout waitEvent quá dài** → `stop()` chờ lâu mới join được.
- **Gọi thẳng `fsm.handle` từ GPIO thread** → race. Qua bus.

---

## 7. ✅ Checkpoint review

**Xong khi:** PTT đổi state qua bus; Vol± đổi âm lượng nghe được, state giữ nguyên; shutdown join sạch.

**Đưa Claude review:** `ButtonController.cpp`. Hỏi:
1. *"Vol có nhảy 2 bước/nhấn vì xử lý cả Press+Release không?"*
2. *"Thread có dừng+join sạch khi shutdown không?"*
3. *"Có đường nào tôi chạm thẳng vào FSM/LVGL từ GPIO thread không?"*
