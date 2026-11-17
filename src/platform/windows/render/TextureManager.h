#pragma once

#include <memory>
#include <string>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace sdlviewer
{
struct TextureInfo
{
    ID3D11ShaderResourceView *view = nullptr;
    int width = 0;
    int height = 0;
};

class TextureManager final
{
public:
    explicit TextureManager(ID3D11Device *device);
    ~TextureManager();

    TextureManager(const TextureManager &) = delete;
    TextureManager &operator=(const TextureManager &) = delete;

    const TextureInfo *GetTexture(const std::string &objName, const std::string &path);
    void ClearCache();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sdlviewer
