#pragma once

#include "UiComponentBase.h"

#include "imgui.h"

#include <functional>

namespace sdlviewer
{
    class UiSlideBase : public UiComponentBase
    {
    public:
        using ReleaseCallback = std::function<void(float value)>;
        using MoveEnableProvider = std::function<bool()>;
        using ValueProvider = std::function<float()>;

        virtual ~UiSlideBase() = default;

        void SetTrackStyle(float trackThickness, float grabRadius, float grabBorderThickness)
        {
            trackThickness_ = trackThickness;
            grabRadius_ = grabRadius;
            grabBorderThickness_ = grabBorderThickness;
        }

        void SetValue(float value)
        {
            currentValue_ = value;
        }

        void SetRange(float min, float max)
        {
            minValue_ = min;
            maxValue_ = max;
        }

        void SetReleaseCallback(ReleaseCallback callback)
        {
            onRelease_ = std::move(callback);
        }

        void SetMoveEnableProvider(MoveEnableProvider provider)
        {
            moveEnableProvider_ = std::move(provider);
        }

        void SetValueProvider(ValueProvider provider)
        {
            valueProvider_ = std::move(provider);
        }

        bool IsMoveEnable() const
        {
            if (!onRelease_ || !moveEnableProvider_)
            {
                return false;
            }
            return moveEnableProvider_();
        }

    protected:
        void InvokeRelease(float value)
        {
            if (onRelease_)
            {
                onRelease_(value);
            }
        }

        bool HasValueProvider() const
        {
            return static_cast<bool>(valueProvider_);
        }

        float PollValueProvider() const
        {
            return valueProvider_ ? valueProvider_() : 0.0f;
        }

        float currentValue_ = 0.0f;
        float trackThickness_ = 6.0f;
        float minValue_ = 0.0f;
        float maxValue_ = 1.0f;

        float grabRadius_ = 10.0f;
        float grabBorderThickness_ = 3.0f;

        ImU32 trackColor_ = IM_COL32(75, 75, 75, 255);
        ImU32 fillColor_ = IM_COL32(30, 134, 255, 255);
        ImU32 grabColor_ = IM_COL32(14, 14, 14, 255);
        ImU32 grabActiveColor_ = IM_COL32(255, 0, 0, 255);
        ImU32 grabBorderColor_ = IM_COL32(176, 176, 176, 255);

    private:
        ReleaseCallback onRelease_;
        MoveEnableProvider moveEnableProvider_;
        ValueProvider valueProvider_;
    };
} // namespace sdlviewer
