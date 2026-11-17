#pragma once

#include "render/RenderTypes.h"

#include <imgui.h>
#include <string>
#include <vector>

union SDL_Event;

namespace sdlviewer
{
    struct UiTextureHandle
    {
        ImTextureID id = 0;
        int width = 0;
        int height = 0;

        bool IsValid() const noexcept
        {
            return id != 0;
        }
    };

    using TextureLoader = std::function<UiTextureHandle(const std::string &objName, const std::string &path)>;

    class IRenderBackend
    {
    public:
        virtual ~IRenderBackend() = default;
        IRenderBackend(const IRenderBackend &) = delete;
        IRenderBackend &operator=(const IRenderBackend &) = delete;

        // 변경: SDL/ImGui platform 수명은 SDL event를 소유한 main thread가 담당한다.
        virtual RenderStatus InitializePlatform(SDL_Window *window) = 0;
        virtual void PreparePlatformFrame() = 0;
        virtual void ShutdownPlatform() noexcept = 0;

        virtual RenderStatus Initialize(const RenderInitParams &params) = 0;
        virtual void Shutdown() noexcept = 0;

        virtual RenderStatus ResizeOutput(
            WindowMetrics metrics) = 0;

        virtual RenderStatus EnsureVideoSurface(const VideoCellState &cell) = 0;
        // Caller-mode backends must support release from buildUi on the owner
        // thread, including removal of any deferred presentation/swap references.
        virtual void ReleaseVideoSurface(VideoId id) noexcept = 0;
        virtual RenderStatus RenderVideoFrame(VideoId id) = 0;

        virtual void BeginFrame(PixelSize outputSize, ClearColor clearColor) = 0;
        virtual void CompositeVideo(const std::vector<VideoLayer> &layers) = 0;
        virtual void RenderUiFrame(const UiFrameBuilder &buildUi) = 0;
        virtual RenderStatus Present() = 0;

        virtual UiTextureHandle LoadUiTexture(const std::string &objName, const std::string &path) = 0;

        virtual void ProcessEvent(const SDL_Event &event) = 0;
        virtual InputCaptureState GetInputCaptureState() const noexcept = 0;

    protected:
        IRenderBackend() = default;
    };

}
