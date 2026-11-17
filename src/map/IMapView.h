#pragma once

#include "ui/core/GPSDataRaw.h"

#include <functional>
#include <string>
#include <vector>

struct SDL_Window;

namespace sdlviewer
{
    // SDL logical window coordinates.
    struct MapViewBounds
    {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    enum class MapMouseEventKind
    {
        Move,
        LeftButtonDown,
        LeftButtonUp,
        Wheel
    };

    struct MapMouseEvent
    {
        MapMouseEventKind kind = MapMouseEventKind::Move;
        float x = 0.0f;
        float y = 0.0f;
        bool leftButtonHeld = false;
        float wheelDelta = 0.0f;
    };

    class IMapView
    {
    public:
        virtual ~IMapView() = default;

        virtual bool Create(
            SDL_Window *window,
            const MapViewBounds &bounds,
            const std::string &url,
            std::function<void()> onReady) = 0;

        virtual void SetBounds(const MapViewBounds &bounds) = 0;
        virtual MapViewBounds GetBounds() const = 0;

        virtual void SetVisible(bool visible) = 0;
        virtual bool IsVisible() const = 0;
        virtual bool IsReady() const = 0;

        virtual void SetGpsData(const std::vector<GPSDataRaw> &data) = 0;
        virtual void Update(double positionSec, double durationSec, bool isPlaying) = 0;

        virtual void ForwardMouseInput(const MapMouseEvent &event) = 0;
    };
} // namespace sdlviewer
