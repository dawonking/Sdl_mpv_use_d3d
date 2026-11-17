#include "render/SdlRenderThread.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>

namespace
{
    using namespace sdlviewer;
    using namespace std::chrono_literals;

    void Check(bool condition, const char *message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    // Also bound deadlocks when this executable is run without CTest.
    class Deadline
    {
    public:
        Deadline() : thread_([this]
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!changed_.wait_for(lock, 20s, [this] { return done_; }))
            {
                std::cerr << "FAIL: scheduling test deadline exceeded\n";
                std::_Exit(EXIT_FAILURE);
            }
        }) {}

        ~Deadline()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                done_ = true;
            }
            changed_.notify_one();
            thread_.join();
        }

    private:
        std::mutex mutex_;
        std::condition_variable changed_;
        bool done_ = false;
        std::thread thread_;
    };

    struct Call
    {
        std::string name;
        std::thread::id thread;
    };

    struct BackendState
    {
        void Record(const char *name)
        {
            std::lock_guard<std::mutex> lock(mutex);
            calls.push_back({name, std::this_thread::get_id()});
        }

        std::vector<Call> Calls() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return calls;
        }

        int Count(const char *name) const
        {
            const auto snapshot = Calls();
            return static_cast<int>(std::count_if(
                snapshot.begin(), snapshot.end(),
                [name](const Call &call) { return call.name == name; }));
        }

        FrameReadyCallback Callback() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return callback;
        }

        mutable std::mutex mutex;
        std::vector<Call> calls;
        FrameReadyCallback callback;
        std::unordered_set<VideoId> surfaces;
        std::atomic<bool> compositedReleasedSurface{false};
        std::atomic<bool> insideUi{false};
        std::atomic<bool> shutdownInsideUi{false};
        std::atomic<bool> insidePrepare{false};
        std::atomic<bool> renderedDuringPrepare{false};
        std::function<void()> prepareHook;
        bool failPlatform = false;
        bool throwPlatform = false;
        bool failInitialize = false;
        bool throwInitialize = false;
        bool notifyDuringInitialize = false;
    };

    class FakeBackend final : public IRenderBackend
    {
    public:
        explicit FakeBackend(std::shared_ptr<BackendState> state)
            : state_(std::move(state)) {}

        RenderStatus InitializePlatform(SDL_Window *) override
        {
            state_->Record("platform-init");
            if (state_->throwPlatform)
            {
                throw std::runtime_error("platform initialization threw");
            }
            return state_->failPlatform
                ? RenderStatus::Error("platform initialization failed")
                : RenderStatus::Ok();
        }

        void PreparePlatformFrame() override
        {
            state_->Record("prepare");
            state_->insidePrepare = true;
            if (state_->prepareHook)
            {
                state_->prepareHook();
            }
            state_->insidePrepare = false;
        }
        void ShutdownPlatform() noexcept override { state_->Record("platform-stop"); }

        RenderStatus Initialize(const RenderInitParams &params) override
        {
            state_->Record("initialize");
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                state_->callback = params.onFrameReady;
            }
            if (state_->notifyDuringInitialize)
            {
                std::thread callbackThread([&]
                {
                    for (int i = 0; i < 100; ++i)
                    {
                        params.onFrameReady(1);
                    }
                });
                callbackThread.join();
            }
            if (state_->throwInitialize)
            {
                throw std::runtime_error("backend initialization threw");
            }
            return state_->failInitialize
                ? RenderStatus::Error("backend initialization failed")
                : RenderStatus::Ok();
        }

        void Shutdown() noexcept override
        {
            state_->Record("shutdown");
            if (state_->insideUi)
            {
                state_->shutdownInsideUi = true;
            }
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->surfaces.clear();
        }

        RenderStatus ResizeOutput(WindowMetrics) override
        {
            state_->Record("resize");
            return RenderStatus::Ok();
        }

        RenderStatus EnsureVideoSurface(const VideoCellState &cell) override
        {
            state_->Record("ensure");
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->surfaces.insert(cell.id);
            return RenderStatus::Ok();
        }

        void ReleaseVideoSurface(VideoId id) noexcept override
        {
            state_->Record("release");
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->surfaces.erase(id);
        }

        RenderStatus RenderVideoFrame(VideoId) override
        {
            state_->Record("video");
            return RenderStatus::Ok();
        }

        void BeginFrame(PixelSize, ClearColor) override
        {
            state_->Record("begin");
            if (state_->insidePrepare)
            {
                state_->renderedDuringPrepare = true;
            }
        }

        void CompositeVideo(const std::vector<VideoLayer> &layers) override
        {
            state_->Record("composite");
            std::lock_guard<std::mutex> lock(state_->mutex);
            for (const auto &layer : layers)
            {
                if (state_->surfaces.count(layer.id) == 0)
                {
                    state_->compositedReleasedSurface = true;
                }
            }
        }

        void RenderUiFrame(const UiFrameBuilder &buildUi) override
        {
            state_->Record("ui");
            if (buildUi)
            {
                state_->insideUi = true;
                buildUi();
                state_->insideUi = false;
            }
        }

        RenderStatus Present() override
        {
            state_->Record("present");
            return RenderStatus::Ok();
        }

        UiTextureHandle LoadUiTexture(const std::string &, const std::string &) override
        {
            state_->Record("texture");
            return {1, 16, 16};
        }

        void ProcessEvent(const SDL_Event &) override { state_->Record("event"); }
        InputCaptureState GetInputCaptureState() const noexcept override { return {}; }

    private:
        std::shared_ptr<BackendState> state_;
    };

    WindowMetrics Metrics() { return {{320, 200}, {640, 400}, 2.0f, 1.0f}; }

    VideoCellState Cell(VideoId id)
    {
        static int opaquePlayer;
        VideoCellState cell;
        cell.id = id;
        // The fake never dereferences or passes this sentinel to mpv.
        cell.player = reinterpret_cast<mpv_handle *>(&opaquePlayer);
        cell.surfaceSize = {160, 100};
        cell.destination = {0, 0, 80, 50};
        return cell;
    }

    std::vector<SDL_Event> TakeWakeEvents()
    {
        std::vector<SDL_Event> events;
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (event.type >= SDL_EVENT_USER)
            {
                events.push_back(event);
            }
        }
        return events;
    }

    void PumpFor(SdlRenderThread &renderer, std::chrono::milliseconds duration)
    {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        do
        {
            SDL_Event event{};
            if (SDL_WaitEventTimeout(&event, 5))
            {
                renderer.ProcessEvent(event);
            }
        } while (std::chrono::steady_clock::now() < deadline);
    }

    template <typename Predicate>
    void PumpUntil(SdlRenderThread &renderer, Predicate done)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!done() && std::chrono::steady_clock::now() < deadline)
        {
            PumpFor(renderer, 5ms);
        }
        Check(done(), "timed out waiting for renderer work");
    }

    void Start(SdlRenderThread &renderer, SDL_Window *window, bool caller)
    {
        std::string error;
        const bool started = caller
            ? renderer.StartOnCallerThread(window, Metrics(), &error)
            : renderer.Start(window, Metrics(), &error);
        if (!started)
        {
            throw std::runtime_error("Start failed: " + error);
        }
        Check(renderer.IsRunning(), "successful start must report running");
    }

    void CheckThreads(const BackendState &state, bool caller)
    {
        const auto mainThread = std::this_thread::get_id();
        std::thread::id renderThread;
        for (const auto &call : state.Calls())
        {
            if (call.name == "initialize")
            {
                renderThread = call.thread;
                break;
            }
        }
        Check(renderThread != std::thread::id{}, "backend initialization was not observed");
        Check(caller ? renderThread == mainThread : renderThread != mainThread,
              "backend initialized on wrong thread");
        for (const auto &call : state.Calls())
        {
            const bool platform = call.name == "platform-init" || call.name == "prepare" ||
                                  call.name == "platform-stop" || call.name == "event";
            Check(call.thread == (platform ? mainThread : renderThread),
                  "backend call violated platform/render thread ownership");
        }
    }

    void TestScheduling(SDL_Window *window, bool caller)
    {
        TakeWakeEvents();
        auto state = std::make_shared<BackendState>();
        SdlRenderThread renderer(std::make_unique<FakeBackend>(state));
        std::string error;
        Check(!(caller ? renderer.StartOnCallerThread(nullptr, Metrics(), &error)
                       : renderer.Start(nullptr, Metrics(), &error)),
              "null window passed start validation");
        Check(!(caller ? renderer.StartOnCallerThread(window, {}, &error)
                       : renderer.Start(window, {}, &error)),
              "invalid metrics passed start validation");
        Check(state->Calls().empty(), "invalid start reached backend initialization");
        Start(renderer, window, caller);
        Check(!(caller ? renderer.StartOnCallerThread(window, Metrics(), &error)
                       : renderer.Start(window, Metrics(), &error)),
              "duplicate start was accepted");
        Check(state->Count("initialize") == 1, "duplicate start initialized backend again");
        PumpUntil(renderer, [&] { return state->Count("present") == 1; });
        PumpFor(renderer, 30ms);
        Check(state->Count("present") == 1, "idle renderer produced extra frames");

        std::vector<std::thread> requesters;
        for (int i = 0; i < 4; ++i)
        {
            requesters.emplace_back([&]
            {
                for (int request = 0; request < 100; ++request)
                {
                    renderer.RequestFrame();
                }
            });
        }
        for (auto &requester : requesters)
        {
            requester.join();
        }
        const auto wakes = TakeWakeEvents();
        Check(wakes.size() == 1, "concurrent frame requests did not coalesce to one SDL wake");
        renderer.ProcessEvent(wakes.front());
        PumpUntil(renderer, [&] { return state->Count("present") == 2; });
        PumpFor(renderer, 30ms);
        Check(state->Count("present") == 2, "coalesced request caused extra frames");

        TakeWakeEvents();
        std::promise<void> beginWait;
        auto waiting = beginWait.get_future();
        std::thread delayedRequester([&]
        {
            if (waiting.wait_for(2s) == std::future_status::ready)
            {
                std::this_thread::sleep_for(20ms);
                renderer.RequestFrame();
            }
        });
        SDL_Event wake{};
        beginWait.set_value();
        const bool woke = SDL_WaitEventTimeout(&wake, 1000);
        delayedRequester.join();
        Check(woke && wake.type >= SDL_EVENT_USER,
              "producer RequestFrame failed to wake main SDL event wait");
        renderer.ProcessEvent(wake);
        PumpUntil(renderer, [&] { return state->Count("present") == 3; });
        PumpFor(renderer, 30ms);
        Check(state->Count("present") == 3, "event-wait wake caused extra frames");
        renderer.Stop();
        renderer.Stop();
        Check(!renderer.IsRunning(), "stopped renderer still reports running");
        Check(state->Count("shutdown") == 1, "repeated Stop repeated backend shutdown");
        Check(state->Count("platform-stop") == 1, "repeated Stop repeated platform shutdown");
        CheckThreads(*state, caller);
    }

    void TestRequestDuringFrame(SDL_Window *window, bool caller)
    {
        TakeWakeEvents();
        auto state = std::make_shared<BackendState>();
        SdlRenderThread *current = nullptr;
        std::atomic<int> uiFrames{0};
        SdlRenderThread renderer(std::make_unique<FakeBackend>(state), [&]
        {
            if (++uiFrames == 1)
            {
                std::thread requester([&]
                {
                    for (int i = 0; i < 100; ++i)
                    {
                        current->RequestFrame();
                    }
                });
                requester.join();
            }
        });
        current = &renderer;
        Start(renderer, window, caller);
        PumpUntil(renderer, [&] { return state->Count("present") == 2; });
        PumpFor(renderer, 30ms);
        Check(state->Count("present") == 2, "request during frame was lost or repeated");
        renderer.Stop();
        CheckThreads(*state, caller);
    }

    void TestSurfaceRelease(SDL_Window *window)
    {
        TakeWakeEvents();
        auto state = std::make_shared<BackendState>();
        SdlRenderThread renderer(std::make_unique<FakeBackend>(state));
        Start(renderer, window, true);
        Check(renderer.UpsertVideoCell(Cell(1)), "could not add video cell");
        PumpUntil(renderer, [&] { return state->Count("video") == 1; });
        auto ownerRelease = renderer.RemoveVideoCell(1);
        Check(ownerRelease.wait_for(0ms) == std::future_status::ready,
              "owner release must complete without pumping another event");
        Check(state->Count("release") == 1, "owner release completed before releasing surface");

        Check(renderer.UpsertVideoCell(Cell(2)), "could not add second video cell");
        PumpUntil(renderer, [&] { return state->Count("video") == 2; });
        std::future<void> otherRelease;
        std::thread remover([&] { otherRelease = renderer.RemoveVideoCell(2); });
        remover.join();
        Check(otherRelease.wait_for(0ms) == std::future_status::timeout,
              "off-thread release ran before the owner processed it");
        PumpUntil(renderer, [&] { return otherRelease.wait_for(0ms) == std::future_status::ready; });
        Check(state->Count("release") == 2, "off-thread release did not release its surface");

        Check(renderer.UpsertVideoCell(Cell(3)), "could not add shutdown video cell");
        PumpUntil(renderer, [&] { return state->Count("video") == 3; });
        std::thread pending([&] { otherRelease = renderer.RemoveVideoCell(3); });
        pending.join();
        renderer.Stop();
        Check(otherRelease.wait_for(0ms) == std::future_status::ready,
              "Stop left a surface release future pending");
        CheckThreads(*state, true);
    }

    void TestReleaseAndTextureDuringUi(SDL_Window *window)
    {
        TakeWakeEvents();
        auto state = std::make_shared<BackendState>();
        SdlRenderThread *current = nullptr;
        bool releasedImmediately = false;
        bool textureLoaded = false;
        bool ran = false;
        SdlRenderThread renderer(std::make_unique<FakeBackend>(state), [&]
        {
            if (!ran)
            {
                ran = true;
                textureLoaded = current->LoadUiTexture("test", "unused").IsValid();
                auto released = current->RemoveVideoCell(1);
                releasedImmediately = released.wait_for(0ms) == std::future_status::ready;
            }
        });
        current = &renderer;
        Start(renderer, window, true);
        Check(renderer.UpsertVideoCell(Cell(1)), "could not add UI callback video cell");
        PumpUntil(renderer, [&] { return state->Count("present") >= 1; });
        Check(ran && textureLoaded, "UI texture load failed during render callback");
        Check(releasedImmediately, "release in UI callback did not complete immediately");
        Check(state->Count("release") == 1, "UI callback did not release surface");
        Check(!state->compositedReleasedSurface, "current frame composited an already released surface");
        renderer.Stop();
        CheckThreads(*state, true);
    }

    void TestWorkerReleaseWithoutPump(SDL_Window *window)
    {
        TakeWakeEvents();
        auto state = std::make_shared<BackendState>();
        SdlRenderThread renderer(std::make_unique<FakeBackend>(state));
        Start(renderer, window, false);
        Check(renderer.UpsertVideoCell(Cell(1)), "could not add worker video cell");
        PumpUntil(renderer, [&] { return state->Count("video") == 1; });
        auto released = renderer.RemoveVideoCell(1);
        Check(released.wait_for(2s) == std::future_status::ready,
              "worker surface release depended on main event processing");
        Check(state->Count("release") == 1, "worker release completed before releasing surface");
        renderer.Stop();
        CheckThreads(*state, false);
    }

    void TestWorkerReleaseDuringPlatformPreparation(SDL_Window *window)
    {
        TakeWakeEvents();
        auto state = std::make_shared<BackendState>();
        SdlRenderThread renderer(std::make_unique<FakeBackend>(state));
        Start(renderer, window, false);
        Check(renderer.UpsertVideoCell(Cell(1)), "could not add preparation-race video cell");
        PumpUntil(renderer, [&] { return state->Count("present") == 1; });
        bool releasedBeforePrepareReturned = false;
        bool frameAdvancedDuringPrepare = false;
        state->prepareHook = [&]
        {
            const int frames = state->Count("present");
            const int uiFrames = state->Count("ui");
            std::future<void> released;
            std::thread remover([&] { released = renderer.RemoveVideoCell(1); });
            remover.join();
            releasedBeforePrepareReturned = released.wait_for(2s) == std::future_status::ready;
            // Keep platform preparation active long enough to expose an erroneous
            // render continuation after the release-only wake completes.
            std::this_thread::sleep_for(20ms);
            frameAdvancedDuringPrepare = state->Count("present") != frames ||
                                         state->Count("ui") != uiFrames;
        };
        renderer.RequestFrame();
        PumpUntil(renderer, [&] { return state->Count("present") == 2; });
        Check(releasedBeforePrepareReturned, "surface release waited for platform preparation to finish");
        Check(!frameAdvancedDuringPrepare && !state->renderedDuringPrepare,
              "worker rendered before platform preparation returned");
        Check(state->Count("release") == 1, "preparation-race release did not run exactly once");
        renderer.Stop();
        CheckThreads(*state, false);
    }

    void TestSameIdReplacementDuringUi(SDL_Window *window)
    {
        TakeWakeEvents();
        auto state = std::make_shared<BackendState>();
        SdlRenderThread *current = nullptr;
        bool replaced = false;
        bool ran = false;
        SdlRenderThread renderer(std::make_unique<FakeBackend>(state), [&]
        {
            if (!ran)
            {
                ran = true;
                auto released = current->RemoveVideoCell(1);
                replaced = released.wait_for(0ms) == std::future_status::ready &&
                           current->UpsertVideoCell(Cell(1));
            }
        });
        current = &renderer;
        Start(renderer, window, true);
        Check(renderer.UpsertVideoCell(Cell(1)), "could not add replacement video cell");
        PumpUntil(renderer, [&] { return state->Count("present") == 2; });
        Check(replaced, "same-id replacement failed in UI callback");
        Check(!state->compositedReleasedSurface, "same-id replacement used the previous frame's surface");
        Check(state->Count("ensure") == 2 && state->Count("video") == 2,
              "replacement cell was not created and rendered in the following frame");
        renderer.Stop();
    }

    void TestCallerThreadGuards(SDL_Window *window)
    {
        TakeWakeEvents();
        auto state = std::make_shared<BackendState>();
        SdlRenderThread renderer(std::make_unique<FakeBackend>(state));
        bool startedOffThread = true;
        std::string error;
        std::thread starter([&]
        {
            startedOffThread = renderer.StartOnCallerThread(window, Metrics(), &error);
        });
        starter.join();
        Check(!startedOffThread && !error.empty(), "caller mode accepted a non-main starting thread");
        Check(state->Calls().empty(), "wrong-thread start called the backend");

        Start(renderer, window, true);
        const auto wakes = TakeWakeEvents();
        Check(wakes.size() == 1, "thread-guard test missing initial wake");
        const auto callCount = state->Calls().size();
        UiTextureHandle texture;
        std::thread foreign([&]
        {
            renderer.ProcessEvent(wakes.front());
            SDL_Event input{};
            input.type = SDL_EVENT_KEY_DOWN;
            renderer.ProcessEvent(input);
            texture = renderer.LoadUiTexture("foreign", "unused");
            renderer.Stop();
        });
        foreign.join();
        Check(!texture.IsValid(), "wrong-thread caller texture load succeeded");
        Check(renderer.IsRunning(), "wrong-thread Stop changed running state");
        Check(state->Calls().size() == callCount, "wrong-thread operation accessed the backend");
        renderer.ProcessEvent(wakes.front());
        Check(state->Count("present") == 1, "wrong-thread event consumed the pending main-thread wake");
        renderer.Stop();
    }

    void TestCallbackConcurrentWithStop(SDL_Window *window)
    {
        TakeWakeEvents();
        auto state = std::make_shared<BackendState>();
        SdlRenderThread renderer(std::make_unique<FakeBackend>(state));
        Start(renderer, window, true);
        Check(renderer.UpsertVideoCell(Cell(1)), "could not add concurrent callback cell");
        PumpUntil(renderer, [&] { return state->Count("present") == 1; });
        const auto callback = state->Callback();
        std::atomic<bool> keepCalling{true};
        std::promise<void> firstCallback;
        auto first = firstCallback.get_future();
        std::thread callbacks([&]
        {
            callback(1);
            firstCallback.set_value();
            while (keepCalling)
            {
                callback(1);
                std::this_thread::yield();
            }
        });
        const bool callbackStarted = first.wait_for(2s) == std::future_status::ready;
        renderer.Stop();
        keepCalling = false;
        callbacks.join();
        Check(callbackStarted, "concurrent callback thread did not begin");
        const auto callCount = state->Calls().size();
        for (const auto &wake : TakeWakeEvents())
        {
            renderer.ProcessEvent(wake);
        }
        callback(1);
        Check(state->Calls().size() == callCount, "callback or wake accessed backend after concurrent Stop");
        Check(TakeWakeEvents().empty(), "callback queued a new wake after concurrent Stop completed");
    }

    void TestInitializationFailure(SDL_Window *window, bool caller)
    {
        for (int failure = 0; failure < 4; ++failure)
        {
            TakeWakeEvents();
            auto state = std::make_shared<BackendState>();
            state->failPlatform = failure == 0;
            state->throwPlatform = failure == 1;
            state->failInitialize = failure == 2;
            state->throwInitialize = failure == 3;
            SdlRenderThread renderer(std::make_unique<FakeBackend>(state));
            std::string error;
            const bool started = caller
                ? renderer.StartOnCallerThread(window, Metrics(), &error)
                : renderer.Start(window, Metrics(), &error);
            Check(!started && !renderer.IsRunning(), "initialization failure reported running");
            Check(!error.empty(), "initialization failure did not supply an error");
            renderer.Stop();
            renderer.Stop();
            Check(state->Count("platform-stop") == 1, "failed start did not clean platform exactly once");
            Check(state->Count("shutdown") == (failure >= 2 ? 1 : 0),
                  "failed start backend cleanup count was wrong");
            Check(TakeWakeEvents().empty(), "failed start queued render work");
            const auto callback = state->Callback();
            if (callback)
            {
                callback(1);
                Check(TakeWakeEvents().empty(), "failed start retained a live callback");
            }
        }
    }

    void TestCallbackDuringInitialize(SDL_Window *window, bool caller)
    {
        TakeWakeEvents();
        auto state = std::make_shared<BackendState>();
        state->notifyDuringInitialize = true;
        SdlRenderThread renderer(std::make_unique<FakeBackend>(state));
        Check(renderer.UpsertVideoCell(Cell(1)), "could not stage early callback cell");
        Start(renderer, window, caller);
        const auto wakes = TakeWakeEvents();
        Check(wakes.size() == 1, "initialization callbacks did not coalesce with initial wake");
        renderer.ProcessEvent(wakes.front());
        PumpUntil(renderer, [&] { return state->Count("present") == 1; });
        PumpFor(renderer, 30ms);
        Check(state->Count("present") == 1, "initialization callbacks caused extra frames");
        Check(state->Count("video") == 1, "initialization callback lost dirty video work");
        renderer.Stop();
    }

    void TestStopDuringUi(SDL_Window *window)
    {
        TakeWakeEvents();
        auto state = std::make_shared<BackendState>();
        SdlRenderThread *current = nullptr;
        bool releasedAfterStop = false;
        {
            SdlRenderThread renderer(std::make_unique<FakeBackend>(state), [&]
            {
                current->Stop();
                auto released = current->RemoveVideoCell(1);
                releasedAfterStop = released.wait_for(0ms) == std::future_status::ready &&
                                    state->Count("release") == 1;
            });
            current = &renderer;
            Start(renderer, window, true);
            Check(renderer.UpsertVideoCell(Cell(1)), "could not add stop-in-UI video cell");
            const auto wakes = TakeWakeEvents();
            Check(wakes.size() == 1, "stop-in-UI test missing initial wake");
            renderer.ProcessEvent(wakes.front());
            Check(!renderer.IsRunning(), "Stop from UI callback left renderer running");
            Check(!state->shutdownInsideUi, "Stop destroyed backend inside its UI call");
            Check(releasedAfterStop, "release after reentrant Stop completed before releasing surface");
            Check(state->Count("composite") == 0 && state->Count("present") == 0,
                  "frame continued after Stop in UI callback");
            Check(state->Count("shutdown") == 1, "deferred shutdown did not complete");
        }
        Check(state->Count("platform-stop") == 1, "destructor repeated platform shutdown");
        Check(state->Count("shutdown") == 1, "destructor repeated backend shutdown");
    }

    void TestStaleWakeAndCallback(SDL_Window *window)
    {
        TakeWakeEvents();
        auto state = std::make_shared<BackendState>();
        FrameReadyCallback retained;
        {
            SdlRenderThread renderer(std::make_unique<FakeBackend>(state));
            Check(renderer.UpsertVideoCell(Cell(1)), "could not stage video cell before start");
            Check(TakeWakeEvents().empty(), "pre-start cell update queued a wake");
            Start(renderer, window, true);
            const auto oldWakes = TakeWakeEvents();
            Check(oldWakes.size() == 1, "start did not queue one initial frame");
            retained = state->Callback();
            Check(static_cast<bool>(retained), "backend did not receive a frame callback");
            renderer.Stop();
            retained(1);
            renderer.RequestFrame();
            renderer.ProcessEvent(oldWakes.front());
            Check(state->Count("present") == 0, "stale event rendered after Stop");
            Check(TakeWakeEvents().empty(), "stopped renderer accepted a wake");

            Start(renderer, window, true);
            const auto newWakes = TakeWakeEvents();
            Check(newWakes.size() == 1, "restart did not queue one initial frame");
            renderer.ProcessEvent(oldWakes.front());
            Check(state->Count("present") == 0, "previous session event rendered after restart");
            renderer.ProcessEvent(newWakes.front());
            Check(state->Count("present") == 1, "current session event failed to render");
            retained(1);
            Check(TakeWakeEvents().empty(), "previous session callback woke restarted renderer");
            retained = state->Callback();
        }
        const auto callCount = state->Calls().size();
        std::thread lateCallback([&] { retained(1); });
        lateCallback.join();
        Check(state->Calls().size() == callCount, "callback used backend after destruction");
        Check(TakeWakeEvents().empty(), "callback queued work after destruction");
    }
}

int main()
{
    Deadline deadline;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        std::cerr << "FAIL: SDL_Init: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    SDL_Window *window = SDL_CreateWindow("Scheduling tests", 320, 200, SDL_WINDOW_HIDDEN);
    if (!window)
    {
        std::cerr << "FAIL: SDL_CreateWindow: " << SDL_GetError() << '\n';
        SDL_Quit();
        return EXIT_FAILURE;
    }

    int result = EXIT_SUCCESS;
    try
    {
        for (const bool caller : {true, false})
        {
            TestScheduling(window, caller);
            TestRequestDuringFrame(window, caller);
            TestInitializationFailure(window, caller);
            TestCallbackDuringInitialize(window, caller);
            std::cout << "PASS: " << (caller ? "caller" : "worker")
                      << " scheduling, thread ownership, coalescing, failure cleanup\n";
        }
        TestSurfaceRelease(window);
        TestReleaseAndTextureDuringUi(window);
        TestWorkerReleaseWithoutPump(window);
        TestWorkerReleaseDuringPlatformPreparation(window);
        TestSameIdReplacementDuringUi(window);
        TestStopDuringUi(window);
        std::cout << "PASS: caller surface release, shutdown completion, UI reentrancy\n";
        TestStaleWakeAndCallback(window);
        TestCallerThreadGuards(window);
        TestCallbackConcurrentWithStop(window);
        std::cout << "PASS: stale wake, restart, callback lifetime\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = EXIT_FAILURE;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
