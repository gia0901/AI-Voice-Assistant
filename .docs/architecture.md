# Architecture

> Detailed design: interface definitions, class relationships, thread model, and design patterns used in BBB Voice Assistant.

---

## System Overview

```
┌───────────────────────────────────────────────────────────────┐
│  PC (Windows)                                                 │
│  ┌─────────────────────────────────┐                          │
│  │  LM Studio (HTTP :1234)         │                          │
│  │  ├─ /v1/audio/transcriptions    │  (Whisper — STT)         │
│  │  ├─ /v1/chat/completions        │  (LLM — chat)            │
│  │  └─ OpenAI-compatible REST API  │                          │
│  └─────────────────────────────────┘                          │
│             ▲                                                 │
│    Ethernet (LAN) — static IP                                 │
└───────────────────────────────────────────────────────────────┘
             │
┌────────────┼──────────────────────────────────────────────────┐
│  BeagleBone Black                                             │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐     │
│  │ APPLICATION LAYER                                    │     │
│  │                                                      │     │
│  │  ┌──────────────────┐  ┌───────────────────────┐     │     │
│  │  │  VoiceAssistant  │  │  DisplayController    │     │     │
│  │  │  (State Machine) │  │  (LVGL UI)            │     │     │
│  │  └────────┬─────────┘  └───────────────────────┘     │     │
│  │           │                                          │     │
│  │  ┌────────┴──────────────────────────────────────┐   │     │
│  │  │              EventBus                         │   │     │
│  │  └────────┬──────────────────────────────────────┘   │     │
│  │           │                                          │     │
│  │  ┌────────┴─────────┐  ┌───────────────────────┐     │     │
│  │  │  ButtonController│  │  (future components)  │     │     │
│  │  └──────────────────┘  └───────────────────────┘     │     │
│  └──────────────────────────────────────────────────────┘     │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐     │
│  │ MIDDLEWARE LAYER                                     │     │
│  │  AudioPipeline    AIClient (libcurl)   TTSEngine     │     │
│  └──────────────────────────────────────────────────────┘     │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐     │
│  │ HAL LAYER  (libhal_audio.so / display.so / gpio.so)   │     │
│  │  IAudioHAL         IDisplayHAL         IGPIOHAL      │     │
│  │  └─ AlsaHAL        └─ FbdevHAL         └─ GpiodHAL   │     │
│  └──────────────────────────────────────────────────────┘     │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐     │
│  │ KERNEL                                               │     │
│  │  ALSA (I2S: INMP441 + MAX98357A)  fbtft (ILI9341)     │     │
│  │  libgpiod                                            │     │
│  └──────────────────────────────────────────────────────┘     │
└───────────────────────────────────────────────────────────────┘
```

---

## HAL Interface Definitions

The HAL layer exposes pure abstract C++ interfaces. Concrete implementations live in separate shared libraries. The application and middleware layers depend only on these interfaces — never on concrete types.

### IAudioHAL.hpp

```cpp
#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <functional>

namespace hal {

struct AudioConfig {
    uint32_t sampleRate = 16000;
    uint16_t channels = 1;
    uint16_t bitsPerSample = 16;
    uint32_t framesPerPeriod = 512;
};

class IAudioHAL {
public:
    virtual ~IAudioHAL() = default;

    // --- Recording ---
    virtual bool openCapture(const AudioConfig& cfg) = 0;
    virtual bool startCapture() = 0;
    virtual bool readFrames(std::vector<int16_t>& buffer, uint32_t frames) = 0;
    virtual bool stopCapture() = 0;

    // --- Playback ---
    virtual bool openPlayback(const AudioConfig& cfg) = 0;
    virtual bool startPlayback() = 0;
    virtual bool writeFrames(std::span<const int16_t> data) = 0;
    virtual bool stopPlayback() = 0;
    virtual bool setVolume(uint8_t percent) = 0;  // 0–100

    virtual void close() = 0;
    virtual bool isCaptureReady() const = 0;
    virtual bool isPlaybackReady() const = 0;
};

} // namespace hal
```

`IAudioHAL` hides audio backend details and provides a uniform capture/playback API to the application. The implementation can map to ALSA I2S devices, direct PCM endpoints, or a mock backend for unit tests.

### IDisplayHAL.hpp

```cpp
#pragma once
#include <cstdint>
#include <span>

namespace hal {

struct DisplayConfig {
    uint16_t width = 320;
    uint16_t height = 240;
    uint8_t rotation = 0;       // 0, 90, 180, 270
    const char* fbDevice = "/dev/fb1";
};

class IDisplayHAL {
public:
    virtual ~IDisplayHAL() = default;

    virtual bool open(const DisplayConfig& cfg) = 0;

    // Raw framebuffer write — LVGL calls this via its flush callback.
    virtual bool flush(uint16_t x1, uint16_t y1,
                       uint16_t x2, uint16_t y2,
                       std::span<const uint16_t> rgb565Data) = 0;

    virtual bool setBacklight(uint8_t percent) = 0;  // 0–100
    virtual void close() = 0;
    virtual bool isReady() const = 0;
};

} // namespace hal
```

`IDisplayHAL` abstracts the framebuffer device and backlight control for LVGL. It separates render operations from the physical display driver and allows the UI layer to remain hardware-agnostic.

### IGPIOHAL.hpp

```cpp
#pragma once
#include <cstdint>
#include <functional>
#include <string_view>

namespace hal {

enum class GpioDirection { Input, Output };
enum class GpioEdge { Rising, Falling, Both };
enum class GpioLevel { Low = 0, High = 1 };

using GpioCallback = std::function<void(uint32_t gpioNum, GpioLevel level)>;

class IGPIOHAL {
public:
    virtual ~IGPIOHAL() = default;

    virtual bool open(std::string_view chipPath = "/dev/gpiochip1") = 0;

    virtual bool configure(uint32_t gpioNum,
                           GpioDirection direction,
                           GpioLevel initial = GpioLevel::Low) = 0;

    virtual bool set(uint32_t gpioNum, GpioLevel level) = 0;
    virtual GpioLevel get(uint32_t gpioNum) = 0;

    // Non-blocking event registration — callback runs in monitoring thread.
    virtual bool watch(uint32_t gpioNum,
                       GpioEdge edge,
                       GpioCallback callback) = 0;

    virtual bool unwatch(uint32_t gpioNum) = 0;
    virtual void close() = 0;
};

} // namespace hal
```

`IGPIOHAL` provides button and LED control through event callbacks and simple digital writes. It hides `libgpiod` details so the application can treat GPIO as events and state.

---

## Middleware Layer

### AudioPipeline

Manages the full record→encode→send flow. Owns an `IAudioHAL` reference and a WAV buffer.

```
AudioPipeline::recordAudio(durationMs)
  └─► IAudioHAL::readFrames() × N
  └─► encodes raw PCM to WAV (in-memory)
  └─► returns std::vector<uint8_t> (WAV bytes)

AudioPipeline::playAudio(wavBytes)
  └─► IAudioHAL::writeFrames()
```

`AudioPipeline` isolates audio logic from the application state machine, converting raw PCM into a format suitable for LM Studio and replaying TTS output through the I2S playback path.

### AIClient

Thin HTTP wrapper using libcurl. All methods are synchronous (called from worker thread).

```
AIClient::transcribeAudio(wavBytes) → std::string
  └─► POST /v1/audio/transcriptions  (multipart/form-data)

AIClient::createChatCompletion(prompt) → std::string
  └─► POST /v1/chat/completions  (JSON body)
  └─► Parses response.choices[0].message.content
```

`AIClient` separates REST API details from the voice assistant logic. It allows the app to focus on state transitions and message handling while the middleware handles HTTP serialization, retries, and response parsing.

### TTSEngine

Wraps eSpeak-ng. Produces WAV audio synchronously.

```
TTSEngine::synthesizeSpeech(text) → std::vector<uint8_t>
  └─► espeak_TextToSpeech() with wave output callback
  └─► collects samples into WAV buffer
```

`TTSEngine` abstracts text-to-speech details and produces a ready-to-play WAV byte stream. This keeps TTS generation independent from the playback implementation.

---

## Application Layer

### EventBus

Simple synchronous pub/sub. Events are dispatched on the publishing thread. Use with `std::mutex` if publishing from multiple threads.

```cpp
// Types.hpp
enum class EventType {
    ButtonPressed,     // payload: ButtonId
    ButtonReleased,
    VolumeChanged,     // payload: int (0–100)
    StateChanged,      // payload: AppState
    TranscriptionReady,// payload: std::string
    AIResponseReady,   // payload: std::string
    TTSComplete,
    Error              // payload: std::string (message)
};

struct Event {
    EventType type;
    std::variant<std::monostate, int, std::string, AppState> payload;
};
```

```cpp
// EventBus.hpp — minimal observer
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    void subscribe(EventType type, Handler handler);
    void publish(const Event& event);

private:
    std::unordered_map<EventType, std::vector<Handler>> handlers_;
    std::mutex mutex_;
};
```

### VoiceAssistant (State Machine)

`VoiceAssistant` owns the main pipeline, UI state, and response flow. It reacts to events from `EventBus`, manages transitions, and invokes middleware services.

Key methods include:
- `initialize()` — configure HALs, display, and audio devices.
- `run()` — drive the main state loop.
- `handleEvent()` — process button and network events.
- `transitionToState()` — enforce valid state changes.

Each state has an `enter()`, `handleEvent()`, and `exit()` method. Implemented as a flat switch statement in V1 for simplicity — see [development/app_layer.md](app_layer.md) for implementation guide.

### ButtonController

Runs a dedicated thread that calls `IGPIOHAL::watch()` for all 3 buttons and publishes events to the EventBus.

```
GPIO P8_11  →  PTT button   →  EventType::ButtonPressed / Released
GPIO P8_12  →  Vol+ button  →  EventType::VolumeChanged (+5%)
GPIO P8_14  →  Vol- button  →  EventType::VolumeChanged (-5%)
GPIO P8_13  →  Status LED   →  driven by VoiceAssistant state
```

---

## Thread Model

```
Main Thread
  └─► VoiceAssistant::run()  (state machine loop)
  └─► LVGL tick + task handler (every 5ms)

Worker Thread  (AudioPipeline)
  └─► ALSA record loop during LISTENING state
  └─► ALSA playback loop during SPEAKING state

Worker Thread  (ButtonController)
  └─► gpiod_line_event_wait() — blocks, wakes on GPIO edge
  └─► publishes to EventBus (thread-safe)

Worker Thread  (AIClient — optional async)
  └─► libcurl HTTP calls during PROCESSING state
```

All cross-thread communication goes through `EventBus` with a mutex. No shared mutable state outside of it.

This design minimizes priority inversion and keeps real-time audio and GPIO handling isolated from the application control flow.

---

## Design Patterns

| Pattern | Where Used | Why |
|---------|-----------|-----|
| **Abstract Interface** | HAL layer (`IAudioHAL`, etc.) | Decouple app from hardware |
| **Factory Function** | `HALFactory::create_audio()` | Swap real vs mock HAL at compile time |
| **Observer / EventBus** | All layers | Loose coupling between app components |
| **State Machine** | `VoiceAssistant` | Explicit, testable state transitions |
| **RAII** | All resources (ALSA handles, GPIO chip, fd) | Deterministic cleanup, no leaks |
| **Strategy** | `TTSEngine` can wrap different backends | Swap eSpeak-ng for Kokoro later |
| **Singleton (app-scoped)** | `Config`, `Logger` | Global access without passing everywhere |

---

## Key Data Flows

### Full Voice Interaction Cycle

```
[User presses PTT]
      │
      ▼
ButtonController ──EventType::ButtonPressed──► VoiceAssistant
      │
      ▼  (state → LISTENING)
AudioPipeline::recordAudio()  ←  IAudioHAL::readFrames()
      │
[User releases PTT]
      │
      ▼  (state → PROCESSING)
AIClient::transcribeAudio(wavBytes)  ──► LM Studio /v1/audio/transcriptions
      │
      ▼
AIClient::createChatCompletion(transcript) ──► LM Studio /v1/chat/completions
      │
      ▼  (state → SPEAKING)
TTSEngine::synthesizeSpeech(responseText)  ──► eSpeak-ng
      │
      ▼
AudioPipeline::playAudio(wavBytes)  ←  IAudioHAL::writeFrames()
      │
      ▼  (state → IDLE)
DisplayController updates LCD via LVGL
LED updated by VoiceAssistant on every state change
```

---

## config.json Structure

```json
{
  "ai": {
    "base_url": "http://192.168.1.100:1234",
    "model": "llama-3-8b-instruct",
    "whisper_model": "whisper-1",
    "max_tokens": 256,
    "timeout_ms": 10000
  },
  "audio": {
    "capture_device": "hw:1,0",
    "playback_device": "hw:1,0",
    "sample_rate": 16000,
    "max_record_ms": 15000
  },
  "display": {
    "fb_device": "/dev/fb1",
    "width": 320,
    "height": 240,
    "rotation": 0
  },
  "gpio": {
    "chip": "/dev/gpiochip1",
    "ptt_pin": 13,
    "vol_up_pin": 12,
    "vol_down_pin": 26,
    "led_pin": 23
  },
  "tts": {
    "voice": "en",
    "speed": 150,
    "pitch": 50
  },
  "volume": {
    "default": 70,
    "step": 5
  }
}
```

`capture_device` and `playback_device` should map to the ALSA I2S card exposed by the kernel overlay, for example `hw:0,0` or a card name assigned by the driver.

