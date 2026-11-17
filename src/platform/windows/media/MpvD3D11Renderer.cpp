#include "platform/windows/media/MpvD3D11Renderer.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <mpv/render.h>
#include <mpv/render_d3.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sdlviewer
{
namespace
{
using Microsoft::WRL::ComPtr;

std::string HResultMessage(const char* operation, HRESULT result)
{
    std::ostringstream stream;
    stream << operation << " failed with HRESULT 0x"
           << std::hex << static_cast<unsigned long>(result) << '.';
    return stream.str();
}
} // namespace

class MpvD3D11Renderer::Impl
{
public:
    struct Surface
    {
        Impl* owner = nullptr;
        VideoId id = 0;
        mpv_handle* player = nullptr;
        mpv_render_context* renderContext = nullptr;
        PixelSize size{};
        bool blockForTargetTime = false;
        bool hasRendered = false;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> shaderResourceView;
    };

    Impl(ID3D11Device* device, FrameReadyCallback frameReady)
        : device_(device), frameReady_(std::move(frameReady))
    {
    }

    ~Impl()
    {
        for (auto& item : surfaces_)
        {
            DestroySurface(*item.second);
        }
    }

    RenderStatus EnsureSurface(const VideoCellState& cell)
    {
        if (!device_ || !cell.IsValid())
        {
            return RenderStatus::Error("Invalid D3D11 mpv surface request.");
        }

        auto found = surfaces_.find(cell.id);
        if (found != surfaces_.end() && found->second->player != cell.player)
        {
            DestroySurface(*found->second);
            surfaces_.erase(found);
            found = surfaces_.end();
        }

        if (found == surfaces_.end())
        {
            auto surface = std::make_unique<Surface>();
            surface->owner = this;
            surface->id = cell.id;
            surface->player = cell.player;
            surface->blockForTargetTime = cell.blockForTargetTime;

            mpv_d3d11_init_params d3dParameters{};
            d3dParameters.device = device_.Get();

            mpv_render_param parameters[] = {
                {
                    MPV_RENDER_PARAM_API_TYPE,
                    const_cast<char*>(MPV_RENDER_API_TYPE_D3D11),
                },
                {MPV_RENDER_PARAM_D3D11_INIT_PARAMS, &d3dParameters},
                {MPV_RENDER_PARAM_INVALID, nullptr},
            };

            if (mpv_render_context_create(
                    &surface->renderContext,
                    surface->player,
                    parameters) < 0)
            {
                return RenderStatus::Error("mpv failed to create a D3D11 render context.");
            }

            mpv_render_context_set_update_callback(
                surface->renderContext,
                &Impl::OnMpvRenderUpdate,
                surface.get());

            const RenderStatus textureStatus = CreateTexture(*surface, cell.surfaceSize);
            if (!textureStatus)
            {
                DestroySurface(*surface);
                return textureStatus;
            }

            surfaces_.emplace(cell.id, std::move(surface));
            return RenderStatus::Ok();
        }

        Surface& surface = *found->second;
        surface.blockForTargetTime = cell.blockForTargetTime;
        if (surface.size != cell.surfaceSize)
        {
            return CreateTexture(surface, cell.surfaceSize);
        }
        return RenderStatus::Ok();
    }

    void ReleaseSurface(VideoId id) noexcept
    {
        const auto found = surfaces_.find(id);
        if (found == surfaces_.end())
        {
            return;
        }

        DestroySurface(*found->second);
        surfaces_.erase(found);
    }

    RenderStatus RenderFrame(VideoId id)
    {
        const auto found = surfaces_.find(id);
        if (found == surfaces_.end() || !found->second->renderContext)
        {
            return RenderStatus::Error("Requested D3D11 mpv surface does not exist.");
        }

        Surface& surface = *found->second;
        const std::uint64_t flags = mpv_render_context_update(surface.renderContext);
        if (surface.hasRendered && (flags & MPV_RENDER_UPDATE_FRAME) == 0)
        {
            return RenderStatus::Ok();
        }

        mpv_d3d11_fbo frameBuffer{};
        frameBuffer.texture = surface.texture.Get();
        frameBuffer.width = surface.size.width;
        frameBuffer.height = surface.size.height;
        frameBuffer.format = DXGI_FORMAT_R8G8B8A8_UNORM;

        int blockForTargetTime = surface.blockForTargetTime ? 1 : 0;
        mpv_render_param parameters[] = {
            {MPV_RENDER_PARAM_D3D11_FBO, &frameBuffer},
            {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &blockForTargetTime},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };

        mpv_render_context_render(surface.renderContext, parameters);
        surface.hasRendered = true;
        renderedContexts_.push_back(surface.renderContext);
        return RenderStatus::Ok();
    }

    ID3D11ShaderResourceView* GetTexture(VideoId id) const noexcept
    {
        const auto found = surfaces_.find(id);
        return found != surfaces_.end()
            ? found->second->shaderResourceView.Get()
            : nullptr;
    }

    void ReportPresentedFrames() noexcept
    {
        for (mpv_render_context* renderContext : renderedContexts_)
        {
            if (renderContext)
            {
                mpv_render_context_report_swap(renderContext);
            }
        }
        renderedContexts_.clear();
    }

private:
    static void OnMpvRenderUpdate(void* context)
    {
        auto* surface = static_cast<Surface*>(context);
        if (surface && surface->owner && surface->owner->frameReady_)
        {
            surface->owner->frameReady_(surface->id);
        }
    }

    RenderStatus CreateTexture(Surface& surface, PixelSize size)
    {
        if (!size.IsValid())
        {
            return RenderStatus::Error("Video texture size must be positive.");
        }

        D3D11_TEXTURE2D_DESC textureDescription{};
        textureDescription.Width = static_cast<UINT>(size.width);
        textureDescription.Height = static_cast<UINT>(size.height);
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags =
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        ComPtr<ID3D11Texture2D> texture;
        HRESULT result = device_->CreateTexture2D(
            &textureDescription,
            nullptr,
            &texture);
        if (FAILED(result))
        {
            return RenderStatus::Error(HResultMessage("Create video texture", result));
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = textureDescription.Format;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        viewDescription.Texture2D.MipLevels = 1;

        ComPtr<ID3D11ShaderResourceView> shaderResourceView;
        result = device_->CreateShaderResourceView(
            texture.Get(),
            &viewDescription,
            &shaderResourceView);
        if (FAILED(result))
        {
            return RenderStatus::Error(HResultMessage("Create video texture view", result));
        }

        surface.texture = std::move(texture);
        surface.shaderResourceView = std::move(shaderResourceView);
        surface.size = size;
        surface.hasRendered = false;
        return RenderStatus::Ok();
    }

    void DestroySurface(Surface& surface) noexcept
    {
        if (surface.renderContext)
        {
            mpv_render_context_set_update_callback(
                surface.renderContext,
                nullptr,
                nullptr);
            mpv_render_context_free(surface.renderContext);
            surface.renderContext = nullptr;
        }
        surface.shaderResourceView.Reset();
        surface.texture.Reset();
        surface.player = nullptr;
        surface.size = {};
    }

    ComPtr<ID3D11Device> device_;
    FrameReadyCallback frameReady_;
    std::unordered_map<VideoId, std::unique_ptr<Surface>> surfaces_;
    std::vector<mpv_render_context*> renderedContexts_;
};

MpvD3D11Renderer::MpvD3D11Renderer(
    ID3D11Device* device,
    FrameReadyCallback frameReady)
    : impl_(std::make_unique<Impl>(device, std::move(frameReady)))
{
}

MpvD3D11Renderer::~MpvD3D11Renderer() = default;

RenderStatus MpvD3D11Renderer::EnsureSurface(const VideoCellState& cell)
{
    return impl_->EnsureSurface(cell);
}

void MpvD3D11Renderer::ReleaseSurface(VideoId id) noexcept
{
    impl_->ReleaseSurface(id);
}

RenderStatus MpvD3D11Renderer::RenderFrame(VideoId id)
{
    return impl_->RenderFrame(id);
}

ID3D11ShaderResourceView* MpvD3D11Renderer::GetTexture(VideoId id) const noexcept
{
    return impl_->GetTexture(id);
}

void MpvD3D11Renderer::ReportPresentedFrames() noexcept
{
    impl_->ReportPresentedFrames();
}
} // namespace sdlviewer
