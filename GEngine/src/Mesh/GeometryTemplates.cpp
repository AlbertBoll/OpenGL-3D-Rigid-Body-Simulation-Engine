#include "gepch.h"
#include "Mesh/GeometryTemplates.h"
#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstring>
#include <limits>
#include <new>
#include <numbers>

namespace GEngine::GeometryTemplates
{
    namespace
    {
        using D3 = glm::dvec3;
        constexpr double Pi = std::numbers::pi_v<double>;
        struct Vertex
        {
            std::array<float, 3> position{};
            std::array<float, 2> uv{};
            std::array<float, 3> normal{}, tangent{}, bitangent{};
        };
        static_assert(sizeof(Vertex) == 14 * sizeof(float));
        D3 Read(const std::array<float, 3>& v)
        {
            return {v[0], v[1], v[2]};
        }
        std::array<float, 3> Write(D3 v)
        {
            return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
        }
        bool Unit(D3 v, D3& result)
        {
            const double squared = glm::dot(v, v);
            if (!std::isfinite(squared) || squared <= 0)
                return false;
            result = v / std::sqrt(squared);
            return true;
        }
        bool Multiply(std::size_t a, std::size_t b, std::size_t& result) noexcept
        {
            if (b && a > (std::numeric_limits<std::size_t>::max)() / b)
                return false;
            result = a * b;
            return true;
        }
        bool Add(std::size_t a, std::size_t b, std::size_t& result) noexcept
        {
            if (a > (std::numeric_limits<std::size_t>::max)() - b)
                return false;
            result = a + b;
            return true;
        }
        bool Range(float value, float lo, float hi)
        {
            return std::isfinite(value) && value >= lo && value <= hi;
        }

        // Frozen from the existing Diamond's two legacy hull passes under the pinned toolchain.
        // Generation uses only these CPU values; Physics/legacy Geometry is a validation reference.
        constexpr std::array<std::array<float, 3>, 56> DiamondPositions{
            {{1.0f, 0.0f, 0.0f},
             {-1.0f, 0.100000001f, 2.98023224e-07f},
             {-0.0707106963f, -1.0f, -0.070710659f},
             {-1.1920929e-07f, 0.100000001f, -0.99999994f},
             {0.100000001f, -1.0f, 0.0f},
             {1.0f, 0.100000001f, 0.0f},
             {0.923879743f, 0.100000001f, 0.3826828f},
             {0.923879743f, 0.0f, 0.3826828f},
             {0.707107246f, 0.100000001f, 0.707106292f},
             {0.707107246f, 0.0f, 0.707106292f},
             {0.739103794f, 0.300000012f, 0.306146204f},
             {0.923879504f, 0.100000001f, -0.382683426f},
             {0.923879504f, 0.0f, -0.382683426f},
             {0.707106709f, 0.100000001f, -0.707106829f},
             {0.707106709f, 0.0f, -0.707106829f},
             {0.739103615f, 0.300000012f, -0.306146741f},
             {0.400000006f, 0.400000006f, 0.0f},
             {0.0707106739f, -1.0f, -0.0707106814f},
             {0.382683337f, 0.100000001f, -0.923879504f},
             {0.382683337f, 0.0f, -0.923879504f},
             {-1.1920929e-07f, 0.0f, -0.99999994f},
             {0.306146681f, 0.300000012f, -0.739103615f},
             {0.282842696f, 0.400000006f, -0.282842726f},
             {-1.19209291e-08f, -1.0f, -0.099999994f},
             {-0.382683516f, 0.100000001f, -0.923879385f},
             {-0.382683516f, 0.0f, -0.923879385f},
             {-0.707106948f, 0.100000001f, -0.70710659f},
             {-0.707106948f, 0.0f, -0.70710659f},
             {-0.306146801f, 0.300000012f, -0.739103496f},
             {-4.76837165e-08f, 0.400000006f, -0.399999976f},
             {-0.923879623f, 0.100000001f, -0.382683158f},
             {-0.923879623f, 0.0f, -0.382683158f},
             {-1.0f, 0.0f, 2.98023224e-07f},
             {-0.739103675f, 0.300000012f, -0.306146562f},
             {-0.282842785f, 0.400000006f, -0.282842636f},
             {-0.100000001f, -1.0f, 2.98023224e-08f},
             {-0.923879385f, 0.100000001f, 0.382683694f},
             {-0.923879385f, 0.0f, 0.382683694f},
             {-0.707106471f, 0.100000001f, 0.707107008f},
             {-0.707106471f, 0.0f, 0.707107008f},
             {-0.739103496f, 0.300000012f, 0.30614695f},
             {-0.400000006f, 0.400000006f, 1.1920929e-07f},
             {-0.0707106516f, -1.0f, 0.0707107037f},
             {-0.382683009f, 0.100000001f, 0.923879623f},
             {-0.382683009f, 0.0f, 0.923879623f},
             {5.36441803e-07f, 0.100000001f, 0.99999994f},
             {5.36441803e-07f, 0.0f, 0.99999994f},
             {-0.306146443f, 0.300000012f, 0.739103675f},
             {-0.282842606f, 0.400000006f, 0.282842815f},
             {5.36441824e-08f, -1.0f, 0.099999994f},
             {0.382683903f, 0.100000001f, 0.923879266f},
             {0.382683903f, 0.0f, 0.923879266f},
             {0.306147099f, 0.300000012f, 0.739103377f},
             {2.1457673e-07f, 0.400000006f, 0.399999976f},
             {0.0707107261f, -1.0f, 0.0707106292f},
             {0.282842904f, 0.400000006f, 0.282842517f}}};
        constexpr std::array<std::array<std::uint32_t, 3>, 108> DiamondTriangles{
            {{0, 5, 6},    {4, 0, 7},    {0, 6, 7},    {7, 6, 8},    {7, 8, 9},    {8, 6, 10},
             {6, 5, 10},   {0, 4, 12},   {5, 0, 12},   {11, 5, 12},  {11, 12, 13}, {13, 12, 14},
             {11, 13, 15}, {10, 5, 15},  {5, 11, 15},  {10, 15, 16}, {4, 2, 17},   {12, 4, 17},
             {14, 12, 17}, {13, 14, 18}, {3, 18, 19},  {14, 17, 19}, {18, 14, 19}, {3, 19, 20},
             {15, 13, 21}, {13, 18, 21}, {18, 3, 21},  {16, 15, 22}, {15, 21, 22}, {19, 17, 23},
             {20, 19, 23}, {17, 2, 23},  {3, 20, 24},  {24, 20, 25}, {20, 23, 25}, {23, 2, 25},
             {25, 2, 27},  {24, 25, 27}, {26, 24, 27}, {24, 26, 28}, {21, 3, 28},  {3, 24, 28},
             {22, 21, 29}, {21, 28, 29}, {16, 22, 29}, {26, 27, 30}, {27, 2, 31},  {30, 27, 31},
             {1, 30, 31},  {1, 31, 32},  {28, 26, 33}, {26, 30, 33}, {30, 1, 33},  {29, 28, 34},
             {28, 33, 34}, {16, 29, 34}, {2, 4, 35},   {31, 2, 35},  {32, 31, 35}, {1, 32, 36},
             {32, 35, 37}, {36, 32, 37}, {36, 37, 38}, {38, 37, 39}, {36, 38, 40}, {33, 1, 40},
             {1, 36, 40},  {16, 34, 41}, {34, 33, 41}, {33, 40, 41}, {35, 4, 42},  {37, 35, 42},
             {39, 37, 42}, {38, 39, 43}, {39, 42, 44}, {43, 39, 44}, {43, 44, 45}, {45, 44, 46},
             {43, 45, 47}, {40, 38, 47}, {38, 43, 47}, {41, 40, 48}, {40, 47, 48}, {16, 41, 48},
             {42, 4, 49},  {44, 42, 49}, {46, 44, 49}, {45, 46, 50}, {9, 8, 50},   {9, 50, 51},
             {50, 46, 51}, {46, 49, 51}, {8, 10, 52},  {50, 8, 52},  {47, 45, 52}, {45, 50, 52},
             {48, 47, 53}, {47, 52, 53}, {16, 48, 53}, {9, 51, 54},  {51, 49, 54}, {49, 4, 54},
             {4, 7, 54},   {7, 9, 54},   {16, 53, 55}, {53, 52, 55}, {10, 16, 55}, {52, 10, 55}}};

        struct Counts
        {
            std::size_t smooth = 0, output = 0, indices = 0, stride = 0;
            bool split = false;
        };
        std::expected<Counts, Error> Size(const Key& k)
        {
            Counts c;
            const std::size_t s = k.subdivisions[0], n = k.subdivisions[1], h = k.subdivisions[2];
            std::size_t triangles = 0, a = 0, b = 0;
            if (k.kind == Kind::Cube)
            {
                c.smooth = 24;
                c.indices = 36;
            }
            else if (k.kind == Kind::Grid)
            {
                if (!Add(k.cellsX, 1, a) || !Add(k.cellsZ, 1, b) || !Multiply(a, b, c.smooth) ||
                    !Multiply(k.cellsX, k.cellsZ, a) || !Multiply(a, 6, c.indices))
                    return std::unexpected(Error{ErrorCode::SizeOverflow});
            }
            else if (k.kind == Kind::Plane || k.kind == Kind::Quad)
            {
                c.smooth = 4;
                c.indices = 6;
            }
            else
            {
                // All operands are already bounded in KeyFor; keep checked payload arithmetic below.
                switch (k.kind)
                {
                case Kind::Sphere:
                    triangles = 2 * s * (n - 1);
                    c.smooth = (s + 1) * (n - 1) + 2 * s;
                    break;
                case Kind::Cylinder:
                    triangles = 2 * s * (n + 1);
                    c.smooth = (s + 1) * (n + 1) + 2 * (s + 2);
                    break;
                case Kind::Cone:
                    triangles = 2 * s * n;
                    c.smooth = (s + 1) * n + s + (s + 2);
                    break;
                case Kind::Capsule:
                    triangles = 2 * s * (2 * n + h - 1);
                    c.smooth = (s + 1) * (2 * n + h - 1) + 2 * s;
                    break;
                case Kind::Torus:
                    triangles = 2 * s * n;
                    c.smooth = (s + 1) * (n + 1);
                    break;
                case Kind::Diamond:
                    triangles = DiamondTriangles.size();
                    c.smooth = DiamondPositions.size();
                    break;
                default:
                    break;
                }
                if (!Multiply(triangles, 3, c.indices))
                    return std::unexpected(Error{ErrorCode::SizeOverflow});
                c.split = k.shading == ShadingMode::Flat ||
                          (k.kind == Kind::Diamond && k.uvLayout == DiamondUVLayout::PerFace);
            }
            c.output = c.split ? c.indices : c.smooth;
            c.stride =
                24 + (k.generateUVs ? 8 : 0) + (k.tangents == TangentMode::Generate ? 24 : 0);
            std::size_t vertices, indices, payload, smooth, split, outputCopies, indexCopies,
                working;
            if (c.smooth > (std::numeric_limits<std::uint32_t>::max)() ||
                c.output > (std::numeric_limits<std::uint32_t>::max)() ||
                !Multiply(c.output, c.stride, vertices) || !Multiply(c.indices, 4, indices) ||
                !Add(vertices, indices, payload) || !Multiply(c.smooth, sizeof(Vertex), smooth) ||
                !Multiply(c.split ? c.output : 0, sizeof(Vertex), split) ||
                !Multiply(vertices, 2, outputCopies) || !Multiply(indices, 2, indexCopies) ||
                !Add(smooth, split, working) || !Add(working, outputCopies, working) ||
                !Add(working, indexCopies, working) || !Add(working, 4096, working))
                return std::unexpected(Error{ErrorCode::SizeOverflow});
            if (payload > MaximumPayloadBytes)
                return std::unexpected(Error{ErrorCode::PayloadLimit, payload});
            if (working > MaximumWorkingBytes)
                return std::unexpected(Error{ErrorCode::WorkingMemoryLimit, working});
            return c;
        }
    }

    std::expected<Key, Error> KeyFor(const Request& request) noexcept
    {
        return std::visit(
            [](const auto& value) -> std::expected<Key, Error>
            {
                using T = std::decay_t<decltype(value)>;
                const auto& options = value.Options;
                if (options.Shading != ShadingMode::PrimitiveDefault &&
                    options.Shading != ShadingMode::Smooth && options.Shading != ShadingMode::Flat)
                    return std::unexpected(Error{ErrorCode::InvalidShadingMode});
                if (options.Tangents != TangentMode::PrimitiveDefault &&
                    options.Tangents != TangentMode::Generate &&
                    options.Tangents != TangentMode::Omit)
                    return std::unexpected(Error{ErrorCode::InvalidTangentMode});
                if (options.Pivot != PivotLocation::Center &&
                    options.Pivot != PivotLocation::Base &&
                    options.Pivot != PivotLocation::MinimumCorner)
                    return std::unexpected(Error{ErrorCode::InvalidPivot});
                Key k;
                std::size_t dimensions = 2;
                if constexpr (std::same_as<T, Cube>)
                {
                    k.kind = Kind::Cube;
                    k.dimensions = {value.width, value.height, value.depth};
                    dimensions = 3;
                }
                else if constexpr (std::same_as<T, Plane>)
                {
                    k.kind = Kind::Plane;
                    k.dimensions = {value.width, value.depth, 0};
                }
                else if constexpr (std::same_as<T, Quad>)
                {
                    k.kind = Kind::Quad;
                    k.dimensions = {value.width, value.height, 0};
                }
                else if constexpr (std::same_as<T, Grid>)
                {
                    k.kind = Kind::Grid;
                    k.dimensions = {value.width, value.depth, 0};
                    k.cellsX = value.cellsX;
                    k.cellsZ = value.cellsZ;
                    if (!value.cellsX || value.cellsX > MaximumCells)
                        return std::unexpected(Error{ErrorCode::InvalidCells, 0});
                    if (!value.cellsZ || value.cellsZ > MaximumCells)
                        return std::unexpected(Error{ErrorCode::InvalidCells, 1});
                }
                else if constexpr (std::same_as<T, Sphere>)
                {
                    k.kind = Kind::Sphere;
                    k.dimensions = {value.radius, 0, 0};
                    k.subdivisions = {value.segments, value.rings, 0};
                    dimensions = 1;
                }
                else if constexpr (std::same_as<T, Torus>)
                {
                    k.kind = Kind::Torus;
                    k.dimensions = {value.majorRadius, value.minorRadius, 0};
                    k.subdivisions = {value.majorSegments, value.minorSegments, 0};
                }
                else if constexpr (std::same_as<T, Diamond>)
                {
                    k.kind = Kind::Diamond;
                    k.dimensions = {value.radius, value.height, 0};
                    k.uvLayout = value.UVLayout;
                    if (k.uvLayout != DiamondUVLayout::LegacyReference &&
                        k.uvLayout != DiamondUVLayout::PerFace)
                        return std::unexpected(Error{ErrorCode::InvalidUVLayout});
                }
                else
                {
                    k.dimensions = {value.radius, value.height, 0};
                    if constexpr (std::same_as<T, Cylinder>)
                    {
                        k.kind = Kind::Cylinder;
                        k.subdivisions = {value.segments, value.heightSegments, 0};
                    }
                    else if constexpr (std::same_as<T, Cone>)
                    {
                        k.kind = Kind::Cone;
                        k.subdivisions = {value.segments, value.heightSegments, 0};
                    }
                    else
                    {
                        k.kind = Kind::Capsule;
                        k.subdivisions = {value.segments, value.hemisphereRings,
                                          value.heightSegments};
                    }
                }
                const bool earlier = k.kind <= Kind::Grid;
                for (std::size_t i = 0; i < dimensions; ++i)
                {
                    const bool radius = !earlier && (i == 0 || k.kind == Kind::Torus);
                    if (!Range(k.dimensions[i], radius ? MinimumRadius : MinimumDimension,
                               radius ? MaximumRadius : MaximumDimension))
                        return std::unexpected(Error{
                            radius ? ErrorCode::InvalidRadius : ErrorCode::InvalidDimension, i});
                }
                if (!earlier && k.kind != Kind::Diamond)
                {
                    if (k.subdivisions[0] < 3 || k.subdivisions[0] > MaximumSegments)
                        return std::unexpected(Error{ErrorCode::InvalidSegments, 0});
                    const bool axial = k.kind == Kind::Cylinder || k.kind == Kind::Cone;
                    const std::uint32_t minimum = axial || k.kind == Kind::Capsule ? 1
                                                  : k.kind == Kind::Torus          ? 3
                                                                                   : 2;
                    const std::uint32_t maximum =
                        k.kind == Kind::Capsule ? MaximumHemisphereRings : MaximumRings;
                    if (k.subdivisions[1] < minimum || k.subdivisions[1] > maximum)
                        return std::unexpected(Error{
                            axial ? ErrorCode::InvalidHeightSegments : ErrorCode::InvalidRings, 1});
                    if (k.kind == Kind::Capsule &&
                        (!k.subdivisions[2] || k.subdivisions[2] > MaximumHeightSegments))
                        return std::unexpected(Error{ErrorCode::InvalidHeightSegments, 2});
                }
                if (k.kind == Kind::Capsule &&
                    double(k.dimensions[1]) - 2 * double(k.dimensions[0]) <
                        double(MinimumDimension))
                    return std::unexpected(Error{ErrorCode::InvalidCapsuleSpan});
                if (k.kind == Kind::Torus &&
                    (double(k.dimensions[0]) - double(k.dimensions[1]) < double(MinimumDimension) ||
                     double(k.dimensions[0]) + double(k.dimensions[1]) > double(MaximumRadius)))
                    return std::unexpected(Error{ErrorCode::InvalidTorusRelation});
                k.shading = options.Shading == ShadingMode::PrimitiveDefault
                                ? (k.kind == Kind::Cube ? ShadingMode::Flat : ShadingMode::Smooth)
                                : options.Shading;
                k.tangents = options.Tangents == TangentMode::PrimitiveDefault
                                 ? (earlier ? TangentMode::Generate : TangentMode::Omit)
                                 : options.Tangents;
                k.generateUVs = options.GenerateUVs;
                k.uvTiling = {options.UVTiling.x, options.UVTiling.y};
                k.pivot = options.Pivot;
                if (k.tangents == TangentMode::Generate && !k.generateUVs)
                    return std::unexpected(Error{ErrorCode::TangentsRequireUV});
                for (std::size_t i = 0; i < 2; ++i)
                    if (!Range(k.uvTiling[i], MinimumUVTiling, MaximumUVTiling))
                        return std::unexpected(Error{ErrorCode::InvalidUVTiling, i});
                if (!k.generateUVs && (k.uvTiling != std::array<float, 2>{1, 1} ||
                                       k.uvLayout != DiamondUVLayout::LegacyReference))
                    return std::unexpected(Error{ErrorCode::InactiveOption});
                if (k.kind == Kind::Diamond && k.uvLayout == DiamondUVLayout::LegacyReference &&
                    k.tangents == TangentMode::Generate)
                    return std::unexpected(Error{ErrorCode::UnsupportedTangentUVLayout});
                return k;
            },
            request);
    }

    std::expected<MeshAsset, Error> Generate(const Request& request) noexcept
    {
        const auto key = KeyFor(request);
        if (!key)
            return std::unexpected(key.error());
        const Key& k = *key;
        const auto sized = Size(k);
        if (!sized)
            return std::unexpected(sized.error());
        const Counts c = *sized;
        std::unique_ptr<Vertex[]> smooth(new (std::nothrow) Vertex[c.smooth]);
        std::unique_ptr<std::uint32_t[]> indices(new (std::nothrow) std::uint32_t[c.indices]);
        if (!smooth || !indices)
            return std::unexpected(Error{ErrorCode::Allocation});
        Vertex* vertices = smooth.get();
        std::size_t v = 0, ix = 0;
        auto put = [&](D3 position, D3 normal, double u, double uvV)
        {
            vertices[v].position = Write(position);
            vertices[v].normal = Write(normal);
            vertices[v].uv = {static_cast<float>(u), static_cast<float>(uvV)};
            return static_cast<std::uint32_t>(v++);
        };
        auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t d)
        {
            indices[ix++] = a;
            indices[ix++] = b;
            indices[ix++] = d;
        };
        auto stitch = [&](std::uint32_t first, std::uint32_t columns, std::uint32_t rows)
        {
            for (std::uint32_t j = 0; j + 1 < rows; ++j)
                for (std::uint32_t i = 0; i + 1 < columns; ++i)
                {
                    const auto a = first + j * columns + i;
                    triangle(a, a + 1, a + columns + 1);
                    triangle(a, a + columns + 1, a + columns);
                }
        };
        auto surface = [&](std::size_t firstVertex, std::size_t firstIndex,
                           std::array<float, 3> origin, std::array<float, 3> tangent,
                           std::array<float, 3> bitangent, float width, float height,
                           std::uint32_t cellsX = 1, std::uint32_t cellsY = 1)
        {
            const std::array<float, 3> normal{tangent[1] * bitangent[2] - tangent[2] * bitangent[1],
                                              tangent[2] * bitangent[0] - tangent[0] * bitangent[2],
                                              tangent[0] * bitangent[1] -
                                                  tangent[1] * bitangent[0]};
            const auto stride = cellsX + 1;
            for (std::uint32_t y = 0; y <= cellsY; ++y)
                for (std::uint32_t x = 0; x <= cellsX; ++x)
                {
                    const float u = static_cast<float>(x) / cellsX;
                    const float uvV = static_cast<float>(y) / cellsY;
                    auto& vertex = vertices[firstVertex + y * stride + x];
                    vertex.uv = {u, uvV};
                    vertex.normal = normal;
                    vertex.tangent = tangent;
                    vertex.bitangent = bitangent;
                    for (std::size_t axis = 0; axis < 3; ++axis)
                        vertex.position[axis] = origin[axis] + tangent[axis] * width * u +
                                                bitangent[axis] * height * uvV;
                }
            for (std::uint32_t y = 0; y < cellsY; ++y)
                for (std::uint32_t x = 0; x < cellsX; ++x)
                {
                    const auto base = static_cast<std::uint32_t>(firstVertex) + y * stride + x;
                    for (auto index : {base, base + 1, base + stride + 1, base, base + stride + 1,
                                       base + stride})
                        indices[firstIndex++] = index;
                }
        };
        const float w = k.dimensions[0], height = k.dimensions[1];
        const std::uint32_t s = k.subdivisions[0], n = k.subdivisions[1], h = k.subdivisions[2];
        auto phase = [&](std::uint32_t i)
        {
            return i == s ? 0.0 : 2 * Pi * i / s;
        };
        if (k.kind == Kind::Cube)
        {
            const float d = k.dimensions[2], x = w / 2, y = height / 2, z = d / 2;
            surface(0, 0, {x, -y, z}, {0, 0, -1}, {0, 1, 0}, d, height);
            surface(4, 6, {-x, -y, -z}, {0, 0, 1}, {0, 1, 0}, d, height);
            surface(8, 12, {-x, y, z}, {1, 0, 0}, {0, 0, -1}, w, d);
            surface(12, 18, {-x, -y, -z}, {1, 0, 0}, {0, 0, 1}, w, d);
            surface(16, 24, {-x, -y, z}, {1, 0, 0}, {0, 1, 0}, w, height);
            surface(20, 30, {x, -y, -z}, {-1, 0, 0}, {0, 1, 0}, w, height);
            if (k.shading == ShadingMode::Smooth)
                for (std::size_t i = 0; i < c.smooth; ++i)
                {
                    D3 normal;
                    Unit({vertices[i].position[0] / double(w),
                          vertices[i].position[1] / double(height),
                          vertices[i].position[2] / double(d)},
                         normal);
                    vertices[i].normal = Write(normal);
                }
            v = c.smooth;
            ix = c.indices;
        }
        else if (k.kind == Kind::Quad || k.kind == Kind::Plane || k.kind == Kind::Grid)
        {
            if (k.kind == Kind::Quad)
                surface(0, 0, {-w / 2, -height / 2, 0}, {1, 0, 0}, {0, 1, 0}, w, height);
            else
                surface(0, 0, {-w / 2, 0, height / 2}, {1, 0, 0}, {0, 0, -1}, w, height,
                        k.kind == Kind::Grid ? k.cellsX : 1, k.kind == Kind::Grid ? k.cellsZ : 1);
            v = c.smooth;
            ix = c.indices;
        }
        else if (k.kind == Kind::Diamond)
        {
            std::array<glm::vec3, 56> unitNormals{};
            std::array<D3, 56> scaledNormals{};
            unitNormals.fill(glm::vec3(0));
            scaledNormals.fill(D3(0));
            for (const auto& t : DiamondTriangles)
            {
                auto a = Read(DiamondPositions[t[0]]), b = Read(DiamondPositions[t[1]]),
                     d = Read(DiamondPositions[t[2]]);
                const auto face = glm::cross(glm::vec3(b - a), glm::vec3(d - a));
                a *= D3(w, double(height) / double(1.4f), w);
                b *= D3(w, double(height) / double(1.4f), w);
                d *= D3(w, double(height) / double(1.4f), w);
                const auto scaled = glm::cross(b - a, d - a);
                for (auto i : t)
                {
                    unitNormals[i] += face;
                    scaledNormals[i] += scaled;
                }
                triangle(t[0], t[1], t[2]);
            }
            for (std::size_t i = 0; i < DiamondPositions.size(); ++i)
            {
                D3 normal;
                const auto reference = glm::normalize(unitNormals[i]);
                if (!Unit(scaledNormals[i], normal) || !std::isfinite(reference.x) ||
                    !std::isfinite(reference.y) || !std::isfinite(reference.z))
                    return std::unexpected(Error{ErrorCode::DegenerateGeometry, i});
                put(Read(DiamondPositions[i]) * D3(w, double(height) / double(1.4f), w), normal,
                    std::atan2(reference.x, reference.z) / (2.f * std::numbers::pi_v<float>)+.5f,
                    reference.y * .5f + .5f);
            }
        }
        else if (k.kind == Kind::Torus)
        {
            for (std::uint32_t j = 0; j <= n; ++j)
            {
                const double t = j == n ? 0.0 : 2 * Pi * j / n;
                for (std::uint32_t i = 0; i <= s; ++i)
                {
                    const double p = phase(i), radial = double(w) + double(height) * std::cos(t);
                    put({radial * std::cos(p), double(height) * std::sin(t), -radial * std::sin(p)},
                        {std::cos(t) * std::cos(p), std::sin(t), -std::cos(t) * std::sin(p)},
                        double(i) / s, double(j) / n);
                }
            }
            stitch(0, s + 1, n + 1);
        }
        else
        {
            auto ring =
                [&](double radius, double y, double radialNormal, double normalY, double uvV)
            {
                for (std::uint32_t i = 0; i <= s; ++i)
                {
                    const double p = phase(i);
                    D3 normal;
                    Unit({radialNormal * std::cos(p), normalY, -radialNormal * std::sin(p)},
                         normal);
                    put({radius * std::cos(p), y, -radius * std::sin(p)}, normal, double(i) / s,
                        uvV);
                }
            };
            auto cap = [&](double y, bool top)
            {
                const auto center = put({0, y, 0}, {0, top ? 1.0 : -1.0, 0}, .5, .5);
                const auto rim = static_cast<std::uint32_t>(v);
                for (std::uint32_t i = 0; i <= s; ++i)
                {
                    const double p = phase(i), x = std::cos(p), z = -std::sin(p);
                    put({double(w) * x, y, double(w) * z}, {0, top ? 1.0 : -1.0, 0}, .5 + .5 * x,
                        .5 + (top ? -.5 : .5) * z);
                }
                for (std::uint32_t i = 0; i < s; ++i)
                    if (top)
                        triangle(center, rim + i, rim + i + 1);
                    else
                        triangle(center, rim + i + 1, rim + i);
            };
            if (k.kind == Kind::Cylinder || k.kind == Kind::Cone)
            {
                const bool cone = k.kind == Kind::Cone;
                for (std::uint32_t j = 0; j < (cone ? n : n + 1); ++j)
                    ring(double(w) * (cone ? 1 - double(j) / n : 1),
                         -double(height) / 2 + double(height) * j / n, cone ? double(height) : 1.0,
                         cone ? double(w) : 0.0, double(j) / n);
                stitch(0, s + 1, cone ? n : n + 1);
                if (cone)
                    for (std::uint32_t i = 0; i < s; ++i)
                    {
                        const double p = 2 * Pi * (double(i) + .5) / s;
                        D3 normal;
                        Unit({double(height) * std::cos(p), w, -double(height) * std::sin(p)},
                             normal);
                        const auto apex =
                            put({0, double(height) / 2, 0}, normal, (double(i) + .5) / s, 1);
                        triangle((n - 1) * (s + 1) + i, (n - 1) * (s + 1) + i + 1, apex);
                    }
                cap(-double(height) / 2, false);
                if (!cone)
                    cap(double(height) / 2, true);
            }
            else
            {
                const bool sphere = k.kind == Kind::Sphere;
                const double span = sphere ? 0 : double(height) - 2 * double(w);
                const double totalArc = Pi * double(w) + span;
                if (sphere)
                    for (std::uint32_t j = 1; j < n; ++j)
                    {
                        const double t = -Pi / 2 + Pi * j / n;
                        ring(double(w) * std::cos(t), double(w) * std::sin(t), std::cos(t),
                             std::sin(t), double(j) / n);
                    }
                else
                {
                    for (std::uint32_t j = 1; j <= n; ++j)
                    {
                        const double t = -Pi / 2 + Pi * j / (2 * n);
                        ring(double(w) * std::cos(t), -span / 2 + double(w) * std::sin(t),
                             std::cos(t), std::sin(t), double(w) * (t + Pi / 2) / totalArc);
                    }
                    for (std::uint32_t j = 1; j < h; ++j)
                        ring(w, -span / 2 + span * j / h, 1, 0,
                             (Pi * double(w) / 2 + span * j / h) / totalArc);
                    for (std::uint32_t j = 0; j < n; ++j)
                    {
                        const double t = Pi * j / (2 * n);
                        ring(double(w) * std::cos(t), span / 2 + double(w) * std::sin(t),
                             std::cos(t), std::sin(t),
                             (Pi * double(w) / 2 + span + double(w) * t) / totalArc);
                    }
                }
                const auto rows = sphere ? n - 1 : 2 * n + h - 1;
                stitch(0, s + 1, rows);
                for (std::uint32_t i = 0; i < s; ++i)
                {
                    const auto bottom =
                        put({0, -double(w) - span / 2, 0}, {0, -1, 0}, (double(i) + .5) / s, 0);
                    triangle(bottom, i + 1, i);
                    const auto top =
                        put({0, double(w) + span / 2, 0}, {0, 1, 0}, (double(i) + .5) / s, 1);
                    triangle((rows - 1) * (s + 1) + i, (rows - 1) * (s + 1) + i + 1, top);
                }
            }
        }
        if (v != c.smooth || ix != c.indices)
            return std::unexpected(
                Error{ErrorCode::SizeOverflow}); // Internal count invariant, never truncate.
        std::unique_ptr<Vertex[]> split;
        if (c.split)
        {
            split.reset(new (std::nothrow) Vertex[c.output]);
            if (!split)
                return std::unexpected(Error{ErrorCode::Allocation});
            for (std::size_t i = 0; i < c.indices; i += 3)
            {
                D3 face;
                if (!Unit(glm::cross(Read(vertices[indices[i + 1]].position) -
                                         Read(vertices[indices[i]].position),
                                     Read(vertices[indices[i + 2]].position) -
                                         Read(vertices[indices[i]].position)),
                          face))
                    return std::unexpected(Error{ErrorCode::DegenerateGeometry, i / 3});
                for (std::size_t j = 0; j < 3; ++j)
                {
                    split[i + j] = vertices[indices[i + j]];
                    if (k.shading == ShadingMode::Flat)
                        split[i + j].normal = Write(face);
                    if (k.kind == Kind::Diamond && k.uvLayout == DiamondUVLayout::PerFace)
                        split[i + j].uv = {j == 1 ? 1.f : 0.f, j == 2 ? 1.f : 0.f};
                    indices[i + j] = static_cast<std::uint32_t>(i + j);
                }
            }
            vertices = split.get();
        }

        // Pivot uses the actual sampled Float32 positions, including odd segment counts.
        D3 lo = Read(vertices[0].position), hi = lo;
        for (std::size_t i = 0; i < c.output; ++i)
        {
            const auto p = Read(vertices[i].position);
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        D3 offset = (lo + hi) / 2.0;
        if (k.pivot == PivotLocation::Base)
            offset.y = lo.y;
        else if (k.pivot == PivotLocation::MinimumCorner)
            offset = lo;
        for (std::size_t i = 0; i < c.output; ++i)
        {
            vertices[i].position = Write(Read(vertices[i].position) - offset);
            vertices[i].uv[0] *= k.uvTiling[0];
            vertices[i].uv[1] *= k.uvTiling[1];
        }
        // Keep Phase 06's exact default tangent payload. Other generated bases follow actual UV derivatives.
        const bool preserveBasis =
            k.kind <= Kind::Grid && !(k.kind == Kind::Cube && k.shading == ShadingMode::Smooth);
        if (k.tangents == TangentMode::Generate && !preserveBasis)
            for (std::size_t i = 0; i < c.output; ++i)
            {
                vertices[i].tangent = {};
                vertices[i].bitangent = {};
            }
        for (std::size_t i = 0; i < c.indices; i += 3)
        {
            auto& a = vertices[indices[i]];
            auto& b = vertices[indices[i + 1]];
            auto& d = vertices[indices[i + 2]];
            const auto ab = Read(b.position) - Read(a.position),
                       ac = Read(d.position) - Read(a.position);
            const auto area = glm::cross(ab, ac);
            if (!std::isfinite(glm::dot(area, area)) || glm::dot(area, area) <= 0)
                return std::unexpected(Error{ErrorCode::DegenerateGeometry, i / 3});
            if (k.tangents == TangentMode::Generate)
            {
                const double bu = double(b.uv[0]) - a.uv[0], bv = double(b.uv[1]) - a.uv[1],
                             du = double(d.uv[0]) - a.uv[0], dv = double(d.uv[1]) - a.uv[1];
                const double determinant = bu * dv - bv * du;
                if (!std::isfinite(determinant) || determinant == 0)
                    return std::unexpected(Error{ErrorCode::DegenerateUV, i / 3});
                if (!preserveBasis)
                {
                    const auto tangent = (ab * dv - ac * bv) / determinant;
                    const auto bitangent = (ac * bu - ab * du) / determinant;
                    for (auto j : {indices[i], indices[i + 1], indices[i + 2]})
                    {
                        vertices[j].tangent = Write(Read(vertices[j].tangent) + tangent);
                        vertices[j].bitangent = Write(Read(vertices[j].bitangent) + bitangent);
                    }
                }
            }
        }
        if (k.tangents == TangentMode::Generate && !preserveBasis)
            for (std::size_t i = 0; i < c.output; ++i)
            {
                const auto normal = Read(vertices[i].normal),
                           accumulated = Read(vertices[i].tangent);
                D3 tangent;
                if (!Unit(accumulated - normal * glm::dot(normal, accumulated), tangent))
                    return std::unexpected(Error{ErrorCode::DegenerateUV, i});
                const auto cross = glm::cross(normal, tangent);
                const double handedness = glm::dot(cross, Read(vertices[i].bitangent));
                if (!std::isfinite(handedness) || handedness == 0)
                    return std::unexpected(Error{ErrorCode::DegenerateUV, i});
                vertices[i].tangent = Write(tangent);
                vertices[i].bitangent = Write(cross * (handedness > 0 ? 1.0 : -1.0));
            }

        std::unique_ptr<std::byte[]> payload(new (std::nothrow) std::byte[c.output * c.stride]);
        if (!payload)
            return std::unexpected(Error{ErrorCode::Allocation});
        std::array<VertexAttribute, 5> attributes{};
        std::size_t attributeCount = 0, fieldOffset = 0;
        auto attribute = [&](VertexSemantic semantic, std::uint8_t components)
        {
            attributes[attributeCount++] = {
                semantic,   AttributeSlot(semantic),        VertexScalarFormat::Float32,
                components, VertexInterpretation::Floating, fieldOffset};
            fieldOffset += std::size_t(components) * sizeof(float);
        };
        attribute(VertexSemantic::Position, 3);
        if (k.generateUVs)
            attribute(VertexSemantic::TexCoord0, 2);
        attribute(VertexSemantic::Normal, 3);
        if (k.tangents == TangentMode::Generate)
        {
            attribute(VertexSemantic::Tangent, 3);
            attribute(VertexSemantic::Bitangent, 3);
        }
        for (std::size_t i = 0; i < c.output; ++i)
        {
            std::size_t offsetBytes = i * c.stride;
            auto copy = [&](const auto& field)
            {
                for (float x : field)
                    if (!std::isfinite(x))
                        return false;
                std::memcpy(payload.get() + offsetBytes, field.data(), sizeof(field));
                offsetBytes += sizeof(field);
                return true;
            };
            if (!copy(vertices[i].position) || (k.generateUVs && !copy(vertices[i].uv)) ||
                !copy(vertices[i].normal) ||
                (k.tangents == TangentMode::Generate &&
                 (!copy(vertices[i].tangent) || !copy(vertices[i].bitangent))))
                return std::unexpected(Error{ErrorCode::DegenerateGeometry, i});
        }
        MeshSourceData data;
        data.layout = {c.stride, {attributes.data(), attributeCount}};
        data.vertices = {payload.get(), c.output * c.stride};
        data.vertexCount = c.output;
        data.indexFormat = MeshIndexFormat::UInt32;
        data.indices = std::as_bytes(std::span(indices.get(), c.indices));
        data.indexCount = c.indices;
        const SubmeshRange submesh{0, c.indices, 0};
        data.submeshes = {&submesh, 1};
        auto mesh = MeshAsset::Create(data);
        if (!mesh)
            return std::unexpected(Error{ErrorCode::Mesh, mesh.error().element, mesh.error()});
        return std::move(*mesh);
    }
}
