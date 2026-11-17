#pragma once

#include "UiButtonBase.h"
#include "UiComponentBase.h"
#include "WidgetConfig.h"
#include "render/IRenderBackend.h"

#include <string>

namespace sdlviewer
{
    class ImgButton final : public UiComponentBase, public UiButtonBase
    {
    public:
        ImgButton(int id, WidgetConfig *opt, TextureLoader textureLoader, std::string imageBasePath);

        void Init() override;
        void Render() override;

        void btnClick() override;
        void btnRelease() override;
        void btnDragStart() override {}
        void btnDragEnd() override {}

    private:
        UiTextureHandle ResolveTexture(const std::string &fileName) const;

        int id_;
        TextureLoader textureLoader_;
        std::string imageBasePath_;

        UiTextureHandle normalTex_;
        UiTextureHandle hoverTex_;
        UiTextureHandle clickTex_;

        ImVec2 padding_;
        ImVec2 texSize_;
        ImU32 bgColor_ = IM_COL32(0, 0, 0, 255);
    };
} // namespace sdlviewer
