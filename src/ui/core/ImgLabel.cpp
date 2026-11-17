#include "ImgLabel.h"

namespace sdlviewer
{
    ImgLabel::ImgLabel(int id, WidgetConfig *opt, TextureLoader textureLoader, std::string imageBasePath)
        : id_(id), textureLoader_(std::move(textureLoader)), imageBasePath_(std::move(imageBasePath))
    {
        opt_ = opt;
    }

    void ImgLabel::Init()
    {
        if (opt_ == nullptr)
        {
            return;
        }

        pos_ = ImVec2(opt_->position.x, opt_->position.y);
        size_ = ImVec2(opt_->size.width, opt_->size.height);
        ClearStyles();

        imageList_.clear();
        for (const std::string &fileName : opt_->images)
        {
            const std::string objName = opt_->objName + "_" + fileName;
            const std::string path = imageBasePath_ + fileName;
            imageList_.push_back(textureLoader_ ? textureLoader_(objName, path) : UiTextureHandle{});
        }

        if (!imageList_.empty() && imageList_.front().IsValid())
        {
            Resize(
                static_cast<float>(imageList_.front().width),
                static_cast<float>(imageList_.front().height));
        }
    }

    void ImgLabel::Render()
    {
        if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(imageList_.size()))
        {
            return;
        }

        const UiTextureHandle &current = imageList_[selectedIndex_];
        if (!current.IsValid())
        {
            return;
        }

        texSize_ = ImVec2(static_cast<float>(current.width), static_cast<float>(current.height));
        padding_.x = (size_.x - texSize_.x) * 0.5f;
        padding_.y = (size_.y - texSize_.y) * 0.5f;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding_);
        PushStyles();

        ImGui::Image(current.id, texSize_);

        ImGui::PopStyleVar();
        PopStyles();
    }

    void ImgLabel::ChangeImg(int num)
    {
        selectedIndex_ = num;
    }
} // namespace sdlviewer
