#pragma once

#include "render/RenderTypes.h"

#include <memory>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace sdlviewer
{
class MpvD3D11Renderer final
{
public:
    MpvD3D11Renderer(
        ID3D11Device* device,
        FrameReadyCallback frameReady);
    ~MpvD3D11Renderer();

    MpvD3D11Renderer(const MpvD3D11Renderer&) = delete;
    MpvD3D11Renderer& operator=(const MpvD3D11Renderer&) = delete;

    RenderStatus EnsureSurface(const VideoCellState& cell);
    void ReleaseSurface(VideoId id) noexcept;
    RenderStatus RenderFrame(VideoId id);

    ID3D11ShaderResourceView* GetTexture(VideoId id) const noexcept;
    void ReportPresentedFrames() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sdlviewer
