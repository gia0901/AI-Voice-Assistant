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
# Hardware Setup

> BeagleBone Black wiring, pin assignments, device tree overlay templates, and Buildroot kernel configuration for the BBB Voice Assistant.

This document merges recommended wiring and device-tree guidance for two audio approaches:
- USB audio (recommended for faster setup and reliable Linux support)
- Native I2S (INMP441 microphone + MAX98357A DAC/amp) when a compact, low-latency hardware path is required

---

## Component list

| Component | Part / Notes | Qty |
|-----------|-------------|-----|
| BeagleBone Black | Rev C, 512 MB RAM | 1 |
| TFT LCD Display | ILI9341, 320×240, SPI, 3.3V | 1 |
| USB Hub | Powered USB hub for peripherals | 1 |
| USB Sound Card (optional) | Any USB audio adapter (e.g. Syba SD-CM-UAUD) | 1 |
| USB Microphone (optional) | USB mic or USB headset (16 kHz capable) | 1 |
| I2S Microphone (optional) | INMP441 (digital I2S) | 1 |
| I2S DAC/Amp (optional) | MAX98357A (I2S in, class-D amp) | 1 |
| Speaker | Small 8Ω speaker (driven by MAX98357A) | 1 |
| Tactile Buttons | 6×6mm push button (through-hole) | 3 |
| Pull-up Resistors | 10 kΩ (1/4W) | 3 |
| LED | Any 3mm/5mm LED (status) | 1 |
| LED Resistor | 330 Ω (1/4W) | 1 |
| SD Card | 16 GB Class 10 microSD | 1 |
| Ethernet Cable | Cat5e/Cat6, BBB ↔ PC/router | 1 |
| Breadboard + Wires | Dupont jumpers (F-F and M-F) | — |

---

## Recommended approach

- Quick start / recommended: Use a USB microphone or USB sound card. Linux supports USB audio out-of-the-box (`snd-usb-audio`) and avoids device-tree and ASoC complexity.
- Advanced: Use INMP441 + MAX98357A on native I2S for a low-latency, compact hardware path. This requires Device Tree overlay and ASoC `simple-audio-card` configuration.

Choose USB audio to validate application flow quickly, then iterate to native I2S if you need lower latency or integrated audio routing.

---

## BBB pin mapping (examples)

Note: verify the exact P8/P9 header pins with the BeagleBone Black schematic for your revision before wiring.

### SPI0 — ILI9341 display (example)

| ILI9341 Pin | BBB Pin | BBB Signal | Notes |
|-------------|---------|------------|-------|
| VCC | P9_3 | 3.3V | Power (or 5V if your panel supports it) |
| GND | P9_1 | GND | Ground |
| CS | P9_17 | SPI0_CS0 | Chip select (CS0) |
| RESET | P9_12 | GPIO1_28 (gpio60) | Reset (active low) |
| DC / RS | P9_15 | GPIO1_16 (gpio48) | Data/Command |
| SDI (MOSI) | P9_18 | SPI0_D1 (MOSI) | MOSI |
| SCK | P9_22 | SPI0_SCLK | Clock |
| LED / BL | P9_14 | GPIO1_18 (gpio50) | Backlight (GPIO or PWM) |
| SDO (MISO) | P9_21 | SPI0_D0 (MISO) | Optional (readback) |

Backlight may be tied to 3.3V for always-on operation; using a GPIO/PWM gives brightness control.

### GPIO — Buttons and LED (example)

| Function | BBB Pin | Linux GPIO | Direction | Notes |
|----------|---------|-----------:|-----------|-------|
| PTT Button | P8_11 | gpio45 | Input | Active LOW, use 10kΩ pull-up to 3.3V |
| Vol + Button | P8_12 | gpio44 | Input | Active LOW, debounce in software |
| Vol − Button | P8_14 | gpio26 | Input | Active LOW |
| Status LED | P8_13 | gpio23 | Output | Drive through 330Ω resistor, HIGH = ON |

GPIO numbering note: Linux GPIO number = bank * 32 + pin (e.g. GPIO1_13 → 1*32+13 = 45).

### I2S (native audio) — signals (example)

If you use INMP441 + MAX98357A you need the McASP/I2S signals exposed from headers. Typical signals required:
- BCLK (bit clock)
- LRCLK / FS (frame sync / word select)
- TXD (I2S data out from BBB to DAC)
- RXD (I2S data in to BBB from mic)
- GND and 3.3V/5V power rails

Pin assignments for McASP vary by overlay and header muxing — check the BBB pinout and your device-tree mapping before wiring.

---

## Wiring diagram (conceptual)

BeagleBone Black P9 header → ILI9341 TFT

P9_1   (GND)    ───► GND
P9_3   (3.3V)   ───► VCC
P9_14  (GPIO50) ───► LED/BL
P9_15  (GPIO48) ───► DC/RS
P9_12  (GPIO60) ───► RESET
P9_17  (SPI0_CS0) ─► CS
P9_18  (SPI0_MOSI) ─► SDI (MOSI)
P9_21  (SPI0_MISO) ─► SDO (MISO)
P9_22  (SPI0_SCLK) ─► SCK

Buttons & LED (P8 header example):

3.3V (P9_3) --[10kΩ pull-up]--> each button input pin (P8_11, P8_12, P8_14) -- button --> GND
P8_13 (GPIO23) --[330Ω]--> LED --> GND

USB peripherals (hub) connected to BBB USB-A port: USB microphone or USB sound card, optionally USB speaker.

---

## Device Tree overlay examples

Below are two templates: (A) SPI + GPIO overlay (concrete example), and (B) I2S/simple-audio-card template (guide). Adjust pinmux entries to match your board and kernel headers.

**A — SPI0 + GPIO overlay (example)**

Save as `kernel/dts/bbb-voice-spi-overlay.dts` and build to `.dtbo`.

```dts
/dts-v1/;
/plugin/;

#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/pinctrl/am33xx.h>

&am33xx_pinmux {
	spi0_pins: pinmux_spi0 {
		pinctrl-single,pins = <
			AM33XX_PADCONF(AM335X_PIN_SPI0_SCLK,  PIN_INPUT_PULLUP, MUX_MODE0)
			AM33XX_PADCONF(AM335X_PIN_SPI0_D0,    PIN_INPUT_PULLUP, MUX_MODE0)
			AM33XX_PADCONF(AM335X_PIN_SPI0_D1,    PIN_OUTPUT,       MUX_MODE0)
			AM33XX_PADCONF(AM335X_PIN_SPI0_CS0,   PIN_OUTPUT,       MUX_MODE0)
		>;
	};

	gpio_button_pins: pinmux_gpio_buttons {
		pinctrl-single,pins = <
			/* Example entries; verify pad names for your revision */
			AM33XX_PADCONF(AM335X_PIN_GPMC_AD13, PIN_INPUT_PULLUP, MUX_MODE7)
			AM33XX_PADCONF(AM335X_PIN_GPMC_AD12, PIN_INPUT_PULLUP, MUX_MODE7)
			AM33XX_PADCONF(AM335X_PIN_GPMC_AD10, PIN_INPUT_PULLUP, MUX_MODE7)
		>;
	};

	gpio_led_pins: pinmux_gpio_led {
		pinctrl-single,pins = <
			AM33XX_PADCONF(AM335X_PIN_GPMC_AD9,  PIN_OUTPUT,       MUX_MODE7)
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

		dc-gpios    = <&gpio1 16 GPIO_ACTIVE_HIGH>;   /* P9_15 */
		reset-gpios = <&gpio1 28 GPIO_ACTIVE_LOW>;    /* P9_12 */
		led-gpios   = <&gpio1 18 GPIO_ACTIVE_HIGH>;   /* P9_14 */

		rotate = <0>;
		bgr;
		buswidth = <8>;
	};
};
```

Build with `dtc -@ -I dts -O dtb -o bbb-voice-spi-overlay.dtbo kernel/dts/bbb-voice-spi-overlay.dts` and copy to `/boot/dtbs/` on the BBB. Enable in `/boot/uEnv.txt`:

```bash
echo "uboot_overlay_addr0=/boot/dtbs/bbb-voice-spi-overlay.dtbo" >> /boot/uEnv.txt
reboot
```

**B — I2S / simple-audio-card (template & notes)**

Native I2S requires a `simple-audio-card` or custom ASoC machine binding. Exact pinmux and CPU node names depend on kernel and McASP naming. Use this as a starting template and adapt pins / `cpu` phandle to your kernel's mcasp node.

```dts
/* Example template — adjust pad names and mcasp phandle */
/dts-v1/;
/plugin/;

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

	/* CPU (mcasp) */
	simple-audio-card,cpu {
		sound-dai = <&mcasp0 0>;
	};

	/* Codec/device nodes (by role) */
	simple-audio-card,codec {
		sound-dai = <&max98357a 0>;
	};
};
```

Notes:
- You will likely need to create `max98357a` and `inmp441` nodes or use existing drivers that bind to the appropriate phandles.
- Check `Documentation/devicetree/bindings/sound/simple-audio-card.txt` in your kernel source for exact properties.

---

## Buildroot / Kernel configuration suggestions

Add the following (or ensure the equivalent menuconfig options) in your Buildroot kernel config fragment (`buildroot/board/bbb/linux.config`):

```
# SPI framework
CONFIG_SPI=y
CONFIG_SPI_MASTER=y
CONFIG_SPI_OMAP24XX=y

# Framebuffer driver for ILI9341 (fbtft may be external)
CONFIG_FB=y
CONFIG_FB_TFT=y
# If your kernel has a driver for ILI9341 enable it, otherwise use fbtft userspace drivers
CONFIG_BACKLIGHT_CLASS_DEVICE=y

# USB Audio (recommended for quick start)
CONFIG_SND=y
CONFIG_SND_USB_AUDIO=y

# ALSA
CONFIG_SOUND=y

# GPIO char device for libgpiod
CONFIG_GPIOLIB=y
CONFIG_GPIO_CDEV=y
CONFIG_GPIO_SYSFS=n

# Device tree and overlays
CONFIG_OF=y
CONFIG_OF_OVERLAY=y
CONFIG_OF_CONFIGFS=y
```

If you move to native I2S, ensure your kernel has McASP (omap) support and ASoC core and `simple-card` support enabled.

---

## Network and LM Studio connectivity

Use static IPs on the same LAN for predictable connectivity. Example `/etc/network/interfaces` snippet for `eth0` on the BBB:

```ini
auto eth0
iface eth0 inet static
	address 192.168.1.50
	netmask 255.255.255.0
	gateway 192.168.1.1
```

Configure LM Studio on the PC to listen on `0.0.0.0:1234` and enable "Local Network Access". Verify from BBB with:

```bash
curl http://192.168.1.100:1234/v1/models
```

---

## First-boot checklist

After flashing your image and booting the BBB, verify:

- [ ] BBB boots and SSH accessible: `ssh root@192.168.1.50`
- [ ] USB sound card detected: `cat /proc/asound/cards`
- [ ] Audio capture works (USB): `arecord -D hw:1,0 -f S16_LE -r 16000 test.wav`
- [ ] Audio playback works: `aplay test.wav`
- [ ] SPI display node present: `ls /dev/fb*` → `/dev/fb0` or `/dev/fb1`
- [ ] Display visual test: `cat /dev/urandom > /dev/fb1` (use with caution)
- [ ] GPIO buttons readable: `gpioget gpiochip1 13` (should be `1` when not pressed)
- [ ] GPIO LED works: `gpioset gpiochip0 23=1` (LED on)
- [ ] LM Studio reachable: `curl http://<PC_IP>:1234/v1/models`
- [ ] eSpeak-ng installed (optional): `espeak-ng "Hello" -w /tmp/test.wav`

---

## Notes & next steps

- For fastest development, test with USB audio first. Once app-level integration is stable, move to native I2S if required.
- When authoring a native I2S Device Tree overlay, validate pinmux names against your kernel's pinctrl driver and existing overlays (see `/lib/firmware` and `/proc/device-tree`).
- If you want, I can:
  - produce a ready-to-build `.dts` overlay for your exact BBB revision and chosen pins,
  - create a Buildroot fragment tailored to your kernel version,
  - or add wiring diagrams (SVG) for the prototype board.

---

File: 97_AI_Voice_Assistant/.docs/hardware_setup.md
