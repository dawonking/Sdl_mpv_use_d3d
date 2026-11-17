#include <Windows.h>
#include <objbase.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "app/RunApplication.h"
#include "platform/windows/files/FolderDialog.h"
#include "platform/windows/map/WebView2MapView.h"
#include "platform/windows/render/D3D11RenderBackend.h"

#include <cstdio>
#include <memory>

int main(int argc, char **argv)
{
    const HRESULT comResult = CoInitializeEx(
        nullptr,
        COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    if (FAILED(comResult))
    {
        std::fprintf(
            stderr,
            "CoInitializeEx failed: 0x%08lX\n",
            static_cast<unsigned long>(comResult));
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        CoUninitialize();
        return 1;
    }

    const SDL_WindowFlags flags =
        SDL_WINDOW_RESIZABLE |
        SDL_WINDOW_HIDDEN |
        SDL_WINDOW_HIGH_PIXEL_DENSITY;

    SDL_Window *window = SDL_CreateWindow(
        u8"한국어",
        1440,
        840,
        flags);

    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        CoUninitialize();
        return 1;
    }

    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    HWND hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(
            props,
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            nullptr));

    if (hwnd != nullptr)
    {
        const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        SetWindowLongPtr(
            hwnd,
            GWL_EXSTYLE,
            exStyle | WS_EX_NOREDIRECTIONBITMAP);
    }

    auto backend =
        std::make_unique<sdlviewer::D3D11RenderBackend>();

    auto mapView =
        std::make_unique<sdlviewer::WebView2MapView>(*backend);

    const int result = sdlviewer::RunApplication(
        argc,
        argv,
        window,
        std::move(backend),
        std::move(mapView),
        sdlviewer::OpenFolderDialog);

    SDL_DestroyWindow(window);
    SDL_Quit();
    CoUninitialize();

    return result;
}