# BBB Voice Assistant — Project Plan

> **BeagleBone Black Voice Assistant** — An embedded Linux voice assistant device integrating a local AI server, displayed on a SPI TFT LCD.

---

## 📋 Overview


### 🎯 Goals
Build a complete embedded Linux device on BeagleBone Black with:
- **Voice Interaction:** Push-to-talk input,speech-to-text via a local Whisper server, chat completion via a local LLM (LM Studio).
- **Visual Feedback:** Status and responses on ILI9341 TFT LCD 320×240 (LVGL UI).
- **Audio Output:** Text-to-speech via eSpeak-ng, record and playback via USB audio.
- **Physical Controls:** PTT + Volume Up/Down buttons, 1 status LED.
- **Driver Development:** Framebuffer ILI9341 driver. (TinyDRM in the future if needed)
---

## 💎 Value

| Aspect | Value |
|--------|-------|
| **Learning** | Device driver utilization, HAL design, embedded Linux system programming |
| **Engineering** | Modern C++17, design patterns, embedded linux architecture |
| **Practical** | Production-quality code, error handling, logging, testing |
| **Extensible** | Easy to swap hardware, AI backend, or add new features |

---

## ⚙️ Technical Requirements

### ✅ Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-1 | Record audio while PTT button is held, or until FR-8 timeout | Must have |
| FR-2 | Speech-to-text via a local Whisper server (separate from LM Studio — see Section 5.2) | Must have |
| FR-3 | Chat completion with local LLM via LM Studio's OpenAI-compatible API | Must have |
| FR-4 | Text-to-speech (eSpeak-ng) + audio playback | Must have |
| FR-5 | Display status and responses on ILI9341 LCD | Must have |
| FR-6 | Volume control via Vol+/Vol- buttons (software PCM gain — most USB audio adapters have no hardware analog volume control) | Should have |
| FR-7 | LED status indicator (idle / active / error) | Should have |
| FR-8 | Hard recording timeout if PTT is held beyond a configurable max (default 15s) — replaces VAD-based auto-stop, which is out of scope | Must have |

### 📊 Non-Functional Requirements

| ID | Requirement | Target |
|----|-------------|--------|
| NFR-1 | Response time (PTT release → TTS audio starts) | < 5 seconds | Budget: network round-trip ~50ms, STT ~0.5-1.5s, LLM ~1-3s (depends on model size/PC), TTS synth ~0.2-0.5s. This is dominated by the PC, not the BBB — keep models small. |
| NFR-2 | RAM usage on BBB | < 200 MB | See Section 10 for the breakdown that makes this achievable. |
| NFR-3 | Boot time | < 30 seconds | Excludes network service availability — boot completing doesn't mean LM Studio/Whisper are reachable yet; app must handle "PC not ready" gracefully on startup. |
| NFR-4 | Audio recording quality | 16 kHz, 16-bit mono | Matches Whisper's native input rate — avoids a resampling step before STT. |
| NFR-5 | Max single recording length | 15 seconds (configurable) | Prevents unbounded memory growth and runaway recordings if PTT release is missed (e.g., GPIO bounce). |

### 🚧 Technical Constraints

| Constraint | Details |
|------------|---------|
| **Hardware** | BeagleBone Black Rev C (512 MB RAM, 4 GB eMMC, 16 GB SD card) |
| **OS** | Linux (kernel 5.10.x), official distro: https://www.beagleboard.org/distros/beaglebone-black-debian-12-14-2026-05-19-iot-v5-10-ti |
| **System Configuration** | Device Tree overlay, modify via `/boot/uEnv.txt` |
| **Network** | Ethernet (RJ45) on the same LAN/switch as the PC running LM Studio + Whisper server. (v1 had a contradiction here — decision log said "via USB"; resolved in favor of Ethernet for simplicity of static IPs and because BBB also needs general internet access for `apt`.) |
| **AI Server** | LM Studio on a separate PC (OpenAI-compatible chat completions API) |
| **STT Server** | A local Whisper server (`whisper.cpp` server or `faster-whisper-server`) on the same PC, separate process/port from LM Studio |
| **Display** | ILI9341 SPI TFT LCD, 320×240, RGB565 |
| **Audio** | USB audio |
| **Language** | English (STT + LLM + TTS) |
| **Dev Host** | Ubuntu 22.04 Virtual Machine (cross-compiling — see Section 11) |

---

## 🏗️ Architecture (Not decide)


## 🗂️ Project Structure (Not decide, below is for reference)
### 4.1 Layers

```
┌─────────────────────────────────────────────────────────┐
│  App Layer                                              │
│  StateMachine | EventBus | LVGL UI | ButtonController   │
├─────────────────────────────────────────────────────────┤
│  Middleware Layer                                       │
│  AudioPipeline | SttClient | LlmClient | TtsEngine      │
├─────────────────────────────────────────────────────────┤
│  HAL Layer (shared libs, mockable)                      │
│  AudioHAL (ALSA) | DisplayHAL (fbdev/DRM) | GpioHAL     │
├─────────────────────────────────────────────────────────┤
│  Common                                                 │
│  Logger (spdlog) | Config (nlohmann/json) | Types | EventBus core │
└─────────────────────────────────────────────────────────┘
```

`SttClient` and `LlmClient` are two separate middleware components (each a thin HTTP client) rather than one "AiClient" — they talk to two different servers on the PC with two different APIs. This is the direct fix for the LM-Studio-doesn't-do-STT gap.

### 4.2 Threading model (resolves the v1 "IPC / Event Bus — Not decide" row)

A single process, multiple threads, one in-process thread-safe queue (`EventBus`). No OS-level IPC (sockets/message queues) is needed since everything runs in one binary — the "IPC" framing in v1 was really about whether the event bus is callback-based or queue-based. **Decision: queue-based.** Callback chaining across threads (GPIO interrupt thread calling directly into LVGL, for instance) is a race-condition trap; a queue with a single consumer avoids that entirely.

| Thread | Owns | Behavior |
|--------|------|----------|
| **Main / Core** | StateMachine, LVGL, EventBus consumer | Loop: wait on queue with a short timeout (e.g. 5–10ms) → dispatch any event to the state machine → call `lv_timer_handler()` → repeat. **All LVGL calls happen only on this thread** — LVGL is not thread-safe, this is non-negotiable. |
| **GPIO thread** | `libgpiod` edge-wait loop for PTT, Vol+, Vol- | Debounces (~20-30ms), pushes `ButtonEvent` onto the queue. Blocking wait, low CPU. |
| **Audio capture thread** | ALSA capture loop | Runs only while in `LISTENING`; fills a buffer; pushes `RecordingComplete(buffer)` or `RecordingTimeout` (FR-8) onto the queue. |
| **Audio playback thread** | ALSA playback loop | Consumes TTS PCM; pushes `PlaybackComplete` onto the queue. |
| **Network worker thread(s)** | `SttClient`, `LlmClient` HTTP calls (libcurl) | Small thread pool (2 is enough); each call pushes a `SttResult` / `LlmResult` / `NetworkError` event onto the queue on completion. Never block the main thread on a network call. |

### 4.3 Sequence: one PTT interaction

```
PTT press   → GPIO thread → ButtonEvent(PTT_DOWN)         → StateMachine: IDLE → LISTENING
                                                              starts Audio capture thread
PTT release → GPIO thread → ButtonEvent(PTT_UP)            → StateMachine: LISTENING → PROCESSING
                                                              Audio capture thread → RecordingComplete
Network thread → SttClient.transcribe(buffer)               → SttResult(text)
Network thread → LlmClient.chat(text)                        → LlmResult(reply)
Main thread     → TtsEngine.synthesize(reply) → PCM          → StateMachine: PROCESSING → SPEAKING
Playback thread → plays PCM                                  → PlaybackComplete
                                                              → StateMachine: SPEAKING → IDLE
```

### 4.4 Display driver — decision gate (resolve in Week 1, Day 1)

- **(Decided)`fbtft`/`fb_ili9341` is available:** `/dev/fb0` is available and terminal appears on that directly, LVGL's fbdev backend works unmodified, and this matches the original plan (good learning value, simplest path).
- **(Future upgrade) DRM is available (likely on a 6.12.x kernel):** use the generic `panel-mipi-dbi-spi` driver via a Device Tree overlay, and switch LVGL to its DRM backend (dumb-buffer mmap) instead of fbdev. This is the actively-maintained path going forward.
- **(Future upgrade) Custom SPI Driver:** fall back to a custom `spidev` userspace driver (already listed as a future alternative) — more work, but it was already your stated fallback and gives full control.

---

## 5. 🌐 Network & Service Topology

| Component | Host | Port (example — confirm in each tool's docs) | Notes |
|-----------|------|------|-------|
| LM Studio server | PC | `1234` | OpenAI-compatible `/v1/chat/completions` |
| Whisper server | PC | e.g. `9000` (whisper.cpp server default differs by build) | Confirm exact endpoint path when you set it up — `whisper.cpp`'s bundled server and third-party OpenAI-shaped wrappers (`faster-whisper-server`) use different paths; don't assume `/v1/audio/transcriptions` works until you've tested it against your specific server. |
| BBB app | BeagleBone | — | Connects out to both via static LAN IPs |

Recommendations:
- Give the PC and BBB static IPs on the LAN (or DHCP reservations) so `config.json` doesn't need to change every reboot.
- Both LM Studio's and the Whisper server's APIs are unauthenticated by default — fine on an isolated LAN, but if the BBB or PC also has general internet access (per the constraints table), make sure these ports aren't reachable from outside your LAN (router NAT alone is usually enough; don't port-forward them).
- Add a connectivity check on app startup (and a visible "PC unreachable" state on the LCD) — NFR-3 only covers BBB boot time, not whether the PC's servers are up yet.

---

## 6. ⚖️ Technical Decisions

| Component | Choice | Key Reason | Future Alternative |
|-----------|--------|------------|--------------------|
| Audio | **ALSA (libasound)** | Embedded standard, direct HW access | PulseAudio / Pipewire |
| TTS | **eSpeak-ng** | Offline, lightweight, no server needed | Kokoro / Cloud TTS |
| LLM | **LM Studio** | Local, easy setup, OpenAI-compatible | Ollama / vLLM |
| STT | **whisper.cpp server / faster-whisper-server** (separate from LLM) | LM Studio has no transcription endpoint as of mid-2026 | Cloud STT (out of scope per privacy goal) |
| GUI | **LVGL** | Lightweight, production-proven | Qt Embedded |
| Display Driver | fbtft is available and it's working normally | Quick and simple | DRM driver, custom spidev driver |
| Event Bus | **In-process thread-safe queue, single consumer (main thread)** | Avoids cross-thread races into LVGL; simplest correct model for one process | — |
| JSON | **nlohmann/json** | Header-only, modern C++, readable | Protobuf (performance) |
| Logging | **spdlog** | Fast, async, modern C++ | syslog |
| GPIO | **libgpiod** | Modern chardev API, actively maintained | sysfs (legacy, deprecated) |
| HTTP | **libcurl** | Robust, well-tested for REST calls | cpp-httplib (header-only) |
| Network topology | **Ethernet (RJ45), static IPs** | Matches constraint table; simpler than USB gadget networking; one network for both LAN-to-PC and general internet | USB gadget Ethernet (192.168.7.x) if you want single-cable power+data |

---

## 7. 🗂️ Project Structure

```
bbb-voice-assistant/
├── CMakeLists.txt
├── README.md
├── toolchain/
│   └── bbb-toolchain.cmake           # cross-compile toolchain file (Section 11)
├── docs/
│   ├── PLAN.md                       # this file
│   ├── architecture.md               # records which display-driver path was chosen, and why
│   ├── timeline.md
│   ├── troubleshooting.md
│   └── development/
│       ├── coding_guide.md
│       ├── device_driver.md
│       ├── hal_layer.md
│       └── app_layer.md
│
├── hal/
│   ├── include/
│   ├── audio/
│   ├── display/                      # fbdev OR drm backend, per architecture.md decision
│   └── gpio/
│
├── middleware/
│   ├── audio_pipeline/
│   ├── stt_client/                   # NEW — talks to the local Whisper server
│   ├── llm_client/                   # renamed from "ai_client" — talks to LM Studio only
│   └── tts/
│
├── app/
│
├── common/
│   ├── Logger.hpp
│   ├── Config.hpp
│   ├── Types.hpp
│   └── EventBus.hpp                  # NEW — queue-based, see Section 4.2
│
├── config/
│   └── config.json                   # host/port for LM Studio + Whisper server, GPIO pin map, timeouts
│
├── scripts/
│   ├── build.sh
│   ├── deploy.sh                     # rsync/scp binary + restart systemd service
│   └── check_board.sh                # NEW — runs the Section 4.4 driver-availability checks
│
├── tests/
│   ├── hal/
│   ├── middleware/
│   └── app/
│
└── kernel/
    └── overlays/
        └── BBB-VOICE-ASSISTANT.dts   # device tree overlay — content depends on driver path chosen
```
---

## 8. State Machine

VAD removed (out of scope per your own decision log); replaced with the FR-8 hard timeout.

```
               ┌──────┐
  power on ──► │ INIT │
               └──────┘
                  │ hw init complete
                  ▼
               ┌──────┐
          ┌──► │ IDLE │ ◄─────────────────────────────┐
          │    └──────┘                               │
          │       │ PTT press                         │
          │       ▼                                   │
          │    ┌───────────┐                          │
          │    │ LISTENING │──── timeout (FR-8, 15s) ─┤
          │    └───────────┘                          │
          │       │ PTT release                       │
          │       ▼                                   │
          │    ┌────────────┐                         │
          │    │ PROCESSING │──── error ──────────────┤
          │    └────────────┘                         │
          │       │ AI response received              │
          │       ▼                                   │
          │    ┌──────────┐                           │
          │    │ SPEAKING │──── TTS complete ─────────┘
          │    └──────────┘
          │       │ PTT press (interrupt)
          │       ▼
          │    ┌────────┐
          └─── │ CANCEL │
               └────────┘
```

### Valid state transitions

| From | To (allowed) |
|------|--------------|
| INIT | IDLE, ERROR |
| IDLE | LISTENING, ERROR |
| LISTENING | PROCESSING, IDLE (timeout, FR-8), ERROR |
| PROCESSING | SPEAKING, IDLE (no response / network error), ERROR |
| SPEAKING | IDLE (TTS complete / interrupt), ERROR |
| ERROR | IDLE (restart app) |

---

## 9. Error Handling & Recovery

| Failure | Detection | Recovery |
|---------|-----------|----------|
| PC / LM Studio unreachable | HTTP connect timeout (set short, e.g. 3s) | State → ERROR → LED error pattern → LCD shows "AI server unreachable" → auto-retry on next PTT press, no app restart needed |
| Whisper server unreachable | Same as above, on the STT call | Same — surfaced distinctly from LLM errors so you can tell which service is down |
| eSpeak-ng process fails / not installed | Non-zero exit code or spawn failure | Log + fall back to a short error beep/LED pattern rather than silently hanging in SPEAKING |
| USB audio device disconnected mid-recording | ALSA write/read error | Abort recording, return to IDLE, LED error blink, log event |
| GPIO bounce / stuck PTT | FR-8 timeout | Auto-return to IDLE after max recording length |

---

## 10. 📐 Memory & Resource Budget (justifying NFR-2: <200MB)

| Component | Approx. RAM |
|-----------|-------------|
| Linux kernel + base Debian userspace | ~60–90 MB |
| App binary + shared libs (HAL/middleware loaded) | ~15–25 MB |
| LVGL framebuffer (320×240 × RGB565, double-buffered) | ~300 KB — negligible |
| Audio capture buffer (15s max @ 16kHz/16-bit mono) | ~480 KB — negligible |
| libcurl + network buffers | a few MB |
| eSpeak-ng (spawned per utterance, short-lived) | a few MB while running |
| **Headroom** | comfortably under 200MB total on a 512MB board, even with margin for kernel page cache |

This budget confirms NFR-2 is realistic without needing to do anything unusual — no AI model weights live on the BBB itself, which is the part that would actually threaten the budget.

---

## 11. Build & Deployment Strategy *(new — not in v1)*

Building natively on the BBB (single-core Cortex-A8 @ 1GHz, 512MB RAM) is slow and not recommended for iterative development.

1. **Cross-compile on the Ubuntu 22.04 VM.** Install an ARM cross toolchain matching the BBB's userspace (e.g. `gcc-arm-linux-gnueabihf` family, or BeagleBoard's published cross toolchain for the specific Debian image you're using).
2. **Build a sysroot** by copying `/usr/lib`, `/usr/include`, and relevant headers from the actual BBB (via `rsync` over the network) rather than guessing library versions — this avoids ABI mismatches with whatever libcurl/ALSA versions ship on the board.
3. **Write a CMake toolchain file** (`toolchain/bbb-toolchain.cmake`) setting `CMAKE_SYSTEM_NAME=Linux`, `CMAKE_SYSTEM_PROCESSOR=arm`, the cross compiler paths, and `CMAKE_FIND_ROOT_PATH` to the sysroot.
4. **`scripts/deploy.sh`** should `rsync`/`scp` the built binary + `config/config.json` to the BBB and restart the systemd service — don't hand-copy files during development, it doesn't scale past day 2.
5. **systemd service**, not a manual run script, for the final deliverable — gives you auto-restart on crash and clean boot integration, and is what NFR-3 (boot time) implicitly assumes.
---

## 12. ⚠️ Risk Register *(new — not in v1)*

| Risk | Impact | Mitigation |
|------|--------|------------|
| LM Studio has no native STT (confirmed during this review) | High — blocks FR-2 entirely if unaddressed | Already resolved by adding a separate Whisper server — but verify its exact API shape against your chosen build before writing `SttClient` |
| USB audio adapter doesn't support 16kHz natively | Medium | Test with `arecord -D plughw:CARD=... -f S16_LE -r 16000 -c 1` before any C++ is written; `plughw` resamples automatically, `hw` does not |
| Cheap USB audio adapter has no usable hardware volume control | Low-medium | FR-6 already planned for software PCM gain — confirm this in Week 1 with `amixer -c <card> scontrols` |
| LVGL called from a non-main thread by accident | Medium — intermittent, hard-to-debug crashes | Threading model in Section 4.2 enforces single-thread LVGL access by design; enforce in code review |
| BBB single-core CPU contention between LVGL redraw, audio I/O, and network | Low | All AI compute is offloaded to the PC; keep LVGL refresh rate modest (state-change-driven, not continuous animation) |

---

## 13. 📅 Timeline Overview

| Week | Phase | Deliverables |
|------|-------|-------------|
| **1** | Foundation | BBB boots from SD, SSH access, **display-driver decision gate resolved (Section 4.4)**, SPI/GPIO device tree, USB audio verified at 16kHz, Ethernet link to PC with static IPs, Whisper server + LM Studio both reachable from BBB via `curl` |
| **2** | HAL Layer | AudioHAL, DisplayHAL (per chosen driver path), GpioHAL as shared libs, CMake structure, cross-compile toolchain working end-to-end, mock-based unit tests |
| **3** | Middleware + App | AudioPipeline, SttClient, LlmClient, TtsEngine, EventBus, StateMachine, LVGL UI, ButtonController |
| **4** | Integration | Full end-to-end pipeline (PTT → STT → LLM → TTS → playback), error handling per Section 9, systemd service, final documentation |

(Day-by-day breakdown belongs in `docs/timeline.md` per your documentation index — Week 1 is the highest-risk week given the open driver/kernel question, so front-load the decision gate there rather than letting it slip into Week 2.)

---

## 14. ✅ Acceptance Criteria / Definition of Done

- Holding PTT and speaking a short question, then releasing, results in an audible spoken answer within NFR-1's 5-second budget, with the LCD reflecting INIT → IDLE → LISTENING → PROCESSING → SPEAKING → IDLE visibly at each stage.
- Disconnecting the PC (or stopping LM Studio/Whisper) produces a visible, non-crashing ERROR state with a clear LCD message, and recovers automatically on the next PTT press once services are back.
- Vol+/Vol- buttons audibly change TTS playback volume.
- The app survives a `systemctl restart` and a full power cycle without manual intervention.
- RAM usage measured on-device (`free -m`) stays under the 200MB NFR-2 target during a full PTT round trip.
- Unit tests (mock-based, per `tests/`) pass in CI/local build for HAL, middleware, and state machine logic.

---

## 15. 📚 Documentation Index

| Document | Content |
|----------|---------|
| [CLAUDE.md](CLAUDE.md) | Overview, requirements, architecture, decisions (this file) |
| [CHECK_LIST.md](CHECK_LIST.md) | Tracking current status |
| [architecture.md](architecture.md) | Interface definitions, class diagrams, thread model, **chosen display-driver path and why** |
| [env_setup.md](env_setup.md) | Setup environment, packages, dependencies, cross-compile toolchain |
| [timeline.md](timeline.md) | 4-week plan with daily tasks |
| [troubleshooting.md](troubleshooting.md) | Common issues and solutions |
| [development/coding_guide.md](development/coding_guide.md) | C++17 patterns, error handling, design patterns, logging, project conventions |
| [development/hal_layer.md](development/hal_layer.md) | HAL interface design, shared library, mock pattern |
| [development/app_layer.md](development/app_layer.md) | State machine, EventBus, LVGL UI, ButtonController |

---

## 16. Key Decisions Log

| # | Decision | Options | Chosen | Rationale |
|---|----------|---------|--------|-----------|
| 1 | GUI Framework + Backend | Raw FB/LVGL/Qt | **LVGL** | Lightweight, production-grade |
| 2 | Audio API + Codec | ALSA/PulseAudio | **ALSA + USB Audio** | Easy to use, embedded standard |
| 3 | Voice Trigger | Wake word/PTT | **Push-to-talk (PTT)** | Simpler, reliable, practical |
| 4 | AI Backend + Network | Cloud/Local + Eth/USB | **Local, via Ethernet (RJ45)** | Privacy; resolves v1's contradiction between the decision log (USB) and constraints table (Ethernet) in favor of simpler static-IP networking |
| 5 | TTS | eSpeak-ng/Flite/Remote | **eSpeak-ng** | Offline, lightweight, good enough |
| 6 | STT | None specified in v1 | **whisper.cpp server / faster-whisper-server, separate from LM Studio** | LM Studio has no transcription endpoint (confirmed during this review) |
| 7 | Serialization | Protobuf/JSON/CBOR | **JSON** | Human-readable, simple |
| 8 | GPIO API | sysfs/libgpiod | **libgpiod** | Modern, chardev, not deprecated |
| 9 | Event Bus | Callback chaining/queue | **In-process thread-safe queue, single consumer** | Avoids cross-thread races into LVGL (resolves v1's "Not decide") |
| 10 | LCD Driver | fbtft / DRM / custom spidev | **fbtft** | decided on hardware, it's working! |

---

## 17. 🚫 Out of Scope (Future Considerations)

- Wake word / hotword detection (PTT only for now)
- Voice Activity Detection (VAD) for auto-stop — explicitly removed from the state machine in this revision to match the PTT-only decision; PTT release + FR-8 timeout cover it
- Vietnamese or multi-language support (English only)
- Cloud AI integration (local only)
- OTA firmware updates