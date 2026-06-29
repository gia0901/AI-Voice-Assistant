# 01 — GpioHal (libgpiod v1)

> Implementation guide (tiếng Việt, Rule §18). **Exemplar** cho template 7 mục — xem cách dùng ở [README.md](README.md). Nguyên tắc: *giàn giáo + gợi ý, không phải lời giải*. Bạn điền thân hàm; Claude review.
>
> **Phong cách mục 5 (mẫu cho cả 02–11):** mỗi hàm trình bày 3 lớp —
> 1. **Bản chất** — bài toán & hướng giải, không phụ thuộc framework (đây là cái học được, chuyển được sang component khác).
> 2. **API toolbox** — bảng tra cứu *cục bộ ngay tại hàm* (chỉ các API hàm đó dùng), để khỏi mất thời gian nghiên cứu từng API.
> 3. **Pseudo** — ghép 1+2 lại, *chừa quyết định khó ở* `TODO(you)`.
>
> Tinh thần: đưa sẵn **từ vựng** (API), giữ lại **ngữ pháp & bài toán** (tư duy).
>
> Nền: [../development/hal_layer.md](../development/hal_layer.md) · [../knowledge/strategy_roadmap.md](../knowledge/strategy_roadmap.md) (Phase A)

---

## 1. 🎯 Mục tiêu & vị trí

Dựng HAL điều khiển **3 nút bấm** (PTT, Vol+, Vol−) và **1 LED RGB trạng thái** (module HW-479), qua **libgpiod** (chardev, Decision #8).

**File sẽ tạo:**
```
hal/include/IGpioHal.hpp     # interface (contract)
hal/gpio/GpioHal.hpp/.cpp    # impl thật trên BBB (class bbb::GpioHal)
tests/hal/MockGpioHal.hpp    # impl giả để test trên VM
```

**Phụ thuộc phải có trước:** `common/Types.hpp` (định nghĩa `ButtonId`, `Edge`, `GpioEvent`, `GpioPinMap`). Không phụ thuộc EventBus — HAL chỉ *trả* sự kiện; việc đẩy `ButtonEvent` lên bus là của GPIO thread ở tầng app ([09-button-controller.md](09-button-controller.md) / `main`).

**Bản đồ chân (single source of truth: [hardware_setup.md §3.2 + §7](../hardware_setup.md), đã mux trong [overlay](../../kernel/overlays/BBB-VOICE-ASSISTANT.dts)):**

| Net | BBB pin | gpiochip, line | Cực tính |
|-----|---------|----------------|----------|
| PTT  | P8_09 | **chip2, line 5**  | active-low (idle High, nhấn Low) |
| Vol+ | P8_10 | **chip2, line 4**  | active-low |
| Vol− | P8_11 | **chip1, line 13** | active-low |
| LED R | P8_14 | **chip0, line 26** | active-high (High = sáng) |
| LED G | P8_15 | **chip1, line 15** | active-high |
| LED B | P8_16 | **chip1, line 14** | active-high |

Nạp map này vào app qua `GpioPinMap` trong `config.json`. Overlay chỉ *mux* các chân (MODE7 + pull), **không** khai `gpio-keys`/`gpio-leds` → libgpiod ở userspace sở hữu line.

> **Hai đặc điểm phần cứng định hình toàn bộ thiết kế HAL này — nắm trước khi đọc tiếp:**
> 1. **Line trải trên nhiều gpiochip.** Nút: PTT/Vol+ ở chip2, Vol− ở chip1. LED: R ở chip0, G/B ở chip1. ⇒ không tồn tại "một con trỏ chip duy nhất".
> 2. **LED là RGB**: 3 line output, **active-high**, dùng để hiển thị **4 màu trạng thái** (§7 / [hardware_setup.md §7](../hardware_setup.md)) — khác hẳn 1 LED on/off, và ngược cực tính với nút.

---

## 2. 📜 Hợp đồng (contract)

Trích [../architecture.md](../architecture.md) §2.1 — `init`/`waitEvent` cố định, **không đổi chữ ký tùy tiện**:

```cpp
enum class ButtonId { Ptt, VolUp, VolDown };
enum class Edge     { Press, Release };
struct GpioEvent    { ButtonId id; Edge edge; };

class IGpioHal {
public:
    virtual ~IGpioHal() = default;
    virtual int  init(const GpioPinMap& pins)            = 0; // 0 = OK, <0 = lỗi
    virtual bool waitEvent(GpioEvent& out, int timeoutMs) = 0; // true nếu có event
    // setLed: API cho LED RGB do BẠN chốt (Q5) — đây là quyết định thiết kế, không bê máy móc.
    virtual void setLed(/* ? */)                          = 0;
};
```

**Ràng buộc:**
- `waitEvent` **blocking** với timeout — gọi từ *một* GPIO thread riêng (architecture.md §3). Trả `false` khi hết timeout mà không có sự kiện (để thread kiểm cờ dừng rồi chờ lại).
- **Không ném exception** qua biên — lỗi trả mã (`int`/`bool`), tầng trên gói thành event ([coding_guide.md §5](../development/coding_guide.md)).
- **Debounce nằm ở đây** (HAL), không ở ButtonController ([hal_layer.md §6](../development/hal_layer.md)).
- RAII: line/chip nhả trong destructor, cấm copy ([coding_guide.md §3](../development/coding_guide.md)).
- LED là RGB → API điều khiển LED là phần hở duy nhất trong contract; bạn chốt ở Q5 trước khi code mục 5.4 phần `setLed`.

---

## 3. 🤔 Quyết định trước khi code (tự trả lời trước)

> Kỷ luật: viết câu trả lời của bạn ra giấy **trước**, rồi mới mở `<details>` đối chiếu.

<details><summary><b>Q1.</b> libgpiod v1 hay v2? API khác nhau hoàn toàn.</summary>

> BBB Debian 12 (bookworm) ship **libgpiod 1.6.x → API v1**. Kiểm: `gpiodetect --version` hoặc `dpkg -l libgpiod2`. Guide này dùng v1 (`gpiod_line_request_*`, `gpiod_line_event_*`). Nếu lên v2 (Debian 13+) thì API là `gpiod_line_request` + `gpiod_line_request_wait_edge_events` — *đừng trộn hai bản*, biên dịch sẽ báo hàm không tồn tại.
</details>

<details><summary><b>Q2.</b> Nút active-low hay active-high? "Press" là cạnh nào?</summary>

> **Nút: active-low** — nối xuống GND + pull-up nội (overlay mux `0x37` = INPUT_PULLUP) → hở = High, nhấn = Low → **Press = FALLING edge**, đặt `activeLow=true`. **LED: active-high** → ghi High để sáng, `activeLow=false`. Hai thứ ngược cực tính nhau; đó là lý do `GpioLineSpec` mang cờ `activeLow` riêng cho từng line thay vì hardcode.
</details>

<details><summary><b>Q3.</b> Một thread chờ cả 3 nút, hay mỗi nút một thread? Nút khác chip thì chờ thế nào?</summary>

> **Một thread** cho cả 3 nút — đúng tinh thần kiến trúc (architecture.md §3); mỗi-nút-một-thread là over-engineering. Mấu chốt: `gpiod_line_event_wait_bulk` (v1) chỉ chờ được các line **trong cùng một chip**. Nút trải 2 chip (chip2: PTT/Vol+, chip1: Vol−) ⇒ **không gộp 3 nút vào một bulk**. Hai hướng giải:
> - **(a) `poll()` trên fd của từng line** — `gpiod_line_event_get_fd` cho mỗi nút, gộp vào một mảng `pollfd`, một lời gọi `poll` chờ được cả nhiều chip. Sạch cho đa chip.
> - **(b) gom theo chip** — bulk cho chip2 (PTT+Vol+) + xử lý riêng Vol− trên chip1. Lằng nhằng hơn.
>
> Hướng (a) gọn hơn và là lý do toolbox `waitEvent` xoay quanh `event_get_fd` + `poll`. Cảnh giác các ví dụ "bulk 3 line" trên mạng — chúng ngầm giả định cùng chip.
</details>

<details><summary><b>Q4.</b> Debounce: chặn theo thời gian, hay đọc lại xác nhận mức?</summary>

> (a) **time-gate** — sau một cạnh, bỏ qua mọi cạnh của *cùng nút* trong `debounceMs`. (b) **confirm-level** — sau cạnh, chờ `debounceMs` rồi đọc lại mức, chỉ phát nếu mức ổn định. (a) đơn giản, đủ cho nút cơ; bắt đầu (a) rồi *đo* thực tế. Đánh đổi: `debounceMs` quá lớn sẽ **nuốt** cú nhấn nhanh (xem Cạm bẫy).
</details>

<details><summary><b>Q5.</b> LED RGB — API điều khiển nên có hình dạng gì?</summary>

> LED có **3 line** (R/G/B) hiển thị **4 màu trạng thái** (IDLE=green, LISTENING=blue, PROCESSING=yellow=R+G, ERROR=red). Một hàm bật/tắt *một* bool không biểu diễn nổi 4 màu. Bạn quyết:
> - **Chữ ký:** `setColor(bool r, bool g, bool b)` (HAL chỉ biết bật từng line) hay `setLed(State)` (HAL biết cả map State→màu)? **Gợi ý:** giữ HAL "ngu" — chỉ bật/tắt R/G/B; còn *State→màu* là **chính sách** của tầng app (StateMachine). Lý do: HAL dễ test, dễ thay LED, không phụ thuộc enum `State` của app.
> - **`GpioPinMap`:** thay một `GpioLineSpec led` bằng `ledR/ledG/ledB` (hoặc `std::array<GpioLineSpec,3>`).
> - **Đa chip:** R ở chip0, G/B ở chip1 — cùng vấn đề đa chip như Q3, áp cho cả output.
</details>

**⚖️ Bạn tự chốt (không có đáp án tuyệt đối):** `debounceMs` (gợi ý 20–30ms, *đo* nút của bạn); **hình dạng API LED (Q5)**; **chiến lược chờ đa chip (Q3)**.

---

## 4. 🔨 Trình tự dựng (mỗi bước có "done-when")

> Triết lý: mỗi bước phải **compile + chạy được** trước khi sang bước sau. Không viết cả file rồi mới build.

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | `Types.hpp` (phần GPIO) + `IGpioHal.hpp` (chỉ interface) | compile sạch, chưa có impl |
| 2 | `MockGpioHal` + 1 test *fail* cho ButtonController (test-first) | test build & chạy (đỏ) trên VM |
| 3 | `init` mở **các** chip + request 3 nút both-edge + 3 line LED output | `init()` trả 0 trên board, không lỗi |
| 4 | `setLed`/`setColor` | LED đổi đủ 4 màu thấy bằng mắt |
| 5 | `waitEvent` cho **1** nút (chưa debounce) | nhấn nút → log đúng Press/Release |
| 6 | mở rộng `poll` 3 nút (2 chip) + debounce | 3 nút phân biệt đúng; không double-trigger |
| 7 | (tầng app) GPIO thread gọi `waitEvent` rồi `bus.push(ButtonEvent)` | nhấn PTT → ButtonEvent lên bus |

Bước 7 thuộc app wiring ([11-main-wiring.md](11-main-wiring.md)) nhưng làm luôn để thấy end-to-end.

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
    GpioLineSpec ptt, volUp, volDown;          // chip của 3 nút KHÔNG giống nhau (2,2,1)
    GpioLineSpec ledR, ledG, ledB;             // hoặc std::array<GpioLineSpec,3> — chốt theo Q5
    int debounceMs = 25;
};
} // namespace bbb
```

### 5.2 `hal/include/IGpioHal.hpp` — *contract, hoàn chỉnh*
Đúng như mục 2 (riêng API LED điền theo Q5). Đây là phần duy nhất guide cho "đủ" — vì nó là hợp đồng, không phải logic.

### 5.3 `hal/gpio/GpioHal.hpp` — *skeleton, giữ nguyên cấu trúc*
```cpp
#pragma once
#include "IGpioHal.hpp"
#include <gpiod.h>          // libgpiod v1
#include <chrono>
#include <array>
#include <map>

namespace bbb {
class GpioHal : public IGpioHal {
public:
    GpioHal() = default;
    ~GpioHal() override;                          // TODO(you): nhả line + đóng MỌI chip đã mở
    GpioHal(const GpioHal&)            = delete;   // không copy fd
    GpioHal& operator=(const GpioHal&) = delete;

    int  init(const GpioPinMap& pins) override;
    bool waitEvent(GpioEvent& out, int timeoutMs) override;
    // setLed/setColor: theo Q5

private:
    // Line trải nhiều chip → giữ tập hợp chip, mở mỗi chip đúng 1 lần.
    // TODO(you): chọn cấu trúc, ví dụ map số-chip → con trỏ chip:
    std::map<int, gpiod_chip*> chips_;
    std::array<gpiod_line*, 3> btn_{};            // [0]=Ptt [1]=VolUp [2]=VolDown
    std::array<gpiod_line*, 3> led_{};            // [0]=R [1]=G [2]=B
    GpioPinMap pins_{};
    // debounce: mốc thời gian cạnh gần nhất mỗi nút
    std::array<std::chrono::steady_clock::time_point, 3> lastEdge_{};

    // TODO(you): helper openChip(n) (mở-hoặc-lấy-từ-cache); map line→ButtonId; Edge từ event_type + activeLow
};
} // namespace bbb
```

### 5.4 `hal/gpio/GpioHal.cpp` — *3 lớp mỗi hàm; thân để bạn điền*

---

#### `init()`

**Bản chất.** Đây là một **transaction giành tài nguyên kernel**, giống mở nhiều file/socket cùng lúc: nhiều bước *acquire*, và **bất kỳ bước nào fail thì phần đã acquire phải được rollback** — nếu không, line bị giữ sẽ báo "busy" ở lần chạy sau. Ràng buộc riêng của board: tài nguyên **trải nhiều chip** (nút chip1+chip2, LED chip0+chip1) ⇒ cần quản lý *tập hợp* chip, mở mỗi chip đúng một lần. Tư duy "acquire có rollback" mới là cái chuyển được sang ALSA/curl sau này — tên hàm chỉ là phương tiện.

**API toolbox** (libgpiod v1):

| Hàm | Công dụng | Gotcha |
|-----|-----------|--------|
| `gpiod_chip_open_by_name("gpiochipN")` | Mở 1 gpiochip | `NULL` nếu lỗi; mỗi chip mở đúng 1 lần (cache lại) |
| `gpiod_chip_get_line(chip, offset)` | Lấy handle 1 line | Chưa "claim" — chưa dùng được, chip vẫn là chủ |
| `gpiod_line_request_both_edges_events(line,"tên")` | Claim line input + đăng ký **cả 2 cạnh** | `EBUSY` nếu line bị driver/overlay khác giữ |
| `gpiod_line_request_output(line,"tên",val0)` | Claim line output + mức khởi tạo | `val0` là mức *điện* (0/1), chưa qua `activeLow` |

**Pseudo:**
```
helper openChip(n):  nếu n đã trong chips_ → trả lại; else open_by_name("gpiochip{n}"); cache; NULL → fail
nút i (0..2):  chip = openChip(pins.<nút_i>.chip);  btn_[i] = get_line(chip, offset_i)
               request_both_edges_events(btn_[i], "bbb-va")
LED k (R,G,B): chip = openChip(spec.chip);          led_[k] = get_line(chip, offset_k)
               request_output(led_[k], "bbb-va", mức_tắt)   // active-high → tắt = 0
bất kỳ bước lỗi → rollback phần đã acquire → return -1
return 0
```
> **TODO(you):** hiện thực `openChip` (mở-hoặc-cache) và **rollback** khi một bước fail (đừng leak line/chip). Đây là chỗ review tạo giá trị, không phải việc "gọi đúng hàm".

---

#### `waitEvent()`

**Bản chất.** Bài toán kinh điển: **chờ trên nhiều nguồn sự kiện bất đồng bộ, có timeout** — đúng pattern `select`/`poll`/`epoll`. Vì 3 nút trải 2 chip, công cụ "chờ nhiều line" của framework (`*_wait_bulk`) **không bắc cầu qua chip** (Q3), nên tụt xuống primitive thấp hơn: lấy fd từng line rồi `poll`. Hai cạm bẫy *bản chất* (không phải về API): (1) **drain** — nhiều fd cùng sẵn sàng mà chỉ xử lý 1 rồi return thì các sự kiện kia đi đâu? (2) **debounce** là lọc *nhiễu cơ học*, không phải logic nghiệp vụ — đừng để nó nuốt cú nhấn thật.

**API toolbox** (libgpiod v1 + POSIX):

| Hàm | Công dụng | Gotcha |
|-----|-----------|--------|
| `gpiod_line_event_get_fd(line)` | Lấy fd của line để đưa vào `poll` | **Chìa khoá đa chip**: gộp fd của nhiều chip vào một `poll` |
| `poll(fds, n, timeoutMs)` | Chờ tới khi ≥1 fd có sự kiện hoặc hết timeout | trả 0 = timeout, <0 = lỗi; kiểm `revents & POLLIN` từng fd |
| `gpiod_line_event_read(line, &ev)` | Đọc 1 sự kiện pending (`ev.event_type` RISING/FALLING) | **Không đọc → fd vẫn readable → `poll` quay lại ngay** (busy-loop) |

**Pseudo:**
```
fds[i].fd = gpiod_line_event_get_fd(btn_[i]); fds[i].events = POLLIN   // gộp 2 chip trong 1 poll
rc = poll(fds, 3, timeoutMs)
rc == 0 → return false (timeout);  rc < 0 → log, return false
for mỗi i có fds[i].revents & POLLIN:
    gpiod_line_event_read(btn_[i], &ev)
    if (now - lastEdge_[i]) < debounceMs → bỏ (nuốt bounce); else lastEdge_[i] = now
    out = { buttonId(i), edgeFrom(ev.event_type, pins.<i>.activeLow) }   // FALLING + activeLow → Press
    return true
return false
```
> **TODO(you):** trả 1 event mỗi lần gọi — nếu nhiều fd cùng `POLLIN`, cái còn lại **có mất không**? (gợi ý: hàng đợi nội bộ nhỏ, hoặc drain hết `POLLIN` trước khi return). Đây là câu #2 đưa Claude review.

---

#### `setLed()` / `setColor()`

**Bản chất.** **Tách chính sách khỏi cơ chế.** HAL chỉ nên biết *cơ chế* — "bật/tắt từng line R/G/B"; còn *chính sách* "State nào → màu nào" thuộc tầng app (StateMachine). Giữ HAL "ngu" ⇒ dễ test (mock không cần biết màu) và dễ thay LED. Đây là lý do Q5 nghiêng về `setColor(r,g,b)` thay vì `setLed(State)`.

**API toolbox** (libgpiod v1):

| Hàm | Công dụng | Gotcha |
|-----|-----------|--------|
| `gpiod_line_set_value(line, val)` | Ghi mức output cho 1 line | `val` là mức *điện*; phải tự map qua `activeLow` (LED active-high → sáng = 1) |

**Pseudo:**
```
// gọi cho từng line R/G/B với on tương ứng (yellow = R on, G on, B off — xem §7)
val = on XOR spec.activeLow            // active-high → on=true ⇒ val=1
gpiod_line_set_value(led_[k], val ? 1 : 0)
```
> **TODO(you):** chốt chữ ký (Q5); viết biểu thức `on`+`activeLow`→0/1 (tự kiểm bằng bảng chân trị); để map **State→(R,G,B) ở tầng app**, không nhồi vào HAL.

---

#### `~GpioHal()`

**Bản chất.** RAII — **đối xứng với `init`**. Nhả mọi thứ đã acquire, an toàn với con trỏ `NULL` (init có thể fail giữa chừng nên không phải line/chip nào cũng tồn tại). Vì đa chip: đóng **mọi** chip trong cache.

**API toolbox** (libgpiod v1):

| Hàm | Công dụng | Gotcha |
|-----|-----------|--------|
| `gpiod_line_release(line)` | Nhả 1 line đã request | guard `NULL`; nhả line **trước** khi đóng chip |
| `gpiod_chip_close(chip)` | Đóng chip | tự nhả mọi line còn lại của chip đó |

**Pseudo:** `gpiod_line_release` từng `btn_`/`led_` đã request → `gpiod_chip_close` mỗi chip trong `chips_`.
> **TODO(you):** guard `NULL` từng line/chip; giữ thứ tự line trước, chip sau.

---

### 5.5 `tests/hal/MockGpioHal.hpp` — *harness, gần đủ; điền 1 chỗ*
```cpp
#pragma once
#include "IGpioHal.hpp"
#include <deque>
#include <array>
namespace bbb {
class MockGpioHal : public IGpioHal {
public:
    std::deque<GpioEvent> scripted;     // nạp sẵn kịch bản nút bấm
    std::array<bool,3> rgb{};           // [R,G,B] để assert màu trong test

    int init(const GpioPinMap&) override { return 0; }
    // setColor/setLed: ghi vào rgb để test kiểm được màu (theo Q5)
    bool waitEvent(GpioEvent& out, int /*timeoutMs*/) override {
        if (scripted.empty()) return false;     // hết kịch bản = timeout
        out = scripted.front();
        scripted.pop_front();
        return true;
    }
};
} // namespace bbb
```

### 5.6 Khung test (GoogleTest/Catch2 tuỳ bạn) — *viết test, rồi code cho pass*
```cpp
TEST_CASE("ButtonController dịch PTT hold thành start/stop record") {
    MockGpioHal gpio;
    gpio.scripted = { {ButtonId::Ptt, Edge::Press},
                      {ButtonId::Ptt, Edge::Release} };
    // TODO(you): bơm 2 event qua ButtonController, assert nó phát đúng intent
    //            (start record khi Press, stop khi Release).
}
```
> Test-first: viết test này ở Bước 2 (đỏ), rồi mới hiện thực ButtonController ([09](09-button-controller.md)). Logic kiểm được trên VM, không cần board.

---

## 6. ⚠️ Cạm bẫy (xem thêm [../troubleshooting.md](../troubleshooting.md) §4)

- **`Permission denied` mở gpiochip** → user chưa thuộc nhóm `gpio` (`sudo usermod -aG gpio $USER`, logout/login) hoặc chạy qua systemd thiếu quyền.
- **Line "Device or resource busy"** → line bị driver/overlay khác claim. Kiểm `gpioinfo` xem line "used". Overlay chỉ *mux* (MODE7), **không** khai `gpio-keys`/`gpio-leds` — lỡ thêm thì kernel chiếm line, libgpiod báo busy.
- **Coi 3 nút như cùng chip** → SAI: PTT/Vol+ ở **chip2** (line 5, 4), Vol− ở **chip1** (line 13); LED còn trải chip0+chip1. `*_wait_bulk` không gộp 2 chip → dùng `poll` nhiều fd (Q3).
- **Sai `gpiochipN`** → BBB có gpiochip0..3, mỗi chip 32 line. `gpioN[M]` = **chip N, line M** (vd Vol− P8_11=gpio1[13] → chip1, line13). Nhầm chip = mở nhầm chân.
- **Quên `event_read` sau `poll`** → fd vẫn readable → `poll` trả ngay lập tức liên tục → busy-loop ngốn CPU.
- **Debounce nuốt cú nhấn nhanh** → `debounceMs` quá lớn. PTT nhả-nhấn nhanh có thể mất. Đo, đừng đoán.
- **Both-edge nhưng chỉ xử lý một cạnh** → PTT cần *cả* Press và Release (giữ để thu, nhả để gửi). Quên Release = kẹt ở LISTENING tới FR-8 timeout.
- **Trộn libgpiod v1/v2** → biên dịch lỗi hàm không tồn tại. Khẳng định version trước (Q1).
- **Rò line khi init fail giữa chừng** → nút thứ 3 request fail nhưng 2 nút đầu đã giữ → leak, lần sau "busy". Rollback/RAII phải đúng.

---

## 7. ✅ Checkpoint review

**"Xong" khi:**
- Nhấn từng nút (PTT/Vol+/Vol−) → `waitEvent` trả đúng `ButtonId` + `Edge`, không double-trigger (nhớ Vol− khác chip).
- LED RGB hiện đúng 4 màu: IDLE green, LISTENING blue, PROCESSING yellow (R+G), ERROR red.
- `MockGpioHal` + test ButtonController **pass trên VM** (không cần board).
- `init` fail (chân sai) → trả <0, không crash, không leak line/chip (chạy lại không "busy").

**Đưa Claude review (cụ thể, không "xem hộ em"):**
- File: `GpioHal.cpp` + GPIO thread đẩy `ButtonEvent` + test.
- Hỏi đích danh:
  1. *"Vòng đời `gpiod_chip`/`gpiod_line` đa chip của tôi có rò khi init fail giữa chừng không (mở 2–3 chip, nhả/đóng đủ chưa)?"*
  2. *"Trong `waitEvent`, tôi `poll` nhiều fd nhiều chip — nếu nhiều line cùng có event một lúc, tôi có làm mất event nào không?"*
  3. *"API LED RGB tôi chốt (Q5) có giữ HAL 'ngu' (State→màu ở app) không, hay logic chính sách lọt vào HAL?"*

> Ba câu trên chính là các `TODO(you)` khó nhất ở mục 5 — đó là nơi review tạo giá trị, không phải ở boilerplate.
