# Timeline

> 4-week detailed plan for BBB Voice Assistant. Each day is ~2–4 hours of focused work.

---

## Week 1 — Foundation

**Goal:** BBB boots from a custom Buildroot image. All hardware verified working. Development environment on WSL2 ready.

### Day 1 — WSL2 + Buildroot Setup
- Install WSL2 Ubuntu 22.04, run `scripts/setup_wsl.sh`
- Download Buildroot 2024.02
- Copy `bbb_voiceassistant_defconfig`, run `make menuconfig` to explore
- Start first build (`make -j$(nproc)`) — leave running overnight
- **Deliverable:** Buildroot build starts without errors

### Day 2 — Flash and Boot BBB
- Flash `output/images/sdcard.img` to SD card
- Boot BBB, connect via UART serial (115200 baud) to observe boot log
- SSH in over Ethernet, verify hostname and IP
- Set up static IP (BBB + PC)
- **Deliverable:** `ssh root@192.168.1.50` works, network ping to PC works

### Day 3 — Device Tree + SPI Display
- Add device tree overlay for SPI0 + ILI9341 (from `hardware_setup.md`)
- Build DTS → DTB, deploy to BBB, enable in `uEnv.txt`
- Verify `/dev/fb1` exists: `ls /dev/fb*`
- Test display: `cat /dev/urandom > /dev/fb1` (shows color noise)
- **Deliverable:** ILI9341 display is alive as `/dev/fb1`

### Day 4 — GPIO Buttons + LED
- Enable GPIO chardev in kernel config (`CONFIG_GPIO_CDEV=y`)
- Test buttons with `gpioget gpiochip1 13` (PTT should read 1 when open, 0 when pressed)
- Test LED: `gpioset gpiochip0 23=1` (LED on), `gpioset gpiochip0 23=0` (LED off)
- Install `libgpiod` tools: `gpiomon gpiochip1 13` to watch events
- **Deliverable:** All 3 buttons and LED respond correctly via `gpioget/gpioset/gpiomon`

### Day 5 — USB Audio
- Plug in USB hub + USB sound card + USB mic
- Verify: `cat /proc/asound/cards` shows USB card (card index 1 typically)
- Record test: `arecord -D hw:1,0 -f S16_LE -r 16000 -d 3 test.wav`
- Playback test: `aplay -D hw:1,0 test.wav`
- Identify correct device string, update `config.json`
- **Deliverable:** Record + playback loop works at 16 kHz

### Day 6 — LM Studio + Network Verify
- Configure LM Studio on PC: listen on `0.0.0.0:1234`, load Whisper + LLM models
- From BBB: `curl http://<PC_IP>:1234/v1/models` → should list models
- Test STT: `curl -F file=@test.wav http://<PC_IP>:1234/v1/audio/transcriptions`
- Test chat: `curl -X POST .../v1/chat/completions -d '{"model":"...", "messages":[...]}'`
- **Deliverable:** Both LM Studio endpoints respond correctly from BBB

### Day 7 — Repo Structure + Buffer Day
- Initialize git repo, set up project directory structure (see PLAN.md)
- Set up `CMakeLists.txt` skeleton with toolchain file
- Write `scripts/deploy.sh`, test SCP pipeline
- Catch up on any Day 1–6 blockers
- **Deliverable:** Project structure committed, deploy script works

---

## Week 2 — HAL Layer

**Goal:** All three HAL implementations (Audio, Display, GPIO) working as shared libraries, tested with mocks.

### Day 8 — IAudioHAL Interface + AlsaHAL Skeleton
- Write `hal/include/IAudioHAL.hpp` (final interface from `architecture.md`)
- Implement `AlsaHAL::open_capture()` and `read_frames()` using ALSA PCM API
- Test: simple capture loop that writes a WAV file on BBB
- **Deliverable:** Can capture 3s of audio to `/tmp/test.wav` via `AlsaHAL`

### Day 9 — AlsaHAL Playback + Volume
- Implement `AlsaHAL::open_playback()`, `write_frames()`, `set_volume()`
- Test: playback WAV captured on Day 8 via `AlsaHAL`
- Handle ALSA underrun/overrun (`snd_pcm_recover`)
- **Deliverable:** Full ALSA record + playback cycle via HAL

### Day 10 — IDisplayHAL Interface + FbdevHAL
- Write `hal/include/IDisplayHAL.hpp`
- Implement `FbdevHAL::open()` (open `/dev/fb1`, mmap framebuffer)
- Implement `FbdevHAL::flush()` (write RGB565 data to mmap'd buffer)
- Test: draw solid color rectangle on display
- **Deliverable:** Can draw to ILI9341 via `FbdevHAL::flush()`

### Day 11 — IGPIOHAL Interface + GpiodHAL
- Write `hal/include/IGPIOHAL.hpp`
- Implement `GpiodHAL::open()`, `configure()`, `set()`, `get()`
- Implement `GpiodHAL::watch()` (spawns monitoring thread, calls callback on edge)
- Test: pressing PTT calls a lambda, LED toggles
- **Deliverable:** GPIO watch callback working for all 3 buttons

### Day 12 — CMake Shared Libraries
- Write `CMakeLists.txt` for each HAL module (`add_library(... SHARED ...)`)
- Verify cross-compilation produces correct `.so` files
- Verify `LD_LIBRARY_PATH` resolution on BBB
- **Deliverable:** `libhal_audio.so`, `libhal_display.so`, `libhal_gpio.so` deploy and load on BBB

### Day 13 — Mock HAL + Unit Tests
- Write `MockAudioHAL`, `MockDisplayHAL`, `MockGpioHAL` in `tests/hal/`
- Write tests: capture returns fake data, flush gets called with expected coords
- Set up host CMake build for test execution
- **Deliverable:** `ctest` passes on host without BBB hardware

### Day 14 — HAL Polish + Buffer Day
- Add error handling: log descriptive messages from HAL methods using spdlog
- Add `is_capture_ready()` and `is_playback_ready()` guards
- Review all HAL interfaces — freeze them (changes later = painful)
- **Deliverable:** All HAL methods have proper error paths and logging

---

## Week 3 — Middleware + Application

**Goal:** Full voice pipeline (record → STT → LLM → TTS) works. State machine transitions correctly. Basic LVGL UI visible.

### Day 15 — AudioPipeline
- Implement `AudioPipeline::record(duration_ms)` — calls `IAudioHAL`, encodes PCM → WAV in memory
- Implement `AudioPipeline::play(wav_bytes)` — writes WAV to `IAudioHAL`
- Write a simple WAV encoder (44-byte header + raw PCM) — no external library needed
- Test end-to-end: record 3s, play back the recording
- **Deliverable:** `AudioPipeline` record→play loop works on BBB

### Day 16 — AIClient (STT)
- Implement `AIClient::transcribe(wav_bytes)` with libcurl multipart POST
- Parse JSON response with nlohmann/json: `response["text"]`
- Test: send captured WAV → receive transcription text
- **Deliverable:** STT working: speech → English text

### Day 17 — AIClient (Chat)
- Implement `AIClient::chat(user_text)` with libcurl POST
- Maintain conversation history (`std::vector<Message>`) for context
- Parse `choices[0].message.content`
- Test: send "What is 2+2?" → verify response
- **Deliverable:** LLM chat working: text → AI response text

### Day 18 — TTSEngine
- Integrate eSpeak-ng library (`espeak_Initialize`, `espeak_TextToSpeech`)
- Implement `TTSEngine::synthesize(text)` → WAV bytes in memory
- Test: "Hello world" → play WAV via `AudioPipeline::play()`
- Tune voice parameters: `en` voice, speed=150, pitch=50
- **Deliverable:** TTS working: text → speech

### Day 19 — EventBus + Types
- Implement `EventBus` (subscribe/publish with mutex)
- Define all `EventType` values in `common/Types.hpp`
- Define `AppState` enum: `INIT, IDLE, LISTENING, PROCESSING, SPEAKING, CANCEL, ERROR`
- Write unit tests for EventBus
- **Deliverable:** EventBus tested and working

### Day 20 — VoiceAssistant State Machine
- Implement `VoiceAssistant` class with `handle_event(Event)` and state switch
- Wire up: PTT press → LISTENING, release → PROCESSING, etc.
- Connect to `AudioPipeline`, `AIClient`, `TTSEngine`
- Test without LCD: run full pipeline from command line, read logs
- **Deliverable:** Full voice interaction cycle works (no LCD yet)

### Day 21 — LVGL UI + DisplayController
- Set up LVGL: configure fbdev flush callback using `FbdevHAL`
- Implement `DisplayController` with 4 screens (idle, listening, processing, speaking)
- Each screen shows: status icon + status text + (for speaking) response text
- Wire `StateChanged` events from VoiceAssistant to DisplayController
- **Deliverable:** LCD shows correct status for each state during a full cycle

---

## Week 4 — Integration + Polish

**Goal:** Rock-solid end-to-end pipeline. Production-ready error handling. systemd service. Documentation complete.

### Day 22 — ButtonController + Full Integration
- Implement `ButtonController` (wraps `GpiodHAL`, publishes to EventBus)
- Wire PTT → VoiceAssistant, Vol+/Vol- → `IAudioHAL::set_volume()`
- Test full hardware loop: press PTT → speak → release → hear response → see LCD update
- **Deliverable:** Full hardware integration working end-to-end

### Day 23 — Error Handling
- Add timeout in LISTENING state (default 15s, configurable)
- Add timeout in PROCESSING state (network timeout via libcurl)
- Implement ERROR state: LED blink, LCD error message, auto-recovery to IDLE after 3s
- Handle: ALSA open fail, network unreachable, LM Studio down, eSpeak fail
- **Deliverable:** System recovers gracefully from all failure modes

### Day 24 — Config + Logging
- Implement `Config::load(path)` using nlohmann/json
- Replace all hardcoded values with config.json values
- Set up spdlog: rotating file logger (`/var/log/voiceassistant.log`) + console
- Add log levels: INFO for state transitions, DEBUG for audio frames, ERROR for failures
- **Deliverable:** All config is file-driven; logs are useful for debugging

### Day 25 — LED Feedback
- Map all states to LED behavior: IDLE=slow blink, LISTENING=fast blink, PROCESSING=on, SPEAKING=off, ERROR=3× rapid blink
- Implement LED control in `VoiceAssistant` on every state transition
- **Deliverable:** LED provides useful at-a-glance status

### Day 26 — systemd Service + Auto-start
- Install service file (from `buildroot/rootfs_overlay`)
- Test `systemctl start/stop/restart voiceassistant`
- Verify auto-start on boot with `systemctl enable voiceassistant`
- Verify journal logging: `journalctl -u voiceassistant`
- **Deliverable:** Service starts on boot and restarts on crash

### Day 27 — Performance + NFR Validation
- Measure and record: PTT release → TTS start latency (target < 5s)
- Measure: RAM usage (`/proc/self/status`, target < 200 MB)
- Measure: Boot time to IDLE state (target < 30s)
- Profile if any target missed; identify bottleneck (network? TTS? ALSA startup?)
- **Deliverable:** NFR-1, NFR-2, NFR-3 validated or blockers identified

### Day 28 — Documentation + Demo
- Update all docs to reflect final implementation (any deviations from plan)
- Record a demo video or write a README walkthrough
- Final review: troubleshooting.md covers all issues hit during development
- Tag `v1.0` in git
- **Deliverable:** Complete, documented, working V1 product

---

## Risk Register

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|-----------|
| fbtft driver unstable on kernel 5.x | Medium | High | Have spidev userspace fallback plan; check `staging/fbtft` patches |
| LM Studio STT latency > 3s | Medium | Medium | Reduce audio clip length; use faster Whisper model (tiny.en) |
| USB hub power issues on BBB | Low | High | Use powered USB hub; check BBB USB current limit (500mA) |
| ALSA USB device index changes on reboot | Low | Medium | Use ALSA device name (e.g., `plughw:CARD=Device`) instead of `hw:1,0` |
| WSL2 Buildroot build fails | Low | Medium | See `troubleshooting.md`; ensure project not on `/mnt/c/` |
