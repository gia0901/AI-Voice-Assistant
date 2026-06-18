# Architecture

> Companion to [CLAUDE.md](../CLAUDE.md). CLAUDE.md is the *what/why* (decisions); this file is the *how* (interfaces, classes, threads, data flow). Written in English per Rule §18.

---

## 1. Layered overview

The system is one process, split into four layers. Dependencies point **downward only** — an upper layer may call the layer directly below it, never the reverse. This keeps the HAL mockable and the middleware testable without hardware.

```
┌──────────────────────────────────────────────────────────────┐
│ App Layer                                                     │
│   StateMachine · ButtonController · LvglUi                    │
│   (consumes EventBus, drives middleware, owns the UI)         │
├──────────────────────────────────────────────────────────────┤
│ Middleware Layer                                             │
│   AudioPipeline · SttClient · LlmClient · TtsEngine          │
│   (policy/orchestration; no raw syscalls, no LVGL)           │
├──────────────────────────────────────────────────────────────┤
│ HAL Layer (shared libs, mockable)                           │
│   IAudioHal(ALSA) · IDisplayHal(fbdev) · IGpioHal(libgpiod)  │
│   (the only code that touches device nodes)                  │
├──────────────────────────────────────────────────────────────┤
│ Common                                                      │
│   Logger(spdlog) · Config(nlohmann/json) · Types · EventBus  │
└──────────────────────────────────────────────────────────────┘
```

**Rule of thumb for placement:** if it touches `/dev/*` it lives in HAL; if it makes a decision about *what to do next* it lives in App; everything that transforms data between those two (resampling, HTTP, JSON shaping, PCM synthesis) is middleware.

---

## 2. Component responsibilities & interfaces

Interfaces below are the **contract**, not final code. All HAL interfaces are pure-virtual so a mock can replace them in unit tests (see [development/hal_layer.md](development/hal_layer.md)).

### 2.1 HAL Layer

```cpp
// hal/include/IAudioHal.hpp
class IAudioHal {
public:
    virtual ~IAudioHal() = default;
    // Capture: blocking read of one period; returns frames read or <0 on error.
    virtual int  openCapture(const AudioFormat& fmt) = 0;   // 16k/S16_LE/mono
    virtual int  readPeriod(int16_t* dst, size_t frames) = 0;
    virtual void closeCapture() = 0;
    // Playback
    virtual int  openPlayback(const AudioFormat& fmt) = 0;
    virtual int  writePeriod(const int16_t* src, size_t frames) = 0;
    virtual void drain() = 0;       // block until buffer emptied
    virtual void closePlayback() = 0;
    // Software volume (FR-6): linear gain 0.0–1.0 applied before writePeriod
    virtual void setVolume(float gain) = 0;
};
```

```cpp
// hal/include/IDisplayHal.hpp  (fbdev backend, per Decision #10)
class IDisplayHal {
public:
    virtual ~IDisplayHal() = default;
    virtual int  init(const char* fbdev) = 0;      // mmap /dev/fb0
    virtual void flush(int x1, int y1, int x2, int y2,
                       const uint16_t* px) = 0;     // RGB565 region → fb
    virtual int  width()  const = 0;
    virtual int  height() const = 0;
};
```

```cpp
// hal/include/IGpioHal.hpp  (libgpiod chardev)
enum class ButtonId { Ptt, VolUp, VolDown };
enum class Edge     { Press, Release };
struct GpioEvent { ButtonId id; Edge edge; };

class IGpioHal {
public:
    virtual ~IGpioHal() = default;
    virtual int  init(const GpioPinMap& pins) = 0;
    // Blocking edge-wait with timeout; out-param filled on event.
    virtual bool waitEvent(GpioEvent& out, int timeoutMs) = 0;
    virtual void setLed(bool on) = 0;
};
```

### 2.2 Middleware Layer

| Component | Owns | Public surface (sketch) |
|-----------|------|--------------------------|
| `AudioPipeline` | capture buffer assembly, FR-8 timeout, software gain | `start()`, `stop() -> PcmBuffer`, `applyGain(float)` |
| `SttClient` | HTTP to Whisper server (libcurl) | `transcribe(PcmBuffer) -> Result<std::string>` |
| `LlmClient` | HTTP to LM Studio `/v1/chat/completions` | `chat(std::string) -> Result<std::string>` |
| `TtsEngine` | spawn eSpeak-ng, capture PCM | `synthesize(std::string) -> Result<PcmBuffer>` |

`Result<T>` is a small `std::variant<T, Error>` wrapper (see [development/coding_guide.md](development/coding_guide.md)). Network/IO functions **never throw across thread boundaries** — they return `Result` and the worker pushes a typed event onto the bus.

### 2.3 App Layer

| Component | Responsibility |
|-----------|----------------|
| `StateMachine` | The 7-state FSM from CLAUDE.md §8. Single owner of "what state are we in". Pure logic — no IO; it asks middleware to act and waits for events. |
| `ButtonController` | Maps raw `GpioEvent` → semantic intent (PTT hold start/stop, volume step) with debounce already applied in the GPIO HAL. |
| `LvglUi` | Builds the LVGL screen objects; exposes `showState(State)`, `showText(std::string)`, `showError(std::string)`. **Only ever called from the main thread.** |

---

## 3. Threading model (detail of CLAUDE.md §4.2)

One consumer, many producers. The only shared mutable structure is the `EventBus` queue, which is internally locked. Nothing else is shared across threads — buffers are *moved* through events, not referenced.

```
   producers (push)                         consumer (pop)
 ┌────────────────┐
 │ GPIO thread    │──ButtonEvent────┐
 ├────────────────┤                 │
 │ Capture thread │──Recording*─────┤      ┌─────────────────────────┐
 ├────────────────┤                 ├────► │ EventBus (mutex + cv)   │
 │ Playback thread│──PlaybackDone───┤      └───────────┬─────────────┘
 ├────────────────┤                 │                  │ pop(timeout)
 │ Net workers ×2 │──Stt/Llm/Err────┘                  ▼
 └────────────────┘                          ┌───────────────────────┐
                                             │ Main thread           │
                                             │  loop:                │
                                             │   ev = bus.pop(10ms)  │
                                             │   if ev: fsm.handle() │
                                             │   lv_timer_handler()  │
                                             └───────────────────────┘
```

**Invariants (enforce in code review — see Risk Register):**
1. `lv_*` calls happen **only** on the main thread.
2. A PCM buffer has exactly one owner at a time; ownership transfers via `std::move` into the event.
3. Worker threads never call into `StateMachine` or `LvglUi` directly — they only `push()`.

### EventBus contract

```cpp
// common/EventBus.hpp
using Event = std::variant<
    ButtonEvent, RecordingComplete, RecordingTimeout,
    SttResult, LlmResult, NetworkError,
    PlaybackComplete, TtsFailed>;

class EventBus {
public:
    void push(Event e);                       // any thread
    std::optional<Event> pop(int timeoutMs);  // main thread only
};
```

A `std::variant` keeps the event set closed and lets the state machine `std::visit` over it — the compiler then flags any unhandled event type when a new one is added.

---

## 4. End-to-end data flow (one PTT turn)

```
[GPIO] PTT press
   └─► ButtonEvent{Ptt,Press}      ─► FSM: IDLE→LISTENING; AudioPipeline.start()
[Capture] fills buffer until PTT release OR 15s (FR-8)
[GPIO] PTT release
   └─► ButtonEvent{Ptt,Release}    ─► FSM: LISTENING→PROCESSING
[Capture] RecordingComplete{pcm}   ─► FSM hands pcm to net worker
[Net]  SttClient.transcribe(pcm)
   └─► SttResult{"what's the weather"} ─► FSM hands text to net worker
[Net]  LlmClient.chat(text)
   └─► LlmResult{"It's sunny..."}   ─► FSM: TtsEngine.synthesize() on main thread
[Main] TtsEngine → PcmBuffer       ─► FSM: PROCESSING→SPEAKING; Playback.start(pcm)
[Playback] PlaybackComplete        ─► FSM: SPEAKING→IDLE
```

Any `NetworkError` / `TtsFailed` along the way short-circuits to the `ERROR` state with a distinct LCD message (CLAUDE.md §9), then auto-recovers on the next PTT press.

---

## 5. Display-driver path (resolves CLAUDE.md §4.4 gate)

**Chosen: `fbtft` / `fb_ili9341` legacy framebuffer driver** (Decision #10). The kernel binds the panel from the device-tree overlay and exposes `/dev/fb0`; LVGL's fbdev backend `mmap`s it directly — no custom userspace SPI code.

Grounding facts from the actual overlay ([kernel/overlays/BBB-VOICE-ASSISTANT.dts](../kernel/overlays/BBB-VOICE-ASSISTANT.dts)):

| Signal | Pin | Notes |
|--------|-----|-------|
| SCLK | P9_22 (spi0_sclk) | MODE0 |
| MOSI | P9_21 (spi0_d0) | `ti,pindir-d0-out-d1-in=1` makes d0 the output |
| MISO | P9_18 (spi0_d1) | input (unused by panel) |
| CS0  | P9_17 (spi0_cs0) | |
| RESET | P9_15 → gpio1_16 | active-low (`reset-gpios = <&gpio1 16 1>`) |
| DC | P9_23 → gpio1_17 | data/command select (`dc-gpios = <&gpio1 17 0>`) |
| Panel | `ilitek,ili9341` | `buswidth=8`, `spi-max-frequency=32000000`, `rotate=0` |

The DRM (`panel-mipi-dbi-spi`) and custom-`spidev` paths remain documented as future upgrades in [development/device_driver.md](development/device_driver.md), but the app is written against fbdev today.

---

## 6. Why these boundaries (design rationale)

- **Two HTTP clients, not one "AiClient"** — STT and LLM are different servers with different APIs on the PC; merging them would couple two failure modes (CLAUDE.md §9 wants them surfaced *distinctly*).
- **FSM owns no IO** — makes the entire control logic unit-testable with a fake EventBus and fake middleware; this is the bulk of the `tests/app/` suite.
- **HAL is the mock seam** — every hardware dependency sits behind a pure-virtual interface, so middleware/app tests run on the dev VM with no BBB attached.

See [development/app_layer.md](development/app_layer.md) for the state machine implementation and [development/hal_layer.md](development/hal_layer.md) for the mock pattern.
