#include "PopupManager.h"

#include "JsonReader.h"

namespace sdlviewer
{
    RenderStatus PopupManager::LoadJson()
    {
        const RenderStatus status = LoadJsonValue(popupConfig_, "LanguagePopup.json");
        if (!status)
        {
            return status;
        }

        widgets_.clear();
        popupPanels_.clear();

        const ConfigResolver resolver = [this](const std::string &name)
        {
            return FindWidget(name);
        };
        int popupId = 0;

        for (auto &entry : popupConfig_)
        {
            WidgetConfig &config = entry.second;
            if (config.type != UiType::popup)
            {
                continue;
            }

            UiPopupPanel *panel = CreateWidget<UiPopupPanel>(
                popupId++,
                &config,
                resolver,
                textureLoader_,
                imageBasePath_);

            popupPanels_.push_back(panel);
        }

        return RenderStatus::Ok();
    }

    void PopupManager::RendUi()
    {
        for (const auto &widget : widgets_)
        {
            if (widget)
            {
                widget->Render();
            }
        }
    }

    WidgetConfig *PopupManager::FindWidget(const std::string &name)
    {
        auto it = popupConfig_.find(name);
        return it != popupConfig_.end() ? &it->second : nullptr;
    }
} // namespace sdlviewer
