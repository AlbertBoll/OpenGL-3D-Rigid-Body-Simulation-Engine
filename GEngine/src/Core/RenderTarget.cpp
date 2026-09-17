#include "gepch.h"
#include "Core/RenderTarget.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace GEngine
{
    namespace
    {
        FrameBufferSpecification ColorDescription(unsigned w, unsigned h)
        { FrameBufferSpecification d; d.Width = w; d.Height = h; d.Colors[0] = FramebufferFormat::RGBA8;
          d.ColorCount = 1; d.Depth = FramebufferFormat::Depth24; return d; }
        FramebufferError InvalidSize()
        { return {FramebufferErrorCode::InvalidDescription, "Target dimensions or sample count are invalid"}; }
    }
    std::expected<FinalFrameBuffer, FramebufferError> FinalFrameBuffer::Create(unsigned w, unsigned h)
    {
        auto d = ColorDescription(w, h); d.Colors[1] = FramebufferFormat::RedInteger; d.ColorCount = 2;
        auto buffer = FrameBuffer::Create(d); if (!buffer) return std::unexpected(buffer.error());
        FinalFrameBuffer result; result.m_Buffer = std::move(*buffer); return result;
    }
    std::expected<MousePickFrameBuffer, FramebufferError> MousePickFrameBuffer::Create(unsigned w, unsigned h)
    {
        auto d = ColorDescription(w, h); d.Colors[0] = FramebufferFormat::RedInteger;
        auto buffer = FrameBuffer::Create(d); if (!buffer) return std::unexpected(buffer.error());
        MousePickFrameBuffer result; result.m_Buffer = std::move(*buffer); return result;
    }
    std::expected<PointShadowFrameBuffer, FramebufferError> PointShadowFrameBuffer::Create(unsigned w, unsigned h)
    {
        FrameBufferSpecification d; d.Width = w; d.Height = h; d.Layers = 6;
        d.Kind = FramebufferKind::Cube; d.Depth = FramebufferFormat::Depth32Float;
        auto buffer = FrameBuffer::Create(d); if (!buffer) return std::unexpected(buffer.error());
        PointShadowFrameBuffer result; result.m_Buffer = std::move(*buffer); return result;
    }
    std::expected<CascadeShadowFrameBuffer, FramebufferError> CascadeShadowFrameBuffer::Create(unsigned w, unsigned h, unsigned splits)
    {
        if (splits == UINT32_MAX) return std::unexpected(InvalidSize());
        FrameBufferSpecification d; d.Width = w; d.Height = h; d.Layers = splits + 1;
        d.Kind = FramebufferKind::Array; d.Depth = FramebufferFormat::Depth32Float;
        auto buffer = FrameBuffer::Create(d); if (!buffer) return std::unexpected(buffer.error());
        CascadeShadowFrameBuffer result; result.m_Buffer = std::move(*buffer); return result;
    }
    std::expected<RenderTarget, FramebufferError> RenderTarget::Allocate(const FrameBufferSpecification& d)
    {
        auto scene = FrameBuffer::Create(d); if (!scene) return std::unexpected(scene.error());
        RenderTarget result; result.m_Render = std::move(*scene);
        if (d.Samples > 1 && d.ColorCount)
        {
            auto resolved = d; resolved.Samples = 1; resolved.Depth = FramebufferFormat::None; resolved.DepthRenderbuffer = false;
            auto buffer = FrameBuffer::Create(resolved); if (!buffer) return std::unexpected(buffer.error());
            result.m_Resolved = std::move(*buffer);
        }
        return result;
    }
    std::expected<RenderTarget, FramebufferError> RenderTarget::Create(const RenderTargetSpecification& spec)
    {
        if (spec.SwapChainTarget) return std::unexpected(FramebufferError{FramebufferErrorCode::Unsupported, "Swap-chain ownership is not a framebuffer descriptor"});
        FrameBufferSpecification d; d.Width = spec.Width; d.Height = spec.Height; d.Samples = spec.Samples;
        for (const auto& a : spec.Attachments.Attachments)
        {
            if (a.TextureFormat == RenderTargetTextureFormat::Depth)
            {
                if (d.Depth != FramebufferFormat::None) return std::unexpected(InvalidSize());
                d.Depth = FramebufferFormat::Depth24Stencil8; d.DepthRenderbuffer = true;
            }
            else if (d.ColorCount < d.Colors.size() && (a.TextureFormat == RenderTargetTextureFormat::RGBA8 || a.TextureFormat == RenderTargetTextureFormat::RED_INTEGER))
                d.Colors[d.ColorCount++] = a.TextureFormat == RenderTargetTextureFormat::RGBA8 ? FramebufferFormat::RGBA8 : FramebufferFormat::RedInteger;
            else return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidDescription, "Invalid or excessive target attachment"});
        }
        return Allocate(d);
    }
    std::expected<RenderTarget, FramebufferError> RenderTarget::Create(int w, int h, unsigned samples)
    {
        if (w < 1 || h < 1) return std::unexpected(InvalidSize());
        auto d = ColorDescription(static_cast<unsigned>(w), static_cast<unsigned>(h));
        d.Samples = samples; d.Depth = FramebufferFormat::Depth24Stencil8; d.DepthRenderbuffer = true; return Allocate(d);
    }
    std::expected<RenderTarget, FramebufferError> RenderTarget::Create(const Math::Vec2f& size)
    {
        if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x < 1 || size.y < 1 || size.x > 8192 || size.y > 8192)
            return std::unexpected(InvalidSize());
        return Create(static_cast<int>(size.x), static_cast<int>(size.y));
    }
    FramebufferResult RenderTarget::BindAndBlitToScreen() const
    {
        if (!m_Render) return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidOperation, "Resolve requires a live render target"});
        if (m_Resolved) for (unsigned i = 0; i < m_Render.Description().ColorCount; ++i)
            if (auto result = m_Render.ResolveTo(m_Resolved, i, i); !result) return result;
        return {};
    }
    std::expected<int, FramebufferError> RenderTarget::ReadPixel(std::uint32_t index, int x, int y) const
    {
        if (auto result = BindAndBlitToScreen(); !result) return std::unexpected(result.error());
        return Buffer(RenderTargetSurface::Resolved).ReadInteger(index, x, y);
    }
    FramebufferResult RenderTarget::ReadColor(std::span<std::byte> rgba) const
    {
        if (auto result = BindAndBlitToScreen(); !result) return result;
        return Buffer(RenderTargetSurface::Resolved).ReadColor(0, rgba);
    }
    FramebufferResult RenderTarget::OnResize(unsigned w, unsigned h)
    {
        auto d = m_Render.Description(); d.Width = w; d.Height = h;
        auto candidate = Allocate(d); if (!candidate) return std::unexpected(candidate.error());
        RenderCounters::RecordTargetReallocation(bool(m_Render)); *this = std::move(*candidate); return {};
    }
    FramebufferResult RenderTarget::RenderSize(const Math::Vec2f& size)
    {
        if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x < 1 || size.y < 1 || size.x > 8192 || size.y > 8192)
            return std::unexpected(InvalidSize());
        return OnResize(static_cast<unsigned>(size.x), static_cast<unsigned>(size.y));
    }
    FramebufferResult RenderTarget::SetSamples(int samples)
    {
        if (samples < 1) return std::unexpected(InvalidSize());
        if (static_cast<unsigned>(samples) == GetSamples()) return {};
        auto d = m_Render.Description(); d.Samples = static_cast<unsigned>(samples);
        auto candidate = Allocate(d); if (!candidate) return std::unexpected(candidate.error());
        RenderCounters::RecordTargetReallocation(bool(m_Render)); *this = std::move(*candidate); return {};
    }

    // Unchanged standalone UBO/RBO legacy contracts: residual error-model owner Phase 66.
	RenderBufferObject::RenderBufferObject(unsigned int width, unsigned int height, unsigned int samples)
	{
		Resize(width, height, samples);
	}

	RenderBufferObject::~RenderBufferObject()
	{
		if (m_ID) glDeleteRenderbuffers(1, &m_ID);
	}

	void RenderBufferObject::Swap(RenderBufferObject& other) noexcept
	{
		std::swap(m_ID, other.m_ID);
		std::swap(m_Width, other.m_Width);
		std::swap(m_Height, other.m_Height);
		std::swap(m_Samples, other.m_Samples);
	}

	RenderBufferObject::RenderBufferObject(RenderBufferObject&& other) noexcept
	{
		Swap(other);
	}

	RenderBufferObject& RenderBufferObject::operator=(RenderBufferObject&& other) noexcept
	{
		if (this != &other)
		{
			RenderBufferObject retired(std::move(other));
			Swap(retired);
		}
		return *this;
	}

	void RenderBufferObject::Resize(unsigned int width, unsigned int height, unsigned int samples)
	{
		if (!width || !height || !samples)
			throw std::invalid_argument("Renderbuffer dimensions and sample count must be positive");
		GLint maxSize = 0, maxSamples = 0, previous = 0;
		glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxSize);
		glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
		if (maxSize <= 0 || maxSamples <= 0 || width > static_cast<unsigned int>(maxSize)
			|| height > static_cast<unsigned int>(maxSize) || samples > static_cast<unsigned int>(maxSamples))
			throw std::out_of_range("Renderbuffer dimensions or samples exceed context limits");
		glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous);
		RenderBufferObject candidate;
		try
		{
			glGenRenderbuffers(1, &candidate.m_ID);
			if (!candidate.m_ID) throw std::runtime_error("Renderbuffer name allocation failed");
			glBindRenderbuffer(GL_RENDERBUFFER, candidate.m_ID);
			if (samples > 1)
				glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
			else
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
			GLint actualWidth = 0, actualHeight = 0, actualSamples = 0, format = 0;
			glGetNamedRenderbufferParameteriv(candidate.m_ID, GL_RENDERBUFFER_WIDTH, &actualWidth);
			glGetNamedRenderbufferParameteriv(candidate.m_ID, GL_RENDERBUFFER_HEIGHT, &actualHeight);
			glGetNamedRenderbufferParameteriv(candidate.m_ID, GL_RENDERBUFFER_SAMPLES, &actualSamples);
			glGetNamedRenderbufferParameteriv(candidate.m_ID, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
			// Query the fresh allocation: unrelated pending GL errors remain observable.
			// Implementations may round multisample counts upward.
			if (actualWidth != static_cast<GLint>(width) || actualHeight != static_cast<GLint>(height)
				|| format != GL_DEPTH24_STENCIL8 || (samples > 1 ? actualSamples < static_cast<GLint>(samples) : actualSamples != 0))
				throw std::runtime_error("Renderbuffer storage allocation failed");
		}
		catch (...)
		{
			glBindRenderbuffer(GL_RENDERBUFFER, previous);
			throw; // candidate retires any generated name, even during constructor failure.
		}
		candidate.m_Width = width;
		candidate.m_Height = height;
		candidate.m_Samples = samples;
		Swap(candidate); // Only a complete allocation replaces the old owner.
	}

	template<UniformType Type>
	UniformBufferObject<Type>::UniformBufferObject(unsigned int max_size, unsigned int bind_point)
		: m_MaxSize(max_size), m_BindingPoint(bind_point)
	{
		using namespace Math;
		if constexpr (Type == UniformType::VEC2F)
		{
			m_UniformTypeSize = sizeof(Vec2f);
		}
		else if constexpr (Type == UniformType::VEC3F)
		{
			m_UniformTypeSize = sizeof(Vec3f);
		}
		else if constexpr (Type == UniformType::VEC4F)
		{
			m_UniformTypeSize = sizeof(Vec4f);
		}
		else if constexpr (Type == UniformType::MATRIX_2_2)
		{
			m_UniformTypeSize = sizeof(Mat2);
		}
		else if constexpr (Type == UniformType::MATRIX_3_3)
		{
			m_UniformTypeSize = sizeof(Mat3);
		}
		else if constexpr (Type == UniformType::MATRIX_4_4)
		{
			m_UniformTypeSize = sizeof(Mat4);
		}
		if (!max_size || !m_UniformTypeSize)
			throw std::invalid_argument("Uniform buffer element count and type must be valid");
		if (max_size > static_cast<std::size_t>((std::numeric_limits<GLsizeiptr>::max)()) / m_UniformTypeSize)
			throw std::length_error("Uniform buffer size exceeds the GL byte-size limit");
		const auto bytes = static_cast<GLsizeiptr>(max_size) * m_UniformTypeSize;
		GLint maxBindings = 0, previous = 0;
		glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxBindings);
		if (maxBindings <= 0 || bind_point >= static_cast<unsigned int>(maxBindings))
			throw std::out_of_range("Uniform buffer binding point exceeds context limits");
		glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &previous);
		try
		{
			glGenBuffers(1, &m_UBO);
			if (!m_UBO) throw std::runtime_error("Uniform buffer name allocation failed");
			glBindBuffer(GL_UNIFORM_BUFFER, m_UBO);
			glBufferData(GL_UNIFORM_BUFFER, bytes, nullptr, GL_STATIC_DRAW);
			GLint64 allocatedBytes = 0;
			glGetBufferParameteri64v(GL_UNIFORM_BUFFER, GL_BUFFER_SIZE, &allocatedBytes);
			if (allocatedBytes != bytes) throw std::runtime_error("Uniform buffer storage allocation failed");
			// Publish the indexed binding only after storage is known to exist.
			glBindBufferBase(GL_UNIFORM_BUFFER, m_BindingPoint, m_UBO);
			glBindBuffer(GL_UNIFORM_BUFFER, 0);
		}
		catch (...)
		{
			glBindBuffer(GL_UNIFORM_BUFFER, previous);
			if (m_UBO) glDeleteBuffers(1, &m_UBO);
			m_UBO = 0;
			throw; // A failed constructor will not run this class's destructor.
		}
	}

	template<UniformType Type>
	UniformBufferObject<Type>::~UniformBufferObject()
	{
		if (m_UBO) glDeleteBuffers(1, &m_UBO);
	}

	template<UniformType Type>
	void UniformBufferObject<Type>::Swap(UniformBufferObject& other) noexcept
	{
		std::swap(m_UBO, other.m_UBO);
		std::swap(m_MaxSize, other.m_MaxSize);
		std::swap(m_BindingPoint, other.m_BindingPoint);
		std::swap(m_UniformTypeSize, other.m_UniformTypeSize);
	}

	template<UniformType Type>
	UniformBufferObject<Type>::UniformBufferObject(UniformBufferObject&& other) noexcept
	{
		Swap(other);
	}

	template<UniformType Type>
	UniformBufferObject<Type>& UniformBufferObject<Type>::operator=(UniformBufferObject&& other) noexcept
	{
		if (this != &other)
		{
			UniformBufferObject retired(std::move(other));
			Swap(retired); // The replaced resource retires here on the calling context thread.
		}
		return *this;
	}

	template class UniformBufferObject<UniformType::VEC2F>;
	template class UniformBufferObject<UniformType::VEC3F>;
	template class UniformBufferObject<UniformType::VEC4F>;
	template class UniformBufferObject<UniformType::MATRIX_2_2>;
	template class UniformBufferObject<UniformType::MATRIX_3_3>;
	template class UniformBufferObject<UniformType::MATRIX_4_4>;



}
