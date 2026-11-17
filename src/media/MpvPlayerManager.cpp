#include "media/MpvPlayerManager.h"
#include "media/VideoFileRules.h"

#include "app/Application.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <future>
#include <utility>

namespace sdlviewer
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr PixelSize kPrimarySurfaceSize{1920, 1080};
        constexpr PixelSize kSecondarySurfaceSize{130, 70};

        constexpr std::array<const char *, 6> kMp4Suffixes{
            "_F_N", "_F_L", "_I_N", "_I_L", "_R_N", "_R_L"};

        struct GridDimensions
        {
            int columns = 0;
            int rows = 0;
        };

        GridDimensions GridDimensionsForLayout(int layoutCount)
        {
            switch (layoutCount)
            {
            case 1:
                return {1, 1};
            case 2:
                return {2, 1};
            case 4:
                return {2, 2};
            case 6:
                return {3, 2};
            default:
                return {0, 0};
            }
        }

    } // namespace

    MpvPlayerManager::MpvPlayerManager(Application &application)
        : application_(application)
    {
    }

    MpvPlayerManager::~MpvPlayerManager()
    {
        for (std::size_t i = 0; i < channels_.size(); ++i)
        {
            RemoveChannel(static_cast<int>(i));
        }
    }

    MpvPlayerManager::Channel *
    MpvPlayerManager::FindChannel(int channelIndex) noexcept
    {
        if (channelIndex < 0 ||
            static_cast<std::size_t>(channelIndex) >= channels_.size())
        {
            return nullptr;
        }

        Channel &channel = channels_[static_cast<std::size_t>(channelIndex)];
        return channel.player ? &channel : nullptr;
    }

    RenderStatus MpvPlayerManager::AddChannel(
        int channelIndex,
        RectF destination,
        MpvPlaybackCallbacks callbacks)
    {
        if (channelIndex < 0)
        {
            return RenderStatus::Error(
                "Channel index must not be negative.");
        }

        if (static_cast<std::size_t>(channelIndex) >= channels_.size())
        {
            channels_.resize(static_cast<std::size_t>(channelIndex) + 1);
            homeDestinations_.resize(channels_.size());
        }

        Channel &channel = channels_[static_cast<std::size_t>(channelIndex)];
        if (channel.player)
        {
            return RenderStatus::Error("Channel is already in use.");
        }

        auto player = std::make_unique<MpvPlayer>(channelIndex);
        const RenderStatus initStatus = player->Initialize();
        if (!initStatus)
        {
            return initStatus;
        }

        const auto onPropertyReply = std::move(callbacks.onPropertyReply);
        callbacks.onPropertyReply =
            [this, channelIndex, onPropertyReply](std::uint64_t requestId, int error)
        {
            if (onPropertyReply)
                onPropertyReply(requestId, error);
            if (requestId != 0)
            {
                application_.PostPlaybackTask(
                    [this, channelIndex, requestId, error]
                    {
                        OnTrackReply(channelIndex, requestId, error);
                        OnVideoFlagReply(channelIndex, requestId, error);
                    });
            }
        };

        auto eventThread = std::make_unique<MpvEventThread>(
            player->Handle(),
            std::move(callbacks));

        channel.id = static_cast<VideoId>(channelIndex + 1);
        channel.player = std::move(player);
        channel.eventThread = std::move(eventThread);
        channel.destination = destination;
        homeDestinations_[channelIndex] = destination;
        channel.surfaceSize = channel.player->ShouldBlockForTargetTime()
                                  ? kPrimarySurfaceSize
                                  : kSecondarySurfaceSize;
        channel.visible = false;
        channel.currentTrackId = 0;
        channel.pendingTrackId = 0;
        channel.pendingTrackRequest = 0;
        channel.displaySlot = channelIndex;
        channel.flags = {};

        if (!PushCellState(channel))
        {
            channel.eventThread.reset();
            channel.player.reset();
            channel.id = 0;

            return RenderStatus::Error(
                "Failed to register the video cell with the render thread.");
        }

        channel.eventThread->Start();

        return RenderStatus::Ok();
    }

    bool MpvPlayerManager::PushCellState(const Channel &channel)
    {
        VideoCellState cell;
        cell.id = channel.id;
        cell.player = channel.player->Handle();
        cell.surfaceSize = channel.surfaceSize;
        cell.destination = channel.destination;
        cell.zOrder = static_cast<int>(channel.id) - 1;
        cell.visible = IsOutputVisible(channel);
        cell.keepAspect = true;
        cell.blockForTargetTime = channel.player->ShouldBlockForTargetTime();

        return application_.SetVideoCell(cell);
    }

    bool MpvPlayerManager::IsOutputVisible(const Channel &channel) const
    {
        return channel.player && channel.visible && channel.currentTrackId > 0 &&
               (!fullscreen_ || channel.displaySlot < layoutCount_);
    }

    void MpvPlayerManager::RemoveChannel(int channelIndex)
    {
        Channel *channel = FindChannel(channelIndex);
        if (!channel)
        {
            return;
        }

        if (selectedChannel_ == channelIndex || selectionTarget_ == channelIndex)
            ResetVideoFlags();

        std::future<void> released =
            application_.RemoveVideoCell(channel->id);
        released.wait();

        channel->eventThread.reset();
        channel->player.reset();
        channel->id = 0;
        channel->destination = RectF{};
        channel->surfaceSize = PixelSize{};
        channel->currentTrackId = 0;
        channel->loadedPath.clear();
        channel->pendingTrackId = 0;
        channel->pendingTrackRequest = 0;
        channel->displaySlot = -1;
        channel->visible = false;
        channel->flags = {};
    }

    RenderStatus MpvPlayerManager::OpenFile(
        int channelIndex,
        const std::string &utf8Path,
        int videoTrackId)
    {
        Channel *channel = FindChannel(channelIndex);
        if (!channel)
        {
            return RenderStatus::Error("Unknown channel index.");
        }

        const RenderStatus status = channel->player->OpenFile(utf8Path, videoTrackId);
        if (status)
        {
            channel->currentTrackId = videoTrackId;
            channel->loadedPath = utf8Path;
            channel->pendingTrackId = 0;
            channel->pendingTrackRequest = 0;
        }
        return status;
    }

    bool MpvPlayerManager::IsFileLoaded(const std::string &utf8Path) const
    {
        if (utf8Path.empty())
            return false;
        const fs::path path = fs::u8path(utf8Path).lexically_normal();
        for (const Channel &channel : channels_)
        {
            if (!channel.player || channel.currentTrackId <= 0 || channel.loadedPath.empty())
                continue;
            const fs::path loaded = fs::u8path(channel.loadedPath).lexically_normal();
            std::error_code error;
            if (loaded == path || fs::equivalent(loaded, path, error))
                return true;
        }
        return false;
    }

    RenderStatus MpvPlayerManager::SetVideo(const std::string &utf8Path)
    {
        if (utf8Path.empty() ||
            utf8Path.find('\0') != std::string::npos)
        {
            return RenderStatus::Error("Video path is invalid.");
        }

        bool isMp4 = false;
        std::array<std::string, 6> paths{};

        try
        {
            const fs::path source = fs::absolute(fs::u8path(utf8Path));

            if (!fs::is_regular_file(source))
            {
                return RenderStatus::Error(
                    "Video file does not exist: " + utf8Path);
            }

            const VideoFileInfo info = InspectVideoFileName(source);
            if (info.kind == VideoFileKind::Unsupported || info.kind == VideoFileKind::IgnoredMp4)
                return RenderStatus::Error("Unsupported video filename: " + source.filename().u8string());

            isMp4 = info.extension == ".mp4";
            if (info.kind == VideoFileKind::NumberedMp4)
            {
                if (info.channelIndex != 0)
                    return RenderStatus::Error("Select the _1.mp4 representative file.");

                for (const auto &entry : fs::directory_iterator(source.parent_path()))
                {
                    if (!entry.is_regular_file())
                        continue;
                    const VideoFileInfo member = InspectVideoFileName(entry.path());
                    if (member.kind != VideoFileKind::NumberedMp4 || member.groupPrefix != info.groupPrefix)
                        continue;
                    std::string &slot = paths[member.channelIndex];
                    if (!slot.empty())
                        return RenderStatus::Error("Duplicate numbered video channel: " + entry.path().u8string());
                    slot = entry.path().u8string();
                }
                if (paths[0].empty())
                    return RenderStatus::Error("The _1.mp4 representative file is missing.");
            }
            else if (info.kind == VideoFileKind::LegacyMp4)
            {
                std::size_t index = 0;
                for (const char *suffix : kMp4Suffixes)
                {
                    const fs::path candidate = source.parent_path() /
                        fs::u8path(info.groupPrefix + suffix + source.extension().u8string());
                    if (fs::is_regular_file(candidate))
                        paths[index++] = candidate.u8string();
                }
            }
            else
            {
                for (int index = 0; index < info.trackCount; ++index)
                    paths[index] = source.u8string();
            }
        }
        catch (const fs::filesystem_error &error)
        {
            return RenderStatus::Error(
                std::string("Failed to inspect video files: ") +
                error.what());
        }

        if (std::all_of(paths.begin(), paths.end(), [](const std::string &path) { return path.empty(); }))
        {
            return RenderStatus::Error("No video files were found.");
        }

        for (std::size_t i = 0; i < paths.size(); ++i)
        {
            if (paths[i].empty())
                continue;
            if (!FindChannel(static_cast<int>(i)))
            {
                return RenderStatus::Error(
                    "Required channel is not initialized: " +
                    std::to_string(i));
            }
        }

        isMp4_ = isMp4;
        StopAll();

        for (std::size_t i = 0; i < channels_.size(); ++i)
            channels_[i].displaySlot = static_cast<int>(i);

        if (layoutViewport_.IsValid())
        {
            const RenderStatus status = SetGridLayout(layoutCount_, layoutViewport_, pixelDensity_);
            if (!status)
                return status;
        }

        for (std::size_t i = 0; i < paths.size(); ++i)
        {
            if (paths[i].empty())
                continue;
            const int videoTrackId =
                isMp4 ? 1 : static_cast<int>(i) + 1;

            const RenderStatus status = OpenFile(
                static_cast<int>(i),
                paths[i],
                videoTrackId);

            if (!status)
            {
                StopAll();

                return RenderStatus::Error(
                    "Failed to queue video for channel " +
                    std::to_string(i) + ": " + status.Message());
            }
        }

        return RenderStatus::Ok();
    }

    void MpvPlayerManager::PauseAll()
    {
        for (Channel &channel : channels_)
        {
            if (channel.player)
                channel.player->TogglePause();
        }
    }

    void MpvPlayerManager::StopAll()
    {
        ResetVideoFlags();
        for (Channel &channel : channels_)
        {
            if (!channel.player)
            {
                continue;
            }

            channel.player->Stop();
            channel.visible = false;
            channel.currentTrackId = 0;
            channel.loadedPath.clear();
            channel.pendingTrackId = 0;
            channel.pendingTrackRequest = 0;
            PushCellState(channel);
        }
    }

    void MpvPlayerManager::SeekAll(float normalizedPosition)
    {
        for (Channel &channel : channels_)
        {
            if (channel.player)
                channel.player->Seek(normalizedPosition);
        }
    }

    RenderStatus MpvPlayerManager::SetPlaybackSpeed(double rate)
    {
        for (Channel &channel : channels_)
        {
            if (!channel.player)
                continue;

            const RenderStatus status = channel.player->SetPlaybackSpeed(rate);
            if (!status)
                return status;
        }
        return RenderStatus::Ok();
    }

    RenderStatus MpvPlayerManager::SetAudio(double volume, bool muted)
    {
        Channel *channel = FindChannel(0);
        if (!channel)
        {
            return RenderStatus::Error("Primary audio channel is not initialized.");
        }
        return channel->player->SetAudio(volume, muted);
    }

    RenderStatus MpvPlayerManager::SeekSecondsAll(double positionSec)
    {
        for (Channel &channel : channels_)
        {
            if (!channel.player || !channel.visible)
                continue;

            const RenderStatus status = channel.player->SeekSeconds(positionSec);
            if (!status)
                return status;
        }
        return RenderStatus::Ok();
    }

    RenderStatus MpvPlayerManager::StepFrameAll(int direction)
    {
        for (Channel &channel : channels_)
        {
            if (!channel.player || !channel.visible)
                continue;

            const RenderStatus status = channel.player->StepFrame(direction);
            if (!status)
                return status;
        }
        return RenderStatus::Ok();
    }

    void MpvPlayerManager::SetDestination(
        int channelIndex,
        RectF destination)
    {
        Channel *channel = FindChannel(channelIndex);
        if (!channel)
            return;

        channel->destination = destination;
        PushCellState(*channel);
    }

    void MpvPlayerManager::SetVisible(int channelIndex, bool visible)
    {
        Channel *channel = FindChannel(channelIndex);
        if (!channel)
            return;

        channel->visible = visible;
        PushCellState(*channel);
    }

    void MpvPlayerManager::SetHomeDestination(
        int slotIndex,
        RectF destination)
    {
        if (slotIndex < 0 || static_cast<std::size_t>(slotIndex) >= homeDestinations_.size())
            return;

        homeDestinations_[slotIndex] = destination;
    }

    RenderStatus MpvPlayerManager::SetGridLayout(int layoutCount, RectF viewport, float pixelDensity)
    {
        const GridDimensions grid = GridDimensionsForLayout(layoutCount);
        if (grid.columns == 0)
        {
            return RenderStatus::Error(
                "Unsupported grid layout count: " +
                std::to_string(layoutCount));
        }

        if (!viewport.IsValid())
        {
            return RenderStatus::Error(
                "Grid layout viewport is invalid.");
        }

        if (!std::isfinite(pixelDensity) || pixelDensity <= 0.0f)
        {
            return RenderStatus::Error(
                "Grid layout pixel density is invalid.");
        }

        layoutCount_ = layoutCount;
        layoutViewport_ = viewport;
        pixelDensity_ = pixelDensity;

        for (std::size_t i = 0; i < channels_.size(); ++i)
        {
            Channel &channel = channels_[i];
            if (!channel.player)
                continue;

            const int slot = channel.displaySlot;
            if (slot < layoutCount)
            {
                const int col = slot % grid.columns;
                const int row = slot / grid.columns;

                const float left =
                    viewport.x + viewport.width * col / grid.columns;
                const float right =
                    viewport.x + viewport.width * (col + 1) / grid.columns;
                const float top =
                    viewport.y + viewport.height * row / grid.rows;
                const float bottom =
                    viewport.y + viewport.height * (row + 1) / grid.rows;

                channel.destination =
                    RectF{
                        left,
                        top,
                        right - left,
                        bottom - top};
            }
            else
            {
                channel.destination = homeDestinations_[slot];
            }

            channel.surfaceSize =
                PixelSize{
                    static_cast<int>(
                        std::ceil(
                            channel.destination.width *
                            pixelDensity)),
                    static_cast<int>(
                        std::ceil(
                            channel.destination.height *
                            pixelDensity))};

            if (!PushCellState(channel))
                return RenderStatus::Error("Failed to update video layout.");
        }

        return RenderStatus::Ok();
    }

    RenderStatus MpvPlayerManager::SwapChannels(int channelA, int channelB)
    {
        if (IsVideoFlagPending() || videoFlagResetFailed_)
            return RenderStatus::Error("Video selection/flip update is not ready.");
        if (channelA < 0 || channelB < 0)
        {
            return RenderStatus::Error("Channel index must not be negative.");
        }

        Channel *first = FindChannel(channelA);
        Channel *second = FindChannel(channelB);
        if (!first || !second)
        {
            return RenderStatus::Error("Unknown channel index.");
        }

        if (!IsOutputVisible(*first) || !IsOutputVisible(*second))
        {
            return RenderStatus::Error("Only visible, loaded channels can be swapped.");
        }
        if (channelA == channelB)
            return RenderStatus::Ok();
        if (first->pendingTrackRequest != 0 || second->pendingTrackRequest != 0)
            return RenderStatus::Error("Video track change is still pending.");

        if (isMp4_)
        {
            std::swap(first->destination, second->destination);
            std::swap(first->surfaceSize, second->surfaceSize);
            std::swap(first->displaySlot, second->displaySlot);

            const bool firstUpdated = PushCellState(*first);
            const bool secondUpdated = PushCellState(*second);
            if (!firstUpdated || !secondUpdated)
            {
                std::swap(first->destination, second->destination);
                std::swap(first->surfaceSize, second->surfaceSize);
                std::swap(first->displaySlot, second->displaySlot);
                const bool firstRestored = PushCellState(*first);
                const bool secondRestored = PushCellState(*second);
                return RenderStatus::Error(firstRestored && secondRestored
                                               ? "Failed to swap video positions; positions restored."
                                               : "Failed to swap and restore video positions.");
            }
        }
        else
        {
            const int firstTrack = first->currentTrackId;
            const int secondTrack = second->currentTrackId;
            const std::uint64_t firstRequest = nextPropertyRequest_++;
            const RenderStatus firstStatus = first->player->SetVideoTrack(secondTrack, firstRequest);
            if (!firstStatus)
                return firstStatus;

            // Keep the confirmed track until its reply; do not overlap requests on a channel.
            first->pendingTrackId = secondTrack;
            first->pendingTrackRequest = firstRequest;
            const std::uint64_t secondRequest = nextPropertyRequest_++;
            const RenderStatus secondStatus = second->player->SetVideoTrack(firstTrack, secondRequest);
            if (!secondStatus)
            {
                StopAll();
                application_.GetControlManager().SetPlayState(PlayState::NotLoad);
                return RenderStatus::Error(secondStatus.Message() + "; playback stopped after partial track request.");
            }
            second->pendingTrackId = firstTrack;
            second->pendingTrackRequest = secondRequest;
        }

        return RenderStatus::Ok();
    }

    RenderStatus MpvPlayerManager::RequestVideoFlag(Channel &channel, VideoFlag flag, bool enabled)
    {
        const std::uint64_t request = nextPropertyRequest_++;
        const RenderStatus status = channel.player->SetVideoFlag(flag, enabled, request);
        if (status)
        {
            auto &state = channel.flags[static_cast<int>(flag)];
            state.request = request;
            state.requested = enabled;
            state.reset = false;
        }
        return status;
    }

    void MpvPlayerManager::ResetVideoFlags()
    {
        selectedChannel_ = -1;
        selectionTarget_ = -1;
        videoFlagResetFailed_ = false;
        for (Channel &channel : channels_)
        {
            // Replacing request IDs also invalidates replies from the previous file.
            channel.flags = {};
            if (!channel.player || !channel.player->SupportsVideoFlags())
                continue;
            for (int index = 0; index < 3; ++index)
            {
                const RenderStatus status = RequestVideoFlag(channel, static_cast<VideoFlag>(index), false);
                if (!status)
                {
                    videoFlagResetFailed_ = true;
                    std::fprintf(stderr, "Video flag reset failed: %s\n", status.Message().c_str());
                }
                channel.flags[index].reset = true;
            }
        }
    }

    bool MpvPlayerManager::IsVideoFlagPending() const
    {
        if (selectionTarget_ >= 0)
            return true;
        for (const Channel &channel : channels_)
            for (const auto &flag : channel.flags)
                if (flag.request != 0)
                    return true;
        return false;
    }

    bool MpvPlayerManager::GetSelectedFlip(VideoFlag flag) const
    {
        if ((flag != VideoFlag::HorizontalFlip && flag != VideoFlag::VerticalFlip) ||
            selectedChannel_ < 0 || static_cast<std::size_t>(selectedChannel_) >= channels_.size())
            return false;
        return channels_[selectedChannel_].flags[static_cast<int>(flag)].value;
    }

    RenderStatus MpvPlayerManager::SelectChannel(int channelIndex)
    {
        Channel *target = FindChannel(channelIndex);
        if (application_.GetControlManager().GetPlayState() != PlayState::Playing ||
            !target || !IsOutputVisible(*target))
            return RenderStatus::Error("Select a visible channel during playback.");
        if (IsVideoFlagPending() || IsTrackChangePending() || videoFlagResetFailed_)
            return RenderStatus::Error("Video selection/flip update is not ready.");
        if (!target->player->SupportsVideoFlags())
            return RenderStatus::Error("This mpv build does not support video selection/flip.");
        if (selectedChannel_ == channelIndex)
            return RequestVideoFlag(*target, VideoFlag::ActiveBorder, false);

        selectionTarget_ = channelIndex;
        Channel *previous = FindChannel(selectedChannel_);
        const RenderStatus status = previous
            ? RequestVideoFlag(*previous, VideoFlag::ActiveBorder, false)
            : RequestVideoFlag(*target, VideoFlag::ActiveBorder, true);
        if (!status)
            selectionTarget_ = -1;
        return status;
    }

    RenderStatus MpvPlayerManager::ToggleSelectedFlip(VideoFlag flag)
    {
        Channel *channel = FindChannel(selectedChannel_);
        if ((flag != VideoFlag::HorizontalFlip && flag != VideoFlag::VerticalFlip) ||
            application_.GetControlManager().GetPlayState() != PlayState::Playing ||
            !channel || channel->currentTrackId <= 0)
            return RenderStatus::Error("Flip requires a selected, playing channel.");
        if (IsVideoFlagPending() || IsTrackChangePending() || videoFlagResetFailed_)
            return RenderStatus::Error("Video selection/flip update is not ready.");
        return RequestVideoFlag(*channel, flag, !GetSelectedFlip(flag));
    }

    void MpvPlayerManager::OnVideoFlagReply(int channelIndex, std::uint64_t requestId, int error)
    {
        Channel *channel = FindChannel(channelIndex);
        if (!channel || requestId == 0)
            return;
        for (int index = 0; index < 3; ++index)
        {
            auto &flag = channel->flags[index];
            if (flag.request != requestId)
                continue;
            flag.request = 0;
            if (error < 0)
            {
                if (flag.reset)
                    videoFlagResetFailed_ = true;
                if (index == static_cast<int>(VideoFlag::ActiveBorder))
                    selectionTarget_ = -1;
                return;
            }

            flag.value = flag.requested;
            if (index != static_cast<int>(VideoFlag::ActiveBorder))
                return;
            if (!flag.value && selectedChannel_ == channelIndex)
                selectedChannel_ = -1;
            if (selectionTarget_ < 0)
                return;
            if (flag.value)
            {
                selectedChannel_ = channelIndex;
                selectionTarget_ = -1;
                return;
            }

            selectedChannel_ = -1;
            Channel *target = FindChannel(selectionTarget_);
            if (!target || !IsOutputVisible(*target) ||
                application_.GetControlManager().GetPlayState() != PlayState::Playing)
            {
                selectionTarget_ = -1;
                return;
            }
            const RenderStatus status = RequestVideoFlag(*target, VideoFlag::ActiveBorder, true);
            if (!status)
            {
                selectionTarget_ = -1;
                std::fprintf(stderr, "Video selection failed: %s\n", status.Message().c_str());
            }
            return;
        }
    }

    bool MpvPlayerManager::IsTrackChangePending() const
    {
        for (const Channel &channel : channels_)
        {
            if (channel.player && channel.pendingTrackRequest != 0)
                return true;
        }
        return false;
    }

    void MpvPlayerManager::OnTrackReply(int channelIndex, std::uint64_t requestId, int error)
    {
        Channel *channel = FindChannel(channelIndex);
        if (!channel || requestId == 0 || channel->pendingTrackRequest != requestId)
            return;

        if (error < 0)
        {
            StopAll();
            application_.GetControlManager().SetPlayState(PlayState::NotLoad);
            return;
        }

        channel->currentTrackId = channel->pendingTrackId;
        channel->pendingTrackId = 0;
        channel->pendingTrackRequest = 0;
    }

    RenderStatus MpvPlayerManager::SetFullscreen(bool fullscreen)
    {
        fullscreen_ = fullscreen;
        for (const Channel &channel : channels_)
        {
            if (channel.player && !PushCellState(channel))
                return RenderStatus::Error("Failed to update fullscreen video visibility.");
        }
        return RenderStatus::Ok();
    }

    int MpvPlayerManager::HitTestChannel(float x, float y) const
    {
        if (!std::isfinite(x) || !std::isfinite(y))
            return -1;

        for (std::size_t i = channels_.size(); i > 0; --i)
        {
            const Channel &channel = channels_[i - 1];
            const RectF &rect = channel.destination;
            if (IsOutputVisible(channel) && rect.IsValid() &&
                x >= rect.x && x < rect.x + rect.width &&
                y >= rect.y && y < rect.y + rect.height)
            {
                return static_cast<int>(i - 1);
            }
        }
        return -1;
    }

} // namespace sdlviewer
