#pragma once
#include "Assets/Textures/Texture.h"
#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>

namespace GEngine
{
    enum class FramebufferFormat { None, RGBA8, RedInteger, Depth24, Depth24Stencil8, Depth32Float };
    enum class FramebufferKind { Image2D, Cube, Array };
    enum class FramebufferBinding { ReadDraw, Read, Draw };
    enum class FramebufferErrorCode { InvalidDescription, Unsupported, Allocation, Storage, Incomplete,
        InvalidAttachment, InvalidCoordinates, InvalidOperation, InvalidView, ContextUnavailable };
    struct FramebufferError
    {
        FramebufferErrorCode code;
        const char* message;
        std::uint32_t width = 0, height = 0, samples = 0;
    };
    using FramebufferResult = std::expected<void, FramebufferError>;
    struct FrameBufferSpecification
    {
        std::uint32_t Width = 0, Height = 0, Samples = 1, Layers = 1;
        FramebufferKind Kind = FramebufferKind::Image2D;
        std::array<FramebufferFormat, 4> Colors{};
        std::uint32_t ColorCount = 0;
        FramebufferFormat Depth = FramebufferFormat::None;
        bool DepthRenderbuffer = false;
    };
    namespace FramebufferDetail { struct Backend; }
    // Owns the framebuffer and storage. Views observe this object's current
    // attachments across resize; this object must outlive all uses of its views.
    class FrameBuffer
    {
    public:
        FrameBuffer() noexcept;
        ~FrameBuffer();
        FrameBuffer(const FrameBuffer&) = delete;
        FrameBuffer& operator=(const FrameBuffer&) = delete;
        FrameBuffer(FrameBuffer&&) noexcept;
        FrameBuffer& operator=(FrameBuffer&&) noexcept;
        static std::expected<FrameBuffer, FramebufferError> Create(const FrameBufferSpecification&);
        explicit operator bool() const noexcept;
        const FrameBufferSpecification& Description() const noexcept;
        // Binding requires a live owner on its creating context thread.
        void Bind(FramebufferBinding = FramebufferBinding::ReadDraw) const;
        static void UnBind(FramebufferBinding = FramebufferBinding::ReadDraw);
        [[nodiscard]] FramebufferResult Resize(std::uint32_t width, std::uint32_t height);
        [[nodiscard]] FramebufferResult ResolveTo(const FrameBuffer& destination,
            std::uint32_t sourceAttachment = 0, std::uint32_t destinationAttachment = 0) const;
        [[nodiscard]] FramebufferResult Present() const;
        [[nodiscard]] std::expected<int, FramebufferError> ReadInteger(std::uint32_t attachment, int x, int y) const;
        [[nodiscard]] FramebufferResult ClearInteger(std::uint32_t attachment, int value) const;
        [[nodiscard]] FramebufferResult ReadColor(std::uint32_t attachment, std::span<std::byte> rgba) const;
        [[nodiscard]] std::expected<Asset::AttachmentView, FramebufferError> ColorView(std::uint32_t attachment = 0) const;
        [[nodiscard]] std::expected<Asset::AttachmentView, FramebufferError> DepthView() const;
    private:
        friend struct FramebufferDetail::Backend;
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
    };
    void ReportFramebufferError(const char* operation, const FramebufferError&);
}


