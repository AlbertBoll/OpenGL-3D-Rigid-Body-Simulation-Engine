#pragma once

#include "GeometryTemplateChecks.h"
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <map>
#include <vector>

namespace PreEditorValidation
{
    namespace ParametricChecks
    {
        using namespace ::GEngine;
        namespace GT = ::GEngine::GeometryTemplates;
        using D3 = glm::dvec3;
        inline const VertexAttribute* Attribute(const MeshAsset& mesh, VertexSemantic semantic)
        {
            for (const auto& a : mesh.Layout().attributes)
                if (a.semantic == semantic)
                    return &a;
            return nullptr;
        }
        inline D3 Read(const MeshAsset& mesh, std::size_t vertex, VertexSemantic semantic)
        {
            D3 result(0.0);
            const auto* a = Attribute(mesh, semantic);
            if (!a)
                return result;
            for (std::size_t c = 0; c < a->components; ++c)
            {
                float value;
                std::memcpy(&value,
                            mesh.Vertices().data() + vertex * mesh.Layout().strideBytes +
                                a->offsetBytes + c * sizeof(float),
                            sizeof(value));
                result[static_cast<int>(c)] = value;
            }
            return result;
        }
        inline std::uint32_t Index(const MeshAsset& mesh, std::size_t element)
        {
            std::uint32_t result;
            std::memcpy(&result, mesh.Indices().data() + element * sizeof(result), sizeof(result));
            return result;
        }
        inline bool Near(D3 a, D3 b, double tolerance = 1e-5)
        {
            return glm::length(a - b) <= tolerance;
        }
        inline bool Same(const MeshAsset& a, const MeshAsset& b)
        {
            return a.VertexCount() == b.VertexCount() && a.IndexCount() == b.IndexCount() &&
                   a.Layout().strideBytes == b.Layout().strideBytes &&
                   a.Vertices().size() == b.Vertices().size() &&
                   a.Indices().size() == b.Indices().size() &&
                   std::equal(a.Vertices().begin(), a.Vertices().end(), b.Vertices().begin()) &&
                   std::equal(a.Indices().begin(), a.Indices().end(), b.Indices().begin());
        }
        inline const GT::Request Defaults[]{GT::Cube{},   GT::Plane{},    GT::Quad{}, GT::Grid{},
                                            GT::Sphere{}, GT::Cylinder{}, GT::Cone{}, GT::Capsule{},
                                            GT::Torus{},  GT::Diamond{}};
        inline GT::Request Options(GT::Request request,
                                   const GT::GeometryGenerationOptions& options)
        {
            std::visit(
                [&](auto& v)
                {
                    v.Options = options;
                },
                request);
            return request;
        }
        inline std::expected<std::size_t, const char*>
        Inspect(const GT::Request& request, const MeshAsset& mesh, bool closed = true)
        {
            std::size_t checks = 0;
#define P07_CHECK(condition, name)                                                                 \
    do                                                                                             \
    {                                                                                              \
        ++checks;                                                                                  \
        if (!(condition))                                                                          \
            return std::unexpected(name);                                                          \
    } while (false)
            auto key = GT::KeyFor(request);
            P07_CHECK(key, "validated inspection key");
            const auto& k = *key;
            const auto layout = mesh.Layout();
            const auto stride = 24u + (k.generateUVs ? 8u : 0u) +
                                (k.tangents == GT::TangentMode::Generate ? 24u : 0u);
            P07_CHECK(layout.strideBytes == stride &&
                          layout.attributes.size() ==
                              (2u + (k.generateUVs ? 1u : 0u) +
                               (k.tangents == GT::TangentMode::Generate ? 2u : 0u)),
                      "optional attributes absent with exact packed stride");
            P07_CHECK(mesh.IndexFormat() == MeshIndexFormat::UInt32 &&
                          mesh.Submeshes().size() == 1 &&
                          mesh.Submeshes()[0].elementCount == mesh.IndexCount() &&
                          mesh.MaterialSlotCount() == 1,
                      "one indexed material submesh");
            P07_CHECK(mesh.Vertices().size() + mesh.Indices().size() <= GT::MaximumPayloadBytes,
                      "8 MiB payload bound");
            const std::size_t s = k.subdivisions[0], n = k.subdivisions[1], h = k.subdivisions[2];
            std::size_t triangles = 0;
            switch (k.kind)
            {
            case GT::Kind::Sphere:
                triangles = 2 * s * (n - 1);
                break;
            case GT::Kind::Cylinder:
                triangles = 2 * s * (n + 1);
                break;
            case GT::Kind::Cone:
                triangles = 2 * s * n;
                break;
            case GT::Kind::Capsule:
                triangles = 2 * s * (2 * n + h - 1);
                break;
            case GT::Kind::Torus:
                triangles = 2 * s * n;
                break;
            case GT::Kind::Diamond:
                triangles = 108;
                break;
            default:
                break;
            }
            if (triangles)
            {
                P07_CHECK(mesh.IndexCount() == 3 * triangles, "parametric exact triangle count");
                if (k.shading == GT::ShadingMode::Flat ||
                    (k.kind == GT::Kind::Diamond && k.uvLayout == GT::DiamondUVLayout::PerFace))
                    P07_CHECK(mesh.VertexCount() == mesh.IndexCount(),
                              "six-family flat/chart splitting");
            }
            std::map<std::array<float, 3>, std::uint32_t> welded;
            std::vector<std::uint32_t> weld(mesh.VertexCount());
            struct Edge
            {
                int direction = 0;
                std::size_t uses = 0;
            };
            std::map<std::pair<std::uint32_t, std::uint32_t>, Edge> edges;
            const auto& bounds = mesh.Bounds();
            const double tolerance = 1e-6 * (std::max)({1.0, bounds.maximum[0] - bounds.minimum[0],
                                                        bounds.maximum[1] - bounds.minimum[1],
                                                        bounds.maximum[2] - bounds.minimum[2]});
            for (std::size_t i = 0; i < mesh.VertexCount(); ++i)
            {
                const auto p = Read(mesh, i, VertexSemantic::Position),
                           normal = Read(mesh, i, VertexSemantic::Normal);
                P07_CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
                              std::isfinite(normal.x) && std::isfinite(normal.y) &&
                              std::isfinite(normal.z) &&
                              std::abs(glm::dot(normal, normal) - 1) < 1e-5,
                          "finite unit normals");
                for (int axis = 0; axis < 3; ++axis)
                    P07_CHECK(p[axis] >= bounds.minimum[axis] - tolerance &&
                                  p[axis] <= bounds.maximum[axis] + tolerance,
                              "actual sampled AABB contains all vertices");
                P07_CHECK(glm::length(p - D3(bounds.sphereCenter[0], bounds.sphereCenter[1],
                                             bounds.sphereCenter[2])) <=
                              bounds.sphereRadius + tolerance,
                          "bounding sphere contains emitted vertices");
                if (k.generateUVs)
                {
                    auto uv = Read(mesh, i, VertexSemantic::TexCoord0);
                    P07_CHECK(std::isfinite(uv.x) && std::isfinite(uv.y) && uv.x >= -1e-6 &&
                                  uv.y >= -1e-6 && uv.x <= k.uvTiling[0] + 1e-6 &&
                                  uv.y <= k.uvTiling[1] + 1e-6,
                              "finite tiled UV range");
                }
                if (k.tangents == GT::TangentMode::Generate)
                {
                    auto t = Read(mesh, i, VertexSemantic::Tangent),
                         b = Read(mesh, i, VertexSemantic::Bitangent);
                    P07_CHECK(std::abs(glm::dot(t, t) - 1) < 1e-5 &&
                                  std::abs(glm::dot(b, b) - 1) < 1e-5 &&
                                  std::abs(glm::dot(t, normal)) < 1e-5 &&
                                  std::abs(glm::dot(b, normal)) < 1e-5 &&
                                  std::abs(glm::dot(t, b)) < 1e-5,
                              "orthonormal TBN");
                }
                const std::array<float, 3> point{static_cast<float>(p.x), static_cast<float>(p.y),
                                                 static_cast<float>(p.z)};
                auto [where, inserted] =
                    welded.try_emplace(point, static_cast<std::uint32_t>(welded.size()));
                weld[i] = where->second;
            }
            for (std::size_t i = 0; i < mesh.IndexCount(); i += 3)
            {
                const auto a = Index(mesh, i), b = Index(mesh, i + 1), c = Index(mesh, i + 2);
                P07_CHECK(a < mesh.VertexCount() && b < mesh.VertexCount() &&
                              c < mesh.VertexCount(),
                          "UInt32 indices in range");
                const auto p = Read(mesh, a, VertexSemantic::Position);
                const auto ab = Read(mesh, b, VertexSemantic::Position) - p,
                           ac = Read(mesh, c, VertexSemantic::Position) - p;
                const auto area = glm::cross(ab, ac);
                P07_CHECK(std::isfinite(glm::dot(area, area)) && glm::dot(area, area) > 0,
                          "no collapsed emitted triangles");
                auto na = Read(mesh, a, VertexSemantic::Normal),
                     nb = Read(mesh, b, VertexSemantic::Normal),
                     nc = Read(mesh, c, VertexSemantic::Normal);
                P07_CHECK(glm::dot(area, na + nb + nc) > 0,
                          "outward CCW winding against surface normals");
                if (k.shading == GT::ShadingMode::Flat)
                {
                    auto face = glm::normalize(area);
                    P07_CHECK(Near(na, face) && Near(nb, face) && Near(nc, face),
                              "actual face normals in flat mode");
                }
                if (k.tangents == GT::TangentMode::Generate)
                {
                    const auto uv = Read(mesh, a, VertexSemantic::TexCoord0);
                    const auto du = Read(mesh, b, VertexSemantic::TexCoord0) - uv,
                               dv = Read(mesh, c, VertexSemantic::TexCoord0) - uv;
                    const double determinant = du.x * dv.y - du.y * dv.x;
                    P07_CHECK(std::isfinite(determinant) && determinant != 0,
                              "nondegenerate tangent UV charts");
                    const auto t = (ab * dv.y - ac * du.y) / determinant,
                               bitangent = (ac * du.x - ab * dv.x) / determinant;
                    for (auto vertex : {a, b, c})
                    {
                        const auto normal = Read(mesh, vertex, VertexSemantic::Normal);
                        const auto actualT = Read(mesh, vertex, VertexSemantic::Tangent);
                        const auto actualB = Read(mesh, vertex, VertexSemantic::Bitangent);
                        P07_CHECK(glm::dot(t, actualT) > 0 && glm::dot(bitangent, actualB) > 0 &&
                                      glm::dot(glm::cross(normal, actualT), actualB) * determinant >
                                          0,
                                  "TBN directions and handedness follow actual UV derivatives");
                    }
                }
                if (closed)
                    for (auto pair : {std::pair{weld[a], weld[b]}, std::pair{weld[b], weld[c]},
                                      std::pair{weld[c], weld[a]}})
                    {
                        auto& edge = edges[std::minmax(pair.first, pair.second)];
                        ++edge.uses;
                        edge.direction += pair.first < pair.second ? 1 : -1;
                    }
            }
            if (closed)
                for (const auto& [pair, edge] : edges)
                    P07_CHECK(pair.first != pair.second && edge.uses == 2 && edge.direction == 0,
                              "welded seams/poles/caps form a closed oriented surface");
            for (int axis = 0; axis < 3; ++axis)
            {
                if (k.pivot == GT::PivotLocation::MinimumCorner ||
                    (k.pivot == GT::PivotLocation::Base && axis == 1))
                    P07_CHECK(std::abs(bounds.minimum[axis]) <= tolerance,
                              "sampled minimum/base pivot");
                else
                    P07_CHECK(std::abs(bounds.minimum[axis] + bounds.maximum[axis]) <=
                                  2 * tolerance,
                              "sampled center pivot");
            }
#undef P07_CHECK
            return checks;
        }

        // CPU comparison fixture: compares triangle corner connectivity/appearance, independent of storage order.
        inline std::expected<std::size_t, const char*> CompareDiamond(const MeshAsset& candidate,
                                                                      const MeshAsset& legacy)
        {
            if (candidate.VertexCount() != 56 || legacy.VertexCount() != 56 ||
                candidate.IndexCount() != 324 || legacy.IndexCount() != 324)
                return std::unexpected("Diamond frozen 56-point/108-triangle reference");
            D3 offset;
            for (int a = 0; a < 3; ++a)
                offset[a] = (legacy.Bounds().minimum[a] + legacy.Bounds().maximum[a]) / 2;
            std::vector<bool> found(legacy.IndexCount() / 3);
            for (std::size_t i = 0; i < candidate.IndexCount(); i += 3)
            {
                bool match = false;
                for (std::size_t j = 0; j < legacy.IndexCount() && !match; j += 3)
                    if (!found[j / 3])
                        for (std::size_t rotation = 0; rotation < 3 && !match; ++rotation)
                        {
                            bool same = true;
                            for (std::size_t corner = 0; corner < 3; ++corner)
                            {
                                auto a = Index(candidate, i + corner),
                                     b = Index(legacy, j + (corner + rotation) % 3);
                                same &= Near(Read(candidate, a, VertexSemantic::Position),
                                             Read(legacy, b, VertexSemantic::Position) - offset,
                                             1e-6) &&
                                        Near(Read(candidate, a, VertexSemantic::Normal),
                                             Read(legacy, b, VertexSemantic::Normal)) &&
                                        Near(Read(candidate, a, VertexSemantic::TexCoord0),
                                             Read(legacy, b, VertexSemantic::TexCoord0), 1e-6);
                            }
                            if (same)
                            {
                                found[j / 3] = true;
                                match = true;
                            }
                        }
                if (!match)
                    return std::unexpected("Diamond reference triangle/profile/normal/UV mismatch");
            }
            return candidate.IndexCount();
        }
    }

    inline std::expected<std::size_t, const char*> CheckCpuParametricGeometry()
    {
        using namespace ParametricChecks;
        std::size_t checks = 0;
#define P07_REQUIRE(condition, name)                                                               \
    do                                                                                             \
    {                                                                                              \
        ++checks;                                                                                  \
        if (!(condition))                                                                          \
            return std::unexpected(name);                                                          \
    } while (false)
        // New Phase 07 compatibility evidence; historical Phase 06 records are never rewritten.
        auto compatible = CheckCpuGeometryTemplates();
        if (!compatible)
            return std::unexpected(compatible.error());
        checks += *compatible;
        for (const auto& request : Defaults)
        {
            auto key = GT::KeyFor(request), normalized = key;
            auto mesh = GT::Generate(request);
            P07_REQUIRE(key && mesh, "all-ten default generation");
            for (int variant = 0; variant < 5; ++variant)
            {
                auto authored = request;
                std::visit(
                    [&](auto& value)
                    {
                        value.Options = {};
                        if (variant == 1)
                        {
                            value.Options.Shading = key->shading;
                            value.Options.Tangents = key->tangents;
                        }
                        if (variant == 2)
                            value.Options.Shading = key->shading;
                        if (variant == 3)
                            value.Options.Tangents = key->tangents;
                        if (variant == 4)
                        {
                            value.Options.Shading = GT::ShadingMode::PrimitiveDefault;
                            value.Options.Tangents = GT::TangentMode::PrimitiveDefault;
                        }
                    },
                    authored);
                auto other = GT::Generate(authored);
                auto otherKey = GT::KeyFor(authored);
                P07_REQUIRE(otherKey && *otherKey == *normalized && other && Same(*mesh, *other),
                            "omitted/empty/partial/concrete defaults share keys and bytes");
            }
            auto uvDefault = request;
            std::visit(
                [](auto& value)
                {
                    value.Options.GenerateUVs = false;
                },
                uvDefault);
            auto uvDefaultMesh = GT::Generate(uvDefault);
            P07_REQUIRE(key->kind <= GT::Kind::Grid
                            ? (!uvDefaultMesh &&
                               uvDefaultMesh.error().code == GT::ErrorCode::TangentsRequireUV)
                            : bool(uvDefaultMesh),
                        "UV-off primitive-default Generate rejects while default Omit succeeds");
            std::vector<GT::Request> changed;
            std::visit(
                [&](const auto& value)
                {
                    auto add = [&](auto edit)
                    {
                        auto copy = value;
                        edit(copy);
                        changed.emplace_back(copy);
                    };
                    if constexpr (requires { value.width; })
                        add(
                            [](auto& x)
                            {
                                x.width += .25f;
                            });
                    if constexpr (requires { value.depth; })
                        add(
                            [](auto& x)
                            {
                                x.depth += .25f;
                            });
                    if constexpr (requires { value.height; })
                        add(
                            [](auto& x)
                            {
                                x.height += .25f;
                            });
                    if constexpr (requires { value.radius; })
                        add(
                            [](auto& x)
                            {
                                x.radius += .25f;
                            });
                    if constexpr (requires { value.majorRadius; })
                    {
                        add(
                            [](auto& x)
                            {
                                x.majorRadius += .25f;
                            });
                        add(
                            [](auto& x)
                            {
                                x.minorRadius += .05f;
                            });
                    }
                    if constexpr (requires { value.cellsX; })
                    {
                        add(
                            [](auto& x)
                            {
                                ++x.cellsX;
                            });
                        add(
                            [](auto& x)
                            {
                                ++x.cellsZ;
                            });
                    }
                    if constexpr (requires { value.segments; })
                        add(
                            [](auto& x)
                            {
                                ++x.segments;
                            });
                    if constexpr (requires { value.rings; })
                        add(
                            [](auto& x)
                            {
                                ++x.rings;
                            });
                    if constexpr (requires { value.heightSegments; })
                        add(
                            [](auto& x)
                            {
                                ++x.heightSegments;
                            });
                    if constexpr (requires { value.hemisphereRings; })
                        add(
                            [](auto& x)
                            {
                                ++x.hemisphereRings;
                            });
                    if constexpr (requires { value.majorSegments; })
                    {
                        add(
                            [](auto& x)
                            {
                                ++x.majorSegments;
                            });
                        add(
                            [](auto& x)
                            {
                                ++x.minorSegments;
                            });
                    }
                    if constexpr (requires { value.UVLayout; })
                        add(
                            [](auto& x)
                            {
                                x.UVLayout = GT::DiamondUVLayout::PerFace;
                            });
                    add(
                        [&](auto& x)
                        {
                            x.Options.Shading = key->shading == GT::ShadingMode::Flat
                                                    ? GT::ShadingMode::Smooth
                                                    : GT::ShadingMode::Flat;
                        });
                    add(
                        [&](auto& x)
                        {
                            x.Options.Tangents = key->tangents == GT::TangentMode::Generate
                                                     ? GT::TangentMode::Omit
                                                     : GT::TangentMode::Generate;
                            if constexpr (requires { x.UVLayout; })
                                x.UVLayout = GT::DiamondUVLayout::PerFace;
                        });
                    add(
                        [](auto& x)
                        {
                            x.Options.Pivot = GT::PivotLocation::Base;
                        });
                    add(
                        [](auto& x)
                        {
                            x.Options.Pivot = GT::PivotLocation::MinimumCorner;
                        });
                    add(
                        [](auto& x)
                        {
                            x.Options.UVTiling.x = 2;
                        });
                    add(
                        [](auto& x)
                        {
                            x.Options.UVTiling.y = 3;
                        });
                    add(
                        [](auto& x)
                        {
                            x.Options.GenerateUVs = false;
                            x.Options.Tangents = GT::TangentMode::Omit;
                        });
                },
                request);
            for (const auto& authored : changed)
            {
                auto different = GT::KeyFor(authored);
                auto generated = GT::Generate(authored);
                P07_REQUIRE(different && *different != *key && generated,
                            "every mesh-affecting field changes canonical key");
            }
            for (auto code : {GT::ErrorCode::InvalidShadingMode, GT::ErrorCode::InvalidTangentMode,
                              GT::ErrorCode::InvalidPivot, GT::ErrorCode::InvalidUVTiling,
                              GT::ErrorCode::TangentsRequireUV, GT::ErrorCode::InactiveOption})
            {
                auto invalid = request;
                std::visit(
                    [&](auto& value)
                    {
                        switch (code)
                        {
                        case GT::ErrorCode::InvalidShadingMode:
                            value.Options.Shading = static_cast<GT::ShadingMode>(255);
                            break;
                        case GT::ErrorCode::InvalidTangentMode:
                            value.Options.Tangents = static_cast<GT::TangentMode>(255);
                            break;
                        case GT::ErrorCode::InvalidPivot:
                            value.Options.Pivot = static_cast<GT::PivotLocation>(255);
                            break;
                        case GT::ErrorCode::InvalidUVTiling:
                            value.Options.UVTiling.x = std::numeric_limits<float>::quiet_NaN();
                            break;
                        case GT::ErrorCode::TangentsRequireUV:
                            value.Options.GenerateUVs = false;
                            value.Options.Tangents = GT::TangentMode::Generate;
                            break;
                        default:
                            value.Options.GenerateUVs = false;
                            value.Options.Tangents = GT::TangentMode::Omit;
                            value.Options.UVTiling.y = 2;
                            break;
                        }
                    },
                    invalid);
                auto error = GT::Generate(invalid);
                P07_REQUIRE(!error && error.error().code == code,
                            "typed option errors precede allocation");
            }
        }
        // Defaults, count minima/maxima and asymmetric/extreme valid cases, followed by each flat form.
        const GT::Request variants[]{GT::Sphere{},
                                     GT::Cylinder{},
                                     GT::Cone{},
                                     GT::Capsule{},
                                     GT::Torus{},
                                     GT::Diamond{},
                                     GT::Sphere{.001f, 3, 2},
                                     GT::Cylinder{.001f, 10000, 3, 1},
                                     GT::Cone{5000, .001f, 3, 1},
                                     GT::Capsule{.001f, 10000, 3, 1, 1},
                                     GT::Torus{.0021f, .001f, 3, 3},
                                     GT::Diamond{.001f, 10000},
                                     GT::Sphere{5000, 128, 64},
                                     GT::Cylinder{5000, 10000, 128, 64},
                                     GT::Cone{5000, 10000, 128, 64},
                                     GT::Capsule{1, 10000, 128, 32, 64},
                                     GT::Torus{4000, 1000, 128, 64},
                                     GT::Diamond{5000, 10000},
                                     GT::Sphere{2, 7, 5},
                                     GT::Cylinder{2, 7, 7, 3},
                                     GT::Cone{2, 7, 7, 3},
                                     GT::Capsule{2, 7, 7, 3, 2},
                                     GT::Torus{2, .5f, 7, 5},
                                     GT::Diamond{2, 7}};
        for (auto request : variants)
            for (int flat = 0; flat < 2; ++flat)
            {
                std::visit(
                    [&](auto& x)
                    {
                        x.Options.Shading = flat ? GT::ShadingMode::Flat : GT::ShadingMode::Smooth;
                    },
                    request);
                auto generated = GT::Generate(request), repeated = GT::Generate(request);
                P07_REQUIRE(generated && repeated && Same(*generated, *repeated),
                            "six-family deterministic valid generation");
                auto inspected = Inspect(request, *generated);
                if (!inspected)
                    return std::unexpected(inspected.error());
                checks += *inspected;
            }
        for (std::size_t family = 4; family < std::size(Defaults); ++family)
        {
            const auto request = Defaults[family];
            auto center = GT::Generate(request);
            P07_REQUIRE(center, "option reference");
            for (auto pivot : {GT::PivotLocation::Center, GT::PivotLocation::Base,
                               GT::PivotLocation::MinimumCorner})
            {
                auto options = GT::GeometryGenerationOptions{};
                options.Pivot = pivot;
                auto authored = Options(request, options);
                auto mesh = GT::Generate(authored);
                P07_REQUIRE(mesh, "all-six pivot generation");
                auto inspected = Inspect(authored, *mesh);
                if (!inspected)
                    return std::unexpected(inspected.error());
                checks += *inspected;
                const auto delta = Read(*mesh, 0, VertexSemantic::Position) -
                                   Read(*center, 0, VertexSemantic::Position);
                for (std::size_t i = 0; i < mesh->VertexCount(); ++i)
                    P07_REQUIRE(Near(Read(*mesh, i, VertexSemantic::Position) -
                                         Read(*center, i, VertexSemantic::Position),
                                     delta, 1e-6) &&
                                    Read(*mesh, i, VertexSemantic::Normal) ==
                                        Read(*center, i, VertexSemantic::Normal) &&
                                    Read(*mesh, i, VertexSemantic::TexCoord0) ==
                                        Read(*center, i, VertexSemantic::TexCoord0),
                                "pivot changes only emitted positions by one translation");
            }
            auto options = GT::GeometryGenerationOptions{};
            options.UVTiling = {2, 3};
            auto tiled = GT::Generate(Options(request, options));
            P07_REQUIRE(tiled, "all-six tiling");
            for (std::size_t i = 0; i < tiled->VertexCount(); ++i)
            {
                auto uv = Read(*center, i, VertexSemantic::TexCoord0),
                     actual = Read(*tiled, i, VertexSemantic::TexCoord0);
                P07_REQUIRE(Near(actual, D3(float(uv.x) * 2.f, float(uv.y) * 3.f, 0), 1e-6) &&
                                Read(*center, i, VertexSemantic::Position) ==
                                    Read(*tiled, i, VertexSemantic::Position),
                            "UV tiling changes UV payload only");
            }
            for (float limit : {GT::MinimumUVTiling, GT::MaximumUVTiling})
            {
                auto limited = GT::GeometryGenerationOptions{};
                limited.UVTiling = {limit, limit};
                auto authored = Options(request, limited);
                auto mesh = GT::Generate(authored);
                P07_REQUIRE(mesh, "all-six inclusive tiling limits");
                auto inspected = Inspect(authored, *mesh);
                if (!inspected)
                    return std::unexpected(inspected.error());
                checks += *inspected;
            }
            options = {};
            options.GenerateUVs = false;
            options.Tangents = GT::TangentMode::Omit;
            auto without = GT::Generate(Options(request, options));
            P07_REQUIRE(without && !Attribute(*without, VertexSemantic::TexCoord0) &&
                            !Attribute(*without, VertexSemantic::Tangent) &&
                            without->Layout().strideBytes == 24,
                        "all-six UV-off layouts contain no fabricated attributes");
            for (int flat = 0; flat < 2; ++flat)
            {
                auto with = request;
                std::visit(
                    [&](auto& x)
                    {
                        x.Options.Tangents = GT::TangentMode::Generate;
                        x.Options.Shading = flat ? GT::ShadingMode::Flat : GT::ShadingMode::Smooth;
                        if constexpr (requires { x.UVLayout; })
                            x.UVLayout = GT::DiamondUVLayout::PerFace;
                    },
                    with);
                auto mesh = GT::Generate(with);
                P07_REQUIRE(mesh, "all-six explicit tangent generation with valid charts");
                auto inspected = Inspect(with, *mesh);
                if (!inspected)
                    return std::unexpected(inspected.error());
                checks += *inspected;
            }
            // Analytic default smooth normals (even default rings have no sample-center offset).
            for (std::size_t i = 0; i < center->VertexCount() && family != 9; ++i)
            {
                auto p = Read(*center, i, VertexSemantic::Position),
                     normal = Read(*center, i, VertexSemantic::Normal);
                D3 expected;
                if (family == 4)
                    expected = glm::normalize(p);
                else if (family == 5 && std::abs(normal.y) > .99)
                    expected = D3(0, normal.y > 0 ? 1 : -1, 0);
                else if (family == 5)
                    expected = glm::normalize(D3(p.x, 0, p.z));
                else if (family == 6 && normal.y < -.99)
                    expected = D3(0, -1, 0);
                else if (family == 6 && glm::length(D3(p.x, 0, p.z)) > 1e-8)
                    expected = glm::normalize(D3(2 * p.x / glm::length(D3(p.x, 0, p.z)), 1,
                                                 2 * p.z / glm::length(D3(p.x, 0, p.z))));
                else if (family == 6)
                    continue; // Apex wedges have a deterministic per-chart limiting normal.
                else if (family == 7)
                    expected = glm::normalize(p - D3(0, std::clamp(p.y, -.5, .5), 0));
                else
                    expected = glm::normalize(p - glm::normalize(D3(p.x, 0, p.z)));
                P07_REQUIRE(Near(normal, expected), "analytic smooth normals and hard cap creases");
            }
        }
        const std::pair<GT::Request, GT::ErrorCode> negatives[]{
            {GT::Sphere{0}, GT::ErrorCode::InvalidRadius},
            {GT::Sphere{std::numeric_limits<float>::infinity()}, GT::ErrorCode::InvalidRadius},
            {GT::Cylinder{1, 0}, GT::ErrorCode::InvalidDimension},
            {GT::Cone{1, std::numeric_limits<float>::quiet_NaN()}, GT::ErrorCode::InvalidDimension},
            {GT::Sphere{1, 0, 16}, GT::ErrorCode::InvalidSegments},
            {GT::Sphere{1, 129, 16}, GT::ErrorCode::InvalidSegments},
            {GT::Sphere{1, 32, 1}, GT::ErrorCode::InvalidRings},
            {GT::Sphere{1, 32, 65}, GT::ErrorCode::InvalidRings},
            {GT::Cylinder{1, 2, 32, 0}, GT::ErrorCode::InvalidHeightSegments},
            {GT::Cone{1, 2, 32, 65}, GT::ErrorCode::InvalidHeightSegments},
            {GT::Capsule{1, 2}, GT::ErrorCode::InvalidCapsuleSpan},
            {GT::Capsule{1, 3, 32, 33, 1}, GT::ErrorCode::InvalidRings},
            {GT::Capsule{1, 3, 32, 8, 0}, GT::ErrorCode::InvalidHeightSegments},
            {GT::Torus{1, 1}, GT::ErrorCode::InvalidTorusRelation},
            {GT::Torus{4000, 2000}, GT::ErrorCode::InvalidTorusRelation},
            {GT::Torus{1, .4f, 32, 2}, GT::ErrorCode::InvalidRings},
            {GT::Sphere{1, (std::numeric_limits<std::uint32_t>::max)(), 16},
             GT::ErrorCode::InvalidSegments},
            {GT::Capsule{1, 3, 32, 8, (std::numeric_limits<std::uint32_t>::max)()},
             GT::ErrorCode::InvalidHeightSegments},
            {GT::Diamond{1, 1.4f, static_cast<GT::DiamondUVLayout>(255)},
             GT::ErrorCode::InvalidUVLayout},
            {GT::Cylinder{0}, GT::ErrorCode::InvalidRadius},
            {GT::Cone{0}, GT::ErrorCode::InvalidRadius},
            {GT::Capsule{0}, GT::ErrorCode::InvalidRadius},
            {GT::Diamond{0}, GT::ErrorCode::InvalidRadius},
            {GT::Diamond{1, 0}, GT::ErrorCode::InvalidDimension},
            {GT::Capsule{1, 0}, GT::ErrorCode::InvalidDimension},
            {GT::Torus{1, 0}, GT::ErrorCode::InvalidRadius},
            {GT::Torus{1, .4f, (std::numeric_limits<std::uint32_t>::max)(), 16},
             GT::ErrorCode::InvalidSegments},
            {GT::Torus{1, .4f, 32, (std::numeric_limits<std::uint32_t>::max)()},
             GT::ErrorCode::InvalidRings},
            {GT::Sphere{1, 32, (std::numeric_limits<std::uint32_t>::max)()},
             GT::ErrorCode::InvalidRings},
            {GT::Capsule{1, 3, 32, (std::numeric_limits<std::uint32_t>::max)(), 1},
             GT::ErrorCode::InvalidRings},
            {GT::Cylinder{1, 2, 32, (std::numeric_limits<std::uint32_t>::max)()},
             GT::ErrorCode::InvalidHeightSegments},
            {GT::Cone{1, 2, 32, (std::numeric_limits<std::uint32_t>::max)()},
             GT::ErrorCode::InvalidHeightSegments},
            {GT::Torus{4999, .001f, 128, 62}, GT::ErrorCode::DegenerateGeometry}};
        for (const auto& [request, code] : negatives)
        {
            auto invalid = GT::Generate(request);
            P07_REQUIRE(!invalid && invalid.error().code == code,
                        "typed invalid dimensions/counts/relations/precision");
        }
        auto diamond = GT::Diamond{};
        diamond.Options.Tangents = GT::TangentMode::Generate;
        auto unsupported = GT::Generate(diamond);
        P07_REQUIRE(!unsupported &&
                        unsupported.error().code == GT::ErrorCode::UnsupportedTangentUVLayout,
                    "Diamond legacy UV tangents rejected without remapping");
        diamond = {};
        diamond.UVLayout = GT::DiamondUVLayout::PerFace;
        P07_REQUIRE(GT::KeyFor(diamond)->tangents == GT::TangentMode::Omit,
                    "PerFace does not change primitive-default tangent resolution");
        // Largest flat payload with tangents: proves admitted bounded allocation, no hostile/OOM probe.
        auto largest = GT::Capsule{1, 10000, 128, 32, 64};
        largest.Options.Shading = GT::ShadingMode::Flat;
        largest.Options.Tangents = GT::TangentMode::Generate;
        auto bounded = GT::Generate(largest);
        P07_REQUIRE(bounded && bounded->VertexCount() == 97536 &&
                        bounded->Vertices().size() + bounded->Indices().size() == 5852160,
                    "maximum flat Capsule full layout fits published bound");
        auto inspected = Inspect(largest, *bounded);
        if (!inspected)
            return std::unexpected(inspected.error());
        checks += *inspected;
#undef P07_REQUIRE
        return checks;
    }
}

#ifndef PRE_EDITOR_CPU_GEOMETRY_ONLY
#include "Managers/ShapeManager.h"
#include "Geometry/Geometry.h"

namespace PreEditorValidation
{
    inline std::expected<void, ::GEngine::PlatformError> CheckParametricPublication(
        ::GEngine::EngineContext& root, ::GEngine::SceneRenderResources& owner,
        ::GEngine::Asset::MeshHandle cube, ::GEngine::Asset::MaterialInstanceHandle material,
        ::GEngine::Asset::MaterialInstanceHandle uvFreeMaterial)
    {
        using namespace ParametricChecks;
        using namespace ::GEngine::Asset;
        std::size_t checks = 0;
#define P07_RUNTIME(condition, name)                                                               \
    do                                                                                             \
    {                                                                                              \
        ++checks;                                                                                  \
        if (!(condition))                                                                          \
        {                                                                                          \
            Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_07_FAIL {}", name);                      \
            return std::unexpected(                                                                \
                PlatformError{PlatformErrorCode::Initialization, "Phase 07 publication", name});   \
        }                                                                                          \
    } while (false)
        auto old = Manager::ShapeManager::GetShape("Diamond");
        P07_RUNTIME(old && *old, "existing legacy Diamond diagnostic lookup");
        auto reference = (*old)->ExportCpuMesh();
        auto diamond = GT::Generate(GT::Diamond{});
        P07_RUNTIME(reference && diamond, "CPU legacy export/reference candidate");
        auto comparison = CompareDiamond(*diamond, *reference);
        P07_RUNTIME(comparison, "legacy Diamond triangle/profile/normal/UV compatibility");
        checks += *comparison;
        const auto mainCount = owner.Meshes().Size();
        auto maintained = owner.PublishGeometry(GT::Cube{});
        P07_RUNTIME(maintained && *maintained == cube, "maintained normal Cube canonical path");
        auto created = SceneRenderResources::Create(root);
        P07_RUNTIME(created, "isolated existing resource owner");
        auto& cache = **created;
        // Fixture meshes and materials must belong to the same resource owner.
        const MaterialInstanceHandle sources[]{material, uvFreeMaterial};
        const SceneMaterialKind kinds[]{SceneMaterialKind::Lit, SceneMaterialKind::PointLight};
        std::array<MaterialInstanceHandle, 2> fixtureMaterials{};
        for (std::size_t i = 0; i < fixtureMaterials.size(); ++i)
        {
            std::vector<MaterialParameterDecl> parameters;
            std::vector<MaterialTextureAssignment> textures;
            {
                auto access = owner.Publication().BeginFrame();
                auto source = owner.Materials().Acquire(access, sources[i]);
                P07_RUNTIME(source, "original fixture material resolves in its owner");
                const auto& declaration = (*source)->Declaration();
                for (std::size_t p = 0; p < declaration->Parameters().size(); ++p)
                {
                    auto parameter = declaration->Parameters()[p].declaration;
                    parameter.defaultValue = (*source)->Values()[p];
                    parameters.push_back(std::move(parameter));
                }
                for (std::size_t t = 0; t < declaration->Textures().size(); ++t)
                    if ((*source)->Textures()[t])
                        textures.push_back({declaration->Textures()[t].declaration.name,
                                            *(*source)->Textures()[t]});
            }
            auto published = cache.PublishMaterial({kinds[i], parameters, textures});
            if (!published)
                Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_07_DIAG fixture_material_error {}",
                                           DescribeSceneResourceError(published.error()));
            P07_RUNTIME(published, "fixture material publication in isolated owner");
            fixtureMaterials[i] = *published;
        }
        material = fixtureMaterials[0];
        uvFreeMaterial = fixtureMaterials[1];
        std::array<MeshHandle, 10> handles{};
        for (std::size_t family = 0; family < std::size(Defaults); ++family)
        {
            auto first = cache.PublishGeometry(Defaults[family]);
            P07_RUNTIME(first, "all-ten canonical publication");
            handles[family] = *first;
            const auto before = cache.Meshes().Size();
            auto key = GT::KeyFor(Defaults[family]);
            for (int variant = 0; variant < 4; ++variant)
            {
                auto request = Defaults[family];
                std::visit(
                    [&](auto& value)
                    {
                        value.Options = {};
                        if (variant == 1)
                        {
                            value.Options.Shading = key->shading;
                            value.Options.Tangents = key->tangents;
                        }
                        if (variant == 2)
                            value.Options.Shading = key->shading;
                        if (variant == 3)
                            value.Options.Tangents = key->tangents;
                    },
                    request);
                auto shared = cache.PublishGeometry(request);
                P07_RUNTIME(shared && *shared == *first && cache.Meshes().Size() == before,
                            "default-equivalent keys reuse live canonical handle without upload");
            }
            auto metadata = cache.MeshMetadata(*first);
            P07_RUNTIME(metadata && metadata->submeshCount == 1,
                        "CPU authoring metadata per new mesh");
        }
        auto scene = CreateRefPtr<_Scene>();
        std::vector<_Entity> entities;
        for (std::size_t family = 4; family < 10; ++family)
        {
            constexpr const char* names[]{"Cube", "Plane", "Quad", "Grid", "Sphere",
                                          "Cylinder", "Cone", "Capsule", "Torus", "Diamond"};
            auto entity = scene->CreateEntity("phase07-render-only");
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_07_DIAG family={} name={} entity_result={}",
                family, names[family], entity.has_value());
            if (!entity)
            {
                const auto& error = entity.error();
                Log::GetCoreLogger()->error(
                    "PRE_EDITOR_PHASE_07_DIAG entity_error type=SceneError code={} "
                    "operation={} message={} entity={} transform_present={}",
                    static_cast<int>(error.code), error.operation, error.message,
                    error.entity, error.transform.has_value());
                if (error.transform)
                    Log::GetCoreLogger()->error(
                        "PRE_EDITOR_PHASE_07_DIAG transform_error type=TransformError "
                        "code={} entity={} parent={}",
                        static_cast<int>(error.transform->code),
                        static_cast<std::uint64_t>(error.transform->entity),
                        static_cast<std::uint64_t>(error.transform->parent));
            }
            P07_RUNTIME(entity, "parametric fixture entity creation");
            auto assigned = cache.AssignRenderable(*entity, {handles[family], material});
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_07_DIAG family={} name={} assignment_result={}",
                family, names[family], assigned.has_value());
            if (!assigned)
            {
                const auto& error = assigned.error();
                Log::GetCoreLogger()->error(
                    "PRE_EDITOR_PHASE_07_DIAG assignment_error type=SceneResourceError "
                    "cause_index={} detail={}",
                    error.cause.index(), DescribeSceneResourceError(error));
                if (const auto* code = std::get_if<SceneAssignmentError>(&error.cause))
                {
                    constexpr const char* codes[]{
                        "InvalidEntity", "ForeignEntity", "MissingIdentity", "ExtractionActive",
                        "InvalidMesh", "InvalidMaterial", "InvalidSubmesh",
                        "IdentityExhausted", "RevisionExhausted"};
                    Log::GetCoreLogger()->error(
                        "PRE_EDITOR_PHASE_07_DIAG assignment_cause "
                        "type=SceneAssignmentError code={} name={}",
                        static_cast<int>(*code), codes[static_cast<std::size_t>(*code)]);
                }
                auto access = cache.Publication().BeginFrame();
                auto localMaterial = cache.Materials().Acquire(access, material);
                auto originalMaterial = owner.Materials().Acquire(access, material);
                Log::GetCoreLogger()->error(
                    "PRE_EDITOR_PHASE_07_DIAG material index={} generation={} registry={} "
                    "isolated_result={} owner_result={} isolated_error_type=RegistryError "
                    "isolated_error_code={}",
                    material.index, material.generation, material.registry,
                    localMaterial.has_value(), originalMaterial.has_value(),
                    localMaterial ? -1 : static_cast<int>(localMaterial.error()));
            }
            P07_RUNTIME(assigned, "same simple API for all six families");
            entity->AddOrReplaceComponent<Transform3DComponent>();
            entities.push_back(*entity);
            auto request = Defaults[family];
            std::visit(
                [](auto& value)
                {
                    value.Options.GenerateUVs = false;
                    value.Options.Tangents = GT::TangentMode::Omit;
                },
                request);
            auto without = cache.PublishGeometry(request);
            P07_RUNTIME(without && *without != handles[family],
                        "UV-free generated mesh publication");
            auto helper = scene->CreateEntity("phase07-UV-free");
            P07_RUNTIME(helper && cache.AssignRenderable(*helper, {*without, uvFreeMaterial}),
                        "UV-free mesh uses existing color-only PointLight material");
        }
        std::optional<RenderFrame> retained;
        const GpuMesh* firstMesh = nullptr;
        {
            auto access = cache.Publication().BeginFrame();
            RenderExtractionStats stats;
            auto frame = ExtractRenderFrame(*scene, cache.ForFrame(access), stats);
            P07_RUNTIME(frame && frame->Draws().size() == 12,
                        "all-six renderable extraction plus UV-free compatible fixtures");
            firstMesh = frame->Resources()[0].Mesh().Get();
            retained.emplace(std::move(*frame));
            auto blocked = cache.PublishGeometry(GT::Sphere{});
            P07_RUNTIME(!blocked && std::get<SceneResourceCode>(blocked.error().cause) ==
                                        SceneResourceCode::PublicationBusy,
                        "new template cache hit respects frame safe point");
        }
        {
            auto access = cache.Publication().BeginFrame();
            RenderExtractionStats stats;
            auto frame = ExtractRenderFrame(*scene, cache.ForFrame(access), stats);
            P07_RUNTIME(frame && frame->Resources()[0].Mesh().Get() == firstMesh &&
                            retained->Resources()[0].Mesh().Get() == firstMesh,
                        "retained exact-version frame remains intact");
        }
        retained.reset();
        scene.reset();
        auto options = GT::GeometryGenerationOptions{};
        options.Shading = GT::ShadingMode::Flat;
        for (std::size_t family = 4; family < 10; ++family)
        {
            auto flat = cache.PublishGeometry(Options(Defaults[family], options));
            P07_RUNTIME(flat && *flat != handles[family],
                        "smooth/flat canonical handles remain distinct");
        }
        const auto fillBegin = cache.Meshes().Size();
        for (std::size_t i = fillBegin; i < GT::MaximumSharedTemplates; ++i)
            P07_RUNTIME(cache.PublishGeometry(GT::Quad{float(i + 10), 1}),
                        "one bounded 128-entry cache across all ten kinds");
        auto full = cache.PublishGeometry(GT::Sphere{1.75f});
        P07_RUNTIME(!full &&
                        std::get<SceneResourceCode>(full.error().cause) ==
                            SceneResourceCode::TemplateCapacity &&
                        cache.Meshes().Size() == GT::MaximumSharedTemplates,
                    "capacity failure preserves prior mesh count");
        auto invalid = cache.PublishGeometry(GT::Sphere{0});
        P07_RUNTIME(!invalid && std::get<GT::Error>(invalid.error().cause).code ==
                                    GT::ErrorCode::InvalidRadius,
                    "invalid request cannot obtain cached success even when full");
        auto prior = cache.PublishGeometry(GT::Sphere{});
        P07_RUNTIME(prior && *prior == handles[4],
                    "failed new/invalid requests preserve canonical handle");
        MeshView oldVersion;
        {
            auto access = cache.Publication().BeginFrame();
            auto view = cache.Meshes().Acquire(access, handles[4]);
            P07_RUNTIME(view, "retain curved version before diagnostic destroy");
            oldVersion = *view;
        }
        {
            auto access = cache.Publication().BeginPublication();
            P07_RUNTIME(cache.Meshes().Destroy(access, handles[4]) &&
                            cache.Meshes().Collect(access) == 0,
                        "retained version prevents retirement");
        }
        auto stale = cache.PublishGeometry(GT::Sphere{});
        P07_RUNTIME(!stale && std::holds_alternative<RegistryError>(stale.error().cause) &&
                        oldVersion.Identity() == handles[4],
                    "canonical cache obeys stale generation without reminting");
        oldVersion = {};
        {
            auto access = cache.Publication().BeginPublication();
            P07_RUNTIME(cache.Meshes().Collect(access) == 1,
                        "curved version retires after last lease");
        }
        entities.clear();
        created->reset();
        P07_RUNTIME(owner.Meshes().Size() == mainCount && owner.Publication().CanPublish(),
                    "validation owners retire before normal rendering");
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_07_PASS checks={} families=6 cache_limit=128 diamond_reference=PASS",
            checks);
#undef P07_RUNTIME
        return {};
    }
}
#endif
