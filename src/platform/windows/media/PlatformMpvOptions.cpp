#include "media/PlatformMpvOptions.h"

#include <mpv/client.h>

namespace sdlviewer
{
    int ApplyPlatformMpvOptions(
        mpv_handle *handle)
    {
        if (handle == nullptr)
        {
            return MPV_ERROR_INVALID_PARAMETER;
        }

        int result = mpv_set_option_string(
            handle,
            "hwdec",
            "d3d11va");

        if (result < 0)
        {
            return result;
        }

        return mpv_set_option_string(
            handle,
            "d3d11va-zero-copy",
            "yes");
    }
}