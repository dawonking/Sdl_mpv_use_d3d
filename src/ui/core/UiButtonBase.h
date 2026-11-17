#pragma once

#include "IUiButton.h"
#include "WidgetConfig.h"

#include <functional>

namespace sdlviewer
{
    enum class ButtonState
    {
        Normal = 0,
        Hover,
        Click
    };

    class UiButtonBase : public IUiButton
    {
    public:
        using ClickCallBack = std::function<void()>;
        using SelectedProvider = std::function<bool()>;

        virtual ~UiButtonBase() = default;

        ButtonState GetBtnState() const
        {
            return btnState_;
        }

        void StateChange(ButtonState state)
        {
            btnState_ = state;
        }

        void SetOnClick(ClickCallBack callback)
        {
            onClick_ = std::move(callback);
        }

        void ClearClick()
        {
            onClick_ = nullptr;
        }

        void SetSelectedProvider(SelectedProvider provider)
        {
            selectedProvider_ = std::move(provider);
        }

        void ClearSelectedProvider()
        {
            selectedProvider_ = nullptr;
        }

        bool HasSelectedProvider() const
        {
            return static_cast<bool>(selectedProvider_);
        }

        bool IsSelected() const
        {
            if (selectedProvider_)
            {
                return selectedProvider_();
            }

            return btnState_ == ButtonState::Click;
        }

    protected:
        void InvokeClick()
        {
            if (onClick_)
            {
                onClick_();
            }
        }

        BtnType btnType_ = BtnType::Normal;
        ButtonState btnState_ = ButtonState::Normal;

    private:
        ClickCallBack onClick_;
        SelectedProvider selectedProvider_;
    };
} // namespace sdlviewer
