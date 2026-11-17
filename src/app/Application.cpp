#include "app/Application.h"

#include <mpv/client.h>

#include <cstdio>
#include <functional>
#include <utility>

namespace sdlviewer
{
    namespace
    {
        constexpr RectF kDefaultVideoViewport{0.0f, 0.0f, 1920.0f, 1080.0f};
    }

    Application::Application(
        IUiFrame &uiFrame,
        std::unique_ptr<IRenderBackend> backend)
        : uiFrame_(uiFrame),
          renderThread_(
              std::move(backend),
              [this]
              {
                  controlManager_.Update();
                  uiFrame_.RenderUi();
              }),
          controlManager_(),
          mpvPlayerManager_(*this)
    {
    }

    Application::~Application()
    {
        Shutdown();
    }

    bool Application::Initialize(
        SDL_Window *window,
        WindowMetrics metrics,
        std::string *error,
        ApplicationStartMode mode)
    {
        if (mode == ApplicationStartMode::CallerUiOnly)
        {
            return renderThread_.StartOnCallerThread(
                window,
                metrics,
                error);
        }

        const auto postPlaybackTask =
            [this](std::function<void()> task)
        {
            controlManager_.PostTaskFromMedia(std::move(task));
            renderThread_.RequestFrame();
        };

        for (int channelIndex = 0;
             channelIndex < kVideoChannelCount;
             ++channelIndex)
        {
            MpvPlaybackCallbacks playbackCallbacks;

            playbackCallbacks.onCommandError =
                [channelIndex](int mpvError)
            {
                std::fprintf(
                    stderr,
                    "[mpv channel %d] command failed: %s\n",
                    channelIndex,
                    mpv_error_string(mpvError));
            };

            playbackCallbacks.onPlaybackEnded =
                [this, channelIndex, postPlaybackTask](
                    int reason,
                    int mpvError)
            {
                if (reason == MPV_END_FILE_REASON_ERROR)
                {
                    std::fprintf(
                        stderr,
                        "[mpv channel %d] playback failed: %s\n",
                        channelIndex,
                        mpv_error_string(mpvError));
                }

                // 대표 채널의 정상 종료만 다음 목록 항목으로 진행한다.
                if (channelIndex == 0 &&
                    reason == MPV_END_FILE_REASON_EOF)
                {
                    postPlaybackTask(
                        [this]
                        {
                            controlManager_.OnPlaybackEnded();
                        });
                }
                else if (channelIndex == 0 && reason == MPV_END_FILE_REASON_ERROR)
                {
                    postPlaybackTask(
                        [this]
                        {
                            mpvPlayerManager_.StopAll();
                            controlManager_.SetPlayState(PlayState::NotLoad);
                        });
                }
            };

            playbackCallbacks.onFirstFrameDecoded =
                [this, channelIndex, postPlaybackTask]
            {
                postPlaybackTask(
                    [this, channelIndex]
                    {
                        mpvPlayerManager_.SetVisible(
                            channelIndex,
                            true);

                        // 대표 재생 상태는 1채널 기준
                        if (channelIndex == 0)
                        {
                            controlManager_.SetPlayState(
                                PlayState::Playing);
                        }
                    });
            };

            // UI의 대표 재생 상태는 1채널만 사용한다.
            if (channelIndex == 0)
            {
                playbackCallbacks.onTimePosition =
                    [this](double positionSec)
                {
                    controlManager_.PostTimeFromWorker(positionSec);
                    renderThread_.RequestFrame();
                };

                playbackCallbacks.onDurationChanged =
                    [this, postPlaybackTask](double durationSec)
                {
                    postPlaybackTask(
                        [this, durationSec]
                        {
                            controlManager_.SetDurationFromMedia(
                                durationSec);
                        });
                };

                playbackCallbacks.onPauseChanged =
                    [this, postPlaybackTask](bool paused)
                {
                    postPlaybackTask(
                        [this, paused]
                        {
                            const PlayState state =
                                controlManager_.GetPlayState();

                            if (state == PlayState::Playing ||
                                state == PlayState::Pause)
                            {
                                controlManager_.SetPlayState(
                                    paused
                                        ? PlayState::Pause
                                        : PlayState::Playing);
                            }
                        });
                };

                playbackCallbacks.onSeekCompleted =
                    [this, postPlaybackTask]
                {
                    postPlaybackTask(
                        [this]
                        {
                            controlManager_.OnSeekCompleted();
                        });
                };
            }

            const RenderStatus status =
                mpvPlayerManager_.AddChannel(
                    channelIndex,
                    kDefaultVideoViewport,
                    std::move(playbackCallbacks));

            if (!status)
            {
                if (error != nullptr)
                {
                    *error =
                        "Failed to add video channel " +
                        std::to_string(channelIndex) +
                        ": " + status.Message();
                }

                return false;
            }
        }

        MpvControlCallbacks callbacks;

        callbacks.pauseAll = [this]
        {
            mpvPlayerManager_.PauseAll();
        };

        callbacks.stopAll = [this]
        {
            mpvPlayerManager_.StopAll();
        };

        callbacks.seek = [this](float ratio)
        {
            mpvPlayerManager_.SeekAll(ratio);
        };

        callbacks.setVideo = [this](const std::string &path)
        {
            const RenderStatus status = mpvPlayerManager_.SetVideo(path);
            if (!status)
                std::fprintf(stderr, "SetVideo failed: %s\n", status.Message().c_str());
            return static_cast<bool>(status);
        };

        callbacks.isFileLoaded = [this](const std::string &path)
        {
            return mpvPlayerManager_.IsFileLoaded(path);
        };

        callbacks.setPlaybackSpeed = [this](double rate)
        {
            const RenderStatus status = mpvPlayerManager_.SetPlaybackSpeed(rate);
            if (!status)
                std::fprintf(stderr, "SetPlaybackSpeed failed: %s\n", status.Message().c_str());
            return static_cast<bool>(status);
        };

        callbacks.setAudio = [this](double volume, bool muted)
        {
            const RenderStatus status = mpvPlayerManager_.SetAudio(volume, muted);
            if (!status)
                std::fprintf(stderr, "SetAudio failed: %s\n", status.Message().c_str());
            return static_cast<bool>(status);
        };

        callbacks.seekSeconds = [this](double positionSec)
        {
            const RenderStatus status = mpvPlayerManager_.SeekSecondsAll(positionSec);
            if (!status)
                std::fprintf(stderr, "SeekSeconds failed: %s\n", status.Message().c_str());
            return static_cast<bool>(status);
        };

        callbacks.stepFrame = [this](int direction)
        {
            const RenderStatus status = mpvPlayerManager_.StepFrameAll(direction);
            if (!status)
                std::fprintf(stderr, "StepFrame failed: %s\n", status.Message().c_str());
            return static_cast<bool>(status);
        };

        controlManager_.SetMpvControlCallbacks(
            std::move(callbacks));

        return renderThread_.Start(
            window,
            metrics,
            error);
    }

    void Application::Shutdown() noexcept
    {
        renderThread_.Stop();
    }

    bool Application::SetVideoCell(const VideoCellState &cell)
    {
        return renderThread_.UpsertVideoCell(cell);
    }

    std::future<void> Application::RemoveVideoCell(VideoId id)
    {
        return renderThread_.RemoveVideoCell(id);
    }

    void Application::NotifyVideoFrame(VideoId id)
    {
        renderThread_.NotifyVideoFrame(id);
    }

    bool Application::Resize(WindowMetrics metrics)
    {
        if (!metrics.IsValid())
        {
            return false;
        }

        uiFrame_.OnResize(metrics.logicalSize);
        return renderThread_.RequestResize(metrics);
    }

    void Application::SetFullscreen(bool fullscreen)
    {
        renderThread_.SetLayerOrder(
            fullscreen
                ? FrameLayerOrder::VideoThenUi
                : FrameLayerOrder::UiThenVideo);
    }

    void Application::ProcessEvent(const SDL_Event &event)
    {
        renderThread_.ProcessEvent(event);
    }

    void Application::PostPlaybackTask(std::function<void()> task)
    {
        controlManager_.PostTaskFromWorker(std::move(task));
        renderThread_.RequestFrame();
    }

    InputCaptureState Application::GetInputCaptureState() const noexcept
    {
        return renderThread_.GetInputCaptureState();
    }

    UiTextureHandle Application::LoadUiTexture(const std::string &objName, const std::string &path)
    {
        return renderThread_.LoadUiTexture(objName, path);
    }

    RenderStatus Application::SetVideoGridLayout(
        int layoutCount,
        RectF viewport,
        float pixelDensity)
    {
        return mpvPlayerManager_.SetGridLayout(
            layoutCount,
            viewport,
            pixelDensity);
    }

} // namespace sdlviewer
