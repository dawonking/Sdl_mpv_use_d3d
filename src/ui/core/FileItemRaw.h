#pragma once

#include <cstdint>

namespace sdlviewer
{
    enum class FileType : int32_t
    {
        INF = 0,
        PRK = 1,
        EVT = 2,
        USR = 3,
        UNKNOWN = -1
    };

#pragma pack(push, 1)
    struct FileItemRaw
    {
        int32_t type;
        char date[12];
        char time1[9];
        char time2[8];
        char filename[128];
        char filepath[256];
        char nativepath[256];
    };
#pragma pack(pop)
} // namespace sdlviewer
