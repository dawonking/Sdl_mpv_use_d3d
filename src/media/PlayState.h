#pragma once

namespace sdlviewer
{
    enum class PlayState
    {
        NotLoad = 0,
        FileLoad,
        PlayReady,
        Playing,
        Pause,
        Stopping,
        StopAll
    };
} // namespace sdlviewer
