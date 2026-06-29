# 03 — DisplayHal (fbdev)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.*
>
> **Phong cách mục 5:** mỗi hàm theo 3 lớp — **Bản chất** → **API toolbox** (tra cứu cục bộ) → **Pseudo** (chừa quyết định khó ở `TODO(you)`).
>
> Nền: [../development/device_driver.md](../development/device_driver.md) (fbtft tạo /dev/fb0) · [../architecture.md](../architecture.md) §5

---

## 1. 🎯 Mục tiêu & vị trí

HAL màn hình: `mmap` `/dev/fb0` và đẩy một vùng pixel RGB565. LVGL ([10](10-lvgl-ui.md)) gọi qua đây, không biết bên dưới là fbtft.

**File:** `hal/include/IDisplayHal.hpp`, `hal/display/FbDisplayHal.hpp/.cpp`.
**Phụ thuộc:** `Types.hpp`, Linux fb headers. **Tiền đề:** `/dev/fb0` đã có ở 320×240 (Week 1, fbtft).

---

## 2. 📜 Hợp đồng

Trích [../architecture.md](../architecture.md) §2.1:
```cpp
class IDisplayHal {
public:
    virtual ~IDisplayHal() = default;
    virtual int  init(const char* fbdev) = 0;                  // mmap /dev/fb0
    virtual void flush(int x1,int y1,int x2,int y2, const uint16_t* px) = 0; // RGB565 vùng
    virtual int  width()  const = 0;   // 320
    virtual int  height() const = 0;   // 240
};
```
**Ràng buộc:** `flush` copy đúng vùng (x1,y1)–(x2,y2) vào framebuffer theo **stride thật** (line_length), không giả định width×2; RAII `munmap`+close ở dtor. Gọi từ **main thread** (LVGL flush_cb).

---

## 3. 🤔 Quyết định trước khi code

<details><summary><b>Q1.</b> Vì sao phải dùng <code>line_length</code> (stride) chứ không phải width×2?</summary>

> Framebuffer có thể *padding* mỗi dòng cho thẳng hàng bộ nhớ → `fix.line_length` (byte/dòng) có thể > width×2. Tính offset dòng bằng `y * line_length`, không phải `y * width * 2`. Sai chỗ này → ảnh xô chéo (xem Cạm bẫy).
</details>

<details><summary><b>Q2.</b> Xác nhận bpp = 16 (RGB565) bằng cách nào?</summary>

> Đọc `FBIOGET_VSCREENINFO` → `var.bits_per_pixel` phải = 16; `var.red/green/blue.offset/length` cho biết RGB565 (5/6/5). Nếu khác (vd 32bpp) thì phải convert. Đừng giả định.
</details>

<details><summary><b>Q3.</b> Flush cả màn hay chỉ vùng dirty?</summary>

> Chỉ vùng (x1,y1)–(x2,y2) LVGL báo dirty → ít byte qua SPI → mượt hơn (SPI 32MHz là nút cổ chai). LVGL vốn gọi flush theo vùng.
</details>

**⚖️ Bạn tự chốt:** có double-buffer ở tầng HAL không (gợi ý: không cần, LVGL tự quản draw buffer — [10](10-lvgl-ui.md)).

---

## 4. 🔨 Trình tự dựng

| Bước | Làm | Done-when |
|------|-----|-----------|
| 1 | `init`: open + ioctl varinfo/fixinfo + mmap | đọc đúng 320×240, bpp 16, line_length |
| 2 | `flush` cả màn 1 màu (đỏ) | màn hình đỏ toàn bộ |
| 3 | `flush` một vùng nhỏ màu khác | ô màu đúng vị trí, không lệch |
| 4 | dtor `munmap` + close | chạy lại không lỗi "busy"/leak |

---

## 5. 🧩 Khung code tự điền

### 5.1 `hal/display/FbDisplayHal.hpp` — *skeleton*
```cpp
#pragma once
#include "IDisplayHal.hpp"
#include <cstdint>
#include <cstddef>
namespace bbb {
class FbDisplayHal : public IDisplayHal {
public:
    ~FbDisplayHal() override;                  // munmap + close
    FbDisplayHal(const FbDisplayHal&) = delete;
    int  init(const char* fbdev) override;
    void flush(int x1,int y1,int x2,int y2,const uint16_t* px) override;
    int  width()  const override { return w_; }
    int  height() const override { return h_; }
private:
    int      fd_   = -1;
    uint8_t* mem_  = nullptr;     // mmap base
    size_t   memLen_ = 0;
    int      w_ = 0, h_ = 0;
    size_t   stride_ = 0;         // line_length (byte/dòng)
};
} // namespace bbb
```

---

### 5.2 `init()`

**Bản chất.** Framebuffer là **bộ nhớ phần cứng ánh xạ vào không gian địa chỉ tiến trình** qua `mmap` — ghi vào RAM đó là pixel hiện lên màn. Nhưng *hình học* của vùng nhớ đó (độ rộng, bpp, byte/dòng) do driver quyết, không phải bạn → phải **hỏi driver** qua `ioctl` rồi tin con số đó, đặc biệt `line_length` (stride) vì driver có thể chèn padding mỗi dòng. Mọi giả định cứng (width×2, 16bpp) đều là mầm bug.

**API toolbox** (Linux fbdev):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `open(fbdev, O_RDWR)` | Mở `/dev/fb0` | `<0` = lỗi (quyền? thiếu device?) |
| `ioctl(fd, FBIOGET_VSCREENINFO, &var)` | Lấy `xres/yres/bits_per_pixel`, offset RGB | kiểm `bits_per_pixel == 16` |
| `ioctl(fd, FBIOGET_FSCREENINFO, &fix)` | Lấy `line_length` (stride) thật | **dùng cái này, không tự nhân** |
| `mmap(0, len, PROT_READ\|PROT_WRITE, MAP_SHARED, fd, 0)` | Ánh xạ fb vào tiến trình | trả `MAP_FAILED` (không phải `NULL`) khi lỗi |

**Pseudo:**
```
fd_ = open(fbdev, O_RDWR);                       <0 → return -1
ioctl(FBIOGET_VSCREENINFO,&var); ioctl(FBIOGET_FSCREENINFO,&fix)
w_=var.xres; h_=var.yres; stride_=fix.line_length
var.bits_per_pixel != 16 → log + return -2
memLen_ = stride_ * h_
mem_ = mmap(0, memLen_, RW, MAP_SHARED, fd_, 0); == MAP_FAILED → return -3
return 0
```
> **TODO(you):** log `var.red/green/blue.offset` để xác nhận RGB565; nếu panel báo BGR thì xử lý ở đâu? (cờ swap, hoặc set color-format ở LVGL — [10](10-lvgl-ui.md)).

---

### 5.3 `flush()`

**Bản chất.** Đây là **copy 2D với hai stride khác nhau**: nguồn (buffer LVGL) nén sát theo *bề rộng vùng dirty*, đích (framebuffer) trải theo *stride toàn màn*. Vì hai stride khác nhau nên phải copy **từng dòng một** — không thể một `memcpy` khối. Bug kinh điển là dùng nhầm stride đích (xô chéo) hoặc ghi tràn mép màn (segfault). Đây là hot path qua SPI → chỉ copy đúng vùng dirty.

**API toolbox** (C):

| API | Công dụng | Gotcha |
|-----|-----------|--------|
| `memcpy(dst, src, n)` | Copy một dòng pixel | `n` tính theo *byte* (pixel × 2), không phải số pixel |

**Pseudo:**
```
clamp x2<w_, y2<h_   // chặn ghi tràn
for y in [y1 .. y2]:
    dst = mem_ + y * stride_ + x1 * 2            // đích theo stride toàn màn
    src = px  + (y - y1) * (x2 - x1 + 1)         // nguồn nén theo bề rộng vùng
    memcpy(dst, src, (x2 - x1 + 1) * 2)
```
> **TODO(you):** clamp biên (x2,y2) tránh ghi tràn. Layout `px` của LVGL theo vùng dirty hay full-width? Xác nhận khi nối LVGL ([10](10-lvgl-ui.md)) — chỗ dễ sai, đưa Claude review.

---

### 5.4 `~FbDisplayHal()`

**Bản chất.** RAII đối xứng với `init`: `munmap` vùng đã map rồi `close` fd, guard giá trị "chưa khởi tạo" (`mem_ == nullptr`/`MAP_FAILED`, `fd_ < 0`).

**API toolbox:** `munmap(mem_, memLen_)` · `close(fd_)`.
> **TODO(you):** guard khi init fail giữa chừng (map chưa thành nhưng fd đã mở).

---

## 6. ⚠️ Cạm bẫy ([troubleshooting.md §2](../troubleshooting.md))

- **Dùng width×2 thay vì line_length** → ảnh xô chéo dần xuống. Luôn theo `stride_`.
- **Giả định 16bpp** mà fb là 32bpp → màu loạn. Đọc và kiểm `bits_per_pixel`.
- **Ghi tràn biên** khi vùng dirty chạm mép → segfault/nhiễu. Clamp x2,y2.
- **Quên munmap** → leak mapping; mở lại có thể lỗi.
- **RGB vs BGR / byte order** → đỏ ra xanh. Xác nhận field offset, không đoán.

---

## 7. ✅ Checkpoint review

**Xong khi:** tô full màn 1 màu đúng; tô vùng nhỏ đúng vị trí + màu; đọc đúng 320×240/16bpp/stride; dtor sạch.

**Đưa Claude review:** `FbDisplayHal.cpp`. Hỏi:
1. *"Tính offset dòng của tôi có đúng theo `line_length` chưa?"*
2. *"`flush` của tôi có clamp biên để không ghi tràn không?"*
3. *"Layout buffer `px` từ LVGL khớp với cách tôi đọc `src` chưa?"*
