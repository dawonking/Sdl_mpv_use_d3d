#include "platform/windows/render/D3D11RenderBackend.h"

#include "platform/windows/media/MpvD3D11Renderer.h"
#include "platform/windows/render/TextureManager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <d3d10_1.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_sdl3.h>
#include <implot.h>

#include <atomic>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

namespace sdlviewer
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        struct Vertex
        {
            float x;
            float y;
            float u;
            float v;
        };

        constexpr char kVertexShader[] = R"(
struct VSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 0.0F, 1.0F);
    output.uv = input.uv;
    return output;
}
)";

        constexpr char kPixelShader[] = R"(
Texture2D videoTexture : register(t0);
SamplerState videoSampler : register(s0);

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    return videoTexture.Sample(videoSampler, uv);
}
)";

        std::string HResultMessage(const char *operation, HRESULT result)
        {
            std::ostringstream stream;
            stream << operation << " failed with HRESULT 0x"
                   << std::hex << static_cast<unsigned long>(result) << '.';
            return stream.str();
        }

        RectF AspectFit(const VideoLayer &layer)
        {
            if (!layer.keepAspect ||
                !layer.destination.IsValid() ||
                !layer.sourceSize.IsValid())
            {
                return layer.destination;
            }

            const double destinationAspect =
                static_cast<double>(layer.destination.width) /
                static_cast<double>(layer.destination.height);
            const double sourceAspect =
                static_cast<double>(layer.sourceSize.width) /
                static_cast<double>(layer.sourceSize.height);

            RectF result = layer.destination;
            if (destinationAspect > sourceAspect)
            {
                result.width = static_cast<float>(
                    static_cast<double>(result.height) * sourceAspect);
                result.x += (layer.destination.width - result.width) * 0.5F;
            }
            else
            {
                result.height = static_cast<float>(
                    static_cast<double>(result.width) / sourceAspect);
                result.y += (layer.destination.height - result.height) * 0.5F;
            }
            return result;
        }
    } // namespace

    class D3D11RenderBackend::Impl
    {
    public:
        RenderStatus InitializePlatform(SDL_Window *window)
        {
            if (sdlBackendReady_)
            {
                return RenderStatus::Ok();
            }
            if (!window)
            {
                return RenderStatus::Error("D3D11 renderer requires an SDL window.");
            }

            // 변경: SDL native handle과 ImGui SDL backend는 main thread에서 준비한다.
            window_ = window;
            const SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
            windowHandle_ = static_cast<HWND>(SDL_GetPointerProperty(
                properties,
                SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                nullptr));
            if (!windowHandle_)
            {
                window_ = nullptr;
                return RenderStatus::Error("SDL did not provide a Win32 HWND.");
            }

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ownsImGuiContext_ = true;
            ImPlot::CreateContext();
            ownsImPlotContext_ = true;

            ImGuiIO &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            ImGui::StyleColorsDark();

            ImFont *koreanFont = io.Fonts->AddFontFromFileTTF(
                "resources/fonts/PretendardVariable.ttf",
                20.0f,
                nullptr,
                io.Fonts->GetGlyphRangesKorean());

            if (koreanFont == nullptr)
            {
                io.Fonts->AddFontDefault();
            }

            sdlBackendReady_ = ImGui_ImplSDL3_InitForD3D(window_);
            if (!sdlBackendReady_)
            {
                ShutdownPlatform();
                return RenderStatus::Error(
                    "Failed to initialize the ImGui SDL3 backend.");
            }

            return RenderStatus::Ok();
        }

        void PreparePlatformFrame()
        {
            std::lock_guard<std::mutex> lock(uiMutex_);

            if (!initialized_ ||
                !sdlBackendReady_ ||
                !ownsImGuiContext_)
            {
                return;
            }

            // 변경: SDL window size/scale 조회가 포함된 호출은 main thread에서만 수행한다.
            ImGui_ImplSDL3_NewFrame();
            platformFramePrepared_ = true;
        }

        void ShutdownPlatform() noexcept
        {
            std::lock_guard<std::mutex> lock(uiMutex_);

            if (sdlBackendReady_)
            {
                ImGui_ImplSDL3_Shutdown();
                sdlBackendReady_ = false;
            }
            if (ownsImPlotContext_)
            {
                ImPlot::DestroyContext();
                ownsImPlotContext_ = false;
            }
            if (ownsImGuiContext_)
            {
                ImGui::DestroyContext();
                ownsImGuiContext_ = false;
            }

            platformFramePrepared_ = false;
            captureMouse_.store(false);
            captureKeyboard_.store(false);
            captureTextInput_.store(false);
            windowHandle_ = nullptr;
            window_ = nullptr;
        }

        RenderStatus Initialize(const RenderInitParams &params)
        {
            if (initialized_)
            {
                return RenderStatus::Ok();
            }
            if (!params.window ||
                params.window != window_ ||
                !windowHandle_ ||
                !sdlBackendReady_)
            {
                return RenderStatus::Error(
                    "D3D11 platform initialization must complete on the main thread first.");
            }

            const HRESULT comResult =
                CoInitializeEx(nullptr, COINIT_MULTITHREADED);

            if (FAILED(comResult))
            {
                return RenderStatus::Error(
                    HResultMessage("CoInitializeEx", comResult));
            }

            comInitialized_ = true;

            const RenderStatus deviceStatus = CreateDevice();
            if (!deviceStatus)
            {
                Shutdown();
                return deviceStatus;
            }

            videoRenderer_ = std::make_unique<MpvD3D11Renderer>(
                device_.Get(),
                params.onFrameReady);

            textureManager_ = std::make_unique<TextureManager>(device_.Get());

            const RenderStatus swapChainStatus = CreateCompositionSwapChain();
            if (!swapChainStatus)
            {
                Shutdown();
                return swapChainStatus;
            }

            const RenderStatus pipelineStatus = CreateVideoPipeline();
            if (!pipelineStatus)
            {
                Shutdown();
                return pipelineStatus;
            }

            const RenderStatus targetStatus = CreateRenderTarget();
            if (!targetStatus)
            {
                Shutdown();
                return targetStatus;
            }

            dx11BackendReady_ =
                ImGui_ImplDX11_Init(device_.Get(), context_.Get());

            if (!dx11BackendReady_)
            {
                Shutdown();
                return RenderStatus::Error(
                    "Failed to initialize the ImGui DX11 backend.");
            }

            initialized_ = true;
            return RenderStatus::Ok();
        }

        void Shutdown() noexcept
        {
            {
                std::lock_guard<std::mutex> lock(uiMutex_);

                if (dx11BackendReady_)
                {
                    ImGui_ImplDX11_Shutdown();
                    dx11BackendReady_ = false;
                }
                platformFramePrepared_ = false;
            }

            videoRenderer_.reset();
            textureManager_.reset();

            if (context_)
            {
                context_->ClearState();
                context_->Flush();
            }

            rasterizerState_.Reset();
            sampler_.Reset();
            vertexBuffer_.Reset();
            inputLayout_.Reset();
            pixelShader_.Reset();
            vertexShader_.Reset();
            renderTarget_.Reset();
            appVisual_.Reset();
            rootVisual_.Reset();
            compositionTarget_.Reset();
            compositionDevice_.Reset();
            swapChain_.Reset();
            context_.Reset();
            device_.Reset();

            if (comInitialized_)
            {
                CoUninitialize();
                comInitialized_ = false;
            }

            captureMouse_.store(false);
            captureKeyboard_.store(false);
            captureTextInput_.store(false);
            initialized_ = false;
        }

        RenderStatus ResizeOutput(
            WindowMetrics metrics)
        {
            if (!initialized_ || !metrics.IsValid())
            {
                return RenderStatus::Error(
                    "Invalid D3D11 output resize request.");
            }

            const PixelSize pixelSize =
                metrics.pixelSize;

            context_->OMSetRenderTargets(0, nullptr, nullptr);
            context_->ClearState();
            context_->Flush();
            renderTarget_.Reset();

            const HRESULT result = swapChain_->ResizeBuffers(
                0,
                static_cast<UINT>(pixelSize.width),
                static_cast<UINT>(pixelSize.height),
                DXGI_FORMAT_UNKNOWN,
                0);

            if (FAILED(result))
            {
                return RenderStatus::Error(
                    HResultMessage(
                        "IDXGISwapChain::ResizeBuffers",
                        result));
            }

            const RenderStatus targetStatus =
                CreateRenderTarget();

            if (!targetStatus)
            {
                return targetStatus;
            }

            currentLogicalSize_ = metrics.logicalSize;
            currentPixelSize_ = metrics.pixelSize;

            return RenderStatus::Ok();
        }

        RenderStatus EnsureVideoSurface(const VideoCellState &cell)
        {
            return videoRenderer_
                       ? videoRenderer_->EnsureSurface(cell)
                       : RenderStatus::Error("D3D11 mpv renderer is not initialized.");
        }

        void ReleaseVideoSurface(VideoId id) noexcept
        {
            if (videoRenderer_)
            {
                videoRenderer_->ReleaseSurface(id);
            }
        }

        RenderStatus RenderVideoFrame(VideoId id)
        {
            return videoRenderer_
                       ? videoRenderer_->RenderFrame(id)
                       : RenderStatus::Error("D3D11 mpv renderer is not initialized.");
        }

        void BeginFrame(PixelSize outputSize, ClearColor clearColor)
        {
            if (!initialized_ || !outputSize.IsValid() || !renderTarget_)
            {
                return;
            }

            ID3D11RenderTargetView *target = renderTarget_.Get();
            context_->OMSetRenderTargets(1, &target, nullptr);

            D3D11_VIEWPORT viewport{};
            viewport.Width =
                static_cast<float>(outputSize.width);
            viewport.Height =
                static_cast<float>(outputSize.height);
            viewport.MinDepth = 0.0F;
            viewport.MaxDepth = 1.0F;
            context_->RSSetViewports(1, &viewport);

            const float color[4] = {
                clearColor.red,
                clearColor.green,
                clearColor.blue,
                clearColor.alpha,
            };
            context_->ClearRenderTargetView(renderTarget_.Get(), color);
            currentPixelSize_ = outputSize;
        }

        void CompositeVideo(const std::vector<VideoLayer> &layers)
        {
            if (!initialized_ ||
                !currentLogicalSize_.IsValid() ||
                !currentPixelSize_.IsValid())
            {
                return;
            }

            ID3D11RenderTargetView *target = renderTarget_.Get();
            context_->OMSetRenderTargets(1, &target, nullptr);

            for (const VideoLayer &layer : layers)
            {
                if (!layer.visible)
                {
                    continue;
                }

                ID3D11ShaderResourceView *texture = videoRenderer_
                                                        ? videoRenderer_->GetTexture(layer.id)
                                                        : nullptr;
                if (!texture)
                {
                    continue;
                }

                DrawTexture(texture, AspectFit(layer));
            }
        }

        void RenderUiFrame(const UiFrameBuilder &buildUi)
        {
            if (!initialized_ || !dx11BackendReady_ || !sdlBackendReady_)
            {
                return;
            }

            std::lock_guard<std::mutex> lock(uiMutex_);

            if (!platformFramePrepared_)
            {
                return;
            }

            platformFramePrepared_ = false;
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();

            if (buildUi)
            {
                buildUi();
            }

            ImGui::Render();
            const ImGuiIO &io = ImGui::GetIO();
            captureMouse_.store(io.WantCaptureMouse);
            captureKeyboard_.store(io.WantCaptureKeyboard);
            captureTextInput_.store(io.WantTextInput);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        RenderStatus Present()
        {
            if (!initialized_ || !swapChain_)
            {
                return RenderStatus::Error("D3D11 swap chain is not initialized.");
            }

            const HRESULT presentResult = swapChain_->Present(1, 0);
            const HRESULT commitResult = compositionDevice_
                                             ? compositionDevice_->Commit()
                                             : S_OK;

            if (videoRenderer_)
            {
                videoRenderer_->ReportPresentedFrames();
            }

            if (FAILED(presentResult))
            {
                return RenderStatus::Error(HResultMessage("IDXGISwapChain::Present", presentResult));
            }
            if (FAILED(commitResult))
            {
                return RenderStatus::Error(HResultMessage("IDCompositionDevice::Commit", commitResult));
            }
            return RenderStatus::Ok();
        }

        void ProcessEvent(const SDL_Event &event)
        {
            if (!initialized_ || !sdlBackendReady_)
            {
                return;
            }

            std::lock_guard<std::mutex> lock(uiMutex_);
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        InputCaptureState GetInputCaptureState() const noexcept
        {
            return InputCaptureState{
                captureMouse_.load(),
                captureKeyboard_.load(),
                captureTextInput_.load(),
            };
        }

        UiTextureHandle LoadUiTexture(const std::string &objName, const std::string &path)
        {
            if (!textureManager_)
            {
                return {};
            }
            const TextureInfo *info = textureManager_->GetTexture(objName, path);
            if (info == nullptr)
            {
                return {};
            }
            return UiTextureHandle{reinterpret_cast<ImTextureID>(info->view), info->width, info->height};
        }

        CompositionHandles GetCompositionHandles() const
        {
            return CompositionHandles{compositionDevice_.Get(), rootVisual_.Get(), appVisual_.Get()};
        }

    private:
        RenderStatus CreateDevice()
        {
            constexpr D3D_FEATURE_LEVEL featureLevels[] = {
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_0,
            };
            D3D_FEATURE_LEVEL selectedFeatureLevel{};

            const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            HRESULT result = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                flags,
                featureLevels,
                static_cast<UINT>(std::size(featureLevels)),
                D3D11_SDK_VERSION,
                &device_,
                &selectedFeatureLevel,
                &context_);

            if (result == DXGI_ERROR_UNSUPPORTED)
            {
                result = D3D11CreateDevice(
                    nullptr,
                    D3D_DRIVER_TYPE_WARP,
                    nullptr,
                    flags,
                    featureLevels,
                    static_cast<UINT>(std::size(featureLevels)),
                    D3D11_SDK_VERSION,
                    &device_,
                    &selectedFeatureLevel,
                    &context_);
            }

            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("D3D11CreateDevice", result));
            }

            ComPtr<ID3D10Multithread> multithread;
            if (SUCCEEDED(device_.As(&multithread)))
            {
                multithread->SetMultithreadProtected(TRUE);
            }
            return RenderStatus::Ok();
        }

        RenderStatus CreateCompositionSwapChain()
        {
            ComPtr<IDXGIDevice> dxgiDevice;
            HRESULT result = device_.As(&dxgiDevice);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("Query IDXGIDevice", result));
            }

            ComPtr<IDXGIAdapter> adapter;
            result = dxgiDevice->GetAdapter(&adapter);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("IDXGIDevice::GetAdapter", result));
            }

            ComPtr<IDXGIFactory2> factory;
            result = adapter->GetParent(IID_PPV_ARGS(&factory));
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("Query IDXGIFactory2", result));
            }

            RECT clientRect{};
            GetClientRect(windowHandle_, &clientRect);

            DXGI_SWAP_CHAIN_DESC1 description{};
            description.Width = static_cast<UINT>(
                clientRect.right > clientRect.left
                    ? clientRect.right - clientRect.left
                    : 1);
            description.Height = static_cast<UINT>(
                clientRect.bottom > clientRect.top
                    ? clientRect.bottom - clientRect.top
                    : 1);
            description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            description.BufferCount = 2;
            description.Scaling = DXGI_SCALING_STRETCH;
            description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

            result = factory->CreateSwapChainForComposition(
                device_.Get(),
                &description,
                nullptr,
                &swapChain_);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("CreateSwapChainForComposition", result));
            }

            result = DCompositionCreateDevice2(
                device_.Get(),
                IID_PPV_ARGS(&compositionDevice_));
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("DCompositionCreateDevice2", result));
            }

            result = compositionDevice_->CreateTargetForHwnd(
                windowHandle_,
                TRUE,
                &compositionTarget_);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("CreateTargetForHwnd", result));
            }

            result = compositionDevice_->CreateVisual(&rootVisual_);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("CreateVisual(root)", result));
            }

            result = compositionTarget_->SetRoot(rootVisual_.Get());
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("SetRoot", result));
            }

            result = compositionDevice_->CreateVisual(&appVisual_);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("CreateVisual(app)", result));
            }

            result = appVisual_->SetContent(swapChain_.Get());
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("SetContent", result));
            }

            result = rootVisual_->AddVisual(appVisual_.Get(), FALSE, nullptr);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("AddVisual", result));
            }

            result = compositionDevice_->Commit();
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("Initial DComp commit", result));
            }
            return RenderStatus::Ok();
        }

        RenderStatus CreateVideoPipeline()
        {
            ComPtr<ID3DBlob> vertexByteCode;
            ComPtr<ID3DBlob> pixelByteCode;
            ComPtr<ID3DBlob> errors;

            HRESULT result = D3DCompile(
                kVertexShader,
                std::strlen(kVertexShader),
                nullptr,
                nullptr,
                nullptr,
                "main",
                "vs_4_0",
                0,
                0,
                &vertexByteCode,
                &errors);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("Compile video vertex shader", result));
            }

            errors.Reset();
            result = D3DCompile(
                kPixelShader,
                std::strlen(kPixelShader),
                nullptr,
                nullptr,
                nullptr,
                "main",
                "ps_4_0",
                0,
                0,
                &pixelByteCode,
                &errors);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("Compile video pixel shader", result));
            }

            result = device_->CreateVertexShader(
                vertexByteCode->GetBufferPointer(),
                vertexByteCode->GetBufferSize(),
                nullptr,
                &vertexShader_);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("CreateVertexShader", result));
            }

            result = device_->CreatePixelShader(
                pixelByteCode->GetBufferPointer(),
                pixelByteCode->GetBufferSize(),
                nullptr,
                &pixelShader_);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("CreatePixelShader", result));
            }

            const D3D11_INPUT_ELEMENT_DESC inputElements[] = {
                {
                    "POSITION",
                    0,
                    DXGI_FORMAT_R32G32_FLOAT,
                    0,
                    0,
                    D3D11_INPUT_PER_VERTEX_DATA,
                    0,
                },
                {
                    "TEXCOORD",
                    0,
                    DXGI_FORMAT_R32G32_FLOAT,
                    0,
                    sizeof(float) * 2,
                    D3D11_INPUT_PER_VERTEX_DATA,
                    0,
                },
            };

            result = device_->CreateInputLayout(
                inputElements,
                static_cast<UINT>(std::size(inputElements)),
                vertexByteCode->GetBufferPointer(),
                vertexByteCode->GetBufferSize(),
                &inputLayout_);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("CreateInputLayout", result));
            }

            D3D11_BUFFER_DESC bufferDescription{};
            bufferDescription.ByteWidth = sizeof(Vertex) * 6;
            bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
            bufferDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            result = device_->CreateBuffer(
                &bufferDescription,
                nullptr,
                &vertexBuffer_);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("CreateBuffer", result));
            }

            D3D11_SAMPLER_DESC samplerDescription{};
            samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
            samplerDescription.MinLOD = 0.0F;
            samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
            result = device_->CreateSamplerState(&samplerDescription, &sampler_);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("CreateSamplerState", result));
            }

            D3D11_RASTERIZER_DESC rasterizerDescription{};
            rasterizerDescription.FillMode = D3D11_FILL_SOLID;
            rasterizerDescription.CullMode = D3D11_CULL_NONE;
            rasterizerDescription.DepthClipEnable = TRUE;
            result = device_->CreateRasterizerState(
                &rasterizerDescription,
                &rasterizerState_);
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("CreateRasterizerState", result));
            }

            return RenderStatus::Ok();
        }

        RenderStatus CreateRenderTarget()
        {
            ComPtr<ID3D11Texture2D> backBuffer;
            const HRESULT result = swapChain_->GetBuffer(
                0,
                IID_PPV_ARGS(&backBuffer));
            if (FAILED(result))
            {
                return RenderStatus::Error(HResultMessage("Get swap-chain buffer", result));
            }

            const HRESULT targetResult = device_->CreateRenderTargetView(
                backBuffer.Get(),
                nullptr,
                &renderTarget_);
            if (FAILED(targetResult))
            {
                return RenderStatus::Error(HResultMessage("CreateRenderTargetView", targetResult));
            }
            return RenderStatus::Ok();
        }

        void DrawTexture(ID3D11ShaderResourceView *texture, RectF destination)
        {
            if (!texture ||
                !destination.IsValid() ||
                !currentLogicalSize_.IsValid())
            {
                return;
            }

            const float outputWidth =
                static_cast<float>(
                    currentLogicalSize_.width);

            const float outputHeight =
                static_cast<float>(
                    currentLogicalSize_.height);

            const float left = destination.x / outputWidth * 2.0F - 1.0F;
            const float right =
                (destination.x + destination.width) / outputWidth * 2.0F - 1.0F;
            const float top = 1.0F - destination.y / outputHeight * 2.0F;
            const float bottom =
                1.0F - (destination.y + destination.height) / outputHeight * 2.0F;

            const Vertex vertices[6] = {
                {left, top, 0.0F, 0.0F},
                {right, top, 1.0F, 0.0F},
                {left, bottom, 0.0F, 1.0F},
                {left, bottom, 0.0F, 1.0F},
                {right, top, 1.0F, 0.0F},
                {right, bottom, 1.0F, 1.0F},
            };

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context_->Map(
                    vertexBuffer_.Get(),
                    0,
                    D3D11_MAP_WRITE_DISCARD,
                    0,
                    &mapped)))
            {
                return;
            }
            std::memcpy(mapped.pData, vertices, sizeof(vertices));
            context_->Unmap(vertexBuffer_.Get(), 0);

            const UINT stride = sizeof(Vertex);
            const UINT offset = 0;
            ID3D11Buffer *vertexBuffer = vertexBuffer_.Get();
            ID3D11SamplerState *sampler = sampler_.Get();
            ID3D11RenderTargetView *target = renderTarget_.Get();

            context_->OMSetRenderTargets(1, &target, nullptr);
            context_->IASetInputLayout(inputLayout_.Get());
            context_->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
            context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
            context_->PSSetSamplers(0, 1, &sampler);
            context_->PSSetShaderResources(0, 1, &texture);
            context_->RSSetState(rasterizerState_.Get());

            const float blendFactor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
            context_->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFFU);
            context_->Draw(6, 0);

            ID3D11ShaderResourceView *noTexture = nullptr;
            context_->PSSetShaderResources(0, 1, &noTexture);
        }

        SDL_Window *window_ = nullptr;
        HWND windowHandle_ = nullptr;
        bool initialized_ = false;
        bool sdlBackendReady_ = false;
        bool dx11BackendReady_ = false;
        bool ownsImGuiContext_ = false;
        bool ownsImPlotContext_ = false;
        bool comInitialized_ = false;
        bool platformFramePrepared_ = false;

        PixelSize currentLogicalSize_{};
        PixelSize currentPixelSize_{};

        mutable std::mutex uiMutex_;
        std::atomic_bool captureMouse_{false};
        std::atomic_bool captureKeyboard_{false};
        std::atomic_bool captureTextInput_{false};

        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<IDXGISwapChain1> swapChain_;
        ComPtr<ID3D11RenderTargetView> renderTarget_;

        ComPtr<IDCompositionDesktopDevice> compositionDevice_;
        ComPtr<IDCompositionTarget> compositionTarget_;
        ComPtr<IDCompositionVisual2> rootVisual_;
        ComPtr<IDCompositionVisual2> appVisual_;

        ComPtr<ID3D11VertexShader> vertexShader_;
        ComPtr<ID3D11PixelShader> pixelShader_;
        ComPtr<ID3D11InputLayout> inputLayout_;
        ComPtr<ID3D11Buffer> vertexBuffer_;
        ComPtr<ID3D11SamplerState> sampler_;
        ComPtr<ID3D11RasterizerState> rasterizerState_;

        std::unique_ptr<MpvD3D11Renderer> videoRenderer_;
        std::unique_ptr<TextureManager> textureManager_;
    };

    D3D11RenderBackend::D3D11RenderBackend()
        : impl_(std::make_unique<Impl>())
    {
    }

    D3D11RenderBackend::~D3D11RenderBackend()
    {
        Shutdown();
        ShutdownPlatform();
    }

    RenderStatus D3D11RenderBackend::InitializePlatform(SDL_Window *window)
    {
        return impl_->InitializePlatform(window);
    }

    void D3D11RenderBackend::PreparePlatformFrame()
    {
        impl_->PreparePlatformFrame();
    }

    void D3D11RenderBackend::ShutdownPlatform() noexcept
    {
        impl_->ShutdownPlatform();
    }

    RenderStatus D3D11RenderBackend::Initialize(const RenderInitParams &params)
    {
        return impl_->Initialize(params);
    }

    void D3D11RenderBackend::Shutdown() noexcept
    {
        impl_->Shutdown();
    }

    RenderStatus D3D11RenderBackend::ResizeOutput(
        WindowMetrics metrics)
    {
        return impl_->ResizeOutput(metrics);
    }

    RenderStatus D3D11RenderBackend::EnsureVideoSurface(const VideoCellState &cell)
    {
        return impl_->EnsureVideoSurface(cell);
    }

    void D3D11RenderBackend::ReleaseVideoSurface(VideoId id) noexcept
    {
        impl_->ReleaseVideoSurface(id);
    }

    RenderStatus D3D11RenderBackend::RenderVideoFrame(VideoId id)
    {
        return impl_->RenderVideoFrame(id);
    }

    void D3D11RenderBackend::BeginFrame(PixelSize outputSize, ClearColor clearColor)
    {
        impl_->BeginFrame(outputSize, clearColor);
    }

    void D3D11RenderBackend::CompositeVideo(const std::vector<VideoLayer> &layers)
    {
        impl_->CompositeVideo(layers);
    }

    void D3D11RenderBackend::RenderUiFrame(const UiFrameBuilder &buildUi)
    {
        impl_->RenderUiFrame(buildUi);
    }

    RenderStatus D3D11RenderBackend::Present()
    {
        return impl_->Present();
    }

    void D3D11RenderBackend::ProcessEvent(const SDL_Event &event)
    {
        impl_->ProcessEvent(event);
    }

    UiTextureHandle D3D11RenderBackend::LoadUiTexture(const std::string &objName, const std::string &path)
    {
        return impl_->LoadUiTexture(objName, path);
    }

    D3D11RenderBackend::CompositionHandles D3D11RenderBackend::GetCompositionHandles() const
    {
        return impl_->GetCompositionHandles();
    }

    InputCaptureState D3D11RenderBackend::GetInputCaptureState() const noexcept
    {
        return impl_->GetInputCaptureState();
    }
} // namespace sdlviewer
