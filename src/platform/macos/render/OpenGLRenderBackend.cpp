#include "platform/macos/render/OpenGLRenderBackend.h"

#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>

#include <SDL3/SDL.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <implot.h>

#include <stb_image.h>
#include <nanosvg.h>
#include <nanosvgrast.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace sdlviewer
{
    namespace
    {
        struct Image
        {
            int width = 0;
            int height = 0;
            std::vector<unsigned char> pixels;
        };

        bool ValidSize(int width, int height, int limit)
        {
            return width > 0 &&
                   height > 0 &&
                   width <= limit &&
                   height <= limit &&
                   width <= std::numeric_limits<int>::max() / 4 &&
                   static_cast<std::size_t>(width) <=
                       std::numeric_limits<std::size_t>::max() /
                           static_cast<std::size_t>(height) / 4;
        }

        Image LoadImage(const std::string &path, int limit)
        {
            Image result;

            std::string ext =
                std::filesystem::path(path).extension().string();

            std::transform(
                ext.begin(),
                ext.end(),
                ext.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });

            if (ext == ".svg")
            {
                std::unique_ptr<NSVGimage, decltype(&nsvgDelete)> svg(
                    nsvgParseFromFile(path.c_str(), "px", 96.0f),
                    nsvgDelete);

                if (!svg ||
                    !std::isfinite(svg->width) ||
                    !std::isfinite(svg->height) ||
                    svg->width < 1.0f ||
                    svg->height < 1.0f ||
                    static_cast<double>(svg->width) > limit ||
                    static_cast<double>(svg->height) > limit)
                {
                    return {};
                }

                result.width = static_cast<int>(svg->width);
                result.height = static_cast<int>(svg->height);

                if (!ValidSize(result.width, result.height, limit))
                {
                    return {};
                }

                std::unique_ptr<
                    NSVGrasterizer,
                    decltype(&nsvgDeleteRasterizer)>
                    raster(
                        nsvgCreateRasterizer(),
                        nsvgDeleteRasterizer);

                if (!raster)
                {
                    return {};
                }

                result.pixels.resize(
                    static_cast<std::size_t>(result.width) *
                    result.height * 4);

                nsvgRasterize(
                    raster.get(),
                    svg.get(),
                    0.0f,
                    0.0f,
                    1.0f,
                    result.pixels.data(),
                    result.width,
                    result.height,
                    result.width * 4);

                return result;
            }

            int channels = 0;

            if (!stbi_info(
                    path.c_str(),
                    &result.width,
                    &result.height,
                    &channels) ||
                !ValidSize(result.width, result.height, limit))
            {
                return {};
            }

            std::unique_ptr<unsigned char, decltype(&stbi_image_free)>
                pixels(
                    stbi_load(
                        path.c_str(),
                        &result.width,
                        &result.height,
                        &channels,
                        4),
                    stbi_image_free);

            if (!pixels ||
                !ValidSize(result.width, result.height, limit))
            {
                return {};
            }

            const std::size_t size =
                static_cast<std::size_t>(result.width) *
                result.height * 4;

            result.pixels.assign(
                pixels.get(),
                pixels.get() + size);

            return result;
        }

        // UI callback 중 texture를 읽어도 기존 upload 상태를 보존한다.
        struct UploadState
        {
            GLint texture;
            GLint buffer;
            GLint alignment;
            GLint rowLength;
            GLint skipRows;
            GLint skipPixels;

            UploadState()
            {
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
                glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
                glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
                glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
                glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows);
                glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);

                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
                glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            }

            ~UploadState()
            {
                glBindTexture(
                    GL_TEXTURE_2D,
                    static_cast<GLuint>(texture));

                glBindBuffer(
                    GL_PIXEL_UNPACK_BUFFER,
                    static_cast<GLuint>(buffer));

                glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
                glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows);
                glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels);
            }
        };
    }

    OpenGLRenderBackend::~OpenGLRenderBackend()
    {
        // 내부에서 Shutdown()을 먼저 호출한다.
        ShutdownPlatform();
    }

    bool OpenGLRenderBackend::MakeCurrent() const noexcept
    {
        if (!SDL_IsMainThread() || !window_ || !glContext_)
        {
            return false;
        }

        if ((SDL_GL_GetCurrentContext() != glContext_ ||
             SDL_GL_GetCurrentWindow() != window_) &&
            !SDL_GL_MakeCurrent(window_, glContext_))
        {
            return false;
        }

        if (imguiContext_)
        {
            ImGui::SetCurrentContext(imguiContext_);
        }

        if (implotContext_)
        {
            ImPlot::SetCurrentContext(implotContext_);
        }

        return true;
    }

    void OpenGLRenderBackend::RequireReady() const
    {
        if (!rendererReady_ || !MakeCurrent())
        {
            throw std::runtime_error(
                "OpenGL backend requires its initialized main-thread context.");
        }
    }

    RenderStatus OpenGLRenderBackend::InitializePlatform(
        SDL_Window *window)
    {
        if (!SDL_IsMainThread() ||
            !window ||
            !(SDL_GetWindowFlags(window) & SDL_WINDOW_OPENGL))
        {
            return RenderStatus::Error(
                "OpenGL platform requires an OpenGL window on the SDL main thread.");
        }

        if (window_ ||
            ImGui::GetCurrentContext() ||
            ImPlot::GetCurrentContext())
        {
            return RenderStatus::Error(
                "An OpenGL platform or ImGui/ImPlot context is already active.");
        }

        const auto fail = [this](std::string message)
        {
            ShutdownPlatform();
            return RenderStatus::Error(std::move(message));
        };

        window_ = window;

        try
        {
            glContext_ = SDL_GL_CreateContext(window_);

            if (!glContext_ || !MakeCurrent())
            {
                return fail(
                    std::string("OpenGL context initialization failed: ") +
                    SDL_GetError());
            }

            IMGUI_CHECKVERSION();

            imguiContext_ = ImGui::CreateContext();

            if (!imguiContext_)
            {
                return fail("Failed to create ImGui context.");
            }

            implotContext_ = ImPlot::CreateContext();

            if (!implotContext_)
            {
                return fail("Failed to create ImPlot context.");
            }

            ImGui::StyleColorsDark();

            ImGuiIO &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            const char *fontPath =
                "resources/fonts/PretendardVariable.ttf";

            ImFont *font = nullptr;

            if (std::ifstream(fontPath, std::ios::binary).good())
            {
                font = io.Fonts->AddFontFromFileTTF(
                    fontPath,
                    20.0f,
                    nullptr,
                    io.Fonts->GetGlyphRangesKorean());
            }

            if (!font)
            {
                io.Fonts->AddFontDefault();
            }

            sdlReady_ = ImGui_ImplSDL3_InitForOpenGL(
                window_,
                glContext_);

            if (!sdlReady_)
            {
                return fail(
                    "Failed to initialize ImGui SDL3 backend.");
            }

            return RenderStatus::Ok();
        }
        catch (...)
        {
            ShutdownPlatform();
            throw;
        }
    }

    RenderStatus OpenGLRenderBackend::Initialize(
        const RenderInitParams &params)
    {
        if (!sdlReady_ ||
            rendererReady_ ||
            params.window != window_ ||
            !params.metrics.IsValid() ||
            !MakeCurrent())
        {
            return RenderStatus::Error(
                "Invalid OpenGL renderer initialization state or thread.");
        }

        if (!SDL_GL_SetSwapInterval(1))
        {
            return RenderStatus::Error(
                std::string("SDL_GL_SetSwapInterval failed: ") +
                SDL_GetError());
        }

        rendererReady_ = ImGui_ImplOpenGL3_Init("#version 150");

        if (!rendererReady_)
        {
            return RenderStatus::Error(
                "Failed to initialize ImGui OpenGL3 backend.");
        }

        metrics_ = params.metrics;

        return RenderStatus::Ok();
    }

    void OpenGLRenderBackend::PreparePlatformFrame()
    {
        RequireReady();

        ImGui_ImplSDL3_NewFrame();
        platformFramePrepared_ = true;
    }

    RenderStatus OpenGLRenderBackend::ResizeOutput(
        WindowMetrics metrics)
    {
        if (!rendererReady_ ||
            !MakeCurrent() ||
            !metrics.IsValid())
        {
            return RenderStatus::Error(
                "Invalid OpenGL resize state or metrics.");
        }

        metrics_ = metrics;

        return RenderStatus::Ok();
    }

    void OpenGLRenderBackend::BeginFrame(
        PixelSize outputSize,
        ClearColor clearColor)
    {
        RequireReady();

        if (!outputSize.IsValid())
        {
            return;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, outputSize.width, outputSize.height);

        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glClearColor(
            clearColor.red,
            clearColor.green,
            clearColor.blue,
            clearColor.alpha);

        glClear(GL_COLOR_BUFFER_BIT);
    }

    void OpenGLRenderBackend::RenderUiFrame(
        const UiFrameBuilder &buildUi)
    {
        RequireReady();

        if (!platformFramePrepared_)
        {
            throw std::runtime_error(
                "PreparePlatformFrame must precede RenderUiFrame.");
        }

        platformFramePrepared_ = false;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        try
        {
            if (buildUi)
            {
                buildUi();
            }

            ImGui::Render();
        }
        catch (...)
        {
            ImGui::EndFrame();
            throw;
        }

        const ImGuiIO &io = ImGui::GetIO();

        captureMouse_.store(io.WantCaptureMouse);
        captureKeyboard_.store(io.WantCaptureKeyboard);
        captureTextInput_.store(io.WantTextInput);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        ImGui_ImplOpenGL3_RenderDrawData(
            ImGui::GetDrawData());
    }

    RenderStatus OpenGLRenderBackend::Present()
    {
        if (!rendererReady_ || !MakeCurrent())
        {
            return RenderStatus::Error(
                "OpenGL presentation requires the main-thread context.");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (!SDL_GL_SwapWindow(window_))
        {
            return RenderStatus::Error(
                std::string("SDL_GL_SwapWindow failed: ") +
                SDL_GetError());
        }

        return RenderStatus::Ok();
    }

    void OpenGLRenderBackend::ProcessEvent(
        const SDL_Event &event)
    {
        RequireReady();
        ImGui_ImplSDL3_ProcessEvent(&event);
    }

    InputCaptureState
    OpenGLRenderBackend::GetInputCaptureState() const noexcept
    {
        return {
            captureMouse_.load(),
            captureKeyboard_.load(),
            captureTextInput_.load()
        };
    }

    UiTextureHandle OpenGLRenderBackend::LoadUiTexture(
        const std::string &objName,
        const std::string &path)
    {
        RequireReady();

        const auto existing = textures_.find(objName);

        if (existing != textures_.end())
        {
            return existing->second;
        }

        GLint limit = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);

        const Image image = LoadImage(path, limit);

        if (image.pixels.empty())
        {
            return {};
        }

        UploadState restore;

        GLuint texture = 0;
        glGenTextures(1, &texture);

        if (!texture)
        {
            return {};
        }

        glBindTexture(GL_TEXTURE_2D, texture);

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            GL_LINEAR);

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MAG_FILTER,
            GL_LINEAR);

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE);

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            image.width,
            image.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            image.pixels.data());

        if (glGetError() != GL_NO_ERROR)
        {
            glDeleteTextures(1, &texture);
            return {};
        }

        const UiTextureHandle handle{
            static_cast<ImTextureID>(texture),
            image.width,
            image.height
        };

        try
        {
            textures_.emplace(objName, handle);
        }
        catch (...)
        {
            glDeleteTextures(1, &texture);
            throw;
        }

        return handle;
    }

    RenderStatus OpenGLRenderBackend::EnsureVideoSurface(
        const VideoCellState &)
    {
        return RenderStatus::Error(
            "OpenGL video surfaces are deferred to Phase 4.");
    }

    void OpenGLRenderBackend::ReleaseVideoSurface(
        VideoId) noexcept
    {
        // Phase 3에서는 video surface를 생성하지 않는다.
    }

    RenderStatus OpenGLRenderBackend::RenderVideoFrame(
        VideoId)
    {
        return RenderStatus::Error(
            "OpenGL video rendering is deferred to Phase 4.");
    }

    void OpenGLRenderBackend::CompositeVideo(
        const std::vector<VideoLayer> &layers)
    {
        RequireReady();

        if (!layers.empty())
        {
            throw std::runtime_error(
                "OpenGL video composition is deferred to Phase 4.");
        }
    }

    void OpenGLRenderBackend::Shutdown() noexcept
    {
        if (!rendererReady_)
        {
            return;
        }

        // 유효한 GL context 없이 GPU 자원을 해제하지 않는다.
        if (!MakeCurrent())
        {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "OpenGL shutdown requires its main-thread context.");

            std::terminate();
        }

        for (const auto &entry : textures_)
        {
            const GLuint texture =
                static_cast<GLuint>(entry.second.id);

            glDeleteTextures(1, &texture);
        }

        textures_.clear();

        ImGui_ImplOpenGL3_Shutdown();

        rendererReady_ = false;
        platformFramePrepared_ = false;

        captureMouse_.store(false);
        captureKeyboard_.store(false);
        captureTextInput_.store(false);

        metrics_ = {};
    }

    void OpenGLRenderBackend::ShutdownPlatform() noexcept
    {
        if (!window_)
        {
            return;
        }

        if (!SDL_IsMainThread())
        {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "OpenGL platform destruction must run on the main thread.");

            std::terminate();
        }

        Shutdown();

        if (imguiContext_)
        {
            ImGui::SetCurrentContext(imguiContext_);
        }

        if (implotContext_)
        {
            ImPlot::SetCurrentContext(implotContext_);
        }

        if (sdlReady_)
        {
            ImGui_ImplSDL3_Shutdown();
            sdlReady_ = false;
        }

        if (implotContext_)
        {
            ImPlot::DestroyContext(implotContext_);
            implotContext_ = nullptr;
        }

        if (imguiContext_)
        {
            ImGui::DestroyContext(imguiContext_);
            imguiContext_ = nullptr;
        }

        if (glContext_)
        {
            if (!SDL_GL_DestroyContext(glContext_))
            {
                SDL_LogError(
                    SDL_LOG_CATEGORY_RENDER,
                    "SDL_GL_DestroyContext: %s",
                    SDL_GetError());
            }

            glContext_ = nullptr;
        }

        window_ = nullptr;
    }
}
