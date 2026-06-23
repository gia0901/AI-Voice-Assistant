# Hardware Setup — BBB Voice Assistant

> Single source of truth for **physical wiring**: every component, every pin, every net.
> Pin-mux offsets are relative to the control-module base `0x44e10800` + `0x800` window,
> i.e. the same numbers used in [`kernel/overlays/BBB-VOICE-ASSISTANT.dts`](../kernel/overlays/BBB-VOICE-ASSISTANT.dts).
> Verify pads against [`bbb_p8_header_pinout.md`](hw-docs/bbb_p8_header_pinout.md) /
> [`bbb_p9_header_pinout.md`](hw-docs/bbb_p9_header_pinout.md) before flashing an overlay.

**Status legend:** ✅ wired today · 🔜 planned (future touch upgrade — buttons-on-screen may replace some physical buttons)

---

## 1. Component inventory

| # | Component | Interface | Status | Notes |
|---|-----------|-----------|--------|-------|
| 1 | ILI9341 TFT LCD 320×240 (RGB565) | SPI0 + 2 GPIO | ✅ | fbtft `fb_ili9341`, `/dev/fb0` |
| 2 | XPT2046 resistive touch (same module) | SPI0 (shared) + 2 GPIO | 🔜 | `ads7846` input driver, PENIRQ interrupt |
| 3 | Push button — PTT | 1 GPIO (input, pull-up) | ✅ | libgpiod edge + debounce |
| 4 | Push button — Vol+ | 1 GPIO (input, pull-up) | ✅ | libgpiod |
| 5 | Push button — Vol− | 1 GPIO (input, pull-up) | ✅ | libgpiod |
| 6 | Status LED | on-board USR LED(s) | ✅ | No header pin used — see §7 |
| 7 | USB audio adapter | USB host | ✅ | Mic in + speaker out (ALSA) |

All logic is **3.3 V**. The BBB is **not 5 V tolerant** on GPIO — never feed 5 V into a pin.

---

## 2. Power distribution

| Rail | BBB source pins | Feeds |
|------|-----------------|-------|
| 3.3 V | P9_03, P9_04 (250 mA max total) | LCD `VCC`, LCD `LED` (backlight), XPT2046 `VCC` |
| GND | P9_01, P9_02 (+ P8_01/02, P9_43–46) | LCD `GND`, XPT2046 `GND`, all button GND legs |

> Backlight `LED` pin is tied straight to **3.3 V** (always on). PWM dimming is a future option
> (would need an EHRPWM pin, e.g. P9_14/P9_16) — out of scope now.

---

## 3. Master pin map

### 3.1 LCD + touch — SPI0 bus (✅ LCD now, 🔜 touch)

| Net | BBB pin | Signal (mode0) | GPIO line | Mux offset | Cfg byte | Direction | Owner |
|-----|---------|----------------|-----------|------------|----------|-----------|-------|
| SCK  / T_CLK | P9_22 | spi0_sclk | gpio0[2] | 0x150 | 0x10 | OUT, PU, MODE0 | spi0 |
| MOSI / T_DIN | P9_21 | spi0_d0   | gpio0[3] | 0x154 | 0x10 | OUT, PU, MODE0 | spi0 |
| MISO / T_DO  | P9_18 | spi0_d1   | gpio0[4] | 0x158 | 0x30 | IN,  PU, MODE0 | spi0 |
| LCD_CS  (cs0)| P9_17 | spi0_cs0  | gpio0[5] | 0x15c | 0x10 | OUT, PU, MODE0 | spi0 native CS0 |
| LCD_RESET    | P9_15 | gpmc_a0   | gpio1[16]| 0x040 | 0x0f | OUT, MODE7 | fbtft `reset-gpios` |
| LCD_DC/RS    | P9_23 | gpmc_a1   | gpio1[17]| 0x044 | 0x0f | OUT, MODE7 | fbtft `dc-gpios` |
| **T_CS**  🔜 | P8_07 | gpmc_advn_ale | gpio2[2] | 0x090 | 0x0f | OUT, MODE7 | spi0 **`cs-gpios`** (GPIO chip-select) |
| **T_IRQ** 🔜 | P8_08 | gpmc_oen_ren  | gpio2[3] | 0x094 | 0x37 | IN, PU, MODE7 | ads7846 **`interrupts`** (PENIRQ) |

> **Why GPIO chip-select for touch:** `spi0_cs1` is **not broken out** on the P9 header (only
> `spi0_cs0` exists; all other `cs1` pads belong to SPI1). So the second SPI device must use a
> GPIO as its chip-select — declared via `cs-gpios` on the `spi0` node. See §6.

### 3.2 Push buttons (✅)

| Net | BBB pin | Signal (mode0) | GPIO line | Mux offset | Cfg byte | Direction |
|-----|---------|----------------|-----------|------------|----------|-----------|
| BTN_PTT  | P8_09 | gpmc_be0n_cle | gpio2[5] | 0x09c | 0x37 | IN, **PU**, MODE7 |
| BTN_VOL+ | P8_10 | gpmc_wen      | gpio2[4] | 0x098 | 0x37 | IN, **PU**, MODE7 |
| BTN_VOL− | P8_11 | gpmc_ad13     | gpio1[13]| 0x034 | 0x37 | IN, **PU**, MODE7 |

Active-**low**: idle = High (internal pull-up), pressed = Low (shorted to GND).

> Cfg byte decode (per pinout bit table): `0x37` = RX-enable(in) + pull-up-select + pull-enabled + MODE7;
> `0x0f` = pull-disabled + MODE7 (output); `0x10`/`0x30` = MODE0 peripheral with pull-up.

### 3.3 GPIO ownership — kernel vs userspace

| Line | Used by | Mechanism | Accessible from app? |
|------|---------|-----------|----------------------|
| gpio0[2..5] | kernel (omap2-mcspi) | SPI0 bus | No |
| gpio1[16], gpio1[17] | kernel (fbtft) | reset/dc | No |
| gpio2[2] (T_CS) 🔜 | kernel (SPI core) | `cs-gpios` | No |
| gpio2[3] (T_IRQ) 🔜 | kernel (ads7846) | DT `interrupts` | No |
| **gpio2[5], gpio2[4], gpio1[13]** | **userspace** | **libgpiod** | **Yes — the 3 buttons** |

Only the three buttons are driven by the app (`GpioHAL` / libgpiod, see
[`architecture.md`](architecture.md) §4.2 GPIO thread). Touch CS/IRQ are owned by the kernel
drivers — the app reads touch through the LVGL evdev indev (`/dev/input/eventX`), not GPIO.

---

## 4. Bus topology

```
                         BeagleBone Black (3.3 V logic)
                        ┌──────────────────────────────┐
                        │           SPI0 bus            │
   ┌────────────────────┤  SCLK P9_22                   │
   │   ┌────────────────┤  MOSI P9_21                   │
   │   │   ┌────────────┤  MISO P9_18                   │
   │   │   │            │  CS0  P9_17 ─────────┐        │
   │   │   │            │  GPIO P8_07 (T_CS) ──┼──┐     │
   │   │   │            └──────────────────────┼──┼─────┘
   │   │   │  shared 3-wire bus                 │  │
   ▼   ▼   ▼                                    ▼  ▼
 ┌───────────────────────── ILI9341 + XPT2046 module ─────────────────────┐
 │  SCK  SDI  SDO ── LCD ── CS  RST  DC ── LED         (LCD @ 32 MHz)      │
 │   │    │    │           P9_17 P9_15 P9_23  └─3.3V                       │
 │   └────┴────┴─ touch ─ T_CLK T_DIN T_DO   T_CS    T_IRQ  (XPT2046 @ 2MHz)│
 │                                            P8_07   P8_08──► PENIRQ int   │
 └─────────────────────────────────────────────────────────────────────────┘
```

- The display and touch **share SCLK/MOSI/MISO**; they differ only by chip-select.
- Each SPI child sets its own `spi-max-frequency` — **LCD 32 MHz, touch ≤ 2 MHz** (XPT2046 limit
  ~2.5 MHz). The McSPI controller reprograms the clock per transfer, so the mismatch is safe.
- The SPI core serializes bus access, so simultaneous fbtft refresh + touch read cannot corrupt
  each other (only minor latency under heavy redraw — acceptable for a state-change-driven UI).

---

## 5. Push-button wiring (4-pin tactile switch)

A 4-pin tact switch is **two pairs**: the two legs on the **same side are permanently joined**
inside; pressing bridges the two sides.

```
   ┌─ A ──●●── B ─┐        A══B  (always connected, internally)
   │   [ button ] │        C══D  (always connected, internally)
   └─ C ──●●── D ─┘        press → A/B  ──short──  C/D
```

- **Electrically 2 pins suffice** — pick **one from each side** (diagonal A↔D is foolproof).
- ⚠️ Picking two **same-side** legs (A+B) = permanent short → "always pressed".
- Soldering **all 4** changes nothing electrically; it only adds **mechanical rigidity**
  (4 anchor points). If you do, wire one side to the GPIO, the other side to GND.

**Per-button circuit** (internal pull-up, no external resistor needed):

```
   3.3V ──[ internal pull-up ]── GPIO pin ──●  (one switch side)
                                            │
                                          (switch)
                                            │
   GND ─────────────────────────────────────●  (other side)
```

Idle = High, pressed = Low. Debounce 20–30 ms in software (GPIO thread); an optional 100 nF cap
across the leads adds hardware debounce but is not required.

---

## 6. Device-tree changes for touch (🔜 — guidance, not a finished overlay)

> Per project rule §18, the overlay is hand-written by Gia; this is the skeleton + the decisions,
> not a copy-paste solution.

When adding touch to [`BBB-VOICE-ASSISTANT.dts`](../kernel/overlays/BBB-VOICE-ASSISTANT.dts):

1. **Pin-mux fragment** — add a group for `T_CS` (gpio2[2], `0x090 0x0f`) and `T_IRQ`
   (gpio2[3], `0x094 0x37`). Buttons get their own group (`0x09c/0x098/0x034`, all `0x37`)
   referenced by a `gpio-keys` node or left for libgpiod.
2. **`spi0` node** — add `cs-gpios = <0 &gpio2 2 GPIO_ACTIVE_LOW>;`
   - index 0 = `<0>` → keep **native CS0** for the display (`display@0`, `reg = <0>`).
   - index 1 = the GPIO → touch uses `reg = <1>`.
3. **Add child `touch@1`** under `spi0`:
   - `compatible = "ti,tsc2046"` (XPT2046 is ADS7846-compatible).
   - `reg = <1>`, `spi-max-frequency = <2000000>`.
   - `interrupt-parent = <&gpio2>; interrupts = <3 IRQ_TYPE_EDGE_FALLING>;` (PENIRQ on gpio2[3]).
   - `pendown-gpio = <&gpio2 3 GPIO_ACTIVE_LOW>;`
   - touch axis / calibration props (`ti,x-min`, `ti,pressure-max`, …) per the `ads7846` binding.
4. **LVGL** consumes touch via the **evdev** indev backend reading `/dev/input/eventX`; plan a
   calibration step (tslib or LVGL's own) since resistive panels need it.

The **display node and its pins stay unchanged** — adding touch is purely additive.

---

## 7. Status LED (FR-7)

Use the **on-board USR LEDs** (USR0–USR3 = gpio1[21..24]) instead of an external LED — no header
pin consumed. They expose `/sys/class/leds/beaglebone:green:usr*` with triggers. Suggested mapping:

| State | Pattern |
|-------|---------|
| IDLE | heartbeat / off |
| LISTENING | solid on |
| PROCESSING | blink |
| ERROR | fast blink |

An external LED on a spare P8 pin (e.g. P8_12 gpio1[12], `0x030`) + series resistor (~330 Ω) is a
future option if a dedicated indicator is wanted.

---

## 8. Free pins remaining (for future expansion)

Clean GPIOs still unused (not eMMC, not HDMI): P8_12, P8_13, P8_14, P8_15, P8_16, P8_17, P8_18,
P8_19, P8_26. Plenty of headroom if more controls are added later.

---

## 9. Bring-up verification checklist

- [ ] `/dev/fb0` present, `fbset` reports 320×240 — LCD up (✅ already).
- [ ] `ls /dev/spidev0.*` — confirm SPI0 children enumerated.
- [ ] After touch overlay: `/dev/input/eventX` appears; `evtest` shows X/Y on touch.
- [ ] `gpioinfo` — confirm the 3 button lines are free (not "used") and on the expected chips.
- [ ] `gpiomon gpiochip2 5` (PTT) toggles Low on press.
- [ ] No two pads share the same mux offset across all fragments.
- [ ] USB audio: `arecord -D plughw:CARD=...,DEV=0 -f S16_LE -r 16000 -c 1 test.wav` (NFR-4).
