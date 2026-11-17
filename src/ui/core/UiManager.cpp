#include "UiManager.h"

#include "JsonReader.h"

#include <algorithm>

namespace sdlviewer
{
    namespace
    {
        struct LayoutRect
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
        };

        float ClampSize(float requested, float available)
        {
            return std::clamp(requested, 0.0f, available);
        }

        RectF PanelRect(const UiPanel &panel)
        {
            const ImVec2 position = panel.GetPos();
            const ImVec2 size = panel.GetSize();
            return {position.x, position.y, size.x, size.y};
        }

        bool Contains(const RectF &rect, float x, float y)
        {
            return rect.IsValid() && x >= rect.x && y >= rect.y &&
                   x < rect.x + rect.width && y < rect.y + rect.height;
        }

        void ArrangeDockPanel(UiPanel &panel, const WidgetConfig &config, LayoutRect &remaining)
        {
            const ImVec2 preferred = panel.GetPreferredSize();

            switch (config.dock)
            {
            case DockType::Top:
            {
                const float height = ClampSize(preferred.y, remaining.height);
                panel.Arrange(remaining.x, remaining.y, remaining.width, height);
                remaining.y += height;
                remaining.height -= height;
                break;
            }

            case DockType::Left:
            {
                const float width = ClampSize(preferred.x, remaining.width);
                panel.Arrange(remaining.x, remaining.y, width, remaining.height);
                remaining.x += width;
                remaining.width -= width;
                break;
            }

            case DockType::Right:
            {
                const float width = ClampSize(preferred.x, remaining.width);
                panel.Arrange(
                    remaining.x + remaining.width - width,
                    remaining.y,
                    width,
                    remaining.height);
                remaining.width -= width;
                break;
            }

            case DockType::Bottom:
            {
                const float height = ClampSize(preferred.y, remaining.height);
                panel.Arrange(
                    remaining.x,
                    remaining.y + remaining.height - height,
                    remaining.width,
                    height);
                remaining.height -= height;
                break;
            }

            case DockType::Fill:
                panel.Arrange(remaining.x, remaining.y, remaining.width, remaining.height);
                break;

            case DockType::None:
            default:
                break;
            }
        }
    } // namespace

    RenderStatus UiManager::LoadJson()
    {
        const RenderStatus status = LoadSettingValue(
            programConfigs_,
            topConfig_,
            leftConfig_,
            bottomConfig_,
            rightConfig_,
            chSideConfig_);
        if (!status)
        {
            return status;
        }

        widgets_.clear();
        panels_.clear();
        normalPanelVisibility_.clear();
        fullscreenHoverArea_ = -1;
        fullscreenHoverAreas_ = {};

        const ConfigResolver resolver = [this](const std::string &name)
        {
            return FindWidget(name);
        };
        int panelId = 0;

        auto createPanels = [this, &resolver, &panelId](std::map<std::string, WidgetConfig> &configs)
        {
            for (auto &entry : configs)
            {
                WidgetConfig &config = entry.second;
                if (config.type != UiType::Pannel)
                {
                    continue;
                }

                UiPanel *panel = CreateWidget<UiPanel>(panelId++, &config, resolver, textureLoader_, imageBasePath_);
                panels_.push_back(panel);
            }
        };

        createPanels(topConfig_);
        createPanels(leftConfig_);
        createPanels(rightConfig_);
        createPanels(bottomConfig_);
        createPanels(chSideConfig_);

        if (fullscreen_)
        {
            for (const UiPanel *panel : panels_)
                normalPanelVisibility_.push_back(!panel->IsHidden());
            ShowFullscreenArea(-1);
            ResizeUi(logicalSize_.width, logicalSize_.height);
        }

        return RenderStatus::Ok();
    }

    WidgetConfig *UiManager::FindWidget(const std::string &name)
    {
        auto findIn = [&name](std::map<std::string, WidgetConfig> &configs) -> WidgetConfig *
        {
            auto it = configs.find(name);
            return it != configs.end() ? &it->second : nullptr;
        };

        if (WidgetConfig *result = findIn(topConfig_))
            return result;
        if (WidgetConfig *result = findIn(leftConfig_))
            return result;
        if (WidgetConfig *result = findIn(bottomConfig_))
            return result;
        if (WidgetConfig *result = findIn(rightConfig_))
            return result;
        if (WidgetConfig *result = findIn(chSideConfig_))
            return result;

        return nullptr;
    }

    void UiManager::RendUi()
    {
        for (const auto &widget : widgets_)
        {
            if (widget)
            {
                widget->Render();
            }
        }
    }

    void UiManager::ResizeUi(int w, int h)
    {
        if (w <= 0 || h <= 0)
            return;

        logicalSize_ = {w, h};
        LayoutRect remaining{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};

        std::vector<UiPanel *> orderedPanels = panels_;
        std::stable_sort(
            orderedPanels.begin(),
            orderedPanels.end(),
            [](const UiPanel *lhs, const UiPanel *rhs)
            {
                return lhs->GetOpt()->dockOrder < rhs->GetOpt()->dockOrder;
            });

        for (UiPanel *panel : orderedPanels)
        {
            if (panel == nullptr || panel->GetOpt() == nullptr)
            {
                continue;
            }

            WidgetConfig *config = panel->GetOpt();
            if (config->dock == DockType::None)
            {
                panel->WindowResize(w, h);
                continue;
            }

            ArrangeDockPanel(*panel, *config, remaining);
        }

        videoViewport_ = RectF{
            remaining.x,
            remaining.y,
            remaining.width,
            remaining.height};

        if (!fullscreen_)
            return;

        const float width = static_cast<float>(w);
        const float height = static_cast<float>(h);
        videoViewport_ = {0.0f, 0.0f, width, height};
        fullscreenHoverAreas_ = {};

        float topHeight = 0.0f;
        for (const char *name : {"topbar", "ChSelectArea"})
        {
            if (UiPanel *panel = FindWidgetAs<UiPanel>(name))
            {
                const float panelHeight = ClampSize(panel->GetPreferredSize().y, height - topHeight);
                panel->Arrange(0.0f, topHeight, width, panelHeight);
                topHeight += panelHeight;
            }
        }
        fullscreenHoverAreas_[0] = {0.0f, 0.0f, width, topHeight};

        if (UiPanel *panel = FindWidgetAs<UiPanel>("rightArea"))
        {
            const float panelWidth = ClampSize(panel->GetPreferredSize().x, width);
            panel->Arrange(width - panelWidth, 0.0f, panelWidth, height);
            fullscreenHoverAreas_[1] = PanelRect(*panel);
        }
        if (UiPanel *panel = FindWidgetAs<UiPanel>("bottomArea"))
        {
            const float panelHeight = ClampSize(panel->GetPreferredSize().y, height);
            panel->Arrange(0.0f, height - panelHeight, width, panelHeight);
            fullscreenHoverAreas_[2] = PanelRect(*panel);
        }
    }

    void UiManager::SetFullscreen(bool fullscreen)
    {
        if (fullscreen_ == fullscreen)
            return;

        fullscreen_ = fullscreen;
        if (fullscreen_)
        {
            normalPanelVisibility_.clear();
            for (const UiPanel *panel : panels_)
                normalPanelVisibility_.push_back(!panel->IsHidden());
            ShowFullscreenArea(-1);
        }
        else
        {
            fullscreenHoverArea_ = -1;
            for (std::size_t index = 0; index < panels_.size() && index < normalPanelVisibility_.size(); ++index)
            {
                if (normalPanelVisibility_[index])
                    panels_[index]->Show();
                else
                    panels_[index]->Hide();
            }
            normalPanelVisibility_.clear();
        }
        ResizeUi(logicalSize_.width, logicalSize_.height);
    }

    void UiManager::ShowFullscreenArea(int area)
    {
        for (UiPanel *panel : panels_)
            panel->Hide();

        fullscreenHoverArea_ = area;
        const auto show = [this](const char *name)
        {
            if (UiPanel *panel = FindWidgetAs<UiPanel>(name))
                panel->Show();
        };
        if (area == 0)
        {
            show("topbar");
            show("ChSelectArea");
        }
        else if (area == 1)
            show("rightArea");
        else if (area == 2)
            show("bottomArea");
    }

    void UiManager::UpdateFullscreenHover(float x, float y, bool holdPanel)
    {
        if (!fullscreen_ || holdPanel)
            return;

        if (fullscreenHoverArea_ >= 0)
        {
            if (!Contains(fullscreenHoverAreas_[fullscreenHoverArea_], x, y))
                ShowFullscreenArea(-1);
            return;
        }

        for (std::size_t index = 0; index < fullscreenHoverAreas_.size(); ++index)
        {
            if (Contains(fullscreenHoverAreas_[index], x, y))
            {
                ShowFullscreenArea(static_cast<int>(index));
                return;
            }
        }
    }

    bool UiManager::IsVideoInputBlocked(float x, float y) const
    {
        for (const UiPanel *panel : panels_)
        {
            if (panel->IsHidden() || panel->GetOpt() == nullptr)
                continue;

            const std::string &name = panel->GetOpt()->objName;
            if ((name == "topbar" || name == "ChSelectArea" ||
                 name == "rightArea" || name == "bottomArea") &&
                Contains(PanelRect(*panel), x, y))
            {
                return true;
            }
        }
        return false;
    }
} // namespace sdlviewer
