#include "gepch.h"
#include "Assets/Fonts/Font.h"
#include <sdl2/SDL_ttf.h>
#include <cmath>
#include <cstring>
#include <new>

namespace GEngine::Asset
{
    struct Font::Impl
    {
        const std::thread::id thread = std::this_thread::get_id();
        ~Impl() { AssetDetail::RequireInvariant(std::this_thread::get_id() == thread); }
        std::string source;
        using Owner = std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)>;
        std::map<int, Owner> sizes;
    };
    Font::Font() = default;
    Font::~Font() = default;
    std::expected<std::unique_ptr<Font>, TextureError> Font::Load(const std::string& source)
    {
        std::unique_ptr<Font> pending(new (std::nothrow) Font);
        if (!pending) return std::unexpected(TextureError{TextureErrorCode::Allocation, source, "Font owner allocation failed"});
        pending->m_Impl.reset(new (std::nothrow) Impl);
        if (!pending->m_Impl) return std::unexpected(TextureError{TextureErrorCode::Allocation, source, "Font storage allocation failed"});
        pending->m_Impl->source = source;
        for (const int size : {8,9,10,11,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,52,56,60,64,68,72})
        {
            Impl::Owner font(TTF_OpenFont(source.c_str(), size), TTF_CloseFont);
            if (!font) return std::unexpected(TextureError{TextureErrorCode::Font, source, TTF_GetError()});
            pending->m_Impl->sizes.emplace(size, std::move(font));
        }
        return pending;
    }
    std::expected<TextPixels, TextureError> Font::Rasterize(const std::string& text, int size,
        const std::array<float, 3>& color) const
    {
        if (!m_Impl || text.empty())
            return std::unexpected(TextureError{TextureErrorCode::Font, {}, "Text requires a loaded font and a nonempty string"});
        AssetDetail::RequireInvariant(std::this_thread::get_id() == m_Impl->thread);
        for (float channel : color) if (!std::isfinite(channel) || channel < 0 || channel > 1)
            return std::unexpected(TextureError{TextureErrorCode::Font, m_Impl->source, "Text color must be finite in [0,1]"});
        auto found = m_Impl->sizes.find(size);
        if (found == m_Impl->sizes.end())
            return std::unexpected(TextureError{TextureErrorCode::Font, m_Impl->source, "Unsupported font point size"});
        TTF_SetFontStyle(found->second.get(), TTF_STYLE_BOLD);
        const SDL_Color tint{static_cast<Uint8>(color[0]*255), static_cast<Uint8>(color[1]*255), static_cast<Uint8>(color[2]*255), 255};
        using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)>;
        Surface rendered(TTF_RenderUTF8_Blended(found->second.get(), text.c_str(), tint), SDL_FreeSurface);
        if (!rendered) return std::unexpected(TextureError{TextureErrorCode::Font, m_Impl->source, TTF_GetError()});
        Surface rgba(SDL_ConvertSurfaceFormat(rendered.get(), SDL_PIXELFORMAT_RGBA32, 0), SDL_FreeSurface);
        if (!rgba) return std::unexpected(TextureError{TextureErrorCode::Font, m_Impl->source, SDL_GetError()});
        TextPixels result;
        result.desc.width = rgba->w; result.desc.height = rgba->h;
        result.desc.colorSpace = TextureColorSpace::Linear;
        result.desc.mips = TextureMipIntent::None;
        result.desc.orientation = ImageOrientation::TopLeft;
        result.rowStride = static_cast<std::size_t>(rgba->pitch);
        result.bytes.resize(result.rowStride * static_cast<std::size_t>(rgba->h));
        std::memcpy(result.bytes.data(), rgba->pixels, result.bytes.size());
        return result;
    }
}
