#pragma once

#include "render/RenderTypes.h"

namespace sdlviewer
{
    class IUiFrame
    {
    public:
        virtual ~IUiFrame() = default;
        virtual void RenderUi() = 0;
        virtual void OnResize(PixelSize size) = 0;
    };

} // namespace sdlviewer
