#pragma once

#include "Mesh/MeshAsset.h"
#include <optional>
#include <variant>

namespace GEngine::GeometryTemplates
{
    struct Cube
    {
        float width = 1, height = 1, depth = 1;
    };
    struct Plane
    {
        float width = 1, depth = 1;
    };
    struct Quad
    {
        float width = 1, height = 1;
    };
    struct Grid
    {
        float width = 1, depth = 1;
        std::uint32_t cellsX = 1, cellsZ = 1;
    };
    using Request = std::variant<Cube, Plane, Quad, Grid>;
    enum class Kind : std::uint8_t
    {
        Cube,
        Plane,
        Quad,
        Grid
    };
    // Exact validated Float32 values; unused dimensions/counts are zero.
    struct Key
    {
        Kind kind;
        std::array<float, 3> dimensions{};
        std::uint32_t cellsX = 0, cellsZ = 0;
        bool operator==(const Key&) const = default;
    };
    enum class ErrorCode : std::uint8_t
    {
        InvalidDimension,
        InvalidCells,
        SizeOverflow,
        PayloadLimit,
        Allocation,
        Mesh
    };
    struct Error
    {
        ErrorCode code;
        std::size_t element = 0;
        std::optional<MeshError> mesh;
    };
    inline constexpr float MinimumDimension = .001f, MaximumDimension = 10000.f;
    inline constexpr std::uint32_t MaximumCells = 256;
    inline constexpr std::size_t MaximumPayloadBytes = 8 * 1024 * 1024;
    inline constexpr std::size_t MaximumSharedTemplates = 128;

    // CPU only. Validation precedes allocation; generation mints no GPU identity.
    std::expected<Key, Error> KeyFor(const Request&) noexcept;
    std::expected<MeshAsset, Error> Generate(const Request&) noexcept;
}
