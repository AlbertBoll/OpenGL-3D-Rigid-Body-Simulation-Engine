#include "gepch.h"
#include "Core/RenderTarget.h"
#include <cmath>
#include <limits>
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
    bool RenderTarget::Allows(RenderTargetUsage usage) const noexcept
    { return (static_cast<unsigned>(m_Description.Usage) & static_cast<unsigned>(usage)) != 0; }
    RenderTarget::RenderTarget(RenderTarget&& other) noexcept { *this = std::move(other); }
    RenderTarget& RenderTarget::operator=(RenderTarget&& other) noexcept
    {
        if (this != &other)
        {
            m_Render = std::move(other.m_Render); m_Resolved = std::move(other.m_Resolved);
            m_Description = std::exchange(other.m_Description, RenderTargetDesc{});
            m_Reallocations = std::exchange(other.m_Reallocations, 0);
            m_HasAllocated = std::exchange(other.m_HasAllocated, false);
        }
        return *this;
    }
    std::expected<RenderTarget, FramebufferError> RenderTarget::Create(const RenderTargetDesc& desc)
    {
        RenderTarget result;
        if (auto configured = result.Reconfigure(desc); !configured) return std::unexpected(configured.error());
        return result;
    }
    FramebufferResult RenderTarget::Reconfigure(const RenderTargetDesc& desc)
    {
        if (desc.SizeSource != TargetSizeSource::Fixed && desc.SizeSource != TargetSizeSource::NativeFramebuffer
            && desc.SizeSource != TargetSizeSource::EditorViewport)
            return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidDescription, "Unknown target size source"});
        const auto usage = static_cast<unsigned>(desc.Usage);
        if ((usage & 1u) == 0 || (usage & ~15u) != 0)
            return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidDescription, "Target usage requires attachment intent and known output usages",
                desc.Storage.Width, desc.Storage.Height, desc.Storage.Samples});
        if (auto valid = FrameBuffer::ValidateDescription(desc.Storage); !valid) return valid;
        m_Render.RequireOwner(); m_Resolved.RequireOwner();
        auto resolved = FrameBufferSpecification{0, 0, 0, 0};
        if (desc.Storage.Samples > 1 && desc.Storage.ColorCount && (usage & ~1u))
        {
            resolved = desc.Storage; resolved.Samples = 1;
            resolved.Depth = FramebufferFormat::None; resolved.DepthRenderbuffer = false;
        }
        const bool sceneChanged = desc.Storage != m_Render.Description();
        const bool resolveChanged = resolved != m_Resolved.Description();
        FrameBuffer scene, output;
        if (sceneChanged)
        {
            auto candidate = m_Render.Prepare(desc.Storage); if (!candidate) return std::unexpected(candidate.error());
            scene = std::move(*candidate);
        }
        if (resolveChanged && resolved.Samples)
        {
            auto candidate = m_Resolved.Prepare(resolved); if (!candidate) return std::unexpected(candidate.error());
            output = std::move(*candidate);
        }
        // Both preparations succeeded. Borrowed attachments transfer only now;
        // neither owner nor its observers change on any earlier failure.
        const bool replacing = (bool(scene) || bool(output)) && m_HasAllocated;
        if (sceneChanged) m_Render.Commit(std::move(scene));
        if (resolveChanged) m_Resolved.Commit(std::move(output));
        m_Description = desc; m_HasAllocated = m_HasAllocated || bool(m_Render);
        m_Reallocations += replacing; RenderCounters::RecordTargetReallocation(replacing);
        return {};
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
        return Create(RenderTargetDesc{d});
    }
    std::expected<RenderTarget, FramebufferError> RenderTarget::Create(int w, int h, unsigned samples)
    {
        if (w < 0 || h < 0) return std::unexpected(InvalidSize());
        auto d = ColorDescription(static_cast<unsigned>(w), static_cast<unsigned>(h));
        d.Samples = samples; d.Depth = FramebufferFormat::Depth24Stencil8; d.DepthRenderbuffer = true; return Create(RenderTargetDesc{d});
    }
    std::expected<RenderTarget, FramebufferError> RenderTarget::Create(const Math::Vec2f& size)
    {
        if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x < 0 || size.y < 0 || size.x > 8192 || size.y > 8192)
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
        if (!Allows(RenderTargetUsage::Readback))
            return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidOperation, "Target was not described for readback"});
        if (auto result = BindAndBlitToScreen(); !result) return std::unexpected(result.error());
        return Buffer(RenderTargetSurface::Resolved).ReadInteger(index, x, y);
    }
    FramebufferResult RenderTarget::ReadColor(std::span<std::byte> rgba) const
    {
        if (!Allows(RenderTargetUsage::Readback))
            return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidOperation, "Target was not described for readback"});
        if (auto result = BindAndBlitToScreen(); !result) return result;
        return Buffer(RenderTargetSurface::Resolved).ReadColor(0, rgba);
    }
    std::expected<Asset::AttachmentView, FramebufferError> RenderTarget::ColorView() const
    {
        if (!Allows(RenderTargetUsage::Sampled))
            return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidView, "Target was not described for sampling"});
        return Buffer(RenderTargetSurface::Resolved).ColorView();
    }
    FramebufferResult RenderTarget::OnResize(unsigned w, unsigned h)
    {
        auto desc = m_Description; desc.Storage.Width = w; desc.Storage.Height = h;
        return Reconfigure(desc);
    }
    FramebufferResult RenderTarget::RenderSize(const Math::Vec2f& size)
    {
        if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x < 0 || size.y < 0 || size.x > 8192 || size.y > 8192)
            return std::unexpected(InvalidSize());
        return OnResize(static_cast<unsigned>(size.x), static_cast<unsigned>(size.y));
    }
    FramebufferResult RenderTarget::SetSamples(int samples)
    {
        if (samples < 1) return std::unexpected(InvalidSize());
        auto desc = m_Description; desc.Storage.Samples = static_cast<unsigned>(samples);
        return Reconfigure(desc);
    }

    // Standalone renderbuffer and uniform-buffer owners use typed failure transport.
    std::expected<RenderBufferObject, FramebufferError> RenderBufferObject::Create(
        unsigned int width, unsigned int height, unsigned int samples)
    {
        RenderBufferObject candidate;
        if (auto resized = candidate.Resize(width, height, samples); !resized)
            return std::unexpected(resized.error());
        return candidate;
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

    FramebufferResult RenderBufferObject::Resize(unsigned int width, unsigned int height, unsigned int samples)
    {
        const auto reject = [&](FramebufferErrorCode code, const char* message) {
            return std::unexpected(FramebufferError{code, message, width, height, samples});
        };
        if (!width || !height || !samples)
            return reject(FramebufferErrorCode::InvalidDescription, "Renderbuffer dimensions and sample count must be positive");
        GLint maxSize = 0, maxSamples = 0, previous = 0;
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxSize);
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        if (maxSize <= 0 || maxSamples <= 0 || width > static_cast<unsigned int>(maxSize)
            || height > static_cast<unsigned int>(maxSize) || samples > static_cast<unsigned int>(maxSamples))
            return reject(FramebufferErrorCode::Unsupported, "Renderbuffer dimensions or samples exceed context limits");
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous);
        RenderBufferObject candidate;
        struct RestoreBinding
        {
            GLint previous;
            bool committed = false;
            ~RestoreBinding() { if (!committed) glBindRenderbuffer(GL_RENDERBUFFER, previous); }
        } restore{previous};
        glGenRenderbuffers(1, &candidate.m_ID);
        if (!candidate.m_ID)
            return reject(FramebufferErrorCode::Allocation, "Renderbuffer name allocation failed");
        glBindRenderbuffer(GL_RENDERBUFFER, candidate.m_ID);
        GLint bound = 0;
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &bound);
        if (static_cast<unsigned int>(bound) != candidate.m_ID)
            return reject(FramebufferErrorCode::InvalidOperation, "Renderbuffer binding failed");
        if (samples > 1)
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
        else
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        GLint actualWidth = 0, actualHeight = 0, actualSamples = 0, format = 0;
        glGetNamedRenderbufferParameteriv(candidate.m_ID, GL_RENDERBUFFER_WIDTH, &actualWidth);
        glGetNamedRenderbufferParameteriv(candidate.m_ID, GL_RENDERBUFFER_HEIGHT, &actualHeight);
        glGetNamedRenderbufferParameteriv(candidate.m_ID, GL_RENDERBUFFER_SAMPLES, &actualSamples);
        glGetNamedRenderbufferParameteriv(candidate.m_ID, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
        // Preserve pending driver errors for the existing diagnostics. Query the
        // fresh allocation, allowing implementations to round sample counts up.
        if (actualWidth != static_cast<GLint>(width) || actualHeight != static_cast<GLint>(height)
            || format != GL_DEPTH24_STENCIL8 || (samples > 1 ? actualSamples < static_cast<GLint>(samples) : actualSamples != 0))
            return reject(FramebufferErrorCode::Storage, "Renderbuffer storage allocation failed");
        candidate.m_Width = width;
        candidate.m_Height = height;
        candidate.m_Samples = samples;
        Swap(candidate);
        restore.committed = true;
        return {};
    }

    template<UniformType Type>
    std::expected<UniformBufferObject<Type>, UniformBufferError> UniformBufferObject<Type>::Create(
        unsigned int max_size, unsigned int bind_point)
    {
        using namespace Math;
        UniformBufferObject candidate;
        candidate.m_MaxSize = max_size;
        candidate.m_BindingPoint = bind_point;
        if constexpr (Type == UniformType::VEC2F) candidate.m_UniformTypeSize = sizeof(Vec2f);
        else if constexpr (Type == UniformType::VEC3F) candidate.m_UniformTypeSize = sizeof(Vec3f);
        else if constexpr (Type == UniformType::VEC4F) candidate.m_UniformTypeSize = sizeof(Vec4f);
        else if constexpr (Type == UniformType::MATRIX_2_2) candidate.m_UniformTypeSize = sizeof(Mat2);
        else if constexpr (Type == UniformType::MATRIX_3_3) candidate.m_UniformTypeSize = sizeof(Mat3);
        else if constexpr (Type == UniformType::MATRIX_4_4) candidate.m_UniformTypeSize = sizeof(Mat4);
        const auto reject = [&](UniformBufferErrorCode code, const char* message) {
            return std::unexpected(UniformBufferError{code, "UniformBufferObject::Create", message,
                max_size, bind_point, candidate.m_UniformTypeSize});
        };
        if (!max_size || !candidate.m_UniformTypeSize)
            return reject(UniformBufferErrorCode::InvalidDescription, "Uniform buffer element count and type must be valid");
        if (max_size > static_cast<std::size_t>((std::numeric_limits<GLsizeiptr>::max)()) / candidate.m_UniformTypeSize)
            return reject(UniformBufferErrorCode::SizeOverflow, "Uniform buffer size exceeds the GL byte-size limit");
        const auto bytes = static_cast<GLsizeiptr>(max_size) * candidate.m_UniformTypeSize;
        GLint maxBindings = 0, previous = 0, previousIndexed = 0;
        GLint64 previousStart = 0, previousSize = 0;
        glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxBindings);
        if (maxBindings <= 0 || bind_point >= static_cast<unsigned int>(maxBindings))
            return reject(UniformBufferErrorCode::InvalidBinding, "Uniform buffer binding point exceeds context limits");
        glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &previous);
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, bind_point, &previousIndexed);
        glGetInteger64i_v(GL_UNIFORM_BUFFER_START, bind_point, &previousStart);
        glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, bind_point, &previousSize);
        struct RestoreBindings
        {
            unsigned int point;
            GLint generic, indexed;
            GLint64 start, size;
            bool publishing = false, committed = false;
            ~RestoreBindings()
            {
                if (committed) return;
                if (publishing)
                {
                    if (indexed && size) glBindBufferRange(GL_UNIFORM_BUFFER, point, indexed, start, size);
                    else glBindBufferBase(GL_UNIFORM_BUFFER, point, indexed);
                }
                glBindBuffer(GL_UNIFORM_BUFFER, generic);
            }
        } restore{bind_point, previous, previousIndexed, previousStart, previousSize};
        glGenBuffers(1, &candidate.m_UBO);
        if (!candidate.m_UBO)
            return reject(UniformBufferErrorCode::Allocation, "Uniform buffer name allocation failed");
        glBindBuffer(GL_UNIFORM_BUFFER, candidate.m_UBO);
        GLint bound = 0;
        glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &bound);
        if (static_cast<unsigned>(bound) != candidate.m_UBO)
            return reject(UniformBufferErrorCode::Binding, "Uniform buffer binding failed");
        glBufferData(GL_UNIFORM_BUFFER, bytes, nullptr, GL_STATIC_DRAW);
        GLint64 allocatedBytes = 0;
        glGetBufferParameteri64v(GL_UNIFORM_BUFFER, GL_BUFFER_SIZE, &allocatedBytes);
        if (allocatedBytes != bytes)
            return reject(UniformBufferErrorCode::Storage, "Uniform buffer storage allocation failed");
        restore.publishing = true;
        glBindBufferBase(GL_UNIFORM_BUFFER, bind_point, candidate.m_UBO);
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, bind_point, &bound);
        if (static_cast<unsigned>(bound) != candidate.m_UBO)
            return reject(UniformBufferErrorCode::Binding, "Uniform buffer indexed binding failed");
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &bound);
        if (bound != 0)
            return reject(UniformBufferErrorCode::Binding, "Uniform buffer unbind failed");
        restore.committed = true;
        return candidate;
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
