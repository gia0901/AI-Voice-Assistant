# audio_prep.md

# BBB Voice Assistant - Audio Bring-up Preparation

> Mục tiêu của tài liệu này là xác minh toàn bộ Audio Stack trên BeagleBone Black trước khi viết Device Tree cuối cùng cho INMP441 và MAX98357A.

---

# 1. Why This Document Exists

Không viết DTS Audio ngay từ đầu.

Lý do:

* Linux 6.12 có thể khác các ví dụ trên Internet.
* McASP bindings thay đổi theo kernel.
* ASoC bindings thay đổi theo kernel.
* Một số DTS node cũ không còn được hỗ trợ.

Mục tiêu:

```text
Xác minh hệ thống thực tế
    ↓
Thu thập thông tin
    ↓
Thiết kế DTS chính xác
```

---

# 2. Prerequisites

Hoàn thành:

* Buildroot boot thành công
* Ethernet hoạt động
* SSH hoạt động
* UART debug hoạt động

Chưa cần:

* INMP441
* MAX98357A

---

# 3. Buildroot Configuration

Bật:

```config
CONFIG_SOUND=y
CONFIG_SND=y
CONFIG_SND_SOC=y

CONFIG_SND_SOC_AM33XX_SOC_PCM=y
CONFIG_SND_SOC_DAVINCI_MCASP=y

CONFIG_DEBUG_FS=y
CONFIG_PROC_FS=y
CONFIG_SYSFS=y
```

---

# 4. Verify Kernel Drivers

Boot BBB.

Kiểm tra:

```bash
dmesg | grep -i mcasp
```

Kỳ vọng:

```text
davinci-mcasp ...
```

xuất hiện.

---

Kiểm tra module:

```bash
cat /proc/asound/cards
```

Có thể chưa có sound card.

Điều này bình thường.

---

# 5. Verify Device Tree Nodes

Liệt kê:

```bash
ls /proc/device-tree/
```

---

Tìm:

```bash
find /proc/device-tree -iname "*mcasp*"
```

---

Mục tiêu:

Xác định:

```text
mcasp0
mcasp1
```

node nào đang tồn tại.

Ghi lại.

---

Ví dụ:

```text
/proc/device-tree/ocp/mcasp@48038000
```

---

# 6. Verify Current DTS

Trích DTS đang chạy:

```bash
dtc -I fs -O dts /proc/device-tree > running.dts
```

---

Tìm:

```bash
grep -n mcasp running.dts
```

---

Ghi lại:

* node name
* serializer count
* pinctrl name

---

# 7. Verify Available Pinmux

Kiểm tra:

```bash
cat /sys/kernel/debug/pinctrl/*/pinmux-pins
```

---

Hoặc:

```bash
grep mcasp /sys/kernel/debug/pinctrl/*/pinmux-pins
```

---

Mục tiêu:

Xác định:

```text
P9_28
P9_29
P9_30
P9_31
```

đang được mux như thế nào.

---

# 8. Verify McASP Serializer Layout

Mở:

```bash
grep -n serializer running.dts
```

---

Ví dụ:

```dts
serial-dir = <
    1
    2
>;
```

---

Ghi lại:

```text
serializer 0
serializer 1
```

vai trò:

```text
TX
RX
```

---

Đây là dữ liệu quan trọng nhất để viết DTS cuối cùng.

---

# 9. Verify Existing ASoC Bindings

Kiểm tra:

```bash
find /proc/device-tree -iname "*sound*"
```

---

Tìm:

```bash
simple-audio-card
audio-graph-card
```

---

Mục tiêu:

Biết kernel đang dùng binding nào.

---

# 10. Check Kernel Bindings

Trong source kernel:

```bash
Documentation/devicetree/bindings/sound/
```

---

Liệt kê:

```bash
ls Documentation/devicetree/bindings/sound
```

---

Tìm:

```bash
simple-audio-card.yaml
audio-graph-card.yaml
```

---

Ghi lại.

---

# 11. Verify MAX98357A Support

Trong kernel source:

```bash
grep -R "max98357" sound/
```

---

Mục tiêu:

Xác định:

```text
Driver có tồn tại không?
```

---

Ví dụ kỳ vọng:

```text
sound/soc/codecs/max98357a.c
```

---

Nếu tồn tại:

```text
MAX98357A dùng driver kernel sẵn có.
```

---

# 12. Verify INMP441 Support

Tìm:

```bash
grep -R "inmp441" sound/
```

---

Có thể:

```text
Không tìm thấy.
```

---

Điều này hoàn toàn bình thường.

INMP441 thường được mô hình hóa như:

```text
simple codec
dummy codec
```

trong ASoC.

---

# 13. Audio Clock Strategy

Chốt:

```text
BBB = I2S Master
```

BBB phát:

```text
BCLK
LRCLK
```

---

INMP441:

```text
Slave
```

---

MAX98357A:

```text
Slave
```

---

Đây là kiến trúc cuối cùng.

---

# 14. Hardware Wiring Verification

Chưa cần DTS.

Chỉ nối dây:

```text
P9_29 -> BCLK
P9_28 -> LRCLK

P9_31 -> MAX98357A DIN

P9_30 -> INMP441 SD
```

---

Dùng oscilloscope hoặc logic analyzer.

Kiểm tra:

```text
BCLK xuất hiện
LRCLK xuất hiện
```

sau khi Audio Card hoạt động.

---

# 15. DTS Design Inputs

Chỉ được viết DTS cuối cùng sau khi thu thập:

* running.dts
* mcasp node
* serializer layout
* pinctrl mapping
* binding type
* codec driver status

---

# 16. Final DTS Deliverables

Sau khi hoàn tất các bước trên sẽ tạo:

## audio_pins.dtsi

Chứa:

```dts
pinctrl
```

---

## audio_mcasp.dtsi

Chứa:

```dts
mcasp0
serializer
```

---

## audio_card.dtsi

Chứa:

```dts
simple-audio-card
```

---

## bbb-voice-assistant.dts

Include:

```dts
audio_pins.dtsi
audio_mcasp.dtsi
audio_card.dtsi
```

---

# Success Criteria

Được xem là hoàn thành khi:

```bash
aplay -l
```

hiển thị:

```text
MAX98357A
```

---

và:

```bash
arecord -l
```

hiển thị:

```text
INMP441
```

---

Sau đó mới tiến hành phát triển Voice Assistant.
