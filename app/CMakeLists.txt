add_executable(app
    main.cpp
)

target_link_libraries(app PRIVATE common) # kéo theo include + spdlog + json

target_include_directories(app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../hal/include
)