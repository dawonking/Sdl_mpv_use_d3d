#include "CustomSlide.h"

#include <algorithm>
#include <iostream>

namespace sdlviewer
{
    CustomSlide::CustomSlide(int id, WidgetConfig *opt)
        : id_(id)
    {
        opt_ = opt;
    }

    void CustomSlide::Init()
    {
        if (opt_ == nullptr)
        {
            return;
        }

        pos_ = ImVec2(opt_->position.x, opt_->position.y);
        size_ = ImVec2(opt_->size.width, opt_->size.height);
        ClearStyles();
    }

    void CustomSlide::Render()
    {
        if (opt_ == nullptr)
        {
            return;
        }

        const float width = size_.x;
        const float height = size_.y;

        ImVec2 startPos = ImGui::GetCursorScreenPos();
        startPos.x += 5.0f;

        ImGui::InvisibleButton(opt_->objName.c_str(), size_);

        const bool isActive = ImGui::IsItemActive() && IsMoveEnable();
        const bool isRelease = ImGui::IsItemDeactivated();
        const bool hasRange = maxValue_ > minValue_;

        if (isActive && hasRange)
        {
            float ratio = (ImGui::GetIO().MousePos.x - startPos.x) / width;
            ratio = std::clamp(ratio, 0.0f, 1.0f);
            currentValue_ = minValue_ + ratio * (maxValue_ - minValue_);
        }
        else if (!isActive && !isRelease && hasRange)
        {
            if (HasValueProvider())
            {
                const float ratio = PollValueProvider();
                currentValue_ = minValue_ + ratio * (maxValue_ - minValue_);
            }
        }

        if (isRelease)
        {
            std::cout << opt_->objName << " slide Click Release\n";
            InvokeRelease(currentValue_);
        }

        const float ratio = hasRange
                                ? (currentValue_ - minValue_) / (maxValue_ - minValue_)
                                : 0.0f;

        const float centerY = startPos.y + height * 0.5f;
        const float grabX = startPos.x + ratio * width;
        const float trackRounding = trackThickness_ * 0.5f;

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 trackMin(startPos.x, centerY - trackThickness_ * 0.5f);
        const ImVec2 trackMax(startPos.x + width, centerY + trackThickness_ * 0.5f);

        drawList->AddRectFilled(trackMin, trackMax, trackColor_, trackRounding);
        drawList->AddRectFilled(trackMin, ImVec2(grabX, trackMax.y), fillColor_, trackRounding);
        drawList->AddCircleFilled(ImVec2(grabX, centerY), grabRadius_, isActive ? grabActiveColor_ : grabColor_, 32);
        drawList->AddCircle(ImVec2(grabX, centerY), grabRadius_, grabBorderColor_, 32, grabBorderThickness_);
    }

} // namespace sdlviewer
