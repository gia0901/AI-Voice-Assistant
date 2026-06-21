cmake_minimum_required(VERSION 3.16)
project(bbb_voice_assistant LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(common)
add_subdirectory(hal)
add_subdirectory(app)