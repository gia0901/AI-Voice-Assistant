# BBB Voice Assistant - Hardware Setup

> Hướng dẫn chi tiết cho phần cứng Beaglebone Black, I2S audio, SPI LCD và GPIO control.

## 1. Mục tiêu

Định nghĩa phần cứng cho dự án voice assistant trên Beaglebone Black, bao gồm:
- Microphone số I2S: **INMP441**
- Amplifier/DAC I2S: **MAX98357A**
- Màn hình SPI TFT: **ILI9341**
- Điều khiển GPIO: nút bấm PTT, tăng/giảm volume, LED trạng thái
- Kết nối tới PC chạy LM Studio qua **USB**

## 2. Tổng quan thiết kế

```text
BeagleBone Black
+-------------------------------+
|                              |
|  McASP / I2S                  |
|   BCLK, LRCLK, TXD, RXD       |<---> MAX98357A (speaker)
|                              |
|                              |<---> INMP441 (mic)
|                              |
|  SPI                          |<---> ILI9341 TFT LCD
|                              |
|  GPIO                         |<---> PTT, VOL+/VOL-, LED
|                              |
|  USB                          |<---> PC (LM Studio)
|                              |
+-------------------------------+
```

## 3. Feasibility review

### 3.1 Microphone: INMP441

**Tính khả thi:** Có.

**Lý do:**
- INMP441 là microphone digital chuẩn I2S
- Chỉ cần 3 dây số: `BCLK`, `LRCLK`, `DOUT`
- Cung cấp điện 3.3V và mass chung
- Bình thường hoạt động trực tiếp với bộ điều khiển I2S của BBB

**Yêu cầu:**
- BBB phải cấu hình McASP/I2S làm input receiver
- Dòng dữ liệu DOUT phải nối tới chân I2S RX của BBB
- Đảm bảo cấp nguồn 3.3V sạch, decoupling 100nF gần chân VDD

### 3.2 Loa + amplifier: MAX98357A

**Tính khả thi:** Có.

**Lý do:**
- MAX98357A nhận dữ liệu I2S trên chân `DIN`
- Có tích hợp DAC và class-D amplifier
- Kết nối trực tiếp tới loa passive 4Ω/8Ω
- Hoạt động tốt ở 5V, phù hợp nguồn nằm trong hệ thống

**Yêu cầu:**
- BBB xuất BCLK và LRCLK qua I2S TX
- DIN của MAX98357A nối tới chân I2S TX data
- GND chung giữa BBB và MAX98357A
- Loa passive 4Ω/8Ω, tốt nhất <= 10W để phù hợp công suất MAX98357A

### 3.3 Combined I2S bus

**Hợp lý:** Có thể dùng chung `BCLK` và `LRCLK` cho cả INMP441 và MAX98357A.

**Lưu ý:**
- INMP441 dùng đường dữ liệu input `DOUT`
- MAX98357A dùng đường dữ liệu output `DIN`
- Hai thiết bị không xung đột nếu tách rõ TX/RX trên BBB
- Phải cấu hình phần mềm để BBB chạy I2S full-duplex hoặc dùng 2 serializers khác nhau

### 3.4 So sánh với I2C audio codec

- Giải pháp INMP441 + MAX98357A là **direct I2S path**, không cần codec I2C trung gian
- Ưu điểm: đơn giản phần cứng, ít linh kiện
- Nhược điểm: phần mềm Linux/ASoC có thể cần custom device tree và simple-card machine driver

## 4. Chi tiết kết nối phần cứng

### 4.1 BeagleBone Black I2S signals

BBB có các giao diện McASP (I2S) được map ra header mở rộng.

**Yêu cầu:**
- `BCLK` (bit clock)
- `LRCLK` (word select / frame sync)
- `TXD` (data output tới MAX98357A)
- `RXD` (data input từ INMP441)
- `GND`
- `3.3V` cho INMP441
- `5V` cho MAX98357A

> Ghi chú: số PIN chính xác cần tham khảo sơ đồ chân BeagleBone Black và Device Tree overlay. Phần cứng này sử dụng bus số chung, nhưng mỗi thiết bị có đường data riêng.

### 4.2 Wiring recommended

| Thiết bị | Chân | Nối tới BBB | Ghi chú |
|----------|------|-------------|---------|
| INMP441 VDD | 3.3V | BBB 3.3V | Cấp nguồn 3.3V ổn định |
| INMP441 GND | GND | BBB GND | Chung mass |
| INMP441 LRCLK | LRCLK | BBB I2S LRCLK | Frame sync |
| INMP441 BCLK | BCLK | BBB I2S BCLK | Bit clock |
| INMP441 DOUT | RXD | BBB I2S RX | Data in |
| MAX98357A VIN | 5V | BBB 5V | Cấp nguồn 5V |
| MAX98357A GND | GND | BBB GND | Chung mass |
| MAX98357A LRCLK | LRCLK | BBB I2S LRCLK | Shared frame sync |
| MAX98357A BCLK | BCLK | BBB I2S BCLK | Shared bit clock |
| MAX98357A DIN | TXD | BBB I2S TX | Data out |
| Loa | SPK+ / SPK- | MAX98357A output | Loa passive |

### 4.3 Power và đất

- Dùng chung mass giữa BBB, INMP441, MAX98357A, TFT, và các nút bấm
- INMP441 phải dùng nguồn 3.3V; không dùng 5V cho mic
- MAX98357A dùng nguồn 5V vì loa cần điện áp cao hơn
- Nên có tụ lọc 100nF gần mỗi IC

## 5. Màn hình SPI TFT ILI9341

### 5.1 Kết nối SPI cơ bản

| Chức năng | TFT | BBB |
|-----------|-----|-----|
| SCK | CLK | SPI SCLK |
| MOSI | SDA | SPI MOSI |
| CS | CS | SPI CS |
| DC | DC | GPIO |
| RESET | RESET | GPIO |
| BL | Backlight | GPIO/PWM |
| VCC | 3.3V/5V | 3.3V or 5V |
| GND | GND | GND |

### 5.2 Gợi ý phần cứng

- Dùng **SPI0** hoặc SPI1 trên BBB
- Dùng GPIO làm `DC`, `RESET`, `BACKLIGHT`
- Đảm bảo tín hiệu logic tương thích 3.3V

## 6. GPIO control

### 6.1 Nút bấm

| Chức năng | Số chân | Loại |
|-----------|---------|------|
| PTT | GPIO input | Edge detect |
| Volume + | GPIO input | Debounce |
| Volume - | GPIO input | Debounce |

### 6.2 LED trạng thái

- 1 LED cho `status/error`
- Có thể dùng GPIO output với điện trở 220Ω

### 6.3 Thư viện phần mềm

- Sử dụng `libgpiod` trên Linux để đọc nút và điều khiển LED
- Sử dụng `gpiod` line consumer model thay vì sysfs

## 7. USB kết nối tới PC

### 7.1 Mục đích

- Kết nối BBB tới PC chạy LM Studio
- Dùng USB làm phương tiện truyền dữ liệu thay vì Ethernet

### 7.2 Kiến trúc

- Dùng USB Ethernet adapter hoặc USB CDC-ACM để trao đổi dữ liệu
- PC chạy LM Studio có thể tiếp nhận HTTP request từ BBB qua kết nối USB

## 8. Phần mềm / Device Tree notes

### 8.1 Device Tree cho I2S

Sử dụng `simple-audio-card` hoặc custom ASoC machine driver để bind McASP với thiết bị I2S.

- MAX98357A: I2S output DAC/amp
- INMP441: I2S input digital mic

Nếu driver không có sẵn, cần custom DTS node và/hoặc kernel patch cho McASP + simple-audio-card.

### 8.2 Tương thích Linux

- `ALSA` là nền tảng chính để điều khiển I2S audio
- `INMP441` và `MAX98357A` không có điều khiển I2C, nên chúng là thiết bị pure I2S
- Không cần codec I2C nếu sử dụng trực tiếp hai module này

## 9. Kết luận

- **INMP441** và **MAX98357A** là một giải pháp khả thi cho microphone và speaker của dự án
- Ưu điểm: đơn giản phần cứng, ít linh kiện, dùng native I2S
- Khó khăn chính là phần mềm: cần cấu hình Device Tree và ASoC đúng, cũng như cấu hình McASP cho full-duplex I2S

---

## 10. Hành động tiếp theo

- Thiết kế Device Tree overlay cho McASP/I2S với INMP441 và MAX98357A
- Kiểm tra lại pinout BBB chính xác trước khi đấu dây
- Xây dựng Prototype board với nguồn 3.3V, 5V, chung GND
- Test `BCLK`, `LRCLK`, `TXD`, `RXD` trước khi nối loa và mic
