#pragma once

#include "render/IRenderBackend.h"

#include <memory>

struct IDCompositionDesktopDevice;
struct IDCompositionVisual2;

namespace sdlviewer
{
    class D3D11RenderBackend final : public IRenderBackend
    {
    public:
        D3D11RenderBackend();
        ~D3D11RenderBackend() override;

        RenderStatus InitializePlatform(SDL_Window *window) override;
        void PreparePlatformFrame() override;
        void ShutdownPlatform() noexcept override;

        RenderStatus Initialize(const RenderInitParams &params) override;
        void Shutdown() noexcept override;

        RenderStatus ResizeOutput(WindowMetrics metrics) override;

        RenderStatus EnsureVideoSurface(const VideoCellState &cell) override;
        void ReleaseVideoSurface(VideoId id) noexcept override;
        RenderStatus RenderVideoFrame(VideoId id) override;

        void BeginFrame(PixelSize outputSize, ClearColor clearColor) override;
        void CompositeVideo(const std::vector<VideoLayer> &layers) override;
        void RenderUiFrame(const UiFrameBuilder &buildUi) override;
        RenderStatus Present() override;

        UiTextureHandle LoadUiTexture(const std::string &objName, const std::string &path) override;

        struct CompositionHandles
        {
            IDCompositionDesktopDevice *device = nullptr;
            IDCompositionVisual2 *rootVisual = nullptr;
            IDCompositionVisual2 *appVisual = nullptr;
        };

        CompositionHandles GetCompositionHandles() const;

        void ProcessEvent(const SDL_Event &event) override;

        InputCaptureState GetInputCaptureState() const noexcept override;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
