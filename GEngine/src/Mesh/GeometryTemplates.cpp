#include "gepch.h"
#include "Mesh/GeometryTemplates.h"
#include <cmath>
#include <limits>
#include <new>

namespace GEngine::GeometryTemplates
{
    namespace
    {
        struct Vertex
        {
            std::array<float, 3> position;
            std::array<float, 2> uv;
            std::array<float, 3> normal, tangent, bitangent;
        };
        static_assert(sizeof(Vertex) == 14 * sizeof(float));
        constexpr VertexAttribute Attributes[]{
            {VertexSemantic::Position, 0, VertexScalarFormat::Float32, 3,
             VertexInterpretation::Floating, offsetof(Vertex, position)},
            {VertexSemantic::TexCoord0, 1, VertexScalarFormat::Float32, 2,
             VertexInterpretation::Floating, offsetof(Vertex, uv)},
            {VertexSemantic::Normal, 2, VertexScalarFormat::Float32, 3,
             VertexInterpretation::Floating, offsetof(Vertex, normal)},
            {VertexSemantic::Tangent, 3, VertexScalarFormat::Float32, 3,
             VertexInterpretation::Floating, offsetof(Vertex, tangent)},
            {VertexSemantic::Bitangent, 4, VertexScalarFormat::Float32, 3,
             VertexInterpretation::Floating, offsetof(Vertex, bitangent)}};
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
    }

    std::expected<Key, Error> KeyFor(const Request& request) noexcept
    {
        return std::visit(
            [](const auto& value) -> std::expected<Key, Error>
            {
                using T = std::decay_t<decltype(value)>;
                Key key{};
                std::size_t count = 2;
                if constexpr (std::same_as<T, Cube>)
                {
                    key = {Kind::Cube, {value.width, value.height, value.depth}};
                    count = 3;
                }
                else if constexpr (std::same_as<T, Quad>)
                    key = {Kind::Quad, {value.width, value.height, 0}};
                else if constexpr (std::same_as<T, Plane>)
                    key = {Kind::Plane, {value.width, value.depth, 0}};
                else
                {
                    key = {Kind::Grid, {value.width, value.depth, 0}, value.cellsX, value.cellsZ};
                    if (!value.cellsX || value.cellsX > MaximumCells)
                        return std::unexpected(Error{ErrorCode::InvalidCells, 0});
                    if (!value.cellsZ || value.cellsZ > MaximumCells)
                        return std::unexpected(Error{ErrorCode::InvalidCells, 1});
                }
                for (std::size_t i = 0; i < count; ++i)
                    if (!std::isfinite(key.dimensions[i]) || key.dimensions[i] < MinimumDimension ||
                        key.dimensions[i] > MaximumDimension)
                        return std::unexpected(Error{ErrorCode::InvalidDimension, i});
                return key;
            },
            request);
    }

    std::expected<MeshAsset, Error> Generate(const Request& request) noexcept
    {
        auto key = KeyFor(request);
        if (!key)
            return std::unexpected(key.error());
        std::size_t vertexCount = key->kind == Kind::Cube ? 24 : 4;
        std::size_t indexCount = key->kind == Kind::Cube ? 36 : 6;
        if (key->kind == Kind::Grid)
        {
            std::size_t columns, rows, cells;
            if (!Add(key->cellsX, 1, columns) || !Add(key->cellsZ, 1, rows) ||
                !Multiply(columns, rows, vertexCount) ||
                !Multiply(key->cellsX, key->cellsZ, cells) || !Multiply(cells, 6, indexCount))
                return std::unexpected(Error{ErrorCode::SizeOverflow});
        }
        std::size_t vertexBytes, indexBytes, payloadBytes;
        if (!Multiply(vertexCount, sizeof(Vertex), vertexBytes) ||
            !Multiply(indexCount, sizeof(std::uint32_t), indexBytes) ||
            !Add(vertexBytes, indexBytes, payloadBytes) ||
            vertexCount > (std::numeric_limits<std::uint32_t>::max)())
            return std::unexpected(Error{ErrorCode::SizeOverflow});
        if (payloadBytes > MaximumPayloadBytes)
            return std::unexpected(Error{ErrorCode::PayloadLimit, payloadBytes});
        std::unique_ptr<Vertex[]> vertices(new (std::nothrow) Vertex[vertexCount]);
        std::unique_ptr<std::uint32_t[]> indices(new (std::nothrow) std::uint32_t[indexCount]);
        if (!vertices || !indices)
            return std::unexpected(Error{ErrorCode::Allocation, payloadBytes});

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
                    const float v = static_cast<float>(y) / cellsY;
                    auto& vertex = vertices[firstVertex + y * stride + x];
                    vertex.uv = {u, v};
                    vertex.normal = normal;
                    vertex.tangent = tangent;
                    vertex.bitangent = bitangent;
                    for (std::size_t axis = 0; axis < 3; ++axis)
                        vertex.position[axis] =
                            origin[axis] + tangent[axis] * width * u + bitangent[axis] * height * v;
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
        const float w = key->dimensions[0], h = key->dimensions[1];
        if (key->kind == Kind::Cube)
        {
            const float d = key->dimensions[2], x = w / 2, y = h / 2, z = d / 2;
            // Same outward face order and face UV orientation as frozen Box.
            surface(0, 0, {x, -y, z}, {0, 0, -1}, {0, 1, 0}, d, h);
            surface(4, 6, {-x, -y, -z}, {0, 0, 1}, {0, 1, 0}, d, h);
            surface(8, 12, {-x, y, z}, {1, 0, 0}, {0, 0, -1}, w, d);
            surface(12, 18, {-x, -y, -z}, {1, 0, 0}, {0, 0, 1}, w, d);
            surface(16, 24, {-x, -y, z}, {1, 0, 0}, {0, 1, 0}, w, h);
            surface(20, 30, {x, -y, -z}, {-1, 0, 0}, {0, 1, 0}, w, h);
        }
        else if (key->kind == Kind::Quad)
            surface(0, 0, {-w / 2, -h / 2, 0}, {1, 0, 0}, {0, 1, 0}, w, h);
        else
            surface(0, 0, {-w / 2, 0, h / 2}, {1, 0, 0}, {0, 0, -1}, w, h,
                    key->kind == Kind::Grid ? key->cellsX : 1,
                    key->kind == Kind::Grid ? key->cellsZ : 1);
        auto source =
            MeshSourceData::FromVertices<Vertex>({vertices.get(), vertexCount}, Attributes);
        source.indexFormat = MeshIndexFormat::UInt32;
        source.indices = std::as_bytes(std::span(indices.get(), indexCount));
        source.indexCount = indexCount;
        const SubmeshRange submesh{0, indexCount, 0};
        source.submeshes = {&submesh, 1};
        auto mesh = MeshAsset::Create(source);
        if (!mesh)
            return std::unexpected(Error{ErrorCode::Mesh, mesh.error().element, mesh.error()});
        return std::move(*mesh);
    }
}
