#pragma once

#include "UiComponentBase.h"
#include "WidgetConfig.h"

#include <memory>
#include <vector>

namespace sdlviewer
{
    namespace UiLayout
    {
        LayoutType ResolveLayoutType(const WidgetConfig &container);

        ImVec2 MeasureChildren(
            const std::vector<std::unique_ptr<UiComponentBase>> &children,
            const WidgetConfig &container);

        void ArrangeChildren(
            const std::vector<std::unique_ptr<UiComponentBase>> &children,
            const WidgetConfig &container,
            const ImVec2 &containerSize);
    } // namespace UiLayout
} // namespace sdlviewer
