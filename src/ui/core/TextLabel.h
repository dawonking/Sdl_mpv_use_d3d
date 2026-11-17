#pragma once

#include "UiComponentBase.h"
#include "WidgetConfig.h"

#include "imgui.h"

#include <functional>
#include <string>

namespace sdlviewer
{
class TextLabel final : public UiComponentBase
{
public:
    using TextProvider = std::function<std::string()>;

    TextLabel(int id, WidgetConfig *opt, std::string text);

    void Init() override;
    void Render() override;

    void SetText(std::string text);
    void SetTextProvider(TextProvider provider);
    void ClearTextProvider();

private:
    int id_;
    std::string text_;
    TextProvider textProvider_;
};
} // namespace sdlviewer
