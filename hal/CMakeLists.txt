add_library(hal_gpio SHARED gpio/GpioHal.cpp)
add_library(bbb:hal_gpio ALIAS hal_gpio) # Tạo tên alias có gồm cả scope để dễ quản lý

target_include_directories(hal_gpio PUBLIC include)
target_link_libraries(hal_gpio PRIVATE common) # kéo theo include + spdlog + json
