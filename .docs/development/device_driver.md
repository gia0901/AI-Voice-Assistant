# Device Driver — ILI9341 qua fbtft

> Tài liệu học tập (tiếng Việt theo Rule §18). Giải thích từ nền tảng: SPI hoạt động ra sao, Device Tree overlay là gì, fbtft bind panel thế nào, và *tại sao* chọn fbtft trước DRM. Bám sát overlay thật: [BBB-VOICE-ASSISTANT.dts](../../kernel/overlays/BBB-VOICE-ASSISTANT.dts).

---

## 1. Bức tranh tổng thể

```
 LVGL (userspace)
   │  vẽ vào framebuffer
   ▼
 /dev/fb0  ◄── fbtft (fb_ili9341) tạo ra
   │  fbtft dịch pixel RGB565 → lệnh SPI
   ▼
 spi0 (kernel SPI master)  ──SCLK/MOSI──►  ILI9341 panel
   │                        ──DC (gpio)──►  (chọn command/data)
   └────────────────────────RESET (gpio)─►
```

Điểm cốt lõi: **ta không tự viết driver**. Kernel đã có `fb_ili9341` (thuộc họ fbtft). Việc của ta là *khai báo phần cứng* qua Device Tree để kernel biết: "có một panel ILI9341 nối vào spi0, chân DC/RESET ở đây" — rồi kernel tự tạo `/dev/fb0`.

---

## 2. Nền tảng: SPI là gì

SPI (Serial Peripheral Interface) là bus nối tiếp đồng bộ, master–slave, 4 dây cơ bản:

| Dây | Vai trò | Pin trên BBB (overlay) |
|-----|--------|------------------------|
| SCLK | clock do master phát | P9_22 (`spi0_sclk`) |
| MOSI | Master Out Slave In (master → panel) | P9_21 (`spi0_d0`) |
| MISO | Master In Slave Out (panel → master) | P9_18 (`spi0_d1`) — panel ILI9341 hầu như không trả dữ liệu, để input |
| CS | Chip Select (active-low) | P9_17 (`spi0_cs0`) |

ILI9341 cần thêm **2 chân GPIO ngoài SPI**:
- **DC (Data/Command)**: mức logic báo byte đang gửi là *lệnh* hay *dữ liệu pixel*. → P9_23 = `gpio1_17`.
- **RESET**: reset cứng panel lúc init, active-low. → P9_15 = `gpio1_16`.

> Vì sao DC không nằm trong giao thức SPI? SPI chỉ truyền byte, không có khái niệm "đây là lệnh". ILI9341 dùng một chân phụ để phân biệt — đây là biến thể thường gọi là "4-line SPI" (MIPI DBI Type C).

`spi-max-frequency = 32 MHz` trong overlay: tốc độ clock tối đa. Nếu hình nhiễu/lệch, hạ xuống (16 MHz) để loại trừ nguyên nhân tín hiệu (xem [troubleshooting.md](../troubleshooting.md) §2).

---

## 3. Nền tảng: Device Tree & overlay

**Device Tree (DT)** là cách kernel Linux mô tả phần cứng *không tự dò được* (khác PCI/USB có khả năng enumerate). ARM SoC như AM335x dùng DT để biết: có những peripheral nào, chân nào, clock nào.

**Overlay** = một mảnh DT *ghép thêm* vào DT gốc lúc boot (qua `/boot/uEnv.txt`), thay vì sửa thẳng file DTB chính. Lợi ích: bật/tắt phần cứng tùy chọn (như màn hình) mà không phải biên dịch lại toàn bộ DT.

Cấu trúc overlay của ta gồm 2 fragment:

### Fragment 0 — pin mux

AM335x cho phép mỗi chân vật lý chạy nhiều chức năng (mode 0–7). `pinmux` chọn chức năng + cấu hình pull-up/down, hướng in/out.

```dts
bb_ili9341_spi0_pins: ... {
    pinctrl-single,pins = <
        0x150 0x10   /* P9_22 spi0_sclk, MODE0, output pull-up */
        0x154 0x10   /* P9_21 spi0_d0 (MOSI) */
        0x158 0x30   /* P9_18 spi0_d1 (MISO), input pull-up */
        0x15c 0x10   /* P9_17 spi0_cs0 */
    >;
};
bb_ili9341_ctrl_pins: ... {
    pinctrl-single,pins = <
        0x040 0x0f   /* P9_15 RESET → gpio1_16, MODE7 (GPIO) */
        0x044 0x0f   /* P9_23 DC    → gpio1_17, MODE7 (GPIO) */
    >;
};
```

- Số đầu (`0x150`...) = offset thanh ghi pinmux của chân đó.
- Số sau = giá trị config (mode + pull + direction). `MODE0` = chức năng SPI; `MODE7` = GPIO thường.

### Fragment 1 — bật spi0 + gắn panel

```dts
target = <&spi0>;
__overlay__ {
    ti,pindir-d0-out-d1-in = <1>;   /* d0=MOSI(out), d1=MISO(in) */
    status = "okay";                /* bật controller */
    pinctrl-0 = <&bb_ili9341_spi0_pins>;

    ili9341_lcd: display@0 {
        compatible = "ilitek,ili9341", ... ;  /* khớp driver fb_ili9341 */
        reg = <0>;                            /* CS0 */
        buswidth = <8>;
        spi-max-frequency = <32000000>;
        rotate = <0>;
        dc-gpios    = <&gpio1 17 0>;          /* DC,    active-high */
        reset-gpios = <&gpio1 16 1>;          /* RESET, active-low (cờ 1) */
    };
};
```

Mấu chốt: chuỗi `compatible` là *chìa khóa* để kernel chọn đúng driver. `"ilitek,ili9341"` khiến `fb_ili9341` nhận panel này. `reg = <0>` nói panel ngồi ở CS0.

---

## 4. Quy trình build & nạp overlay

Script [copy_dtbo.sh](../../kernel/overlays/copy_dtbo.sh):

```bash
dtc -O dtb -o BBB-VOICE-ASSISTANT.dtbo -b 0 -@ BBB-VOICE-ASSISTANT.dts
sudo cp BBB-VOICE-ASSISTANT.dtbo /lib/firmware/
```

- `dtc` = Device Tree Compiler; `-@` giữ symbol để overlay tham chiếu node gốc (`&spi0`).
- Kết quả `.dtbo` đặt ở `/lib/firmware/`, rồi khai báo trong `/boot/uEnv.txt`:
  ```
  uboot_overlay_addr0=/lib/firmware/BBB-VOICE-ASSISTANT.dtbo
  ```
- Reboot → kiểm tra: `dmesg | grep -i fb_ili9341` và `ls /dev/fb0`.

---

## 5. So sánh các hướng driver (vì sao chọn fbtft)

| Hướng | Cơ chế | Ưu | Nhược | Quyết định |
|-------|--------|-----|-------|-----------|
| **fbtft / fb_ili9341** | driver legacy tạo `/dev/fb0` | Có sẵn, đơn giản, LVGL fbdev dùng ngay; giá trị học tập tốt | Framework fb cũ, đang deprecated dần | ✅ **Chọn** (Decision #10) — đã chạy được |
| **DRM `panel-mipi-dbi-spi`** | driver DRM hiện đại, dumb-buffer | Được maintain lâu dài; chuẩn đi tới | Cần kernel mới (6.x), LVGL phải đổi sang DRM backend | 🔜 Nâng cấp tương lai |
| **Custom spidev userspace** | tự đẩy byte qua `/dev/spidevX.Y` | Toàn quyền kiểm soát, hiểu sâu | Tự lo timing, init sequence, refresh; nhiều việc | 🔜 Fallback nếu cần |

Tư duy ra quyết định: ở giai đoạn này mục tiêu là **chạy được nhanh + học cơ chế bind driver qua DT**. fbtft đạt cả hai. DRM "đúng hướng dài hạn" nhưng kéo theo đổi kernel + đổi backend LVGL — chi phí không đáng lúc này (tránh over-engineering, Rule §18). Ghi nhận DRM là nợ kỹ thuật có chủ đích.

---

## 6. Liên hệ tầng trên

`IDisplayHal` (xem [hal_layer.md](hal_layer.md)) `mmap` `/dev/fb0` và đẩy vùng RGB565 — hoàn toàn không biết bên dưới là fbtft hay DRM. Nhờ vậy khi nâng lên DRM sau này, chỉ phần impl của HAL đổi, middleware/app giữ nguyên. Đó là lợi ích của ranh giới HAL.
