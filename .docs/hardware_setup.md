# BBB Voice Assistant - Hardware Setup

> Hướng dẫn phần cứng BeagleBone Black cho dự án voice assistant chỉ dùng audio native I2S.

## 1. Mục tiêu

Mục tiêu của tài liệu này là định nghĩa phần cứng cho dự án voice assistant trên BeagleBone Black với:
- Microphone số I2S: **INMP441**
- DAC/Amp I2S: **MAX98357A**
- Màn hình SPI TFT: **ILI9341**
- Điều khiển GPIO: nút bấm PTT, tăng/giảm volume, LED trạng thái

> Ghi chú: INMP441 là micro kỹ thuật số sử dụng bus I2S, không phải I2C.

## 2. Tổng quan thiết kế

Dự án sử dụng native I2S audio để giữ đường truyền âm thanh ngắn và giảm độ trễ. Giải pháp này yêu cầu:
- McASP / I2S pinout và device-tree overlay phù hợp cho BeagleBone Black
- ASoC `simple-audio-card` hoặc driver tương đương cho MAX98357A và INMP441
- Không dùng USB hub, USB sound card hoặc USB microphone cho phần âm thanh của dự án

---

## Component list

| Component | Part / Notes | Qty |
|-----------|-------------|-----|
| BeagleBone Black | Rev C hoặc tương đương | 1 |
| I2S Microphone | INMP441 digital microphone | 1 |
| I2S DAC/Amp | MAX98357A class-D amplifier | 1 |
| Speaker | 8Ω speaker phù hợp với MAX98357A | 1 |
| TFT LCD Display | ILI9341, 320×240, SPI, 3.3V | 1 |
| Tactile Buttons | 6×6mm push button | 3 |
| Pull-up Resistors | 10 kΩ | 3 |
| LED | 3mm/5mm LED | 1 |
| LED Resistor | 330 Ω | 1 |
| SD Card | 16 GB Class 10 microSD | 1 |
| Breadboard + Wires | Dupont jumper | — |

---

## I2S audio design

### Audio devices

- INMP441: microphone kỹ thuật số, xuất đầu ra I2S `DOUT`, đồng bộ với `BCLK` và `LRCLK`
- MAX98357A: DAC + amplifier, nhận dữ liệu I2S `DIN` và xuất ra loa

### Required signals

- `BCLK` (bit clock)
- `LRCLK` / `FS` (frame sync / word select)
- `DATA IN` cho DAC (BBB → MAX98357A)
- `DATA OUT` cho mic (INMP441 → BBB)
- 3.3V và GND cho cả hai module

> Lưu ý: tất cả dữ liệu âm thanh phải đi qua I2S/McASP. Không dùng USB audio.

---

## Wiring overview

### I2S wiring (BBB → audio)

| Signal | BBB Pin | Module | Notes |
|--------|---------|--------|-------|
| BCLK | P9_29 | INMP441 / MAX98357A | McASP0 bit clock (AHCLKX) |
| LRCLK / FS | P9_35 | INMP441 / MAX98357A | McASP0 frame sync (FSX) |
| DATA OUT (to DAC) | P9_31 | MAX98357A DIN | McASP0 data serializer AXR0 |
| DATA IN (from mic) | P9_33 | INMP441 DOUT | McASP0 data serializer AXR1 |
| 3.3V | P9_3 | Power | Cung cấp cho INMP441 và MAX98357A nếu dùng 3.3V |
| GND | P9_1 | Ground | Chung GND |

> Đây là cấu hình pin I2S dành cho McASP0 trên BeagleBone Black, chọn các chân P9_29/P9_31/P9_33/P9_35 để tránh xung đột với SPI0 và GPIO P8 đã dùng cho màn hình và nút bấm.

> Cần kiểm tra pinmux và overlay với driver kernel của bạn; các chân trên là nhóm McASP0 chuẩn trên BBB.

### SPI0 — ILI9341 display

| ILI9341 Pin | BBB Pin | Notes |
|-------------|---------|-------|
| VCC | P9_3 | 3.3V |
| GND | P9_1 | Ground |
| CS | P9_17 | SPI0_CS0 |
| RESET | P9_12 | GPIO1_28 (gpio60) |
| DC / RS | P9_15 | GPIO1_16 (gpio48) |
| SDI (MOSI) | P9_18 | SPI0_D1 |
| SCK | P9_22 | SPI0_SCLK |
| LED / BL | P9_14 | GPIO1_18 (gpio50) |
| SDO (MISO) | P9_21 | SPI0_D0 (optional) |

### GPIO — Buttons and LED

| Function | BBB Pin | Linux GPIO | Notes |
|----------|---------|-----------|-------|
| PTT Button | P8_11 | gpio45 | Active LOW, 10kΩ pull-up |
| Vol + Button | P8_12 | gpio44 | Active LOW |
| Vol − Button | P8_14 | gpio26 | Active LOW |
| Status LED | P8_13 | gpio23 | Through 330Ω resistor |

---

## Device Tree overlay guidance

### SPI + GPIO overlay

Sử dụng overlay này để bật SPI0, cấu hình pin GPIO cho màn hình và nút bấm.

```dts
/dts-v1/;
/plugin/;

#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/pinctrl/am33xx.h>

&am33xx_pinmux {
	spi0_pins: pinmux_spi0 {
		pinctrl-single,pins = <
			AM33XX_PADCONF(AM335X_PIN_SPI0_SCLK, PIN_INPUT_PULLUP, MUX_MODE0)
			AM33XX_PADCONF(AM335X_PIN_SPI0_D0,   PIN_INPUT_PULLUP, MUX_MODE0)
			AM33XX_PADCONF(AM335X_PIN_SPI0_D1,   PIN_OUTPUT,       MUX_MODE0)
			AM33XX_PADCONF(AM335X_PIN_SPI0_CS0,  PIN_OUTPUT,       MUX_MODE0)
		>;
	};

	gpio_button_pins: pinmux_gpio_buttons {
		pinctrl-single,pins = <
			AM33XX_PADCONF(AM335X_PIN_GPMC_AD13, PIN_INPUT_PULLUP, MUX_MODE7)
			AM33XX_PADCONF(AM335X_PIN_GPMC_AD12, PIN_INPUT_PULLUP, MUX_MODE7)
			AM33XX_PADCONF(AM335X_PIN_GPMC_AD10, PIN_INPUT_PULLUP, MUX_MODE7)
		>;
	};

	gpio_led_pins: pinmux_gpio_led {
		pinctrl-single,pins = <
			AM33XX_PADCONF(AM335X_PIN_GPMC_AD9, PIN_OUTPUT, MUX_MODE7)
		>;
	};
};

&spi0 {
	status = "okay";
	pinctrl-names = "default";
	pinctrl-0 = <&spi0_pins>;

	ili9341: display@0 {
		compatible = "ilitek,ili9341";
		reg = <0>;
		spi-max-frequency = <32000000>;

		dc-gpios    = <&gpio1 16 GPIO_ACTIVE_HIGH>;
		reset-gpios = <&gpio1 28 GPIO_ACTIVE_LOW>;
		led-gpios   = <&gpio1 18 GPIO_ACTIVE_HIGH>;

		rotate = <0>;
		bgr;
		buswidth = <8>;
	};
};
```

### I2S / simple-audio-card overlay

Dùng overlay này để bật McASP/I2S và cấu hình audio path cho INMP441 + MAX98357A.

```dts
/dts-v1/;
/plugin/;

#include <dt-bindings/sound/simple-card.h>
#include <dt-bindings/pinctrl/am33xx.h>

&am33xx_pinmux {
	mcasp_pins: pinmux_mcasp {
		pinctrl-single,pins = <
			/* Thay bằng pad cụ thể cho McASP/I2S */
			AM33XX_PADCONF(AM335X_PIN_MCASP0_AHCLKR, PIN_OUTPUT, MUX_MODE0)
			AM33XX_PADCONF(AM335X_PIN_MCASP0_FSX,    PIN_OUTPUT, MUX_MODE0)
			AM33XX_PADCONF(AM335X_PIN_MCASP0_AXR0,   PIN_OUTPUT, MUX_MODE0)
			AM33XX_PADCONF(AM335X_PIN_MCASP0_AIR0,   PIN_INPUT,  MUX_MODE0)
		>;
	};
};

&mcasp0 {
	status = "okay";
	pinctrl-names = "default";
	pinctrl-0 = <&mcasp_pins>;
};

sound: simple-audio-card@0 {
	compatible = "simple-audio-card";
	simple-audio-card,name = "bbb-voice-i2s";
	simple-audio-card,format = "i2s";

	simple-audio-card,frame-length = <32>;

	simple-audio-card,cpu {
		sound-dai = <&mcasp0 0>;
	};

	simple-audio-card,codec {
		sound-dai = <&max98357a 0>;
	};
};

&max98357a {
	compatible = "maxim,max98357a";
	reg = <0>;
	sound-dai-format = "i2s";
};

&inmp441 {
	compatible = "invensense,inmp441";
	reg = <1>;
	sound-dai-format = "i2s";
};
```

> Điều quan trọng: cần điều chỉnh `AM335X_PIN_*` cho đúng pad MCU mà kernel hỗ trợ. Kiểm tra `Documentation/devicetree/bindings/sound/simple-audio-card.txt` và pinout BeagleBone Black.

---

## Buildroot / Kernel configuration suggestions

Thêm các tùy chọn sau hoặc bật tương đương trong `buildroot/board/bbb/linux.config`:

```
# SPI framework
CONFIG_SPI=y
CONFIG_SPI_MASTER=y
CONFIG_SPI_OMAP24XX=y

# Framebuffer driver for ILI9341
CONFIG_FB=y
CONFIG_FB_TFT=y
CONFIG_BACKLIGHT_CLASS_DEVICE=y

# ALSA and ASoC
CONFIG_SOUND=y
CONFIG_SND_SOC=y
CONFIG_SND_SOC_SIMPLE_CARD=y
CONFIG_SND_SOC_AM33XX_SOC_PCM=y

# Native I2S for INMP441 / MAX98357A
CONFIG_SND_SOC_MAX98357A=y

# GPIO char device for libgpiod
CONFIG_GPIOLIB=y
CONFIG_GPIO_CDEV=y
```

> Không cần cấu hình USB audio cho phần âm thanh. Dự án chỉ dùng audio qua McASP/I2S.
