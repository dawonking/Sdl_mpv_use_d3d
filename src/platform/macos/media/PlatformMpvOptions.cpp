#include "media/PlatformMpvOptions.h"

#include <mpv/client.h>

namespace sdlviewer
{
    int ApplyPlatformMpvOptions(mpv_handle *handle)
    {
        return handle != nullptr
            ? 0
            : MPV_ERROR_INVALID_PARAMETER;
    }
}