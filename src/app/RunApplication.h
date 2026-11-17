#pragma once

#include "app/Application.h"

#include <functional>
#include <memory>
#include <string>

struct SDL_Window;

namespace sdlviewer
{
    class IMapView;
    class IRenderBackend;

    int RunApplication(
        int argc,
        char **argv,
        ::SDL_Window *window,
        std::unique_ptr<IRenderBackend> backend,
        std::unique_ptr<IMapView> mapView,
        std::function<std::string()> folderPicker,
        ApplicationStartMode mode =
            ApplicationStartMode::WorkerWithMedia);
}
