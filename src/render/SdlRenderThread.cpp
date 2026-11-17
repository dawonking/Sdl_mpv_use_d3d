#include "render/SdlRenderThread.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sdlviewer
{
    namespace
    {
        std::vector<VideoLayer> BuildLayers(
            const std::vector<VideoCellState> &cells)
        {
            std::vector<VideoLayer> layers;
            layers.reserve(cells.size());

            for (const VideoCellState &cell : cells)
            {
                if (!cell.visible)
                {
                    continue;
                }

                layers.push_back(VideoLayer{
                    cell.id,
                    cell.surfaceSize, // VideoLayer::sourceSize에 전달됨
                    cell.destination,
                    cell.zOrder,
                    cell.visible,
                    cell.keepAspect,
                });
            }

            std::stable_sort(
                layers.begin(),
                layers.end(),
                [](const VideoLayer &lhs, const VideoLayer &rhs)
                {
                    return lhs.zOrder < rhs.zOrder;
                });

            return layers;
        }
    } // namespace

    SdlRenderThread::SdlRenderThread(
        std::unique_ptr<IRenderBackend> backend,
        UiFrameBuilder buildUi)
        : backend_(std::move(backend)), buildUi_(std::move(buildUi))
    {
        if (!backend_)
        {
            throw std::invalid_argument("SdlRenderThread requires a render backend.");
        }
    }

    SdlRenderThread::~SdlRenderThread()
    {
        Stop();
        // Destroying the coordinator from its own UI callback or another thread
        // cannot safely preserve the backend's lifetime.
        if (platformInitialized_ || starting_ || renderThread_.joinable())
        {
            std::terminate();
        }
    }

    bool SdlRenderThread::Start(
        SDL_Window *window,
        WindowMetrics metrics,
        std::string *error)
    {
        return StartImpl(
            window, metrics, error, ExecutionMode::Worker);
    }

    bool SdlRenderThread::StartOnCallerThread(
        SDL_Window *window,
        WindowMetrics metrics,
        std::string *error)
    {
        return StartImpl(
            window, metrics, error, ExecutionMode::Caller);
    }

    bool SdlRenderThread::StartImpl(
        SDL_Window *window,
        WindowMetrics metrics,
        std::string *error,
        ExecutionMode mode)
    {
        if (window == nullptr || !metrics.IsValid())
        {
            if (error)
            {
                *error = "Invalid initial window metrics.";
            }
            return false;
        }
        if (mode == ExecutionMode::Caller && !SDL_IsMainThread())
        {
            if (error)
            {
                *error = "Caller mode requires the SDL main thread.";
            }
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (starting_ || platformInitialized_ || renderThread_.joinable())
            {
                if (error)
                {
                    *error = "The render thread has already been started.";
                }
                return false;
            }

            executionMode_ = mode;
            callerThreadId_ = std::this_thread::get_id();
            starting_ = true;
            stopRequested_ = false;
            wakeRequested_ = false;
            frameRequested_ = false;
            frameInFlight_ = false;
            workerFrameReady_ = false;
            mainWakePending_ = false;
            startFinished_ = false;
            backendInitialized_ = false;
            windowMetrics_ = metrics;
            resizePending_ = true;
            removedDuringFrame_.clear();
            lastError_.clear();
            wakeGeneration_ = wakeGeneration_ == std::numeric_limits<std::int32_t>::max()
                                  ? 1 : wakeGeneration_ + 1;
        }

        const auto failStart = [this, error](const RenderStatus &status)
        {
            DisconnectFrameCallback();
            if (platformInitialized_)
            {
                backend_->ShutdownPlatform();
            }
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                starting_ = false;
                platformInitialized_ = false;
                backendInitialized_ = false;
                stopRequested_ = true;
                mainWakePending_ = false;
                frameRequested_ = false;
                lastError_ = status.Message();
            }
            CompleteAllReleases();
            if (error)
            {
                *error = status.Message();
            }
            return false;
        };

        if (renderEventType_ == 0)
        {
            const auto eventType = SDL_RegisterEvents(1);
            if (eventType == 0)
            {
                return failStart(RenderStatus::Error(
                    std::string("SDL_RegisterEvents failed: ") + SDL_GetError()));
            }
            std::lock_guard<std::mutex> lock(stateMutex_);
            renderEventType_ = eventType;
        }

        try
        {
            frameCallbackState_ = std::make_shared<FrameCallbackState>();
            frameCallbackState_->owner = this;
            // Track partial platform initialization so every failure rolls it back.
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                platformInitialized_ = true;
            }
            const RenderStatus status = backend_->InitializePlatform(window);
            if (!status)
            {
                return failStart(status);
            }
        }
        catch (const std::exception &exception)
        {
            return failStart(RenderStatus::Error(exception.what()));
        }
        catch (...)
        {
            return failStart(RenderStatus::Error(
                "Unknown renderer platform initialization failure."));
        }

        if (mode == ExecutionMode::Caller)
        {
            const RenderStatus status = InitializeBackend(window, metrics);
            if (!status)
            {
                DisconnectFrameCallback();
                backend_->Shutdown();
                return failStart(status);
            }
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                backendInitialized_ = true;
                startFinished_ = true;
                starting_ = false;
                // A restarted backend owns new surfaces, so upload existing cells again.
                for (const auto &item : cells_)
                {
                    dirtyCells_.insert(item.first);
                }
            }
            RequestFrame();
            return true;
        }

        std::unique_lock<std::mutex> lock(stateMutex_);
        try
        {
            renderThread_ = std::thread(
                &SdlRenderThread::RenderLoop, this, window, metrics);
        }
        catch (const std::exception &exception)
        {
            lock.unlock();
            return failStart(RenderStatus::Error(exception.what()));
        }
        stateChanged_.wait(lock, [this] { return startFinished_; });
        const bool started = backendInitialized_;
        const std::string startError = lastError_;
        if (started)
        {
            starting_ = false;
        }
        lock.unlock();

        if (!started)
        {
            renderThread_.join();
            return failStart(RenderStatus::Error(startError));
        }
        RequestFrame();
        return true;
    }

    void SdlRenderThread::DisconnectFrameCallback() noexcept
    {
        if (frameCallbackState_)
        {
            // Wait for an active callback without holding renderer/backend locks.
            std::lock_guard<std::mutex> lock(frameCallbackState_->mutex);
            frameCallbackState_->owner = nullptr;
        }
    }

    void SdlRenderThread::Stop() noexcept
    {
        ExecutionMode mode;
        bool shutdownBackend = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (!platformInitialized_ && !starting_ && !renderThread_.joinable())
            {
                return;
            }
            if (callerThreadId_ != std::this_thread::get_id() || starting_)
            {
                lastError_ = "Stop must run on the starting thread after initialization.";
                return;
            }
            if (shuttingDown_)
            {
                return;
            }
            stopRequested_ = true;
            wakeRequested_ = true;
            mode = executionMode_;
            if (mode == ExecutionMode::Caller && frameInFlight_)
            {
                // UI callbacks unwind before the backend and its context are destroyed.
                return;
            }
            shuttingDown_ = true;
            shutdownBackend = backendInitialized_;
        }

        DisconnectFrameCallback();
        if (mode == ExecutionMode::Worker)
        {
            stateChanged_.notify_all();
            if (renderThread_.joinable())
            {
                renderThread_.join();
            }
        }
        else if (shutdownBackend)
        {
            backend_->Shutdown();
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            backendInitialized_ = false;
        }
        CompleteAllReleases();
        backend_->ShutdownPlatform();

        std::lock_guard<std::mutex> lock(stateMutex_);
        platformInitialized_ = false;
        shuttingDown_ = false;
        frameRequested_ = false;
        frameInFlight_ = false;
        workerFrameReady_ = false;
        mainWakePending_ = false;
        wakeRequested_ = false;
        removedDuringFrame_.clear();
    }

    bool SdlRenderThread::IsRunning() const noexcept
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return backendInitialized_ && !stopRequested_;
    }

    bool SdlRenderThread::UpsertVideoCell(const VideoCellState &cell)
    {
        if (!cell.IsValid())
        {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            if (pendingRelease_.find(cell.id) !=
                pendingRelease_.end())
            {
                return false;
            }

            cells_[cell.id] = cell;
            dirtyCells_.insert(cell.id);
        }

        RequestFrame();
        return true;
    }

    std::future<void> SdlRenderThread::RemoveVideoCell(VideoId id)
    {
        auto completion = std::make_shared<std::promise<void>>();
        std::future<void> future = completion->get_future();
        bool completeImmediately = false;
        bool releaseOnCaller = false;
        bool wakeCaller = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            cells_.erase(id);
            dirtyCells_.erase(id);
            if (!backendInitialized_ && !starting_)
            {
                completeImmediately = true;
            }
            else
            {
                pendingRelease_.insert(id);
                releaseWaiters_[id].push_back(completion);
                if (executionMode_ == ExecutionMode::Caller)
                {
                    if (frameInFlight_)
                    {
                        removedDuringFrame_.insert(id);
                    }
                    releaseOnCaller = backendInitialized_ &&
                        callerThreadId_ == std::this_thread::get_id();
                    wakeCaller = !releaseOnCaller;
                }
                else
                {
                    wakeRequested_ = true;
                }
            }
        }
        if (completeImmediately)
        {
            completion->set_value();
        }
        else if (releaseOnCaller)
        {
            // The caller may immediately wait on this future, even inside buildUi.
            // Caller-mode backend access is owner-thread-only and needs no mutex.
            backend_->ReleaseVideoSurface(id);
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                pendingRelease_.erase(id);
            }
            CompleteRelease(id);
        }
        else if (wakeCaller)
        {
            RequestFrame();
        }
        else
        {
            stateChanged_.notify_one();
        }
        return future;
    }

    void SdlRenderThread::NotifyVideoFrame(VideoId id)
    {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (cells_.find(id) == cells_.end())
            {
                return;
            }

            dirtyCells_.insert(id);
        }
        RequestFrame();
    }

    bool SdlRenderThread::RequestResize(
        WindowMetrics metrics)
    {
        if (!metrics.IsValid())
        {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            windowMetrics_ = metrics;
            resizePending_ = true;
        }

        RequestFrame();
        return true;
    }

    void SdlRenderThread::SetLayerOrder(FrameLayerOrder order)
    {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            layerOrder_ = order;
        }
        RequestFrame();
    }

    void SdlRenderThread::SetClearColor(ClearColor color)
    {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            clearColor_ = color;
        }
        RequestFrame();
    }

    void SdlRenderThread::ProcessEvent(const SDL_Event &event)
    {
        bool callerMode = false;
        bool renderEvent = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            callerMode = executionMode_ == ExecutionMode::Caller;
            if (callerMode && callerThreadId_ != std::this_thread::get_id())
            {
                lastError_ = "Caller-mode events must run on the starting thread.";
                return;
            }
            if (!backendInitialized_ || stopRequested_)
            {
                return;
            }
            renderEvent = event.type == renderEventType_;
            if (renderEvent)
            {
                // Ignore a wake retained across Stop/Start before changing pending state.
                if (event.user.code != wakeGeneration_)
                {
                    return;
                }
                mainWakePending_ = false;
                if (!frameRequested_ || frameInFlight_)
                {
                    return;
                }
                frameRequested_ = false;
                frameInFlight_ = true;
                removedDuringFrame_.clear();
            }
            else if (callerMode && frameInFlight_)
            {
                lastError_ = "ProcessEvent cannot be nested inside a caller-mode frame.";
                return;
            }
        }

        if (renderEvent)
        {
            try
            {
                backend_->PreparePlatformFrame();
            }
            catch (const std::exception &exception)
            {
                RecordError(exception.what());
                if (callerMode)
                {
                    RenderPendingWork(false);
                }
                FinishRenderFrame();
                return;
            }
            catch (...)
            {
                RecordError("Unknown failure while preparing the renderer platform frame.");
                if (callerMode)
                {
                    RenderPendingWork(false);
                }
                FinishRenderFrame();
                return;
            }

            if (callerMode)
            {
                RenderPendingWork(true);
            }
            else
            {
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    if (stopRequested_)
                    {
                        frameInFlight_ = false;
                        return;
                    }
                    wakeRequested_ = true;
                    workerFrameReady_ = true;
                }
                stateChanged_.notify_one();
            }
            return;
        }

        // Windows input must not wait for the worker's mpv/Present backend lock.
        backend_->ProcessEvent(event);
        RequestFrame();
    }

    InputCaptureState SdlRenderThread::GetInputCaptureState() const noexcept
    {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (!backendInitialized_ || stopRequested_ ||
                (executionMode_ == ExecutionMode::Caller &&
                 callerThreadId_ != std::this_thread::get_id()))
            {
                return {};
            }
        }
        return backend_->GetInputCaptureState();
    }

    UiTextureHandle SdlRenderThread::LoadUiTexture(
        const std::string &objName, const std::string &path)
    {
        bool callerMode = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (!backendInitialized_ || stopRequested_)
            {
                return {};
            }
            callerMode = executionMode_ == ExecutionMode::Caller;
            if (callerMode && callerThreadId_ != std::this_thread::get_id())
            {
                lastError_ = "Caller-mode textures must be loaded on the starting thread.";
                return {};
            }
        }
        if (callerMode)
        {
            return backend_->LoadUiTexture(objName, path);
        }
        std::lock_guard<std::mutex> lock(backendMutex_);
        return backend_->LoadUiTexture(objName, path);
    }

    std::string SdlRenderThread::LastError() const
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return lastError_;
    }

    RenderStatus SdlRenderThread::InitializeBackend(
        SDL_Window *window,
        WindowMetrics metrics) noexcept
    {
        try
        {
            RenderInitParams params;
            params.window = window;
            params.metrics = metrics;
            const auto callbackState = frameCallbackState_;
            params.onFrameReady = [callbackState](VideoId id)
            {
                std::lock_guard<std::mutex> lock(callbackState->mutex);
                if (callbackState->owner)
                {
                    callbackState->owner->NotifyVideoFrame(id);
                }
            };

            if (executionMode_ == ExecutionMode::Worker)
            {
                std::lock_guard<std::mutex> lock(backendMutex_);
                return backend_->Initialize(params);
            }

            return backend_->Initialize(params);
        }
        catch (const std::exception &exception)
        {
            return RenderStatus::Error(exception.what());
        }
        catch (...)
        {
            return RenderStatus::Error(
                "Unknown renderer initialization failure.");
        }
    }

    void SdlRenderThread::RenderLoop(
        SDL_Window *window,
        WindowMetrics initialMetrics) noexcept
    {
        const RenderStatus initializeStatus =
            InitializeBackend(window, initialMetrics);
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            backendInitialized_ = static_cast<bool>(initializeStatus);
            startFinished_ = true;
            if (!initializeStatus)
            {
                lastError_ = initializeStatus.Message();
            }
        }
        stateChanged_.notify_all();

        if (initializeStatus)
        {
            while (true)
            {
                bool renderFrame = false;
                {
                    std::unique_lock<std::mutex> lock(stateMutex_);
                    stateChanged_.wait(lock, [this]
                    {
                        return stopRequested_ || wakeRequested_;
                    });
                    if (stopRequested_)
                    {
                        break;
                    }
                    wakeRequested_ = false;
                    // Surface release can wake us while main is still preparing
                    // the platform frame. Only consume explicitly prepared work.
                    renderFrame = workerFrameReady_;
                    workerFrameReady_ = false;
                }
                RenderPendingWork(renderFrame);
            }
        }

        DisconnectFrameCallback();
        {
            std::lock_guard<std::mutex> lock(backendMutex_);
            backend_->Shutdown();
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            backendInitialized_ = false;
        }
        CompleteAllReleases();
    }

    void SdlRenderThread::RenderPendingWork(bool renderFrame) noexcept
    {
        try
        {
            // The lambda gives every early exit one common frame completion path.
            const auto render = [this, renderFrame]
            {
                WindowMetrics windowMetrics{};
                bool resizeOutput = false;
                FrameLayerOrder layerOrder = FrameLayerOrder::VideoThenUi;
                ClearColor clearColor{};
                std::vector<VideoCellState> cells;
                std::unordered_set<VideoId> dirtyCells;
                std::unordered_set<VideoId> releaseCells;
                const bool callerMode = executionMode_ == ExecutionMode::Caller;
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    if (renderFrame)
                    {
                        windowMetrics = windowMetrics_;
                        resizeOutput = resizePending_;
                        resizePending_ = false;
                        layerOrder = layerOrder_;
                        clearColor = clearColor_;
                        cells.reserve(cells_.size());
                        for (const auto &item : cells_)
                        {
                            cells.push_back(item.second);
                        }
                        dirtyCells.swap(dirtyCells_);
                    }
                    releaseCells.swap(pendingRelease_);
                }

                std::unique_lock<std::mutex> backendLock(backendMutex_, std::defer_lock);
                if (!callerMode)
                {
                    backendLock.lock();
                }
                for (VideoId id : releaseCells)
                {
                    backend_->ReleaseVideoSurface(id);
                    CompleteRelease(id);
                }
                if (!renderFrame)
                {
                    return;
                }

                const auto callerStopped = [this, callerMode]
                {
                    if (!callerMode)
                    {
                        return false;
                    }
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    return stopRequested_;
                };
                if (callerStopped())
                {
                    return;
                }
                if (resizeOutput)
                {
                    const RenderStatus status = backend_->ResizeOutput(windowMetrics);
                    if (!status)
                    {
                        RecordError(status);
                        std::lock_guard<std::mutex> lock(stateMutex_);
                        dirtyCells_.insert(dirtyCells.begin(), dirtyCells.end());
                        return;
                    }
                }
                if (callerStopped())
                {
                    return;
                }

                std::vector<VideoCellState> readyCells;
                readyCells.reserve(cells.size());
                const bool canRenderOutput = windowMetrics.pixelSize.IsValid();
                for (const VideoCellState &cell : cells)
                {
                    const RenderStatus status = backend_->EnsureVideoSurface(cell);
                    if (!status)
                    {
                        RecordError(status);
                        if (dirtyCells.find(cell.id) != dirtyCells.end())
                        {
                            std::lock_guard<std::mutex> lock(stateMutex_);
                            dirtyCells_.insert(cell.id);
                        }
                        continue;
                    }
                    readyCells.push_back(cell);
                    if (canRenderOutput && dirtyCells.find(cell.id) != dirtyCells.end())
                    {
                        const RenderStatus frameStatus = backend_->RenderVideoFrame(cell.id);
                        if (!frameStatus)
                        {
                            RecordError(frameStatus);
                            std::lock_guard<std::mutex> lock(stateMutex_);
                            dirtyCells_.insert(cell.id);
                        }
                    }
                    if (callerStopped())
                    {
                        return;
                    }
                }
                if (!canRenderOutput)
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    dirtyCells_.insert(dirtyCells.begin(), dirtyCells.end());
                    return;
                }

                std::vector<VideoLayer> layers = BuildLayers(readyCells);
                const auto composite = [this, callerMode, &layers]
                {
                    if (callerMode)
                    {
                        // A UI callback may synchronously remove/recreate a cell.
                        // Never composite the surface captured before that callback.
                        std::lock_guard<std::mutex> lock(stateMutex_);
                        layers.erase(std::remove_if(layers.begin(), layers.end(),
                            [this](const VideoLayer &layer)
                            {
                                return cells_.find(layer.id) == cells_.end() ||
                                       removedDuringFrame_.find(layer.id) != removedDuringFrame_.end();
                            }), layers.end());
                    }
                    backend_->CompositeVideo(layers);
                };
                backend_->BeginFrame(windowMetrics.pixelSize, clearColor);
                if (callerStopped())
                {
                    return;
                }
                if (layerOrder == FrameLayerOrder::VideoThenUi)
                {
                    composite();
                    if (!callerStopped())
                    {
                        backend_->RenderUiFrame(buildUi_);
                    }
                }
                else
                {
                    backend_->RenderUiFrame(buildUi_);
                    if (!callerStopped())
                    {
                        composite();
                    }
                }
                if (!callerStopped())
                {
                    RecordError(backend_->Present());
                }
            };
            render();
        }
        catch (const std::exception &exception)
        {
            RecordError(exception.what());
        }
        catch (...)
        {
            RecordError("Unknown failure in the render loop.");
        }
        if (renderFrame)
        {
            FinishRenderFrame();
        }
    }

    void SdlRenderThread::RequestFrame()
    {
        std::uint32_t eventType = 0;
        std::int32_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (stopRequested_)
            {
                return;
            }
            frameRequested_ = true;
            if (backendInitialized_ && renderEventType_ != 0 &&
                !frameInFlight_ && !mainWakePending_)
            {
                mainWakePending_ = true;
                eventType = renderEventType_;
                generation = wakeGeneration_;
            }
        }
        if (eventType != 0)
        {
            PushMainThreadFrameEvent(eventType, generation);
        }
    }

    void SdlRenderThread::PushMainThreadFrameEvent(
        std::uint32_t eventType, std::int32_t generation)
    {
        SDL_Event event{};
        event.type = eventType;
        event.user.code = generation;
        if (SDL_PushEvent(&event))
        {
            return;
        }
        const std::string message =
            std::string("SDL_PushEvent(renderer) failed: ") + SDL_GetError();
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (generation == wakeGeneration_)
        {
            mainWakePending_ = false;
            lastError_ = message;
        }
    }

    void SdlRenderThread::FinishRenderFrame()
    {
        std::uint32_t eventType = 0;
        std::int32_t generation = 0;
        bool finishStop = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            frameInFlight_ = false;
            finishStop = stopRequested_ && executionMode_ == ExecutionMode::Caller;
            if (!stopRequested_ && frameRequested_ && !mainWakePending_)
            {
                mainWakePending_ = true;
                eventType = renderEventType_;
                generation = wakeGeneration_;
            }
        }
        if (finishStop)
        {
            Stop();
        }
        else if (eventType != 0)
        {
            PushMainThreadFrameEvent(eventType, generation);
        }
    }

    void SdlRenderThread::CompleteRelease(VideoId id) noexcept
    {
        std::vector<std::shared_ptr<std::promise<void>>> waiters;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            const auto found = releaseWaiters_.find(id);

            if (found != releaseWaiters_.end())
            {
                waiters = std::move(found->second);
                releaseWaiters_.erase(found);
            }
        }

        for (const auto &waiter : waiters)
        {
            try
            {
                waiter->set_value();
            }
            catch (...)
            {
            }
        }
    }

    void SdlRenderThread::CompleteAllReleases() noexcept
    {
        std::vector<std::shared_ptr<std::promise<void>>> waiters;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            for (auto &item : releaseWaiters_)
            {
                for (auto &waiter : item.second)
                {
                    waiters.push_back(std::move(waiter));
                }
            }

            releaseWaiters_.clear();
            pendingRelease_.clear();
        }

        for (const auto &waiter : waiters)
        {
            try
            {
                waiter->set_value();
            }
            catch (...)
            {
            }
        }
    }

    void SdlRenderThread::RecordError(const RenderStatus &status)
    {
        if (!status)
        {
            RecordError(status.Message());
        }
    }

    void SdlRenderThread::RecordError(std::string message)
    {
        if (message.empty())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_ = std::move(message);
    }

}
