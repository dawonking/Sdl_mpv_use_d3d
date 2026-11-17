#pragma once

#include "render/IRenderBackend.h"

#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sdlviewer
{

    class SdlRenderThread final
    {
    public:
        explicit SdlRenderThread(
            std::unique_ptr<IRenderBackend> backend,
            UiFrameBuilder buildUi = {});
        ~SdlRenderThread();

        SdlRenderThread(const SdlRenderThread &) = delete;
        SdlRenderThread &operator=(const SdlRenderThread &) = delete;

        bool Start(
            SDL_Window *window,
            WindowMetrics metrics,
            std::string *error = nullptr);

        // Start, event processing and destruction belong to the SDL main thread.
        // Caller mode keeps all backend operations on that thread.
        bool StartOnCallerThread(
            SDL_Window *window,
            WindowMetrics metrics,
            std::string *error = nullptr);

        // In a caller-mode UI callback, shutdown finishes when that frame unwinds.
        // Destruction must occur after the callback/event dispatch has returned.
        void Stop() noexcept;

        bool IsRunning() const noexcept;

        bool UpsertVideoCell(const VideoCellState &cell);
        std::future<void> RemoveVideoCell(VideoId id);
        void NotifyVideoFrame(VideoId id);

        bool RequestResize(WindowMetrics metrics);
        void SetLayerOrder(FrameLayerOrder order);
        void SetClearColor(ClearColor color);

        UiTextureHandle LoadUiTexture(const std::string &objName, const std::string &path);

        void ProcessEvent(const SDL_Event &event);
        InputCaptureState GetInputCaptureState() const noexcept;

        void RequestFrame();

        std::string LastError() const;

    private:
        enum class ExecutionMode
        {
            Worker,
            Caller
        };

        bool StartImpl(
            SDL_Window *window,
            WindowMetrics metrics,
            std::string *error,
            ExecutionMode mode);

        RenderStatus InitializeBackend(
            SDL_Window *window,
            WindowMetrics metrics) noexcept;

        void RenderPendingWork(bool renderFrame) noexcept;

        struct FrameCallbackState
        {
            std::mutex mutex;
            SdlRenderThread *owner = nullptr;
        };
        void DisconnectFrameCallback() noexcept;

        ExecutionMode executionMode_ = ExecutionMode::Worker;
        std::thread::id callerThreadId_{};
        bool starting_ = false;
        bool platformInitialized_ = false;
        bool shuttingDown_ = false;
        std::shared_ptr<FrameCallbackState> frameCallbackState_;

        void PushMainThreadFrameEvent(
            std::uint32_t eventType, std::int32_t generation);
        void FinishRenderFrame();
        void RenderLoop(
            SDL_Window *window,
            WindowMetrics initialMetrics) noexcept;
        void CompleteRelease(VideoId id) noexcept;
        void CompleteAllReleases() noexcept;
        void RecordError(const RenderStatus &status);
        void RecordError(std::string message);

        std::unique_ptr<IRenderBackend> backend_;
        UiFrameBuilder buildUi_;

        mutable std::mutex stateMutex_;
        mutable std::mutex backendMutex_;
        std::condition_variable stateChanged_;
        std::thread renderThread_;

        bool stopRequested_ = false;
        bool wakeRequested_ = false;
        bool frameRequested_ = false;
        bool frameInFlight_ = false;
        bool workerFrameReady_ = false;
        bool mainWakePending_ = false;
        bool startFinished_ = false;
        bool backendInitialized_ = false;
        std::uint32_t renderEventType_ = 0;
        std::int32_t wakeGeneration_ = 0;

        WindowMetrics windowMetrics_{};
        bool resizePending_ = false;
        FrameLayerOrder layerOrder_ = FrameLayerOrder::UiThenVideo;
        ClearColor clearColor_{};

        std::unordered_map<VideoId, VideoCellState> cells_;
        std::unordered_set<VideoId> dirtyCells_;
        std::unordered_set<VideoId> pendingRelease_;
        std::unordered_set<VideoId> removedDuringFrame_;
        std::unordered_map<
            VideoId,
            std::vector<std::shared_ptr<std::promise<void>>>>
            releaseWaiters_;

        std::string lastError_;
    };

}
