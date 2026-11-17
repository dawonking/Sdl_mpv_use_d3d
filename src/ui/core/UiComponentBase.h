#pragma once

#include "IUiComponent.h"
#include "WidgetConfig.h"

#include "imgui.h"

#include <string>
#include <variant>
#include <vector>

namespace sdlviewer
{
class UiComponentBase : public IUiComponent
{
public:
    virtual ~UiComponentBase() = default;

    void Move(float x, float y) override;
    void Resize(float w, float h) override;
    void MoveAddPoint(float x, float y) override;
    void ResizeAddPoint(float w, float h) override;
    void Refresh(float x, float y, float w, float h) override;
    void Arrange(float x, float y, float w, float h) override;
    void WindowResize(int w, int h) override;

    ImVec2 GetPos() const override;
    ImVec2 GetSize() const override;
    ImVec2 GetPreferredSize() const override;

    void SetParentComponent(UiComponentBase *parentComponent);
    const UiComponentBase *GetParentComponent() const;
    ImVec2 GetWindowPos() const;

    WidgetConfig *GetOpt();
    const WidgetConfig *GetOpt() const;
    WidgetConfig *GetParentOpt();

    virtual UiComponentBase *FindByObjName(const std::string &targetName);

    void AddStyle(ImGuiCol idx, const ImVec4 &color);
    void AddStyle(ImGuiStyleVar idx, float value);
    void AddStyle(ImGuiStyleVar idx, const ImVec2 &value);
    void PushStyles();
    void PopStyles();
    void ClearStyles();

    void Hide();
    void Show();
    bool IsHidden() const;

protected:
    ImVec2 pos_{0.0f, 0.0f};
    ImVec2 size_{0.0f, 0.0f};

    UiComponentBase *parentComponent_ = nullptr;

    WidgetConfig *opt_ = nullptr;
    WidgetConfig *parent_ = nullptr;

private:
    struct ColorStyle
    {
        ImGuiCol idx;
        ImVec4 color;
    };

    struct VarStyle
    {
        ImGuiStyleVar idx;
        std::variant<float, ImVec2> value;
    };

    std::vector<ColorStyle> colorStyles_;
    std::vector<VarStyle> varStyles_;

    bool renderEnabled_ = true;
};
} // namespace sdlviewer
