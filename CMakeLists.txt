cmake_minimum_required(VERSION 3.25)
project(SDLWindowTest LANGUAGES CXX)

include(cmake/Dependencies.cmake)

add_executable(sdl_window_test
    src/app/Application.h
    src/app/Application.cpp
    src/render/SdlRenderThread.h
    src/render/SdlRenderThread.cpp
    src/ui/core/IUiFrame.h
    src/ui/core/IUiComponent.h
    src/ui/core/WidgetConfig.h
    src/media/MpvPlayer.h
    src/media/MpvPlayer.cpp
    src/media/MpvEventThread.h
    src/media/MpvEventThread.cpp
    src/media/MpvPlayerManager.h
    src/media/MpvPlayerManager.cpp
    src/media/VideoFileRules.cpp
    src/media/PlayState.h
    src/media/PlaybackTypes.h
    src/media/ControlManager.h
    src/media/ControlManager.cpp
    ${IMGUI_CORE_SOURCES}
    src/ui/core/UiComponentBase.h
    src/ui/core/UiComponentBase.cpp
    src/ui/core/JsonReader.h
    src/ui/core/JsonReader.cpp
    src/ui/core/UiLayout.h
    src/ui/core/UiLayout.cpp
    src/ui/core/UiPanel.h
    src/ui/core/UiPanel.cpp
    src/ui/core/UiManager.h
    src/ui/core/UiManager.cpp
    src/ui/core/TextLabel.h
    src/ui/core/TextLabel.cpp
    src/ui/core/IUiButton.h
    src/ui/core/UiButtonBase.h
    src/ui/core/ViewButton.h
    src/ui/core/ViewButton.cpp
    src/ui/core/ImgButton.h
    src/ui/core/ImgButton.cpp
    src/ui/core/ImgLabel.h
    src/ui/core/ImgLabel.cpp
    src/ui/core/UiSlideBase.h
    src/ui/core/CustomSlide.h
    src/ui/core/CustomSlide.cpp
    src/ui/core/WidgetGroup.h
    src/ui/core/WidgetGroup.cpp
    src/ui/core/FileItemRaw.h
    src/ui/core/FileItemRaw.cpp
    src/ui/core/CustomFileList.h
    src/ui/core/CustomFileList.cpp
    src/ui/core/ChartDataRaw.h
    src/ui/core/SensorGraph.h
    src/ui/core/SensorGraph.cpp
    src/ui/core/UiPopupPanel.h
    src/ui/core/UiPopupPanel.cpp
    src/ui/core/PopupManager.h
    src/ui/core/PopupManager.cpp
    src/map/IMapView.h
    src/ui/core/GPSDataRaw.h
    src/ui/core/FileCheckExpression.h
    src/ui/core/FileListManager.h
    src/ui/core/FileListManager.cpp
    src/ui/core/DummySensorDataSource.h
    src/ui/core/DummySensorDataSource.cpp
    src/app/MainDispatcher.h
    src/app/MainDispatcher.cpp
    src/app/RunApplication.h
    src/app/RunApplication.cpp
    src/media/PlatformMpvOptions.h
    src/render/StbImageImpl.cpp
    src/render/NanoSvgImpl.cpp
)

target_compile_features(sdl_window_test PRIVATE cxx_std_17)

target_include_directories(sdl_window_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${IMGUI_DIR}"
    "${JSON_INCLUDE_DIR}"
    "${STB_IMAGE_DIR}"
    "${NANOSVG_DIR}")

# --- UI 레이아웃 JSON 리소스 복사 (UiManager::LoadJson()이 실행 시점에 읽음) ---
add_custom_command(TARGET sdl_window_test POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
        "${PROJECT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:sdl_window_test>/resources"
    VERBATIM
)

if(WIN32)
    include(cmake/platform/Windows.cmake)
elseif(APPLE)
    include(cmake/platform/MacOS.cmake)
else()
    message(FATAL_ERROR "Supported platforms: Windows and macOS")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(render_scheduling_tests
        tests/SdlRenderThreadTests.cpp
        src/render/SdlRenderThread.cpp
    )
    target_compile_features(render_scheduling_tests PRIVATE cxx_std_17)
    target_include_directories(render_scheduling_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${IMGUI_DIR}"
    )
    target_link_libraries(render_scheduling_tests PRIVATE "${SDLVIEWER_SDL3_TARGET}")
    if(MSVC)
        target_compile_options(render_scheduling_tests PRIVATE /utf-8)
    endif()
    if(WIN32)
        add_custom_command(TARGET render_scheduling_tests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${SDL3_DLL}" "$<TARGET_FILE_DIR:render_scheduling_tests>"
            VERBATIM
        )
    endif()
    add_test(NAME render_scheduling COMMAND render_scheduling_tests)
    set_tests_properties(render_scheduling PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        TIMEOUT 25
    )

    add_executable(playback_controls_tests
        tests/PlaybackControlsTests.cpp
        src/media/ControlManager.cpp
        src/media/MpvPlayer.cpp
        src/media/MpvEventThread.cpp
        src/ui/core/FileListManager.cpp
        src/media/VideoFileRules.cpp
        src/ui/core/CustomFileList.cpp
        src/ui/core/TextLabel.cpp
        src/ui/core/UiComponentBase.cpp
        ${IMGUI_CORE_SOURCES}
    )
    target_compile_features(playback_controls_tests PRIVATE cxx_std_17)
    target_include_directories(playback_controls_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${IMGUI_DIR}"
        "${JSON_INCLUDE_DIR}"
    )
    target_compile_definitions(playback_controls_tests PRIVATE
        PLAYBACK_TEST_TEMP_ROOT="${PROJECT_BINARY_DIR}/playback-test-data"
    )
    target_link_libraries(playback_controls_tests PRIVATE
        "${SDLVIEWER_SDL3_TARGET}"
        "${SDLVIEWER_MPV_TARGET}"
    )
    if(MSVC)
        target_compile_options(playback_controls_tests PRIVATE /utf-8)
    endif()
    if(WIN32)
        add_custom_command(TARGET playback_controls_tests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
                "${PROJECT_SOURCE_DIR}/third_party/bin"
                "$<TARGET_FILE_DIR:playback_controls_tests>"
            VERBATIM
        )
    endif()
    add_test(NAME playback_controls COMMAND playback_controls_tests)
    set_tests_properties(playback_controls PROPERTIES TIMEOUT 15)
    set(SDLVIEWER_TEST_MEDIA_FILE "" CACHE FILEPATH "Optional video fixture of at least three seconds for playback tests")
    if(SDLVIEWER_TEST_MEDIA_FILE)
        add_test(NAME playback_media COMMAND playback_controls_tests "${SDLVIEWER_TEST_MEDIA_FILE}")
        set_tests_properties(playback_media PROPERTIES TIMEOUT 40)
    endif()

    add_executable(fullscreen_channel_tests
        tests/FullscreenChannelTests.cpp
        src/app/Application.cpp
        src/app/RunApplication.cpp
        src/app/MainDispatcher.cpp
        src/render/SdlRenderThread.cpp
        src/media/ControlManager.cpp
        src/media/MpvPlayer.cpp
        src/media/MpvEventThread.cpp
        src/media/MpvPlayerManager.cpp
        src/ui/core/UiManager.cpp
        src/ui/core/UiPanel.cpp
        src/ui/core/WidgetGroup.cpp
        src/ui/core/UiLayout.cpp
        src/ui/core/JsonReader.cpp
        src/ui/core/UiComponentBase.cpp
        src/ui/core/TextLabel.cpp
        src/ui/core/ViewButton.cpp
        src/ui/core/ImgButton.cpp
        src/ui/core/ImgLabel.cpp
        src/ui/core/CustomSlide.cpp
        src/ui/core/CustomFileList.cpp
        src/ui/core/FileListManager.cpp
        src/media/VideoFileRules.cpp
        src/ui/core/DummySensorDataSource.cpp
        src/ui/core/SensorGraph.cpp
        ${IMGUI_CORE_SOURCES}
    )
    target_compile_features(fullscreen_channel_tests PRIVATE cxx_std_17)
    target_include_directories(fullscreen_channel_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${IMGUI_DIR}"
        "${JSON_INCLUDE_DIR}"
    )
    target_compile_definitions(fullscreen_channel_tests PRIVATE
        FULLSCREEN_TEST_TEMP_ROOT="${PROJECT_BINARY_DIR}/fullscreen-test-data"
    )
    target_link_libraries(fullscreen_channel_tests PRIVATE
        "${SDLVIEWER_SDL3_TARGET}"
        "${SDLVIEWER_MPV_TARGET}"
    )
    if(MSVC)
        target_compile_options(fullscreen_channel_tests PRIVATE /utf-8)
    endif()
    if(WIN32)
        add_custom_command(TARGET fullscreen_channel_tests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
                "${PROJECT_SOURCE_DIR}/third_party/bin"
                "$<TARGET_FILE_DIR:fullscreen_channel_tests>"
            VERBATIM
        )
    endif()
    add_test(NAME fullscreen_ui COMMAND fullscreen_channel_tests)
    set_tests_properties(fullscreen_ui PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        TIMEOUT 15
    )
    add_test(NAME fullscreen_window_events COMMAND fullscreen_channel_tests --window-events)
    set_tests_properties(fullscreen_window_events PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        TIMEOUT 15
    )
    set(SDLVIEWER_TEST_MULTITRACK_FILE "" CACHE FILEPATH "Optional AVI fixture with three video tracks for channel swap tests")
    if(SDLVIEWER_TEST_MEDIA_FILE)
        if(SDLVIEWER_TEST_MULTITRACK_FILE)
            add_test(NAME fullscreen_channels COMMAND fullscreen_channel_tests "${SDLVIEWER_TEST_MEDIA_FILE}" "${SDLVIEWER_TEST_MULTITRACK_FILE}")
        else()
            add_test(NAME fullscreen_channels COMMAND fullscreen_channel_tests "${SDLVIEWER_TEST_MEDIA_FILE}")
        endif()
        set_tests_properties(fullscreen_channels PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 40
        )
    endif()

    add_test(NAME window_smoke COMMAND sdl_window_test --smoke-test)
    set_tests_properties(window_smoke PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        TIMEOUT 10
    )
endif()
