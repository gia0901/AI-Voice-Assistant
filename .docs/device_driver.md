# Device Driver Guide

> ILI9341 SPI LCD driver on BeagleBone Black: the `fbtft` kernel driver approach (primary), the userspace `spidev` approach (fallback), and how LVGL connects to both.

---

## Overview: Two Driver Approaches

| Approach | How It Works | Pros | Cons |
|----------|-------------|------|------|
| **fbtft (primary)** | Kernel driver creates `/dev/fb1` | Standard fbdev API, LVGL compatible out-of-box | Requires kernel config + device tree; in `staging/` |
| **spidev (fallback)** | Userspace writes SPI commands directly | Full control, no kernel driver needed | Must implement ILI9341 init sequence manually |

**Use fbtft** unless the driver is unavailable in your kernel or proves unstable.

---

## ILI9341 Hardware Overview

The ILI9341 is a 320×240 RGB LCD controller. Key characteristics:

- **Interface:** SPI (mode 0 or 3, up to 32 MHz for display writes)
- **Color depth:** 16-bit RGB565 (5 red, 6 green, 5 blue bits per pixel)
- **Control signals:**
  - `CS` — chip select (active LOW)
  - `DC/RS` — data/command select (LOW = command, HIGH = data)
  - `RESET` — hardware reset (active LOW, pull HIGH for normal operation)
  - `BL/LED` — backlight (drive HIGH or connect to PWM for brightness control)

---

## Approach 1: fbtft Kernel Driver (Recommended)

### How fbtft Works

```
Application / LVGL
       │ write RGB565 to /dev/fb1 (mmap or write)
       ▼
fbtft kernel driver (ili9341.ko)
       │ DMA or PIO transfer over SPI
       ▼
am335x SPI controller (spi_omap24xx.ko)
       │ clocks: SCLK, MOSI, CS, DC (toggled by driver)
       ▼
ILI9341 controller
       │ drives pixel data to LCD panel
       ▼
Display (320×240)
```

The driver handles:
- ILI9341 initialization sequence (MADCTL, pixel format, sleep-out, display-on)
- Dirty region tracking — only flushes changed areas
- SPI transfers via the kernel SPI framework

### Enable in Buildroot Kernel Config

In `buildroot/board/bbb/linux.config`:
```
CONFIG_FB=y
CONFIG_FB_DEFERRED_IO=y
CONFIG_FB_SYS_FILLRECT=y
CONFIG_FB_SYS_COPYAREA=y
CONFIG_FB_SYS_IMAGEBLIT=y
CONFIG_FB_SYS_FOPS=y

# fbtft staging driver
CONFIG_STAGING=y
CONFIG_FB_TFT=y
CONFIG_FB_TFT_ILI9341=y

# Backlight (for BL pin control)
CONFIG_BACKLIGHT_CLASS_DEVICE=y
CONFIG_BACKLIGHT_GPIO=y
```

### Device Tree Node

The critical part is the DTS node under `&spi0`. See full overlay in `hardware_setup.md`. Key properties:

```dts
ili9341: display@0 {
    compatible = "ilitek,ili9341";
    reg = <0>;                          /* CS0 */
    spi-max-frequency = <32000000>;     /* 32 MHz — max for ILI9341 */
    spi-cpol;                           /* SPI mode 3 */
    spi-cpha;

    dc-gpios    = <&gpio1 16 GPIO_ACTIVE_HIGH>;  /* P9_15 = DC */
    reset-gpios = <&gpio1 28 GPIO_ACTIVE_LOW>;   /* P9_12 = RESET */
    led-gpios   = <&gpio1 18 GPIO_ACTIVE_HIGH>;  /* P9_14 = backlight */

    /* Display parameters */
    rotate = <0>;        /* 0, 90, 180, 270 */
    bgr;                 /* swap R/B — needed for most ILI9341 modules */
    fps = <30>;
    buswidth = <8>;
    debug = <0>;         /* set to 7 for verbose fbtft debug messages */
};
```

### Verify fbtft is Working

```bash
# After reboot with overlay:
dmesg | grep -i fbtft
# Expected: "fbtft: module loaded" and "fb1: ili9341 frame buffer device"

ls -la /dev/fb*
# Expected: /dev/fb0 (HDMI) and /dev/fb1 (ILI9341)

# Quick visual test — fills display with random colors
cat /dev/urandom > /dev/fb1

# Clear display to white
dd if=/dev/zero bs=153600 count=1 > /dev/fb1
# 320 × 240 × 2 bytes = 153600
```

### FbdevHAL Implementation

```cpp
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

bool FbdevHAL::open(const DisplayConfig& cfg) {
    fd_ = ::open(cfg.fb_device, O_RDWR);
    if (fd_ < 0) {
        LOG_ERROR("FbdevHAL: cannot open {}", cfg.fb_device);
        return false;
    }

    // Get display info
    fb_fix_screeninfo finfo;
    fb_var_screeninfo vinfo;
    ioctl(fd_, FBIOGET_FSCREENINFO, &finfo);
    ioctl(fd_, FBIOGET_VSCREENINFO, &vinfo);

    line_length_ = finfo.line_length;
    size_t fb_size = finfo.smem_len;

    // Memory-map the framebuffer
    fb_ptr_ = static_cast<uint16_t*>(
        mmap(nullptr, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));

    if (fb_ptr_ == MAP_FAILED) {
        LOG_ERROR("FbdevHAL: mmap failed");
        return false;
    }

    LOG_INFO("FbdevHAL: {}×{} @ {} bpp, fb_size={}",
             vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, fb_size);
    return true;
}

bool FbdevHAL::flush(uint16_t x1, uint16_t y1,
                     uint16_t x2, uint16_t y2,
                     std::span<const uint16_t> data) {
    const uint16_t* src = data.data();
    for (uint16_t y = y1; y <= y2; ++y) {
        uint16_t* dst = fb_ptr_ + y * (line_length_ / 2) + x1;
        std::memcpy(dst, src, (x2 - x1 + 1) * sizeof(uint16_t));
        src += (x2 - x1 + 1);
    }
    return true;
}
```

---

## Approach 2: spidev Userspace Driver (Fallback)

Use this if fbtft is not available or unstable on your kernel.

### Enable spidev in Device Tree

Replace the `ili9341` DTS node with:

```dts
spidev@0 {
    compatible = "linux,spidev";
    reg = <0>;
    spi-max-frequency = <32000000>;
    spi-cpol;
    spi-cpha;
};
```

This creates `/dev/spidev0.0`. You then control DC, RESET, and BL via libgpiod.

### ILI9341 Initialization Sequence

The display needs a sequence of commands to initialize. Key commands:

```cpp
// In FbdevHAL (spidev mode) — send command byte
void send_cmd(uint8_t cmd) {
    // DC = LOW (command)
    gpio_hal_->set(DC_PIN, GpioLevel::Low);
    spi_write(&cmd, 1);
}

// Send data byte(s)
void send_data(const uint8_t* data, size_t len) {
    // DC = HIGH (data)
    gpio_hal_->set(DC_PIN, GpioLevel::High);
    spi_write(data, len);
}

// Minimal init sequence
void ili9341_init() {
    // Hardware reset
    gpio_hal_->set(RESET_PIN, GpioLevel::Low);
    usleep(10000);  // 10ms
    gpio_hal_->set(RESET_PIN, GpioLevel::High);
    usleep(120000); // 120ms

    send_cmd(0x01);  // SWRESET — software reset
    usleep(150000);

    send_cmd(0x11);  // SLPOUT — sleep out
    usleep(120000);

    // Pixel format: 16-bit RGB565
    send_cmd(0x3A);
    send_data_byte(0x55);

    // Memory access control: row/col order, RGB/BGR
    send_cmd(0x36);
    send_data_byte(0x48);  // MX|BGR — adjust for your module

    send_cmd(0x29);  // DISPON — display on
    usleep(50000);
}

// Write a full framebuffer (320×240 × 2 bytes = 153600 bytes)
void ili9341_flush_full(const uint16_t* rgb565) {
    // Set column address (0 to 319)
    send_cmd(0x2A);
    uint8_t ca[] = {0x00, 0x00, 0x01, 0x3F};
    send_data(ca, 4);

    // Set row address (0 to 239)
    send_cmd(0x2B);
    uint8_t ra[] = {0x00, 0x00, 0x00, 0xEF};
    send_data(ra, 4);

    // Memory write
    send_cmd(0x2C);
    send_data(reinterpret_cast<const uint8_t*>(rgb565), 320 * 240 * 2);
}
```

> **Note:** spidev transfers are limited to `bufsiz` kernel bytes per ioctl (default 4096). For full-screen transfers (153600 bytes), call `ioctl(SPI_IOC_MESSAGE)` in chunks.

---

## Connecting LVGL to fbdev

LVGL needs two things: a **draw buffer** and a **flush callback**.

```cpp
#include "lvgl/lvgl.h"

// Draw buffer — at least width × 10 pixels recommended
static lv_color_t draw_buf_1[320 * 10];
static lv_color_t draw_buf_2[320 * 10];

// Flush callback — called by LVGL when a region needs to be redrawn
static void my_flush_cb(lv_display_t* disp,
                        const lv_area_t* area,
                        uint8_t* color_p) {
    // color_p contains RGB565 data for the area
    auto* hal = static_cast<IDisplayHAL*>(lv_display_get_user_data(disp));

    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;

    hal->flush(area->x1, area->y1, area->x2, area->y2,
               std::span<const uint16_t>(
                   reinterpret_cast<uint16_t*>(color_p), w * h));

    lv_display_flush_ready(disp);  // Tell LVGL flush is done
}

// Setup in DisplayController::init()
void DisplayController::init(IDisplayHAL* hal) {
    lv_init();

    lv_display_t* disp = lv_display_create(320, 240);
    lv_display_set_flush_cb(disp, my_flush_cb);
    lv_display_set_user_data(disp, hal);
    lv_display_set_buffers(disp, draw_buf_1, draw_buf_2,
                           sizeof(draw_buf_1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}
```

### LVGL Main Loop

```cpp
// In main thread, every ~5ms
while (running) {
    lv_tick_inc(5);           // Advance LVGL time
    lv_task_handler();        // Process LVGL events and render
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}
```

---

## SPI Frequency Notes

| Operation | Max Frequency |
|-----------|--------------|
| ILI9341 write (display) | 32 MHz (some modules: 40 MHz) |
| ILI9341 read | 6.67 MHz (much slower) |
| BBB SPI0 hardware max | 48 MHz |

Start with 16 MHz if you see display corruption, then increase to 32 MHz once stable.

In DTS: `spi-max-frequency = <16000000>;` → `<32000000>;`
