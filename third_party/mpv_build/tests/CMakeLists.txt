cmake_minimum_required(VERSION 3.25)
project(MpvD3DSmoke LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The SDK smoke test requires Windows MSVC x64")
endif()
foreach(file IN ITEMS
    "${SDK_ROOT}/mpv/include/mpv/render_d3.h"
    "${SDK_ROOT}/mpv/lib/mpv.lib"
    "${SDK_ROOT}/sdl3/include/SDL3/SDL.h"
    "${SDK_ROOT}/sdl3/lib/SDL3.lib"
    "${RUNTIME_ROOT}/libmpv-2.dll"
    "${RUNTIME_ROOT}/SDL3.dll")
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "Missing smoke test dependency: ${file}")
    endif()
endforeach()

add_executable(mpv_d3d_smoke sdl_d3d_test.cpp)
target_compile_features(mpv_d3d_smoke PRIVATE cxx_std_17)
target_compile_definitions(mpv_d3d_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(mpv_d3d_smoke PRIVATE /utf-8)
target_include_directories(mpv_d3d_smoke PRIVATE "${SDK_ROOT}/mpv/include" "${SDK_ROOT}/sdl3/include")
target_link_libraries(mpv_d3d_smoke PRIVATE
    "${SDK_ROOT}/mpv/lib/mpv.lib" "${SDK_ROOT}/sdl3/lib/SDL3.lib" d3d11 dxgi)
add_custom_command(TARGET mpv_d3d_smoke POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
        "${RUNTIME_ROOT}" "$<TARGET_FILE_DIR:mpv_d3d_smoke>"
    VERBATIM)
