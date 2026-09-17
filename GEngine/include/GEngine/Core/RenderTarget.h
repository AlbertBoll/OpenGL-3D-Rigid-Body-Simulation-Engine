#pragma once
#include "Core/FrameBuffer.h"
#include "Math/Math.h"
#include <initializer_list>
#include <vector>
#include <variant>

namespace GEngine
{
    // Native-free startup error transport; existing platform arguments stay phase-owned.
    using ApplicationInitializationError = std::variant<FramebufferError, Asset::TextureError>;
    using ApplicationInitializationResult = std::expected<void, ApplicationInitializationError>;
    enum class RenderTargetTextureFormat { None, RGBA8, RED_INTEGER, DEPTH24STENCIL8, Depth = DEPTH24STENCIL8 };
    struct RenderTargetTextureSpecification
    {
        RenderTargetTextureSpecification() = default;
        RenderTargetTextureSpecification(RenderTargetTextureFormat f) : TextureFormat(f) {}
        RenderTargetTextureFormat TextureFormat = RenderTargetTextureFormat::None;
    };
    struct RenderTargetAttachmentSpecification
    {
        RenderTargetAttachmentSpecification() = default;
        RenderTargetAttachmentSpecification(std::initializer_list<RenderTargetTextureSpecification> a) : Attachments(a) {}
        std::vector<RenderTargetTextureSpecification> Attachments;
    };
    struct RenderTargetSpecification
    {
        std::uint32_t Width = 0, Height = 0, Samples = 4;
        RenderTargetAttachmentSpecification Attachments{RenderTargetTextureFormat::RGBA8, RenderTargetTextureFormat::Depth};
        bool SwapChainTarget = false;
    };
    // Semantic convenience operations over one move-only framebuffer owner.
    class FramebufferTarget
    {
    public:
        void Bind() const { m_Buffer.Bind(); }
        void UnBind() const { FrameBuffer::UnBind(); }
        Math::Vec2f GetResolution() const
        { return {static_cast<float>(m_Buffer.Description().Width), static_cast<float>(m_Buffer.Description().Height)}; }
        [[nodiscard]] FramebufferResult OnResize(std::uint32_t w, std::uint32_t h) { return m_Buffer.Resize(w, h); }
        [[nodiscard]] std::expected<Asset::AttachmentView, FramebufferError> DepthView() const { return m_Buffer.DepthView(); }
        const FrameBuffer& Buffer() const noexcept { return m_Buffer; }
        explicit operator bool() const noexcept { return bool(m_Buffer); }
    protected:
        FramebufferTarget() = default;
        ~FramebufferTarget() = default;
        FramebufferTarget(FramebufferTarget&&) noexcept = default;
        FramebufferTarget& operator=(FramebufferTarget&&) noexcept = default;
        FrameBuffer m_Buffer;
    };
    class FinalFrameBuffer final : public FramebufferTarget
    {
    public:
        static std::expected<FinalFrameBuffer, FramebufferError> Create(std::uint32_t width, std::uint32_t height);
        [[nodiscard]] std::expected<int, FramebufferError> ReadPixel(int x, int y) const { return m_Buffer.ReadInteger(1, x, y); }
        [[nodiscard]] FramebufferResult ClearMousePickAttachment(int value) const { return m_Buffer.ClearInteger(1, value); }
        void BindReadFrameBuffer() const { m_Buffer.Bind(FramebufferBinding::Read); }
        void BindDefaultDrawFrameBuffer() const { FrameBuffer::UnBind(FramebufferBinding::Draw); }
        [[nodiscard]] FramebufferResult BlitFrameBuffer() const { return m_Buffer.Present(); }
    };
    class MousePickFrameBuffer final : public FramebufferTarget
    {
    public:
        static std::expected<MousePickFrameBuffer, FramebufferError> Create(std::uint32_t width, std::uint32_t height);
        [[nodiscard]] std::expected<int, FramebufferError> ReadPixel(int x, int y) const { return m_Buffer.ReadInteger(0, x, y); }
        [[nodiscard]] FramebufferResult ClearAttachment(std::uint32_t index, int value) const { return m_Buffer.ClearInteger(index, value); }
    };
    class PointShadowFrameBuffer final : public FramebufferTarget
    {
    public:
        static std::expected<PointShadowFrameBuffer, FramebufferError> Create(std::uint32_t width, std::uint32_t height);
    };
    class CascadeShadowFrameBuffer final : public FramebufferTarget
    {
    public:
        // splitCount boundaries describe splitCount + 1 depth layers.
        static std::expected<CascadeShadowFrameBuffer, FramebufferError> Create(std::uint32_t width, std::uint32_t height, std::uint32_t splitCount);
    };

	enum class UniformType
	{
		VEC2F,
		VEC3F,
		VEC4F,
		MATRIX_2_2,
		MATRIX_3_3,
		MATRIX_4_4
	};

	template<UniformType Type>
	class UniformBufferObject
	{
	public:
		// max_size is a nonzero element count; invalid bindings/allocation failures throw.
		UniformBufferObject(unsigned int max_size, unsigned int bind_point = 0);
		// Move transfers ownership, not the context/thread that may delete the buffer.
		~UniformBufferObject();
		UniformBufferObject(const UniformBufferObject&) = delete;
		UniformBufferObject& operator=(const UniformBufferObject&) = delete;
		UniformBufferObject(UniformBufferObject&& other) noexcept;
		UniformBufferObject& operator=(UniformBufferObject&& other) noexcept;
		unsigned int GetUBO() const { return m_UBO; }
		unsigned int GetUniformTypeSize() const { return m_UniformTypeSize; }
		
	private:
		void Swap(UniformBufferObject& other) noexcept;
		unsigned int m_UBO{};
		unsigned int m_MaxSize{};
		unsigned int m_BindingPoint{};
		unsigned int m_UniformTypeSize{};
	};

	// Owns one depth/stencil renderbuffer. Resize allocates before retiring the old
	// name; a failed allocation preserves the owner and the prior GL binding.
	class RenderBufferObject
	{
	public:
		RenderBufferObject() noexcept = default;
		RenderBufferObject(unsigned int width, unsigned int height, unsigned int samples = 1);
		// Destruction/replacement require the owning current context thread.
		~RenderBufferObject();
		RenderBufferObject(const RenderBufferObject&) = delete;
		RenderBufferObject& operator=(const RenderBufferObject&) = delete;
		RenderBufferObject(RenderBufferObject&& other) noexcept;
		RenderBufferObject& operator=(RenderBufferObject&& other) noexcept;
		void Resize(unsigned int width, unsigned int height, unsigned int samples = 1);
		unsigned int GetID() const { return m_ID; }
		unsigned int GetWidth() const { return m_Width; }
		unsigned int GetHeight() const { return m_Height; }
		unsigned int GetSamples() const { return m_Samples; }
	private:
		void Swap(RenderBufferObject& other) noexcept;
		unsigned int m_ID{}, m_Width{}, m_Height{}, m_Samples{};
	};


    enum class RenderTargetSurface { Scene, Resolved };
    class RenderTarget
    {
    public:
        RenderTarget() = default;
        RenderTarget(const RenderTarget&) = delete;
        RenderTarget& operator=(const RenderTarget&) = delete;
        RenderTarget(RenderTarget&&) noexcept = default;
        RenderTarget& operator=(RenderTarget&&) noexcept = default;
        static std::expected<RenderTarget, FramebufferError> Create(const RenderTargetSpecification&);
        static std::expected<RenderTarget, FramebufferError> Create(int width, int height, unsigned samples = 16);
        static std::expected<RenderTarget, FramebufferError> Create(const Math::Vec2f& resolution);
        int GetWidth() const { return static_cast<int>(m_Render.Description().Width); }
        int GetHeight() const { return static_cast<int>(m_Render.Description().Height); }
        unsigned GetSamples() const { return m_Render.Description().Samples; }
        bool IsMultiSampled() const { return GetSamples() > 1; }
        explicit operator bool() const noexcept { return bool(m_Render); }
        const FrameBuffer& Buffer(RenderTargetSurface surface = RenderTargetSurface::Scene) const
        { return surface == RenderTargetSurface::Resolved && m_Resolved ? m_Resolved : m_Render; }
        void Bind(FramebufferBinding binding = FramebufferBinding::ReadDraw, RenderTargetSurface surface = RenderTargetSurface::Scene) const
        { Buffer(surface).Bind(binding); }
        void UnBind() const { FrameBuffer::UnBind(); }
        [[nodiscard]] FramebufferResult BindAndBlitToScreen() const;
        [[nodiscard]] FramebufferResult ClearAttachment(std::uint32_t index, int value) const { return m_Render.ClearInteger(index, value); }
        [[nodiscard]] std::expected<int, FramebufferError> ReadPixel(std::uint32_t index, int x, int y) const;
        [[nodiscard]] FramebufferResult ReadColor(std::span<std::byte> rgba) const;
        [[nodiscard]] std::expected<Asset::AttachmentView, FramebufferError> ColorView() const
        { return Buffer(RenderTargetSurface::Resolved).ColorView(); }
        [[nodiscard]] FramebufferResult OnResize(std::uint32_t width, std::uint32_t height);
        [[nodiscard]] FramebufferResult RenderSize(const Math::Vec2f& resolution = {512, 512});
        [[nodiscard]] FramebufferResult SetSamples(int samples);
    private:
        static std::expected<RenderTarget, FramebufferError> Allocate(const FrameBufferSpecification&);
        FrameBuffer m_Render, m_Resolved;
    };
}
