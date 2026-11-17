#pragma once

#include <filesystem>
#include <string>

namespace sdlviewer
{
    enum class VideoFileKind
    {
        Unsupported,
        Single,
        NumberedMp4,
        IgnoredMp4,
        LegacyMp4,
        MultitrackAvi
    };

    struct VideoFileInfo
    {
        VideoFileKind kind = VideoFileKind::Unsupported;
        std::string extension;
        std::string groupPrefix;
        int channelIndex = -1;
        int trackCount = 1;
        std::string type;
        std::string date;
        std::string time;
    };

    // Classifies names only; callers check that the actual files exist.
    VideoFileInfo InspectVideoFileName(const std::filesystem::path &path);
}
