#pragma once

#include "Mesh/MeshAsset.h"
#include <glm/vec2.hpp>
#include <optional>
#include <variant>

namespace GEngine::GeometryTemplates
{
    enum class ShadingMode : std::uint8_t
    {
        PrimitiveDefault,
        Smooth,
        Flat
    };
    enum class TangentMode : std::uint8_t
    {
        PrimitiveDefault,
        Generate,
        Omit
    };
    enum class PivotLocation : std::uint8_t
    {
        Center,
        Base,
        MinimumCorner
    };
    enum class DiamondUVLayout : std::uint8_t
    {
        LegacyReference,
        PerFace
    };

    struct GeometryGenerationOptions
    {
        bool GenerateUVs = true;
        glm::vec2 UVTiling{1.f, 1.f};
        ShadingMode Shading = ShadingMode::PrimitiveDefault;
        TangentMode Tangents = TangentMode::PrimitiveDefault;
        PivotLocation Pivot = PivotLocation::Center;
    };
    struct Cube
    {
        float width = 1, height = 1, depth = 1;
        GeometryGenerationOptions Options{};
    };
    struct Plane
    {
        float width = 1, depth = 1;
        GeometryGenerationOptions Options{};
    };
    struct Quad
    {
        float width = 1, height = 1;
        GeometryGenerationOptions Options{};
    };
    struct Grid
    {
        float width = 1, depth = 1;
        std::uint32_t cellsX = 1, cellsZ = 1;
        GeometryGenerationOptions Options{};
    };
    struct Sphere
    {
        float radius = 1;
        std::uint32_t segments = 64, rings = 32;
        GeometryGenerationOptions Options{};
    };
    struct Cylinder
    {
        float radius = 1, height = 2;
        std::uint32_t segments = 32, heightSegments = 1;
        GeometryGenerationOptions Options{};
    };
    struct Cone
    {
        float radius = 1, height = 2;
        std::uint32_t segments = 32, heightSegments = 1;
        GeometryGenerationOptions Options{};
    };
    struct Capsule
    {
        float radius = 1, height = 3; // Total tip-to-tip height; cylindrical span must be positive.
        std::uint32_t segments = 32, hemisphereRings = 8, heightSegments = 1;
        GeometryGenerationOptions Options{};
    };
    struct Torus
    {
        float majorRadius = 1, minorRadius = .4f;
        std::uint32_t majorSegments = 32, minorSegments = 16;
        GeometryGenerationOptions Options{};
    };
    struct Diamond
    {
        float radius = 1, height = 1.4f;
        DiamondUVLayout UVLayout = DiamondUVLayout::LegacyReference;
        GeometryGenerationOptions Options{};
    };

    using Request =
        std::variant<Cube, Plane, Quad, Grid, Sphere, Cylinder, Cone, Capsule, Torus, Diamond>;
    enum class Kind : std::uint8_t
    {
        Cube,
        Plane,
        Quad,
        Grid,
        Sphere,
        Cylinder,
        Cone,
        Capsule,
        Torus,
        Diamond
    };
    // Exact validated Float32 values. Unused fields are zero; default modes are resolved by kind.
    struct Key
    {
        Kind kind{};
        std::array<float, 3> dimensions{};
        std::uint32_t cellsX = 0, cellsZ = 0;
        std::array<std::uint32_t, 3> subdivisions{};
        bool generateUVs = true;
        std::array<float, 2> uvTiling{1, 1};
        ShadingMode shading = ShadingMode::Smooth;
        TangentMode tangents = TangentMode::Generate;
        PivotLocation pivot = PivotLocation::Center;
        DiamondUVLayout uvLayout = DiamondUVLayout::LegacyReference;
        bool operator==(const Key&) const = default;
    };
    enum class ErrorCode : std::uint8_t
    {
        InvalidDimension,
        InvalidCells,
        SizeOverflow,
        PayloadLimit,
        Allocation,
        Mesh,
        InvalidRadius,
        InvalidSegments,
        InvalidRings,
        InvalidHeightSegments,
        InvalidCapsuleSpan,
        InvalidTorusRelation,
        InvalidShadingMode,
        InvalidTangentMode,
        InvalidPivot,
        InvalidUVTiling,
        InvalidUVLayout,
        InactiveOption,
        TangentsRequireUV,
        UnsupportedTangentUVLayout,
        DegenerateGeometry,
        DegenerateUV,
        WorkingMemoryLimit
    };
    struct Error
    {
        ErrorCode code;
        std::size_t element = 0;
        std::optional<MeshError> mesh;
    };
    inline constexpr float MinimumDimension = .001f, MaximumDimension = 10000.f;
    inline constexpr float MinimumRadius = .001f, MaximumRadius = 5000.f;
    inline constexpr float MinimumUVTiling = .001f, MaximumUVTiling = 1024.f;
    inline constexpr std::uint32_t MaximumCells = 256, MaximumSegments = 128;
    inline constexpr std::uint32_t MaximumRings = 64, MaximumHeightSegments = 64;
    inline constexpr std::uint32_t MaximumHemisphereRings = 32, MaximumMinorSegments = 64;
    inline constexpr std::size_t MaximumPayloadBytes = 8 * 1024 * 1024;
    inline constexpr std::size_t MaximumWorkingBytes = 24 * 1024 * 1024;
    inline constexpr std::size_t MaximumSharedTemplates = 128;

    // CPU only. Validation/default resolution precedes allocation and cache lookup.
    // KeyFor does not mutate the authored request; generation mints no GPU/collider identity.
    std::expected<Key, Error> KeyFor(const Request&) noexcept;
    std::expected<MeshAsset, Error> Generate(const Request&) noexcept;
}
