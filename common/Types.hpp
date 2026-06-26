#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <variant>

namespace bbb {

enum class State { Init, Idle, Listening, Processing, Speaking, Error };
enum class Service { Stt, Llm }; // Network Service (STT or LLM server)

struct Error { int code; std::string msg; };
template<typename T> using Result = std::variant<T, Error>;

// --------- Audio ---------
using PcmBuffer = std::vector<int16_t>; // S16_LE mono (signed 16 bit - little endian) 


// --------- GPIO ---------
enum class ButtonId { Ptt, VolUp, VolDown };
enum class Edge { Press, Release };

struct GpioLineSpec { // gpio essential configurations
    int chip;
    int offset;
    bool activeLow;
};

struct GpioPinMap {
    GpioLineSpec ptt, volUp, volDown, led;
    int debounceMs;
};


// --------- Event on Bus ---------
struct ButtonEvent          { ButtonId id; Edge edge; };  // when pressing a button
struct RecordingComplete    { PcmBuffer pcm; };
struct RecordingTimeout     {};
struct SttResult            { std::string text; }; // speech-to-text result from Whisper server
struct LlmResult            { std::string reply; }; // response from LLM server
struct NetworkError         { Service svc; std::string msg; };
struct PlaybackComplete     {};
struct TtsFailed            { std::string msg; }; // failed to convert text-to-speech (eSpeak-ng)

} // namespace bbb

