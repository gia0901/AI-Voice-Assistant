# Troubleshooting

> Symptom → likely cause → fix, grouped by subsystem. English per Rule §18. Add a row whenever you lose more than ~20 minutes to something — future-you will hit it again.

---

## 1. Boot / SD card

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Board won't boot from SD | eMMC takes priority | Hold **S2 (boot)** button while powering on, or clear the eMMC MLO; verify with serial console on J1 (115200 8N1) |
| No serial output | Wrong baud / TX-RX swapped | 115200 8N1; FTDI GND→GND, board-TX→adapter-RX |
| Boots but no network | RJ45 link or static IP misconfig | `ip a`; check `/etc/network/interfaces` or netplan; confirm cable/switch |

---

## 2. Display (ILI9341 / fbtft)

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `/dev/fb0` missing after overlay | Overlay not loaded | `dmesg \| grep -i fb_ili9341`; confirm `uboot_overlay_addr0=...` in `/boot/uEnv.txt`; `ls /lib/firmware/BBB-VOICE-ASSISTANT.dtbo` |
| `dmesg` shows SPI but no panel | `compatible`/pins mismatch | Cross-check pin offsets in [BBB-VOICE-ASSISTANT.dts](../kernel/overlays/BBB-VOICE-ASSISTANT.dts) against P9 header; rebuild with `copy_dtbo.sh` |
| White / blank screen | RESET or DC wiring | RESET = gpio1_16 (active-low), DC = gpio1_17; verify continuity; check 3.3V not 5V |
| Garbled / shifted image | SPI too fast or wrong rotate | Lower `spi-max-frequency` (e.g. 16 MHz) to isolate; adjust `rotate` |
| Colors inverted | RGB/BGR or invert flag | Try `fbset`/driver `invert` option; ILI9341 panels vary |
| LVGL draws nothing but fb works | fbdev backend not pointed at `/dev/fb0` | Check LVGL fbdev init path + resolution 320×240 |

Quick fb test without the app:
```bash
cat /dev/urandom > /dev/fb0    # noise = fb works; Ctrl-C
```

---

## 3. Audio (ALSA / USB)

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `arecord` "Device or resource busy" | Another process holds the card | `fuser -v /dev/snd/*`; stop PulseAudio if present |
| Record fails at `hw:` but works at `plughw:` | Card has no native 16 kHz | Use `plughw` (auto-resamples) or capture native rate + resample (CLAUDE.md Risk Register) |
| No volume change from Vol± | No hardware mixer control | Expected — use software PCM gain (FR-6); confirm with `amixer -c <n> scontrols` |
| Crackling / xruns | Period/buffer too small or CPU contention | Increase ALSA period/buffer; keep LVGL refresh modest |
| Card index changes across reboots | USB enumeration order | Pin via `/etc/asound.conf` or a udev name; reference `CARD=` not `hw:1` |

Find the card:
```bash
arecord -l ; aplay -l ; cat /proc/asound/cards
```

---

## 4. GPIO / buttons / LED

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Button does nothing | Wrong chip/line or pin muxed away | `gpioinfo`; confirm line not claimed by another function in the overlay |
| Multiple events per press | Bounce | Debounce 20–30 ms in the GPIO HAL (CLAUDE.md §4.2) |
| PTT never releases | Stuck/bounce | FR-8 hard timeout returns to IDLE after 15 s |
| `Permission denied` on gpiochip | Not in `gpio` group | `sudo usermod -aG gpio $USER` or run via systemd with the right caps |

---

## 5. Network / AI services

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `curl` to LM Studio refused | Server bound to localhost only | In LM Studio enable "serve on local network"; use the PC's LAN IP not 127.0.0.1 |
| STT 404 / wrong shape | Endpoint path assumption | Confirm the exact path for *your* Whisper build (`whisper.cpp` server vs `faster-whisper-server` differ — CLAUDE.md §5) |
| Long stalls then failure | No connect timeout | Set short connect timeout (~3 s) in libcurl; surface as ERROR, retry on next PTT |
| BBB reaches PC but not internet | Routing/DNS over the wrong link | If on USB gadget only, enable IP-forwarding/NAT on the dev PC; prefer RJ45 for runtime |

---

## 6. Cross-compile / deploy

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `command not found: arm-...-gcc` | prepare.sh executed, not sourced | `source ./prepare.sh` |
| Link errors / missing `.so` | Sysroot incomplete or stale | Re-`rsync` `/usr/lib`,`/usr/include`,`/lib` from board (env_setup §3) |
| Runs on VM, `Exec format error` on BBB | Built native, not cross | Pass `-DCMAKE_TOOLCHAIN_FILE=...`; check `file build/<bin>` says ARM |
| `GLIBC_2.xx not found` on BBB | Toolchain glibc newer than board | Pin crosstool-ng glibc to the board's version |
| Binary deploys but won't start under systemd | Wrong WorkingDirectory / missing config | Check `journalctl -u bbb-voice-assistant`; ensure `config.json` deployed alongside |

---

## 7. Runtime crashes

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Random segfault under load | LVGL called off the main thread | Audit: only the main thread may call `lv_*` (architecture.md §3 invariant 1) |
| Use-after-free on PCM buffer | Buffer referenced after being moved into an event | Transfer ownership with `std::move`; never keep a copy of the pointer |
| Hang in SPEAKING | eSpeak spawn failed silently | Check exit code; fall back to error beep (CLAUDE.md §9) |
| RAM creeps over 200 MB | Buffer not freed / leak | `free -m` during a turn; valgrind/ASan on host build |
