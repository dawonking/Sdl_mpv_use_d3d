#pragma once

#include "PlaybackTypes.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace sdlviewer
{
    class FileListManager;
    class UiManager;
    struct MpvControlCallbacks
    {
        std::function<void()> pauseAll;
        std::function<void()> stopAll;
        std::function<void(float ratio)> seek;
        std::function<bool(const std::string &path)> setVideo;
        std::function<bool(const std::string &path)> isFileLoaded;
        std::function<bool(double rate)> setPlaybackSpeed;
        std::function<bool(double volume, bool muted)> setAudio;
        std::function<bool(double positionSec)> seekSeconds;
        std::function<bool(int direction)> stepFrame;
    };

    class ControlManager
    {
    public:
        using SubscriptionId = std::uint64_t;
        using PlaybackCallback = std::function<void(const PlaybackChangedEvent &)>;

        explicit ControlManager(FileListManager *fileListManager = nullptr);

        void SetMpvControlCallbacks(MpvControlCallbacks callbacks) { mpvControls_ = std::move(callbacks); }
        void SetFileListManager(FileListManager *fileListManager) { fileListManager_ = fileListManager; }
        void SetUiManager(UiManager *uiManager) { uiManager_ = uiManager; }
        void Update();

        void TogglePlayback();
        void StopCallBack();
        void SetPlayState(PlayState state);
        void SetPlaybackSpeed(PlaybackSpeed speed);
        void OnMediaChanged();
        void StopPlayback();

        void FilelistClickAndPlay(int num);
        void PlayNextFile();
        void PlayPreviousFile();
        void OnPlaybackEnded();
        void DeleteCheckedFiles();
        bool HasActiveMedia() const;

        void SetVolume(float ratio);
        void ToggleMute();
        float GetVolumeRatio() const { return volume_ / 100.0f; }
        bool IsMuted() const { return muted_; }
        bool IsAudioInitialized() const { return audioInitialized_; }

        void ToggleLooping() { looping_ = !looping_; }
        bool IsLooping() const { return looping_; }
        void ToggleSeekMode() { frameMode_ = !frameMode_; }
        bool IsFrameMode() const { return frameMode_; }
        void StepPlayback(int direction);

        void RequestSeek(float ratio);
        void OnSeekCompleted();
        void SetDurationFromMedia(double durationSec);

        const PlaybackState &GetPlaybackState() const { return playbackState_; }
        PlayState GetPlayState() const { return playbackState_.playState; }
        PlaybackSpeed GetPlaybackSpeed() const { return playbackState_.speed; }
        double GetPositionSec() const { return playbackState_.position; }
        double GetDurationSec() const { return playbackState_.duration; }
        const std::string &GetCurrentFileName() const { return currentFileName_; }
        float GetProgressRatio() const;

        SubscriptionId SubscribePlaybackChanged(PlaybackCallback callback);
        void UnsubscribePlaybackChanged(SubscriptionId id);

        bool IsNotifying() const { return isNotifying_; }

        void PostTimeFromWorker(double positionSec);
        void PostTaskFromWorker(std::function<void()> task);
        void PostTaskFromMedia(std::function<void()> task);

        static std::string FormatTime(double seconds);
        
        bool BindButton(
            const std::string &name,
            std::function<void()> onClick,
            std::function<bool()> isSelected = nullptr);

        bool BindSlideFunc(
            const std::string &name,
            std::function<void(float value)> onRelease,
            std::function<float()> valueProvider,
            std::function<bool()> moveEnable = nullptr);

        bool BindFileListFunc(
            const std::string &name,
            std::function<void(int index)> onLoad,
            std::function<bool()> isSelectable = nullptr);

        bool BindTextFunc(
            const std::string &name,
            std::function<std::string()> textProvider);

    private:
        void NotifyPlaybackChanged(PlaybackChange change);
        void DispatchPendingNotifications();
        bool PlayFile(int index);

        FileListManager *fileListManager_ = nullptr;
        UiManager *uiManager_ = nullptr;
        MpvControlCallbacks mpvControls_;

        PlaybackState playbackState_;
        std::string currentFileName_;

        SubscriptionId nextSubscriptionId_ = 1;
        std::unordered_map<SubscriptionId, PlaybackCallback> playbackCallbacks_;
        std::deque<PlaybackChangedEvent> pendingNotifications_;
        bool isNotifying_ = false;

        std::atomic<double> sharedTime_{0.0};
        std::atomic<std::uint64_t> mediaEventRevision_{0};

        std::mutex workerTaskMutex_;
        std::deque<std::function<void()>> workerTasks_;
        bool seekPending_ = false;
        float volume_ = 0.0f;
        float previousVolume_ = 100.0f;
        bool muted_ = false;
        bool audioInitialized_ = false;
        bool looping_ = false;
        bool frameMode_ = false;
    };
} // namespace sdlviewer
