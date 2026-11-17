#pragma once

#include "map/IMapView.h"

#include <memory>
#include <string>

namespace sdlviewer
{
    class D3D11RenderBackend;

    class WebView2MapView final : public IMapView
    {
    public:
        explicit WebView2MapView(D3D11RenderBackend &renderBackend);
        ~WebView2MapView() override;

        WebView2MapView(const WebView2MapView &) = delete;
        WebView2MapView &operator=(const WebView2MapView &) = delete;

        bool Create(
            SDL_Window *window,
            const MapViewBounds &bounds,
            const std::string &url,
            std::function<void()> onReady) override;

        void SetBounds(const MapViewBounds &bounds) override;
        MapViewBounds GetBounds() const override;

        void SetVisible(bool visible) override;
        bool IsVisible() const override;
        bool IsReady() const override;

        void SetGpsData(const std::vector<GPSDataRaw> &data) override;
        void Update(double positionSec, double durationSec, bool isPlaying) override;

        void ForwardMouseInput(const MapMouseEvent &event) override;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace sdlviewer
