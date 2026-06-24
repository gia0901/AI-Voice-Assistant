#pragma once
#include <string>
#include <map>
#include <spdlog/spdlog.h>

namespace bbb {

struct Logger {
    static std::map<std::string, spdlog::level::level_enum> strToLevel {
        {"info", spdlog::level::info},
        {"debug", spdlog::level::debug},
        {"trace", spdlog::level::trace},
        {"warn", spdlog::level::warn},
        {"error", spdlog::level::err},
    };

    static void init(const std::string& level, const std::string& pattern) {
        auto it = strToLevel.find(level);
        if (it != strToLevel.end()) {
            spdlog::set_level(it->second);
        }
        else { // mặc định là info
            spdlog::set_level(spdlog::level::info);
        }
    }
};

} // namespace bbb