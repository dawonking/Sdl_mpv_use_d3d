enable_language(OBJCXX)

set_target_properties(sdl_window_test PROPERTIES
    OBJCXX_STANDARD 17
    OBJCXX_STANDARD_REQUIRED ON
)

find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)

target_sources(sdl_window_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/platform/macos/main_macos.mm"
    "${PROJECT_SOURCE_DIR}/src/platform/macos/media/PlatformMpvOptions.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/macos/render/OpenGLRenderBackend.h"
    "${PROJECT_SOURCE_DIR}/src/platform/macos/render/OpenGLRenderBackend.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

target_include_directories(sdl_window_test PRIVATE
    "${IMGUI_DIR}/backends"
)

target_link_libraries(sdl_window_test PRIVATE
    "${SDLVIEWER_SDL3_TARGET}"
    "${SDLVIEWER_MPV_TARGET}"
    "${APPKIT_FRAMEWORK}"
    "${OPENGL_FRAMEWORK}"
    "${WEBKIT_FRAMEWORK}"
)