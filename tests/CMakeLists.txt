cmake_minimum_required(VERSION 3.16)
project(bbb_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- GoogleTest via FetchContent (pull when configure first time) ---
include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# --- Enable sanitizer (catch UB / use-after-free / data race) ---
add_compile_options(-Wall -Wextra -g -O1)
option(USE_TSAN "ThreadSanitizer for multithreading" OFF)
if(USE_TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
else()
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# --- EventBus Test ---
add_executable(eventbus_test common/EventBusTest.cpp)
target_include_directories(eventbus_test PRIVATE ${CMAKE_SOURCE_DIR}/../common)
target_link_libraries(eventbus_test PRIVATE GTest::gtest_main)
gtest_discover_tests(eventbus_test)

# --- Next component ---