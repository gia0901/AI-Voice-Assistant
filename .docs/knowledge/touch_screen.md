# Kiến thức: Touch screen điện trở & XPT2046

> Tài liệu học tập (tiếng Việt theo Rule §18). Nền tảng để hiểu node `touch@1` trong
> [../../kernel/overlays/BBB-VOICE-ASSISTANT.dts](../../kernel/overlays/BBB-VOICE-ASSISTANT.dts)
> và phần wiring trong [../hardware_setup.md](../hardware_setup.md). Bổ trợ cho
> [../development/device_driver.md](../development/device_driver.md) (SPI/Device Tree).

---

## 1. Màn cảm ứng điện trở (resistive) hoạt động thế nào

Module ILI9341 của dự án đi kèm một panel **resistive 4 dây** điều khiển bởi chip **XPT2046**.

Cấu tạo:

- **2 lớp màng trong suốt dẫn điện** (phủ ITO), đặt sát nhau, cách nhau bởi các hạt cách ly li ti
  → bình thường **không chạm nhau**.
- Một lớp lo trục **X**, một lớp lo trục **Y**. Mỗi lớp có điện cực ở **2 cạnh đối diện**.
- Khi **ấn**, hai lớp **chạm nhau tại đúng điểm đó**.

Đo toạ độ **X**: cấp điện áp ngang lớp X (cạnh trái 0V, cạnh phải 3.3V) → tạo một **dải điện áp dốc**
từ trái sang phải. Lớp Y lúc này đóng vai "que đo vôn kế", đọc điện áp tại điểm chạm →
**điện áp đó tỉ lệ với vị trí X**. Đảo vai trò 2 lớp để đo **Y**.

> Ý tưởng cốt lõi: **vị trí chạm = một mức điện áp analog**. Muốn biến thành con số → cần **ADC**.

---

## 2. XPT2046 — chip làm gì

XPT2046 (tương thích ADS7846) là **bộ điều khiển touch**, nằm giữa panel analog và BBB số:

| Khối trong chip | Vai trò |
|-----------------|---------|
| **ADC 12-bit** | Số hoá điện áp điểm chạm → giá trị **0..4095** (lý do `ABS_X/Y Max = 4095`) |
| **Mux + analog switch** | Chuyển qua lại: "cấp điện lớp X / đo lớp Y" rồi ngược lại; chọn kênh đo X, Y, Z(áp lực) |
| **PENIRQ** | Bộ so sánh: khi 2 lớp chạm nhau thì **kéo dây xuống Low** báo CPU "có chạm" (khỏi dò liên tục) |
| **SPI slave** | Giao tiếp với BBB; mỗi lần đọc là 1 lệnh SPI (chọn kênh) + nhận lại 12-bit kết quả |

XPT2046 dùng chung bus SPI0 với LCD, chỉ khác chip-select — xem topology trong
[../hardware_setup.md](../hardware_setup.md) §4.

---

## 3. Một chu kỳ đọc (từ chạm → số)

```
   ngón ấn → 2 lớp chạm
        │
        ▼
   PENIRQ kéo Low  ──(ngắt)──►  kernel: "có chạm, bắt đầu lấy mẫu"
        │
        ▼  với mỗi trục:
   1) mux cấp điện cho lớp tương ứng
   2) CHỜ analog ổn định        ← settle-delay-usec
   3) ADC đọc 12-bit → 0..4095
   4) đọc lặp vài lần cho chắc  ← debounce-max/tol/rep
        │
        ▼
   tính áp lực Rt từ điện trở   ← x-plate-ohms, lọc bằng pressure-max
        │
        ▼
   báo lên input subsystem: ABS_X, ABS_Y, ABS_PRESSURE, BTN_TOUCH
        │
        ▼
   lặp lại theo timer tới khi PENIRQ nhả (nhấc ngón) → BTN_TOUCH = 0
```

Điểm quan trọng: **một lần chạm và giữ** sinh **một** cạnh PENIRQ, sau đó driver tự **lấy mẫu lặp
theo timer** cho tới khi nhấc ngón (phát hiện qua `pendown-gpio`). Đó là lý do số đếm trong
`/proc/interrupts` không nhất thiết bằng số lần chạm.

---

## 4. Phía Linux: driver `ads7846` + input subsystem

- Driver kernel **`ads7846`** (khớp `compatible = "ti,tsc2046"`) là người thực hiện chu kỳ ở §3.
- Nó đăng ký một **input device** → xuất hiện ở `/dev/input/eventX`, tên `"ADS7846 Touchscreen"`.
- Ứng dụng (LVGL qua backend **evdev**) đọc các **event**:
  - `EV_ABS` / `ABS_X`, `ABS_Y` — toạ độ thô
  - `EV_ABS` / `ABS_PRESSURE` — lực ấn
  - `EV_KEY` / `BTN_TOUCH` — chạm (1) / nhả (0)

> `ads7846` ở image này là **module** (`CONFIG_TOUCHSCREEN_ADS7846=m`). SPI device tạo từ device-tree
> hay không tự nạp module → cần ép load (`/etc/modules-load.d/`). Đây là lý do lúc đầu `dmesg` im
> dù DTS đúng.

---

## 5. Giải nghĩa từng setting trong `touch@1` (ánh xạ vào §1–§3)

| Setting | Nghĩa "người thường" | Liên hệ vật lý |
|---|---|---|
| `interrupts` + `pendown-gpio` | **Chuông cửa** báo có chạm | PENIRQ kéo Low → `ACTIVE_LOW`, cạnh `FALLING` |
| `spi-max-frequency = 2MHz` | Nói chuyện **chậm** với chip | ADC chịu ~2.5MHz; nhanh hơn → kết quả hỏng (LCD chạy 32MHz vẫn ổn) |
| **`ti,settle-delay-usec`** | **Chờ một nhịp rồi mới đọc** | Sau khi mux đổi kênh, điện áp trên màng cần thời gian ổn định (điện dung). Đọc sớm = **toạ độ rác**. Thiếu cái này khiến evtest im lúc đầu |
| **`ti,x-plate-ohms`** | Hằng số tấm panel để **tính lực ấn** | Lớp màng có điện trở; ấn mạnh → tiếp xúc rộng → Rt nhỏ. Phải biết điện trở tấm (~600Ω) mới suy ra lực |
| `ti,pressure-max` | **Lọc chạm hờ** | Chạm nhẹ/vô tình → tiếp xúc kém → Rt lớn; vượt ngưỡng = bỏ qua |
| `ti,debounce-max/tol/rep` | **Đọc nhiều lần cho chắc** | Panel nhiễu: `rep` lần đọc khớp nhau mới nhận; `tol` = lệch bao nhiêu vẫn coi cùng điểm; `max` = thử tối đa mấy lần. Ổn định hơn nhưng trễ hơn |
| `ti,keep-vref-on` | Giữ điện áp tham chiếu **luôn bật** | ADC cần Vref; tắt/bật liên tục thêm nhiễu + thời gian ổn định. Thiết bị cắm nguồn → cứ để bật |
| `wakeup-source` | Cho **chạm đánh thức** board khỏi suspend | Tính năng quản lý nguồn |

> Bẫy cú pháp: các property `ti,*` ở trên là **u16**, phải viết kèm **`/bits/ 16`**. Viết cell
> 32-bit thường → driver đọc sai giá trị → vẫn hỏng dù "trông có vẻ đúng".

---

## 6. Giá trị thô ≠ pixel: calibration & xoay màn

- `ABS_X/Y` trả về **0..4095** là **số đọc thô của ADC**, KHÔNG phải toạ độ điểm ảnh (320×240).
- Cần bước **calibration**: ánh xạ `thô → pixel` (hàm tuyến tính, bù lệch tâm/méo của panel).
  Thường do **tslib** hoặc **LVGL** lo.
- Vì là thô nên **trục có thể ngược/đảo** so với chiều hiển thị. Dự án đặt `rotate = <180>` cho LCD →
  touch phải chỉnh khớp bằng một trong hai cách:
  - thêm `touchscreen-inverted-x`, `touchscreen-inverted-y`, `touchscreen-swapped-x-y` vào node, **hoặc**
  - để LVGL/tslib calibrate ở tầng ứng dụng.

> Nguyên tắc: **chạy ra toạ độ thô trước, chỉnh trục/calibrate sau.** Đừng đảo trục khi chưa thấy
> toạ độ nhảy.

---

## 7. Resistive vs Capacitive (định vị kiến thức)

| | Resistive (XPT2046 — dự án) | Capacitive (điện thoại) |
|---|---|---|
| Nguyên lý | Ấn cho 2 lớp chạm nhau | Cảm điện trường ngón tay |
| Cần lực ấn | Có (đo được áp lực) | Không |
| Calibration | **Bắt buộc** | Hầu như không |
| Đa điểm (multi-touch) | Không | Có |
| Bút / găng tay | Mọi vật cứng đều dùng được | Cần ngón/bút điện dung |
| Giá | Rẻ | Đắt hơn |

→ Resistive hợp với thiết bị nhúng giá rẻ, thao tác đơn giản (1 điểm), đúng nhu cầu của dự án.

---

## 8. Liên hệ dự án & debug nhanh

Đường đi đã kiểm chứng khi bring-up (xem [../hardware_setup.md](../hardware_setup.md) §9):

```bash
dmesg | grep -i ads7846                 # driver bind: "touchscreen, irq ..."
cat /proc/interrupts | grep -i ads7846  # số đếm tăng khi chạm → PENIRQ chạy
sudo evtest /dev/input/eventN           # chạm → ABS_X/Y nhảy, BTN_TOUCH 1/0
```

Tách lỗi phần cứng vs cấu hình: tháo driver rồi soi cạnh thô —
`sudo modprobe -r ads7846 && gpiomon -f gpiochip2 3` (chạm phải ra FALLING EDGE). Có cạnh mà vẫn
không ra toạ độ → lỗi **property** (mục §5), không phải wiring.

**Một câu để nhớ:** *XPT2046 biến điểm bạn ấn thành con số nhờ đo điện áp; các setting chủ yếu để
(1) chờ tín hiệu ổn định — `settle-delay`, (2) tính lực ấn — `x-plate-ohms`, (3) lọc nhiễu —
`debounce`/`pressure-max`.*
