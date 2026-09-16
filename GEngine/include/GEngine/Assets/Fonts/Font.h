#pragma once
#include "Assets/Textures/Texture.h"
#include <array>
#include <vector>

namespace GEngine::Asset
{
    struct TextPixels { TextureDesc desc; std::vector<std::byte> bytes; std::size_t rowStride = 0; };
    class Font final
    {
    public:
        Font();
        ~Font();
        Font(const Font&) = delete;
        Font& operator=(const Font&) = delete;
        static std::expected<std::unique_ptr<Font>, TextureError> Load(const std::string& source);
        std::expected<TextPixels, TextureError> Rasterize(const std::string& text, int pointSize,
            const std::array<float, 3>& color) const;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
