#pragma once

#include "media/ControlManager.h"
#include "media/MpvPlayerManager.h"
#include "render/SdlRenderThread.h"
#include "ui/core/IUiFrame.h"

#include <future>
#include <string>

union SDL_Event;

namespace sdlviewer
{
    enum class ApplicationStartMode
    {
        WorkerWithMedia,
        CallerUiOnly
    };

    class Application final
    {
    public:
        explicit Application(IUiFrame &uiFrame, std::unique_ptr<IRenderBackend> backend);
        ~Application();

        Application(const Application &) = delete;
        Application &operator=(const Application &) = delete;

        bool Initialize(
            SDL_Window *window,
            WindowMetrics metrics,
            std::string *error = nullptr,
            ApplicationStartMode mode =
                ApplicationStartMode::WorkerWithMedia);

        void Shutdown() noexcept;

        bool SetVideoCell(const VideoCellState &cell);
        std::future<void> RemoveVideoCell(VideoId id);
        void NotifyVideoFrame(VideoId id);

        bool Resize(WindowMetrics metrics);
        void SetFullscreen(bool fullscreen);

        void ProcessEvent(const SDL_Event &event);
        void PostPlaybackTask(std::function<void()> task);

        InputCaptureState GetInputCaptureState() const noexcept;

        UiTextureHandle LoadUiTexture(const std::string &objName, const std::string &path);

        MpvPlayerManager &GetMpvPlayerManager() noexcept { return mpvPlayerManager_; }
        ControlManager &GetControlManager() noexcept { return controlManager_; }
        RenderStatus SetVideoGridLayout(
            int layoutCount,
            RectF viewport,
            float pixelDensity);

    private:
        static constexpr int kVideoChannelCount = 6;

        IUiFrame &uiFrame_;
        SdlRenderThread renderThread_;
        ControlManager controlManager_;
        MpvPlayerManager mpvPlayerManager_;
    };
}
