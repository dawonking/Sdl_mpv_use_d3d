#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "app/RunApplication.h"
#include "map/IMapView.h"
#include "platform/macos/render/OpenGLRenderBackend.h"

#include <cstdio>
#include <memory>
#include <utility>

int main(int argc, char **argv)
{
    @autoreleasepool
    {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }

        const bool glConfigured =
            SDL_GL_SetAttribute(
                SDL_GL_CONTEXT_FLAGS,
                SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG) &&
            SDL_GL_SetAttribute(
                SDL_GL_CONTEXT_PROFILE_MASK,
                SDL_GL_CONTEXT_PROFILE_CORE) &&
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) &&
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2) &&
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        if (!glConfigured)
        {
            std::fprintf(stderr, "OpenGL attributes failed: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }

        SDL_Window *window = SDL_CreateWindow(
            u8"한국어",
            1440,
            840,
            SDL_WINDOW_OPENGL |
                SDL_WINDOW_RESIZABLE |
                SDL_WINDOW_HIDDEN |
                SDL_WINDOW_HIGH_PIXEL_DENSITY);

        if (window == nullptr)
        {
            std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }

        auto backend =
            std::make_unique<sdlviewer::OpenGLRenderBackend>();

        std::unique_ptr<sdlviewer::IMapView> mapView;

        const int result = sdlviewer::RunApplication(
            argc,
            argv,
            window,
            std::move(backend),
            std::move(mapView),
            {},
            sdlviewer::ApplicationStartMode::CallerUiOnly);

        SDL_DestroyWindow(window);
        SDL_Quit();
        return result;
    }
}
