#pragma once

#include "media/MpvEventThread.h"
#include "media/MpvPlayer.h"
#include "render/RenderTypes.h"

#include <memory>
#include <array>
#include <string>
#include <vector>

namespace sdlviewer
{
    class Application;

    class MpvPlayerManager final
    {
    public:
        explicit MpvPlayerManager(Application &application);
        ~MpvPlayerManager();

        MpvPlayerManager(const MpvPlayerManager &) = delete;
        MpvPlayerManager &operator=(const MpvPlayerManager &) = delete;

        // callbacks는 해당 채널의 mpv 이벤트 스레드에서 실행된다.
        RenderStatus AddChannel(
            int channelIndex,
            RectF destination,
            MpvPlaybackCallbacks callbacks = {});

        void RemoveChannel(int channelIndex);

        RenderStatus OpenFile(
            int channelIndex,
            const std::string &utf8Path,
            int videoTrackId);

        RenderStatus SetVideo(const std::string &utf8Path);
        bool IsFileLoaded(const std::string &utf8Path) const;

        void PauseAll();
        void StopAll();
        void SeekAll(float normalizedPosition);
        RenderStatus SetPlaybackSpeed(double rate);
        RenderStatus SetAudio(double volume, bool muted);
        RenderStatus SeekSecondsAll(double positionSec);
        RenderStatus StepFrameAll(int direction);

        void SetDestination(int channelIndex, RectF destination);
        void SetVisible(int channelIndex, bool visible);

        void SetHomeDestination(int slotIndex, RectF destination);
        RenderStatus SetGridLayout(
            int layoutCount,
            RectF viewport,
            float pixelDensity);
        RenderStatus SwapChannels(int channelA, int channelB);
        bool IsTrackChangePending() const;
        RenderStatus SetFullscreen(bool fullscreen);
        int HitTestChannel(float x, float y) const;
        RenderStatus SelectChannel(int channelIndex);
        RenderStatus ToggleSelectedFlip(VideoFlag flag);
        int GetSelectedChannel() const { return selectedChannel_; }
        bool GetSelectedFlip(VideoFlag flag) const;
        bool IsVideoFlagPending() const;

    private:
        struct Channel
        {
            struct FlagState
            {
                bool value = false;
                bool requested = false;
                bool reset = false;
                std::uint64_t request = 0;
            };

            VideoId id = 0;
            std::unique_ptr<MpvPlayer> player;
            std::unique_ptr<MpvEventThread> eventThread;
            RectF destination{};
            PixelSize surfaceSize{};
            int currentTrackId = 0;
            std::string loadedPath;
            int pendingTrackId = 0;
            std::uint64_t pendingTrackRequest = 0;
            int displaySlot = -1;
            bool visible = false;
            std::array<FlagState, 3> flags{};
        };

        bool PushCellState(const Channel &channel);
        bool IsOutputVisible(const Channel &channel) const;
        void OnTrackReply(int channelIndex, std::uint64_t requestId, int error);
        void OnVideoFlagReply(int channelIndex, std::uint64_t requestId, int error);
        RenderStatus RequestVideoFlag(Channel &channel, VideoFlag flag, bool enabled);
        void ResetVideoFlags();
        Channel *FindChannel(int channelIndex) noexcept;

        Application &application_;
        std::vector<Channel> channels_;
        std::vector<RectF> homeDestinations_;
        RectF layoutViewport_{};
        int layoutCount_ = 1;
        float pixelDensity_ = 1.0f;
        std::uint64_t nextPropertyRequest_ = 1;
        int selectedChannel_ = -1;
        int selectionTarget_ = -1;
        bool videoFlagResetFailed_ = false;
        bool fullscreen_ = false;
        bool isMp4_ = false;
    };

} // namespace sdlviewer
