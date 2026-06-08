# BBB Voice Assistant — Project Plan

> **BeagleBone Black Voice Assistant** — An embedded Linux voice assistant device integrating a local AI server, displayed on a SPI TFT LCD.

---

## 📋 Overview


### 🎯 Goals
Build a complete embedded Linux device on BeagleBone Black with:
- **Voice Interaction:** Push-to-talk input, speech-to-text via local AI (Whisper in LM Studio).
- **Visual Feedback:** Status and responses on ILI9341 TFT LCD 320×240 (LVGL UI).
- **Audio Output:** Text-to-speech via eSpeak-ng, playback via native I2S hardware (INMP441 microphone + MAX98357A DAC/amp). USB audio is a fallback for prototyping.
- **Physical Controls:** PTT + Volume Up/Down buttons, 1 status LED.
- **Driver Development:** Self-made ILI9341 Driver, re-use Audio drivers of Linux kernel.
---

## 💎 Value

| Aspect | Value |
|--------|-------|
| **Learning** | Device driver dev, HAL design, embedded Linux system programming |
| **Engineering** | Modern C++17, design patterns, layered architecture |
| **Practical** | Production-quality code, error handling, logging, testing |
| **Extensible** | Easy to swap hardware, AI backend, or add new features |

---

## ⚙️ Technical Requirements

### ✅ Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-1 | Record audio while PTT button is held | Must have |
| FR-2 | Speech-to-text via LM Studio API (Whisper) | Must have |
| FR-3 | Chat completion with local LLM (LM Studio) | Must have |
| FR-4 | Text-to-speech (eSpeak-ng) + audio playback | Must have |
| FR-5 | Display status and responses on ILI9341 LCD | Must have |
| FR-6 | Volume control via Vol+/Vol- buttons | Should have |
| FR-7 | LED status indicator (idle / active / error) | Should have |

### 📊 Non-Functional Requirements

| ID | Requirement | Target |
|----|-------------|--------|
| NFR-1 | Response time (PTT release → TTS start) | < 5 seconds |
| NFR-2 | RAM usage | < 200 MB |
| NFR-3 | Boot time | < 30 seconds |
| NFR-4 | Audio recording quality | 16 kHz, 16-bit mono |

### 🚧 Technical Constraints

| Constraint | Details |
|------------|---------|
| **Hardware** | BeagleBone Black Rev C (512 MB RAM, 4 GB eMMC, 16 GB SD card) |
| **OS** | Linux (kernel 5.x), custom root filesystem via Buildroot |
| **Build System** | Buildroot — cross-compiled from WSL2 (Ubuntu 22.04 on Windows) |
| **Network** | Local LAN only — BBB ↔ PC via Ethernet (no internet required) |
| **AI Server** | LM Studio on separate PC (OpenAI-compatible REST API) |
| **Display** | ILI9341 SPI TFT LCD, 320×240, RGB565 |
| **Audio** | Native I2S: `INMP441` (I2S microphone) + `MAX98357A` (I2S DAC/amp). USB audio via `snd-usb-audio` is fallback for prototyping. |
| **Language** | English (STT + LLM + TTS) |
| **Dev Host** | Windows with WSL2 (Ubuntu 22.04) |

---

## 🏗️ Kiến trúc được chọn

### 📐 Layered Architecture với HAL

```
┌─────────────────────────────────────────────────────────┐
│                   APPLICATION LAYER                     │
│  VoiceAssistant    DisplayController   ButtonController │
│  (State Machine)   (LVGL UI)           (GPIO Events)    │
│                        EVENT BUS                        │
├─────────────────────────────────────────────────────────┤
│                   MIDDLEWARE LAYER                      │
│   AudioPipeline        AIClient          TTSEngine      │
├─────────────────────────────────────────────────────────┤
│               HAL LAYER  (shared libraries)             │
│  IAudioHAL           IDisplayHAL       IGPIOHAL         │
│  └─ AlsaHAL          └─ FbdevHAL       └─ GpiodHAL      │
├─────────────────────────────────────────────────────────┤
│                   KERNEL / DRIVERS                      │
│  ALSA (I2S: INMP441 + MAX98357A)    fbtft / ILI9341    libgpiod        │
└─────────────────────────────────────────────────────────┘
```
See [architecture.md](architecture.md) for full interface definitions, class diagrams, thread model, and design patterns.

### Gợi ý / Recommendations
- **Event Bus / IPC:** `POSIX message queue`. Low latency (~0.5ms)
- **Interface stability:** Define and document `IAudioHAL`, `IDisplayHAL`, `IGPIOHAL` (threading, blocking, error model).
- **Testing:** Provide `mock_HAL` implementations and unit tests for `VoiceAssistant` and middleware.
- **Docs:** Add sequence diagrams for main flows (PTT → STT → AI → TTS → Playback) in `docs/architecture.md`.
- **Versioning:** Add HAL semantic versioning to support gradual driver/impl upgrades.

### Why Layered Architecture?

| Criteria | Rationale |
|----------|-----------|
| **Separation of Concerns** | Each layer has one clear responsibility |
| **Testability** | Mock HAL enables unit tests without real hardware |
| **Portability** | Swapping hardware = only HAL changes |
| **Maintainability** | Easy to debug, easy to understand for newcomers |
| **Embedded fit** | Minimal overhead, no unnecessary framework |


## 🔩 Hardware

See [hardware_setup.md](hardware_setup.md) for full wiring diagrams, pin mapping, device tree overlay, and Buildroot kernel config.

---

## 🗂️ Project Structure

```
bbb-voice-assistant/
├── CMakeLists.txt                    # Top-level CMake
├── README.md
├── docs/                             # All documentation
│   ├── PLAN.md
│   ├── architecture.md
│   ├── hardware_setup.md
│   ├── build_system.md
│   ├── timeline.md
│   ├── troubleshooting.md
│   └── development/
│       ├── coding_guide.md
│       ├── device_driver.md
│       ├── hal_layer.md
│       └── app_layer.md
│
├── hal/                              # HAL Layer — compiled as shared libs
│   ├── include/
│   │   ├── IAudioHAL.hpp
│   │   ├── IDisplayHAL.hpp
│   │   └── IGPIOHAL.hpp
│   ├── audio/
│   │   ├── AlsaHAL.cpp
│   │   ├── AlsaHAL.hpp
│   │   └── CMakeLists.txt
│   ├── display/
│   │   ├── FbdevHAL.cpp
│   │   ├── FbdevHAL.hpp
│   │   └── CMakeLists.txt
│   └── gpio/
│       ├── GpiodHAL.cpp
│       ├── GpiodHAL.hpp
│       └── CMakeLists.txt
│
├── middleware/                       # Middleware Layer
│   ├── audio_pipeline/
│   │   ├── AudioPipeline.cpp
│   │   ├── AudioPipeline.hpp
│   │   └── CMakeLists.txt
│   ├── ai_client/
│   │   ├── AIClient.cpp
│   │   ├── AIClient.hpp
│   │   └── CMakeLists.txt
│   └── tts/
│       ├── TTSEngine.cpp
│       ├── TTSEngine.hpp
│       └── CMakeLists.txt
│
├── app/                              # Application Layer
│   ├── main.cpp
│   ├── VoiceAssistant.cpp
│   ├── VoiceAssistant.hpp
│   ├── DisplayController.cpp
│   ├── DisplayController.hpp
│   ├── ButtonController.cpp
│   ├── ButtonController.hpp
│   ├── EventBus.hpp
│   └── CMakeLists.txt
│
├── common/                           # Shared utilities
│   ├── Logger.hpp                    # spdlog wrapper
│   ├── Config.hpp                    # JSON config loader (nlohmann/json)
│   ├── Types.hpp                     # Shared enums, structs, constants
│   └── CMakeLists.txt
│
├── config/
│   └── config.json                   # Runtime configuration
│
├── scripts/
│   ├── setup_wsl.sh                  # WSL2 + Buildroot prerequisites
│   ├── build.sh                      # Full build (Buildroot + app)
│   └── deploy.sh                     # SCP binary to BBB
│
├── tests/
│   ├── CMakeLists.txt
│   ├── hal/                          # HAL unit tests (mock HAL)
│   ├── middleware/                   # Middleware tests
│   └── app/                          # State machine + logic tests
│
├── buildroot/
│   ├── configs/
│   │   └── bbb_voiceassistant_defconfig
│   └── board/bbb/
│       ├── rootfs_overlay/
│       │   └── etc/systemd/system/
│       │       └── voiceassistant.service
│       └── post_build.sh
│
└── kernel/
    └── dts/
        └── bbb-voiceassistant-overlay.dts
```

---

## State Machine

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
          │    │ LISTENING │──── timeout ─────────────┤
          │    └───────────┘                          │
          │       │ PTT release / VAD stop            │
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

  Any state ── critical error ──► ┌───────┐
                                  │ ERROR │──► IDLE (auto-recovery)
                                  └───────┘
```
### Valid State transitions
| From | To (allowed) |
|------|--------------|
| INIT | IDLE, ERROR  |
| IDLE | LISTENING, ERROR |
| LISTENING | PROCESSING, IDLE (timeout), ERROR |
| PROCESSING | SPEAKING, IDLE (no response), ERROR |
| SPEAKING | IDLE (TTS complete / interrupt), ERROR |
| ERROR | IDLE (restart app) |

---

## ⚖️ Technical Decisions

| Component | Choice | Key Reason | Future Alternative |
|-----------|--------|------------|--------------------|
| Build System | **Buildroot** | Simpler than Yocto, faster iteration | Yocto (production scale) |
| Audio | **ALSA (libasound)** | Embedded standard, direct HW access | PulseAudio / Pipewire |
| TTS | **eSpeak-ng** | Offline, lightweight, no server needed | Kokoro / Cloud TTS |
| AI / LLM | **LM Studio** | Local, easy setup, OpenAI-compatible | Ollama / vLLM |
| GUI | **LVGL** | Lightweight, fbdev backend, production-proven | Qt Embedded |
| Display Driver | **fbtft (ILI9341)** | Kernel-integrated, `/dev/fb1` for LVGL | Custom spidev userspace driver |
| JSON | **nlohmann/json** | Header-only, modern C++, readable | Protobuf (performance) |
| Logging | **spdlog** | Fast, async, modern C++ | syslog |
| GPIO | **libgpiod** | Modern chardev API, actively maintained | sysfs (legacy, deprecated) |
| HTTP | **libcurl** | Robust, well-tested for REST calls | cpp-httplib (header-only) |

---

## 📅 Timeline Overview

| Week | Phase | Deliverables |
|------|-------|-------------|
| **1** | Foundation | Buildroot image, BBB boot from SD, SSH, SPI/GPIO device tree, audio verify |
| **2** | HAL Layer | AudioHAL, DisplayHAL, GPIOHAL, shared libs, CMake structure, mock-based tests |
| **3** | Middleware + App | Audio pipeline, AI client, TTS engine, state machine, LVGL UI |
| **4** | Integration | Full end-to-end pipeline, error handling, systemd service, final documentation |

→ See [timeline.md](timeline.md) for detailed daily breakdown.

---

## 📚 Documentation Index

| Document | Content |
|----------|---------|
| [PLAN.md](PLAN.md) | Overview, requirements, architecture, decisions (this file) |
| [architecture.md](architecture.md) | Interface definitions, class diagrams, thread model, patterns |
| [hardware_setup.md](hardware_setup.md) | BBB pinout, wiring, device tree, Buildroot kernel config |
| [build_system.md](build_system.md) | Buildroot config, CMake cross-compile, WSL2 setup, deploy |
| [timeline.md](timeline.md) | 4-week plan with daily tasks |
| [troubleshooting.md](troubleshooting.md) | Common issues and solutions |
| [development/coding_guide.md](development/coding_guide.md) | C++17 patterns, error handling, logging, project conventions |
| [development/device_driver.md](development/device_driver.md) | ILI9341 driver, fbtft, SPI device tree configuration |
| [development/hal_layer.md](development/hal_layer.md) | HAL interface design, shared library, mock pattern |
| [development/app_layer.md](development/app_layer.md) | State machine, EventBus, LVGL UI, ButtonController |

---

## Key decisions log
| # | Decision | Options | Chosen | Rationale |
|---|----------|---------|--------|-----------|
| 1 | GUI Framework + Backend | Raw FB/LVGL/Qt | **LVGL + custom SPI driver** | Lightweight, production-grade, direct control of LCD |
| 2 | Audio API + Codec | ALSA/PulseAudio (± codec IC) | **ALSA + native I2S hardware** | Direct control, low-latency path with INMP441 and MAX98357A |
| 3 | Voice Trigger | Wake word/Push-to-talk (PTT) | **Push-to-talk (PTT)** | Simpler, reliable, practical |
| 4 | AI Backend + Network | Cloud/Local + Eth/USB | **Local (LM Studio) via USB** | Privacy, direct connection to PC |
| 5 | TTS | eSpeak-ng/Flite/Remote | **eSpeak-ng** | Offline, lightweight, good enough |
| 6 | Serialization | Protobuf/JSON/CBOR | **JSON** | Human-readable, simple |
| 7 | GPIO API | sysfs/libgpiod | **libgpiod** | Modern, chardev, not deprecated |
| 8 | IPC / Event Bus | Unix socket/mq/inproc | **POSIX message queue** | Low latency, simplicity |
| 9 | LCD Driver | Pre-built fbdev/Custom SPI | **Custom SPI + DMA** | Learning value, full control over ILI9341 |
| 10 | Audio Codec IC | No codec/I2C codec | **I2C-based codec IC** | Flexible, integrated solution, easier control |

## 🚫 Out of Scope (Future Considerations)
- DRM/KMS display driver (fbdev sufficient)
- Wake word / hotword detection (PTT only for now)
- Vietnamese or multi-language support (English only)
- Cloud AI integration (local only)
- Plugin / module architecture (monolithic app for now)
- OTA firmware updates
