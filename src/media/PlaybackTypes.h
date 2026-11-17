#pragma once

#include "PlayState.h"

#include <cstdint>

namespace sdlviewer
{
    enum class PlaybackSpeed
    {
        Half = 0,
        Normal,
        OnePointFive,
        Double
    };

    inline double ToPlaybackRate(PlaybackSpeed speed)
    {
        switch (speed)
        {
        case PlaybackSpeed::Half:
            return 0.5;
        case PlaybackSpeed::OnePointFive:
            return 1.5;
        case PlaybackSpeed::Double:
            return 2.0;
        case PlaybackSpeed::Normal:
        default:
            return 1.0;
        }
    }

    struct PlaybackState
    {
        PlayState playState = PlayState::NotLoad;
        PlaybackSpeed speed = PlaybackSpeed::Normal;
        std::uint64_t mediaRevision = 0;
        double position = 0.0;
        double duration = 0.0;
    };

    enum class PlaybackChange
    {
        PlayStateChanged = 0,
        SpeedChanged,
        MediaChanged,
        Reset
    };

    struct PlaybackChangedEvent
    {
        PlaybackState state;
        PlaybackChange change = PlaybackChange::Reset;
    };
} // namespace sdlviewer
