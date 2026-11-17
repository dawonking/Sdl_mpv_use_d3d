#pragma once

#include "UiComponentBase.h"
#include "WidgetConfig.h"
#include "render/IRenderBackend.h"

#include <memory>
#include <string>
#include <vector>

namespace sdlviewer
{
    class UiPopupPanel final : public UiComponentBase
    {
    public:
        UiPopupPanel(int id, WidgetConfig *opt, ConfigResolver resolver, TextureLoader textureLoader, std::string imageBasePath);

        void Init() override;
        void Render() override;
        void WindowResize(int w, int h) override;
        void Arrange(float x, float y, float w, float h) override;
        ImVec2 GetPreferredSize() const override;

        void Move(float x, float y) override;
        void Resize(float w, float h) override;

        template <typename T, typename... Args>
        T *CreateChild(Args &&...args)
        {
            auto child = std::make_unique<T>(std::forward<Args>(args)...);
            T *rawPtr = child.get();
            rawPtr->SetParentComponent(this);
            rawPtr->Init();
            children_.push_back(std::move(child));
            return rawPtr;
        }

        void Open()
        {
            openRequested_ = true;
        }

        void Close()
        {
            closeRequested_ = true;
        }

        bool IsOpen() const
        {
            return isOpen_;
        }

        const std::string &GetName() const
        {
            return name_;
        }

    private:
        void InitChildren();

        ImGuiWindowFlags flags_ = 0;
        int id_;
        std::string name_;
        ConfigResolver resolveConfig_;
        std::vector<std::unique_ptr<UiComponentBase>> children_;

        ImVec4 backgroundColor_{0.0f, 0.0f, 0.0f, 0.0f};

        TextureLoader textureLoader_;
        std::string imageBasePath_;

        bool openRequested_ = false;
        bool closeRequested_ = false;
        bool isOpen_ = false;
    };
} // namespace sdlviewer
