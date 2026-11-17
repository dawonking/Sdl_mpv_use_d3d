#include "ControlManager.h"

#include "ui/core/CustomFileList.h"
#include "ui/core/FileListManager.h"
#include "ui/core/TextLabel.h"
#include "ui/core/UiButtonBase.h"
#include "ui/core/UiManager.h"
#include "ui/core/UiSlideBase.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace sdlviewer
{
    ControlManager::ControlManager(FileListManager *fileListManager)
        : fileListManager_(fileListManager)
    {
    }

    void ControlManager::PostTimeFromWorker(double positionSec)
    {
        sharedTime_.store(positionSec, std::memory_order_relaxed);
    }

    void ControlManager::PostTaskFromWorker(std::function<void()> task)
    {
        if (!task)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(workerTaskMutex_);
        workerTasks_.push_back(std::move(task));
    }

    void ControlManager::PostTaskFromMedia(std::function<void()> task)
    {
        if (!task)
            return;
        const std::uint64_t revision = mediaEventRevision_.load();
        PostTaskFromWorker([this, revision, task = std::move(task)]
        {
            if (revision == mediaEventRevision_.load() && HasActiveMedia())
                task();
        });
    }

    void ControlManager::Update()
    {
        std::deque<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(workerTaskMutex_);
            tasks.swap(workerTasks_);
        }

        for (auto &task : tasks)
        {
            task();
        }

        if (!seekPending_ && HasActiveMedia())
        {
            const double t = sharedTime_.load(std::memory_order_relaxed);
            if (t != playbackState_.position)
            {
                playbackState_.position = t;
            }
        }
    }

    void ControlManager::TogglePlayback()
    {
        if (playbackState_.playState != PlayState::Playing &&
            playbackState_.playState != PlayState::Pause)
        {
            return;
        }

        if (mpvControls_.pauseAll)
        {
            mpvControls_.pauseAll();
        }

        SetPlayState(
            playbackState_.playState == PlayState::Playing
                ? PlayState::Pause
                : PlayState::Playing);
    }

    void ControlManager::StopCallBack()
    {
        switch (playbackState_.playState)
        {
        case PlayState::NotLoad:
        case PlayState::StopAll:
        case PlayState::Stopping:
            return;
        default:
            break;
        }

        SetPlayState(PlayState::NotLoad);
        if (mpvControls_.stopAll)
        {
            mpvControls_.stopAll();
        }
    }

    void ControlManager::SetPlayState(PlayState state)
    {
        const bool shouldReset =
            state == PlayState::NotLoad ||
            state == PlayState::Stopping ||
            state == PlayState::StopAll;

        if (shouldReset)
        {
            const bool changed =
                playbackState_.playState != state ||
                playbackState_.position != 0.0 ||
                playbackState_.duration != 0.0 ||
                !currentFileName_.empty();

            currentFileName_.clear();

            if (!changed)
            {
                return;
            }

            playbackState_.playState = state;
            playbackState_.mediaRevision = ++mediaEventRevision_;
            playbackState_.position = 0.0;
            playbackState_.duration = 0.0;
            sharedTime_.store(0.0, std::memory_order_relaxed);
            seekPending_ = false;
            NotifyPlaybackChanged(PlaybackChange::Reset);
            return;
        }

        if (playbackState_.playState == state)
        {
            return;
        }

        playbackState_.playState = state;
        NotifyPlaybackChanged(PlaybackChange::PlayStateChanged);
    }

    void ControlManager::SetPlaybackSpeed(PlaybackSpeed speed)
    {
        if (playbackState_.speed == speed)
        {
            return;
        }

        if (!mpvControls_.setPlaybackSpeed ||
            !mpvControls_.setPlaybackSpeed(ToPlaybackRate(speed)))
        {
            return;
        }

        playbackState_.speed = speed;
        NotifyPlaybackChanged(PlaybackChange::SpeedChanged);
    }

    void ControlManager::OnMediaChanged()
    {
        currentFileName_.clear();
        playbackState_.mediaRevision = ++mediaEventRevision_;
        playbackState_.playState = PlayState::FileLoad;
        playbackState_.position = 0.0;
        sharedTime_.store(0.0, std::memory_order_relaxed);
        playbackState_.duration = 0.0;
        sharedTime_.store(0.0, std::memory_order_relaxed);
        seekPending_ = false;
        NotifyPlaybackChanged(PlaybackChange::MediaChanged);
    }

    void ControlManager::StopPlayback()
    {
        SetPlayState(PlayState::StopAll);
    }

    void ControlManager::RequestSeek(float ratio)
    {
        switch (playbackState_.playState)
        {
        case PlayState::Pause:
        case PlayState::Playing:
            break;
        default:
            return;
        }

        ratio = std::clamp(ratio, 0.0f, 1.0f);

        if (mpvControls_.seek)
        {
            mpvControls_.seek(ratio);
        }

        playbackState_.position = playbackState_.duration * static_cast<double>(ratio);
        sharedTime_.store(playbackState_.position, std::memory_order_relaxed);
        seekPending_ = true;
    }

    void ControlManager::OnSeekCompleted()
    {
        seekPending_ = false;
    }

    void ControlManager::SetDurationFromMedia(double durationSec)
    {
        if (!HasActiveMedia() || durationSec < 0.0)
        {
            durationSec = 0.0;
        }

        if (playbackState_.duration == durationSec)
        {
            return;
        }

        playbackState_.duration = durationSec;
    }

    float ControlManager::GetProgressRatio() const
    {
        if (playbackState_.duration <= 0.0)
        {
            return 0.0f;
        }

        const double ratio = playbackState_.position / playbackState_.duration;
        return static_cast<float>(std::clamp(ratio, 0.0, 1.0));
    }

    void ControlManager::FilelistClickAndPlay(int num)
    {
        if (playbackState_.playState == PlayState::NotLoad ||
            playbackState_.playState == PlayState::Playing ||
            playbackState_.playState == PlayState::Pause)
        {
            PlayFile(num);
        }
    }

    bool ControlManager::PlayFile(int index)
    {
        if (!fileListManager_ || !mpvControls_.setVideo ||
            index < 0 || index >= fileListManager_->GetFileCount())
        {
            return false;
        }

        const std::string path = fileListManager_->GetVideoPath(index);
        OnMediaChanged();
        if (!fileListManager_->LoadSensorData(index) ||
            !mpvControls_.setVideo(path))
        {
            if (mpvControls_.stopAll)
                mpvControls_.stopAll();
            SetPlayState(PlayState::NotLoad);
            return false;
        }

        currentFileName_ = std::filesystem::u8path(path).filename().u8string();
        if (!audioInitialized_)
        {
            SetVolume(1.0f);
        }
        return true;
    }

    bool ControlManager::HasActiveMedia() const
    {
        return playbackState_.playState == PlayState::FileLoad ||
               playbackState_.playState == PlayState::PlayReady ||
               playbackState_.playState == PlayState::Playing ||
               playbackState_.playState == PlayState::Pause;
    }

    void ControlManager::DeleteCheckedFiles()
    {
        if (!fileListManager_)
            return;
        const std::vector<int> indices = fileListManager_->GetCheckedIndices();
        if (indices.empty())
            return;

        for (int index : indices)
        {
            if (mpvControls_.isFileLoaded && mpvControls_.isFileLoaded(fileListManager_->GetVideoPath(index)))
            {
                SetPlayState(PlayState::NotLoad);
                if (mpvControls_.stopAll)
                    mpvControls_.stopAll();
                fileListManager_->ClearCurrentFile();
                break;
            }
        }
        fileListManager_->RemoveFiles(indices);
    }

    void ControlManager::PlayNextFile()
    {
        if (fileListManager_)
            FilelistClickAndPlay(fileListManager_->GetAdjacentFileIndex(1, looping_));
    }

    void ControlManager::PlayPreviousFile()
    {
        if (fileListManager_)
            FilelistClickAndPlay(fileListManager_->GetAdjacentFileIndex(-1, looping_));
    }

    void ControlManager::OnPlaybackEnded()
    {
        if (playbackState_.playState != PlayState::Playing &&
            playbackState_.playState != PlayState::Pause)
        {
            return;
        }

        const int next = fileListManager_
                             ? fileListManager_->GetAdjacentFileIndex(1, looping_)
                             : -1;
        if (next >= 0 && PlayFile(next))
        {
            return;
        }

        if (mpvControls_.stopAll)
            mpvControls_.stopAll();
        SetPlayState(PlayState::NotLoad);
    }

    void ControlManager::SetVolume(float ratio)
    {
        if (!std::isfinite(ratio))
            return;

        const float volume = std::clamp(ratio, 0.0f, 1.0f) * 100.0f;
        const bool muted = volume == 0.0f;
        if (!mpvControls_.setAudio || !mpvControls_.setAudio(volume, muted))
            return;

        volume_ = volume;
        muted_ = muted;
        audioInitialized_ = true;
        if (volume > 0.0f)
            previousVolume_ = volume;
    }

    void ControlManager::ToggleMute()
    {
        if (audioInitialized_)
            SetVolume(muted_ ? previousVolume_ / 100.0f : 0.0f);
    }

    void ControlManager::StepPlayback(int direction)
    {
        if ((direction != -1 && direction != 1) ||
            (playbackState_.playState != PlayState::Playing &&
             playbackState_.playState != PlayState::Pause))
        {
            return;
        }

        if (frameMode_)
        {
            if (mpvControls_.stepFrame && mpvControls_.stepFrame(direction))
                SetPlayState(PlayState::Pause);
            return;
        }

        if (playbackState_.duration <= 0.0)
            return;

        const double target = std::clamp(
            playbackState_.position + direction, 0.0, playbackState_.duration);
        if (mpvControls_.seekSeconds && mpvControls_.seekSeconds(target))
        {
            playbackState_.position = target;
            sharedTime_.store(target, std::memory_order_relaxed);
            seekPending_ = true;
        }
    }

    ControlManager::SubscriptionId ControlManager::SubscribePlaybackChanged(PlaybackCallback callback)
    {
        if (!callback)
        {
            return 0;
        }

        const SubscriptionId id = nextSubscriptionId_++;
        playbackCallbacks_.emplace(id, std::move(callback));
        return id;
    }

    void ControlManager::UnsubscribePlaybackChanged(SubscriptionId id)
    {
        if (id == 0)
        {
            return;
        }

        playbackCallbacks_.erase(id);
    }

    std::string ControlManager::FormatTime(double seconds)
    {
        const int mainPart = static_cast<int>(seconds);
        const int subPart = static_cast<int>(std::abs(seconds - mainPart) * 100);

        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << mainPart << ":"
            << std::setfill('0') << std::setw(2) << subPart;

        return oss.str();
    }

    void ControlManager::NotifyPlaybackChanged(PlaybackChange change)
    {
        pendingNotifications_.push_back(PlaybackChangedEvent{playbackState_, change});
        DispatchPendingNotifications();
    }

    void ControlManager::DispatchPendingNotifications()
    {
        if (isNotifying_)
        {
            return;
        }

        isNotifying_ = true;

        while (!pendingNotifications_.empty())
        {
            const PlaybackChangedEvent event = pendingNotifications_.front();
            pendingNotifications_.pop_front();

            std::vector<SubscriptionId> ids;
            ids.reserve(playbackCallbacks_.size());
            for (const auto &entry : playbackCallbacks_)
            {
                ids.push_back(entry.first);
            }

            for (SubscriptionId id : ids)
            {
                auto it = playbackCallbacks_.find(id);
                if (it == playbackCallbacks_.end())
                {
                    continue;
                }

                PlaybackCallback callback = it->second;
                if (callback)
                {
                    callback(event);
                }
            }
        }

        isNotifying_ = false;
    }

    bool ControlManager::BindButton(
        const std::string &name,
        std::function<void()> onClick,
        std::function<bool()> isSelected)
    {
        if (!uiManager_)
        {
            return false;
        }

        UiButtonBase *button = uiManager_->FindWidgetAs<UiButtonBase>(name);
        if (button == nullptr)
        {
            std::cout << name << " Not Found\n";
            return false;
        }

        button->SetOnClick(std::move(onClick));

        if (isSelected)
        {
            button->SetSelectedProvider(std::move(isSelected));
        }
        else
        {
            button->ClearSelectedProvider();
        }

        return true;
    }

    bool ControlManager::BindSlideFunc(
        const std::string &name,
        std::function<void(float value)> onRelease,
        std::function<float()> valueProvider,
        std::function<bool()> moveEnable)
    {
        if (!uiManager_)
        {
            return false;
        }

        UiSlideBase *slide = uiManager_->FindWidgetAs<UiSlideBase>(name);
        if (slide == nullptr)
        {
            std::cout << name << " Not Found\n";
            return false;
        }

        slide->SetReleaseCallback(std::move(onRelease));
        slide->SetValueProvider(std::move(valueProvider));
        slide->SetMoveEnableProvider(std::move(moveEnable));

        return true;
    }

    bool ControlManager::BindFileListFunc(
        const std::string &name,
        std::function<void(int index)> onLoad,
        std::function<bool()> isSelectable)
    {
        if (!uiManager_)
        {
            return false;
        }

        CustomFileList *fileList = uiManager_->FindWidgetAs<CustomFileList>(name);
        if (fileList == nullptr)
        {
            std::cout << name << " Not Found\n";
            return false;
        }

        fileList->SetDoubleClickCallback(std::move(onLoad));
        fileList->SetSelectedProvider(std::move(isSelectable));

        return true;
    }

    bool ControlManager::BindTextFunc(
        const std::string &name,
        std::function<std::string()> textProvider)
    {
        if (!uiManager_)
        {
            return false;
        }

        TextLabel *textLabel = uiManager_->FindWidgetAs<TextLabel>(name);
        if (textLabel == nullptr)
        {
            std::cout << name << " Not Found\n";
            return false;
        }

        textLabel->SetTextProvider(std::move(textProvider));

        return true;
    }

} // namespace sdlviewer
