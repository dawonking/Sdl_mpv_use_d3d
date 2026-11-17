#include "UiComponentBase.h"

namespace sdlviewer
{
    void UiComponentBase::Move(float x, float y)
    {
        pos_.x = x;
        pos_.y = y;
    }

    void UiComponentBase::Resize(float w, float h)
    {
        size_.x = w;
        size_.y = h;
    }

    void UiComponentBase::MoveAddPoint(float x, float y)
    {
        pos_.x += x;
        pos_.y += y;
    }

    void UiComponentBase::ResizeAddPoint(float w, float h)
    {
        size_.x += w;
        size_.y += h;
    }

    void UiComponentBase::Refresh(float x, float y, float w, float h)
    {
        pos_.x = x;
        pos_.y = y;
        size_.x = w;
        size_.y = h;
    }

    void UiComponentBase::Arrange(float x, float y, float w, float h)
    {
        Refresh(x, y, w, h);
    }

    void UiComponentBase::WindowResize(int w, int h)
    {
    }

    ImVec2 UiComponentBase::GetPos() const
    {
        return pos_;
    }

    ImVec2 UiComponentBase::GetSize() const
    {
        return size_;
    }

    ImVec2 UiComponentBase::GetPreferredSize() const
    {
        if (opt_ == nullptr)
        {
            return size_;
        }

        return ImVec2(
            opt_->size.width > 0.0f ? opt_->size.width : size_.x,
            opt_->size.height > 0.0f ? opt_->size.height : size_.y);
    }

    ImVec2 UiComponentBase::GetWindowPos() const
    {
        ImVec2 windowPos = pos_;

        for (const UiComponentBase *current = parentComponent_;
             current != nullptr;
             current = current->parentComponent_)
        {
            windowPos.x += current->pos_.x;
            windowPos.y += current->pos_.y;
        }

        return windowPos;
    }

    void UiComponentBase::SetParentComponent(UiComponentBase *parentComponent)
    {
        parentComponent_ = parentComponent;
    }

    const UiComponentBase *UiComponentBase::GetParentComponent() const
    {
        return parentComponent_;
    }

    WidgetConfig *UiComponentBase::GetOpt()
    {
        return opt_;
    }

    const WidgetConfig *UiComponentBase::GetOpt() const
    {
        return opt_;
    }

    WidgetConfig *UiComponentBase::GetParentOpt()
    {
        return parent_;
    }

    UiComponentBase *UiComponentBase::FindByObjName(const std::string &targetName)
    {
        if (opt_ != nullptr && opt_->objName == targetName)
        {
            return this;
        }

        return nullptr;
    }

    void UiComponentBase::AddStyle(ImGuiCol idx, const ImVec4 &color)
    {
        colorStyles_.push_back({idx, color});
    }

    void UiComponentBase::AddStyle(ImGuiStyleVar idx, float value)
    {
        varStyles_.push_back({idx, value});
    }

    void UiComponentBase::AddStyle(ImGuiStyleVar idx, const ImVec2 &value)
    {
        varStyles_.push_back({idx, value});
    }

    void UiComponentBase::PushStyles()
    {
        for (const auto &style : colorStyles_)
        {
            ImGui::PushStyleColor(style.idx, style.color);
        }

        for (const auto &style : varStyles_)
        {
            if (std::holds_alternative<float>(style.value))
            {
                ImGui::PushStyleVar(style.idx, std::get<float>(style.value));
            }
            else
            {
                ImGui::PushStyleVar(style.idx, std::get<ImVec2>(style.value));
            }
        }
    }

    void UiComponentBase::PopStyles()
    {
        if (!colorStyles_.empty())
        {
            ImGui::PopStyleColor(static_cast<int>(colorStyles_.size()));
        }

        if (!varStyles_.empty())
        {
            ImGui::PopStyleVar(static_cast<int>(varStyles_.size()));
        }
    }

    void UiComponentBase::ClearStyles()
    {
        colorStyles_.clear();
        varStyles_.clear();
    }
    void UiComponentBase::Hide()
    {
        renderEnabled_ = false;
    }

    void UiComponentBase::Show()
    {
        renderEnabled_ = true;
    }

    bool UiComponentBase::IsHidden() const
    {
        return !renderEnabled_;
    }

} // namespace sdlviewer
