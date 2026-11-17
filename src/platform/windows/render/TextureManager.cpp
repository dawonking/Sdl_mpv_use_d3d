#include "TextureManager.h"

#include "stb_image.h"
#include "nanosvg.h"
#include "nanosvgrast.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace sdlviewer
{
    class TextureManager::Impl
    {
    public:
        explicit Impl(ID3D11Device *device) : device_(device)
        {
        }

        const TextureInfo *GetTexture(const std::string &objName, const std::string &path)
        {
            auto it = cache_.find(objName);
            if (it != cache_.end())
            {
                return &it->second.info;
            }

            Entry entry = IsSvgPath(path) ? LoadSvg(path) : LoadPng(path);
            if (entry.info.view == nullptr)
            {
                return nullptr;
            }

            cache_[objName] = std::move(entry);
            return &cache_[objName].info;
        }

        void ClearCache()
        {
            cache_.clear();
        }

    private:
        struct Entry
        {
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
            TextureInfo info;
        };

        static bool IsSvgPath(const std::string &path)
        {
            std::string extension = std::filesystem::path(path).extension().string();
            std::transform(
                extension.begin(),
                extension.end(),
                extension.begin(),
                [](unsigned char ch)
                {
                    return static_cast<char>(std::tolower(ch));
                });

            return extension == ".svg";
        }

        Entry LoadSvg(const std::string &filepath)
        {
            Entry entry;

            NSVGimage *svgImage = nsvgParseFromFile(filepath.c_str(), "px", 96.0f);
            if (svgImage == nullptr)
            {
                return entry;
            }

            const int texWidth = static_cast<int>(svgImage->width);
            const int texHeight = static_cast<int>(svgImage->height);

            if (texWidth <= 0 || texHeight <= 0)
            {
                nsvgDelete(svgImage);
                return entry;
            }

            NSVGrasterizer *rasterizer = nsvgCreateRasterizer();
            if (rasterizer == nullptr)
            {
                nsvgDelete(svgImage);
                return entry;
            }

            std::vector<unsigned char> data(
                static_cast<std::size_t>(texWidth) * texHeight * 4);

            nsvgRasterize(
                rasterizer,
                svgImage,
                0.0f,
                0.0f,
                1.0f,
                data.data(),
                texWidth,
                texHeight,
                texWidth * 4);

            nsvgDeleteRasterizer(rasterizer);
            nsvgDelete(svgImage);

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = texWidth;
            desc.Height = texHeight;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA sub{};
            sub.pSysMem = data.data();
            sub.SysMemPitch = texWidth * 4;

            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            HRESULT hr = device_->CreateTexture2D(&desc, &sub, texture.GetAddressOf());
            if (FAILED(hr) || !texture)
            {
                return entry;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            hr = device_->CreateShaderResourceView(texture.Get(), &srvDesc, entry.view.GetAddressOf());
            if (FAILED(hr))
            {
                return entry;
            }

            entry.info.view = entry.view.Get();
            entry.info.width = texWidth;
            entry.info.height = texHeight;
            return entry;
        }

        Entry LoadPng(const std::string &filepath)
        {
            Entry entry;

            int texWidth = 0;
            int texHeight = 0;
            int channels = 0;
            unsigned char *data = stbi_load(filepath.c_str(), &texWidth, &texHeight, &channels, 4);
            if (data == nullptr)
            {
                return entry;
            }

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = texWidth;
            desc.Height = texHeight;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA sub{};
            sub.pSysMem = data;
            sub.SysMemPitch = texWidth * 4;

            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            HRESULT hr = device_->CreateTexture2D(&desc, &sub, texture.GetAddressOf());
            stbi_image_free(data);

            if (FAILED(hr) || !texture)
            {
                return entry;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            hr = device_->CreateShaderResourceView(texture.Get(), &srvDesc, entry.view.GetAddressOf());
            if (FAILED(hr))
            {
                return entry;
            }

            entry.info.view = entry.view.Get();
            entry.info.width = texWidth;
            entry.info.height = texHeight;
            return entry;
        }

        ID3D11Device *device_ = nullptr;
        std::unordered_map<std::string, Entry> cache_;
    };

    TextureManager::TextureManager(ID3D11Device *device)
        : impl_(std::make_unique<Impl>(device))
    {
    }

    TextureManager::~TextureManager() = default;

    const TextureInfo *TextureManager::GetTexture(const std::string &objName, const std::string &path)
    {
        return impl_->GetTexture(objName, path);
    }

    void TextureManager::ClearCache()
    {
        impl_->ClearCache();
    }
} // namespace sdlviewer
