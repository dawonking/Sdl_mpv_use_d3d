#pragma once

#include "UiButtonBase.h"
#include "UiComponentBase.h"
#include "WidgetConfig.h"

#include <string>

namespace sdlviewer
{
class ViewButton final : public UiComponentBase, public UiButtonBase
{
public:
    ViewButton(int id, WidgetConfig *opt, std::string text);

    void Init() override;
    void Render() override;
    void SetText(std::string text) { buttonText_ = std::move(text); }

    void btnClick() override;
    void btnRelease() override;
    void btnDragStart() override;
    void btnDragEnd() override;

private:
    int id_;
    std::string buttonText_;
};
} // namespace sdlviewer
