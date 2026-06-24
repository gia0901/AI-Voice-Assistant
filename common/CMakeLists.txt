# common nên là interface library (header-only), để khi sử dụng có thể kéo theo các package liên quan

add_library(common INTERFACE) 
target_include_directories(common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(common INTERFACE
    spdlog::spdlog_header_only  # header-only, ko build lib
    nlohmann_json::nlohmann_json)
