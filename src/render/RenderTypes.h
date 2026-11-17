#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

struct SDL_Window;
struct mpv_handle;

namespace sdlviewer
{
    using VideoId = std::uint32_t;

    struct PixelSize
    {
        int width;
        int height;

        bool IsValid() const noexcept
        {
            return width > 0 && height > 0;
        }

        friend bool operator==(const PixelSize &lhs, const PixelSize &rhs) noexcept
        {
            return lhs.width == rhs.width && lhs.height == rhs.height;
        }

        friend bool operator!=(const PixelSize &lhs, const PixelSize &rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    struct WindowMetrics
    {
        PixelSize logicalSize{};
        PixelSize pixelSize{};
        float pixelDensity = 1.0f;
        float displayScale = 1.0f;

        bool IsValid() const noexcept
        {
            return logicalSize.IsValid() &&
                   pixelSize.IsValid() &&
                   pixelDensity > 0.0f &&
                   displayScale > 0.0f;
        }
    };

    struct RectF
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        bool IsValid() const noexcept
        {
            return width > 0.0f && height > 0.0f;
        }
    };

    struct ClearColor
    {
        float red = 0.0F;
        float green = 0.0F;
        float blue = 0.0F;
        float alpha = 1.0F;
    };

    struct InputCaptureState
    {
        bool mouse = false;
        bool keyboard = false;
        bool textInput = false;
    };

    struct VideoCellState
    {
        VideoId id = 0;
        mpv_handle *player = nullptr;
        PixelSize surfaceSize{}; // Physical texture pixels.
        RectF destination{};     // Logical window coordinates.

        int zOrder = 0;
        bool visible = true;
        bool keepAspect = true;
        bool blockForTargetTime = false;

        bool IsValid() const noexcept
        {
            return id != 0 && player != nullptr && surfaceSize.IsValid() && destination.IsValid();
        }
    };

    struct VideoLayer
    {
        VideoId id = 0;
        PixelSize sourceSize{}; // Physical texture pixels.
        RectF destination{};    // Logical window coordinates.
        int zOrder = 0;
        bool visible = true;
        bool keepAspect = true;
    };

    enum class FrameLayerOrder
    {
        VideoThenUi,
        UiThenVideo,
    };

    class RenderStatus
    {
    public:
        static RenderStatus Ok()
        {
            return RenderStatus(true, {});
        }

        static RenderStatus Error(std::string message)
        {
            return RenderStatus(false, std::move(message));
        }

        explicit operator bool() const noexcept
        {
            return ok_;
        }

        const std::string &Message() const noexcept
        {
            return message_;
        }

    private:
        RenderStatus(bool ok, std::string message)
            : ok_(ok), message_(std::move(message))
        {
        }

        bool ok_ = false;
        std::string message_;
    };

    using FrameReadyCallback = std::function<void(VideoId)>;
    using UiFrameBuilder = std::function<void()>;

    struct RenderInitParams
    {
        SDL_Window *window = nullptr;
        WindowMetrics metrics{};
        FrameReadyCallback onFrameReady;
    };

}
