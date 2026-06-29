# 10 — LvglUi

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.*
>
> **Phong cách mục 5:** mỗi hàm theo 3 lớp — **Bản chất** → **API toolbox** (tra cứu cục bộ) → **Pseudo** (chừa quyết định khó ở `TODO(you)`).
>
> Nền: [../development/app_layer.md](../development/app_layer.md) §5 (luật main-thread) · [03-display-hal.md](03-display-hal.md)

---

## 1. 🎯 Mục tiêu & vị trí

Dựng UI LVGL: khởi tạo LVGL trỏ flush vào `IDisplayHal`, hiển thị trạng thái + câu trả lời + lỗi. **Mọi lời gọi `lv_*` chỉ trên main thread.**

**File:** `app/LvglUi.hpp/.cpp`.
**Phụ thuộc:** LVGL (config 320×240 RGB565), `IDisplayHal` ([03](03-display-hal.md)). Gọi bởi FSM ([08](08-state-machine.md)) trên main thread.

---

## 2. 📜 Hợp đồng

```cpp
class LvglUi {
public:
    explicit LvglUi(IDisplayHal* disp);
    void init();                          // lv_init + draw buffer + flush_cb + dựng widget
    void showState(State s);              // nhãn trạng thái
    void showText(const std::string&);    // câu trả lời
    void showError(const std::string&);   // thông báo lỗi
    void tick();                          // = lv_timer_handler(), gọi mỗi vòng main loop
};
```
**Ràng buộc bất biến #1:** chỉ main thread gọi các hàm này (architecture.md §3). Refresh **theo sự kiện đổi state**, không animation liên tục (NFR/Risk — BBB single-core).

---

## 3. 🤔 Quyết định trước khi code

<details><summary><b>Q1.</b> Làm sao đảm bảo LVGL chỉ chạy main thread — bằng design?</summary>

> Thread khác **không giữ tham chiếu** `LvglUi`. Chúng push event; main thread nhận → FSM gọi `ui.showX()`. Không có đường gọi sai. ([app_layer.md §5](../development/app_layer.md))
</details>

<details><summary><b>Q2.</b> Nối LVGL với phần cứng qua đâu?</summary>

> Qua `flush_cb`: LVGL gọi callback với vùng dirty + buffer pixel → bạn chuyển sang `IDisplayHal::flush(x1,y1,x2,y2,px)` rồi `lv_display_flush_ready()`. LVGL **không** biết fbtft. (Khớp [03](03-display-hal.md).)
</details>

<details><summary><b>Q3.</b> Draw buffer to bao nhiêu?</summary>

> Một phần màn (vd 1/10 = 320×24) là đủ cho partial render, tiết kiệm RAM (NFR-2). Toàn màn 320×240×2 = 150KB cũng nhỏ. Đánh đổi RAM vs số lần flush. ([app_layer.md](../development/app_layer.md))
</details>

<details><summary><b>Q4.</b> Câu trả lời dài hiển thị sao trên 320×240?</summary>

> `lv_label` với `LV_LABEL_LONG_WRAP` + có thể cuộn, hoặc cắt bớt. UX thật sự — *bạn quyết*. Đừng để tràn ra ngoài màn.
</details>

**⚖️ Bạn tự chốt:** layout (nhãn state ở đâu, vùng text ở đâu), font size, draw buffer size, cách xử lý text dài.

---

## 4. 🔨 Trình tự dựng

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | `init`: lv_init + draw buf + flush_cb → DisplayHal | màn LVGL sạch (nền) hiện ra |
| 2 | nhãn state, `showState` đổi text | đổi state thấy chữ đổi |
| 3 | vùng text `showText` (wrap) | câu trả lời hiện đủ |
| 4 | `showError` (màu/icon khác) | lỗi nổi bật, phân biệt state thường |
| 5 | `tick()` trong main loop nhịp đều | UI mượt, không đứng |

---

## 5. 🧩 Khung code tự điền

### 5.1 `LvglUi.hpp` — *skeleton*
```cpp
#pragma once
#include "IDisplayHal.hpp"
#include "Types.hpp"
#include "lvgl.h"
#include <string>
namespace bbb {
class LvglUi {
public:
    explicit LvglUi(IDisplayHal* d) : disp_(d) {}
    void init();
    void showState(State s);
    void showText(const std::string& t);
    void showError(const std::string& e);
    void tick() { lv_timer_handler(); }
private:
    IDisplayHal* disp_;
    lv_obj_t* stateLabel_ = nullptr;
    lv_obj_t* textLabel_  = nullptr;
    // draw buffers
};
} // namespace bbb
```

---

### 5.2 `init()` + `flush_cb` — nối LVGL vào phần cứng

**Bản chất.** LVGL là **engine vẽ độc lập phần cứng**: nó dựng pixel vào draw buffer rồi *gọi ngược* `flush_cb` của bạn để "đẩy vùng này lên màn". Toàn bộ việc nối hạ tầng gói trong một callback — đổi từ fbdev sang DRM sau này chỉ là đổi thân `flush()` ([03](03-display-hal.md)), LVGL không hay biết. Hai điểm bắt buộc đúng: (1) **báo xong** — quên `lv_display_flush_ready` thì LVGL đứng chờ mãi; (2) **bắc cầu C↔C++** — LVGL là C, callback không mang `this`, nên cần trampoline (qua `user_data`). Và **layout `px_map`** mà LVGL đưa phải khớp đúng cách `IDisplayHal::flush` đọc — đây là điểm hai bên phải đồng thuận.

**API toolbox** (LVGL v9 — xác nhận version bạn dùng):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `lv_init()` | Khởi tạo thư viện | gọi một lần |
| `lv_display_create(w, h)` | Tạo đối tượng display 320×240 | |
| `lv_display_set_buffers(disp, b1, b2, size, MODE_PARTIAL)` | Gắn draw buffer | partial = tiết kiệm RAM (Q3) |
| `lv_display_set_flush_cb(disp, cb)` | Đăng ký callback đẩy pixel | cb là hàm C → trampoline |
| `lv_display_set_user_data` / `lv_display_get_user_data` | Mang `this` qua biên C | dùng để trampoline gọi lại method |
| `lv_display_flush_ready(disp)` | Báo LVGL "đã flush xong" | **quên = treo** |

**Pseudo:**
```
init():  lv_init(); d = lv_display_create(320,240)
         lv_display_set_buffers(d, buf1, buf2, size, PARTIAL)
         lv_display_set_user_data(d, this)
         lv_display_set_flush_cb(d, &LvglUi::flushTrampoline)
         dựng stateLabel_, textLabel_

flushTrampoline(d, area, px_map):    // static
         self = (LvglUi*) lv_display_get_user_data(d)
         self->disp_->flush(area->x1, area->y1, area->x2, area->y2, (uint16_t*)px_map)
         lv_display_flush_ready(d)
```
> **TODO(you):** xác nhận **layout `px_map`** (theo vùng dirty) khớp cách `IDisplayHal::flush` đọc `px` ([03](03-display-hal.md)) — hai chỗ phải đồng ý nhau. **Claude soi chỗ này.**

---

### 5.3 `showState()` / `showText()` / `showError()`

**Bản chất.** Đây chỉ là **cập nhật nội dung widget** — rẻ, nhưng phải gọi từ main thread (bất biến #1). Refresh **do sự kiện đổi state**, không vòng lặp animation (BBB single-core, tranh CPU với audio). `showError` cần *nổi bật* khác state thường (màu/đỏ) để người dùng phân biệt ngay.

**API toolbox** (LVGL):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `lv_label_set_text(label, str)` | Đổi text widget | copy chuỗi vào trong; `c_str()` ok |
| `lv_obj_set_style_text_color(obj, color, sel)` | Đổi màu (cho ERROR) | dùng cờ phân biệt lỗi/bình thường |

**Pseudo:**
```
showState(s): lv_label_set_text(stateLabel_, toString(s))         // "IDLE","LISTENING"...
showText(t):  lv_label_set_text(textLabel_, t.c_str())            // wrap
showError(e): lv_label_set_text(stateLabel_, ("ERR: "+e).c_str()); set màu đỏ
```
> **TODO(you):** `toString(State)` map enum→chuỗi; chọn cách làm nổi ERROR; xử lý text dài (Q4).

---

## 6. ⚠️ Cạm bẫy ([troubleshooting.md §2](../troubleshooting.md))

- **Gọi `lv_*` từ thread khác** → crash ngẫu nhiên. Bất biến #1, enforce review.
- **Quên `lv_display_flush_ready`** → LVGL treo chờ flush xong.
- **Layout `px_map` ≠ cách flush đọc** → ảnh lệch/nhiễu. Đồng bộ với [03](03-display-hal.md).
- **Animation liên tục** → ngốn CPU single-core, tranh với audio. Refresh theo state.
- **`tick()` gọi thưa** → UI giật. Gọi mỗi vòng main loop (~10ms).
- **API LVGL v8 vs v9 khác** → xác nhận version đang dùng.

---

## 7. ✅ Checkpoint review

**Xong khi:** màn hiện đúng INIT→IDLE→LISTENING→PROCESSING→SPEAKING→IDLE khi FSM đổi state; câu trả lời + lỗi hiển thị rõ; UI mượt; không gọi `lv_*` ngoài main thread.

**Đưa Claude review:** `LvglUi.cpp` (đặc biệt `flush_cb` ↔ DisplayHal). Hỏi:
1. *"`px_map` của LVGL khớp cách `IDisplayHal::flush` đọc buffer chưa?"*
2. *"Có chỗ nào `lv_*` có thể bị gọi ngoài main thread không?"*
3. *"Draw buffer + cadence `tick` của tôi có hợp lý cho BBB single-core không?"*
