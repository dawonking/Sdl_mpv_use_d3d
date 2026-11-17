#pragma once

#include "UiPopupPanel.h"
#include "WidgetConfig.h"
#include "render/RenderTypes.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sdlviewer
{
    class PopupManager
    {
    public:
        RenderStatus LoadJson();
        void RendUi();

        WidgetConfig *FindWidget(const std::string &name);

        template <typename T = UiPopupPanel>
        T *FindPanelAs(const std::string &targetName)
        {
            for (const auto &widget : widgets_)
            {
                if (widget)
                {
                    if (UiComponentBase *component = widget->FindByObjName(targetName))
                    {
                        return dynamic_cast<T *>(component);
                    }
                }
            }
            return nullptr;
        }

        void SetTextureLoader(TextureLoader loader) { textureLoader_ = std::move(loader); }
        void SetImageBasePath(std::string path) { imageBasePath_ = std::move(path); }

    private:
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
        std::vector<UiPopupPanel *> popupPanels_;

        std::map<std::string, WidgetConfig> popupConfig_;

        TextureLoader textureLoader_;
        std::string imageBasePath_;
    };
} // namespace sdlviewer
