#include "gepch.h"
#include "TextureBackend.h"
#include "Core/GLContextThread.h"
#include "stb_image/stb_image.h"
#include <array>
#include <cstring>
#include <limits>
#include <new>

namespace GEngine::Asset
{
    namespace
    {
        struct Format { GLint internal; GLenum external, type; std::size_t channels, element; };
        std::expected<Format, TextureError> Convert(const TextureDesc& d)
        {
            const bool srgb = d.colorSpace == TextureColorSpace::SRGB;
            if ((d.kind != TextureKind::Image2D && d.kind != TextureKind::Cube)
                || (d.colorSpace != TextureColorSpace::SRGB && d.colorSpace != TextureColorSpace::Linear)
                || (d.mips != TextureMipIntent::None && d.mips != TextureMipIntent::Generate)
                || (d.orientation != ImageOrientation::TopLeft && d.orientation != ImageOrientation::BottomLeft)
                || (d.usage != TextureUsage::Sampled && d.usage != TextureUsage::Attachment))
                return std::unexpected(TextureError{TextureErrorCode::InvalidDescription, {}, "Invalid texture semantics"});
            switch (d.format)
            {
            case TextureFormat::R8: if (!srgb) return Format{GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1, 1}; break;
            case TextureFormat::RGB8: return Format{srgb ? GL_SRGB8 : GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 3, 1};
            case TextureFormat::RGBA8: return Format{srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 1};
            case TextureFormat::RGB16Float: if (!srgb) return Format{GL_RGB16F, GL_RGB, GL_FLOAT, 3, 4}; break;
            case TextureFormat::RGBA16Float: if (!srgb) return Format{GL_RGBA16F, GL_RGBA, GL_FLOAT, 4, 4}; break;
            case TextureFormat::Depth32Float: if (!srgb) return Format{GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 1, 4}; break;
            }
            return std::unexpected(TextureError{TextureErrorCode::InvalidDescription, {}, "Unsupported format/color-space combination"});
        }
        GLenum Target(TextureKind kind) { return kind == TextureKind::Cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D; }
        struct UploadState
        {
            GLenum target;
            GLint binding = 0, buffer = 0;
            static constexpr std::array<GLenum, 8> settings{GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
                GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_IMAGES,
                GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST};
            std::array<GLint, 8> saved{};
            explicit UploadState(GLenum value) : target(value)
            {
                glGetIntegerv(target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_BINDING_CUBE_MAP : GL_TEXTURE_BINDING_2D, &binding);
                glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                for (std::size_t i = 0; i < settings.size(); ++i)
                {
                    glGetIntegerv(settings[i], &saved[i]);
                    glPixelStorei(settings[i], i == 0 ? 1 : 0);
                }
            }
            ~UploadState()
            {
                glBindTexture(target, static_cast<GLuint>(binding));
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(buffer));
                for (std::size_t i = 0; i < settings.size(); ++i) glPixelStorei(settings[i], saved[i]);
            }
        };
    }
    struct TextureResource::Storage
    {
        GLuint name = 0;
        GLint unitLimit = 0;
        Storage() { glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &unitLimit); }
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        ~Storage()
        {
            if (!name) return;
            GLContextThread::RequireCurrent("Texture retirement");
            AssetDetail::RequireInvariant(SDL_GL_GetCurrentContext() == context);
            glDeleteTextures(1, &name);
        }
    };
    TextureResource::TextureResource() noexcept = default;
    TextureResource::~TextureResource() = default;
    TextureResource::TextureResource(TextureResource&&) noexcept = default;
    TextureResource& TextureResource::operator=(TextureResource&& other) noexcept
    {
        if (this != &other) { m_Storage = std::move(other.m_Storage); m_Desc = other.m_Desc; }
        return *this;
    }
    TextureResource::operator bool() const noexcept { return m_Storage && m_Storage->name; }

    std::expected<TextureResource, TextureError> TextureResource::Create(const TextureDesc& desc, TexturePixels pixels)
    {
        auto format = Convert(desc);
        if (!format) return std::unexpected(format.error());
        if (desc.width <= 0 || desc.height <= 0 || (desc.kind == TextureKind::Cube && desc.width != desc.height))
            return std::unexpected(TextureError{TextureErrorCode::InvalidDescription, {}, "Invalid image dimensions"});
        GLContextThread::RequireCurrent("Texture creation");
        GLint limit = 0;
        glGetIntegerv(desc.kind == TextureKind::Cube ? GL_MAX_CUBE_MAP_TEXTURE_SIZE : GL_MAX_TEXTURE_SIZE, &limit);
        if (desc.width > limit || desc.height > limit)
            return std::unexpected(TextureError{TextureErrorCode::InvalidDescription, {}, "Image exceeds context size limit"});
        const auto tight = std::size_t(desc.width) * format->channels * format->element;
        const auto stride = pixels.rowStride ? pixels.rowStride : tight;
        const std::size_t faces = desc.kind == TextureKind::Cube ? 6 : 1;
        const auto rows = std::size_t(desc.height) * faces;
        if (stride < tight || stride > (std::numeric_limits<std::size_t>::max)() / rows
            || (!pixels.bytes.empty() && pixels.bytes.size() < stride * (rows - 1) + tight))
            return std::unexpected(TextureError{TextureErrorCode::InvalidPixels, {}, "Pixel span/row stride does not cover the image"});
        std::vector<std::byte> packed;
        auto bytes = pixels.bytes;
        if (!bytes.empty() && stride != tight)
        {
            packed.resize(tight * rows);
            for (std::size_t row = 0; row < rows; ++row)
                std::memcpy(packed.data() + row * tight, bytes.data() + row * stride, tight);
            bytes = packed;
        }
        TextureResource pending;
        pending.m_Desc = desc;
        pending.m_Storage.reset(new (std::nothrow) Storage);
        if (!pending.m_Storage)
            return std::unexpected(TextureError{TextureErrorCode::Allocation, {}, "Texture owner allocation failed"});
        const auto target = Target(desc.kind);
        UploadState restore(target);
        glGenTextures(1, &pending.m_Storage->name);
        if (!pending.m_Storage->name)
            return std::unexpected(TextureError{TextureErrorCode::Allocation, {}, "Driver did not allocate a texture name"});
        glBindTexture(target, pending.m_Storage->name);
        for (std::size_t face = 0; face < faces; ++face)
        {
            const auto imageTarget = faces == 6 ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face) : target;
            const void* data = bytes.empty() ? nullptr : bytes.data() + face * tight * std::size_t(desc.height);
            glTexImage2D(imageTarget, 0, format->internal, desc.width, desc.height, 0, format->external, format->type, data);
            GLint width = 0, height = 0;
            glGetTexLevelParameteriv(imageTarget, 0, GL_TEXTURE_WIDTH, &width);
            glGetTexLevelParameteriv(imageTarget, 0, GL_TEXTURE_HEIGHT, &height);
            if (width != desc.width || height != desc.height)
                return std::unexpected(TextureError{TextureErrorCode::Storage, {}, "Driver failed to allocate complete image storage"});
        }
        const bool mip = desc.mips == TextureMipIntent::Generate;
        if (mip)
        {
            glGenerateMipmap(target);
            int level = 0, side = (std::max)(desc.width, desc.height);
            while (side > 1) { side /= 2; ++level; }
            for (std::size_t face = 0; face < faces; ++face)
            {
                GLint width = 0, height = 0;
                const auto imageTarget = faces == 6 ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face) : target;
                glGetTexLevelParameteriv(imageTarget, level, GL_TEXTURE_WIDTH, &width);
                glGetTexLevelParameteriv(imageTarget, level, GL_TEXTURE_HEIGHT, &height);
                if (width != 1 || height != 1)
                    return std::unexpected(TextureError{TextureErrorCode::Storage, {}, "Driver failed to allocate the mip chain"});
            }
        }
        // Legacy texture-only callers retain these defaults. Material sampling
        // overrides them with independently cached sampler objects.
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, faces == 6 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, faces == 6 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        if (faces == 6) glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        if (GLAD_GL_EXT_texture_filter_anisotropic)
        {
            GLfloat largest = 1;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largest);
            glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, largest);
        }
        return pending;
    }

    std::expected<TextureResource, TextureError> TextureResource::Load(const std::filesystem::path& path,
        const TextureDesc& requested, const std::string& extension)
    {
        if (requested.width || requested.height)
            return std::unexpected(TextureError{TextureErrorCode::InvalidDescription, path.string(), "File loads use decoded dimensions"});
        auto format = Convert(requested);
        if (!format) return std::unexpected(format.error());
        if (requested.format == TextureFormat::Depth32Float)
            return std::unexpected(TextureError{TextureErrorCode::InvalidDescription, path.string(), "Depth images cannot be decoded as color files"});
        static constexpr std::array<const char*, 6> suffixes{"posx", "negx", "posy", "negy", "posz", "negz"};
        const std::size_t faces = requested.kind == TextureKind::Cube ? 6 : 1;
        TextureDesc desc = requested;
        std::vector<std::byte> decoded;
        stbi_set_flip_vertically_on_load_thread(requested.orientation == ImageOrientation::BottomLeft);
        for (std::size_t face = 0; face < faces; ++face)
        {
            const auto source = faces == 6 ? path / (std::string(suffixes[face]) + extension) : path;
            int width = 0, height = 0, channels = 0;
            std::unique_ptr<void, decltype(&stbi_image_free)> data(format->element == 4
                ? static_cast<void*>(stbi_loadf(source.string().c_str(), &width, &height, &channels, static_cast<int>(format->channels)))
                : static_cast<void*>(stbi_load(source.string().c_str(), &width, &height, &channels, static_cast<int>(format->channels))), stbi_image_free);
            if (!data)
                return std::unexpected(TextureError{TextureErrorCode::Decode, source.string(),
                    stbi_failure_reason() ? stbi_failure_reason() : "Image decoding failed"});
            if (width <= 0 || height <= 0 || (face && (width != desc.width || height != desc.height)))
                return std::unexpected(TextureError{TextureErrorCode::Decode, source.string(), "Cube faces must have identical positive dimensions"});
            desc.width = width; desc.height = height;
            const auto size = std::size_t(width) * std::size_t(height) * format->channels * format->element;
            const auto* first = static_cast<const std::byte*>(data.get());
            decoded.insert(decoded.end(), first, first + size);
        }
        auto result = Create(desc, {decoded, 0});
        if (!result) result.error().source = path.string();
        return result;
    }

    AttachmentView AssetDetail::TextureBackend::Borrow(std::function<GLuint()> currentName, GLenum target)
    {
        GLContextThread::RequireCurrent("Attachment view creation");
        AttachmentView result;
        GLint units = 0; glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &units);
        result.m_State = std::make_shared<AttachmentState>(AttachmentState{std::move(currentName), target, SDL_GL_GetCurrentContext(), units});
        return result;
    }
    std::expected<GLuint, TextureError> AssetDetail::TextureBackend::Name(const TextureView& view)
    {
        GLContextThread::RequireCurrent("Texture view access");
        if (view.m_Image && view.m_Image->m_Storage)
        {
            AssetDetail::RequireInvariant(view.m_Image->m_Storage->context == SDL_GL_GetCurrentContext());
            return view.m_Image->m_Storage->name;
        }
        if (view.m_Attachment)
        {
            const auto& state = *view.m_Attachment.m_State;
            AssetDetail::RequireInvariant(state.context == SDL_GL_GetCurrentContext());
            if (const auto name = state.currentName()) return name;
        }
        return std::unexpected(TextureError{TextureErrorCode::InvalidView, {}, "Texture view has no live image"});
    }
    GLenum AssetDetail::TextureBackend::Target(const TextureView& view)
    {
        return view.m_Image ? ::GEngine::Asset::Target(view.m_Image->Description().kind)
            : view.m_Attachment ? view.m_Attachment.m_State->target : GL_TEXTURE_2D;
    }
    std::expected<void, TextureError> AssetDetail::TextureBackend::Bind(const TextureView& view, std::uint32_t unit)
    {
        auto name = Name(view);
        if (!name) return std::unexpected(name.error());
        const auto count = view.m_Image ? view.m_Image->m_Storage->unitLimit : view.m_Attachment.m_State->unitLimit;
        if (unit >= static_cast<std::uint32_t>(count))
            return std::unexpected(TextureError{TextureErrorCode::InvalidUnit, {}, "Texture unit exceeds the context limit"});
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(Target(view), *name);
        // A texture-only caller explicitly selects its authored defaults; never
        // inherit a sampler left on this unit by a preceding material.
        glBindSampler(unit, 0);
        return {};
    }
    std::expected<void, TextureError> TextureView::Bind(std::uint32_t unit) const
    { return AssetDetail::TextureBackend::Bind(*this, unit); }
}
