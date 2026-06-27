#pragma once
#include <string>
#include <map>
#include <spdlog/spdlog.h>

namespace bbb {

struct Logger {
    static inline std::map<std::string, spdlog::level::level_enum> strToLevel {
        {"info", spdlog::level::info},
        {"debug", spdlog::level::debug},
        {"trace", spdlog::level::trace},
        {"warn", spdlog::level::warn},
        {"error", spdlog::level::err},
    };

    static void init(const std::string& level, const std::string& pattern = "") {
        // convert từ string, nếu ko hợp lệ -> spdlog::level::off
        auto enum_level = spdlog::level::from_str(level);
        
        if (enum_level == spdlog::level::off) { // fallback về info thay vì off
            spdlog::set_level(spdlog::level::info);
        } else {
            spdlog::set_level(enum_level);
        }

        // set pattern. Ex: "[%H:%M:%S %z] [%n] [%^---%L---%$] [thread %t] %v"
        if (!pattern.empty()) {
            spdlog::set_pattern(pattern);
        }
    }

    // wrap spdlog bằng variadic template forward
    template<typename... Args>
    static void info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::info(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void debug(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::debug(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::warn(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::error(fmt, std::forward<Args>(args)...);
    }
};

} // namespace bbb