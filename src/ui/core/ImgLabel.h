#pragma once

#include "UiComponentBase.h"
#include "WidgetConfig.h"
#include "render/IRenderBackend.h"

#include <string>
#include <vector>

namespace sdlviewer
{
    class ImgLabel final : public UiComponentBase
    {
    public:
        ImgLabel(int id, WidgetConfig *opt, TextureLoader textureLoader, std::string imageBasePath);

        void Init() override;
        void Render() override;

        void ChangeImg(int num);

    private:
        int id_;
        TextureLoader textureLoader_;
        std::string imageBasePath_;

        std::vector<UiTextureHandle> imageList_;

        ImVec2 padding_;
        ImVec2 texSize_;
        int selectedIndex_ = 0;
    };
} // namespace sdlviewer
