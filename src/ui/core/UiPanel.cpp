#include "UiPanel.h"

#include "TextLabel.h"
#include "UiLayout.h"
#include "ViewButton.h"
#include "ImgLabel.h"
#include "ImgButton.h"
#include "CustomSlide.h"
#include "WidgetGroup.h"

#include <algorithm>
#include <iostream>

namespace sdlviewer
{
    UiPanel::UiPanel(int id, WidgetConfig *opt, ConfigResolver resolver, TextureLoader textureLoader, std::string imageBasePath)
        : id_(id), resolveConfig_(std::move(resolver)),
          textureLoader_(std::move(textureLoader)), imageBasePath_(std::move(imageBasePath))
    {
        opt_ = opt;
        name_ = opt_ != nullptr ? opt_->objName : std::string();
    }

    void UiPanel::Init()
    {
        flags_ = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;

        if (opt_ == nullptr)
        {
            return;
        }

        InitChildren();
        pos_ = ImVec2(opt_->position.x, opt_->position.y);
        size_ = GetPreferredSize();
        ClearStyles();

        backgroundColor_ = ImVec4(
            opt_->color.Normal.r,
            opt_->color.Normal.g,
            opt_->color.Normal.b,
            opt_->color.Normal.a);
    }

    void UiPanel::InitChildren()
    {
        for (const std::string &childName : opt_->grouplist)
        {
            WidgetConfig *childOpt = resolveConfig_ ? resolveConfig_(childName) : nullptr;
            if (childOpt == nullptr)
            {
                std::cout << childName << " Not Found\n";
                continue;
            }

            switch (childOpt->type)
            {
            case UiType::Pannel:
                CreateChild<UiPanel>(
                    static_cast<int>(children_.size()),
                    childOpt,
                    resolveConfig_,
                    textureLoader_,
                    imageBasePath_);
                break;

            case UiType::Text:
                CreateChild<TextLabel>(
                    static_cast<int>(children_.size()),
                    childOpt,
                    childOpt->text);
                break;

            case UiType::ClickBtn:
                CreateChild<ViewButton>(
                    static_cast<int>(children_.size()),
                    childOpt,
                    childOpt->text);
                break;
            case UiType::imgBtn:
                CreateChild<ImgButton>(
                    static_cast<int>(children_.size()),
                    childOpt,
                    textureLoader_,
                    imageBasePath_);
                break;

            case UiType::img:
                CreateChild<ImgLabel>(
                    static_cast<int>(children_.size()),
                    childOpt,
                    textureLoader_,
                    imageBasePath_);
                break;
            case UiType::slide:
                CreateChild<CustomSlide>(
                    static_cast<int>(children_.size()),
                    childOpt);
                break;
            case UiType::group:
                CreateChild<WidgetGroup>(
                    static_cast<int>(children_.size()),
                    childOpt,
                    resolveConfig_,
                    textureLoader_,
                    imageBasePath_);
                break;

            default:
                // Phase 5에서 각 위젯 타입이 포팅되는 대로 case를 추가한다.
                break;
            }
        }
    }

    UiComponentBase *UiPanel::FindByObjName(const std::string &targetName)
    {
        if (UiComponentBase *component = UiComponentBase::FindByObjName(targetName))
        {
            return component;
        }

        for (const auto &child : children_)
        {
            if (child)
            {
                if (UiComponentBase *component = child->FindByObjName(targetName))
                {
                    return component;
                }
            }
        }

        return nullptr;
    }

    ImVec2 UiPanel::GetPreferredSize() const
    {
        if (opt_ == nullptr)
        {
            return size_;
        }

        const ImVec2 measured = UiLayout::MeasureChildren(children_, *opt_);
        return ImVec2(
            opt_->size.width > 0.0f ? opt_->size.width : measured.x,
            opt_->size.height > 0.0f ? opt_->size.height : measured.y);
    }

    void UiPanel::Arrange(float x, float y, float w, float h)
    {
        UiComponentBase::Arrange(x, y, w, h);
        if (opt_ != nullptr)
        {
            UiLayout::ArrangeChildren(children_, *opt_, size_);
        }
    }

    void UiPanel::WindowResize(int w, int h)
    {
        if (opt_ == nullptr)
        {
            return;
        }

        const ImVec2 preferred = GetPreferredSize();
        const float panelWidth = opt_->scale.width == ScaleType::Expanding
                                     ? std::max(0.0f, static_cast<float>(w) - opt_->position.x)
                                     : preferred.x;
        const float panelHeight = opt_->scale.height == ScaleType::Expanding
                                      ? std::max(0.0f, static_cast<float>(h) - opt_->position.y)
                                      : preferred.y;

        Arrange(opt_->position.x, opt_->position.y, panelWidth, panelHeight);
    }

    void UiPanel::Move(float x, float y)
    {
        Arrange(x, y, size_.x, size_.y);
    }

    void UiPanel::Resize(float w, float h)
    {
        Arrange(pos_.x, pos_.y, w, h);
    }

    void UiPanel::MoveAddPoint(float x, float y)
    {
        Move(pos_.x + x, pos_.y + y);
    }

    void UiPanel::ResizeAddPoint(float w, float h)
    {
        Resize(size_.x + w, size_.y + h);
    }

    void UiPanel::Render()
    {
        if (IsHidden())
        {
            return;
        }

        ImGui::SetNextWindowPos(pos_, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size_, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, backgroundColor_);

        PushStyles();
        ImGui::Begin(name_.c_str(), nullptr, flags_);

        for (const auto &child : children_)
        {
            if (!child || child->IsHidden())
            {
                continue;
            }

            ImGui::SetCursorPos(child->GetPos());
            child->Render();
        }

        ImGui::End();
        PopStyles();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
} // namespace sdlviewer
