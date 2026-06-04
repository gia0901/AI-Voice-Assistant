# Troubleshooting

> Common issues encountered during development, with causes and solutions.

---

## WSL2 / Build Issues

### Buildroot build fails with "permission denied" or symlink errors

**Cause:** Project is on the Windows filesystem (`/mnt/c/...`). NTFS does not support symlinks or case-sensitive filenames properly.

**Fix:** Move the project to the WSL2 native filesystem:
```bash
cp -r /mnt/c/projects/bbb-voice-assistant ~/bbb-voice-assistant
cd ~/bbb-voice-assistant
```

---

### `make: /bin/sh: not found` or strange locale errors in Buildroot

**Cause:** WSL2 shell may not be Bash by default, or locale is misconfigured.

**Fix:**
```bash
sudo apt install locales
sudo locale-gen en_US.UTF-8
export LANG=en_US.UTF-8
# Make sure /bin/sh → bash or dash (not Windows sh)
ls -la /bin/sh
```

---

### Cross-compiler not found after Buildroot build

**Cause:** Wrong path in `cmake/toolchain-bbb.cmake`.

**Fix:** Locate the actual compiler:
```bash
find ~/buildroot-upstream/output/host -name "arm-*-gcc" 2>/dev/null
```
Update `TOOLCHAIN_ROOT` in the toolchain file accordingly.

---

### `cmake` can't find `libasound` or `libgpiod` when cross-compiling

**Cause:** `pkg-config` is finding the host system libraries instead of the sysroot.

**Fix:** Set `PKG_CONFIG_SYSROOT_DIR` and `PKG_CONFIG_PATH`:
```bash
export PKG_CONFIG_SYSROOT_DIR=~/buildroot-upstream/output/host/arm-buildroot-linux-gnueabi/sysroot
export PKG_CONFIG_PATH=$PKG_CONFIG_SYSROOT_DIR/usr/lib/pkgconfig
cmake -B build-bbb -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-bbb.cmake ...
```

---

## BBB Boot / SSH Issues

### BBB won't boot from SD card

**Cause:** SD card not seated, or SD image wasn't flashed correctly, or Boot button not held.

**Fix:**
1. Hold the Boot button (S2, near SD slot) while powering on to force SD boot.
2. Re-flash: verify image size matches expected (`ls -lh output/images/sdcard.img`).
3. Check SD card with `fdisk -l /dev/sdX` — should show 2 partitions (boot + rootfs).

---

### Can't SSH to BBB — connection refused

**Cause:** OpenSSH not installed, network not configured, or wrong IP.

**Fix:**
1. Connect via UART serial first: `screen /dev/ttyUSB0 115200`
2. Check IP: `ip addr show eth0`
3. Verify sshd is running: `systemctl status sshd`
4. If missing: ensure `BR2_PACKAGE_OPENSSH=y` in Buildroot config, rebuild.

---

### BBB gets a random DHCP IP instead of static

**Cause:** `/etc/network/interfaces` or NetworkManager config not applied in image.

**Fix:** After SSH in, set static IP manually:
```bash
ip addr add 192.168.1.50/24 dev eth0
ip route add default via 192.168.1.1
# Then persist in /etc/network/interfaces
```

---

## Display (ILI9341 / fbtft) Issues

### `/dev/fb1` does not appear after reboot

**Cause:** Device tree overlay not loaded, or overlay has errors.

**Fix:**
1. Check if overlay is in `uEnv.txt`: `cat /boot/uEnv.txt | grep overlay`
2. Check kernel boot log for DT errors: `dmesg | grep -i fbtft`
3. Check the SPI bus is enabled: `dmesg | grep spi`
4. Rebuild DTB if DTS was modified: `dtc -@ -I dts -O dtb ...`

---

### Display shows garbage / wrong colors

**Cause:** Wrong SPI mode (CPOL/CPHA), incorrect rotation, or BGR vs RGB byte order.

**Fix:**
```
# In device tree, ILI9341 typically needs:
spi-cpol;
spi-cpha;
bgr;   # swap R/B channels if colors are inverted
```
If colors are inverted, toggle `bgr` in the DTS. If display is rotated, change `rotate = <90>` (values: 0, 90, 180, 270).

---

### LVGL display flickers or tears

**Cause:** LVGL flush callback writes to framebuffer without double-buffering; fbtft refreshes at fixed rate.

**Fix:** Use LVGL's `LV_DISPLAY_RENDER_MODE_PARTIAL` with a reasonably-sized draw buffer (at least `width × 10` pixels). Avoid full-screen refreshes for static UI elements.

---

### fbtft driver missing from Buildroot kernel

**Fix:** Add to `buildroot/board/bbb/linux.config`:
```
CONFIG_FB_TFT=y
CONFIG_FB_TFT_ILI9341=y
```
Then `make linux-rebuild` in Buildroot.

---

## Audio Issues

### `arecord` reports "Device or resource busy"

**Cause:** Another process already opened the ALSA device, or the device string is wrong.

**Fix:**
```bash
# List all audio devices
aplay -l
arecord -l
# Kill any other user of the device
fuser /dev/snd/*
# Use the correct hw string — the USB card index may not always be 1:
arecord -D plughw:CARD=Device,DEV=0 -f S16_LE -r 16000 test.wav
```

Use the card name (`plughw:CARD=Device`) in `config.json` instead of `hw:1,0` to avoid index changes across reboots.

---

### USB audio card not recognized

**Cause:** Missing `snd-usb-audio` kernel module.

**Fix:**
```bash
modprobe snd-usb-audio
# Check dmesg for USB device detection
dmesg | grep -i "usb audio"
# Ensure in Buildroot config:
# CONFIG_SND_USB_AUDIO=y
```

---

### Recording audio is very quiet / clipped

**Fix:** Use `alsamixer` to adjust capture levels:
```bash
alsamixer -c 1   # card 1 = USB
# Press F4 for capture, use arrow keys to adjust Mic gain
# Save: alsactl store
```
Or set via amixer:
```bash
amixer -c 1 sset Mic 80%
```

---

### eSpeak-ng produces no audio

**Cause:** eSpeak-ng synthesizes but `AudioPipeline::play()` fails silently.

**Debug steps:**
```bash
# Test eSpeak directly to file
espeak-ng "Hello world" -w /tmp/test.wav
# Then play with aplay
aplay -D hw:1,0 /tmp/test.wav
# If that works, bug is in TTSEngine → AudioPipeline handoff
```
Check WAV header format: eSpeak-ng produces 22050 Hz by default. Ensure `AudioPipeline::play()` opens the playback device with matching sample rate.

---

## GPIO / Button Issues

### `gpioget` returns wrong value (always 0 or always 1)

**Cause:** Missing pull-up resistor, or wrong GPIO number.

**Fix:**
1. Verify hardware: button between GPIO pin and GND, 10kΩ pull-up to 3.3V.
2. Verify GPIO number: `gpiochip1` is GPIO bank 1. Pin P8_11 = GPIO1[13] = offset 13 on gpiochip1.
3. Test: `gpioget gpiochip1 13` should return 1 (unpressed) and 0 (pressed).

---

### `gpiod::watch()` callback not firing

**Cause:** `CONFIG_GPIO_CDEV=y` not enabled in kernel, or using wrong edge type.

**Fix:**
```bash
# Check if chardev is available
ls /dev/gpiochip*
# Should see gpiochip0, gpiochip1, etc.
# Test with gpiomon:
gpiomon --rising-edge --falling-edge gpiochip1 13
# Press button — events should appear
```

---

### Permission denied accessing `/dev/gpiochip*`

**Fix:** Add user to `gpio` group, or run as root. In production (systemd service), the service runs as root, so no issue.
```bash
sudo usermod -aG gpio $USER
# Logout + login to apply
```

---

## Network / AI Issues

### `curl` to LM Studio returns "Connection refused"

**Cause:** LM Studio not configured to listen on external interfaces, firewall blocking port 1234.

**Fix:**
1. In LM Studio → Settings → Server → set "Host" to `0.0.0.0` (not `127.0.0.1`).
2. Check Windows Firewall: allow inbound TCP port 1234.
3. From BBB: `ping <PC_IP>` first, then `curl http://<PC_IP>:1234/v1/models`.

---

### STT transcription returns empty string or garbled text

**Cause:** Audio quality too low, wrong sample rate sent, or too-quiet recording.

**Fix:**
1. Increase mic gain (see Audio section above).
2. Verify WAV is 16 kHz, 16-bit mono before sending.
3. Try `whisper-tiny.en` model in LM Studio for faster, more consistent results.
4. Listen to captured WAV on PC to confirm speech is audible.

---

### LLM response takes > 5 seconds (NFR-1 miss)

**Cause:** Model too large for PC hardware, or network latency.

**Fix:**
1. Use a smaller / quantized model: `llama-3.2-1b-instruct.Q4_K_M.gguf` instead of 8B.
2. Reduce `max_tokens` in config.json (e.g., 128 instead of 256).
3. Enable GPU inference in LM Studio if a GPU is available.
4. Keep conversation history short (last 3–5 exchanges only).

---

## Application Issues

### App crashes on startup with `SIGSEGV` in HAL init

**Cause:** HAL not initialized before use, or shared library not found.

**Debug:**
```bash
# Check library loading
LD_LIBRARY_PATH=/opt/voiceassistant/lib ldd /opt/voiceassistant/voiceassistant

# Run with address sanitizer (rebuild in Debug with -fsanitize=address)
# Or use gdb on BBB:
gdb /opt/voiceassistant/voiceassistant
```

---

### State machine gets stuck in PROCESSING

**Cause:** AIClient HTTP call hanging (no timeout set), or response parsing fails.

**Fix:**
1. Set libcurl timeout: `CURLOPT_TIMEOUT_MS` = 10000 (10 seconds)
2. Log raw HTTP response before parsing
3. Verify LM Studio is returning valid JSON: `response["choices"][0]["message"]["content"]`
4. Add a PROCESSING timeout in VoiceAssistant (e.g., 15s force-return to IDLE)

---

### LVGL UI not updating / frozen

**Cause:** `lv_task_handler()` not being called regularly, or flush callback not working.

**Fix:**
1. Call `lv_tick_inc(5)` every 5ms from a timer or thread.
2. Call `lv_task_handler()` in the main loop regularly (at least every 50ms).
3. Verify `FbdevHAL::flush()` is being called by adding a log inside it.
