# Windows local SDL3/mpv dependencies

# Dear ImGui + ImPlot
set(
    IMGUI_DIR
    "${PROJECT_SOURCE_DIR}/third_party/imgui"
    CACHE PATH
    "Dear ImGui + ImPlot source root"
)

set(IMGUI_CORE_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/implot.cpp"
    "${IMGUI_DIR}/implot_items.cpp"
)

# nlohmann/json
set(
    JSON_INCLUDE_DIR
    "${PROJECT_SOURCE_DIR}/third_party/json/include"
    CACHE PATH
    "nlohmann/json include root"
)

# stb_image
set(
    STB_IMAGE_DIR
    "${PROJECT_SOURCE_DIR}/third_party/stb_image"
    CACHE PATH
    "stb_image include root"
)

# NanoSVG
set(
    NANOSVG_DIR
    "${PROJECT_SOURCE_DIR}/third_party/nanosvg/src"
    CACHE PATH
    "NanoSVG source root"
)

if(WIN32)
    set(SDL3_ROOT
        "${PROJECT_SOURCE_DIR}/third_party/prebuilt/windows-x64/sdl3"
        CACHE PATH
        "SDL3 development package root"
    )

    set(SDL3_DLL
        "${PROJECT_SOURCE_DIR}/third_party/bin/SDL3.dll"
        CACHE FILEPATH
        "SDL3 runtime DLL matching the development package"
    )

    set(MPV_ROOT
        "${PROJECT_SOURCE_DIR}/third_party/prebuilt/windows-x64/mpv"
        CACHE PATH
        "mpv development package root"
    )

    foreach(_file IN ITEMS
        "${SDL3_ROOT}/include/SDL3/SDL.h"
        "${SDL3_ROOT}/lib/SDL3.lib"
        "${SDL3_DLL}"
    )
        if(NOT EXISTS "${_file}")
            message(
                FATAL_ERROR
                "Missing SDL3 dependency: ${_file}"
            )
        endif()
    endforeach()

    foreach(_file IN ITEMS
        "${MPV_ROOT}/include/mpv/client.h"
        "${MPV_ROOT}/include/mpv/render.h"
        "${MPV_ROOT}/include/mpv/render_d3.h"
        "${PROJECT_SOURCE_DIR}/third_party/bin/libmpv-2.dll"
        "${MPV_ROOT}/lib/mpv.lib"
    )
        if(NOT EXISTS "${_file}")
            message(
                FATAL_ERROR
                "Missing mpv dependency: ${_file}"
            )
        endif()
    endforeach()

    add_library(
        SDLViewer::SDL3
        UNKNOWN
        IMPORTED
        GLOBAL
    )

    set_target_properties(
        SDLViewer::SDL3
        PROPERTIES
        IMPORTED_LOCATION
        "${SDL3_ROOT}/lib/SDL3.lib"
        INTERFACE_INCLUDE_DIRECTORIES
        "${SDL3_ROOT}/include"
    )

    add_library(
        SDLViewer::mpv
        UNKNOWN
        IMPORTED
        GLOBAL
    )

    set_target_properties(
        SDLViewer::mpv
        PROPERTIES
        IMPORTED_LOCATION
        "${MPV_ROOT}/lib/mpv.lib"
        INTERFACE_INCLUDE_DIRECTORIES
        "${MPV_ROOT}/include"
    )

    set(
        SDLVIEWER_SDL3_TARGET
        "SDLViewer::SDL3"
    )

    set(
        SDLVIEWER_MPV_TARGET
        "SDLViewer::mpv"
    )
elseif(APPLE)
    find_package(SDL3 3.2 CONFIG REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

    set(
        SDLVIEWER_SDL3_TARGET
        "SDL3::SDL3"
    )

    set(
        SDLVIEWER_MPV_TARGET
        "PkgConfig::MPV"
    )
endif()