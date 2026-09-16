#pragma once

#include "Assets/Textures/Texture.h"
#include <array>
#include <variant>

namespace GEngine::Asset
{
    enum class SamplerFilter { Nearest, Linear };
    enum class SamplerMipFilter { None, Nearest, Linear };
    enum class SamplerWrap { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder };
    enum class SamplerCompare { Disabled, Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
    enum class SamplerAnisotropy { Disabled, ClampToLimit, RequireExact };
    struct SamplerDesc
    {
        SamplerFilter minFilter = SamplerFilter::Linear, magFilter = SamplerFilter::Linear;
        SamplerMipFilter mipFilter = SamplerMipFilter::None;
        SamplerWrap wrapU = SamplerWrap::Repeat, wrapV = SamplerWrap::Repeat, wrapW = SamplerWrap::Repeat;
        SamplerCompare compare = SamplerCompare::Disabled;
        SamplerAnisotropy anisotropy = SamplerAnisotropy::Disabled;
        float maxAnisotropy = 1;
        std::array<float, 4> borderColor{};
        bool operator==(const SamplerDesc&) const = default;
    };
    enum class SamplerErrorCode { InvalidDescription, Unsupported, Allocation, Driver, Registry, InvalidSampler, InvalidUnit };
    struct SamplerError
    {
        SamplerErrorCode code;
        const char* message;
        RegistryError registry = RegistryError::InvalidHandle;
    };
    struct SamplingError
    {
        std::string message;
        std::variant<TextureError, SamplerError> cause;
        SamplingError(TextureError error) : message(error.message), cause(std::move(error)) {}
        SamplingError(SamplerError error) : message(error.message), cause(error) {}
    };

    class GpuSampler final
    {
    public:
        GpuSampler() noexcept;
        ~GpuSampler();
        GpuSampler(const GpuSampler&) = delete;
        GpuSampler& operator=(const GpuSampler&) = delete;
        GpuSampler(GpuSampler&&) noexcept;
        GpuSampler& operator=(GpuSampler&&) noexcept;
        static std::expected<SamplerDesc, SamplerError> Normalize(const SamplerDesc&);
        static std::expected<GpuSampler, SamplerError> Create(const SamplerDesc&);
        const SamplerDesc& Description() const noexcept { return m_Desc; }
        std::expected<void, SamplerError> ValidateUnit(std::uint32_t) const;
        std::expected<void, SamplerError> Bind(std::uint32_t) const;
        explicit operator bool() const noexcept;
    private:
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
        SamplerDesc m_Desc;
    };
    using SamplerRegistry = AssetRegistry<SamplerHandle, GpuSampler>;
    using SamplerView = SamplerRegistry::Lease;

    // Root-owned, owner-thread-only cache. Handles identify immutable policies;
    // leases keep resolved versions alive until the last material/frame use ends.
    class SamplerCache final
    {
    public:
        explicit SamplerCache(AssetPublication&, AssetRegistryLimits = {});
        ~SamplerCache();
        SamplerCache(const SamplerCache&) = delete;
        SamplerCache& operator=(const SamplerCache&) = delete;
        std::expected<SamplerHandle, SamplerError> Get(const SamplerDesc&);
        std::expected<SamplerView, SamplerError> Resolve(SamplerHandle) const;
        std::size_t Size() const { return m_Registry.Size(); }
    private:
        struct Entry;
        AssetPublication& m_Publication;
        SamplerRegistry m_Registry;
        std::unique_ptr<Entry> m_First;
    };

    // Normal material binding contract: no backend names, and no image duplication.
    struct SampledTextureBinding
    {
        TextureView texture;
        SamplerView sampler;
        TextureHandle TextureIdentity() const noexcept { return texture.Identity(); }
        SamplerHandle SamplerIdentity() const noexcept { return sampler.Identity(); }
        std::expected<void, SamplingError> Bind(std::uint32_t unit) const;
    };

    // New consumers can set sampling through this contract without including the
    // legacy concrete Material/shader/component headers (owned by Phases 34-35).
    class MaterialTextureBindings
    {
    public:
        virtual ~MaterialTextureBindings() = default;
        virtual std::expected<void, SamplingError> SetSampledTextureBinding(const std::string& uniform,
            const SampledTextureBinding&, std::uint32_t unit) = 0;
    };

    // Transitional adapter for image/attachment defaults authored before sampler objects.
    // Reads existing policy privately; never changes texture state or image ownership.
    std::expected<SamplerDesc, SamplingError> TextureSamplingDefaults(const TextureView&);
}
