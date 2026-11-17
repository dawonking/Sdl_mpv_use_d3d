#include "app/RunApplication.h"

#include "app/Application.h"
#include "app/MainDispatcher.h"
#include "map/IMapView.h"
#include "media/ControlManager.h"
#include "media/PlayState.h"
#include "render/IRenderBackend.h"
#include "ui/core/CustomFileList.h"
#include "ui/core/DummySensorDataSource.h"
#include "ui/core/FileListManager.h"
#include "ui/core/SensorGraph.h"
#include "ui/core/UiComponentBase.h"
#include "ui/core/UiManager.h"
#include "ui/core/UiPanel.h"
#include "ui/core/ViewButton.h"
#include "render/RenderTypes.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace sdlviewer
{

    namespace
    {
        bool QueryWindowMetrics(
            ::SDL_Window *window,
            WindowMetrics &metrics)
        {
            if (window == nullptr)
            {
                return false;
            }

            WindowMetrics next{};

            if (!SDL_GetWindowSize(
                    window,
                    &next.logicalSize.width,
                    &next.logicalSize.height) ||
                !SDL_GetWindowSizeInPixels(
                    window,
                    &next.pixelSize.width,
                    &next.pixelSize.height))
            {
                return false;
            }

            next.pixelDensity =
                SDL_GetWindowPixelDensity(window);

            next.displayScale =
                SDL_GetWindowDisplayScale(window);

            if (!next.IsValid())
            {
                return false;
            }

            metrics = next;
            return true;
        }
    }

    int RunApplication(
        int argc,
        char **argv,
        ::SDL_Window *window,
        std::unique_ptr<IRenderBackend> backend,
        std::unique_ptr<IMapView> mapView,
        std::function<std::string()> folderPicker,
        ApplicationStartMode mode)
    {
        const bool uiOnly =
            mode == ApplicationStartMode::CallerUiOnly;

        if (window == nullptr ||
            backend == nullptr ||
            (!uiOnly &&
             (mapView == nullptr || !folderPicker)))
        {
            std::fprintf(
                stderr,
                "RunApplication: invalid platform dependency\n");
            return 1;
        }

        MainDispatcher dispatcher;
        dispatcher.Init();

        if (dispatcher.EventType() == 0)
        {
            std::fprintf(
                stderr,
                "SDL_RegisterEvents failed: %s\n",
                SDL_GetError());

            return 1;
        }

        {
            int pixelWidth = 0;
            int pixelHeight = 0;

            SDL_GetWindowSizeInPixels(
                window,
                &pixelWidth,
                &pixelHeight);

            std::printf(
                "window %d , %d\n",
                pixelWidth,
                pixelHeight);
        }

        UiManager uiManager;
        Application app(
            uiManager,
            std::move(backend));

        // Application보다 나중에 선언한다.
        // 따라서 함수 종료 시 map이 Application/backend보다 먼저 파괴된다.
        auto ownedMapView = std::move(mapView);
        IMapView *map = ownedMapView.get();

        WindowMetrics metrics{};

        if (!QueryWindowMetrics(window, metrics))
        {
            std::fprintf(
                stderr,
                "QueryWindowMetrics failed: %s\n",
                SDL_GetError());

            return 1;
        }

        std::string initError;
        if (!app.Initialize(
                window,
                metrics,
                &initError,
                mode))
        {
            std::fprintf(
                stderr,
                "Error: Application::Initialize(): %s\n",
                initError.c_str());
            return 1;
        }

        uiManager.SetTextureLoader(
            [&app](
                const std::string &objectName,
                const std::string &path)
            {
                return app.LoadUiTexture(
                    objectName,
                    path);
            });

        uiManager.SetImageBasePath(
            "resources/images/");

        const RenderStatus jsonStatus =
            uiManager.LoadJson();

        if (!jsonStatus)
        {
            std::printf(
                "Warning: UiManager::LoadJson(): %s\n",
                jsonStatus.Message().c_str());
        }

        ControlManager &controlManager =
            app.GetControlManager();

        controlManager.SetUiManager(
            &uiManager);

        CustomFileList *customFileList =
            uiManager.FindWidgetAs<CustomFileList>(
                "customFileList");

        SensorGraph *sensorGraph =
            uiManager.FindWidgetAs<SensorGraph>(
                "SensorGraph");

        FileListManager fileListManager(
            customFileList,
            sensorGraph);

        if (!uiOnly)
        {
            fileListManager.SetFolderPicker(
                std::move(folderPicker));
        }

        fileListManager.SetSensorDataSource(
            GenerateDummySensorData);

        fileListManager.SetUiApplyDispatcher(
            [&controlManager](
                std::function<void()> task)
            {
                controlManager.PostTaskFromWorker(
                    std::move(task));
            });

        controlManager.SetFileListManager(
            &fileListManager);

        int currentVideoLayout = 1;
        WindowMetrics renderMetrics = metrics;

        struct VideoDrag
        {
            int channel = -1;
            float x = 0.0f;
            float y = 0.0f;
            std::uint64_t mediaRevision = 0;
            bool moved = false;
        } videoDrag;

        std::atomic_bool fullscreen{
            (SDL_GetWindowFlags(window) &
             SDL_WINDOW_FULLSCREEN) != 0};
        bool fullscreenPending = false;
        bool requestedFullscreen = fullscreen.load();
        bool wasMaximized = false;
        Uint64 fullscreenRequestTime = 0;
        SDL_Rect windowedBounds{};
        bool hasWindowedBounds = false;
        bool windowRestorePending = false;
        bool windowBoundsApplied = false;
        bool restoreMaximized = false;

        auto saveWindowedBounds = [&]
        {
            const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
            if (fullscreenPending || windowRestorePending ||
                (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)) != 0)
                return;

            SDL_Rect bounds{};
            if (SDL_GetWindowPosition(window, &bounds.x, &bounds.y) &&
                SDL_GetWindowSize(window, &bounds.w, &bounds.h) &&
                bounds.w > 0 && bounds.h > 0)
            {
                windowedBounds = bounds;
                hasWindowedBounds = true;
            }
        };

        auto toggleFullscreen = [&]
        {
            if (fullscreenPending || windowRestorePending)
                return;

            const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
            requestedFullscreen = (flags & SDL_WINDOW_FULLSCREEN) == 0;
            if (requestedFullscreen)
            {
                saveWindowedBounds();
                wasMaximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
            }

            if (!SDL_SetWindowFullscreen(window, requestedFullscreen))
            {
                std::fprintf(stderr, "SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
                return;
            }

            fullscreenPending = true;
            fullscreenRequestTime = SDL_GetTicks();
        };

        auto applyVideoLayout =
            [&](int layoutCount)
        {
            videoDrag.channel = -1;
            if (uiOnly)
            {
                currentVideoLayout = layoutCount;
                return true;
            }

            const RectF viewport =
                uiManager.GetVideoViewport();

            if (!viewport.IsValid())
            {
                return false;
            }

            std::array<RectF, 6> homeRects{};
            homeRects[0] = viewport;

            for (int channel = 1;
                 channel < 6;
                 ++channel)
            {
                UiComponentBase *subArea =
                    uiManager.FindWidgetAs<UiComponentBase>(
                        "SubArea" +
                        std::to_string(channel));

                if (subArea == nullptr)
                {
                    return false;
                }

                const ImVec2 position =
                    subArea->GetWindowPos();

                const ImVec2 size =
                    subArea->GetSize();

                homeRects[channel] = {
                    position.x,
                    position.y,
                    size.x,
                    size.y};

                if (!homeRects[channel].IsValid())
                {
                    return false;
                }
            }

            auto &players =
                app.GetMpvPlayerManager();

            for (int channel = 0;
                 channel < 6;
                 ++channel)
            {
                players.SetHomeDestination(
                    channel,
                    homeRects[channel]);
            }

            const RenderStatus status =
                app.SetVideoGridLayout(
                    layoutCount,
                    viewport,
                    renderMetrics.pixelDensity);

            if (!status)
            {
                return false;
            }

            currentVideoLayout = layoutCount;
            return true;
        };

        controlManager.BindButton(
            "LocalOpenBtn",
            [&dispatcher, &fileListManager, uiOnly]
            {
                if (uiOnly)
                {
                    return;
                }

                dispatcher.PostTask(
                    [&fileListManager]
                    {
                        fileListManager.OpenFileList();
                    });
            });

        controlManager.BindButton(
            "FullScreenBtn",
            [&]
            {
                dispatcher.PostTask(toggleFullscreen);
            },
            [&fullscreen]
            {
                return fullscreen.load();
            });

        controlManager.BindButton(
            "MinBtn",
            [&]
            {
                dispatcher.PostTask([&]
                {
                    if (fullscreenPending || windowRestorePending)
                        return;
                    saveWindowedBounds();
                    if (!SDL_MinimizeWindow(window))
                        std::fprintf(stderr, "Window minimize failed: %s\n", SDL_GetError());
                });
            });

        controlManager.BindButton(
            "MaximumBtn",
            [&]
            {
                dispatcher.PostTask([&]
                {
                    if (fullscreenPending || windowRestorePending)
                        return;

                    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
                    if ((flags & SDL_WINDOW_FULLSCREEN) != 0)
                    {
                        toggleFullscreen();
                        if (fullscreenPending)
                            wasMaximized = true;
                        return;
                    }

                    saveWindowedBounds();
                    const bool maximize = (flags & SDL_WINDOW_MAXIMIZED) == 0;
                    if (!(maximize ? SDL_MaximizeWindow(window) : SDL_RestoreWindow(window)))
                    {
                        std::fprintf(stderr, "Window maximize/restore failed: %s\n", SDL_GetError());
                        return;
                    }

                    restoreMaximized = maximize;
                    windowRestorePending = true;
                    windowBoundsApplied = false;
                    fullscreenRequestTime = SDL_GetTicks();
                });
            });

        controlManager.BindButton(
            "CloseBtn",
            [&]
            {
                dispatcher.PostTask([&]
                {
                    SDL_Event close{};
                    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
                    close.window.windowID = SDL_GetWindowID(window);
                    if (!SDL_PushEvent(&close))
                        std::fprintf(stderr, "Window close request failed: %s\n", SDL_GetError());
                });
            });

        controlManager.BindFileListFunc(
            "customFileList",
            [&controlManager, uiOnly](int index)
            {
                if (uiOnly)
                {
                    return;
                }

                controlManager.FilelistClickAndPlay(
                    index);
            },
            [&controlManager, uiOnly]
            {
                if (uiOnly)
                {
                    return false;
                }

                switch (controlManager.GetPlayState())
                {
                case PlayState::NotLoad:
                case PlayState::Pause:
                case PlayState::Playing:
                    return true;

                default:
                    return false;
                }
            });

        const std::string mapUrl =
            "https://www.google.com/maps";

        bool mapCreated = false;
        bool mapVisible = map != nullptr;
        bool mapShown = false;
        bool mapUpdateRequested = false;
        bool requestedMapVisible = false;
        MapViewBounds requestedMapBounds{};

        if (UiComponentBase *mapArea =
                uiManager.FindWidgetAs<UiComponentBase>("MapArea"))
        {
            if (mapVisible)
                mapArea->Show();
            else
                mapArea->Hide();
        }

        auto updateMapBounds = [&]
        {
            if (map == nullptr)
            {
                return;
            }

            ViewButton *mapWidget =
                uiManager.FindWidgetAs<ViewButton>(
                    "MapWidget");

            if (mapWidget == nullptr)
            {
                return;
            }

            const ImVec2 mapPosition =
                mapWidget->GetWindowPos();

            const ImVec2 mapSize =
                mapWidget->GetSize();

            if (mapSize.x <= 0.0f ||
                mapSize.y <= 0.0f)
            {
                return;
            }

            const MapViewBounds mapBounds{
                static_cast<int>(
                    std::lround(mapPosition.x)),
                static_cast<int>(
                    std::lround(mapPosition.y)),
                static_cast<int>(
                    std::lround(
                        mapPosition.x +
                        mapSize.x)),
                static_cast<int>(
                    std::lround(
                        mapPosition.y +
                        mapSize.y))};

            const UiPanel *rightPanel =
                uiManager.FindWidgetAs<UiPanel>("rightArea");
            const UiComponentBase *mapArea =
                uiManager.FindWidgetAs<UiComponentBase>("MapArea");
            const bool showMap = mapVisible &&
                                 mapArea != nullptr && !mapArea->IsHidden() &&
                                 (!uiManager.IsFullscreen() ||
                                  (rightPanel != nullptr && !rightPanel->IsHidden()));

            if (mapUpdateRequested && requestedMapVisible == showMap &&
                requestedMapBounds.left == mapBounds.left &&
                requestedMapBounds.top == mapBounds.top &&
                requestedMapBounds.right == mapBounds.right &&
                requestedMapBounds.bottom == mapBounds.bottom)
            {
                return;
            }

            mapUpdateRequested = true;
            requestedMapVisible = showMap;
            requestedMapBounds = mapBounds;
            // UI geometry is read on the render thread; native map calls stay on main.
            dispatcher.PostTask(
                [&, mapBounds, showMap]
                {
                    mapShown = showMap;
                    if (!mapCreated)
                    {
                        mapCreated = map->Create(
                            window, mapBounds, mapUrl,
                            [map, &mapShown] { map->SetVisible(mapShown); });
                        if (!mapCreated)
                            std::printf("Warning: IMapView::Create() failed\n");
                    }
                    else
                    {
                        map->SetBounds(mapBounds);
                    }
                    map->SetVisible(showMap);
                });
        };

        controlManager.BindButton(
            "MapBtn",
            [&]
            {
                if (map == nullptr)
                {
                    return;
                }

                mapVisible = !mapVisible;

                UiComponentBase *mapArea =
                    uiManager.FindWidgetAs<UiComponentBase>(
                        "MapArea");

                if (mapArea != nullptr)
                {
                    if (mapVisible)
                    {
                        mapArea->Show();
                    }
                    else
                    {
                        mapArea->Hide();
                    }
                }

                if (UiPanel *rightPanel =
                        uiManager.FindWidgetAs<UiPanel>(
                            "rightArea"))
                {
                    const ImVec2 size =
                        rightPanel->GetSize();

                    rightPanel->Resize(
                        size.x,
                        size.y);
                }

                updateMapBounds();
            },
            [&mapVisible]
            {
                return mapVisible;
            });

        controlManager.BindButton(
            "OneChBtn",
            [&]
            {
                applyVideoLayout(1);
            },
            [&]
            {
                return currentVideoLayout == 1;
            });

        controlManager.BindButton(
            "TwoChBtn",
            [&]
            {
                applyVideoLayout(2);
            },
            [&]
            {
                return currentVideoLayout == 2;
            });

        controlManager.BindButton(
            "FourChBtn",
            [&]
            {
                applyVideoLayout(4);
            },
            [&]
            {
                return currentVideoLayout == 4;
            });

        controlManager.BindButton(
            "SixChBtn",
            [&]
            {
                applyVideoLayout(6);
            },
            [&]
            {
                return currentVideoLayout == 6;
            });

        controlManager.BindButton(
            "PlayBtn",
            [&controlManager, uiOnly]
            {
                if (uiOnly)
                {
                    return;
                }

                controlManager.TogglePlayback();
            },
            [&controlManager]
            {
                return controlManager.GetPlayState() ==
                       PlayState::Playing;
            });

        controlManager.BindButton(
            "StopBtn",
            [&controlManager, uiOnly]
            {
                if (uiOnly)
                {
                    return;
                }

                controlManager.StopCallBack();
            },
            [&controlManager]
            {
                return controlManager.GetPlayState() ==
                       PlayState::Playing;
            });

        controlManager.BindSlideFunc(
            "SlideArea_Component",
            [&controlManager, uiOnly](float value)
            {
                if (uiOnly)
                {
                    return;
                }

                controlManager.RequestSeek(
                    value);
            },
            [&controlManager]
            {
                return controlManager.GetProgressRatio();
            },
            [&controlManager, uiOnly]
            {
                if (uiOnly)
                {
                    return false;
                }

                switch (controlManager.GetPlayState())
                {
                case PlayState::Pause:
                case PlayState::Playing:
                    return true;

                default:
                    return false;
                }
            });

        controlManager.BindTextFunc(
            "FileNameText",
            [&controlManager]() -> std::string
            {
                const PlayState state = controlManager.GetPlayState();
                if (state == PlayState::Playing || state == PlayState::Pause)
                    return controlManager.GetCurrentFileName();
                return {};
            });

        controlManager.BindTextFunc(
            "curTimeText",
            [&controlManager]
            {
                return ControlManager::FormatTime(
                    controlManager.GetPositionSec());
            });

        controlManager.BindTextFunc(
            "endTimeText",
            [&controlManager]
            {
                return ControlManager::FormatTime(
                    controlManager.GetDurationSec());
            });

        if (!uiOnly)
        {
            const auto playbackAction = [&app](std::function<void()> action)
            {
                return [&app, action = std::move(action)]
                {
                    app.PostPlaybackTask(action);
                };
            };

            const std::pair<const char *, PlaybackSpeed> speedButtons[] = {
                {"HalfSpeedBtn", PlaybackSpeed::Half},
                {"NomalSpeedBtn", PlaybackSpeed::Normal},
                {"HalfDoubleBtn", PlaybackSpeed::OnePointFive},
                {"DoubleBtn", PlaybackSpeed::Double}};

            for (const auto &button : speedButtons)
            {
                const PlaybackSpeed speed = button.second;
                controlManager.BindButton(
                    button.first,
                    playbackAction([&controlManager, speed]
                    {
                        controlManager.SetPlaybackSpeed(speed);
                    }),
                    [&controlManager, speed]
                    {
                        return controlManager.GetPlaybackSpeed() == speed;
                    });
            }

            controlManager.BindButton(
                "VolMuteBtn",
                playbackAction([&controlManager] { controlManager.ToggleMute(); }),
                [&controlManager] { return controlManager.IsMuted(); });

            controlManager.BindSlideFunc(
                "VolumeSlider",
                [&app, &controlManager](float ratio)
                {
                    app.PostPlaybackTask(
                        [&controlManager, ratio]
                        {
                            if (controlManager.IsAudioInitialized())
                                controlManager.SetVolume(ratio);
                        });
                },
                [&controlManager] { return controlManager.GetVolumeRatio(); },
                [&controlManager] { return controlManager.IsAudioInitialized(); });

            controlManager.BindButton(
                "PrevBtn",
                playbackAction([&controlManager] { controlManager.PlayPreviousFile(); }));
            controlManager.BindButton(
                "listDeleteBtn",
                playbackAction([&controlManager] { controlManager.DeleteCheckedFiles(); }));
            controlManager.BindButton(
                "NextBtn",
                playbackAction([&controlManager] { controlManager.PlayNextFile(); }));
            controlManager.BindButton(
                "LoopBtn",
                playbackAction([&controlManager] { controlManager.ToggleLooping(); }),
                [&controlManager] { return controlManager.IsLooping(); });

            ViewButton *seekModeButton =
                uiManager.FindWidgetAs<ViewButton>("SecFrameBtn");
            if (seekModeButton != nullptr)
                seekModeButton->SetText("Sec###SecFrameBtn");

            controlManager.BindButton(
                "SecFrameBtn",
                playbackAction([&controlManager, seekModeButton]
                {
                    controlManager.ToggleSeekMode();
                    if (seekModeButton != nullptr)
                        seekModeButton->SetText(controlManager.IsFrameMode()
                                                    ? "Frame###SecFrameBtn"
                                                    : "Sec###SecFrameBtn");
                }),
                [&controlManager] { return controlManager.IsFrameMode(); });

            controlManager.BindButton(
                "backWindBtn",
                playbackAction([&controlManager] { controlManager.StepPlayback(-1); }));
            controlManager.BindButton(
                "forWindBtn",
                playbackAction([&controlManager] { controlManager.StepPlayback(1); }));
        }

        const std::pair<const char *, VideoFlag> flipButtons[] = {
            {"HorMirrorBtn", VideoFlag::HorizontalFlip},
            {"VerMirrorBtn", VideoFlag::VerticalFlip}};
        for (const auto &button : flipButtons)
        {
            const VideoFlag flag = button.second;
            controlManager.BindButton(
                button.first,
                [&, flag]
                {
                    if (uiOnly)
                        return;
                    app.PostPlaybackTask([&, flag]
                    {
                        auto &players = app.GetMpvPlayerManager();
                        if (controlManager.GetPlayState() != PlayState::Playing ||
                            players.GetSelectedChannel() < 0)
                            return;
                        const RenderStatus status = players.ToggleSelectedFlip(flag);
                        if (!status)
                            std::fprintf(stderr, "Video flip failed: %s\n", status.Message().c_str());
                    });
                },
                [&, flag] { return app.GetMpvPlayerManager().GetSelectedFlip(flag); });
        }

        auto refreshWindowState = [&]
        {
            WindowMetrics nextMetrics{};
            if (!QueryWindowMetrics(window, nextMetrics))
                return false;

            const bool actualFullscreen =
                (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
            fullscreen.store(actualFullscreen);
            app.PostPlaybackTask(
                [&, nextMetrics, actualFullscreen]
                {
                    videoDrag.channel = -1;
                    renderMetrics = nextMetrics;
                    uiManager.SetFullscreen(actualFullscreen);
                    app.SetFullscreen(actualFullscreen);
                    if (!uiOnly)
                    {
                        const RenderStatus status =
                            app.GetMpvPlayerManager().SetFullscreen(actualFullscreen);
                        if (!status)
                            std::fprintf(stderr, "Fullscreen video update failed: %s\n", status.Message().c_str());
                    }
                    if (app.Resize(nextMetrics))
                    {
                        if (!applyVideoLayout(currentVideoLayout))
                            std::fprintf(stderr, "Video layout update failed.\n");
                        updateMapBounds();
                    }
                    else
                    {
                        std::fprintf(stderr, "Window resize update failed.\n");
                    }
                });
            return true;
        };

        auto completeFullscreenTransition = [&]
        {
            if (!fullscreenPending || windowRestorePending)
                return;

            const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
            if (((flags & SDL_WINDOW_FULLSCREEN) != 0) != requestedFullscreen)
                return;

            if (requestedFullscreen)
            {
                fullscreenPending = false;
                return;
            }

            // Keep the saved normal bounds until the asynchronous restoration finishes.
            windowRestorePending = true;
            windowBoundsApplied = false;
            restoreMaximized = wasMaximized;
            fullscreenRequestTime = SDL_GetTicks();
            if (wasMaximized)
            {
                if (!SDL_MaximizeWindow(window))
                    std::fprintf(stderr, "Window maximize restore failed: %s\n", SDL_GetError());
            }
            else if ((flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)) != 0)
            {
                if (!SDL_RestoreWindow(window))
                    std::fprintf(stderr, "Window restore failed: %s\n", SDL_GetError());
            }
        };

        auto updateWindowRestore = [&]
        {
            if (!windowRestorePending)
                return;

            const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
            bool restored = false;
            if ((flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED)) == 0)
            {
                if (restoreMaximized)
                {
                    restored = (flags & SDL_WINDOW_MAXIMIZED) != 0;
                }
                else if ((flags & SDL_WINDOW_MAXIMIZED) == 0)
                {
                    if (!windowBoundsApplied)
                    {
                        windowBoundsApplied = true;
                        if (hasWindowedBounds)
                        {
                            if (!SDL_SetWindowPosition(window, windowedBounds.x, windowedBounds.y))
                                std::fprintf(stderr, "Window position restore failed: %s\n", SDL_GetError());
                            if (!SDL_SetWindowSize(window, windowedBounds.w, windowedBounds.h))
                                std::fprintf(stderr, "Window size restore failed: %s\n", SDL_GetError());
                        }
                    }

                    SDL_Rect bounds{};
                    restored = !hasWindowedBounds ||
                        (SDL_GetWindowPosition(window, &bounds.x, &bounds.y) &&
                         SDL_GetWindowSize(window, &bounds.w, &bounds.h) &&
                         bounds.x == windowedBounds.x && bounds.y == windowedBounds.y &&
                         bounds.w == windowedBounds.w && bounds.h == windowedBounds.h);
                }
            }

            if (restored || SDL_GetTicks() - fullscreenRequestTime >= 2000)
            {
                if (!restored)
                    std::fprintf(stderr, "Window restoration did not reach the requested bounds or state.\n");
                windowRestorePending = false;
                fullscreenPending = false;
                // The window manager may constrain the requested position or size.
                saveWindowedBounds();
                refreshWindowState();
            }
        };

        auto handleVideoPointer = [&](Uint32 type, float x, float y, bool held)
        {
            const auto &state = controlManager.GetPlaybackState();
            if (videoDrag.channel >= 0 &&
                (videoDrag.mediaRevision != state.mediaRevision ||
                 (state.playState != PlayState::Playing && state.playState != PlayState::Pause)))
            {
                videoDrag.channel = -1;
            }

            if (type == SDL_EVENT_MOUSE_MOTION)
            {
                if (videoDrag.channel >= 0)
                {
                    const float dx = x - videoDrag.x;
                    const float dy = y - videoDrag.y;
                    videoDrag.moved = videoDrag.moved || dx * dx + dy * dy >= 25.0f;
                    if (!held)
                        videoDrag.channel = -1;
                }
                uiManager.UpdateFullscreenHover(x, y, held || ImGui::IsAnyItemActive());
            }
            else if (type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                videoDrag.channel = -1;
                uiManager.UpdateFullscreenHover(x, y);
                if (!uiOnly && !uiManager.IsVideoInputBlocked(x, y) &&
                    !app.GetMpvPlayerManager().IsTrackChangePending() &&
                    !app.GetMpvPlayerManager().IsVideoFlagPending() &&
                    !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                    (state.playState == PlayState::Playing || state.playState == PlayState::Pause))
                {
                    videoDrag.channel = app.GetMpvPlayerManager().HitTestChannel(x, y);
                    videoDrag.x = x;
                    videoDrag.y = y;
                    videoDrag.mediaRevision = state.mediaRevision;
                    videoDrag.moved = false;
                }
            }
            else if (type == SDL_EVENT_MOUSE_BUTTON_UP)
            {
                const float dx = x - videoDrag.x;
                const float dy = y - videoDrag.y;
                videoDrag.moved = videoDrag.moved || dx * dx + dy * dy >= 25.0f;
                if (videoDrag.channel >= 0 &&
                    !uiManager.IsVideoInputBlocked(x, y) &&
                    !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                {
                    auto &players = app.GetMpvPlayerManager();
                    const int target = players.HitTestChannel(x, y);
                    if (videoDrag.moved && target >= 0 && target != videoDrag.channel)
                    {
                        const RenderStatus status = players.SwapChannels(videoDrag.channel, target);
                        if (!status)
                            std::fprintf(stderr, "SwapChannels failed: %s\n", status.Message().c_str());
                    }
                    else if (!videoDrag.moved && target == videoDrag.channel && state.playState == PlayState::Playing)
                    {
                        const RenderStatus status = players.SelectChannel(target);
                        if (!status)
                            std::fprintf(stderr, "Video selection failed: %s\n", status.Message().c_str());
                    }
                }
                videoDrag.channel = -1;
                // Let a slider/button receive this release before hiding its panel.
                uiManager.UpdateFullscreenHover(x, y, ImGui::IsAnyItemActive());
            }
            updateMapBounds();
        };

        SDL_SetWindowPosition(
            window,
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED);

        SDL_ShowWindow(window);
        saveWindowedBounds();

        if (!refreshWindowState())
        {
            std::fprintf(
                stderr,
                "Initial window resize failed: %s\n",
                SDL_GetError());

            return 1;
        }

        bool done = false;
        int result = 0;

        if (argc == 2 &&
            std::strcmp(
                argv[1],
                "--smoke-test") == 0)
        {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;

            if (!SDL_PushEvent(&quit))
            {
                std::fprintf(
                    stderr,
                    "Error: SDL_PushEvent(): %s\n",
                    SDL_GetError());

                result = 1;
                done = true;
            }
        }

        while (!done)
        {
            SDL_Event event{};

            if (fullscreenPending && !windowRestorePending &&
                SDL_GetTicks() - fullscreenRequestTime >= 2000)
            {
                completeFullscreenTransition();
                if (fullscreenPending && !windowRestorePending)
                {
                    fullscreenPending = false;
                    std::fprintf(stderr, "Fullscreen transition was not applied.\n");
                }
                refreshWindowState();
            }
            updateWindowRestore();

            if (fullscreenPending || windowRestorePending)
            {
                if (!SDL_WaitEventTimeout(&event, 100))
                    continue;
            }
            else if (!SDL_WaitEvent(&event))
            {
                std::fprintf(
                    stderr,
                    "Error: SDL_WaitEvent(): %s\n",
                    SDL_GetError());

                result = 1;
                break;
            }

            const bool fullscreenKey =
                (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                event.key.windowID == SDL_GetWindowID(window) &&
                (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0 &&
                ((!app.GetInputCaptureState().textInput &&
                  (event.key.scancode == SDL_SCANCODE_RETURN ||
                   event.key.scancode == SDL_SCANCODE_KP_ENTER)) ||
                 (event.key.scancode == SDL_SCANCODE_ESCAPE &&
                  (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0));

            const bool playbackKey =
                !uiOnly &&
                (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                event.key.windowID == SDL_GetWindowID(window) &&
                (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0 &&
                !app.GetInputCaptureState().textInput &&
                (event.key.scancode == SDL_SCANCODE_SPACE ||
                 event.key.scancode == SDL_SCANCODE_LEFT ||
                 event.key.scancode == SDL_SCANCODE_RIGHT);

            if (fullscreenKey)
            {
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
                    toggleFullscreen();
            }
            else if (playbackKey)
            {
                // Do not also activate an ImGui button with the same key.
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
                {
                    const SDL_Scancode key = event.key.scancode;
                    app.PostPlaybackTask(
                        [&controlManager, key]
                        {
                            if (key == SDL_SCANCODE_SPACE)
                                controlManager.TogglePlayback();
                            else
                                controlManager.StepPlayback(key == SDL_SCANCODE_LEFT ? -1 : 1);
                        });
                }
            }
            else
            {
                app.ProcessEvent(event);
            }
            dispatcher.ProcessPendingTasks();

            switch (event.type)
            {
            case SDL_EVENT_MOUSE_MOTION:
                if (event.motion.windowID == SDL_GetWindowID(window))
                {
                    app.PostPlaybackTask(
                        [&, mouse = event.motion]
                        {
                            handleVideoPointer(SDL_EVENT_MOUSE_MOTION, mouse.x, mouse.y,
                                               (mouse.state & SDL_BUTTON_LMASK) != 0);
                        });
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.windowID == SDL_GetWindowID(window) &&
                    event.button.button == SDL_BUTTON_LEFT)
                {
                    app.PostPlaybackTask(
                        [&, mouse = event.button]
                        {
                            handleVideoPointer(mouse.type, mouse.x, mouse.y,
                                               mouse.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                        });
                }
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (event.window.windowID == SDL_GetWindowID(window))
                {
                    app.PostPlaybackTask(
                        [&]
                        {
                            videoDrag.channel = -1;
                            uiManager.UpdateFullscreenHover(-1.0f, -1.0f, ImGui::IsAnyItemActive());
                            updateMapBounds();
                        });
                }
                break;
            case SDL_EVENT_QUIT:
                done = true;
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID ==
                    SDL_GetWindowID(window))
                {
                    done = true;
                }
                break;
            case SDL_EVENT_WINDOW_MOVED:
                if (event.window.windowID == SDL_GetWindowID(window))
                    saveWindowedBounds();
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            {
                if (event.window.windowID != SDL_GetWindowID(window))
                    break;

                if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
                    event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
                {
                    completeFullscreenTransition();
                }

                saveWindowedBounds();
                if (!refreshWindowState())
                    std::fprintf(stderr, "Window metrics refresh failed: %s\n", SDL_GetError());
                break;
            }

            default:
                break;
            }
        }

        // WebView2 map이 D3D composition backend보다 먼저 파괴되어야 한다.
        ownedMapView.reset();
        app.Shutdown();

        return result;
    }
}
