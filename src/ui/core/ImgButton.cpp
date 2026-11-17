#include "ImgButton.h"

#include <iostream>

namespace sdlviewer
{
    ImgButton::ImgButton(int id, WidgetConfig *opt, TextureLoader textureLoader, std::string imageBasePath)
        : id_(id), textureLoader_(std::move(textureLoader)), imageBasePath_(std::move(imageBasePath))
    {
        opt_ = opt;
    }

    UiTextureHandle ImgButton::ResolveTexture(const std::string &fileName) const
    {
        if (fileName.empty() || !textureLoader_ || opt_ == nullptr)
        {
            return {};
        }

        const std::string objName = opt_->objName + "_" + fileName;
        const std::string path = imageBasePath_ + fileName;
        return textureLoader_(objName, path);
    }

    void ImgButton::Init()
    {
        if (opt_ == nullptr)
        {
            return;
        }

        pos_ = ImVec2(opt_->position.x, opt_->position.y);
        size_ = ImVec2(opt_->size.width, opt_->size.height);
        ClearStyles();

        btnType_ = opt_->btnType;

        normalTex_ = ResolveTexture(opt_->eventimages.normalImg);
        hoverTex_ = ResolveTexture(opt_->eventimages.hoverImg);
        clickTex_ = ResolveTexture(opt_->eventimages.clickImg);

        if (!opt_->eventimages.hoverImg.empty() && !hoverTex_.IsValid())
        {
            std::cout << opt_->objName << " imgBtn Hover img Not Found\n";
        }
        if (!opt_->eventimages.clickImg.empty() && !clickTex_.IsValid())
        {
            std::cout << opt_->objName << " imgBtn Click img Not Found\n";
        }

        if (normalTex_.IsValid())
        {
            texSize_ = ImVec2(static_cast<float>(normalTex_.width), static_cast<float>(normalTex_.height));
        }

        padding_.x = (size_.x - texSize_.x) * 0.5f;
        padding_.y = (size_.y - texSize_.y) * 0.5f;
    }

    void ImgButton::Render()
    {
        btnRelease();
        btnClick();
    }

    void ImgButton::btnClick()
    {
    }

    void ImgButton::btnRelease()
    {
        const ImVec2 screenPos = ImGui::GetCursorScreenPos();

        const bool pressed = ImGui::InvisibleButton(opt_->objName.c_str(), size_);
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();

        if (pressed)
        {
            if (btnType_ == BtnType::Toggle && !HasSelectedProvider())
            {
                StateChange(IsSelected() ? ButtonState::Normal : ButtonState::Click);
            }
            InvokeClick();
        }

        const ImVec2 btnMin = ImGui::GetItemRectMin();
        const ImVec2 btnMax = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(btnMin, btnMax, bgColor_);

        UiTextureHandle current = normalTex_;
        if (IsSelected() && clickTex_.IsValid())
        {
            current = clickTex_;
        }
        else if (held && clickTex_.IsValid())
        {
            current = clickTex_;
        }
        else if (hovered && hoverTex_.IsValid())
        {
            current = hoverTex_;
        }

        if (!current.IsValid())
        {
            return;
        }

        texSize_ = ImVec2(static_cast<float>(current.width), static_cast<float>(current.height));
        padding_.x = (size_.x - texSize_.x) * 0.5f;
        padding_.y = (size_.y - texSize_.y) * 0.5f;

        const ImVec2 imgMin(screenPos.x + padding_.x, screenPos.y + padding_.y);
        const ImVec2 imgMax(imgMin.x + texSize_.x, imgMin.y + texSize_.y);

        ImGui::GetWindowDrawList()->AddImage(current.id, imgMin, imgMax);
    }
} // namespace sdlviewer
