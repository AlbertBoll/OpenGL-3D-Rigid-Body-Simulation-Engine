#include "gepch.h"
#include "Renderer/PassTiming.h"
#include "Renderer/FrameSubmission.h"
#include "../Renderer/GLStateCache.h"
#include "FramebufferBackend.h"
#include "../Assets/TextureBackend.h"
#include <new>

namespace GEngine
{
    namespace
    {
        using Format = FramebufferFormat;
        GLenum Internal(Format f)
        {
            switch (f)
            {
            case Format::RGBA8: return GL_RGBA8;
            case Format::RedInteger: return GL_R32I;
            case Format::Depth24: return GL_DEPTH_COMPONENT24;
            case Format::Depth24Stencil8: return GL_DEPTH24_STENCIL8;
            case Format::Depth32Float: return GL_DEPTH_COMPONENT32F;
            default: return 0;
            }
        }
        GLenum External(Format f)
        { return f == Format::RGBA8 ? GL_RGBA : f == Format::RedInteger ? GL_RED_INTEGER
            : f == Format::Depth24Stencil8 ? GL_DEPTH_STENCIL : GL_DEPTH_COMPONENT; }
        GLenum PixelType(Format f)
        { return f == Format::RGBA8 ? GL_UNSIGNED_BYTE : f == Format::RedInteger ? GL_INT
            : f == Format::Depth24Stencil8 ? GL_UNSIGNED_INT_24_8 : GL_FLOAT; }
        GLenum Target(const FrameBufferSpecification& d)
        { return d.Kind == FramebufferKind::Cube ? GL_TEXTURE_CUBE_MAP : d.Kind == FramebufferKind::Array
            ? GL_TEXTURE_2D_ARRAY : d.Samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D; }
        GLenum Binding(FramebufferBinding b)
        {
            switch (b)
            {
            case FramebufferBinding::ReadDraw: return GL_FRAMEBUFFER;
            case FramebufferBinding::Read: return GL_READ_FRAMEBUFFER;
            case FramebufferBinding::Draw: return GL_DRAW_FRAMEBUFFER;
            }
            GLContextThread::Detail::Fail("Framebuffer binding enum");
        }
        FramebufferError Error(FramebufferErrorCode code, const char* message, const FrameBufferSpecification& d = {})
        { return {code, message, d.Width, d.Height, d.Samples}; }
        struct Bindings
        {
            GLint read{}, draw{}, renderbuffer{}, texture{};
            GLenum target;
            explicit Bindings(GLenum t) : target(t)
            {
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
                glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
                glGetIntegerv(t == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_BINDING_CUBE_MAP : t == GL_TEXTURE_2D_ARRAY
                    ? GL_TEXTURE_BINDING_2D_ARRAY : t == GL_TEXTURE_2D_MULTISAMPLE ? GL_TEXTURE_BINDING_2D_MULTISAMPLE : GL_TEXTURE_BINDING_2D, &texture);
            }
            ~Bindings()
            {
                glBindTexture(target, texture); glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, read); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
            }
        };
        bool SupportsSamples(GLenum target, Format format, std::uint32_t samples)
        {
            if (samples == 1) return true;
            GLint count = 0; glGetInternalformativ(target, Internal(format), GL_NUM_SAMPLE_COUNTS, 1, &count);
            std::array<GLint, 32> supported{};
            if (count <= 0 || count > static_cast<GLint>(supported.size())) return false;
            glGetInternalformativ(target, Internal(format), GL_SAMPLES, count, supported.data());
            return std::find(supported.begin(), supported.begin() + count, static_cast<GLint>(samples)) != supported.begin() + count;
        }
        struct PixelPack
        {
            GLint buffer{};
            static constexpr GLenum keys[]{GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS,
                GL_PACK_SKIP_ROWS, GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_IMAGES, GL_PACK_SWAP_BYTES, GL_PACK_LSB_FIRST};
            std::array<GLint, 8> values{};
            PixelPack()
            {
                glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &buffer); glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                for (unsigned i = 0; i < values.size(); ++i)
                { glGetIntegerv(keys[i], &values[i]); glPixelStorei(keys[i], i == 0 ? 1 : 0); }
            }
            ~PixelPack()
            {
                glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
                for (unsigned i = 0; i < values.size(); ++i) glPixelStorei(keys[i], values[i]);
            }
        };
        struct Scissor
        {
            GLboolean enabled = glIsEnabled(GL_SCISSOR_TEST);
            Scissor() { glDisable(GL_SCISSOR_TEST); }
            ~Scissor() { if (enabled) glEnable(GL_SCISSOR_TEST); }
        };
    }
    struct FrameBuffer::Storage
    {
        FramebufferStorageHandle identity;
        FrameBufferSpecification desc;
        GLuint name = 0, depth = 0, renderbuffer = 0;
        std::array<GLuint, 4> colors{};
        // Prepared replacements borrow unchanged names until Commit transfers
        // ownership. Failure only destroys newly allocated storage.
        std::array<bool, 4> borrowedColors{};
        bool borrowedDepth = false;
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        void Require() const
        {
            GLContextThread::RequireCurrent("Framebuffer operation");
            if (context != SDL_GL_GetCurrentContext()) GLContextThread::Detail::Fail("Framebuffer creating context");
        }
        ~Storage()
        {
            Require();
            if (name) glDeleteFramebuffers(1, &name);
            for (unsigned i = 0; i < colors.size(); ++i)
                if (colors[i] && !borrowedColors[i]) glDeleteTextures(1, &colors[i]);
            if (depth && !borrowedDepth) glDeleteTextures(1, &depth);
            if (renderbuffer && !borrowedDepth) glDeleteRenderbuffers(1, &renderbuffer);
        }
    };
    FrameBuffer::FrameBuffer() noexcept = default;
    FrameBuffer::~FrameBuffer() = default;
    FrameBuffer::FrameBuffer(FrameBuffer&& other) noexcept { *this = std::move(other); }
    FrameBuffer& FrameBuffer::operator=(FrameBuffer&& other) noexcept
    {
        if (this != &other)
        {
            m_Storage = std::move(other.m_Storage);
            m_Description = std::exchange(other.m_Description, FrameBufferSpecification{0, 0, 0, 0});
            m_Reallocations = std::exchange(other.m_Reallocations, 0);
            m_HasAllocated = std::exchange(other.m_HasAllocated, false);
        }
        return *this;
    }
    FrameBuffer::operator bool() const noexcept { return m_Storage && m_Storage->name; }
    FramebufferStorageHandle FrameBuffer::StorageIdentity() const noexcept
    { return m_Storage ? m_Storage->identity : FramebufferStorageHandle{}; }
    void FrameBuffer::RequireOwner() const { if (m_Storage) m_Storage->Require(); }
    const FrameBufferSpecification& FrameBuffer::Description() const noexcept
    { return m_Description; }
    void ReportFramebufferError(const char* operation, const FramebufferError& e)
    { GENGINE_CORE_ERROR("Framebuffer {}: {} (code={}, {}x{}, samples={})", operation,
        e.message, static_cast<int>(e.code), e.width, e.height, e.samples); }

    std::expected<FrameBuffer, FramebufferError> FrameBuffer::Create(const FrameBufferSpecification& d)
    { return FrameBuffer{}.Prepare(d); }
    FramebufferResult FrameBuffer::ValidateDescription(const FrameBufferSpecification& d)
    {
        if (!d.Samples || !d.Layers || d.Width > 8192 || d.Height > 8192
            || d.Samples > 64 || d.ColorCount > d.Colors.size()
            || (d.Kind != FramebufferKind::Image2D && d.Kind != FramebufferKind::Cube && d.Kind != FramebufferKind::Array)
            || (d.Kind != FramebufferKind::Image2D && (d.ColorCount || d.Samples != 1 || d.DepthRenderbuffer))
            || (d.Kind == FramebufferKind::Cube && ((d.Width && d.Height && d.Width != d.Height) || d.Layers != 6))
            || (d.Kind == FramebufferKind::Image2D && d.Layers != 1)
            || (d.Depth != Format::None && d.Depth != Format::Depth24 && d.Depth != Format::Depth24Stencil8 && d.Depth != Format::Depth32Float)
            || (d.DepthRenderbuffer && d.Depth == Format::None) || (!d.ColorCount && d.Depth == Format::None))
            return std::unexpected(Error(FramebufferErrorCode::InvalidDescription, "Invalid framebuffer dimensions, layers, samples or attachments", d));
        for (std::uint32_t i = 0; i < d.Colors.size(); ++i)
            if (i < d.ColorCount ? d.Colors[i] != Format::RGBA8 && d.Colors[i] != Format::RedInteger : d.Colors[i] != Format::None)
                return std::unexpected(Error(FramebufferErrorCode::InvalidDescription, "Invalid color attachment layout", d));
        return {};
    }
    std::expected<FrameBuffer, FramebufferError> FrameBuffer::Prepare(const FrameBufferSpecification& d) const
    {
        if (auto valid = ValidateDescription(d); !valid) return std::unexpected(valid.error());
        if (m_Storage) m_Storage->Require();
        FrameBuffer result; result.m_Description = d;
        if (!d.Width || !d.Height) return result;
        if (!GLContextThread::IsCurrentOwner())
            return std::unexpected(Error(FramebufferErrorCode::ContextUnavailable, "Framebuffer creation requires a current owning context", d));
        GLint limit = 0, layers = 0, draws = 0, colors = 0, rboLimit = 0;
        glGetIntegerv(d.Kind == FramebufferKind::Cube ? GL_MAX_CUBE_MAP_TEXTURE_SIZE : GL_MAX_TEXTURE_SIZE, &limit);
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &layers); glGetIntegerv(GL_MAX_DRAW_BUFFERS, &draws);
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &colors); glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &rboLimit);
        if (d.Width > static_cast<unsigned>(limit) || d.Height > static_cast<unsigned>(limit)
            || (d.Kind == FramebufferKind::Array && d.Layers > static_cast<unsigned>(layers))
            || d.ColorCount > static_cast<unsigned>((std::min)(draws, colors))
            || (d.DepthRenderbuffer && (d.Width > static_cast<unsigned>(rboLimit) || d.Height > static_cast<unsigned>(rboLimit))))
            return std::unexpected(Error(FramebufferErrorCode::Unsupported, "Framebuffer exceeds context limits", d));
        for (std::uint32_t i = 0; i < d.ColorCount; ++i)
            if (!SupportsSamples(GL_TEXTURE_2D_MULTISAMPLE, d.Colors[i], d.Samples))
                return std::unexpected(Error(FramebufferErrorCode::Unsupported, "Unsupported color sample count", d));
        if (d.Depth != Format::None && !SupportsSamples(d.DepthRenderbuffer ? GL_RENDERBUFFER : GL_TEXTURE_2D_MULTISAMPLE, d.Depth, d.Samples))
            return std::unexpected(Error(FramebufferErrorCode::Unsupported, "Unsupported depth sample count", d));
        const auto target = Target(d);
        Bindings restore(target);
        result.m_Storage.reset(new (std::nothrow) Storage);
        if (!result.m_Storage) return std::unexpected(Error(FramebufferErrorCode::Allocation, "Framebuffer owner allocation failed", d));
        auto& s = *result.m_Storage; s.desc = d;
        auto identity = Asset::AssetDetail::TakeRegistryIdentity(Asset::AssetDetail::nextRegistryIdentity);
        if (!identity) return std::unexpected(Error(FramebufferErrorCode::Allocation, "Framebuffer storage identity exhausted", d));
        s.identity = {0, 1, *identity};
        const auto& previous = Description();
        const bool sameShape = m_Storage && d.Width == previous.Width && d.Height == previous.Height
            && d.Samples == previous.Samples && d.Kind == previous.Kind && d.Layers == previous.Layers;
        glGenFramebuffers(1, &s.name);
        if (!s.name) return std::unexpected(Error(FramebufferErrorCode::Allocation, "Framebuffer name allocation failed", d));
        glBindFramebuffer(GL_FRAMEBUFFER, s.name);
        GLint unpack = 0; glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
        struct UnpackBuffer { GLint previous; ~UnpackBuffer() { glBindBuffer(GL_PIXEL_UNPACK_BUFFER, previous); } } unpackRestore{unpack};
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        const auto texture = [&](GLuint& name, Format format, GLenum attachment) -> FramebufferResult
        {
            glGenTextures(1, &name);
            if (!name) return std::unexpected(Error(FramebufferErrorCode::Allocation, "Attachment name allocation failed", d));
            glBindTexture(target, name);
            if (d.Samples > 1) glTexImage2DMultisample(target, d.Samples, Internal(format), d.Width, d.Height, GL_TRUE);
            else if (d.Kind == FramebufferKind::Array)
                glTexImage3D(target, 0, Internal(format), d.Width, d.Height, d.Layers, 0, External(format), PixelType(format), nullptr);
            else for (unsigned face = 0; face < (d.Kind == FramebufferKind::Cube ? 6u : 1u); ++face)
                glTexImage2D(d.Kind == FramebufferKind::Cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : target,
                    0, Internal(format), d.Width, d.Height, 0, External(format), PixelType(format), nullptr);
            if (d.Samples == 1)
            {
                const auto filter = format == Format::RedInteger || d.Kind == FramebufferKind::Array ? GL_NEAREST : GL_LINEAR;
                glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filter); glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
                glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0); glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 0);
                for (const auto axis : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
                    glTexParameteri(target, axis, d.Kind == FramebufferKind::Image2D ? GL_CLAMP_TO_EDGE : GL_CLAMP_TO_BORDER);
                if (d.Kind != FramebufferKind::Image2D)
                { const float border[]{1, 1, 1, 1}; glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, border); }
            }
            for (unsigned face = 0; face < (d.Kind == FramebufferKind::Cube ? 6u : 1u); ++face)
            {
                const auto q = d.Kind == FramebufferKind::Cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : target;
                GLint width = 0, height = 0, depth = 0, formatValue = 0, samples = 0;
                glGetTexLevelParameteriv(q, 0, GL_TEXTURE_WIDTH, &width); glGetTexLevelParameteriv(q, 0, GL_TEXTURE_HEIGHT, &height);
                glGetTexLevelParameteriv(q, 0, GL_TEXTURE_INTERNAL_FORMAT, &formatValue);
                if (d.Kind == FramebufferKind::Array) glGetTexLevelParameteriv(q, 0, GL_TEXTURE_DEPTH, &depth);
                if (d.Samples > 1) glGetTexLevelParameteriv(q, 0, GL_TEXTURE_SAMPLES, &samples);
                if (width != static_cast<GLint>(d.Width) || height != static_cast<GLint>(d.Height)
                    || formatValue != static_cast<GLint>(Internal(format))
                    || (d.Kind == FramebufferKind::Array && depth != static_cast<GLint>(d.Layers))
                    || (d.Samples > 1 && samples != static_cast<GLint>(d.Samples)))
                    return std::unexpected(Error(FramebufferErrorCode::Storage, "Attachment storage was not allocated as described", d));
            }
            glFramebufferTexture(GL_FRAMEBUFFER, attachment, name, 0); return {};
        };
        for (std::uint32_t i = 0; i < d.ColorCount; ++i)
        {
            if (sameShape && d.Colors[i] == previous.Colors[i])
            {
                s.colors[i] = m_Storage->colors[i]; s.borrowedColors[i] = true;
                glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, s.colors[i], 0);
            }
            else if (auto a = texture(s.colors[i], d.Colors[i], GL_COLOR_ATTACHMENT0 + i); !a) return std::unexpected(a.error());
        }
        if (d.Depth != Format::None)
        {
            const auto point = d.Depth == Format::Depth24Stencil8 ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
            if (sameShape && d.Depth == previous.Depth && d.DepthRenderbuffer == previous.DepthRenderbuffer)
            {
                s.depth = m_Storage->depth; s.renderbuffer = m_Storage->renderbuffer; s.borrowedDepth = true;
                if (d.DepthRenderbuffer) glFramebufferRenderbuffer(GL_FRAMEBUFFER, point, GL_RENDERBUFFER, s.renderbuffer);
                else glFramebufferTexture(GL_FRAMEBUFFER, point, s.depth, 0);
            }
            else if (d.DepthRenderbuffer)
            {
                glGenRenderbuffers(1, &s.renderbuffer);
                if (!s.renderbuffer) return std::unexpected(Error(FramebufferErrorCode::Allocation, "Renderbuffer name allocation failed", d));
                glBindRenderbuffer(GL_RENDERBUFFER, s.renderbuffer);
                if (d.Samples > 1) glRenderbufferStorageMultisample(GL_RENDERBUFFER, d.Samples, Internal(d.Depth), d.Width, d.Height);
                else glRenderbufferStorage(GL_RENDERBUFFER, Internal(d.Depth), d.Width, d.Height);
                GLint width = 0, height = 0, format = 0, samples = 0;
                glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
                glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
                glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
                glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &samples);
                if (width != static_cast<GLint>(d.Width) || height != static_cast<GLint>(d.Height)
                    || format != static_cast<GLint>(Internal(d.Depth)) || samples != (d.Samples > 1 ? static_cast<GLint>(d.Samples) : 0))
                    return std::unexpected(Error(FramebufferErrorCode::Storage, "Renderbuffer storage was not allocated as described", d));
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, point, GL_RENDERBUFFER, s.renderbuffer);
            }
            else if (auto a = texture(s.depth, d.Depth, point); !a) return std::unexpected(a.error());
        }
        const GLenum buffers[]{GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
        if (d.ColorCount) { glDrawBuffers(d.ColorCount, buffers); glReadBuffer(GL_COLOR_ATTACHMENT0); }
        else { glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE); }
        const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            GENGINE_CORE_ERROR("Framebuffer completeness failed: status={}, {}x{}, samples={}, layers={}", status, d.Width, d.Height, d.Samples, d.Layers);
            return std::unexpected(Error(FramebufferErrorCode::Incomplete, "Framebuffer attachment configuration is incomplete", d));
        }
        result.m_HasAllocated = true;
        return result;
    }
    void FrameBuffer::Bind(FramebufferBinding b) const
    {
        if (!*this) GLContextThread::Detail::Fail("Binding an empty framebuffer owner");
        m_Storage->Require();
        if(auto* state=RenderBackend::GLStateCache::Current()) {
            state->Framebuffer(Binding(b),m_Storage->name);
            // Submission currently accepts exactly one color attachment or depth
            // only. Re-establish object-local routing after an external boundary.
            Asset::AssetDetail::RequireInvariant(Description().ColorCount<=1);
            const auto buffer=Description().ColorCount?GL_COLOR_ATTACHMENT0:GL_NONE;
            if(b!=FramebufferBinding::Read) state->DrawBuffer(buffer);
            if(b!=FramebufferBinding::Draw) state->ReadBuffer(buffer);
        } else glBindFramebuffer(Binding(b), m_Storage->name);
    }
    void FrameBuffer::UnBind(FramebufferBinding b)
    { GLContextThread::RequireCurrent("Default framebuffer binding"); glBindFramebuffer(Binding(b), 0); }
    FramebufferResult FrameBuffer::Resize(std::uint32_t width, std::uint32_t height)
    {
        auto desc = Description(); desc.Width = width; desc.Height = height;
        return Reconfigure(desc);
    }
    FramebufferResult FrameBuffer::Reconfigure(const FrameBufferSpecification& desc)
    {
        if (auto valid = ValidateDescription(desc); !valid) return valid;
        if (m_Storage) m_Storage->Require();
        if (desc == Description()) return {};
        auto candidate = Prepare(desc); if (!candidate) return std::unexpected(candidate.error());
        const bool replacing = bool(*candidate) && m_HasAllocated;
        Commit(std::move(*candidate)); RenderCounters::RecordTargetReallocation(replacing); return {};
    }
    void FrameBuffer::Commit(FrameBuffer&& candidate) noexcept
    {
        if (candidate.m_Storage)
        {
            auto& s = *candidate.m_Storage;
            for (unsigned i = 0; i < s.colors.size(); ++i)
                if (s.borrowedColors[i]) { m_Storage->colors[i] = 0; s.borrowedColors[i] = false; }
            if (s.borrowedDepth)
            { m_Storage->depth = m_Storage->renderbuffer = 0; s.borrowedDepth = false; }
        }
        const auto count = m_Reallocations + (bool(candidate) && m_HasAllocated);
        const bool allocated = m_HasAllocated || bool(candidate);
        *this = std::move(candidate); m_Reallocations = count; m_HasAllocated = allocated;
    }
    FramebufferResult FrameBuffer::ResolveTo(const FrameBuffer& destination, std::uint32_t source, std::uint32_t target) const
    {
        const auto& a = Description(); const auto& b = destination.Description();
        if (!*this || !destination || this == &destination || source >= a.ColorCount || target >= b.ColorCount
            || a.Colors[source] != b.Colors[target] || a.Width != b.Width || a.Height != b.Height || b.Samples != 1)
            return std::unexpected(Error(FramebufferErrorCode::InvalidOperation, "Resolve requires distinct matching attachments and a single-sample destination", a));
        m_Storage->Require(); destination.m_Storage->Require(); Bindings restore(GL_TEXTURE_2D); Scissor scissor;
        Bind(FramebufferBinding::Read); destination.Bind(FramebufferBinding::Draw);
        GLint read = 0; glGetIntegerv(GL_READ_BUFFER, &read);
        std::array<GLenum, 4> draws{};
        for (std::uint32_t i = 0; i < b.ColorCount; ++i)
        { GLint draw = 0; glGetIntegerv(GL_DRAW_BUFFER0 + i, &draw); draws[i] = draw; }
        glReadBuffer(GL_COLOR_ATTACHMENT0 + source); glDrawBuffer(GL_COLOR_ATTACHMENT0 + target);
        glBlitFramebuffer(0, 0, a.Width, a.Height, 0, 0, b.Width, b.Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glReadBuffer(read); glDrawBuffers(b.ColorCount, draws.data()); return {};
    }
    FramebufferResult FrameBuffer::Present() const
    {
        const auto& d = Description();
        if (!*this || !d.ColorCount || d.Colors[0] != Format::RGBA8)
            return std::unexpected(Error(FramebufferErrorCode::InvalidOperation, "Presentation requires an RGBA color attachment", d));
        m_Storage->Require(); Bindings restore(GL_TEXTURE_2D); Scissor scissor; Bind(FramebufferBinding::Read);
        GLint read = 0; glGetIntegerv(GL_READ_BUFFER, &read); glReadBuffer(GL_COLOR_ATTACHMENT0);
        UnBind(FramebufferBinding::Draw);
        glBlitFramebuffer(0, 0, d.Width, d.Height, 0, 0, d.Width, d.Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glReadBuffer(read); return {};
    }
    std::expected<int, FramebufferError> FrameBuffer::ReadInteger(std::uint32_t i, int x, int y) const
    {
        const auto& d = Description();
        if (!*this || i >= d.ColorCount || d.Colors[i] != Format::RedInteger || d.Samples != 1)
            return std::unexpected(Error(FramebufferErrorCode::InvalidAttachment, "Integer read requires a single-sample integer attachment", d));
        if (x < 0 || y < 0 || static_cast<unsigned>(x) >= d.Width || static_cast<unsigned>(y) >= d.Height)
            return std::unexpected(Error(FramebufferErrorCode::InvalidCoordinates, "Read coordinates are outside the attachment", d));
        m_Storage->Require();
        // Measure the synchronous transfer/wait and pack-state restoration apart
        // from rasterization. Invalid coordinates never issue or record a read.
        PassTiming::Scope timing(RenderPass::PickingReadback);
        int value = 0;
        {
            PixelPack pack;
            glGetTextureSubImage(m_Storage->colors[i], 0, x, y, 0, 1, 1, 1, GL_RED_INTEGER, GL_INT, sizeof(value), &value);
        }
        PassTiming::Submitted(1,0);
        timing.Complete();
        return value;
    }
    FramebufferResult FrameBuffer::ClearInteger(std::uint32_t i, int value) const
    {
        const auto& d = Description();
        if (!*this || i >= d.ColorCount || d.Colors[i] != Format::RedInteger)
            return std::unexpected(Error(FramebufferErrorCode::InvalidAttachment, "Integer clear requires an integer attachment", d));
        m_Storage->Require(); Bindings restore(GL_TEXTURE_2D); Scissor scissor; Bind(FramebufferBinding::Draw);
        std::array<GLenum, 4> draws{};
        for (unsigned slot = 0; slot < d.ColorCount; ++slot)
        { GLint draw = 0; glGetIntegerv(GL_DRAW_BUFFER0 + slot, &draw); draws[slot] = draw; }
        GLboolean mask[4]{}; glGetBooleani_v(GL_COLOR_WRITEMASK, 0, mask);
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDrawBuffer(GL_COLOR_ATTACHMENT0 + i);
        const GLint values[]{value, value, value, value}; glClearBufferiv(GL_COLOR, 0, values);
        glColorMaski(0, mask[0], mask[1], mask[2], mask[3]); glDrawBuffers(d.ColorCount, draws.data());
        return {};
    }
    FramebufferResult FrameBuffer::ReadColor(std::uint32_t i, std::span<std::byte> rgba) const
    {
        const auto& d = Description();
        if (!*this || i >= d.ColorCount || d.Colors[i] != Format::RGBA8 || d.Samples != 1
            || rgba.size() != std::size_t(d.Width) * d.Height * 4)
            return std::unexpected(Error(FramebufferErrorCode::InvalidAttachment, "Color read requires a matching RGBA8 span and single-sample attachment", d));
        m_Storage->Require(); PixelPack pack;
        glGetTextureImage(m_Storage->colors[i], 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(rgba.size()), rgba.data()); return {};
    }
    std::expected<Asset::AttachmentView, FramebufferError> FrameBuffer::ColorView(std::uint32_t i) const
    {
        const auto& d = Description();
        if (!*this || i >= d.ColorCount || d.Samples != 1)
            return std::unexpected(Error(FramebufferErrorCode::InvalidView, "Sampled color view requires a single-sample color attachment", d));
        m_Storage->Require();
        return Asset::AssetDetail::TextureBackend::Borrow([this, i, kind = d.Kind, format = d.Colors[i]]
        {
            const auto& current = Description();
            return m_Storage && current.Kind == kind && current.Samples == 1 && current.Colors[i] == format
                ? m_Storage->colors[i] : 0;
        }, Target(d));
    }
    std::expected<Asset::AttachmentView, FramebufferError> FrameBuffer::DepthView() const
    {
        const auto& d = Description();
        if (!*this || !m_Storage->depth || d.Samples != 1)
            return std::unexpected(Error(FramebufferErrorCode::InvalidView, "Sampled depth view requires a single-sample depth texture", d));
        m_Storage->Require();
        return Asset::AssetDetail::TextureBackend::Borrow([this, kind = d.Kind, format = d.Depth]
        {
            const auto& current = Description();
            return m_Storage && current.Kind == kind && current.Samples == 1 && current.Depth == format
                ? m_Storage->depth : 0;
        }, Target(d));
    }
    GLuint FramebufferDetail::Backend::Name(const FrameBuffer& b) { return b.m_Storage ? b.m_Storage->name : 0; }
    GLuint FramebufferDetail::Backend::Color(const FrameBuffer& b, std::uint32_t i) { return b.m_Storage && i < 4 ? b.m_Storage->colors[i] : 0; }
    GLuint FramebufferDetail::Backend::Depth(const FrameBuffer& b) { return b.m_Storage ? b.m_Storage->depth : 0; }
    GLuint FramebufferDetail::Backend::Renderbuffer(const FrameBuffer& b) { return b.m_Storage ? b.m_Storage->renderbuffer : 0; }
}
