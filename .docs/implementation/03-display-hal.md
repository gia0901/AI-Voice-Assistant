# 03 — DisplayHal (fbdev)

> Implementation guide (tiếng Việt, Rule §18). Template 7 mục — [README.md](README.md). *Giàn giáo + gợi ý.*
>
> Nền: [../development/device_driver.md](../development/device_driver.md) (fbtft tạo /dev/fb0) · [../architecture.md](../architecture.md) §5

---

## 1. 🎯 Mục tiêu & vị trí

HAL màn hình: `mmap` `/dev/fb0` và đẩy một vùng pixel RGB565. LVGL (guide 10) gọi qua đây, không biết bên dưới là fbtft.

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
**Ràng buộc:** `flush` copy đúng vùng (x1,y1)–(x2,y2) vào framebuffer theo **stride thật** (line_length), không giả định width*2; RAII `munmap`+close ở dtor. Gọi từ **main thread** (LVGL flush_cb).

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

**⚖️ Bạn tự chốt:** có double-buffer ở tầng HAL không (gợi ý: không cần, LVGL tự quản draw buffer — guide 10).

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

### 5.1 `FbDisplayHal.hpp` — *skeleton*
```cpp
#pragma once
#include "IDisplayHal.hpp"
#include <cstdint>
namespace bbb::hal {
class FbDisplayHal : public IDisplayHal {
public:
    ~FbDisplayHal() override;                  // munmap + close
    FbDisplayHal(const FbDisplayHal&) = delete;
    int  init(const char* fbdev) override;
    void flush(int x1,int y1,int x2,int y2,const uint16_t* px) override;
    int  width()  const override { return w_; }
    int  height() const override { return h_; }
private:
    int      fd_ = -1;
    uint8_t* mem_ = nullptr;     // mmap base
    size_t   memLen_ = 0;
    int      w_ = 0, h_ = 0;
    size_t   stride_ = 0;        // line_length (byte/dòng)
};
} // namespace bbb::hal
```

### 5.2 Thuật toán `init`
```
fd_ = open(fbdev, O_RDWR);                 nếu <0 → return -1
ioctl(fd_, FBIOGET_VSCREENINFO, &var);
ioctl(fd_, FBIOGET_FSCREENINFO, &fix);
w_ = var.xres; h_ = var.yres; stride_ = fix.line_length;
kiểm var.bits_per_pixel == 16              nếu không → return -2 (cảnh báo)
memLen_ = stride_ * h_;
mem_ = mmap(NULL, memLen_, PROT_READ|PROT_WRITE, MAP_SHARED, fd_, 0);
nếu mem_ == MAP_FAILED → return -3
return 0
```
> TODO(you): log var.red/green/blue offset để xác nhận RGB565; nếu panel báo BGR thì sao? (gợi ý: cờ swap, hoặc set ở LVGL).

### 5.3 Thuật toán `flush` (copy vùng theo stride)
```
for y in [y1 .. y2]:
    dst = mem_ + y * stride_ + x1 * 2          // 2 byte/pixel
    src = px  + (y - y1) * (x2 - x1 + 1)        // buffer LVGL nén theo vùng
    memcpy(dst, src, (x2 - x1 + 1) * 2)
```
> TODO(you): kiểm biên (x2<w_, y2<h_) tránh ghi tràn. Layout `px` của LVGL là theo vùng dirty hay full-width? (xác nhận khi nối LVGL ở guide 10 — đây là chỗ dễ sai, hỏi Claude review).

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
