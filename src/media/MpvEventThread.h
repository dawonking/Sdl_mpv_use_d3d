// src/media/MpvEventThread.h
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

struct mpv_handle;
struct mpv_event;

namespace sdlviewer
{ 
    struct MpvPlaybackCallbacks
    {
        std::function<void(double)> onTimePosition;
        std::function<void(double)> onDurationChanged;
        std::function<void(bool)> onPauseChanged;
        std::function<void()> onFirstFrameDecoded;
        std::function<void()> onSeekCompleted;
        std::function<void(int reason, int error)> onPlaybackEnded;
        std::function<void(int error)> onCommandError;
        std::function<void(std::uint64_t requestId, int error)> onPropertyReply;
    };

    class MpvEventThread final
    {
    public:
        MpvEventThread(mpv_handle *handle, MpvPlaybackCallbacks callbacks);
        ~MpvEventThread();

        MpvEventThread(const MpvEventThread &) = delete;
        MpvEventThread &operator=(const MpvEventThread &) = delete;

        void Start();
        void Stop() noexcept;
        bool IsRunning() const noexcept;

    private:
        void EventLoop();
        void RegisterObservers();
        void HandleEvent(mpv_event *event);
        void HandlePlaybackEnded(int reason, int error);
        void HandleCommandReply(int error);

        mpv_handle *handle_ = nullptr;
        MpvPlaybackCallbacks callbacks_;

        double lastTimePosition_ = 0.0;
        bool firstFrameSent_ = false;

        std::atomic<bool> running_{false};
        std::thread thread_;
    };
}
