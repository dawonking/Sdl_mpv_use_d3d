#pragma once

#include <cstdint>

namespace sdlviewer
{
    struct GPSDataRaw
    {
        int32_t index;
        float latitude;
        float longitude;
        float speed;
    };
} // namespace sdlviewer
