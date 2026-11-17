#include "app/Application.h"
#include "app/RunApplication.h"
#include "map/IMapView.h"
#include "media/PlatformMpvOptions.h"
#include "ui/core/UiManager.h"
#include "ui/core/CustomFileList.h"
#include "ui/core/FileListManager.h"

#include <SDL3/SDL.h>
#include <mpv/client.h>
#include <implot.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sdlviewer
{
    int ApplyPlatformMpvOptions(mpv_handle *handle)
    {
        for (const auto &option : {std::pair{"vo", "null"}, {"ao", "null"}, {"hwdec", "no"}})
        {
            const int result = mpv_set_option_string(handle, option.first, option.second);
            if (result < 0)
                return result;
        }
        return 0;
    }
}

namespace
{
    using namespace sdlviewer;
    using namespace std::chrono_literals;
    namespace fs = std::filesystem;

    void Check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void CheckStatus(const RenderStatus &status, const char *message)
    {
        if (!status)
            throw std::runtime_error(std::string(message) + ": " + status.Message());
    }

    bool Near(float a, float b)
    {
        return std::abs(a - b) < 0.01f;
    }

    bool SameRect(RectF a, RectF b)
    {
        return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.width, b.width) && Near(a.height, b.height);
    }

    RectF Bounds(UiComponentBase *widget)
    {
        const ImVec2 position = widget->GetWindowPos();
        const ImVec2 size = widget->GetSize();
        return {position.x, position.y, size.x, size.y};
    }

    struct ImGuiContextScope
    {
        ImGuiContextScope()
        {
            ImGui::CreateContext();
            ImPlot::CreateContext();
            auto &io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(1440, 810);
            io.DeltaTime = 1.0f / 60.0f;
            unsigned char *pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ImGui::NewFrame();
        }
        ~ImGuiContextScope()
        {
            ImGui::EndFrame();
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
        }
    };

    void TestFullscreenUi()
    {
        ImGuiContextScope context;
        UiManager ui;
        CheckStatus(ui.LoadJson(), "Could not load the application's UI JSON");
        ui.ResizeUi(1440, 810);
        const RectF normalViewport = ui.GetVideoViewport();
        Check(normalViewport.IsValid() && !SameRect(normalViewport, {0, 0, 1440, 810}), "Normal video viewport did not reserve UI space");

        const std::array<const char *, 5> names{"topbar", "ChSelectArea", "rightArea", "bottomArea", "LeftArea"};
        std::array<UiComponentBase *, 5> panels{};
        std::array<RectF, 5> normalBounds{};
        std::array<bool, 5> normalHidden{};
        for (size_t i = 0; i < names.size(); ++i)
        {
            panels[i] = ui.FindWidgetAs<UiComponentBase>(names[i]);
            Check(panels[i] != nullptr, "Expected root panel is missing");
            normalBounds[i] = Bounds(panels[i]);
            normalHidden[i] = panels[i]->IsHidden();
        }
        auto allHidden = [&]
        {
            for (const auto *panel : panels)
                if (!panel->IsHidden())
                    return false;
            return true;
        };

        ui.SetFullscreen(true);
        Check(ui.IsFullscreen() && SameRect(ui.GetVideoViewport(), {0, 0, 1440, 810}), "Fullscreen did not use the whole logical window");
        Check(allHidden() && !ui.IsVideoInputBlocked(720, 405), "Fullscreen initially showed panels or blocked central video input");

        ui.UpdateFullscreenHover(720, 1);
        Check(!panels[0]->IsHidden() && !panels[1]->IsHidden(), "Top hover did not show both title and channel controls");
        Check(panels[2]->IsHidden() && panels[3]->IsHidden() && panels[4]->IsHidden(), "Top hover exposed unrelated panels");
        Check(ui.IsVideoInputBlocked(720, 1), "Visible title controls did not block video dragging");
        ui.UpdateFullscreenHover(720, 405, true);
        Check(!panels[0]->IsHidden(), "A held mouse interaction hid its active panel");
        ui.UpdateFullscreenHover(720, 405);
        Check(allHidden(), "Leaving the top panel did not hide the overlay");

        ui.UpdateFullscreenHover(1439, 405);
        Check(!panels[2]->IsHidden() && ui.IsVideoInputBlocked(1439, 405), "Right hover did not expose interactive file controls");
        ui.UpdateFullscreenHover(720, 809);
        Check(allHidden(), "Leaving an active panel immediately switched to another overlay");
        ui.UpdateFullscreenHover(720, 809);
        Check(!panels[3]->IsHidden() && ui.IsVideoInputBlocked(720, 809), "Bottom hover did not expose playback controls");
        Check(SameRect(ui.GetVideoViewport(), {0, 0, 1440, 810}), "Showing an overlay shrank the fullscreen video viewport");

        ui.SetFullscreen(false);
        Check(!ui.IsFullscreen() && SameRect(ui.GetVideoViewport(), normalViewport), "Fullscreen exit did not restore the normal viewport");
        for (size_t i = 0; i < panels.size(); ++i)
        {
            Check(panels[i]->IsHidden() == normalHidden[i], "Fullscreen exit changed a panel's previous visibility");
            Check(SameRect(Bounds(panels[i]), normalBounds[i]), "Fullscreen exit changed a normal panel's placement");
        }
        const RectF left = Bounds(panels[4]);
        Check(!ui.IsVideoInputBlocked(left.x + left.width * 0.5f, left.y + left.height * 0.5f), "Thumbnail video area was treated as a blocking UI panel");

        UiComponentBase *mapArea = ui.FindWidgetAs<UiComponentBase>("MapArea");
        Check(mapArea != nullptr, "Map area is missing");
        panels[2]->Hide();
        mapArea->Hide();
        ui.SetFullscreen(true);
        ui.ResizeUi(1920, 1080);
        Check(SameRect(ui.GetVideoViewport(), {0, 0, 1920, 1080}), "Fullscreen resize kept stale video dimensions");
        ui.UpdateFullscreenHover(1919, 540);
        const RectF right = Bounds(panels[2]);
        Check(Near(right.x + right.width, 1920) && Near(right.height, 1080), "Right overlay did not follow fullscreen resize");
        ui.SetFullscreen(false);
        Check(panels[2]->IsHidden() && mapArea->IsHidden(), "Fullscreen lost a pre-existing hidden panel or map choice");
    }

    struct BackendState
    {
        std::map<VideoId, VideoCellState> cells;
        std::string order;
        std::string presentedOrder;
        int frames = 0;
        WindowMetrics metrics{};
        InputCaptureState capture{};
        bool renderUi = false;
        std::function<void()> onPresent;
        std::function<void(const SDL_Event &)> onEvent;
    };

    class FakeBackend final : public IRenderBackend
    {
    public:
        explicit FakeBackend(BackendState &state) : state_(state) {}
        RenderStatus InitializePlatform(SDL_Window *) override { return RenderStatus::Ok(); }
        void PreparePlatformFrame() override {}
        void ShutdownPlatform() noexcept override {}
        RenderStatus Initialize(const RenderInitParams &) override { return RenderStatus::Ok(); }
        void Shutdown() noexcept override {}
        RenderStatus ResizeOutput(WindowMetrics metrics) override
        {
            state_.metrics = metrics;
            return RenderStatus::Ok();
        }
        RenderStatus EnsureVideoSurface(const VideoCellState &cell) override
        {
            state_.cells[cell.id] = cell;
            return RenderStatus::Ok();
        }
        void ReleaseVideoSurface(VideoId id) noexcept override { state_.cells.erase(id); }
        RenderStatus RenderVideoFrame(VideoId) override { return RenderStatus::Ok(); }
        void BeginFrame(PixelSize, ClearColor) override { state_.order.clear(); }
        void CompositeVideo(const std::vector<VideoLayer> &) override { state_.order += 'V'; }
        void RenderUiFrame(const UiFrameBuilder &buildUi) override
        {
            state_.order += 'U';
            if (state_.renderUi)
            {
                ImGui::GetIO().DisplaySize = ImVec2(
                    static_cast<float>(state_.metrics.logicalSize.width),
                    static_cast<float>(state_.metrics.logicalSize.height));
                ImGui::NewFrame();
            }
            if (buildUi)
                buildUi();
            if (state_.renderUi)
                ImGui::Render();
        }
        RenderStatus Present() override
        {
            state_.presentedOrder = state_.order;
            ++state_.frames;
            if (state_.onPresent)
                state_.onPresent();
            return RenderStatus::Ok();
        }
        UiTextureHandle LoadUiTexture(const std::string &, const std::string &) override
        {
            return state_.renderUi ? UiTextureHandle{1, 32, 32} : UiTextureHandle{};
        }
        void ProcessEvent(const SDL_Event &event) override
        {
            if (state_.onEvent)
                state_.onEvent(event);
        }
        InputCaptureState GetInputCaptureState() const noexcept override { return state_.capture; }

    private:
        BackendState &state_;
    };

    struct MapState
    {
        bool created = false;
        bool visible = false;
        bool ready = false;
        MapViewBounds bounds{};
        std::function<void()> onReady;
    };

    class FakeMapView final : public IMapView
    {
    public:
        explicit FakeMapView(MapState &state) : state_(state) {}
        bool Create(SDL_Window *, const MapViewBounds &bounds, const std::string &, std::function<void()> onReady) override
        {
            state_.created = true;
            state_.bounds = bounds;
            state_.onReady = std::move(onReady);
            return true;
        }
        void SetBounds(const MapViewBounds &bounds) override { state_.bounds = bounds; }
        MapViewBounds GetBounds() const override { return state_.bounds; }
        void SetVisible(bool visible) override { state_.visible = visible; }
        bool IsVisible() const override { return state_.visible; }
        bool IsReady() const override { return state_.ready; }
        void SetGpsData(const std::vector<GPSDataRaw> &) override {}
        void Update(double, double, bool) override {}
        void ForwardMouseInput(const MapMouseEvent &) override {}
    private:
        MapState &state_;
    };

    void TestWindowEvents()
    {
        ImGuiContextScope context;
        ImGui::EndFrame();
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Fullscreen window event tests", 960, 640,
                             SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
        Check(window != nullptr, "Could not create the window event test window");
        const Uint32 wakeType = SDL_RegisterEvents(1);
        Check(wakeType != 0, "Could not register the window test event");
        const std::array<SDL_Rect, 2> bounds{{{90, 100, 800, 600}, {160, 120, 900, 650}}};
        BackendState state;
        MapState mapState;
        UiManager buttonLayout;
        CheckStatus(buttonLayout.LoadJson(), "Could not load window button positions");
        state.renderUi = true;
        int stage = 0;
        int barrier = 0;
        int clickPhase = 0;
        bool complete = false;
        bool closeRequested = false;
        std::string failure;
        const bool nativeWindows = std::string(SDL_GetCurrentVideoDriver()) == "windows";
        const Uint64 deadline = SDL_GetTicks() + 20000;
        auto post = [&](Uint32 type, int code = 0)
        {
            SDL_Event event{};
            event.type = type;
            event.user.code = code;
            Check(SDL_PushEvent(&event), "Could not queue a window test event");
        };
        auto key = [&](SDL_Scancode scancode, Uint32 type = SDL_EVENT_KEY_DOWN,
                       bool repeat = false, SDL_Keymod modifiers = SDL_KMOD_NONE)
        {
            SDL_Event event{};
            event.type = type;
            event.key.windowID = SDL_GetWindowID(window.get());
            event.key.scancode = scancode;
            event.key.down = type == SDL_EVENT_KEY_DOWN;
            event.key.repeat = repeat;
            event.key.mod = modifiers;
            Check(SDL_PushEvent(&event), "Could not queue the fullscreen shortcut");
        };
        auto setBounds = [&](const SDL_Rect &rect)
        {
            Check(SDL_SetWindowPosition(window.get(), rect.x, rect.y) &&
                  SDL_SetWindowSize(window.get(), rect.w, rect.h), "Could not move/resize the normal test window");
        };
        auto matches = [&](const SDL_Rect &rect)
        {
            SDL_Rect actual{};
            return SDL_GetWindowPosition(window.get(), &actual.x, &actual.y) &&
                   SDL_GetWindowSize(window.get(), &actual.w, &actual.h) &&
                   actual.x == rect.x && actual.y == rect.y && actual.w == rect.w && actual.h == rect.h &&
                   state.metrics.logicalSize.width == rect.w && state.metrics.logicalSize.height == rect.h;
        };
        auto click = [&](const char *name)
        {
            auto &io = ImGui::GetIO();
            switch (clickPhase++)
            {
            case 0:
            {
                buttonLayout.SetFullscreen((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0);
                buttonLayout.ResizeUi(state.metrics.logicalSize.width, state.metrics.logicalSize.height);
                auto *button = buttonLayout.FindWidgetAs<UiComponentBase>(name);
                Check(button != nullptr, "Window button is missing from the UI JSON");
                const RectF rect = Bounds(button);
                SDL_Event motion{};
                motion.type = SDL_EVENT_MOUSE_MOTION;
                motion.motion.windowID = SDL_GetWindowID(window.get());
                motion.motion.x = rect.x + rect.width * 0.5f;
                motion.motion.y = rect.y + rect.height * 0.5f;
                io.AddMousePosEvent(motion.motion.x, motion.motion.y);
                Check(SDL_PushEvent(&motion), "Could not expose the fullscreen title buttons");
                break;
            }
            case 1: io.AddMouseButtonEvent(0, true); break;
            case 2: io.AddMouseButtonEvent(0, false); break;
            default:
                clickPhase = 0;
                return true;
            }
            return false;
        };
        state.onEvent = [&](const SDL_Event &event)
        {
            if (event.type == wakeType && event.user.code != 0)
                barrier = event.user.code;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window.get()))
                closeRequested = true;
        };
        state.onPresent = [&]
        {
            try
            {
                if (SDL_GetTicks() >= deadline)
                {
                    SDL_Rect actual{};
                    SDL_GetWindowPosition(window.get(), &actual.x, &actual.y);
                    SDL_GetWindowSize(window.get(), &actual.w, &actual.h);
                    std::cerr << "DIAG window-timeout stage=" << stage << " flags=" << SDL_GetWindowFlags(window.get())
                              << " bounds=" << actual.x << ',' << actual.y << ',' << actual.w << ',' << actual.h
                              << " render=" << state.metrics.logicalSize.width << 'x' << state.metrics.logicalSize.height
                              << " order=" << state.presentedOrder << '\n';
                    throw std::runtime_error("Window event test timed out at stage " + std::to_string(stage));
                }
                const SDL_WindowFlags flags = SDL_GetWindowFlags(window.get());
                const bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
                const bool normalFrame = !fullscreen && state.presentedOrder == "UV";
                const bool fullscreenFrame = fullscreen && state.presentedOrder == "VU";
                switch (stage)
                {
                case 0:
                    setBounds(bounds[0]);
                    ++stage;
                    break;
                case 1:
                    if (!matches(bounds[0]) || !mapState.created) break;
                    Check(mapState.visible, "Map did not start enabled with its button");
                    state.capture.textInput = true;
                    key(SDL_SCANCODE_RETURN);
                    key(SDL_SCANCODE_RETURN, SDL_EVENT_KEY_UP);
                    post(wakeType, 1);
                    ++stage;
                    break;
                case 2:
                    if (barrier < 1) break;
                    Check(normalFrame, "Enter toggled fullscreen while text input was captured");
                    state.capture.textInput = false;
                    key(SDL_SCANCODE_ESCAPE);
                    key(SDL_SCANCODE_ESCAPE, SDL_EVENT_KEY_UP);
                    key(SDL_SCANCODE_RETURN, SDL_EVENT_KEY_DOWN, true);
                    key(SDL_SCANCODE_RETURN, SDL_EVENT_KEY_UP);
                    key(SDL_SCANCODE_KP_ENTER, SDL_EVENT_KEY_DOWN, false, SDL_KMOD_CTRL);
                    post(wakeType, 2);
                    ++stage;
                    break;
                case 3:
                    if (barrier < 2) break;
                    Check(normalFrame, "Normal Escape, repeat, key release or modified Enter toggled fullscreen");
                    key(SDL_SCANCODE_RETURN);
                    ++stage;
                    break;
                case 4:
                    if (!fullscreenFrame) break;
                    key(SDL_SCANCODE_RETURN, SDL_EVENT_KEY_UP);
                    key(SDL_SCANCODE_RETURN, SDL_EVENT_KEY_DOWN, true);
                    post(wakeType, 3);
                    key(SDL_SCANCODE_ESCAPE, SDL_EVENT_KEY_UP);
                    key(SDL_SCANCODE_ESCAPE, SDL_EVENT_KEY_DOWN, true);
                    key(SDL_SCANCODE_ESCAPE, SDL_EVENT_KEY_DOWN, false, SDL_KMOD_CTRL);
                    post(wakeType, 4);
                    ++stage;
                    break;
                case 5:
                    if (barrier < 4) break;
                    Check(fullscreenFrame, "Repeat or key release exited fullscreen");
                    Check(!mapState.visible, "Hidden fullscreen panel left the map visible");
                    mapState.ready = true;
                    std::exchange(mapState.onReady, {})();
                    Check(!mapState.visible, "Late map initialization restored stale startup visibility");
                    key(SDL_SCANCODE_KP_ENTER);
                    ++stage;
                    break;
                case 6:
                    if (!normalFrame || !matches(bounds[0]) || !mapState.visible) break;
                    setBounds(bounds[1]);
                    ++stage;
                    break;
                case 7:
                    if (!matches(bounds[1])) break;
                    key(SDL_SCANCODE_KP_ENTER);
                    ++stage;
                    break;
                case 8:
                    if (!fullscreenFrame) break;
                    state.capture.textInput = true;
                    key(SDL_SCANCODE_ESCAPE);
                    ++stage;
                    break;
                case 9:
                    if (!normalFrame || !matches(bounds[1])) break;
                    state.capture.textInput = false;
                    if (nativeWindows)
                    {
                        if (click("MaximumBtn")) ++stage;
                        break;
                    }
                    stage = 22;
                    break;
                case 10:
                    if ((flags & SDL_WINDOW_MAXIMIZED) == 0) break;
                    key(SDL_SCANCODE_RETURN);
                    ++stage;
                    break;
                case 11:
                    if (!fullscreenFrame) break;
                    key(SDL_SCANCODE_ESCAPE);
                    ++stage;
                    break;
                case 12:
                    if (!normalFrame || (flags & SDL_WINDOW_MAXIMIZED) == 0) break;
                    if (click("MaximumBtn")) ++stage;
                    break;
                case 13:
                    if (!normalFrame || (flags & SDL_WINDOW_MAXIMIZED) != 0 || !matches(bounds[1])) break;
                    key(SDL_SCANCODE_RETURN);
                    ++stage;
                    break;
                case 14:
                    if (fullscreenFrame && click("MaximumBtn")) ++stage;
                    break;
                case 15:
                    if (normalFrame && (flags & SDL_WINDOW_MAXIMIZED) != 0 && click("MaximumBtn")) ++stage;
                    break;
                case 16:
                    if (normalFrame && matches(bounds[1]) && click("MinBtn")) ++stage;
                    break;
                case 17:
                    if ((flags & SDL_WINDOW_MINIMIZED) == 0) break;
                    Check(SDL_RestoreWindow(window.get()), "Could not restore the minimized normal window");
                    ++stage;
                    break;
                case 18:
                    if (!normalFrame || (flags & SDL_WINDOW_MINIMIZED) != 0 || !matches(bounds[1])) break;
                    key(SDL_SCANCODE_RETURN);
                    ++stage;
                    break;
                case 19:
                    if (fullscreenFrame && click("MinBtn")) ++stage;
                    break;
                case 20:
                    if ((flags & SDL_WINDOW_MINIMIZED) == 0) break;
                    // RestoreWindow has no direct effect in fullscreen; emulate taskbar activation on Windows.
                    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
                    Check(SDL_RaiseWindow(window.get()), "Could not reactivate the minimized fullscreen window");
                    SDL_ResetHint(SDL_HINT_FORCE_RAISEWINDOW);
                    ++stage;
                    break;
                case 21:
                    if (!fullscreenFrame || (flags & SDL_WINDOW_MINIMIZED) != 0) break;
                    key(SDL_SCANCODE_RETURN);
                    ++stage;
                    break;
                case 22:
                    if (!complete && normalFrame && matches(bounds[1]))
                        complete = click("CloseBtn");
                    break;
                }
                post(wakeType);
            }
            catch (const std::exception &error)
            {
                failure = error.what();
                SDL_Event quit{};
                quit.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quit);
            }
        };
        char executable[] = "fullscreen_channel_tests";
        char *arguments[]{executable};
        Check(RunApplication(1, arguments, window.get(), std::make_unique<FakeBackend>(state),
                             std::make_unique<FakeMapView>(mapState), {}, ApplicationStartMode::CallerUiOnly) == 0, "RunApplication failed during the window event test");
        if (!failure.empty())
            throw std::runtime_error(failure);
        Check(complete, "RunApplication exited before completing the window event test");
        Check(closeRequested, "Close button did not use the normal window close event");
        std::cout << "PASS: real event-loop Return/KP Enter, ignored repeat/release/text input, latest window bounds restoration\n";
        std::cout << "PASS: Escape exits fullscreen with text input captured, restores bounds/maximized state, and never enters fullscreen\n";
        std::cout << "PASS: map initially enabled, fullscreen hiding, late initialization and windowed restoration\n";
        std::cout << "PASS: CloseBtn click dispatched the window close event and RunApplication returned normally\n";
        std::cout << (nativeWindows ? "PASS: native MaximumBtn/MinBtn clicks, fullscreen transitions and normal bounds restoration\n"
                                   : "NOT TESTED: native maximized restoration (run --window-events with SDL_VIDEODRIVER=windows)\n");
    }

    struct EmptyUi final : IUiFrame
    {
        void RenderUi() override {}
        void OnResize(PixelSize) override {}
    };

    class Session
    {
    public:
        Session()
            : window_(SDL_CreateWindow("Fullscreen/channel tests", 1440, 810, SDL_WINDOW_HIDDEN), SDL_DestroyWindow),
              app(ui_, std::make_unique<FakeBackend>(state))
        {
            Check(window_ != nullptr, "Could not create a hidden SDL test window");
            std::string error;
            Check(app.Initialize(window_.get(), {{1440, 810}, {1440, 810}, 1, 1}, &error, ApplicationStartMode::CallerUiOnly), "Could not initialize the caller-mode test application");
            for (int channel = 0; channel < 6; ++channel)
            {
                restarts[channel].store(0);
                firstFrames[channel].store(0);
                commandErrors[channel].store(0);
                MpvPlaybackCallbacks callbacks;
                callbacks.onSeekCompleted = [this, channel] { ++restarts[channel]; };
                callbacks.onFirstFrameDecoded = [this, channel] { ++firstFrames[channel]; };
                callbacks.onCommandError = [this, channel](int error) { commandErrors[channel].store(error); };
                const RectF home{0, 100.0f + channel * 100.0f, 160, 90};
                CheckStatus(Players().AddChannel(channel, home, std::move(callbacks)), "Could not add a real mpv channel");
                Players().SetHomeDestination(channel, home);
            }
            CheckStatus(Players().SetGridLayout(1, normalViewport, 1), "Could not set initial grid layout");
            Wait([&] { return state.cells.size() == 6; }, "Video cells did not reach the fake render backend");
        }

        MpvPlayerManager &Players() { return app.GetMpvPlayerManager(); }
        void Pump()
        {
            SDL_Event event{};
            while (SDL_PollEvent(&event))
                app.ProcessEvent(event);
        }
        void Wait(const std::function<bool()> &condition, const char *message)
        {
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            do
            {
                Pump();
                if (condition())
                    return;
                std::this_thread::sleep_for(2ms);
            } while (std::chrono::steady_clock::now() < deadline);
            throw std::runtime_error(message);
        }
        void Flush()
        {
            const int before = state.frames;
            app.PostPlaybackTask([] {});
            Wait([&] { return state.frames > before; }, "Requested frame was not rendered");
        }
        mpv_handle *Player(int index) { return state.cells.at(static_cast<VideoId>(index + 1)).player; }
        void WaitLoadedAndPause(int channels)
        {
            for (int i = 0; i < channels; ++i)
            {
                Wait([&]
                {
                    double duration = 0;
                    return mpv_get_property(Player(i), "duration", MPV_FORMAT_DOUBLE, &duration) >= 0 &&
                           duration >= 3 && restarts[i].load() > 0;
                }, "Initial playback restart did not arrive or fixture duration is below three seconds");
                Check(mpv_set_property_string(Player(i), "pause", "yes") >= 0, "Could not pause a loaded fixture");
                Players().SetVisible(i, true);
            }
            Flush();
        }
        int HitCenter(RectF rectangle)
        {
            return Players().HitTestChannel(rectangle.x + rectangle.width * 0.5f, rectangle.y + rectangle.height * 0.5f);
        }

    private:
        EmptyUi ui_;
    public:
        BackendState state;
        std::array<std::atomic<int>, 6> restarts;
        std::array<std::atomic<int>, 6> firstFrames;
        std::array<std::atomic<int>, 6> commandErrors;
    private:
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window_;
    public:
        Application app;
        const RectF normalViewport{200, 100, 800, 600};
    };

    class VideoFixture
    {
    public:
        explicit VideoFixture(const fs::path &source,
                              std::initializer_list<const char *> names = {
                                  "20261025_120000_INF_F_N.mp4", "20261025_120000_INF_I_N.mp4", "20261025_120000_INF_R_N.mp4"})
        {
            const fs::path root = fs::absolute(FULLSCREEN_TEST_TEMP_ROOT).lexically_normal();
            fs::create_directories(root);
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                directory_ = root / ("channels-" + std::to_string(stamp) + "-" + std::to_string(attempt));
                if (fs::create_directory(directory_))
                    break;
                if (attempt == 99)
                    throw std::runtime_error("Could not create an isolated MP4 fixture directory");
            }
            for (const char *name : names)
            {
                files_.push_back(directory_ / fs::u8path(name));
                fs::copy_file(source, files_.back());
            }
        }
        ~VideoFixture()
        {
            std::error_code error;
            for (const auto &file : files_)
                fs::remove(file, error);
            fs::remove(directory_, error);
        }
        std::string Path() const { return files_.front().u8string(); }
        std::string Path(const char *name) const { return (directory_ / fs::u8path(name)).u8string(); }
    private:
        fs::path directory_;
        std::vector<fs::path> files_;
    };

    void TestDeleteLoadedGroup(const fs::path &source, bool numbered)
    {
        VideoFixture fixture(source, numbered
            ? std::initializer_list<const char *>{"sample_1.mp4", "sample_2.mp4", "sample_3.mp4"}
            : std::initializer_list<const char *>{"20261025_120000_INF_F_N.mp4", "20261025_120000_INF_I_N.mp4", "20261025_120000_INF_R_N.mp4"});
        WidgetConfig config{};
        CustomFileList widget(0, &config);
        FileListManager list(&widget, nullptr);
        list.SetFolderPicker([&] { return fs::u8path(fixture.Path()).parent_path().u8string(); });
        list.OpenFileList();
        Check(list.GetFileCount() == (numbered ? 1 : 3), "Unexpected group row count");
        Session session;
        auto &players = session.Players();
        auto &controls = session.app.GetControlManager();
        controls.SetFileListManager(&list);
        MpvControlCallbacks callbacks;
        callbacks.setVideo = [&](const std::string &path) { return static_cast<bool>(players.SetVideo(path)); };
        callbacks.isFileLoaded = [&](const std::string &path) { return players.IsFileLoaded(path); };
        callbacks.stopAll = [&] { players.StopAll(); };
        controls.SetMpvControlCallbacks(std::move(callbacks));
        controls.FilelistClickAndPlay(0);
        session.WaitLoadedAndPause(3);
        controls.SetPlayState(PlayState::Pause);
        session.Wait([&] { return !players.IsVideoFlagPending(); }, "Group reset did not finish");
        CheckStatus(players.SwapChannels(0, 1), "Could not swap the group before deletion");
        const std::string companion = fixture.Path(numbered ? "sample_2.mp4" : "20261025_120000_INF_I_N.mp4");
        Check(players.IsFileLoaded(fixture.Path()) && players.IsFileLoaded(companion), "Loaded path lookup lost a swapped group channel");
        widget.SetChecked(numbered ? 0 : 1, true);
        controls.DeleteCheckedFiles();
        Check(controls.GetPlayState() == PlayState::NotLoad && controls.GetCurrentFileName().empty() &&
              list.GetCurrentIndex() == -1 && list.GetFileCount() == (numbered ? 0 : 2),
              "Deleting a representative/companion did not stop the real group");
        Check(!players.IsFileLoaded(fixture.Path()) && !players.IsFileLoaded(companion), "Stopped group retained loaded paths");
        session.Wait([&]
        {
            for (int i = 0; i < 3; ++i)
            {
                int idle = 0;
                if (mpv_get_property(session.Player(i), "idle-active", MPV_FORMAT_FLAG, &idle) < 0 || !idle)
                    return false;
            }
            return !players.IsVideoFlagPending();
        }, "Deleting the playing list item did not stop all mpv channels");
        for (int i = 0; i < 3; ++i)
            Check(!session.state.cells.at(i + 1).visible, "Deleted group left a visible video cell");
        Check(fs::exists(fs::u8path(companion)), "Playlist deletion removed the actual MP4 file");
        list.OpenFileList();
        Check(list.GetFileCount() == (numbered ? 1 : 3), "Folder refresh did not restore removed group rows");
        std::cout << "PASS: real mpv " << (numbered ? "numbered representative" : "legacy companion") << " list deletion stops all channels and preserves disk files\n";
    }

    void TestVideoFlags(const fs::path &source, bool avi)
    {
        VideoFixture fixture(source, avi
            ? std::initializer_list<const char *>{"20261025_120000_INF_3.avi"}
            : std::initializer_list<const char *>{"20261025_120000_INF_F_N.mp4", "20261025_120000_INF_I_N.mp4", "20261025_120000_INF_R_N.mp4"});
        Session session;
        auto &players = session.Players();
        const auto horizontal = VideoFlag::HorizontalFlip;
        const auto vertical = VideoFlag::VerticalFlip;
        const char *names[]{"setHFlip", "setVFlip", "activeBorder"};
        auto flag = [&](int channel, int index)
        {
            int value = -1;
            Check(mpv_get_property(session.Player(channel), names[index], MPV_FORMAT_FLAG, &value) >= 0,
                  "Could not query a custom video flag");
            return value != 0;
        };
        int supported = 0;
        if (mpv_get_property(session.Player(0), names[0], MPV_FORMAT_FLAG, &supported) == MPV_ERROR_PROPERTY_NOT_FOUND)
        {
            Check(!players.SelectChannel(0), "Unsupported mpv accepted channel selection");
            std::cout << "NOT TESTED: custom Flip/border properties are absent in this mpv build\n";
            return;
        }
        auto idle = [&]
        {
            session.Wait([&] { return !players.IsVideoFlagPending() && !players.IsTrackChangePending(); },
                         "Video flag/track replies did not finish");
        };
        auto select = [&](int channel)
        {
            const int previous = players.GetSelectedChannel();
            CheckStatus(players.SelectChannel(channel), "Could not select the channel");
            if (previous >= 0 && previous != channel)
                Check(!flag(channel, 2), "New border was enabled before the previous border reply");
            idle();
            Check(players.GetSelectedChannel() == channel, "Selection was not confirmed");
            for (int i = 0; i < 3; ++i)
                Check(flag(i, 2) == (i == channel), "Border did not match the single selected channel");
        };
        auto reset = [&]
        {
            Check(players.GetSelectedChannel() == -1 && !players.GetSelectedFlip(horizontal) && !players.GetSelectedFlip(vertical),
                  "Reset left selection or Flip UI state active");
            idle();
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    Check(!flag(i, j), "A video flag survived stop/file reset");
        };
        Check(players.GetSelectedChannel() == -1 && !players.GetSelectedFlip(horizontal) && !players.GetSelectedFlip(vertical),
              "Video selection/Flip did not start disabled");
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                Check(!flag(i, j), "mpv did not start with video flags disabled");
        Check(!players.SelectChannel(0) && !players.ToggleSelectedFlip(horizontal), "Unloaded video accepted selection/Flip");
        const std::string path = fixture.Path(avi ? "20261025_120000_INF_3.avi" : "20261025_120000_INF_F_N.mp4");
        CheckStatus(players.SetVideo(path), "Could not load the Flip fixture");
        session.WaitLoadedAndPause(3);
        reset();
        auto &controls = session.app.GetControlManager();
        controls.SetPlayState(PlayState::Playing);
        Check(!players.ToggleSelectedFlip(horizontal), "Flip without a selection was accepted");
        Check(!players.SelectChannel(5), "Unloaded channel was selected");
        CheckStatus(players.SelectChannel(0), "Could not request initial selection");
        Check(players.GetSelectedChannel() == -1 && !players.SelectChannel(1) && !players.ToggleSelectedFlip(horizontal),
              "Pending selection was committed early or accepted overlapping input");
        idle();
        CheckStatus(players.ToggleSelectedFlip(horizontal), "Horizontal Flip failed");
        Check(!players.GetSelectedFlip(horizontal), "Flip UI changed before its reply");
        idle();
        Check(flag(0, 0) && players.GetSelectedFlip(horizontal), "Horizontal Flip reply/state mismatch");
        CheckStatus(players.ToggleSelectedFlip(horizontal), "Could not disable horizontal Flip");
        idle();
        Check(!flag(0, 0) && !players.GetSelectedFlip(horizontal), "Horizontal Flip did not toggle off");
        CheckStatus(players.ToggleSelectedFlip(horizontal), "Could not re-enable horizontal Flip");
        idle();
        select(1);
        Check(!players.GetSelectedFlip(horizontal) && !players.GetSelectedFlip(vertical), "Selection reused another channel's Flip state");
        CheckStatus(players.ToggleSelectedFlip(vertical), "Vertical Flip failed");
        idle();
        Check(flag(1, 1) && players.GetSelectedFlip(vertical), "Vertical Flip reply/state mismatch");
        CheckStatus(players.ToggleSelectedFlip(vertical), "Could not disable vertical Flip");
        idle();
        Check(!flag(1, 1) && !players.GetSelectedFlip(vertical), "Vertical Flip did not toggle off");
        CheckStatus(players.ToggleSelectedFlip(vertical), "Could not re-enable vertical Flip");
        idle();
        select(0);
        Check(players.GetSelectedFlip(horizontal) && !players.GetSelectedFlip(vertical), "Reselection lost per-channel Flip state");
        auto deselect = [&](int channel)
        {
            CheckStatus(players.SelectChannel(channel), "Could not deselect the same channel");
            Check(players.GetSelectedChannel() == channel && players.IsVideoFlagPending(),
                  "Deselection was committed before its border reply");
            Check(!players.SelectChannel(channel), "Pending deselection accepted another click");
            idle();
            Check(players.GetSelectedChannel() == -1 && !players.GetSelectedFlip(horizontal) && !players.GetSelectedFlip(vertical),
                  "Deselection did not clear both Flip UI states");
            for (int i = 0; i < 3; ++i)
                Check(!flag(i, 2), "Deselection left an active border");
            Check(flag(0, 0) && flag(1, 1), "Deselection cleared the channels' saved Flip values");
            Check(!players.ToggleSelectedFlip(horizontal), "Deselected video accepted Flip input");
        };
        deselect(0);
        select(1);
        Check(!players.GetSelectedFlip(horizontal) && players.GetSelectedFlip(vertical), "Reselection did not restore vertical Flip UI");
        deselect(1);
        select(0);
        Check(players.GetSelectedFlip(horizontal) && !players.GetSelectedFlip(vertical), "Reselection did not restore horizontal Flip UI");
        controls.SetPlayState(PlayState::Pause);
        Check(!players.ToggleSelectedFlip(horizontal) && !players.SelectChannel(1), "Paused video accepted selection/Flip changes");
        Check(players.GetSelectedFlip(horizontal), "Pause cleared confirmed Flip state");
        controls.SetPlayState(PlayState::Playing);
        const RectF before = session.state.cells.at(1).destination;
        CheckStatus(players.SwapChannels(0, 1), "Selected channel swap failed");
        idle();
        session.Flush();
        Check(players.GetSelectedChannel() == 0 && players.GetSelectedFlip(horizontal) &&
              flag(0, 2) && !flag(1, 2) && flag(0, 0) && flag(1, 1), "Swap moved/reset selection or Flip state");
        Check(SameRect(before, session.state.cells.at(1).destination) == avi, "Swap used the wrong track/position policy");
        deselect(0);
        select(0);
        CheckStatus(players.SetFullscreen(true), "Could not enter fullscreen with Flip");
        CheckStatus(players.SetFullscreen(false), "Could not leave fullscreen with Flip");
        Check(players.GetSelectedChannel() == 0 && players.GetSelectedFlip(horizontal), "Fullscreen reset selection/Flip");
        CheckStatus(players.SelectChannel(1), "Could not queue selection before stop");
        players.StopAll();
        reset();
        const int beforeReload = session.restarts[0].load();
        CheckStatus(players.SetVideo(path), "Could not reload the Flip fixture");
        reset();
        session.Wait([&] { return session.restarts[0].load() > beforeReload; }, "Reload did not restart playback");
        players.SetVisible(0, true);
        select(0);
        CheckStatus(players.ToggleSelectedFlip(horizontal), "Could not queue Flip before file change");
        CheckStatus(players.SetVideo(path), "Could not change file with a pending Flip reply");
        reset();
        Check(!players.ToggleSelectedFlip(horizontal), "File change retained an active selection");
        std::cout << "PASS: " << (avi ? "AVI" : "MP4") << " real mpv selection/border, per-channel Flip, swap preservation and pending-request reset\n";
    }

    void TestMp4Channels(const fs::path &source)
    {
        VideoFixture fixture(source);
        Session session;
        auto &players = session.Players();
        Check(players.HitTestChannel(300, 200) == -1, "Unloaded channel accepted video input");
        CheckStatus(players.SetVideo(fixture.Path()), "Could not queue the MP4 channel group");
        session.WaitLoadedAndPause(3);
        Check(session.HitCenter(session.normalViewport) == 0 && players.HitTestChannel(80, 245) == 1, "Normal main/thumbnail hit testing did not match loaded channels");
        Check(players.HitTestChannel(-1, -1) == -1 && players.HitTestChannel(std::numeric_limits<float>::quiet_NaN(), 200) == -1, "Invalid video coordinates selected a channel");
        Check(!players.SwapChannels(0, 3), "An unloaded channel was swappable");
        CheckStatus(players.SwapChannels(0, 1), "First MP4 swap failed");
        Check(session.HitCenter(session.normalViewport) == 1 && players.HitTestChannel(80, 245) == 0, "MP4 swap did not move display destinations");
        CheckStatus(players.SwapChannels(0, 1), "Reverse MP4 swap failed");
        Check(session.HitCenter(session.normalViewport) == 0 && players.HitTestChannel(80, 245) == 1, "Swapping the same MP4 pair twice did not restore its order");
        CheckStatus(players.SwapChannels(0, 1), "Could not reapply the MP4 swap");
        CheckStatus(players.SwapChannels(1, 2), "Repeated MP4 swap failed");
        Check(session.HitCenter(session.normalViewport) == 2, "Repeated MP4 swap lost the current main channel");

        const RectF resized{240, 80, 960, 720};
        CheckStatus(players.SetGridLayout(1, resized, 2), "Post-swap resize failed");
        session.Flush();
        Check(session.HitCenter(resized) == 2 && SameRect(session.state.cells.at(3).destination, resized), "Resize reset the MP4 channel order");
        Check(session.state.cells.at(3).surfaceSize.width == 1920 && session.state.cells.at(3).surfaceSize.height == 1440, "Swapped main channel retained a thumbnail-sized surface");
        Check(session.state.presentedOrder == "UV", "Normal mode did not composite video above UI");

        session.app.SetFullscreen(true);
        CheckStatus(players.SetFullscreen(true), "Could not enable fullscreen channel policy");
        CheckStatus(players.SetGridLayout(1, {0, 0, 1440, 810}, 1), "Could not expand fullscreen video");
        session.Flush();
        Check(players.HitTestChannel(720, 405) == 2 && session.state.cells.at(3).visible, "Fullscreen did not retain the swapped main video");
        Check(!session.state.cells.at(1).visible && !session.state.cells.at(2).visible, "Fullscreen exposed secondary thumbnail slots");
        Check(!players.SwapChannels(2, 0), "Fullscreen accepted dragging to a hidden channel");
        Check(session.state.presentedOrder == "VU", "Fullscreen UI was not composited above video");

        const std::array<int, 3> restartsBeforeSeek{session.restarts[0].load(), session.restarts[1].load(), session.restarts[2].load()};
        auto dumpPlayback = [&](const char *stage)
        {
            for (int i = 0; i < 3; ++i)
            {
                std::cerr << "DIAG " << stage << " channel=" << i;
                std::cerr << " restarts=" << restartsBeforeSeek[i] << "->" << session.restarts[i].load()
                          << " firstFrames=" << session.firstFrames[i].load()
                          << " commandError=" << session.commandErrors[i].load();
                for (const char *property : {"time-pos", "seeking", "pause", "paused-for-cache", "estimated-frame-number", "core-idle", "eof-reached"})
                {
                    char *value = mpv_get_property_string(session.Player(i), property);
                    std::cerr << ' ' << property << '=' << (value ? value : "unavailable");
                    mpv_free(value);
                }
                std::cerr << '\n';
            }
        };
        std::array<double, 3> beforeStep{};
        CheckStatus(players.SeekSecondsAll(1.0), "Fullscreen seek could not be queued");
        try
        {
            session.Wait([&]
            {
                for (int i = 0; i < 3; ++i)
                {
                    int seeking = 1;
                    int paused = 0;
                    if (session.restarts[i].load() <= restartsBeforeSeek[i] ||
                        mpv_get_property(session.Player(i), "time-pos", MPV_FORMAT_DOUBLE, &beforeStep[i]) < 0 ||
                        !std::isfinite(beforeStep[i]) || std::abs(beforeStep[i] - 1.0) > 0.08 ||
                        mpv_get_property(session.Player(i), "seeking", MPV_FORMAT_FLAG, &seeking) < 0 || seeking != 0 ||
                        mpv_get_property(session.Player(i), "pause", MPV_FORMAT_FLAG, &paused) < 0 || paused != 1)
                        return false;
                }
                return true;
            }, "Fullscreen seek did not reach every loaded channel, including hidden slots");
        }
        catch (...)
        {
            dumpPlayback("seek-timeout");
            throw;
        }
        CheckStatus(players.StepFrameAll(1), "Fullscreen frame step could not be queued");
        try
        {
            session.Wait([&]
            {
                for (int i = 0; i < 3; ++i)
                {
                    double position = 0;
                    if (mpv_get_property(session.Player(i), "time-pos", MPV_FORMAT_DOUBLE, &position) < 0 ||
                        !std::isfinite(position) || position <= beforeStep[i] + 0.001)
                        return false;
                }
                return true;
            }, "Fullscreen frame step did not advance every loaded channel, including hidden slots");
        }
        catch (...)
        {
            std::cerr << "DIAG frame-step-baseline=" << beforeStep[0] << ',' << beforeStep[1] << ',' << beforeStep[2] << '\n';
            dumpPlayback("frame-step-timeout");
            throw;
        }

        CheckStatus(players.SetGridLayout(2, {0, 0, 1440, 810}, 1), "Fullscreen two-channel layout failed");
        Check(players.HitTestChannel(360, 405) == 2 && players.HitTestChannel(1080, 405) == 0, "Fullscreen grid ignored swapped display slots");

        session.app.SetFullscreen(false);
        CheckStatus(players.SetFullscreen(false), "Could not restore normal channel policy");
        CheckStatus(players.SetGridLayout(1, resized, 1), "Could not restore normal video layout");
        session.Flush();
        Check(session.HitCenter(resized) == 2 && session.state.cells.at(1).visible && session.state.cells.at(2).visible, "Fullscreen exit lost swapped order or secondary visibility");
        CheckStatus(players.SetVideo(fixture.Path()), "Could not reload the MP4 group");
        for (int i = 0; i < 3; ++i)
            players.SetVisible(i, true);
        Check(session.HitCenter(resized) == 0, "A newly loaded recording inherited the previous display-slot swaps");
    }

    void TestNumberedMp4Channels(const fs::path &source)
    {
        VideoFixture full(source, {"test_1.mp4", "test_2.mp4", "test_3.mp4", "test_4.mp4", "test_5.mp4", "test_6.mp4"});
        VideoFixture sparse(source, {"test_1.mp4", "test_3.MP4", "test_6.mp4", "test_7.mp4", "test_20.mp4",
                                     "test.mp4", "test.middle.mp4", "other_2.mp4", "other_6.mp4"});
        Session session;
        auto &players = session.Players();
        const auto load = [&](const std::string &representative, const std::array<std::string, 6> &expected)
        {
            std::array<int, 6> before{};
            for (int i = 0; i < 6; ++i)
                before[i] = session.restarts[i].load();
            CheckStatus(players.SetVideo(representative), "Could not load numeric group or ordinary MP4");
            for (int i = 0; i < 6; ++i)
            {
                if (expected[i].empty())
                {
                    session.Wait([&]
                    {
                        int idle = 0;
                        return mpv_get_property(session.Player(i), "idle-active", MPV_FORMAT_FLAG, &idle) >= 0 && idle == 1;
                    }, "Missing channel retained the previous recording");
                    continue;
                }
                session.Wait([&]
                {
                    char *value = mpv_get_property_string(session.Player(i), "path");
                    const std::string path = value ? value : "";
                    mpv_free(value);
                    double duration = 0;
                    std::error_code error;
                    return !path.empty() && fs::equivalent(fs::u8path(path), fs::u8path(expected[i]), error) &&
                           session.restarts[i].load() > before[i] &&
                           mpv_get_property(session.Player(i), "duration", MPV_FORMAT_DOUBLE, &duration) >= 0 && duration >= 3;
                }, "A file did not load into its numbered channel");
                Check(mpv_set_property_string(session.Player(i), "pause", "yes") >= 0, "Could not pause numeric channel");
                players.SetVisible(i, true);
            }
            session.Flush();
            for (int i = 0; i < 6; ++i)
                Check(session.state.cells.at(i + 1).visible == !expected[i].empty(), "Channel visibility did not match the selected files");
        };
        std::array<std::string, 6> all{};
        for (int i = 0; i < 6; ++i)
            all[i] = full.Path(("test_" + std::to_string(i + 1) + ".mp4").c_str());
        load(full.Path(), all);
        load(sparse.Path(), {sparse.Path(), "", sparse.Path("test_3.MP4"), "", "", sparse.Path("test_6.mp4")});
        Check(!players.SwapChannels(0, 1), "Missing numbered channel accepted a swap");
        CheckStatus(players.SwapChannels(0, 5), "Loaded sparse channel could not be swapped");
        CheckStatus(players.SwapChannels(0, 5), "Sparse swap could not be restored");
        load(sparse.Path("test.mp4"), {sparse.Path("test.mp4"), "", "", "", "", ""});
        load(sparse.Path("test.middle.mp4"), {sparse.Path("test.middle.mp4"), "", "", "", "", ""});
        Check(!players.SetVideo(sparse.Path("test_7.mp4")) && !players.SetVideo(sparse.Path("test_20.mp4")),
              "Out-of-range numeric MP4 was playable as an ordinary file");
        Check(!players.SetVideo(sparse.Path("other_2.mp4")), "Orphan channel was playable without _1");
        Check(fs::remove(fs::u8path(sparse.Path())), "Could not remove isolated representative fixture");
        Check(!players.SetVideo(sparse.Path()), "Removed representative was accepted");
    }

    void TestAviChannels(const fs::path &source)
    {
        VideoFixture fixture(source, {"20261011_145725_INF_3.avi", "ordinary_6.avi"});
        Session session;
        auto &players = session.Players();
        CheckStatus(players.SetVideo(fixture.Path()), "Could not queue the three-track AVI fixture");
        session.WaitLoadedAndPause(3);
        auto expectTracks = [&](std::array<int64_t, 3> expected)
        {
            session.Wait([&]
            {
                if (players.IsTrackChangePending())
                    return false;
                for (int i = 0; i < 3; ++i)
                {
                    int64_t track = 0;
                    if (mpv_get_property(session.Player(i), "vid", MPV_FORMAT_INT64, &track) < 0 || track != expected[i])
                        return false;
                }
                return true;
            }, "AVI swap selected the wrong video track");
        };
        expectTracks({1, 2, 3});
        CheckStatus(players.SwapChannels(0, 1), "First AVI swap failed");
        Check(players.IsTrackChangePending() && !players.SwapChannels(0, 1), "AVI accepted another swap before the first command replies were applied");
        expectTracks({2, 1, 3});
        CheckStatus(players.SwapChannels(0, 1), "Reverse AVI swap failed");
        expectTracks({1, 2, 3});
        CheckStatus(players.SwapChannels(0, 1), "Could not reapply the AVI swap");
        expectTracks({2, 1, 3});
        CheckStatus(players.SwapChannels(1, 2), "Repeated AVI swap failed");
        expectTracks({2, 3, 1});
        CheckStatus(players.SwapChannels(0, 2), "Third AVI swap failed");
        expectTracks({1, 3, 2});
        Check(session.HitCenter(session.normalViewport) == 0 && players.HitTestChannel(80, 245) == 1, "AVI track swapping unexpectedly moved channel destinations");
        CheckStatus(players.SetGridLayout(4, {200, 100, 800, 600}, 1), "AVI grid resize failed");
        expectTracks({1, 3, 2});
        CheckStatus(players.SetVideo(fixture.Path()), "Could not reload the AVI fixture");
        expectTracks({1, 2, 3});
        const int before = session.restarts[0].load();
        CheckStatus(players.SetVideo(fixture.Path("ordinary_6.avi")), "Could not load an ordinary AVI");
        session.Wait([&] { return session.restarts[0].load() > before; }, "Ordinary AVI did not decode");
        session.Wait([&]
        {
            int64_t track = 0;
            int idle1 = 0, idle2 = 0;
            return mpv_get_property(session.Player(0), "vid", MPV_FORMAT_INT64, &track) >= 0 && track == 1 &&
                   mpv_get_property(session.Player(1), "idle-active", MPV_FORMAT_FLAG, &idle1) >= 0 && idle1 == 1 &&
                   mpv_get_property(session.Player(2), "idle-active", MPV_FORMAT_FLAG, &idle2) >= 0 && idle2 == 1;
        }, "Ordinary AVI opened more than its first video track");
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--window-events")
        {
            Check(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization failed");
            TestWindowEvents();
            SDL_Quit();
            return EXIT_SUCCESS;
        }
        TestFullscreenUi();
        std::cout << "PASS: real JSON fullscreen viewport, panel hover, input blocking and restoration\n";
        if (argc > 1)
        {
            Check(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization failed");
            TestNumberedMp4Channels(fs::u8path(argv[1]));
            std::cout << "PASS: numeric MP4 groups, fixed sparse channels, standalone files and excluded suffixes\n";
            TestMp4Channels(fs::u8path(argv[1]));
            TestDeleteLoadedGroup(fs::u8path(argv[1]), false);
            TestDeleteLoadedGroup(fs::u8path(argv[1]), true);
            TestVideoFlags(fs::u8path(argv[1]), false);
            std::cout << "PASS: real MP4 manager swaps, hit tests, resize, fullscreen visibility and composition order\n";
            if (argc > 2)
            {
                TestAviChannels(fs::u8path(argv[2]));
                TestVideoFlags(fs::u8path(argv[2]), true);
                std::cout << "PASS: repeated real AVI video-track swaps\n";
            }
            else
                std::cout << "NOT TESTED: AVI track swaps (pass a three-track AVI fixture as the second argument)\n";
            SDL_Quit();
        }
        else
            std::cout << "NOT TESTED: channel playback (pass a video fixture path)\n";
    }
    catch (const std::exception &error)
    {
        SDL_Quit();
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
