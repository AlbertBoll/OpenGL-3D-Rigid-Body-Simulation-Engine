#pragma once
#include "Assets/Fonts/Font.h"
namespace GEngine::Asset
{
    struct TextTexture
    {
        static std::expected<TextureResource, TextureError> Create(const Font& font,
            const std::string& text, int size, const std::array<float, 3>& color);
    };
}
