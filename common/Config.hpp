#pragma once
#include "Types.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace bbb {

/** Example of AppConfig
{
  "llm": {
    "host": "192.168.1.50",          // LAN IP của PC chạy LM Studio (KHÔNG phải 192.168.7.x)
    "port": 1234,
    "path": "/v1/chat/completions",  // OpenAI-compatible
    "model": "qwen2.5-3b-instruct"   // tên model đúng như LM Studio expose
  },
  "stt": {
    "host": "192.168.1.50",          // cùng PC, port khác
    "port": 9000,
    "path": "/inference",            // ⚠️ confirm với server thật của bạn — whisper.cpp vs faster-whisper khác path (CLAUDE.md §5)
    "model": "ggml-base.en",
    "lang": "en"
  },
  "network": {
    "connectMs": 3000,               // connect timeout ngắn → phát hiện "server chưa bật" nhanh (§9)
    "totalMs": 10000                 // total timeout dài hơn cho câu trả lời LLM dài
  },

  "gpio": {
    "debounceMs": 25,
    "ptt":     { "chip": 1, "offset": 0, "activeLow": true },
    "volUp":   { "chip": 1, "offset": 0, "activeLow": true },
    "volDown": { "chip": 1, "offset": 0, "activeLow": true },
    "led":     { "chip": 1, "offset": 0, "activeLow": false }
  },

  "audio": {
    "captureDevice":  "plughw:CARD=Device,DEV=0",  // plughw → tự resample, an toàn adapter rẻ
    "playbackDevice": "plughw:CARD=Device,DEV=0",
    "maxRecordSec": 15,              // FR-8 hard timeout - có thể tùy chỉnh
    "initialGain": 1.0,              // có thể tùy chỉnh
    "gainStep": 0.1                  // có thể tùy chỉnh
  },

  "display": {
    "fbdev": "/dev/fb0",
    "width": 320,
    "height": 240,
    "rotation": 0
  }
}
**/
/**
* @brief  Các biến và hàm ở đây đều nằm trên header.
*         Để tránh lỗi multiple definition, ta dùng inline để cho phép
*         nhiều source .cpp có thể include và sử dụng đồng thời.
*/


// -------- DEFAULT definitions ---------
// Network
inline constexpr int kNetConnectMs = 3000;
inline constexpr int kNetTotalMs = 10000;

// Audio
inline constexpr int kAudioSampleRate = 16000;
inline constexpr int kAudioChannels = 1;
inline constexpr int kAudioMaxRecordSec = 15;
inline constexpr float kAudioInitialGain = 1.0f;
inline constexpr float kAudioGainStep = 0.1f;

// Display
inline constexpr int kDisplayWidth = 320;
inline constexpr int kDisplayHeight = 240;
inline constexpr int kDisplayRotation = 0;

// Error code
enum class ConfigError : int{
    NotFound = 1, // tránh bắt đầu từ 0 (SUCCESS code)
    ParseError,
};

struct LlmConfig { std::string host, path, model; int port = 0; };
struct SttConfig { std::string host, path, model, lang; int port = 0; };
struct NetConfig { int connectMs = kNetConnectMs, totalMs = kNetTotalMs; };
struct AudioConfig { std::string captureDevice, playbackDevice; 
                    int sampleRate = kAudioSampleRate, channels = kAudioChannels; // fixed by NFR-4, not from config
                    int  maxRecordSec = kAudioMaxRecordSec; // configurable
                    float initialGain = kAudioInitialGain, gainStep = kAudioGainStep; };
struct DisplayConfig { std::string fbdev; int width = kDisplayWidth, height = kDisplayHeight, rotation = kDisplayRotation; };

struct AppConfig {
    LlmConfig   llm;
    SttConfig   stt;
    NetConfig   net;
    GpioPinMap  gpio;
    AudioConfig audio;
    DisplayConfig display;
};

inline Result<AppConfig> loadConfig(const std::string& path) {
    std::ifstream f(path);
    AppConfig config;    

    if (!f)
        return Error{static_cast<int>(ConfigError::NotFound), "config not found: " + path};

    try {
        nlohmann::json j;
        f >> j;

        const auto& llm = j.at("llm");
        config.llm.host  = llm.at("host");
        config.llm.path  = llm.at("path");
        config.llm.model = llm.at("model");
        config.llm.port  = llm.at("port");

        const auto& stt = j.at("stt");
        config.stt.host  = stt.at("host");
        config.stt.path  = stt.at("path");
        config.stt.model = stt.at("model");
        config.stt.lang  = stt.at("lang");
        config.stt.port  = stt.at("port");

        const auto& net = j.at("network");
        config.net.connectMs = net.at("connectMs");
        config.net.totalMs   = net.at("totalMs");

        const auto& g = j.at("gpio");
        config.gpio.debounceMs = g.at("debounceMs");

        auto parseLine = [](const nlohmann::json& n) -> GpioLineSpec {
            return { n.at("chip").get<int>(),
                     n.at("offset").get<int>(),
                     n.at("activeLow").get<bool>() };
        };
        config.gpio.ptt     = parseLine(g.at("ptt"));
        config.gpio.volUp   = parseLine(g.at("volUp"));
        config.gpio.volDown = parseLine(g.at("volDown"));
        config.gpio.led     = parseLine(g.at("led"));

        const auto& audio = j.at("audio");
        config.audio.captureDevice = audio.at("captureDevice");
        config.audio.playbackDevice = audio.at("playbackDevice");
        config.audio.maxRecordSec = audio.value("maxRecordSec", kAudioMaxRecordSec);
        config.audio.initialGain = audio.value("initialGain", kAudioInitialGain);
        config.audio.gainStep = audio.value("gainStep", kAudioGainStep);
        
        const auto& display = j.at("display");
        config.display.fbdev = display.at("fbdev");
        config.display.width = display.at("width");
        config.display.height = display.at("height");
        config.display.rotation = display.value("rotation", kDisplayRotation);
        
    } catch (const std::exception& e) {
        return Error{static_cast<int>(ConfigError::ParseError), std::string("invalid config: ") + e.what()};
    }

    return config;
}
} // namespace bbb