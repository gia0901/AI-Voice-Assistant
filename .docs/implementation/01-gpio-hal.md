# 01 — GpioHal (libgpiod)

> Implementation guide (tiếng Việt, Rule §18). **Exemplar** cho template 7 mục — xem cách dùng ở [README.md](README.md). Nhắc lại nguyên tắc: *giàn giáo + gợi ý, không phải lời giải*. Phần thân hàm để bạn điền.
>
> Lý thuyết nền: [../development/hal_layer.md](../development/hal_layer.md) · Phase A: [../knowledge/strategy_roadmap.md](../knowledge/strategy_roadmap.md) (mục "Phase A")

---

## 1. 🎯 Mục tiêu & vị trí

Dựng HAL điều khiển nút bấm (PTT, Vol+, Vol-) và 1 LED trạng thái, qua **libgpiod** (chardev, Decision #8).

**File sẽ tạo:**
```
hal/include/IGpioHal.hpp     # interface (contract)
hal/gpio/GpiodHal.hpp/.cpp   # impl thật trên BBB
tests/hal/MockGpioHal.hpp    # impl giả để test trên VM
```

**Phụ thuộc phải có trước:** `common/Types.hpp` (định nghĩa `ButtonId`, `Edge`, `GpioEvent`, `GpioPinMap`). Không phụ thuộc EventBus — HAL chỉ *trả* sự kiện; việc đẩy `ButtonEvent` lên bus là của GPIO thread ở tầng app ([09-button-controller.md] / `main`).

> ⚠️ **Tiền đề phần cứng chưa có trong overlay.** [BBB-VOICE-ASSISTANT.dts](../../kernel/overlays/BBB-VOICE-ASSISTANT.dts) hiện chỉ khai báo chân màn hình (SPI + DC/RESET). Nút bấm + LED **chưa** có pin. Trước khi code: chọn 4 chân GPIO rảnh (3 nút + 1 LED), đảm bảo *không* trùng chân đã mux cho SPI/DC/RESET, ghi lại dạng `(chip, offset)`. Ví dụ P9_15=gpio1_16 → chip=1, offset=16. Các chân này nạp vào app qua `GpioPinMap` trong `config.json`, không cần sửa overlay (GPIO mode mặc định).

---

## 2. 📜 Hợp đồng (contract)

Trích [../architecture.md](../architecture.md) §2.1 — **không đổi chữ ký tùy tiện**:

```cpp
enum class ButtonId { Ptt, VolUp, VolDown };
enum class Edge     { Press, Release };
struct GpioEvent    { ButtonId id; Edge edge; };

class IGpioHal {
public:
    virtual ~IGpioHal() = default;
    virtual int  init(const GpioPinMap& pins) = 0;        // 0 = OK, <0 = lỗi
    virtual bool waitEvent(GpioEvent& out, int timeoutMs) = 0; // true nếu có event
    virtual void setLed(bool on) = 0;
};
```

**Ràng buộc:**
- `waitEvent` **blocking** với timeout — gọi từ *một* GPIO thread riêng (architecture.md §3). Trả `false` khi hết timeout mà không có sự kiện (để thread kiểm cờ dừng rồi chờ lại).
- **Không ném exception** qua biên — lỗi trả mã (`int`/`bool`), tầng trên gói thành event ([coding_guide.md §5](../development/coding_guide.md)).
- **Debounce nằm ở đây** (HAL), không ở ButtonController ([hal_layer.md §6](../development/hal_layer.md)).
- RAII: chip/line đóng trong destructor, cấm copy ([coding_guide.md §3](../development/coding_guide.md)).

---

## 3. 🤔 Quyết định trước khi code (tự trả lời trước)

<details><summary><b>Q1.</b> libgpiod v1 hay v2? API khác nhau hoàn toàn.</summary>

> BBB Debian 12 (bookworm) ship **libgpiod 1.6.x → API v1**. Kiểm: `gpiodetect --version` hoặc `dpkg -l libgpiod2`. Guide này dùng API v1 (`gpiod_line_request_*`, `gpiod_line_event_wait`). Nếu lỡ ở v2 (Debian 13+), API là `gpiod_line_request` + `gpiod_line_request_wait_edge_events` — *đừng trộn hai bản*.
</details>

<details><summary><b>Q2.</b> Nút nối active-low hay active-high? Ảnh hưởng "Press" là cạnh nào.</summary>

> Phổ biến nhất: nút nối **xuống GND + pull-up nội** → hở = mức cao, nhấn = mức thấp → **Press = FALLING edge**. Khi đó đặt `activeLow=true`. Phải khớp phần cứng thật của bạn; nối kiểu khác thì Press là RISING. Đây là lý do `GpioLineSpec` có cờ `activeLow` thay vì hardcode.
</details>

<details><summary><b>Q3.</b> Một thread chờ cả 3 nút, hay mỗi nút một thread?</summary>

> **Một thread** chờ cả 3 line bằng `gpiod_line_event_wait_bulk` (libgpiod hỗ trợ chờ nhiều line cùng lúc qua một lời gọi, nền là poll/epoll). Ít thread hơn = ít phức tạp hơn, đúng tinh thần kiến trúc (architecture.md §3). Mỗi-nút-một-thread là over-engineering ở đây.
</details>

<details><summary><b>Q4.</b> Debounce: chặn theo thời gian, hay đọc lại xác nhận mức?</summary>

> Hai cách: (a) **time-gate** — sau một cạnh, bỏ qua mọi cạnh của *cùng nút đó* trong `debounceMs`; (b) **confirm-level** — sau cạnh, chờ `debounceMs` rồi đọc lại mức, chỉ phát nếu mức vẫn ổn định. (a) đơn giản, đủ dùng cho nút cơ; (b) chắc hơn nhưng phức tạp. Bắt đầu (a), đo thực tế. Cảnh báo đánh đổi: `debounceMs` quá lớn sẽ **nuốt** cú nhấn nhanh (xem Cạm bẫy).
</details>

<details><summary><b>Q5.</b> LED chung chip với nút không? active-high?</summary>

> LED là một line **output** riêng (`gpiod_line_request_output`). Có thể khác chip với nút. Cờ `activeLow` của LED quyết định `setLed(true)` ghi 1 hay 0. Tách spec LED riêng trong `GpioPinMap`.
</details>

**⚖️ Quyết định bạn tự chốt (không có đáp án đúng tuyệt đối):** giá trị `debounceMs` (gợi ý 20–30ms, *đo* nút của bạn); cách nối nút (active-low/high); 4 chân GPIO cụ thể.

---

## 4. 🔨 Trình tự dựng (mỗi bước có "done-when")

> Triết lý: mỗi bước phải **compile + chạy được** trước khi sang bước sau. Không viết cả file rồi mới build.

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | `Types.hpp` + `IGpioHal.hpp` (chỉ interface) | compile sạch, chưa có impl |
| 2 | `MockGpioHal` + 1 test *fail* cho ButtonController (test-first) | test build & chạy (đỏ) trên VM |
| 3 | `GpiodHal::init` mở chip + request LED output + 3 nút both-edge | `init()` trả 0 trên board, không lỗi |
| 4 | `setLed` | LED bật/tắt được bằng mắt |
| 5 | `waitEvent` cho **1** nút (chưa debounce, chưa bulk) | nhấn nút → log ra đúng Press/Release |
| 6 | mở rộng bulk 3 nút + debounce | 3 nút phân biệt đúng; không double-trigger |
| 7 | (tầng app) GPIO thread gọi `waitEvent` rồi `bus.push(ButtonEvent)` | nhấn PTT → ButtonEvent lên bus |

Bước 7 thuộc app wiring ([11-main-wiring.md]) nhưng làm luôn để thấy end-to-end.

---

## 5. 🧩 Khung code tự điền

### 5.1 `common/Types.hpp` (phần GPIO) — *contract, có thể hoàn chỉnh*
```cpp
#pragma once
namespace bbb {
enum class ButtonId { Ptt, VolUp, VolDown };
enum class Edge     { Press, Release };
struct GpioEvent    { ButtonId id; Edge edge; };

struct GpioLineSpec { int chip; int offset; bool activeLow; };
struct GpioPinMap {
    GpioLineSpec ptt, volUp, volDown, led;
    int debounceMs = 25;
};
} // namespace bbb
```

### 5.2 `hal/include/IGpioHal.hpp` — *contract, hoàn chỉnh*
(Đúng như mục 2. Đây là phần duy nhất guide cho "đủ" — vì nó là hợp đồng, không phải logic.)

### 5.3 `hal/gpio/GpiodHal.hpp` — *skeleton, bạn giữ nguyên cấu trúc*
```cpp
#pragma once
#include "IGpioHal.hpp"
#include <gpiod.h>          // libgpiod v1
#include <chrono>
#include <array>

namespace bbb::hal {
class GpiodHal : public IGpioHal {
public:
    GpiodHal() = default;
    ~GpiodHal() override;                          // TODO(you): đóng line + chip
    GpiodHal(const GpiodHal&) = delete;            // không copy fd
    GpiodHal& operator=(const GpiodHal&) = delete;

    int  init(const GpioPinMap& pins) override;
    bool waitEvent(GpioEvent& out, int timeoutMs) override;
    void setLed(bool on) override;

private:
    gpiod_chip* chip_  = nullptr;     // (giả định 3 nút cùng chip; mở rộng nếu khác)
    gpiod_line* led_   = nullptr;
    std::array<gpiod_line*, 3> btn_{}; // [0]=Ptt [1]=VolUp [2]=VolDown
    GpioPinMap  pins_{};
    // debounce: mốc thời gian cạnh gần nhất mỗi nút
    std::array<std::chrono::steady_clock::time_point, 3> lastEdge_{};

    // TODO(you): helper map line→ButtonId, và Edge từ event_type + activeLow
};
} // namespace bbb::hal
```

### 5.4 `GpiodHal.cpp` — *thân hàm để bạn điền; dưới là pseudo-code*

**`init()` — thuật toán:**
```
mở chip:           chip_ = gpiod_chip_open_by_name("gpiochipN")   // N = pins.ptt.chip
                   nếu null → return -1
lấy line LED:      led_ = gpiod_chip_get_line(chip_, pins.led.offset)
request output:    gpiod_line_request_output(led_, "bbb-va", giá_trị_tắt_ban_đầu)
for mỗi nút i:     btn_[i] = gpiod_chip_get_line(chip_, offset_nút_i)
                   gpiod_line_request_both_edges_events(btn_[i], "bbb-va")
                   nếu bất kỳ bước nào lỗi → dọn dẹp phần đã mở → return -1
return 0
```
> TODO(you): xử lý lỗi từng bước (đừng để rò line nếu nút thứ 2 request fail). Nghĩ: nếu nút khác chip thì sao? (gợi ý: cho phép mở nhiều chip, hoặc khẳng định cùng chip và assert).

**`waitEvent()` — thuật toán (bulk wait + debounce time-gate):**
```
dựng gpiod_line_bulk từ btn_[0..2]
ts = timeoutMs → struct timespec
rc = gpiod_line_event_wait_bulk(&bulk, &ts, &evbulk)
nếu rc == 0  → return false        // hết timeout, không có gì
nếu rc <  0  → log, return false   // lỗi
for mỗi line trong evbulk:
    gpiod_line_event_read(line, &ev)            // ev.event_type RISING/FALLING
    i  = index_của(line)                        // → ButtonId
    now = steady_clock::now()
    nếu (now - lastEdge_[i]) < debounceMs → bỏ qua (nuốt bounce)
    lastEdge_[i] = now
    out.id   = buttonId(i)
    out.edge = edgeFrom(ev.event_type, pins activeLow)  // FALLING+activeLow → Press
    return true                                  // trả 1 event mỗi lần gọi
return false
```
> TODO(you): nếu nhiều line cùng có event trong một lần wait, bạn trả cái nào và cái còn lại có mất không? (gợi ý: hàng đợi nội bộ nhỏ, hoặc đảm bảo đọc hết — nghĩ về race này, đây là chỗ Claude sẽ soi khi review).

**`setLed()`:**
```
val = on XOR pins.led.activeLow ? ...   // map on→mức điện đúng theo activeLow
gpiod_line_set_value(led_, val)
```
> TODO(you): viết biểu thức map `on` + `activeLow` → 0/1 cho đúng. Tự kiểm bằng bảng chân trị.

**`~GpiodHal()`:** `gpiod_line_release` từng line đã request, rồi `gpiod_chip_close(chip_)`. TODO(you): an toàn với con trỏ null (init có thể fail giữa chừng).

### 5.5 `tests/hal/MockGpioHal.hpp` — *harness, gần đủ; điền 1 chỗ*
```cpp
#pragma once
#include "IGpioHal.hpp"
#include <deque>
namespace bbb::hal {
class MockGpioHal : public IGpioHal {
public:
    std::deque<GpioEvent> scripted;   // nạp sẵn kịch bản nút bấm
    bool ledState = false;

    int init(const GpioPinMap&) override { return 0; }
    void setLed(bool on) override { ledState = on; }
    bool waitEvent(GpioEvent& out, int /*timeoutMs*/) override {
        if (scripted.empty()) return false;          // hết kịch bản = timeout
        out = scripted.front();
        scripted.pop_front();
        return true;
    }
};
} // namespace bbb::hal
```

### 5.6 Khung test (Catch2/GoogleTest tuỳ bạn chọn) — *viết test, rồi code cho pass*
```cpp
TEST_CASE("ButtonController dịch PTT hold thành start/stop record") {
    MockGpioHal gpio;
    gpio.scripted = { {ButtonId::Ptt, Edge::Press},
                      {ButtonId::Ptt, Edge::Release} };
    // TODO(you): bơm 2 event qua ButtonController, assert nó phát đúng intent
    //            (start record khi Press, stop khi Release).
}
```
> Test-first: viết test này ở Bước 2 (đỏ), rồi mới hiện thực ButtonController ([09]). Logic kiểm được trên VM, không cần board.

---

## 6. ⚠️ Cạm bẫy (xem thêm [../troubleshooting.md](../troubleshooting.md) §4)

- **`Permission denied` mở gpiochip** → user chưa thuộc nhóm `gpio` (`sudo usermod -aG gpio $USER`, logout/login) hoặc chạy qua systemd thiếu quyền.
- **Line "Device or resource busy"** → chân đã bị overlay/driver khác claim. Kiểm `gpioinfo` xem line "used". Đừng chọn chân trùng SPI/DC/RESET.
- **Sai `gpiochipN`** → BBB có gpiochip0..3, mỗi chip 32 line. P9_15=gpio1_16 nghĩa là **chip 1, offset 16**, không phải chip 0. Nhầm chip = mở nhầm chân.
- **Debounce nuốt cú nhấn nhanh** → `debounceMs` quá lớn. PTT nhả-nhấn nhanh có thể mất. Đo, đừng đoán.
- **Both-edge nhưng chỉ xử lý một cạnh** → PTT cần *cả* Press và Release (giữ để thu, nhả để gửi). Quên Release = kẹt ở LISTENING tới FR-8 timeout.
- **Trộn libgpiod v1/v2** → biên dịch lỗi hàm không tồn tại. Khẳng định version trước (Q1).
- **Rò line khi init fail giữa chừng** → request nút thứ 3 fail nhưng 2 nút đầu đã giữ → leak tới lần chạy sau "busy". RAII/dọn dẹp phải đúng.

---

## 7. ✅ Checkpoint review

**"Xong" khi:**
- Nhấn từng nút (PTT/Vol+/Vol-) → `waitEvent` trả đúng `ButtonId` + `Edge`, không double-trigger.
- `setLed(true/false)` đổi trạng thái LED thấy bằng mắt.
- `MockGpioHal` + test ButtonController **pass trên VM** (không cần board).
- `init` fail (chân sai) → trả <0, không crash, không leak line (chạy lại không "busy").

**Đưa Claude review (cụ thể, không "xem hộ em"):**
- File: `GpiodHal.cpp` + GPIO thread đẩy `ButtonEvent` + test.
- Hỏi đích danh:
  1. *"Vòng đời `gpiod_chip`/`gpiod_line` của tôi có rò khi init fail giữa chừng không?"*
  2. *"Trong `waitEvent`, nếu nhiều line cùng có event một lúc, tôi có làm mất event nào không?"*
  3. *"Biểu thức map `activeLow` → Press/Release của tôi đúng cho cả hai kiểu nối chưa?"*

> Ba câu trên chính là các `TODO(you)` khó nhất ở mục 5 — đó là nơi review tạo giá trị, không phải ở phần boilerplate.
