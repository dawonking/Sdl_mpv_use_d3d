#pragma once

#include "UiComponentBase.h"
#include "WidgetConfig.h"
#include "render/IRenderBackend.h"

#include <memory>
#include <string>
#include <vector>

namespace sdlviewer
{
    class WidgetGroup final : public UiComponentBase
    {
    public:
        WidgetGroup(int id, WidgetConfig *opt, ConfigResolver resolver, TextureLoader textureLoader, std::string imageBasePath);

        void Init() override;
        void Render() override;
        void WindowResize(int w, int h) override;
        void Arrange(float x, float y, float w, float h) override;
        ImVec2 GetPreferredSize() const override;
        UiComponentBase *FindByObjName(const std::string &targetName) override;

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

    private:
        void InitChildren();

        int id_;
        std::string name_;
        ConfigResolver resolveConfig_;
        std::vector<std::unique_ptr<UiComponentBase>> children_;

        ImGuiWindowFlags childFlags_ = 0;
        ImVec4 backgroundColor_{0.0f, 0.0f, 0.0f, 0.0f};

        TextureLoader textureLoader_;
        std::string imageBasePath_;
    };
} // namespace sdlviewer
