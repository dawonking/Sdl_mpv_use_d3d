#pragma once

#include "IUiFrame.h"
#include "UiPanel.h"
#include "WidgetConfig.h"
#include "render/RenderTypes.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sdlviewer
{
    class UiManager : public IUiFrame
    {
    public:
        RectF GetVideoViewport() const noexcept
        {
            return videoViewport_;
        }
        RenderStatus LoadJson();
        void ResizeUi(int w, int h);
        void RendUi();
        void SetFullscreen(bool fullscreen);
        bool IsFullscreen() const noexcept { return fullscreen_; }
        void UpdateFullscreenHover(float x, float y, bool holdPanel = false);
        bool IsVideoInputBlocked(float x, float y) const;

        void RenderUi() override { RendUi(); }
        void OnResize(PixelSize size) override { ResizeUi(size.width, size.height); }

        WidgetConfig *FindWidget(const std::string &name);

        template <typename T>
        T *FindWidgetAs(const std::string &name)
        {
            for (const auto &widget : widgets_)
            {
                if (!widget)
                {
                    continue;
                }

                UiComponentBase *component = widget->FindByObjName(name);
                if (component != nullptr)
                {
                    return dynamic_cast<T *>(component);
                }
            }

            return nullptr;
        }

        void SetTextureLoader(TextureLoader loader) { textureLoader_ = std::move(loader); }
        void SetImageBasePath(std::string path) { imageBasePath_ = std::move(path); }

    private:
        void ShowFullscreenArea(int area);

        RectF videoViewport_{};
        PixelSize logicalSize_{};
        bool fullscreen_ = false;
        int fullscreenHoverArea_ = -1;
        std::array<RectF, 3> fullscreenHoverAreas_{}; // Top, right, bottom.
        std::vector<bool> normalPanelVisibility_;
        template <typename T, typename... Args>
        T *CreateWidget(Args &&...args)
        {
            auto widget = std::make_unique<T>(std::forward<Args>(args)...);
            T *rawPtr = widget.get();
            rawPtr->Init();
            widgets_.push_back(std::move(widget));
            return rawPtr;
        }

        std::vector<std::unique_ptr<UiComponentBase>> widgets_;
        std::vector<UiPanel *> panels_;

        std::map<std::string, StartData> programConfigs_;
        std::map<std::string, WidgetConfig> topConfig_;
        std::map<std::string, WidgetConfig> leftConfig_;
        std::map<std::string, WidgetConfig> bottomConfig_;
        std::map<std::string, WidgetConfig> rightConfig_;
        std::map<std::string, WidgetConfig> chSideConfig_;

        TextureLoader textureLoader_;
        std::string imageBasePath_;
    };
} // namespace sdlviewer
