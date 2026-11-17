#include "WebView2MapView.h"

#include "platform/windows/render/D3D11RenderBackend.h"

#include <Windows.h>
#include <wrl.h>
#include <wil/com.h>
#include <WebView2.h>
#include <dcomp.h>
#include <SDL3/SDL.h>

#include <charconv>
#include <iostream>
#include <vector>

using namespace Microsoft::WRL;

namespace sdlviewer
{
    namespace
    {
        std::wstring ToWide(const std::string &utf8)
        {
            if (utf8.empty())
            {
                return {};
            }
            const int sizeNeeded = MultiByteToWideChar(
                CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            std::wstring result(sizeNeeded, L'\0');
            MultiByteToWideChar(
                CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), sizeNeeded);
            return result;
        }

        std::wstring ToFixedW(double v, int precision = 7)
        {
            char buf[64];
            auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, precision);
            if (r.ec != std::errc{})
            {
                return L"0";
            }
            return std::wstring(buf, r.ptr);
        }
    } // namespace

    class WebView2MapView::Impl
    {
    public:
        explicit Impl(D3D11RenderBackend &renderBackend)
            : renderBackend_(renderBackend)
        {
        }

        ~Impl()
        {
            const D3D11RenderBackend::CompositionHandles handles = renderBackend_.GetCompositionHandles();
            if (visual_ && handles.rootVisual)
            {
                handles.rootVisual->RemoveVisual(visual_.get());
            }
            if (controller_)
            {
                controller_->Close();
            }
        }

        bool Create(::SDL_Window *window, const MapViewBounds &bounds, const std::string &url, std::function<void()> onReady)
        {
            pendingBounds_ = bounds;
            hasPendingBounds_ = true;
            pendingUrl_ = ToWide(url);

            if (window == nullptr)
            {
                return false;
            }

            const SDL_PropertiesID props = SDL_GetWindowProperties(window);
            HWND parent = static_cast<HWND>(
                SDL_GetPointerProperty(
                    props,
                    SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                    nullptr));

            if (parent == nullptr)
            {
                return false;
            }

            const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
                nullptr, nullptr, nullptr,
                Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                    [this, parent, onReady](HRESULT result, ICoreWebView2Environment *env) -> HRESULT
                    {
                        if (FAILED(result) || env == nullptr)
                        {
                            std::cout << "[MapView] CreateCoreWebView2EnvironmentWithOptions failed: 0x"
                                      << std::hex << static_cast<unsigned int>(result) << std::dec << "\n";
                            return result;
                        }

                        wil::com_ptr<ICoreWebView2Environment3> env3;
                        const HRESULT qiResult = env->QueryInterface(IID_PPV_ARGS(&env3));
                        if (FAILED(qiResult) || !env3)
                        {
                            std::cout << "[MapView] QueryInterface(ICoreWebView2Environment3) failed: 0x"
                                      << std::hex << static_cast<unsigned int>(qiResult) << std::dec << "\n";
                            return qiResult;
                        }

                        return env3->CreateCoreWebView2CompositionController(
                            parent,
                            Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                                [this, onReady](HRESULT result, ICoreWebView2CompositionController *compositionController) -> HRESULT
                                {
                                    return OnCompositionControllerCreated(result, compositionController, onReady);
                                })
                                .Get());
                    })
                    .Get());

            return SUCCEEDED(hr);
        }

        void SetBounds(const MapViewBounds &bounds)
        {
            pendingBounds_ = bounds;
            hasPendingBounds_ = true;

            if (controller_)
            {
                ApplyBounds();
            }
        }

        MapViewBounds GetBounds() const { return pendingBounds_; }

        void SetVisible(bool visible)
        {
            visible_ = visible;
            if (controller_)
            {
                controller_->put_IsVisible(visible ? TRUE : FALSE);
            }
        }

        bool IsVisible() const { return visible_; }
        bool IsReady() const { return controller_ != nullptr; }

        void SetGpsData(const std::vector<GPSDataRaw> &data)
        {
            gpsData_ = data;
            OnLoadMap();
        }

        void Update(double positionSec, double durationSec, bool isPlaying)
        {
            if (!webview_ || !isPlaying || scriptInFlight_ || durationSec <= 0.0)
            {
                return;
            }

            const Uint64 nowTick = SDL_GetTicks();
            if (nowTick - lastTick_ < kTickIntervalMs)
            {
                return;
            }
            lastTick_ = nowTick;

            const int gpsSize = static_cast<int>(gpsData_.size());
            const int pos = static_cast<int>((gpsSize * positionSec) / durationSec);

            if (pos <= 0 || pos >= gpsSize)
            {
                return;
            }

            std::wstring script = L"moveToPin(";
            script += ToFixedW(pos);
            script += L")";

            scriptInFlight_ = true;
            RunScript(script, L"moveToPin", [this](bool, const std::wstring &)
                      { scriptInFlight_ = false; });
        }

        void ForwardMouseInput(const MapMouseEvent &event)
        {
            if (!compositionController_)
            {
                return;
            }

            COREWEBVIEW2_MOUSE_EVENT_KIND kind;
            COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS keys = COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE;
            UINT32 mouseData = 0;

            switch (event.kind)
            {
            case MapMouseEventKind::Move:
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE;
                if (event.leftButtonHeld)
                {
                    keys = COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON;
                }
                break;
            case MapMouseEventKind::LeftButtonDown:
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN;
                keys = COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON;
                break;
            case MapMouseEventKind::LeftButtonUp:
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP;
                break;
            case MapMouseEventKind::Wheel:
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL;
                mouseData = static_cast<UINT32>(static_cast<int32_t>(event.wheelDelta * WHEEL_DELTA));
                break;
            default:
                return;
            }

            const POINT localPt{static_cast<LONG>(event.x), static_cast<LONG>(event.y)};
            compositionController_->SendMouseInput(kind, keys, mouseData, localPt);
        }

    private:
        HRESULT OnCompositionControllerCreated(
            HRESULT result,
            ICoreWebView2CompositionController *compositionController,
            const std::function<void()> &onReady)
        {
            if (FAILED(result) || compositionController == nullptr)
            {
                std::cout << "[MapView] CreateCoreWebView2CompositionController failed: 0x"
                          << std::hex << static_cast<unsigned int>(result) << std::dec << "\n";
                return result;
            }

            const D3D11RenderBackend::CompositionHandles handles = renderBackend_.GetCompositionHandles();
            if (!handles.device || !handles.rootVisual)
            {
                std::cout << "[MapView] DirectComposition device/root visual not ready — "
                             "map visual cannot be attached.\n";
                return E_FAIL;
            }

            compositionController_ =
                compositionController;

            compositionController_.query_to(
                &controller_);

            controller_->get_CoreWebView2(
                &webview_);

            // Keep map bounds logical; WebView2 applies the monitor DPI scale.
            if (auto controller3 =
                    controller_.try_query<
                        ICoreWebView2Controller3>())
            {
                controller3->put_ShouldDetectMonitorScaleChanges(
                    TRUE);

                controller3->put_BoundsMode(
                    COREWEBVIEW2_BOUNDS_MODE_USE_RASTERIZATION_SCALE);
            }

            wil::com_ptr<ICoreWebView2Settings> settings;
            if (webview_ && SUCCEEDED(webview_->get_Settings(&settings)) && settings)
            {
                settings->put_IsScriptEnabled(TRUE);
                settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                settings->put_IsWebMessageEnabled(TRUE);
                settings->put_AreDefaultContextMenusEnabled(FALSE);
                settings->put_AreDevToolsEnabled(FALSE);
                settings->put_IsZoomControlEnabled(FALSE);

                if (auto settings3 = settings.try_query<ICoreWebView2Settings3>())
                {
                    settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
                }
            }

            ApplyInputLockdown();

            HRESULT hr = handles.device->CreateVisual(&visual_);
            if (FAILED(hr) || !visual_)
            {
                std::cout << "[MapView] CreateVisual(map) failed: 0x"
                          << std::hex << static_cast<unsigned int>(hr) << std::dec << "\n";
                return hr;
            }

            hr = compositionController_->put_RootVisualTarget(visual_.get());
            if (FAILED(hr))
            {
                std::cout << "[MapView] put_RootVisualTarget failed: 0x"
                          << std::hex << static_cast<unsigned int>(hr) << std::dec << "\n";
                return hr;
            }

            hr = handles.rootVisual->AddVisual(visual_.get(), TRUE, handles.appVisual);
            if (FAILED(hr))
            {
                std::cout << "[MapView] AddVisual(map) failed: 0x"
                          << std::hex << static_cast<unsigned int>(hr) << std::dec << "\n";
                return hr;
            }

            if (hasPendingBounds_)
            {
                ApplyBounds();
            }

            controller_->put_IsVisible(TRUE);
            visible_ = true;

            std::cout << "[MapView] Composition visual attached OK\n";

            if (webview_ && !pendingUrl_.empty())
            {
                webview_->Navigate(pendingUrl_.c_str());
            }

            if (onReady)
            {
                onReady();
            }

            return S_OK;
        }

        void ApplyInputLockdown()
        {
            if (!controller_ || !webview_)
            {
                return;
            }

            controller_->add_AcceleratorKeyPressed(
                Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                    [](ICoreWebView2Controller *, ICoreWebView2AcceleratorKeyPressedEventArgs *args) -> HRESULT
                    {
                        args->put_Handled(TRUE);
                        return S_OK;
                    })
                    .Get(),
                &acceleratorKeyToken_);
        }

        void ApplyBounds()
        {
            if (!controller_ || !visual_)
            {
                return;
            }

            const LONG width = pendingBounds_.right - pendingBounds_.left;
            const LONG height = pendingBounds_.bottom - pendingBounds_.top;

            if (width <= 0 || height <= 0)
            {
                std::cout << "[MapView] ApplyBounds skipped: non-positive size ("
                          << width << "x" << height << ")\n";
                return;
            }

            const RECT contentBounds{0, 0, width, height};
            controller_->put_Bounds(contentBounds);

            visual_->SetOffsetX(static_cast<float>(pendingBounds_.left));
            visual_->SetOffsetY(static_cast<float>(pendingBounds_.top));

            const D3D11RenderBackend::CompositionHandles handles = renderBackend_.GetCompositionHandles();
            if (handles.device)
            {
                handles.device->Commit();
            }

            std::cout << "[MapView] ApplyBounds " << width << "x" << height
                      << " at (" << pendingBounds_.left << "," << pendingBounds_.top << ")\n";
        }

        void RunScript(const std::wstring &script, const std::wstring &tag, std::function<void(bool, const std::wstring &)> onDone)
        {
            if (!webview_)
            {
                if (onDone)
                {
                    onDone(false, L"");
                }
                return;
            }

            const HRESULT hr = webview_->ExecuteScript(
                script.c_str(),
                Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                    [tag, onDone](HRESULT errorCode, LPCWSTR resultJson) -> HRESULT
                    {
                        const bool ok = SUCCEEDED(errorCode);
                        if (!ok)
                        {
                            std::wcout << L"[" << tag << L"] failed: 0x" << std::hex << errorCode << std::dec << L"\n";
                        }
                        if (onDone)
                        {
                            onDone(ok, resultJson ? resultJson : L"");
                        }
                        return S_OK;
                    })
                    .Get());

            if (FAILED(hr))
            {
                std::wcout << L"ExecuteScript call failed: 0x" << std::hex << hr << std::dec << L"\n";
                if (onDone)
                {
                    onDone(false, L"");
                }
            }
        }

        void OnLoadMap()
        {
            if (!webview_)
            {
                std::cout << "WebView2 not ready.\n";
                return;
            }

            RunScript(L"clearPath();", L"clearPath", nullptr);

            const size_t total = gpsData_.size();
            if (total == 0)
            {
                scriptInFlight_ = true;
                return;
            }

            std::vector<double> lat, lng;
            lat.reserve(total);
            lng.reserve(total);
            for (const auto &p : gpsData_)
            {
                lat.push_back(p.latitude);
                lng.push_back(p.longitude);
            }

            double lastLat = kFallbackLatitude;
            double lastLng = kFallbackLongitude;
            for (size_t i = total; i-- > 0;)
            {
                if (lat[i] != 0.0 || lng[i] != 0.0)
                {
                    lastLat = lat[i];
                    lastLng = lng[i];
                }
                else
                {
                    lat[i] = lastLat;
                    lng[i] = lastLng;
                }
            }

            std::wstring script;
            script.reserve(total * 48 + 64);
            for (size_t i = 0; i < total; ++i)
            {
                script += L"addCoordinateToArray(";
                script += ToFixedW(lat[i]);
                script += L',';
                script += ToFixedW(lng[i]);
                script += L");";
            }
            script += L"drawPolyline();";
            script += L"setEvntOpt(" + ToFixedW(lat[0]) + L"," + ToFixedW(lng[0]) + L");";

            RunScript(script, L"drawPath", nullptr);

            scriptInFlight_ = false;
        }

        D3D11RenderBackend &renderBackend_;

        wil::com_ptr<ICoreWebView2CompositionController> compositionController_;
        wil::com_ptr<IDCompositionVisual2> visual_;
        wil::com_ptr<ICoreWebView2Controller> controller_;
        wil::com_ptr<ICoreWebView2> webview_;

        bool visible_ = true;

        MapViewBounds pendingBounds_{};
        std::wstring pendingUrl_;
        bool hasPendingBounds_ = false;

        EventRegistrationToken acceleratorKeyToken_{};

        std::vector<GPSDataRaw> gpsData_;

        static constexpr Uint64 kTickIntervalMs = 500;
        static constexpr double kFallbackLatitude = 37.40081314794344;
        static constexpr double kFallbackLongitude = 127.1076502556133;

        Uint64 lastTick_ = 0;
        bool scriptInFlight_ = false;
    };

    WebView2MapView::WebView2MapView(D3D11RenderBackend &renderBackend)
        : impl_(std::make_unique<Impl>(renderBackend))
    {
    }

    WebView2MapView::~WebView2MapView() = default;

    bool WebView2MapView::Create(
        ::SDL_Window *window,
        const MapViewBounds &bounds,
        const std::string &url,
        std::function<void()> onReady)
    {
        return impl_->Create(window, bounds, url, std::move(onReady));
    }

    void WebView2MapView::SetBounds(const MapViewBounds &bounds) { impl_->SetBounds(bounds); }
    MapViewBounds WebView2MapView::GetBounds() const { return impl_->GetBounds(); }

    void WebView2MapView::SetVisible(bool visible) { impl_->SetVisible(visible); }
    bool WebView2MapView::IsVisible() const { return impl_->IsVisible(); }
    bool WebView2MapView::IsReady() const { return impl_->IsReady(); }

    void WebView2MapView::SetGpsData(const std::vector<GPSDataRaw> &data) { impl_->SetGpsData(data); }
    void WebView2MapView::Update(double positionSec, double durationSec, bool isPlaying) { impl_->Update(positionSec, durationSec, isPlaying); }

    void WebView2MapView::ForwardMouseInput(const MapMouseEvent &event) { impl_->ForwardMouseInput(event); }
} // namespace sdlviewer
