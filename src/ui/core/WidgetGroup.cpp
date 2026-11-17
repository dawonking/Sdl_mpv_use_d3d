#include "WidgetGroup.h"

#include "UiLayout.h"
#include "TextLabel.h"
#include "ViewButton.h"
#include "ImgButton.h"
#include "ImgLabel.h"
#include "CustomSlide.h"
#include "CustomFileList.h"
#include "SensorGraph.h"

#include <iostream>

namespace sdlviewer
{
    WidgetGroup::WidgetGroup(int id, WidgetConfig *opt, ConfigResolver resolver, TextureLoader textureLoader, std::string imageBasePath)
        : id_(id), resolveConfig_(std::move(resolver)),
          textureLoader_(std::move(textureLoader)), imageBasePath_(std::move(imageBasePath))
    {
        opt_ = opt;
        name_ = opt_ != nullptr ? opt_->objName : std::string();
    }

    void WidgetGroup::Init()
    {
        if (opt_ == nullptr)
        {
            return;
        }

        childFlags_ = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar;
        InitChildren();
        size_ = GetPreferredSize();

        backgroundColor_ = ImVec4(
            opt_->color.Normal.r,
            opt_->color.Normal.g,
            opt_->color.Normal.b,
            opt_->color.Normal.a);
    }

    void WidgetGroup::InitChildren()
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
            case UiType::group:
                CreateChild<WidgetGroup>(
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
            case UiType::filelist:
                CreateChild<CustomFileList>(
                    static_cast<int>(children_.size()),
                    childOpt);
                break;
            case UiType::graph:
                CreateChild<SensorGraph>(
                    static_cast<int>(children_.size()),
                    childOpt);
                break;

            default:
                // graph(SensorGraph)/filelist(customFileList)는 아직 미포팅 — 다음 단계에서 추가.
                break;
            }
        }
    }

    UiComponentBase *WidgetGroup::FindByObjName(const std::string &targetName)
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

    ImVec2 WidgetGroup::GetPreferredSize() const
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

    void WidgetGroup::Arrange(float x, float y, float w, float h)
    {
        UiComponentBase::Arrange(x, y, w, h);
        if (opt_ != nullptr)
        {
            UiLayout::ArrangeChildren(children_, *opt_, size_);
        }
    }

    void WidgetGroup::WindowResize(int w, int h)
    {
        Arrange(pos_.x, pos_.y, static_cast<float>(w), static_cast<float>(h));
    }

    void WidgetGroup::Render()
    {
        if (IsHidden())
        {
            return;
        }

        ImGui::SetCursorPos(pos_);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, backgroundColor_);
        ImGui::BeginChild(name_.c_str(), size_, ImGuiChildFlags_None, childFlags_);

        for (const auto &child : children_)
        {
            if (!child || child->IsHidden())
            {
                continue;
            }

            ImGui::SetCursorPos(child->GetPos());
            child->Render();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
} // namespace sdlviewer
