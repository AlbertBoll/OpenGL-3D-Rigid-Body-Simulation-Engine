#pragma once
#include "Core/RenderTarget.h"
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string_view>

namespace GEngine
{
    enum class ShadowQuality { Low, Medium, High, Custom };
    enum class ShadowFallback { None, LowerTiers };
    struct ShadowQualityDesc
    {
        // High preserves the owner-approved 4096 visual tradeoff. Lower tiers
        // and fallback are explicit choices; 8192 is custom-only.
        ShadowQuality quality = ShadowQuality::High;
        std::uint32_t customResolution = 4096;
        std::uint64_t byteBudget = 3ull * 1024 * 1024 * 1024;
        ShadowFallback fallback = ShadowFallback::None;
        bool operator==(const ShadowQualityDesc&) const = default;
    };
    struct ShadowQualityPlan
    {
        std::uint32_t resolution{};
        std::uint64_t estimatedBytes{};
    };
    struct ShadowMemory
    {
        std::uint64_t estimatedBytes{}, allocatedDepthBytes{}, cascadeBytes{}, pointBytes{};
        // OpenGL does not expose portable physical/resident allocation overhead.
        // allocatedDepthBytes is the queried depth-image payload, not total VRAM.
        std::optional<std::uint64_t> physicalBytes;
    };
    struct ShadowQualityState
    {
        ShadowQualityDesc requested;
        ShadowQuality effective = ShadowQuality::High;
        std::uint32_t resolution{};
        ShadowMemory memory;
        std::optional<FramebufferError> fallbackReason;
        std::uint64_t successfulReplacements{};
    };
    struct ShadowTargets
    {
        std::unique_ptr<CascadeShadowFrameBuffer> cascade;
        std::unique_ptr<PointShadowFrameBuffer> point;
        ShadowQualityState state;
    };
    std::string_view ShadowQualityLabel(ShadowQuality) noexcept;
    // CPU-only descriptor validation. Budget admission occurs at allocation so
    // explicitly allowed lower-tier fallback can satisfy a smaller budget.
    [[nodiscard]] std::expected<ShadowQualityPlan, FramebufferError> PlanShadowQuality(const ShadowQualityDesc&);
    [[nodiscard]] std::expected<ShadowQualityDesc, FramebufferError> ParseShadowQuality(const char* tier, const char* resolution);
    [[nodiscard]] std::expected<ShadowQualityDesc, FramebufferError> ShadowQualityFromEnvironment();
    // Owner-context transaction: both six-layer/face targets or neither.
    // The caller retains its previous pair until this result is complete.
    [[nodiscard]] std::expected<ShadowTargets, FramebufferError> CreateShadowTargets(const ShadowQualityDesc&);
}
