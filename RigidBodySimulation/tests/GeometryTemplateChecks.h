#pragma once

#include "Mesh/GeometryTemplates.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace PreEditorValidation
{
    inline std::expected<std::size_t, const char*> CheckCpuGeometryTemplates()
    {
        using namespace ::GEngine;
        namespace GT = ::GEngine::GeometryTemplates;
        std::size_t checks = 0;
#define GEOMETRY_CPU_REQUIRE(condition, name)                                                      \
    do                                                                                             \
    {                                                                                              \
        ++checks;                                                                                  \
        if (!(condition))                                                                          \
            return std::unexpected(name);                                                          \
    } while (false)
        const GT::Request cases[]{GT::Cube{},
                                  GT::Plane{},
                                  GT::Quad{},
                                  GT::Grid{},
                                  GT::Cube{2, 3, 5},
                                  GT::Plane{2, 5},
                                  GT::Quad{2, 3},
                                  GT::Grid{2, 5, 3, 7},
                                  GT::Cube{.001f, .001f, .001f},
                                  GT::Cube{10000, 10000, 10000},
                                  GT::Plane{.001f, 10000},
                                  GT::Quad{10000, .001f},
                                  GT::Grid{3, 3, 4, 4},
                                  GT::Grid{.001f, 10000, 256, 256},
                                  GT::Grid{10000, .001f, 256, 256}};
        for (const auto& request : cases)
        {
            auto key = GT::KeyFor(request);
            auto mesh = GT::Generate(request);
            GEOMETRY_CPU_REQUIRE(key && mesh, "valid template generation");
            auto duplicate = GT::Generate(request);
            GEOMETRY_CPU_REQUIRE(duplicate &&
                                     mesh->Vertices().size() == duplicate->Vertices().size() &&
                                     std::equal(mesh->Vertices().begin(), mesh->Vertices().end(),
                                                duplicate->Vertices().begin()) &&
                                     std::equal(mesh->Indices().begin(), mesh->Indices().end(),
                                                duplicate->Indices().begin()),
                                 "deterministic payload");
            const bool cube = key->kind == GT::Kind::Cube, grid = key->kind == GT::Kind::Grid;
            const std::size_t vertices =
                cube   ? 24
                : grid ? (std::size_t(key->cellsX) + 1) * (std::size_t(key->cellsZ) + 1)
                       : 4;
            const std::size_t indices = cube   ? 36
                                        : grid ? 6 * std::size_t(key->cellsX) * key->cellsZ
                                               : 6;
            GEOMETRY_CPU_REQUIRE(mesh->VertexCount() == vertices && mesh->IndexCount() == indices &&
                                     mesh->IndexFormat() == MeshIndexFormat::UInt32,
                                 "exact topology counts");
            GEOMETRY_CPU_REQUIRE(mesh->Vertices().size() + mesh->Indices().size() <=
                                     GT::MaximumPayloadBytes,
                                 "bounded payload");
            GEOMETRY_CPU_REQUIRE(
                mesh->Submeshes().size() == 1 && mesh->Submeshes()[0].firstElement == 0 &&
                    mesh->Submeshes()[0].elementCount == indices &&
                    mesh->Submeshes()[0].materialSlot == 0 && mesh->MaterialSlotCount() == 1,
                "one material submesh");
            const auto layout = mesh->Layout();
            GEOMETRY_CPU_REQUIRE(layout.attributes.size() == 5 && layout.strideBytes == 56,
                                 "complete tangent vertex layout");
            const std::array<std::size_t, 5> components{3, 2, 3, 3, 3};
            for (std::size_t slot = 0; slot < 5; ++slot)
                GEOMETRY_CPU_REQUIRE(
                    layout.attributes[slot].slot == slot &&
                        AttributeSlot(layout.attributes[slot].semantic) == slot &&
                        layout.attributes[slot].scalar == VertexScalarFormat::Float32 &&
                        layout.attributes[slot].interpretation == VertexInterpretation::Floating &&
                        layout.attributes[slot].components == components[slot],
                    "CPU attribute semantics");
            auto read = [&](std::size_t vertex, std::size_t slot)
            {
                std::array<double, 3> result{};
                for (std::size_t c = 0; c < components[slot]; ++c)
                {
                    float value;
                    std::memcpy(&value,
                                mesh->Vertices().data() + vertex * layout.strideBytes +
                                    layout.attributes[slot].offsetBytes + c * sizeof(float),
                                sizeof(value));
                    result[c] = value;
                }
                return result;
            };
            auto index = [&](std::size_t i)
            {
                std::uint32_t value;
                std::memcpy(&value, mesh->Indices().data() + i * sizeof(value), sizeof(value));
                return value;
            };
            auto dot = [](auto a, auto b)
            {
                return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
            };
            auto cross = [](auto a, auto b) -> std::array<double, 3>
            {
                return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                        a[0] * b[1] - a[1] * b[0]};
            };
            const std::array<double, 3> dimensions{
                key->dimensions[0], cube || key->kind == GT::Kind::Quad ? key->dimensions[1] : 0,
                cube                          ? key->dimensions[2]
                : key->kind == GT::Kind::Quad ? 0
                                              : key->dimensions[1]};
            GEOMETRY_CPU_REQUIRE(!mesh->Bounds().empty, "nonempty bounds");
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const auto tolerance = 1e-6 * (std::max)(1.0, dimensions[axis]);
                GEOMETRY_CPU_REQUIRE(
                    std::abs(mesh->Bounds().minimum[axis] + dimensions[axis] / 2) <= tolerance &&
                        std::abs(mesh->Bounds().maximum[axis] - dimensions[axis] / 2) <= tolerance,
                    "centered exact AABB");
            }
            for (std::size_t v = 0; v < vertices; ++v)
            {
                for (std::size_t slot = 0; slot < 5; ++slot)
                    for (double value : read(v, slot))
                        GEOMETRY_CPU_REQUIRE(std::isfinite(value), "finite vertex fields");
                const auto p = read(v, 0), uv = read(v, 1), n = read(v, 2), t = read(v, 3),
                           b = read(v, 4);
                GEOMETRY_CPU_REQUIRE(uv[0] >= 0 && uv[0] <= 1 && uv[1] >= 0 && uv[1] <= 1,
                                     "UV range");
                GEOMETRY_CPU_REQUIRE(
                    std::abs(dot(n, n) - 1) <= 1e-5 && std::abs(dot(t, t) - 1) <= 1e-5 &&
                        std::abs(dot(b, b) - 1) <= 1e-5 && std::abs(dot(n, t)) <= 1e-5 &&
                        std::abs(dot(n, b)) <= 1e-5 && std::abs(dot(t, b)) <= 1e-5 &&
                        std::abs(dot(cross(t, b), n) - 1) <= 1e-5,
                    "orthonormal oriented tangent basis");
                GEOMETRY_CPU_REQUIRE(cube ? dot(p, n) > 0
                                          : n == (key->kind == GT::Kind::Quad
                                                      ? std::array<double, 3>{0, 0, 1}
                                                      : std::array<double, 3>{0, 1, 0}),
                                     "outward normal");
                double radiusSquared = 0;
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    const auto tolerance = 1e-6 * (std::max)(1.0, dimensions[axis]);
                    GEOMETRY_CPU_REQUIRE(p[axis] >= mesh->Bounds().minimum[axis] - tolerance &&
                                             p[axis] <= mesh->Bounds().maximum[axis] + tolerance,
                                         "bounds contain position");
                    radiusSquared += (p[axis] - mesh->Bounds().sphereCenter[axis]) *
                                     (p[axis] - mesh->Bounds().sphereCenter[axis]);
                }
                GEOMETRY_CPU_REQUIRE(std::sqrt(radiusSquared) <= mesh->Bounds().sphereRadius + 1e-6,
                                     "sphere contains position");
            }
            for (std::size_t i = 0; i < indices; i += 3)
            {
                GEOMETRY_CPU_REQUIRE(index(i) < vertices && index(i + 1) < vertices &&
                                         index(i + 2) < vertices,
                                     "index range");
                const auto a = read(index(i), 0), b = read(index(i + 1), 0),
                           c = read(index(i + 2), 0);
                std::array<double, 3> ab{}, ac{};
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    ab[axis] = b[axis] - a[axis];
                    ac[axis] = c[axis] - a[axis];
                }
                const auto area = cross(ab, ac);
                GEOMETRY_CPU_REQUIRE(dot(area, area) > 0 && dot(area, read(index(i), 2)) > 0,
                                     "nondegenerate outward CCW triangles");
                const auto ua = read(index(i), 1), ub = read(index(i + 1), 1),
                           uc = read(index(i + 2), 1);
                const double determinant =
                    (ub[0] - ua[0]) * (uc[1] - ua[1]) - (ub[1] - ua[1]) * (uc[0] - ua[0]);
                GEOMETRY_CPU_REQUIRE(determinant > 0, "UV handedness");
                std::array<double, 3> tangent{}, bitangent{};
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    tangent[axis] =
                        (ab[axis] * (uc[1] - ua[1]) - ac[axis] * (ub[1] - ua[1])) / determinant;
                    bitangent[axis] =
                        (ac[axis] * (ub[0] - ua[0]) - ab[axis] * (uc[0] - ua[0])) / determinant;
                }
                GEOMETRY_CPU_REQUIRE(dot(tangent, read(index(i), 3)) > 0 &&
                                         dot(bitangent, read(index(i), 4)) > 0,
                                     "tangent directions agree with UV derivatives");
            }
            if (cube && key->dimensions == std::array<float, 3>{1, 1, 1})
            {
                // Frozen Box triangle positions and UV order, independent of generator storage order.
                const std::array<std::array<float, 3>, 8> points{{{-.5f, -.5f, -.5f},
                                                                  {.5f, -.5f, -.5f},
                                                                  {-.5f, .5f, -.5f},
                                                                  {.5f, .5f, -.5f},
                                                                  {-.5f, -.5f, .5f},
                                                                  {.5f, -.5f, .5f},
                                                                  {-.5f, .5f, .5f},
                                                                  {.5f, .5f, .5f}}};
                const unsigned expected[]{5, 1, 3, 5, 3, 7, 0, 4, 6, 0, 6, 2, 6, 7, 3, 6, 3, 2,
                                          0, 1, 5, 0, 5, 4, 4, 5, 7, 4, 7, 6, 1, 0, 2, 1, 2, 3};
                const std::array<std::array<double, 3>, 6> uvs{
                    {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 0, 0}, {1, 1, 0}, {0, 1, 0}}};
                for (std::size_t i = 0; i < 36; ++i)
                {
                    const auto actual = read(index(i), 0);
                    for (std::size_t axis = 0; axis < 3; ++axis)
                        GEOMETRY_CPU_REQUIRE(actual[axis] == points[expected[i]][axis],
                                             "legacy Box position compatibility");
                    GEOMETRY_CPU_REQUIRE(read(index(i), 1) == uvs[i % 6],
                                         "legacy Box UV compatibility");
                }
            }
        }
        for (const auto& request : {GT::Request{GT::Cube{1, 1, 1}}, GT::Request{GT::Plane{1, 1}},
                                    GT::Request{GT::Quad{1, 1}}, GT::Request{GT::Grid{1, 1, 1, 1}}})
        {
            GEOMETRY_CPU_REQUIRE(*GT::KeyFor(request) == *GT::KeyFor(cases[request.index()]),
                                 "default canonical key");
        }
        GEOMETRY_CPU_REQUIRE(GT::KeyFor(GT::Plane{})->kind != GT::KeyFor(GT::Grid{})->kind,
                             "distinct kinds");
        GEOMETRY_CPU_REQUIRE(*GT::KeyFor(GT::Cube{}) !=
                                 *GT::KeyFor(GT::Cube{std::nextafter(1.f, 2.f), 1, 1}),
                             "no epsilon key merging");
        for (float bad :
             {0.f, -0.f, -1.f, .0009f, 10001.f, (std::numeric_limits<float>::max)(),
              std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
              std::numeric_limits<float>::quiet_NaN()})
            for (const GT::Request& request :
                 {GT::Request{GT::Cube{bad, 1, 1}}, GT::Request{GT::Cube{1, bad, 1}},
                  GT::Request{GT::Cube{1, 1, bad}}, GT::Request{GT::Plane{bad, 1}},
                  GT::Request{GT::Plane{1, bad}}, GT::Request{GT::Quad{bad, 1}},
                  GT::Request{GT::Quad{1, bad}}, GT::Request{GT::Grid{bad, 1}},
                  GT::Request{GT::Grid{1, bad}}})
            {
                auto key = GT::KeyFor(request);
                auto mesh = GT::Generate(request);
                GEOMETRY_CPU_REQUIRE(!key && !mesh &&
                                         key.error().code == GT::ErrorCode::InvalidDimension &&
                                         mesh.error().code == GT::ErrorCode::InvalidDimension,
                                     "typed invalid dimension");
            }
        for (std::uint32_t bad : {0u, 257u, (std::numeric_limits<std::uint32_t>::max)()})
            for (const GT::Request& request :
                 {GT::Request{GT::Grid{1, 1, bad, 1}}, GT::Request{GT::Grid{1, 1, 1, bad}}})
            {
                auto mesh = GT::Generate(request);
                GEOMETRY_CPU_REQUIRE(!mesh && mesh.error().code == GT::ErrorCode::InvalidCells,
                                     "cell limits reject before arithmetic/allocation");
            }
#undef GEOMETRY_CPU_REQUIRE
        return checks;
    }
}

#ifndef PRE_EDITOR_CPU_GEOMETRY_ONLY
#include "Renderer/SceneRenderResources.h"
#include "Renderer/RenderExtraction.h"
#include "Scene/_Entity.h"
#include "Core/Log.h"

namespace PreEditorValidation
{
    inline std::expected<void, ::GEngine::PlatformError>
    CheckGeometryPublication(::GEngine::EngineContext& root, ::GEngine::SceneRenderResources& owner,
                             ::GEngine::Asset::MeshHandle box,
                             ::GEngine::Asset::MaterialInstanceHandle material)
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Asset;
        namespace GT = ::GEngine::GeometryTemplates;
        std::size_t checks = 0;
        auto require = [&](bool value, const char* name)
        {
            ++checks;
            if (!value)
                Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_06_FAIL {}", name);
            return value;
        };
#define GEOMETRY_REQUIRE(condition, name)                                                          \
    if (!require(bool(condition), name))                                                           \
    return std::unexpected(                                                                        \
        PlatformError{PlatformErrorCode::Initialization, "Phase 06 geometry", name})
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_06_BEGIN");
        const auto count = owner.Meshes().Size();
        auto reused = owner.PublishGeometry(GT::Cube{1, 1, 1});
        GEOMETRY_REQUIRE(reused && *reused == box && owner.Meshes().Size() == count,
                         "scene Cube cache hit");
        auto scene = CreateRefPtr<_Scene>();
        auto entity = scene->CreateEntity("phase06-retained-template");
        GEOMETRY_REQUIRE(entity && owner.AssignRenderable(*entity, {box, material}),
                         "frame fixture");
        std::optional<RenderFrame> retained;
        const GpuMesh* retainedMesh = nullptr;
        {
            auto access = owner.Publication().BeginFrame();
            RenderExtractionStats stats;
            auto frame = ExtractRenderFrame(*scene, owner.ForFrame(access), stats);
            GEOMETRY_REQUIRE(frame && frame->Draws().size() == 1, "template frame extraction");
            retainedMesh = frame->Resources()[0].Mesh().Get();
            retained.emplace(std::move(*frame));
            auto blocked = owner.PublishGeometry(GT::Cube{});
            GEOMETRY_REQUIRE(!blocked && std::get<SceneResourceCode>(blocked.error().cause) ==
                                             SceneResourceCode::PublicationBusy,
                             "cache hit excludes active frame");
            GEOMETRY_REQUIRE(!owner.PublishGeometry(GT::Quad{7, 9}),
                             "cache miss excludes active frame");
        }
        reused = owner.PublishGeometry(GT::Cube{});
        GEOMETRY_REQUIRE(reused && *reused == box && owner.Meshes().Size() == count,
                         "cache hit does not publish");
        {
            auto access = owner.Publication().BeginFrame();
            RenderExtractionStats stats;
            auto frame = ExtractRenderFrame(*scene, owner.ForFrame(access), stats);
            GEOMETRY_REQUIRE(frame && frame->Draws().size() == 1 &&
                                 frame->Resources()[0].Mesh().Get() == retainedMesh &&
                                 retained->Resources()[0].Mesh().Get() == retainedMesh &&
                                 frame->Resources()[0].Mesh().Identity() == box,
                             "retained exact mesh/frame identity");
        }
        retained.reset();
        scene.reset();
        {
            auto created = SceneRenderResources::Create(root);
            GEOMETRY_REQUIRE(created, "isolated cache owner");
            auto& cache = **created;
            auto cube = cache.PublishGeometry(GT::Cube{});
            auto plane = cache.PublishGeometry(GT::Plane{});
            auto quad = cache.PublishGeometry(GT::Quad{});
            auto grid = cache.PublishGeometry(GT::Grid{});
            GEOMETRY_REQUIRE(cube && plane && quad && grid && *cube != box && *plane != *grid &&
                                 *cube != *quad && *quad != *plane && cache.Meshes().Size() == 4,
                             "distinct keys and owner domain");
            for (std::size_t i = 4; i < GT::MaximumSharedTemplates; ++i)
                GEOMETRY_REQUIRE(cache.PublishGeometry(GT::Quad{static_cast<float>(i + 2), 1}),
                                 "bounded cache fill");
            auto full = cache.PublishGeometry(GT::Quad{999, 1});
            GEOMETRY_REQUIRE(!full &&
                                 std::get<SceneResourceCode>(full.error().cause) ==
                                     SceneResourceCode::TemplateCapacity &&
                                 cache.Meshes().Size() == GT::MaximumSharedTemplates,
                             "129th key rejected without publication");
            auto invalid = cache.PublishGeometry(GT::Cube{0, 1, 1});
            GEOMETRY_REQUIRE(!invalid && std::get<GT::Error>(invalid.error().cause).code ==
                                             GT::ErrorCode::InvalidDimension,
                             "typed parameter failure before capacity");
            auto existing = cache.PublishGeometry(GT::Cube{1, 1, 1});
            GEOMETRY_REQUIRE(existing && *existing == *cube &&
                                 cache.Meshes().Size() == GT::MaximumSharedTemplates,
                             "full/invalid failures preserve canonical handle");
            MeshView old;
            {
                auto access = cache.Publication().BeginFrame();
                auto view = cache.Meshes().Acquire(access, *cube);
                GEOMETRY_REQUIRE(view, "retain version before destroy");
                old = *view;
            }
            {
                auto publication = cache.Publication().BeginPublication();
                GEOMETRY_REQUIRE(cache.Meshes().Destroy(publication, *cube), "diagnostic destroy");
                GEOMETRY_REQUIRE(cache.Meshes().Collect(publication) == 0,
                                 "retained version not collected");
            }
            auto stale = cache.PublishGeometry(GT::Cube{});
            GEOMETRY_REQUIRE(!stale && std::holds_alternative<RegistryError>(stale.error().cause) &&
                                 old.Identity() == *cube && old->VertexCount() == 24,
                             "cache respects stale registry generation");
            old = {};
            {
                auto publication = cache.Publication().BeginPublication();
                GEOMETRY_REQUIRE(cache.Meshes().Collect(publication) == 1,
                                 "retirement after final lease");
            }
        }
        {
            AssetPublication publication;
            MeshRegistry constrained(publication, AssetRegistryLimits{1});
            auto mesh = GT::Generate(GT::Cube{});
            GEOMETRY_REQUIRE(mesh, "upload error fixture");
            MeshHandle first;
            {
                auto access = publication.BeginPublication();
                auto a = PublishMesh(constrained, access, *mesh);
                GEOMETRY_REQUIRE(a, "constrained first upload");
                first = *a;
                auto failed = PublishMesh(constrained, access, *mesh);
                GEOMETRY_REQUIRE(!failed && failed.error().code == GpuMeshErrorCode::Registry &&
                                     failed.error().registry == RegistryError::SlotsExhausted &&
                                     constrained.Size() == 1,
                                 "typed upload/publication failure rollback");
                GEOMETRY_REQUIRE(DescribeSceneResourceError({"geometry upload", failed.error()})
                                         .find("registry=") != std::string::npos,
                                 "upload diagnostic retains typed cause");
            }
            {
                auto access = publication.BeginFrame();
                auto prior = constrained.Acquire(access, first);
                GEOMETRY_REQUIRE(prior && prior->Revision() == 1 && (*prior)->VertexCount() == 24,
                                 "failed upload preserves prior version");
            }
            {
                auto access = publication.BeginPublication();
                GEOMETRY_REQUIRE(constrained.Close(access), "constrained owner drains");
            }
        }
        GEOMETRY_REQUIRE(owner.Meshes().Size() == count && owner.Publication().CanPublish(),
                         "test fixtures retired before normal rendering");
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_06_PASS checks={} templates=4 cache_limit=128",
                                   checks);
#undef GEOMETRY_REQUIRE
        return {};
    }
}
#endif
