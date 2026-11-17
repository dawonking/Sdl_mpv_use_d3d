#pragma once

#include "render/RenderTypes.h"

#include <cstdint>
#include <string>

namespace sdlviewer
{
    enum class VideoFlag
    {
        HorizontalFlip = 0,
        VerticalFlip,
        ActiveBorder
    };

    class MpvPlayer final
    {
    public:
        explicit MpvPlayer(int channelIndex);
        ~MpvPlayer();

        MpvPlayer(const MpvPlayer &) = delete;
        MpvPlayer &operator=(const MpvPlayer &) = delete;

        RenderStatus Initialize();
        void Shutdown() noexcept;

        mpv_handle *Handle() const noexcept { return handle_; }

        RenderStatus OpenFile(
            const std::string &utf8Path,
            int videoTrackId);

        void TogglePause();
        void Stop();
        void Seek(float normalizedPosition);
        RenderStatus SetPlaybackSpeed(double rate);
        RenderStatus SetAudio(double volume, bool muted);
        RenderStatus SeekSeconds(double positionSec);
        RenderStatus StepFrame(int direction);
        // Reports queue acceptance; execution errors arrive through the mpv event thread.
        RenderStatus SetVideoTrack(int trackId, std::uint64_t requestId);
        RenderStatus SetVideoFlag(VideoFlag flag, bool enabled, std::uint64_t requestId);
        bool SupportsVideoFlags() const noexcept { return supportsVideoFlags_; }

        bool ShouldBlockForTargetTime() const noexcept
        {
            return channelIndex_ == 0;
        }

    private:
        int channelIndex_ = 0;
        mpv_handle *handle_ = nullptr;
        double playbackSpeed_ = 1.0;
        double seekBoundaryInterval_ = 0.9;
        bool supportsVideoFlags_ = false;
    };
} // namespace sdlviewer
