#pragma once

#include <atomic>
#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <expected>
#include <cassert>
#include <exception>

namespace GEngine::Asset
{
    enum class RegistryError { IdentityExhausted, InvalidLimits, SlotsExhausted, RevisionExhausted,
        InvalidHandle, InvalidFence, Busy, Closed, RegistryCountExhausted };

    namespace AssetDetail
    {
        inline void RequireInvariant(bool condition) noexcept
        {
            if (!condition) std::terminate();
        }
    }

    // Process-local identity. Neither a GL name nor an address; not a disk ID.
    template<class Tag>
    struct AssetHandle
    {
        static constexpr std::uint32_t NullIndex = (std::numeric_limits<std::uint32_t>::max)();
        std::uint32_t index = NullIndex;
        std::uint64_t generation = 0;
        std::uint64_t registry = 0;

        explicit constexpr operator bool() const noexcept
        { return index != NullIndex && generation != 0 && registry != 0; }
        auto operator<=>(const AssetHandle&) const = default;
    };

    struct MeshAssetTag;
    struct TextureAssetTag;
    struct SamplerAssetTag;
    struct ShaderProgramAssetTag;
    struct PipelineAssetTag;
    struct MaterialTemplateAssetTag;
    struct MaterialInstanceAssetTag;
    using MeshHandle = AssetHandle<MeshAssetTag>;
    using TextureHandle = AssetHandle<TextureAssetTag>;
    using SamplerHandle = AssetHandle<SamplerAssetTag>;
    using ShaderProgramHandle = AssetHandle<ShaderProgramAssetTag>;
    using PipelineHandle = AssetHandle<PipelineAssetTag>;
    using MaterialTemplateHandle = AssetHandle<MaterialTemplateAssetTag>;
    using MaterialInstanceHandle = AssetHandle<MaterialInstanceAssetTag>;

    namespace AssetDetail
    {
        inline std::atomic<std::uint64_t> nextRegistryIdentity{1};

        // Zero is a permanent exhausted sentinel. Never wrap or recycle a domain.
        inline std::expected<std::uint64_t, RegistryError> TakeRegistryIdentity(std::atomic<std::uint64_t>& next)
        {
            auto value = next.load(std::memory_order_relaxed);
            for (;;)
            {
                if (!value) return std::unexpected(RegistryError::IdentityExhausted);
                const auto following = value == (std::numeric_limits<std::uint64_t>::max)() ? 0 : value + 1;
                if (next.compare_exchange_weak(value, following, std::memory_order_relaxed)) return value;
            }
        }
    }
}

namespace std
{
    template<class Tag>
    struct hash<GEngine::Asset::AssetHandle<Tag>>
    {
        size_t operator()(const GEngine::Asset::AssetHandle<Tag>& handle) const noexcept
        {
            size_t result = hash<uint64_t>{}(handle.registry);
            result ^= hash<uint64_t>{}(handle.generation) + 0x9e3779b9u + (result << 6) + (result >> 2);
            return result ^ (hash<uint32_t>{}(handle.index) + 0x9e3779b9u + (result << 6) + (result >> 2));
        }
    };
}
