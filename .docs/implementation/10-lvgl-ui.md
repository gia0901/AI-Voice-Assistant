# 10 — LvglUi

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.*
>
> Nền: [../development/app_layer.md](../development/app_layer.md) §5 (luật main-thread) · [03-display-hal.md](03-display-hal.md)

---

## 1. 🎯 Mục tiêu & vị trí

Dựng UI LVGL: khởi tạo LVGL trỏ flush vào `IDisplayHal`, hiển thị trạng thái + câu trả lời + lỗi. **Mọi lời gọi `lv_*` chỉ trên main thread.**

**File:** `app/LvglUi.hpp/.cpp`.
**Phụ thuộc:** LVGL (config 320×240 RGB565), `IDisplayHal` (03). Gọi bởi FSM (08) trên main thread.

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

> Qua `flush_cb`: LVGL gọi callback với vùng dirty + buffer pixel → bạn chuyển sang `IDisplayHal::flush(x1,y1,x2,y2,px)` rồi `lv_display_flush_ready()`. LVGL **không** biết fbtft. (Khớp guide 03.)
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
    explicit LvglUi(hal::IDisplayHal* d) : disp_(d) {}
    void init();
    void showState(State s);
    void showText(const std::string& t);
    void showError(const std::string& e);
    void tick() { lv_timer_handler(); }
private:
    hal::IDisplayHal* disp_;
    lv_obj_t* stateLabel_ = nullptr;
    lv_obj_t* textLabel_  = nullptr;
    // draw buffers
};
} // namespace bbb
```

### 5.2 Thuật toán `init` (LVGL v9 ý tưởng — API tùy phiên bản bạn dùng)
```
lv_init()
tạo display 320x240
cấp draw buffer (partial) → lv_display_set_buffers(...)
lv_display_set_flush_cb(disp, &LvglUi::flushTrampoline)   // trampoline → flush()
dựng widget: stateLabel_, textLabel_ trên màn active
```
Trong `flush_cb`:
```
flush_cb(disp, area, px_map):
    disp_->flush(area->x1, area->y1, area->x2, area->y2, (uint16_t*)px_map)
    lv_display_flush_ready(disp)
```
> TODO(you): LVGL là C, callback không nhận `this` trực tiếp → dùng trampoline/`lv_display_set_user_data`. Xác nhận **layout `px_map`** (theo vùng) khớp cách `IDisplayHal::flush` đọc `px` (guide 03) — hai chỗ phải đồng ý nhau, **Claude soi chỗ này**.

### 5.3 `showState` / `showError`
```
showState(s): lv_label_set_text(stateLabel_, toString(s))   // "IDLE", "LISTENING"...
showError(e): lv_label_set_text(stateLabel_, ("ERR: "+e).c_str()); đổi màu đỏ
showText(t):  lv_label_set_text(textLabel_, t.c_str())      // wrap
```
> TODO(you): `toString(State)` map enum→chuỗi hiển thị. Màu/độ nổi cho ERROR.

---

## 6. ⚠️ Cạm bẫy ([troubleshooting.md §2](../troubleshooting.md))

- **Gọi `lv_*` từ thread khác** → crash ngẫu nhiên. Bất biến #1, enforce review.
- **Quên `lv_display_flush_ready`** → LVGL treo chờ flush xong.
- **Layout `px_map` ≠ cách flush đọc** → ảnh lệch/nhiễu. Đồng bộ với guide 03.
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
