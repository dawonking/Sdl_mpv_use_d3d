#include "TextLabel.h"

#include <algorithm>

namespace sdlviewer
{
    TextLabel::TextLabel(int id, WidgetConfig *opt, std::string text)
        : id_(id), text_(std::move(text))
    {
        opt_ = opt;
    }

    void TextLabel::Init()
    {
        if (opt_ == nullptr)
        {
            return;
        }

        size_ = ImVec2(opt_->size.width, opt_->size.height);
    }

    void TextLabel::Render()
    {
        const std::string currentText = textProvider_ ? textProvider_() : text_;
        const ImVec2 textSize = ImGui::CalcTextSize(currentText.c_str());
        const ImVec2 textPos(
            pos_.x + std::max(0.0f, size_.x - textSize.x) * 0.5f,
            pos_.y + std::max(0.0f, size_.y - textSize.y) * 0.5f);

        ImGui::SetCursorPos(textPos);
        ImGui::TextUnformatted(currentText.c_str(), currentText.c_str() + currentText.size());
    }

    void TextLabel::SetText(std::string text)
    {
        text_ = std::move(text);
        textProvider_ = {};
    }

    void TextLabel::SetTextProvider(TextProvider provider)
    {
        textProvider_ = std::move(provider);
    }

    void TextLabel::ClearTextProvider()
    {
        textProvider_ = {};
    }
} // namespace sdlviewer
