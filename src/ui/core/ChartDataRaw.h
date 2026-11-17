#pragma once

#include <cstdint>

namespace sdlviewer
{
    struct ChartDataRaw
    {
        int32_t index;
        float x;
        float y;
        float z;
        float speed;
        int32_t signal;
    };
} // namespace sdlviewer
