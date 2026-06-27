#include "Types.hpp"
#include "Logger.hpp"
#include "Config.hpp"
#include <string>
using namespace bbb;

const char* config_path = "/home/gia/bbb_voice_assistant/config/config.json";
const char* default_config_path = "/home/gia/bbb_voice_assistant/config/default_config.json";

Result<AppConfig> loadAppConfig(const std::string& path);


int main() {    
    // 1. init Logger
    Logger::init("debug");
    Logger::debug("Hello from {}", std::string("Beaglebone Black"));

    // 2. load configuration
    auto config = loadAppConfig(config_path);
    if (std::holds_alternative<Error>(config)) {
        const auto& err = std::get<Error>(config);
        Logger::error("(Error code:{}) {}", err.code, err.msg);
        return EXIT_FAILURE;
    }

    // get config và sử dụng (TBD)

    return EXIT_SUCCESS;
}

Result<AppConfig> loadAppConfig(const std::string& path) {
    auto result = loadConfig(path);
    
    // Get config thành công
    if (std::holds_alternative<AppConfig>(result)) {
        return std::get<AppConfig>(result);
    }

    // Fail: nếu config not found, cố thử lại với default_config
    // copy-by-value: dễ đọc, tránh dangling trong tương lai
    const auto err = std::get<Error>(result);

    if (err.code == static_cast<int>(ConfigError::NotFound)) {
        Logger::warn("use default_config.json!");
        result = loadConfig(default_config_path);
        if (std::holds_alternative<AppConfig>(result)) {
            return std::get<AppConfig>(result);
        }
        // update error mới
        const auto& newErr = std::get<Error>(result);
        return Error{newErr.code, "Failed to load default_config: " + newErr.msg};
    }
    else if (err.code == static_cast<int>(ConfigError::ParseError)) {
        return Error{err.code, "Parse error: " + err.msg};
    }
    else {
        return Error{err.code, "Unknown error: " + err.msg};
    }
}
