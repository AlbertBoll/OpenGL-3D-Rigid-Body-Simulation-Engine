#include "gepch.h"
#include "Assets/Textures/TextTexture.h"
namespace GEngine::Asset
{
    std::expected<TextureResource, TextureError> TextTexture::Create(const Font& font,
        const std::string& text, int size, const std::array<float, 3>& color)
    {
        auto pixels = font.Rasterize(text, size, color);
        if (!pixels) return std::unexpected(pixels.error());
        return TextureResource::Create(pixels->desc, {pixels->bytes, pixels->rowStride});
    }
}
