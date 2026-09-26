#include "gepch.h"
#include "Core/Image.h"
#include "UI/FramebufferImage.h"
#include <imgui/imgui_internal.h>
#include <cmath>
#include <limits>
#include <utility>

namespace GEngine
{
    namespace
    {
        ImageResult Description(uint32_t width, uint32_t height, ImageFormat format, std::string_view operation)
        {
            if (!width || !height || width > uint32_t((std::numeric_limits<int32_t>::max)())
                || height > uint32_t((std::numeric_limits<int32_t>::max)()))
                return std::unexpected(ImageError{.code=ImageErrorCode::InvalidExtent, .operation=operation,
                    .message="Image dimensions must be positive and supported", .width=width, .height=height, .format=format});
            if (format != ImageFormat::RGBA && format != ImageFormat::RGBA32F)
                return std::unexpected(ImageError{.code=ImageErrorCode::InvalidFormat, .operation=operation,
                    .message="Image format is unsupported", .width=width, .height=height, .format=format});
            return {};
        }
        std::uintptr_t CurrentContext() { return reinterpret_cast<std::uintptr_t>(SDL_GL_GetCurrentContext()); }
    }

    Image::Image(std::string_view path)
    {

    }
    Image::Image(uint32_t width, uint32_t height, ImageFormat format)
        : m_Width(width), m_Height(height), m_Format(format) {}

    std::expected<Image, ImageError> Image::Create(uint32_t width, uint32_t height, ImageFormat format,
        std::span<const uint8_t> data)
    {
        if (auto result=Description(width,height,format,"Image::Create"); !result) return std::unexpected(result.error());
        Image image(width,height,format);
        if (!data.empty()) if (auto result=image.SetData(data); !result) return std::unexpected(result.error());
        return image;
    }

    Image::Image(Image&& other) noexcept
        : m_Width(std::exchange(other.m_Width,0)), m_Height(std::exchange(other.m_Height,0)),
          m_Format(std::exchange(other.m_Format,ImageFormat::None)), m_TexID(std::exchange(other.m_TexID,0)),
          m_StorageWidth(std::exchange(other.m_StorageWidth,0)), m_StorageHeight(std::exchange(other.m_StorageHeight,0)),
          m_Context(std::exchange(other.m_Context,0)) {}

    Image& Image::operator=(Image&& other) noexcept
    {
        if (this != &other) {
            Release();
            m_Width=std::exchange(other.m_Width,0);m_Height=std::exchange(other.m_Height,0);
            m_Format=std::exchange(other.m_Format,ImageFormat::None);m_TexID=std::exchange(other.m_TexID,0);
            m_StorageWidth=std::exchange(other.m_StorageWidth,0);m_StorageHeight=std::exchange(other.m_StorageHeight,0);
            m_Context=std::exchange(other.m_Context,0);
        }
        return *this;
    }
    void Image::Release() noexcept
    {
        if (!m_TexID) return;
        GLContextThread::RequireCurrent("Image retirement");
        if (CurrentContext()!=m_Context) GLContextThread::Detail::Fail("Image retirement (owning context)");
        glDeleteTextures(1,&m_TexID);m_TexID=0;m_Context=0;m_StorageWidth=m_StorageHeight=0;
    }
    Image::~Image()
    {
        GENGINE_CORE_INFO("Image destructor was called");
        Release();
    }
    ImageError Image::Error(ImageErrorCode code,std::string_view operation,std::string_view message,
        uint64_t actualElements,uint32_t backendCode) const
    {
        return {.code=code,.operation=operation,.message=message,.width=m_Width,.height=m_Height,.format=m_Format,
            .sourceWidth=m_StorageWidth,.sourceHeight=m_StorageHeight,
            .expectedElements=uint64_t(m_Width)*m_Height*4,.actualElements=actualElements,.backendCode=backendCode};
    }
    ImageResult Image::ValidateData(std::span<const uint8_t> data,std::string_view operation) const
    {
        if (auto description=Description(m_Width,m_Height,m_Format,operation); !description) return description;
        if (data.size()!=uint64_t(m_Width)*m_Height*4)
            return std::unexpected(Error(ImageErrorCode::InvalidData,operation,"RGBA byte payload size does not match image extent",data.size()));
        if (!GLContextThread::IsCurrentOwner() || (m_Context && CurrentContext()!=m_Context))
            return std::unexpected(Error(ImageErrorCode::Context,operation,"Image upload requires its owning context thread",data.size()));
        return {};
    }
    ImageResult Image::ReAllocateData(std::span<const uint8_t> data) { return SetData(data); }
    ImageResult Image::SetData(std::span<const uint8_t> data)
    {
        constexpr std::string_view operation="Image::SetData";
        if (auto valid=ValidateData(data,operation); !valid) return valid;
        if (const auto error=glGetError(); error!=GL_NO_ERROR)
            return std::unexpected(Error(ImageErrorCode::Backend,operation,"Backend error before image allocation",data.size(),error));
        struct Pending {
            GLuint name=0;
            ~Pending() { if (name) glDeleteTextures(1,&name); }
        } pending;
        glGenTextures(1,&pending.name);
        if (const auto error=glGetError(); error!=GL_NO_ERROR)
            return std::unexpected(Error(ImageErrorCode::Backend,operation,"Image texture allocation failed",data.size(),error));
        if (!pending.name)
            return std::unexpected(Error(ImageErrorCode::Allocation,operation,"Image texture allocation returned no resource",data.size()));
        glBindTexture(GL_TEXTURE_2D,pending.name);
        glTexImage2D(GL_TEXTURE_2D,0,m_Format==ImageFormat::RGBA?GL_RGBA8:GL_RGBA32F,
            m_Width,m_Height,0,GL_RGBA,GL_UNSIGNED_BYTE,data.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        if (const auto error=glGetError(); error!=GL_NO_ERROR)
            return std::unexpected(Error(ImageErrorCode::Backend,operation,"Image storage upload failed",data.size(),error));
        Release();m_TexID=std::exchange(pending.name,0);m_Context=CurrentContext();
        m_StorageWidth=m_Width;m_StorageHeight=m_Height;
        return {};
    }
    ImageResult Image::UpdateData(std::span<const uint8_t> data)
    {
        constexpr std::string_view operation="Image::UpdateData";
        if (auto valid=ValidateData(data,operation); !valid) return valid;
        if (!m_TexID || m_StorageWidth!=m_Width || m_StorageHeight!=m_Height)
            return std::unexpected(Error(ImageErrorCode::Unavailable,operation,"Image storage must be allocated for the requested extent",data.size()));
        if (const auto error=glGetError(); error!=GL_NO_ERROR)
            return std::unexpected(Error(ImageErrorCode::Backend,operation,"Backend error before image update",data.size(),error));
        glBindTexture(GL_TEXTURE_2D,m_TexID);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,m_Width,m_Height,GL_RGBA,GL_UNSIGNED_BYTE,data.data());
        if (const auto error=glGetError(); error!=GL_NO_ERROR)
            return std::unexpected(Error(ImageErrorCode::Backend,operation,"Image update failed",data.size(),error));
        return {};
    }
    ImageResult Image::Resize(uint32_t width,uint32_t height)
    {
        if (auto valid=Description(width,height,m_Format,"Image::Resize"); !valid) return valid;
        m_Width=width;m_Height=height;return {};
    }
    ImageResult UI::RasterImage(const ::GEngine::Image& image,float width,float height)
    {
        constexpr std::string_view operation="UI::RasterImage";
        if (!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0)
            return std::unexpected(image.Error(ImageErrorCode::InvalidExtent,operation,"UI image dimensions must be positive and finite"));
        if (!image.m_TexID || image.m_Width!=image.m_StorageWidth || image.m_Height!=image.m_StorageHeight)
            return std::unexpected(image.Error(ImageErrorCode::Unavailable,operation,"UI image storage is not ready"));
        if (!GLContextThread::IsCurrentOwner() || CurrentContext()!=image.m_Context
            || !ImGui::GetCurrentContext() || !GImGui->WithinFrameScope)
            return std::unexpected(image.Error(ImageErrorCode::Context,operation,"Image presentation requires an active UI frame on its owning context"));
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(image.m_TexID)),{width,height},{0.f,1.f},{1.f,0.f});
        return {};
    }
}
