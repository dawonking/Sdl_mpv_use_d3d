#pragma once

#include "UiSlideBase.h"
#include "WidgetConfig.h"

namespace sdlviewer
{
    class CustomSlide final : public UiSlideBase
    {
    public:
        CustomSlide(int id, WidgetConfig *opt);

        void Init() override;
        void Render() override;

    private:
        int id_;
    };
} // namespace sdlviewer
