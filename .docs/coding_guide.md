# Coding Guide

> C++17 patterns, conventions, and best practices used in BBB Voice Assistant. Aimed at developers new to production-quality embedded C++.

---

## Why these choices?

This project targets embedded Linux on BeagleBone Black, with native I2S audio, framebuffer display, GPIO events, and a local AI server. The coding conventions favor:
- low-overhead abstractions,
- deterministic cleanup,
- clear ownership,
- testable decoupling,
- and a small, maintainable runtime footprint.

The guide explains not only what we use, but why we choose it over older or more expensive alternatives.

---

## C++17 Features Used in This Project

### `std::string_view` — non-owning string references

Use in function parameters when you only need read-only access. It is lighter than `std::string` and more flexible than `const std::string&` when accepting string literals, `std::string`, or other contiguous string data.

Why choose it:
- avoids copies and heap allocations,
- works with string literals and `std::string` transparently,
- expresses intent: caller retains ownership.

Why not use raw C strings:
- `std::string_view` is safer than `const char*` because it carries length explicitly,
- avoids accidental buffer overruns.

```cpp
// Good — accepts string literal, std::string, or any contiguous range
bool GpiodHAL::open(std::string_view chip_path) {
    chip_ = gpiod_chip_open(chip_path.data());
    ...
}

// Avoid — copies the string for no reason
bool GpiodHAL::open(std::string chip_path) { ... }
```

> Note: do not store `std::string_view` across function boundaries unless the lifetime of the referenced data is guaranteed.

---

### `std::span` — non-owning view of contiguous data

Use `std::span` for buffers instead of raw pointer + length pairs. It makes the API self-documenting and reduces parameter count.

Why choose it:
- safer than raw pointers,
- works with arrays, `std::vector`, and raw buffers,
- no ownership or allocation overhead.

Why not use `std::vector` for every buffer parameter:
- `std::vector` implies ownership and dynamic allocation,
- `std::span` is the right abstraction for mere access.

```cpp
bool IAudioHAL::writeFrames(std::span<const int16_t> data);

std::vector<int16_t> samples = ...;
hal->writeFrames(samples);  // std::span deduces size automatically
```

---

### `std::variant` — type-safe union for event payloads

Used to implement `Event::payload` without unsafe casts or hidden type assumptions.

Why choose it:
- safer than `void*` or raw unions,
- enables exhaustive visitation,
- works well with event-driven architectures.

Why not use inheritance here:
- simple payload values are easier to express with `std::variant` than with extra derived classes,
- avoids heap allocation for small payloads.

```cpp
struct Event {
    EventType type;
    std::variant<std::monostate, int, std::string, AppState> payload;
};

bus.publish({ EventType::VolumeChanged, 75 });
bus.publish({ EventType::AIResponseReady, std::string("Hello!") });

void handle(const Event& e) {
    if (e.type == EventType::AIResponseReady) {
        auto text = std::get<std::string>(e.payload);
        display_->showResponse(text);
    }
}
```

---

### `std::optional` — expressive missing values

Use `std::optional` for functions that may fail to produce a value, but where failure is expected and not exceptional.

Why choose it:
- expresses optional results clearly,
- avoids sentinel values such as empty strings or negative integers,
- better than raw pointers when returning by value.

```cpp
std::optional<Config> Config::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;

    nlohmann::json j = nlohmann::json::parse(f);
    Config cfg;
    cfg.audio_device = j["audio"]["capture_device"].get<std::string>();
    return cfg;
}
```

---

### Structured Bindings

Use structured bindings to make multi-value returns readable and avoid the `.first / .second` smell.

Why choose it:
- improves readability,
- prevents accidental misuse of tuple indices,
- works cleanly with `std::pair`, `std::tuple`, and custom structs.

```cpp
auto [value, error] = parse_config(path);
if (!value) {
    LOG_ERROR("Config parse failed: {}", error);
}
```

---

### `if constexpr` — compile-time branching

Use `if constexpr` in templates to choose behavior at compile time without generating invalid branches.

Why choose it:
- avoids runtime overhead,
- keeps template code safe,
- allows clean specialization without separate overloads.

```cpp
template<typename T>
void logPayload(const T& payload) {
    if constexpr (std::is_same_v<T, std::string>) {
        LOG_INFO("Payload: {}", payload);
    } else if constexpr (std::is_same_v<T, int>) {
        LOG_INFO("Payload: {}", payload);
    }
}
```

---

### `constexpr` — compile-time constants

Use `constexpr` for constants that should be evaluated at compile time.

Why choose it:
- avoids preprocessor macros,
- provides type safety,
- allows the compiler to optimize values into the code.

```cpp
static constexpr uint32_t kDefaultSampleRate = 16000;
static constexpr char kDefaultAiUrl[] = "http://192.168.1.100:1234";
```

---

## RAII — Resource Acquisition Is Initialization

Every resource (file descriptor, ALSA handle, GPIO chip, framebuffer FD) must be owned by an object and released in its destructor.

Why choose it:
- deterministic cleanup,
- prevents leaks even on early returns,
- simplifies error handling in failure paths.

Why not manage raw handles manually:
- manual cleanup is error-prone,
- embedded code with limited resources must avoid leaks.

```cpp
class AlsaHAL : public IAudioHAL {
public:
    ~AlsaHAL() override {
        close();
    }

    void close() override {
        if (capture_handle_) {
            snd_pcm_close(capture_handle_);
            capture_handle_ = nullptr;
        }
    }

private:
    snd_pcm_t* capture_handle_ = nullptr;
};
```

For C APIs, prefer `std::unique_ptr` with custom deleters when ownership semantics are clear.

```cpp
auto handle = std::unique_ptr<snd_pcm_t, decltype(&snd_pcm_close)>{nullptr, snd_pcm_close};
```

---

## Smart Pointers

| Type | Use case | Why |
|------|---------|-----|
| `std::unique_ptr<T>` | Single ownership | Clear ownership, zero-cost destructor |
| `std::shared_ptr<T>` | Shared ownership | Use only when multiple owners exist |
| `std::weak_ptr<T>` | Break cycles | Avoid lifetime cycles with `shared_ptr` |
| Raw pointer `T*` | Non-owning reference | Use only for borrowing, never ownership |

Why choose smart pointers:
- avoid manual `new/delete`,
- make ownership explicit,
- reduce resource leaks.

```cpp
auto audio_hal = std::make_unique<AlsaHAL>();
auto display_hal = std::make_unique<FbdevHAL>();

AudioPipeline pipeline(audio_hal.get());
```

> Rule: Prefer `std::unique_ptr` by default. Use `std::shared_ptr` only when ownership must be shared, such as `EventBus`, `Config`, or runtime plugin registries.

---

## Error Handling

This project uses return-value error reporting instead of exceptions in HAL and middleware layers.

Why choose return-value error handling:
- smaller binary size,
- predictable control flow,
- easier reasoning in low-level embedded code,
- avoids exception unwinding across C APIs.

Why not use exceptions in this codebase:
- exception handling can add hidden runtime cost,
- some embedded toolchains or runtime environments do not fully support it,
- C libraries such as ALSA and gpiod do not use exceptions.

### Pattern: `bool` return + `spdlog` logging

```cpp
bool AlsaHAL::openCapture(const AudioConfig& cfg) {
    int err = snd_pcm_open(&capture_handle_, cfg_device_.c_str(),
                           SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        LOG_ERROR("AlsaHAL: cannot open capture device '{}': {}",
                  cfg_device_, snd_strerror(err));
        return false;
    }
    LOG_INFO("AlsaHAL: capture opened at {} Hz", cfg.sampleRate);
    return true;
}
```

### Pattern: early return on failure

```cpp
bool VoiceAssistant::runProcessing() {
    auto wav = pipeline_->record(config_.max_record_ms);
    if (wav.empty()) {
        LOG_ERROR("Recording produced empty audio");
        transitionToState(AppState::ERROR);
        return false;
    }

    auto transcript = ai_client_->transcribe(wav);
    if (transcript.empty()) {
        LOG_WARN("STT returned empty transcript — returning to IDLE");
        transitionToState(AppState::IDLE);
        return false;
    }

    auto response = ai_client_->chat(transcript);
    if (response.empty()) {
        transitionToState(AppState::ERROR);
        return false;
    }

    return true;
}
```

### Pattern: `std::optional` for parse results

```cpp
std::optional<Config> Config::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;

    auto j = nlohmann::json::parse(f);
    Config cfg;
    cfg.audioDevice = j["audio"]["capture_device"].get<std::string>();
    return cfg;
}
```

---

## Logging with spdlog

Use `spdlog` instead of `printf` or `syslog` for readable, thread-safe, and configurable application logging.

Why choose `spdlog`:
- built-in formatting with `{}` syntax,
- thread-safe sinks,
- rotating file support,
- consistent logging API across the app.

Why not use `printf`:
- not structured,
- harder to control log levels,
- not suitable for concurrent threads.

### Setup (in `common/Logger.hpp`)

```cpp
#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

inline void init_logger(const std::string& log_file) {
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file    = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                       log_file, 1024 * 1024, 3);

    auto logger = std::make_shared<spdlog::logger>(
        "main", spdlog::sinks_init_list{console, file});

    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::warn);
}

#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...)  spdlog::info(__VA_ARGS__)
#define LOG_WARN(...)  spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
```

### Log Level Convention

| Level | When to use |
|-------|------------|
| `DEBUG` | audio buffer timing, GPIO events, raw HTTP payloads |
| `INFO` | successful init, state transitions, config loaded |
| `WARN` | recoverable conditions, retry attempts, empty STT result |
| `ERROR` | failed hardware init, connection errors, invalid config |

### Log Format

Always include context and origin information.

```cpp
LOG_INFO("AudioPipeline: recorded {} bytes in {}ms", wav.size(), elapsed_ms);
LOG_ERROR("AIClient: HTTP {} from LM Studio: {}", status_code, response_body);
```

Avoid logging in hot audio loops except for exceptional conditions.

---

## JSON with nlohmann/json

Use `nlohmann/json` for configuration and API payload handling.

Why choose it:
- header-only and easy to add,
- expressive, readable syntax,
- well suited for config parsing and AI request/response handling.

Why not choose `protobuf` for config:
- JSON is more human-readable for runtime config,
- lower integration cost for this project,
- protobuf is better reserved for binary or high-performance wire protocols.

### Reading Config

```cpp
#include <nlohmann/json.hpp>
#include <fstream>

struct Config {
    std::string aiBaseUrl;
    int maxTokens;
    std::string captureDevice;

    static std::optional<Config> load(const std::string& path) {
        std::ifstream f(path);
        if (!f) return std::nullopt;

        nlohmann::json j = nlohmann::json::parse(f);
        Config cfg;
        cfg.aiBaseUrl = j["ai"]["base_url"].get<std::string>();
        cfg.maxTokens = j["ai"]["max_tokens"].get<int>();
        cfg.captureDevice = j["audio"]["capture_device"].get<std::string>();
        return cfg;
    }
};
```

### Parsing AI Response

```cpp
std::string AIClient::chat(const std::string& user_text) {
    auto body = postRequest(...);
    try {
        auto j = nlohmann::json::parse(body);
        return j["choices"][0]["message"]["content"].get<std::string>();
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("AIClient: JSON parse error: {}", e.what());
        LOG_DEBUG("AIClient: raw response: {}", body);
        return {};
    }
}
```

---

## Thread Safety Rules

1. `EventBus` has an internal mutex and is safe to publish from any thread.
2. `HAL` objects are not thread-safe; ownership stays with one thread or is serialized explicitly.
3. `LVGL` is not thread-safe. All `lv_` calls must happen on the main UI thread.
4. `spdlog` is thread-safe with `_mt` sinks.
5. `nlohmann/json` is not thread-safe for concurrent writes.

Why these rules:
- embedded systems have limited concurrency debugging tools,
- predictable threading reduces data races,
- thread-local and message-passing designs are easier to maintain.

```cpp
// Wrong — two threads calling LVGL functions
std::thread t([&]{ lv_label_set_text(label, "hello"); });

// Correct — post update via EventBus, handle on main thread
bus.publish({ EventType::AIResponseReady, std::string("hello") });
```

---

## Naming Conventions

Consistent names make the codebase easier to navigate.

| Item | Convention | Example |
|------|------------|---------|
| Classes | PascalCase | `VoiceAssistant`, `AlsaHAL` |
| Interfaces | `I` prefix + PascalCase | `IAudioHAL`, `IDisplayHAL` |
| Methods | camelCase | `openCapture()`, `setVolume()` |
| Variables | snake_case with `_` suffix | `capture_handle_`, `config_` |
| Constants | `k` prefix + PascalCase | `kDefaultSampleRate` |
| Enums | PascalCase | `AppState::Listening` |
| Files | PascalCase or descriptive names | `AlsaHAL.cpp`, `VoiceAssistant.hpp` |
| Macros | ALL_CAPS | `LOG_ERROR`, `CONFIG_PATH` |

Why this convention:
- separates ownership from behavior,
- keeps interfaces readable,
- matches modern C++ style and the rest of the project.

---

## CMake Conventions

Use target-based CMake to keep dependencies explicit and avoid global side effects.

Why choose target-based commands:
- dependencies are declared per target,
- include paths are contained,
- the build graph is easier to reason about.

```cmake
target_include_directories(hal_audio
    PUBLIC  ${CMAKE_SOURCE_DIR}/hal/include
    PRIVATE ${ALSA_INCLUDE_DIRS}
)

target_link_libraries(app PRIVATE hal_audio spdlog::spdlog)
```

Avoid `include_directories()` or `link_libraries()` at the top level unless absolutely necessary.

---

## Design Patterns

This project uses several patterns to make the architecture clear and extensible. Each pattern is chosen for embedded constraints, testability, and safe interaction with C APIs.

| Pattern | Where Used | Why |
|---------|------------|-----|
| Abstract Interface | HAL layer (`IAudioHAL`, `IDisplayHAL`, `IGPIOHAL`) | Decouple application logic from hardware details |
| Factory | `HALFactory::createAudio()` | Create concrete objects at startup without leaking implementation details |
| Strategy | `TTSEngine`, audio backends | Swap implementations at runtime or compile time with the same interface |
| Adapter | `AlsaHAL`, `GpiodHAL`, `FbdevHAL` | Wrap C APIs in safer C++ interfaces |
| Observer / EventBus | app event dispatch | Loose coupling between components and threads |
| State Machine | `VoiceAssistant` | Explicit, testable voice interaction flow |
| RAII | resource wrappers | Deterministic cleanup and leak prevention |
| Dependency Injection | passing HALs to middleware/app | Easily test and swap implementations |
| Singleton (app-scoped) | `Config`, `Logger` | Global access without scattered statics |

### Abstract Interface

The HAL layer defines pure interfaces to separate hardware-specific behavior from business logic.

Why choose it:
- makes the app layer independent of ALSA, framebuffer, or GPIO APIs,
- enables mock implementations for unit testing,
- supports multiple hardware backends later.

Example:
```cpp
class IAudioHAL {
public:
    virtual ~IAudioHAL() = default;
    virtual bool openCapture(const AudioConfig& cfg) = 0;
    virtual bool startCapture() = 0;
    virtual bool readFrames(std::vector<int16_t>& buffer, uint32_t frames) = 0;
    virtual void close() = 0;
};
```

A concrete implementation can remain private to `hal/audio/AlsaHAL.cpp` while the app layer only includes `IAudioHAL.hpp`.

### Factory

Use a factory to create concrete HAL objects while keeping callers dependent only on the abstract interface.

Why choose it:
- centralizes object construction,
- hides implementation selection details,
- simplifies test harness setup.

Example:
```cpp
namespace hal {
std::unique_ptr<IAudioHAL> create_audio(const Config& config) {
    if (config.use_mock_audio) {
        return std::make_unique<MockAudioHAL>();
    }
    return std::make_unique<AlsaHAL>(config.audioDevice);
}
}
```

The app code can then simply call:
```cpp
auto audio_hal = hal::create_audio(config);
```

### Strategy

`TTSEngine` uses the Strategy pattern by exposing a stable interface while allowing different speech backends.

Why choose it:
- supports future backend swaps without changing callers,
- isolates backend-specific initialization and cleanup,
- makes behavior configurable.

Example:
```cpp
class ITTSEngine {
public:
    virtual ~ITTSEngine() = default;
    virtual std::vector<uint8_t> synthesizeSpeech(const std::string& text) = 0;
};

class ESpeakEngine : public ITTSEngine {
    std::vector<uint8_t> synthesizeSpeech(const std::string& text) override;
};
```

### Adapter

Use adapters to wrap C APIs from ALSA, libgpiod, and framebuffer into idiomatic C++ classes.

Why choose it:
- reduces direct use of raw handles,
- provides RAII semantics,
- exposes a clean interface to the rest of the application.

Example:
```cpp
class GpiodHAL : public IGPIOHAL {
public:
    bool open(std::string_view chipPath) override {
        chip_ = gpiod_chip_open(chipPath.data());
        return chip_ != nullptr;
    }
    void close() override {
        if (chip_) gpiod_chip_close(chip_);
    }
private:
    gpiod_chip* chip_ = nullptr;
};
```

### Observer / EventBus

The EventBus provides a publish/subscribe mechanism for UI, input, and audio state changes.

Why choose it:
- decouples producers and consumers,
- makes threading easier by isolating event delivery,
- avoids direct dependencies between app components.

Example:
```cpp
bus.subscribe(EventType::ButtonPressed, [&](const Event& e) {
    voiceAssistant.handleEvent(e);
});

bus.publish({ EventType::ButtonPressed, buttonId });
```

### State Machine

`VoiceAssistant` is modeled as a state machine to keep voice interaction logic explicit and robust.

Why choose it:
- prevents invalid transitions,
- makes timeout and error handling clearer,
- supports testing of each state independently.

Example states:
- `INIT`
- `IDLE`
- `LISTENING`
- `PROCESSING`
- `SPEAKING`
- `ERROR`

Example transition:
```cpp
void VoiceAssistant::transitionToState(AppState next) {
    exitState(currentState_);
    enterState(next);
    currentState_ = next;
}
```

### RAII

This pattern is the foundation for resource safety in embedded C++.

Why choose it:
- avoids leaks in the face of early returns or exceptions,
- centralizes cleanup logic,
- makes ownership explicit.

Example:
```cpp
class AlsaHandle {
public:
    explicit AlsaHandle(snd_pcm_t* handle) : handle_(handle) {}
    ~AlsaHandle() { if (handle_) snd_pcm_close(handle_); }
    snd_pcm_t* get() const { return handle_; }
private:
    snd_pcm_t* handle_;
};
```

### Dependency Injection

Pass HAL implementations and other dependencies into constructors instead of creating them internally.

Why choose it:
- makes classes easier to unit test,
- avoids hidden dependencies,
- improves modularity.

Example:
```cpp
VoiceAssistant::VoiceAssistant(std::unique_ptr<IAudioHAL> audio,
                               std::unique_ptr<IDisplayHAL> display,
                               std::unique_ptr<IGPIOHAL> gpio)
    : audio_(std::move(audio)), display_(std::move(display)), gpio_(std::move(gpio)) {}
```

### Singleton (app-scoped)

Use singletons sparingly for truly shared resources such as configuration and logging.

Why choose it:
- controlled global access,
- avoids scattered static instances,
- centralizes shared state initialization.

Example:
```cpp
class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }
    void init(const std::string& path);
};
```

Avoid using singletons for hardware objects or mutable application state. Keep them limited to cross-cutting services.

Why these patterns:
- embedded code benefits from explicit dependency boundaries,
- testability increases when hardware is injected,
- a clear state machine reduces runtime ambiguity,
- adapters make C libraries safer to use in C++.

---

## Testing and Mocking

The HAL abstraction enables unit testing without hardware.

Why choose mock HALs:
- validate application logic offline,
- fail fast on state machine behavior,
- avoid requiring BeagleBone hardware for every test run.

Example:
```cpp
auto mock_audio = std::make_unique<MockAudioHAL>();
AudioPipeline pipeline(mock_audio.get());
```

Use mocks for `VoiceAssistant`, `AudioPipeline`, and `AIClient` to isolate behavior.

---

## Thread Safety Rules

1. **EventBus** has an internal mutex. Safe to publish from any thread.
2. **HAL objects** are NOT thread-safe. Create one per thread, or serialize access with a mutex.
3. **LVGL** is NOT thread-safe. All `lv_` calls must happen from the same thread (main thread).
4. **spdlog** IS thread-safe by default with `_mt` sinks.
5. **nlohmann/json** objects are NOT thread-safe for concurrent writes.

```cpp
// Wrong — two threads calling LVGL functions
std::thread t([&]{ lv_label_set_text(label, "hello"); });  // DON'T

// Correct — post update request via EventBus, handle in main thread
bus.publish({ EventType::AIResponseReady, std::string("hello") });
// Main thread: handle event → lv_label_set_text(...)
```

---

## Naming Conventions

| Item | Convention | Example |
|------|-----------|---------|
| Classes | PascalCase | `VoiceAssistant`, `AlsaHAL` |
| Interfaces | `I` prefix + PascalCase | `IAudioHAL` |
| Methods | camelCase | `openCapture()`, `readFrames()` |
| Members | snake_case + `_` suffix | `capture_handle_`, `config_` |
| Constants / enums | PascalCase | `AppState::LISTENING` |
| Files | PascalCase for classes | `AlsaHAL.cpp`, `IAudioHAL.hpp` |
| Macros | ALL_CAPS | `LOG_INFO`, `LOG_ERROR` |

---

## CMake Conventions

- One `CMakeLists.txt` per subdirectory.
- Use `target_*` (not global `include_directories`, `link_libraries`).
- Mark includes `PUBLIC` only if downstream targets need them.

```cmake
# Good
target_include_directories(hal_audio
    PUBLIC  ${CMAKE_SOURCE_DIR}/hal/include   # AIClient needs IAudioHAL.hpp
    PRIVATE ${ALSA_INCLUDE_DIRS}              # nobody else needs ALSA headers
)

# Avoid
include_directories(${ALSA_INCLUDE_DIRS})    # pollutes all targets
```
