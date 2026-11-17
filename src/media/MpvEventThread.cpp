#include "media/MpvEventThread.h"

#include <mpv/client.h>

#include <cmath>
#include <cstring>
#include <utility>

namespace sdlviewer
{

    MpvEventThread::MpvEventThread(mpv_handle *handle, MpvPlaybackCallbacks callbacks)
        : handle_(handle), callbacks_(std::move(callbacks))
    {
    }

    MpvEventThread::~MpvEventThread()
    {
        Stop();
    }

    void MpvEventThread::Start()
    {
        if (!handle_ || thread_.joinable())
        {
            return;
        }

        RegisterObservers();

        running_.store(true, std::memory_order_release);
        thread_ = std::thread(&MpvEventThread::EventLoop, this);
    }

    void MpvEventThread::Stop() noexcept
    {
        running_.store(false, std::memory_order_release);

        if (handle_)
        {
            mpv_wakeup(handle_);
        }

        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    bool MpvEventThread::IsRunning() const noexcept
    {
        return running_.load(std::memory_order_acquire);
    }

    void MpvEventThread::RegisterObservers()
    {
        mpv_observe_property(handle_, 0, "time-pos", MPV_FORMAT_DOUBLE);
        mpv_observe_property(handle_, 0, "duration", MPV_FORMAT_DOUBLE);
        mpv_observe_property(handle_, 0, "estimated-frame-number", MPV_FORMAT_INT64);
        mpv_observe_property(handle_, 0, "pause", MPV_FORMAT_FLAG);
        mpv_observe_property(handle_, 0, "vid", MPV_FORMAT_INT64);
        mpv_observe_property(handle_, 0, "core-idle", MPV_FORMAT_FLAG);
    }

    void MpvEventThread::EventLoop()
    {
        while (running_.load(std::memory_order_acquire))
        {
            mpv_event *event = mpv_wait_event(handle_, -1.0);

            if (event->event_id == MPV_EVENT_NONE)
            {
                continue;
            }

            if (event->event_id == MPV_EVENT_SHUTDOWN)
            {
                running_.store(false, std::memory_order_release);
                break;
            }

            HandleEvent(event);
        }
    }

    void MpvEventThread::HandleEvent(mpv_event *event)
    {
        switch (event->event_id)
        {
        case MPV_EVENT_PROPERTY_CHANGE:
        {
            auto *prop = static_cast<mpv_event_property *>(event->data);

            if (std::strcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE)
            {
                const double currentTime = *static_cast<double *>(prop->data);
                if (std::isfinite(currentTime) && currentTime != lastTimePosition_)
                {
                    lastTimePosition_ = currentTime;
                    if (callbacks_.onTimePosition)
                    {
                        callbacks_.onTimePosition(currentTime);
                    }
                }
            }
            else if (std::strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE)
            {
                if (callbacks_.onDurationChanged)
                {
                    callbacks_.onDurationChanged(*static_cast<double *>(prop->data));
                }
            }
            else if (std::strcmp(prop->name, "estimated-frame-number") == 0 &&
                     prop->format == MPV_FORMAT_INT64 && !firstFrameSent_)
            {
                firstFrameSent_ = true;
                if (callbacks_.onFirstFrameDecoded)
                {
                    callbacks_.onFirstFrameDecoded();
                }
            }
            else if (std::strcmp(prop->name, "pause") == 0 && prop->format == MPV_FORMAT_FLAG)
            {
                const bool paused = (*static_cast<int *>(prop->data)) != 0;
                if (callbacks_.onPauseChanged)
                {
                    callbacks_.onPauseChanged(paused);
                }
            }
            break;
        }
        case MPV_EVENT_START_FILE:
            lastTimePosition_ = 0.0;
            firstFrameSent_ = false;
            break;
        case MPV_EVENT_PLAYBACK_RESTART:
            if (callbacks_.onSeekCompleted)
            {
                callbacks_.onSeekCompleted();
            }
            break;
        case MPV_EVENT_END_FILE:
        {
            auto *endData = static_cast<mpv_event_end_file *>(event->data);
            HandlePlaybackEnded(endData->reason, endData->error);
            break;
        }
        case MPV_EVENT_COMMAND_REPLY:
            HandleCommandReply(event->error);
            break;
        case MPV_EVENT_SET_PROPERTY_REPLY:
            HandleCommandReply(event->error);
            if (callbacks_.onPropertyReply)
            {
                callbacks_.onPropertyReply(event->reply_userdata, event->error);
            }
            break;

        default:
            break;
        }
    }

    void MpvEventThread::HandlePlaybackEnded(int reason, int error)
    {
        if (callbacks_.onPlaybackEnded)
        {
            callbacks_.onPlaybackEnded(reason, error);
        }
    }

    void MpvEventThread::HandleCommandReply(int error)
    {
        if (error >= 0)
        {
            return;
        }

        if (callbacks_.onCommandError)
        {
            callbacks_.onCommandError(error);
        }
    }

}
