#include "media/MpvPlayer.h"
#include "media/PlatformMpvOptions.h"

#include <mpv/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace sdlviewer
{
    namespace
    {
        constexpr const char *kVideoFlags[]{"setHFlip", "setVFlip", "activeBorder"};

        std::string MpvErrorMessage(const char *operation, int errorCode)
        {
            std::ostringstream stream;
            stream << operation << " failed: " << mpv_error_string(errorCode);
            return stream.str();
        }
    } // namespace

    MpvPlayer::MpvPlayer(int channelIndex)
        : channelIndex_(channelIndex)
    {
    }

    MpvPlayer::~MpvPlayer()
    {
        Shutdown();
    }

    RenderStatus MpvPlayer::Initialize()
    {
        handle_ = mpv_create();
        if (!handle_)
        {
            return RenderStatus::Error(
                "Failed to create an mpv instance.");
        }

        mpv_set_option_string(
            handle_,
            "profile",
            "fast");

        mpv_set_option_string(
            handle_,
            "vo",
            "libmpv");

        const int platformOptionsResult =
            ApplyPlatformMpvOptions(handle_);

        if (platformOptionsResult < 0)
        {
            const std::string message =
                MpvErrorMessage(
                    "ApplyPlatformMpvOptions",
                    platformOptionsResult);

            mpv_destroy(handle_);
            handle_ = nullptr;

            return RenderStatus::Error(message);
        }

        mpv_set_option_string(
            handle_,
            "gpu-dumb-mode",
            "yes");

        mpv_set_option_string(
            handle_,
            "sub-visibility",
            "no");

        mpv_set_option_string(
            handle_,
            "osd-level",
            "0");

        mpv_set_option_string(
            handle_,
            "terminal",
            "no");

        mpv_set_option_string(
            handle_,
            "mute",
            channelIndex_ == 0 ? "no" : "yes");

        mpv_set_option_string(
            handle_,
            "video-sync",
            channelIndex_ == 0
                ? "display-resample"
                : "audio");

        const int result =
            mpv_initialize(handle_);

        if (result < 0)
        {
            mpv_destroy(handle_);
            handle_ = nullptr;

            return RenderStatus::Error(
                MpvErrorMessage(
                    "mpv_initialize",
                    result));
        }

        supportsVideoFlags_ = true;
        for (const char *name : kVideoFlags)
        {
            int value = 0;
            if (mpv_get_property(handle_, name, MPV_FORMAT_FLAG, &value) < 0)
                supportsVideoFlags_ = false;
        }
        return RenderStatus::Ok();
    }

    void MpvPlayer::Shutdown() noexcept
    {
        if (handle_)
        {
            mpv_destroy(handle_);
            handle_ = nullptr;
        }
    }

    RenderStatus MpvPlayer::OpenFile(
        const std::string &utf8Path,
        int videoTrackId)
    {
        if (!handle_)
        {
            return RenderStatus::Error("mpv instance is not initialized.");
        }

        if (utf8Path.empty() ||
            utf8Path.find('\0') != std::string::npos)
        {
            return RenderStatus::Error("Video path is invalid.");
        }

        if (videoTrackId <= 0)
        {
            return RenderStatus::Error(
                "Video track ID must be greater than zero.");
        }

        std::string opts =
            "vid=" + std::to_string(videoTrackId) +
            ",speed=" + std::to_string(playbackSpeed_) + ",pause=no";

        if (channelIndex_ != 0)
        {
            opts += ",aid=no";
        }

        const char *cmd[] = {
            "loadfile",
            utf8Path.c_str(),
            "replace",
            "-1",
            opts.c_str(),
            nullptr};

        const int result = mpv_command_async(handle_, 0, cmd);
        if (result < 0)
        {
            return RenderStatus::Error(
                MpvErrorMessage("loadfile", result));
        }

        return RenderStatus::Ok();
    }

    void MpvPlayer::TogglePause()
    {
        if (!handle_)
            return;

        int flag = 0;
        mpv_get_property(handle_, "pause", MPV_FORMAT_FLAG, &flag);
        flag = !flag;
        mpv_set_property_async(handle_, 0, "pause", MPV_FORMAT_FLAG, &flag);
    }

    void MpvPlayer::Stop()
    {
        if (!handle_)
            return;

        const char *cmd[] = {"stop", nullptr};
        mpv_command_async(handle_, 0, cmd);
    }

    void MpvPlayer::Seek(float normalizedPosition)
    {
        if (!handle_)
            return;

        double duration = 0.0;
        mpv_get_property(handle_, "duration", MPV_FORMAT_DOUBLE, &duration);

        double targetPos = duration * static_cast<double>(normalizedPosition);
        const double remaining = duration - targetPos;

        if (remaining < seekBoundaryInterval_)
        {
            targetPos = duration - seekBoundaryInterval_;
        }

        if (targetPos < 0.0)
        {
            targetPos = 0.0;
        }

        const std::string posStr = std::to_string(targetPos);
        const char *cmd[] = {
            "seek",
            posStr.c_str(),
            "absolute",
            nullptr};

        mpv_command_async(handle_, 0, cmd);
    }

    RenderStatus MpvPlayer::SetPlaybackSpeed(double rate)
    {
        if (!handle_)
        {
            return RenderStatus::Error("mpv instance is not initialized.");
        }
        if (!std::isfinite(rate) || rate <= 0.0)
        {
            return RenderStatus::Error("Playback speed must be finite and positive.");
        }

        const int result =
            mpv_set_property_async(handle_, 0, "speed", MPV_FORMAT_DOUBLE, &rate);
        if (result < 0)
        {
            return RenderStatus::Error(MpvErrorMessage("set speed", result));
        }

        playbackSpeed_ = rate;
        return RenderStatus::Ok();
    }

    RenderStatus MpvPlayer::SetAudio(double volume, bool muted)
    {
        if (!handle_)
        {
            return RenderStatus::Error("mpv instance is not initialized.");
        }
        if (!std::isfinite(volume))
        {
            return RenderStatus::Error("Volume must be finite.");
        }

        volume = std::clamp(volume, 0.0, 100.0);
        int result =
            mpv_set_property_async(handle_, 0, "volume", MPV_FORMAT_DOUBLE, &volume);
        if (result < 0)
        {
            return RenderStatus::Error(MpvErrorMessage("set volume", result));
        }

        int mute = muted ? 1 : 0;
        result = mpv_set_property_async(handle_, 0, "mute", MPV_FORMAT_FLAG, &mute);
        if (result < 0)
        {
            return RenderStatus::Error(MpvErrorMessage("set mute", result));
        }
        return RenderStatus::Ok();
    }

    RenderStatus MpvPlayer::SeekSeconds(double positionSec)
    {
        if (!handle_)
        {
            return RenderStatus::Error("mpv instance is not initialized.");
        }
        if (!std::isfinite(positionSec))
        {
            return RenderStatus::Error("Seek position must be finite.");
        }

        double duration = 0.0;
        const int durationResult =
            mpv_get_property(handle_, "duration", MPV_FORMAT_DOUBLE, &duration);
        if (durationResult < 0)
        {
            return RenderStatus::Error(MpvErrorMessage("get duration", durationResult));
        }
        if (!std::isfinite(duration) || duration <= 0.0)
        {
            return RenderStatus::Error("Video duration is not available.");
        }

        const std::string position =
            std::to_string(std::clamp(positionSec, 0.0, duration));
        const char *cmd[] = {"seek", position.c_str(), "absolute+exact", nullptr};
        const int result = mpv_command_async(handle_, 0, cmd);
        if (result < 0)
        {
            return RenderStatus::Error(MpvErrorMessage("seek", result));
        }
        return RenderStatus::Ok();
    }

    RenderStatus MpvPlayer::StepFrame(int direction)
    {
        if (!handle_)
        {
            return RenderStatus::Error("mpv instance is not initialized.");
        }
        if (direction != -1 && direction != 1)
        {
            return RenderStatus::Error("Frame direction must be -1 or 1.");
        }

        const char *command = direction > 0 ? "frame-step" : "frame-back-step";
        const char *cmd[] = {command, nullptr};
        const int result = mpv_command_async(handle_, 0, cmd);
        if (result < 0)
        {
            return RenderStatus::Error(MpvErrorMessage(command, result));
        }
        return RenderStatus::Ok();
    }

    RenderStatus MpvPlayer::SetVideoTrack(int trackId, std::uint64_t requestId)
    {
        if (!handle_)
        {
            return RenderStatus::Error("mpv instance is not initialized.");
        }
        if (trackId <= 0)
        {
            return RenderStatus::Error("Video track ID must be greater than zero.");
        }

        std::int64_t vid = trackId;
        const int result = mpv_set_property_async(handle_, requestId, "vid", MPV_FORMAT_INT64, &vid);
        if (result < 0)
        {
            return RenderStatus::Error(MpvErrorMessage("queue video track change", result));
        }
        return RenderStatus::Ok();
    }

    RenderStatus MpvPlayer::SetVideoFlag(VideoFlag flag, bool enabled, std::uint64_t requestId)
    {
        const int index = static_cast<int>(flag);
        if (!handle_ || !supportsVideoFlags_ || index < 0 || index >= 3)
            return RenderStatus::Error("mpv video flag is unavailable.");

        int value = enabled ? 1 : 0;
        const int result = mpv_set_property_async(handle_, requestId, kVideoFlags[index], MPV_FORMAT_FLAG, &value);
        if (result < 0)
            return RenderStatus::Error(MpvErrorMessage(kVideoFlags[index], result));
        return RenderStatus::Ok();
    }
} // namespace sdlviewer
