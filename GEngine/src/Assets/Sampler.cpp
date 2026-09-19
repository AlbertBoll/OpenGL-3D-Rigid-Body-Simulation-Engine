#include "gepch.h"
#include "Assets/Samplers/Sampler.h"
#include "TextureBackend.h"
#include "../Renderer/GLStateCache.h"
#include "Core/GLContextThread.h"
#include <cmath>
#include <new>

namespace GEngine::Asset
{
    namespace
    {
        constexpr GLenum wraps[]{GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_BORDER};
        constexpr GLenum comparisons[]{GL_LEQUAL, GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL,
            GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
        constexpr GLenum minFilters[][2]{{GL_NEAREST, GL_LINEAR},
            {GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST},
            {GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR}};
        SamplerError RegistryFailure(RegistryError code)
        { return {SamplerErrorCode::Registry, "Sampler registry operation failed", code}; }
        bool HasAnisotropy() { return GLAD_GL_EXT_texture_filter_anisotropic != 0; }
    }
    struct GpuSampler::Storage
    {
        GLuint name = 0;
        GLint units = 0;
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        ~Storage()
        {
            if (!name) return;
            GLContextThread::RequireCurrent("Sampler retirement");
            AssetDetail::RequireInvariant(context == SDL_GL_GetCurrentContext());
            glDeleteSamplers(1, &name);
        }
    };
    GpuSampler::GpuSampler() noexcept = default;
    GpuSampler::~GpuSampler() = default;
    GpuSampler::GpuSampler(GpuSampler&&) noexcept = default;
    GpuSampler& GpuSampler::operator=(GpuSampler&& other) noexcept
    {
        if (this != &other) { m_Storage = std::move(other.m_Storage); m_Desc = other.m_Desc; }
        return *this;
    }
    GpuSampler::operator bool() const noexcept { return m_Storage && m_Storage->name; }

    std::expected<SamplerDesc, SamplerError> GpuSampler::Normalize(const SamplerDesc& requested)
    {
        auto d = requested;
        if (static_cast<unsigned>(d.minFilter) > 1 || static_cast<unsigned>(d.magFilter) > 1
            || static_cast<unsigned>(d.mipFilter) > 2 || static_cast<unsigned>(d.wrapU) > 3
            || static_cast<unsigned>(d.wrapV) > 3 || static_cast<unsigned>(d.wrapW) > 3
            || static_cast<unsigned>(d.compare) > 8 || static_cast<unsigned>(d.anisotropy) > 2
            || !std::isfinite(d.maxAnisotropy) || d.maxAnisotropy < 1
            || !std::all_of(d.borderColor.begin(), d.borderColor.end(), [](float v) { return std::isfinite(v); }))
            return std::unexpected(SamplerError{SamplerErrorCode::InvalidDescription, "Invalid sampler description"});
        GLContextThread::RequireCurrent("Sampler description validation");
        float limit = 1;
        if (HasAnisotropy()) glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &limit);
        if (d.anisotropy == SamplerAnisotropy::RequireExact && d.maxAnisotropy > limit)
            return std::unexpected(SamplerError{SamplerErrorCode::Unsupported, "Requested anisotropy exceeds backend support"});
        d.maxAnisotropy = d.anisotropy == SamplerAnisotropy::Disabled ? 1 : (std::min)(d.maxAnisotropy, limit);
        // Cache by effective state, including policies that normalize to the same limit.
        d.anisotropy = d.maxAnisotropy == 1 ? SamplerAnisotropy::Disabled : SamplerAnisotropy::RequireExact;
        if (d.wrapU != SamplerWrap::ClampToBorder && d.wrapV != SamplerWrap::ClampToBorder
            && d.wrapW != SamplerWrap::ClampToBorder) d.borderColor = {};
        return d;
    }
    std::expected<GpuSampler, SamplerError> GpuSampler::Create(const SamplerDesc& description)
    {
        auto normalized = Normalize(description);
        if (!normalized) return std::unexpected(normalized.error());
        GpuSampler result;
        result.m_Desc = *normalized;
        result.m_Storage.reset(new (std::nothrow) Storage);
        if (!result.m_Storage)
            return std::unexpected(SamplerError{SamplerErrorCode::Allocation, "Sampler owner allocation failed"});
        auto& state = *result.m_Storage;
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &state.units);
        glGenSamplers(1, &state.name);
        if (!state.name)
            return std::unexpected(SamplerError{SamplerErrorCode::Allocation, "Driver did not allocate a sampler"});
        const auto& d = result.m_Desc;
        const std::pair<GLenum, GLint> parameters[]{
            {GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minFilters[static_cast<unsigned>(d.mipFilter)][static_cast<unsigned>(d.minFilter)])},
            {GL_TEXTURE_MAG_FILTER, d.magFilter == SamplerFilter::Nearest ? GL_NEAREST : GL_LINEAR},
            {GL_TEXTURE_WRAP_S, static_cast<GLint>(wraps[static_cast<unsigned>(d.wrapU)])},
            {GL_TEXTURE_WRAP_T, static_cast<GLint>(wraps[static_cast<unsigned>(d.wrapV)])},
            {GL_TEXTURE_WRAP_R, static_cast<GLint>(wraps[static_cast<unsigned>(d.wrapW)])},
            {GL_TEXTURE_COMPARE_MODE, d.compare == SamplerCompare::Disabled ? GL_NONE : GL_COMPARE_REF_TO_TEXTURE},
            {GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(comparisons[static_cast<unsigned>(d.compare)])}};
        for (const auto& [parameter, value] : parameters)
        {
            glSamplerParameteri(state.name, parameter, value);
            GLint actual = -1;
            glGetSamplerParameteriv(state.name, parameter, &actual);
            if (actual != value)
                return std::unexpected(SamplerError{SamplerErrorCode::Driver, "Driver rejected sampler state"});
        }
        glSamplerParameterfv(state.name, GL_TEXTURE_BORDER_COLOR, d.borderColor.data());
        if (HasAnisotropy()) glSamplerParameterf(state.name, GL_TEXTURE_MAX_ANISOTROPY_EXT, d.maxAnisotropy);
        std::array<float, 4> border{};
        glGetSamplerParameterfv(state.name, GL_TEXTURE_BORDER_COLOR, border.data());
        float anisotropy = 1;
        if (HasAnisotropy()) glGetSamplerParameterfv(state.name, GL_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy);
        if (border != d.borderColor || anisotropy != d.maxAnisotropy)
            return std::unexpected(SamplerError{SamplerErrorCode::Driver, "Driver rejected sampler floating-point state"});
        return result;
    }
    std::expected<void, SamplerError> GpuSampler::ValidateUnit(std::uint32_t unit) const
    {
        GLContextThread::RequireCurrent("Sampler binding");
        if (!*this) return std::unexpected(SamplerError{SamplerErrorCode::InvalidSampler, "Empty sampler"});
        AssetDetail::RequireInvariant(m_Storage->context == SDL_GL_GetCurrentContext());
        if (unit >= static_cast<std::uint32_t>(m_Storage->units))
            return std::unexpected(SamplerError{SamplerErrorCode::InvalidUnit, "Sampler unit exceeds context limit"});
        return {};
    }
    std::expected<void, SamplerError> GpuSampler::Bind(std::uint32_t unit) const
    {
        auto valid = ValidateUnit(unit);
        if (!valid) return valid;
        if(auto* state=RenderBackend::GLStateCache::Current()) state->Sampler(unit,m_Storage->name);
        else glBindSampler(unit, m_Storage->name);
        return {};
    }

    struct SamplerCache::Entry
    {
        SamplerDesc desc;
        SamplerHandle handle;
        std::unique_ptr<Entry> next;
    };
    SamplerCache::SamplerCache(AssetPublication& publication, AssetRegistryLimits limits)
        : m_Publication(publication), m_Registry(publication, limits) {}
    SamplerCache::~SamplerCache()
    {
        auto access = m_Publication.BeginPublication();
        AssetDetail::RequireInvariant(bool(m_Registry.Close(access)));
        while (m_First)
        {
            auto next = std::move(m_First->next);
            m_First = std::move(next);
        }
    }
    std::expected<SamplerHandle, SamplerError> SamplerCache::Get(const SamplerDesc& requested)
    {
        (void)m_Registry.Size(); // Reuse the registry's owner-thread invariant before touching the cache.
        auto normalized = GpuSampler::Normalize(requested);
        if (!normalized) return std::unexpected(normalized.error());
        for (auto* entry = m_First.get(); entry; entry = entry->next.get())
            if (entry->desc == *normalized) return entry->handle;
        std::unique_ptr<Entry> pending(new (std::nothrow) Entry{*normalized, {}, {}});
        if (!pending) return std::unexpected(SamplerError{SamplerErrorCode::Allocation, "Sampler cache allocation failed"});
        auto sampler = GpuSampler::Create(*normalized);
        if (!sampler) return std::unexpected(sampler.error());
        auto access = m_Publication.BeginPublication();
        auto handle = m_Registry.Create(access, std::move(*sampler));
        if (!handle) return std::unexpected(RegistryFailure(handle.error()));
        pending->handle = *handle;
        pending->next = std::move(m_First);
        m_First = std::move(pending);
        return *handle;
    }
    std::expected<SamplerView, SamplerError> SamplerCache::Resolve(SamplerHandle handle) const
    {
        auto access = m_Publication.BeginFrame();
        auto lease = m_Registry.Acquire(access, handle);
        if (!lease) return std::unexpected(RegistryFailure(lease.error()));
        return std::move(*lease);
    }
    std::expected<void, SamplingError> SampledTextureBinding::Bind(std::uint32_t unit) const
    {
        if (!sampler) return std::unexpected(SamplingError(SamplerError{SamplerErrorCode::InvalidSampler, "Missing sampler lease"}));
        if (auto valid = sampler->ValidateUnit(unit); !valid) return std::unexpected(SamplingError(valid.error()));
        if (auto bound = texture.Bind(unit); !bound) return std::unexpected(SamplingError(bound.error()));
        if (auto bound = sampler->Bind(unit); !bound) return std::unexpected(SamplingError(bound.error()));
        return {};
    }
    std::expected<SamplerDesc, SamplingError> TextureSamplingDefaults(const TextureView& texture)
    {
        auto name = AssetDetail::TextureBackend::Name(texture);
        if (!name) return std::unexpected(SamplingError(name.error()));
        const auto target = AssetDetail::TextureBackend::Target(texture);
        GLenum binding = 0;
        switch (target)
        {
        case GL_TEXTURE_2D: binding = GL_TEXTURE_BINDING_2D; break;
        case GL_TEXTURE_CUBE_MAP: binding = GL_TEXTURE_BINDING_CUBE_MAP; break;
        case GL_TEXTURE_2D_ARRAY: binding = GL_TEXTURE_BINDING_2D_ARRAY; break;
        default: return std::unexpected(SamplingError(SamplerError{SamplerErrorCode::Unsupported, "Texture target does not support this sampler adapter"}));
        }
        GLint previous = 0; glGetIntegerv(binding, &previous);
        glBindTexture(target, *name);
        SamplerDesc d;
        GLint min = 0, mag = 0, modes[3]{}, compareMode = 0, compareFunc = 0;
        glGetTexParameteriv(target, GL_TEXTURE_MIN_FILTER, &min);
        glGetTexParameteriv(target, GL_TEXTURE_MAG_FILTER, &mag);
        glGetTexParameteriv(target, GL_TEXTURE_WRAP_S, &modes[0]);
        glGetTexParameteriv(target, GL_TEXTURE_WRAP_T, &modes[1]);
        glGetTexParameteriv(target, GL_TEXTURE_WRAP_R, &modes[2]);
        glGetTexParameteriv(target, GL_TEXTURE_COMPARE_MODE, &compareMode);
        glGetTexParameteriv(target, GL_TEXTURE_COMPARE_FUNC, &compareFunc);
        glGetTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, d.borderColor.data());
        if (HasAnisotropy()) glGetTexParameterfv(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, &d.maxAnisotropy);
        glBindTexture(target, static_cast<GLuint>(previous));
        bool foundMin = false;
        for (unsigned mip = 0; mip < 3; ++mip)
            for (unsigned filter = 0; filter < 2; ++filter)
                if (min == static_cast<GLint>(minFilters[mip][filter]))
                { d.minFilter = static_cast<SamplerFilter>(filter); d.mipFilter = static_cast<SamplerMipFilter>(mip); foundMin = true; }
        if (!foundMin || (mag != GL_NEAREST && mag != GL_LINEAR))
            return std::unexpected(SamplingError(SamplerError{SamplerErrorCode::Unsupported, "Unsupported legacy texture filter"}));
        d.magFilter = mag == GL_NEAREST ? SamplerFilter::Nearest : SamplerFilter::Linear;
        SamplerWrap* destinations[]{&d.wrapU, &d.wrapV, &d.wrapW};
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            auto found = std::find(std::begin(wraps), std::end(wraps), static_cast<GLenum>(modes[axis]));
            if (found == std::end(wraps))
                return std::unexpected(SamplingError(SamplerError{SamplerErrorCode::Unsupported, "Unsupported legacy texture wrap"}));
            *destinations[axis] = static_cast<SamplerWrap>(found - std::begin(wraps));
        }
        if (compareMode == GL_COMPARE_REF_TO_TEXTURE)
            for (unsigned i = 1; i < std::size(comparisons); ++i)
                if (compareFunc == static_cast<GLint>(comparisons[i])) d.compare = static_cast<SamplerCompare>(i);
        d.anisotropy = SamplerAnisotropy::RequireExact;
        return d;
    }
}
