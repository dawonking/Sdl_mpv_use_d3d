#pragma once

#include "render/IRenderBackend.h"
#include <SDL3/SDL_video.h>

#include <atomic>
#include <unordered_map>

struct ImPlotContext;

namespace sdlviewer
{
    // StartOnCallerThread() 전용.
    // 초기화/호출/파괴는 SDL main thread에서 한다.
    class OpenGLRenderBackend final : public IRenderBackend
    {
    public:
        OpenGLRenderBackend() = default;
        ~OpenGLRenderBackend() override;

        RenderStatus InitializePlatform(SDL_Window *window) override;
        void PreparePlatformFrame() override;
        void ShutdownPlatform() noexcept override;

        RenderStatus Initialize(const RenderInitParams &params) override;
        void Shutdown() noexcept override;
        RenderStatus ResizeOutput(WindowMetrics metrics) override;

        RenderStatus EnsureVideoSurface(const VideoCellState &cell) override;
        void ReleaseVideoSurface(VideoId id) noexcept override;
        RenderStatus RenderVideoFrame(VideoId id) override;

        void BeginFrame(
            PixelSize outputSize,
            ClearColor clearColor) override;

        void CompositeVideo(
            const std::vector<VideoLayer> &layers) override;

        void RenderUiFrame(const UiFrameBuilder &buildUi) override;
        RenderStatus Present() override;

        UiTextureHandle LoadUiTexture(
            const std::string &objName,
            const std::string &path) override;

        void ProcessEvent(const SDL_Event &event) override;
        InputCaptureState GetInputCaptureState() const noexcept override;

    private:
        bool MakeCurrent() const noexcept;
        void RequireReady() const;

        // 창은 entry point가 소유한다. backend가 파괴하지 않는다.
        SDL_Window *window_ = nullptr;

        SDL_GLContext glContext_ = nullptr;
        ImGuiContext *imguiContext_ = nullptr;
        ImPlotContext *implotContext_ = nullptr;

        WindowMetrics metrics_{};

        bool sdlReady_ = false;
        bool rendererReady_ = false;
        bool platformFramePrepared_ = false;

        std::atomic<bool> captureMouse_{false};
        std::atomic<bool> captureKeyboard_{false};
        std::atomic<bool> captureTextInput_{false};

        std::unordered_map<std::string, UiTextureHandle> textures_;
    };
}
