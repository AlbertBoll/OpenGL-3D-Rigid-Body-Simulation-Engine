#pragma once
#include "Core/ImageError.h"
#include <span>
#include <string_view>

namespace GEngine
{
    class Image;
    namespace UI { [[nodiscard]] ImageResult RasterImage(const ::GEngine::Image&, float width, float height); }

    class Image
    {
    public:
        Image(std::string_view path); // Retained legacy stub; maintained Ray creation uses Create.
        [[nodiscard]] static std::expected<Image, ImageError> Create(uint32_t width, uint32_t height,
            ImageFormat format, std::span<const uint8_t> data = {});
        ~Image();
        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;
        Image(Image&&) noexcept;
        Image& operator=(Image&&) noexcept;

        // Both formats retain the existing RGBA unsigned-byte source contract.
        [[nodiscard]] ImageResult ReAllocateData(std::span<const uint8_t> data);
        [[nodiscard]] ImageResult SetData(std::span<const uint8_t> data);
        [[nodiscard]] ImageResult UpdateData(std::span<const uint8_t> data);
        [[nodiscard]] ImageResult Resize(uint32_t width, uint32_t height);
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }

    private:
        friend ImageResult UI::RasterImage(const ::GEngine::Image&, float, float);
        Image(uint32_t width, uint32_t height, ImageFormat format);
        ImageError Error(ImageErrorCode, std::string_view operation, std::string_view message,
            uint64_t actualElements = 0, uint32_t backendCode = 0) const;
        ImageResult ValidateData(std::span<const uint8_t>, std::string_view operation) const;
        void Release() noexcept;
        uint32_t m_Width = 0, m_Height = 0;
        ImageFormat m_Format = ImageFormat::None;
        uint32_t m_TexID = 0;
        uint32_t m_StorageWidth = 0, m_StorageHeight = 0;
        std::uintptr_t m_Context = 0;
    };
}
