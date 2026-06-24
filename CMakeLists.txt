cmake_minimum_required(VERSION 3.16)
project(bbb_voice_assistant LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#### Packages ####
# spdlog
include(FetchContent)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1)

# nlohmann_json
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3)

FetchContent_MakeAvailable(spdlog nlohmann_json)

add_subdirectory(common)
add_subdirectory(hal)
add_subdirectory(app)