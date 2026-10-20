add_library(screenrecord_core
    capture/capture.cpp
)

target_include_directories(screenrecord_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(screenrecord_core PRIVATE d3d11 dxgi)

add_executable(screenrecord main.cpp)

target_link_libraries(screenrecord PRIVATE screenrecord_core)

target_compile_options(screenrecord_core PRIVATE /W4)