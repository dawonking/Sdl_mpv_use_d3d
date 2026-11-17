# --- Windows compile settings ---

set_target_properties(sdl_window_test PROPERTIES
    WIN32_EXECUTABLE "$<CONFIG:Release>"
)

target_sources(sdl_window_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/platform/windows/main_windows.cpp"
)

target_compile_definitions(
    sdl_window_test
    PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(
        sdl_window_test
        PRIVATE
        /utf-8
    )
endif()

target_link_libraries(
    sdl_window_test
    PRIVATE
    "${SDLVIEWER_SDL3_TARGET}"
    user32
)

# --- D3D11RenderBackend (Windows 전용 프로젝트 소스) ---
target_sources(sdl_window_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/render/IRenderBackend.h"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/render/D3D11RenderBackend.h"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/render/D3D11RenderBackend.cpp"
)

# --- TextureManager (Windows 전용, stb_image/nanosvg 기반 PNG/SVG 로더) ---
target_sources(sdl_window_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/platform/windows/render/TextureManager.h"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/render/TextureManager.cpp"
)

# --- FolderDialog (Windows 전용, IFileOpenDialog 기반 폴더 선택) ---
target_sources(sdl_window_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/platform/windows/files/FolderDialog.h"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/files/FolderDialog.cpp"
)
target_link_libraries(sdl_window_test PRIVATE ole32)

# --- WebView2MapView (Windows 전용, WebView2 SDK 기반 지도 뷰) ---
set(WEBVIEW2_ROOT "${PROJECT_SOURCE_DIR}/third_party/WebView2/MSWebView2"
    CACHE PATH "Microsoft.Web.WebView2 NuGet package root")
set(WIL_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/WebView2/MSImplementLibrary/include"
    CACHE PATH "Windows Implementation Library (wil) include root")

foreach(_file IN ITEMS
    "${WEBVIEW2_ROOT}/build/native/include/WebView2.h"
    "${WEBVIEW2_ROOT}/build/native/x64/WebView2LoaderStatic.lib"
    "${WIL_INCLUDE_DIR}/wil/com.h")
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "Missing WebView2 dependency: ${_file}.")
    endif()
endforeach()

target_include_directories(sdl_window_test PRIVATE
    "${WEBVIEW2_ROOT}/build/native/include"
    "${WIL_INCLUDE_DIR}"
)
target_sources(sdl_window_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/platform/windows/map/WebView2MapView.h"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/map/WebView2MapView.cpp"
)
target_link_libraries(sdl_window_test PRIVATE "${WEBVIEW2_ROOT}/build/native/x64/WebView2LoaderStatic.lib" version)

# --- ImGui Windows 백엔드 (DX11 + SDL3) ---
target_sources(sdl_window_test PRIVATE
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
)
target_include_directories(sdl_window_test PRIVATE "${IMGUI_DIR}/backends")

# --- D3D 시스템 라이브러리 (Windows SDK 제공, include 경로 불필요) ---
target_link_libraries(sdl_window_test PRIVATE d3d11 dxgi dcomp d3dcompiler)

target_sources(sdl_window_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/platform/windows/media/MpvD3D11Renderer.h"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/media/MpvD3D11Renderer.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/media/PlatformMpvOptions.cpp"
)

# --- mpv ---
target_link_libraries(
    sdl_window_test
    PRIVATE
    "${SDLVIEWER_MPV_TARGET}"
)

# --- Windows runtime DLL copy ---
add_custom_command(
    TARGET sdl_window_test
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        -E copy_directory_if_different
        "${PROJECT_SOURCE_DIR}/third_party/bin"
        "$<TARGET_FILE_DIR:sdl_window_test>"
    VERBATIM
)

# --- Windows Release runtime package ---
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include(InstallRequiredSystemLibraries)
if(MSVC AND NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    message(FATAL_ERROR "MSVC redistributable runtime files were not found")
endif()
install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
    DESTINATION . CONFIGURATIONS Release COMPONENT Runtime)
install(TARGETS sdl_window_test
    RUNTIME DESTINATION . CONFIGURATIONS Release COMPONENT Runtime)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/resources/"
    DESTINATION resources CONFIGURATIONS Release COMPONENT Runtime)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/third_party/bin/"
    DESTINATION . CONFIGURATIONS Release COMPONENT Runtime
    FILES_MATCHING PATTERN "*.dll")
