// Exercise the installed SDK through SDL3, a D3D11 swap chain and CPU frame readback.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <SDL3/SDL.h>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_d3.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

static void Check(bool success, const char* message)
{
    if (!success) throw std::runtime_error(message);
}

static void MpvCheck(int result, const char* operation)
{
    if (result < 0) throw std::runtime_error(std::string(operation) + ": " + mpv_error_string(result));
}

struct Session
{
    SDL_Window* window = nullptr;
    HANDLE wake = nullptr;
    mpv_handle* player = nullptr;
    mpv_render_context* renderer = nullptr;
    ~Session()
    {
        if (renderer) {
            mpv_render_context_set_update_callback(renderer, nullptr, nullptr);
            mpv_render_context_free(renderer);
        }
        if (player) mpv_terminate_destroy(player);
        if (wake) CloseHandle(wake);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

int main(int argc, char** argv)
{
    try {
        Check(argc == 2, "Usage: mpv_d3d_smoke.exe <video>");
        wchar_t modulePath[32768]{};
        wchar_t exePath[32768]{};
        Check(GetModuleFileNameW(GetModuleHandleW(L"libmpv-2.dll"), modulePath, 32768) != 0,
              "Cannot determine loaded mpv DLL path");
        Check(GetModuleFileNameW(nullptr, exePath, 32768) != 0, "Cannot determine executable path");
        Check(std::filesystem::equivalent(std::filesystem::path(modulePath).parent_path(),
                                         std::filesystem::path(exePath).parent_path()),
              "mpv DLL was loaded from outside the test deployment");
        std::wcout << L"Loaded mpv: " << modulePath << std::endl;

        // Keep D3D resources alive until the libmpv renderer has been destroyed.
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IDXGISwapChain> swap;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11Texture2D> staging;
        Session session;
        Check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        session.window = SDL_CreateWindow("Patched mpv D3D11 smoke test", 640, 360, 0);
        Check(session.window != nullptr, SDL_GetError());
        auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(session.window),
                                      SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        Check(hwnd != nullptr, "SDL did not create a Windows window");
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = 640;
        desc.BufferDesc.Height = 360;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.OutputWindow = hwnd;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        D3D_FEATURE_LEVEL feature{};
        Check(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &desc, &swap, &device, &feature, &context)), "Hardware D3D11 device/swap chain creation failed");
        Check(SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&texture))), "Cannot get swap chain texture");
        D3D11_TEXTURE2D_DESC texDesc{};
        texture->GetDesc(&texDesc);
        texDesc.Usage = D3D11_USAGE_STAGING;
        texDesc.BindFlags = 0;
        texDesc.MiscFlags = 0;
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Check(SUCCEEDED(device->CreateTexture2D(&texDesc, nullptr, &staging)), "Cannot create readback texture");

        session.wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Check(session.wake != nullptr, "Cannot create render wake event");
        session.player = mpv_create();
        Check(session.player != nullptr, "mpv_create failed");
        for (auto option : {std::pair{"config", "no"}, {"vo", "libmpv"}, {"ao", "null"},
                            {"hwdec", "no"}, {"profile", "fast"}, {"gpu-dumb-mode", "yes"}}) {
            MpvCheck(mpv_set_option_string(session.player, option.first, option.second), option.first);
        }
        MpvCheck(mpv_initialize(session.player), "mpv_initialize");
        for (const char* name : {"setHFlip", "setVFlip", "activeBorder"}) {
            int enabled = 1;
            MpvCheck(mpv_set_property(session.player, name, MPV_FORMAT_FLAG, &enabled), name);
            int actual = 0;
            MpvCheck(mpv_get_property(session.player, name, MPV_FORMAT_FLAG, &actual), name);
            Check(actual == enabled, "Custom property did not round-trip");
            enabled = 0;
            MpvCheck(mpv_set_property(session.player, name, MPV_FORMAT_FLAG, &enabled), name);
        }
        char* roundView = nullptr;
        MpvCheck(mpv_get_property(session.player, "round-view", MPV_FORMAT_STRING, &roundView), "round-view");
        mpv_free(roundView);
        mpv_d3d11_init_params init{device.Get()};
        mpv_render_param parameters[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_D3D11)},
            {MPV_RENDER_PARAM_D3D11_INIT_PARAMS, &init}, {MPV_RENDER_PARAM_INVALID, nullptr}};
        MpvCheck(mpv_render_context_create(&session.renderer, session.player, parameters), "D3D renderer creation");
        mpv_render_context_set_update_callback(session.renderer, [](void* event) { SetEvent(event); }, session.wake);
        const char* command[] = {"loadfile", argv[1], nullptr};
        MpvCheck(mpv_command(session.player, command), "loadfile");

        const ULONGLONG deadline = GetTickCount64() + 20000;
        int frames = 0;
        bool variedPixels = false;
        while (GetTickCount64() < deadline && (frames < 3 || !variedPixels)) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) Check(event.type != SDL_EVENT_QUIT, "Test window closed before completion");
            for (;;) {
                auto* mpvEvent = mpv_wait_event(session.player, 0);
                if (mpvEvent->event_id == MPV_EVENT_NONE) break;
                if (mpvEvent->event_id == MPV_EVENT_END_FILE) {
                    auto* end = static_cast<mpv_event_end_file*>(mpvEvent->data);
                    MpvCheck(end->error, "playback");
                }
            }
            WaitForSingleObject(session.wake, 10);
            if (!(mpv_render_context_update(session.renderer) & MPV_RENDER_UPDATE_FRAME)) continue;
            mpv_d3d11_fbo fbo{texture.Get(), 640, 360, DXGI_FORMAT_R8G8B8A8_UNORM};
            int wait = 0;
            mpv_render_param render[] = {{MPV_RENDER_PARAM_D3D11_FBO, &fbo},
                {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &wait}, {MPV_RENDER_PARAM_INVALID, nullptr}};
            MpvCheck(mpv_render_context_render(session.renderer, render), "D3D render");
            context->CopyResource(staging.Get(), texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            Check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "Frame readback failed");
            const auto* first = static_cast<const unsigned char*>(mapped.pData);
            for (unsigned y = 0; y < 360 && !variedPixels; y += 13) {
                const auto* row = first + mapped.RowPitch * y;
                for (unsigned x = 0; x < 640; x += 13) {
                    if (row[x * 4] != first[0] || row[x * 4 + 1] != first[1] || row[x * 4 + 2] != first[2]) {
                        variedPixels = true;
                        break;
                    }
                }
            }
            context->Unmap(staging.Get(), 0);
            Check(SUCCEEDED(swap->Present(1, 0)), "Swap chain Present failed");
            mpv_render_context_report_swap(session.renderer);
            ++frames;
        }
        Check(frames >= 3 && variedPixels, "Timed out waiting for rendered video with nonuniform pixels");
        std::cout << "PASS: hardware D3D11, " << frames
                  << " frames, pixel readback, custom properties, installed SDK and local DLL.\n"
                  << "NOT TESTED: hardware decoding, resize, multichannel and macOS.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
