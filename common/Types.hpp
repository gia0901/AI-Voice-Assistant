#pragma once
#include <cstddef>
#include <string>
#include <variant>

namespace bbb {


enum class State { Init, Idle, Listening, Processing, Speaking, Error };
enum class Service { Stt, Llm };

struct Error { int code; std::string msg; };
template<typename T> using Result = std::variant<T, Error>;  // Error = { code, message }



};

