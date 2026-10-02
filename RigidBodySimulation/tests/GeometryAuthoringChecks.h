#pragma once

#include "Scene/_Entity.h"
#include "Renderer/SceneRenderResources.h"
#include "Renderer/RenderExtraction.h"
#include "Core/GEngine.h"
#include "Core/Log.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/ShapeSphere.h"
#include "Physics/ShapeBox.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <type_traits>

namespace PreEditorValidation
{
    inline bool AuthoredBytesEqual(const ::GEngine::MeshAsset& a, const ::GEngine::MeshAsset& b)
    {
        if (a.VertexCount() != b.VertexCount() || a.IndexCount() != b.IndexCount() ||
            a.IndexFormat() != b.IndexFormat() || a.UpdateIntent() != b.UpdateIntent() ||
            a.MaterialSlotCount() != b.MaterialSlotCount() || a.Layout().strideBytes != b.Layout().strideBytes ||
            a.Layout().attributes.size() != b.Layout().attributes.size() || a.Submeshes().size() != b.Submeshes().size() ||
            !std::ranges::equal(a.Vertices(), b.Vertices()) || !std::ranges::equal(a.Indices(), b.Indices())) return false;
        for (std::size_t i = 0; i < a.Layout().attributes.size(); ++i)
        {
            const auto& x = a.Layout().attributes[i]; const auto& y = b.Layout().attributes[i];
            if (x.semantic != y.semantic || x.slot != y.slot || x.scalar != y.scalar || x.components != y.components ||
                x.interpretation != y.interpretation || x.offsetBytes != y.offsetBytes) return false;
        }
        for (std::size_t i = 0; i < a.Submeshes().size(); ++i)
        {
            const auto& x = a.Submeshes()[i]; const auto& y = b.Submeshes()[i];
            if (x.firstElement != y.firstElement || x.elementCount != y.elementCount || x.materialSlot != y.materialSlot) return false;
        }
        return a.Bounds().empty == b.Bounds().empty && a.Bounds().minimum == b.Bounds().minimum &&
            a.Bounds().maximum == b.Bounds().maximum && a.Bounds().sphereCenter == b.Bounds().sphereCenter &&
            a.Bounds().sphereRadius == b.Bounds().sphereRadius;
    }

    inline bool AuthoredClose(double actual, double reference)
    {
        return std::isfinite(actual) && std::abs(actual - reference) <= 1e-6 * (std::max)(1., std::abs(reference));
    }

    inline glm::dvec3 AuthoredVector(const ::GEngine::MeshAsset& mesh, std::size_t vertex,
                                    const ::GEngine::VertexAttribute& attribute)
    {
        std::array<float, 3> values;
        std::memcpy(values.data(), mesh.Vertices().data() + vertex * mesh.Layout().strideBytes + attribute.offsetBytes, sizeof(values));
        return {values[0], values[1], values[2]};
    }

    inline bool GeometryFailure(const ::GEngine::SceneResourceError& error, ::GEngine::GeometryAuthoringCode code)
    {
        const auto* actual = std::get_if<::GEngine::GeometryAuthoringCode>(&error.cause);
        return actual && *actual == code;
    }

    inline std::expected<void, ::GEngine::PlatformError>
    CheckGeometryAuthoring(::GEngine::EngineContext& root, ::GEngine::_Scene& active,
                           ::GEngine::SceneRenderResources& owner, ::GEngine::Asset::MeshHandle cube,
                           ::GEngine::Asset::MaterialInstanceHandle material)
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Component;
        using namespace ::GEngine::Math;
        static_assert(std::same_as<decltype(std::declval<_Entity&>().GetComponent<MeshRendererComponent>()), const MeshRendererComponent&>);
        std::size_t checks = 0;
        auto require = [&](bool condition, const char* name)
        {
            ++checks;
            if (!condition) Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_08_FAIL {}", name);
            return condition;
        };
#define GEOM08_REQUIRE(condition, name) \
        if (!require(bool(condition), name)) return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "Phase 08 geometry authoring", name})
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_08_BEGIN");
        const auto originalBodies = active.GetAllEntitiesWith<RigidBody3DComponent>().size();
        const auto originalOwners = owner.GeometryOwners(cube);
        const auto originalSource = owner.GeometrySource(cube);
        GEOM08_REQUIRE(originalOwners && originalSource && originalOwners->canonicalCachePinned, "canonical entry source");
        const auto baseOwners = originalOwners->entityOwners;

        // Pure typed snapshot cases cover empty/invalid/degenerate bounds without
        // creating a second publication path or corrupting an immutable MeshAsset.
        {
            auto identity = originalSource->geometry;
            auto measured = MeasureObjectDimensions(identity, {-2, 0, 3});
            GEOM08_REQUIRE(measured && measured->dimensions[0] == 4 && measured->dimensions[1] == 0 && measured->dimensions[2] == 12,
                           "object dimensions exclude rotation and retain scale sign separately");
            auto restored = ResolveDimensionScale(*measured, {6, 9, 8});
            GEOM08_REQUIRE(restored && restored->x == -3 && restored->y == 3 && AuthoredClose(restored->z, 2), "signed and zero scale restoration");
            for (const double bad : {-1., 0., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
                                     std::numeric_limits<double>::denorm_min(), (std::numeric_limits<double>::max)()})
            {
                auto invalid = ResolveDimensionScale(*measured, {2, 3, bad});
                GEOM08_REQUIRE(!invalid && invalid.error() == GeometryAuthoringCode::InvalidDimensions, "invalid dimension not clamped");
            }
            identity.bounds.empty = true;
            auto empty = MeasureObjectDimensions(identity, {1, 1, 1});
            GEOM08_REQUIRE(empty && empty->status == BoundsStatus::Empty, "empty query has explicit status");
            auto emptySet = ResolveDimensionScale(*empty, {1, 1, 1});
            GEOM08_REQUIRE(!emptySet && emptySet.error() == GeometryAuthoringCode::EmptyBounds, "empty set rejected");
            for (int dimensions = 0; dimensions < 3; ++dimensions)
            {
                identity.bounds.empty = false;
                identity.bounds.minimum = {0, 0, 0}; identity.bounds.maximum = {0, 0, 0};
                for (int axis = 0; axis < dimensions; ++axis) identity.bounds.maximum[axis] = 2;
                auto degenerate = MeasureObjectDimensions(identity, {-3, 4, -5});
                GEOM08_REQUIRE(degenerate, "point line plane dimensions");
                std::array<double, 3> request{};
                for (int axis = 0; axis < dimensions; ++axis) request[axis] = 4;
                auto preserved = ResolveDimensionScale(*degenerate, request);
                GEOM08_REQUIRE(preserved && (*preserved)[dimensions] == degenerate->localScale[dimensions], "degenerate axis keeps scale");
                request[dimensions] = 1e-12;
                auto thickness = ResolveDimensionScale(*degenerate, request);
                GEOM08_REQUIRE(!thickness && thickness.error() == GeometryAuthoringCode::DegenerateExtent, "positive degenerate thickness rejected exactly");
            }
            identity = originalSource->geometry;
            identity.bounds.maximum[0] = identity.bounds.minimum[0] - 1;
            GEOM08_REQUIRE(!MeasureObjectDimensions(identity, {1, 1, 1}), "inverted bounds rejected");
            identity = originalSource->geometry;
            identity.bounds.sphereCenter[1] = std::numeric_limits<double>::quiet_NaN();
            GEOM08_REQUIRE(!MeasureObjectDimensions(identity, {1, 1, 1}), "nonfinite bounds rejected");
            identity = originalSource->geometry;
            GEOM08_REQUIRE(!MeasureObjectDimensions(identity, {1, std::numeric_limits<float>::infinity(), 1}), "nonfinite scale rejected");
        }

        // CPU preparation rejects unsupported layouts and preserves explicit basis
        // handedness. It mints no identity and does not bypass retained-source gates.
        {
            struct Vertex { float p[3], n[3], t[4], b[3]; std::uint8_t color[4], padding[4]; };
            const Vertex vertex{{1, 2, 3}, {0, 0, 1}, {1, 0, 0, -1}, {0, -1, 0}, {17, 29, 43, 71}, {7, 11, 13, 19}};
            std::array<VertexAttribute, 5> attributes{{
                {VertexSemantic::Position, AttributeSlot(VertexSemantic::Position), VertexScalarFormat::Float32, 3, VertexInterpretation::Floating, offsetof(Vertex, p)},
                {VertexSemantic::Normal, AttributeSlot(VertexSemantic::Normal), VertexScalarFormat::Float32, 3, VertexInterpretation::Floating, offsetof(Vertex, n)},
                {VertexSemantic::Tangent, AttributeSlot(VertexSemantic::Tangent), VertexScalarFormat::Float32, 4, VertexInterpretation::Floating, offsetof(Vertex, t)},
                {VertexSemantic::Bitangent, AttributeSlot(VertexSemantic::Bitangent), VertexScalarFormat::Float32, 3, VertexInterpretation::Floating, offsetof(Vertex, b)},
                {VertexSemantic::Color0, AttributeSlot(VertexSemantic::Color0), VertexScalarFormat::UInt8, 4, VertexInterpretation::Normalized, offsetof(Vertex, color)}}};
            const SubmeshRange range{0, 1, 0};
            auto source = MeshSourceData::FromVertices<Vertex>({&vertex, 1}, attributes);
            source.submeshes = {&range, 1};
            auto mesh = MeshAsset::Create(source);
            GEOM08_REQUIRE(mesh, "complete authored basis fixture");
            auto prepared = BakeAuthoredGeometry(*mesh, {2, 3, 4});
            GEOM08_REQUIRE(prepared, "basis preparation");
            Vertex result;
            std::memcpy(&result, prepared->Vertices().data(), sizeof(result));
            GEOM08_REQUIRE(result.p[0] == 2 && result.p[1] == 6 && result.p[2] == 12 && result.t[3] == -1 && result.b[1] == -1 &&
                std::memcmp(result.color, vertex.color, 8) == 0, "handedness bitangent authored color and padding retained");
            attributes[4].semantic = VertexSemantic::JointIndices;
            attributes[4].slot = AttributeSlot(VertexSemantic::JointIndices);
            attributes[4].interpretation = VertexInterpretation::Integer;
            auto unsupported = MeshAsset::Create(source);
            GEOM08_REQUIRE(unsupported, "legal CPU layout outside initial Bake basis subset");
            auto rejected = BakeAuthoredGeometry(*unsupported, {2, 3, 4});
            GEOM08_REQUIRE(!rejected && GeometryFailure(rejected.error(), GeometryAuthoringCode::UnsupportedLayout), "unsupported Bake layout typed rejection");
        }

        // Actual transitions, including failed Duplicate/Copy after renderer copying.
        {
            auto scene = CreateRefPtr<_Scene>();
            auto created = scene->CreateEntityWithUUID(UUID(0x8001), "count-source");
            GEOM08_REQUIRE(created, "owner entity creation");
            auto entity = *created;
            auto assigned = owner.AssignRenderable(entity, {cube, material});
            GEOM08_REQUIRE(assigned && assigned->changed, "semantic assignment");
            auto noOp = owner.AssignRenderable(entity, {cube, material});
            GEOM08_REQUIRE(noOp && !noOp->changed && noOp->revision == assigned->revision, "assignment no-op revision");
            auto duplicate = entity.Duplicate();
            GEOM08_REQUIRE(duplicate && duplicate->GetUUID() != entity.GetUUID() &&
                duplicate->GetComponent<MeshRendererComponent>().mesh == cube, "Duplicate shares mesh with fresh Entity");
            GEOM08_REQUIRE(owner.GeometryOwners(cube)->entityOwners == baseOwners + 2, "Duplicate increments owners");
            auto copied = _Scene::Copy(scene);
            GEOM08_REQUIRE(copied && owner.GeometryOwners(cube)->entityOwners == baseOwners + 4, "Scene Copy increments cross Scene owners");
            copied->reset();
            GEOM08_REQUIRE(owner.GeometryOwners(cube)->entityOwners == baseOwners + 2, "Scene destruction removes only its contributions");
            entity.AddOrReplaceComponent<RenderComponent>(); // Invalid legacy program: typed late copy failure.
            auto failedDuplicate = entity.Duplicate();
            GEOM08_REQUIRE(!failedDuplicate && failedDuplicate.error().code == SceneErrorCode::InvalidProgram &&
                owner.GeometryOwners(cube)->entityOwners == baseOwners + 2, "failed Duplicate owner cleanup");
            auto failedCopy = _Scene::Copy(scene);
            GEOM08_REQUIRE(!failedCopy && failedCopy.error().code == SceneErrorCode::InvalidProgram &&
                owner.GeometryOwners(cube)->entityOwners == baseOwners + 2, "failed Scene Copy owner cleanup");
            entity.RemoveComponent<RenderComponent>();
            auto renderId = scene->RenderData().Identify(entity);
            GEOM08_REQUIRE(renderId, "value replacement identity");
            const auto originalIntent = entity.GetComponent<MeshRendererComponent>();
            auto invalidIntent = originalIntent; invalidIntent.submesh = UINT32_MAX;
            GEOM08_REQUIRE(scene->RenderData().Replace(*renderId, invalidIntent), "invalid assignment preflight setup");
            auto retainedBefore = owner.GeometryWork(); const auto revisionBefore = scene->GetRenderAssignmentRevision();
            auto invalidUnique = owner.MakeGeometryUnique(entity);
            GEOM08_REQUIRE(!invalidUnique && std::holds_alternative<SceneAssignmentError>(invalidUnique.error().cause) &&
                std::get<SceneAssignmentError>(invalidUnique.error().cause) == SceneAssignmentError::InvalidSubmesh &&
                owner.GeometryWork().sourceRecords == retainedBefore.sourceRecords && scene->GetRenderAssignmentRevision() == revisionBefore &&
                entity.GetComponent<MeshRendererComponent>() == invalidIntent, "assignment preparation failure publishes no Clone");
            GEOM08_REQUIRE(scene->RenderData().Replace(*renderId, originalIntent), "restore supported test value");
            auto clone = owner.CloneGeometry(cube);
            GEOM08_REQUIRE(clone && clone->handle != cube && clone->revision == 1, "explicit standalone Clone");
            GEOM08_REQUIRE(scene->RenderData().Replace(*renderId, MeshRendererComponent{clone->handle, material}), "value replacement transition");
            GEOM08_REQUIRE(owner.GeometryOwners(cube)->entityOwners == baseOwners + 1 &&
                owner.GeometryOwners(clone->handle)->entityOwners == 1, "reassignment owner deltas");
            auto beforeRetention = owner.GeometryWork();
            GEOM08_REQUIRE(scene->RenderData().Remove<MeshRendererComponent>(*renderId), "renderer removal");
            GEOM08_REQUIRE(owner.GeometryOwners(clone->handle)->entityOwners == 0, "removal decrements clone owner");
            GEOM08_REQUIRE(scene->RenderData().Add(*renderId, MeshRendererComponent{clone->handle, material}), "renderer construction transition");
            GEOM08_REQUIRE(owner.GeometryOwners(clone->handle)->entityOwners == 1, "construction increments clone owner");
            GEOM08_REQUIRE(scene->DestroyEntity(entity), "Entity destruction");
            auto recycled = scene->CreateEntityWithUUID(UUID(0x8002), "generation-reuse");
            GEOM08_REQUIRE(recycled && owner.AssignRenderable(*recycled, {clone->handle, material}) &&
                owner.GeometryOwners(clone->handle)->entityOwners == 1, "Entity generation receipt reuse");
            GEOM08_REQUIRE(owner.GeometryWork().sourceRecords == beforeRetention.sourceRecords &&
                owner.GeometryWork().sourcePayloadBytes == beforeRetention.sourcePayloadBytes, "destroyed owners do not reclaim CPU source");
            recycled->RemoveComponent<IDComponent>();
            auto missingCopy = _Scene::Copy(scene);
            GEOM08_REQUIRE(!missingCopy && missingCopy.error().code == SceneErrorCode::MissingIdentity, "invalid copy identity rejected before accounting");
        }
        GEOM08_REQUIRE(owner.GeometryOwners(cube)->entityOwners == baseOwners, "temporary Scene complete owner cleanup");

        // Bounded query work is independent of Scene/component population.
        for (const std::size_t population : {std::size_t(1), std::size_t(64), std::size_t(1024)})
        {
            auto scene = CreateRefPtr<_Scene>();
            for (std::size_t i = 0; i < population; ++i)
            {
                auto entity = scene->CreateEntityWithUUID(UUID(0x80800000 + population * 4096 + i), "query-owner");
                GEOM08_REQUIRE(entity && owner.AssignRenderable(*entity, {cube, material}), "query population assignment");
            }
            const auto before = owner.GeometryWork();
            for (int query = 0; query < 100; ++query)
            {
                auto owners = owner.GeometryOwners(cube);
                GEOM08_REQUIRE(owners && owners->entityOwners == baseOwners + population, "query exact owner count");
            }
            const auto after = owner.GeometryWork();
            GEOM08_REQUIRE(after.ownershipQueries - before.ownershipQueries == 100 &&
                after.ownershipKeyComparisons - before.ownershipKeyComparisons <= 900, "query at most nine key comparisons independent of owners");
            const auto copyBefore = owner.GeometryWork();
            auto copied = _Scene::Copy(scene);
            GEOM08_REQUIRE(copied && owner.GeometryWork().ownerAdditions - copyBefore.ownerAdditions == population &&
                owner.GeometryOwners(cube)->entityOwners == baseOwners + 2 * population, "copy updates each owner once");
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_08_OWNERS population={} queries=100 key_comparisons={} copy_additions={} receipt_bytes={} copied_receipt_bytes={}",
                population, after.ownershipKeyComparisons - before.ownershipKeyComparisons, population,
                scene->GeometryOwnershipReceiptBytes(), (*copied)->GeometryOwnershipReceiptBytes());
        }
        GEOM08_REQUIRE(owner.GeometryOwners(cube)->entityOwners == baseOwners, "population retirement restores count");

        // Ordinary dimensions/pivot/parent edits do not publish geometry.
        {
            auto scene = CreateRefPtr<_Scene>();
            GeometryTemplates::Cube request{2, 3, 4}; request.Options.Pivot = GeometryTemplates::PivotLocation::MinimumCorner;
            auto mesh = owner.PublishGeometry(request);
            auto entity = scene->CreateEntity("dimensions-child"); auto parent = scene->CreateEntity("dimensions-parent");
            GEOM08_REQUIRE(mesh && entity && parent && owner.AssignRenderable(*entity, {*mesh, material}), "pivot dimensions setup");
            auto parentId = scene->RenderData().Identify(*parent);
            GEOM08_REQUIRE(parentId, "parent identity");
            RelationshipComponent relationship;
            relationship.ParentHandle = parent->GetUUID(); relationship.ParentIdentity = *parentId;
            entity->AddOrReplaceComponent<RelationshipComponent>(relationship);
            parent->Transform().SetScale({3, 2, 1}); parent->Transform().SetRotation({0, .7f, .3f});
            entity->Transform().SetRotation({.2f, .4f, .6f});
            entity->Transform().SetScale({-2, 1, .5f});
            auto source = owner.GeometrySource(*mesh); const auto work = owner.GeometryWork();
            std::size_t notifications = 0;
            auto notify = [&](const Vec3f&) { ++notifications; };
            auto connection = entity->Transform().OnScaleChanged.Connect<decltype(notify), &decltype(notify)::operator()>(notify);
            auto dimensions = owner.SetDimensions(*entity, {6, 9, 8});
            GEOM08_REQUIRE(dimensions && dimensions->changed && entity->Transform().Scale == Vec3f(-3, 3, 2) && notifications == 1,
                "dimensions use supported setter preserving sign");
            auto unchanged = owner.SetDimensions(*entity, {6, 9, 8});
            GEOM08_REQUIRE(unchanged && !unchanged->changed && notifications == 1, "dimension setter no-op callback");
            auto invalid = owner.SetDimensions(*entity, {6, 9, 0});
            GEOM08_REQUIRE(!invalid && entity->Transform().Scale == Vec3f(-3, 3, 2) && notifications == 1, "invalid dimensions transactional");
            entity->Transform().OnScaleChanged.Disconnect(connection);
            auto world = scene->UpdateWorldTransforms();
            GEOM08_REQUIRE(world, "parent world composition");
            const auto id = scene->RenderData().Identify(*entity);
            auto found = std::find_if(world->transforms.begin(), world->transforms.end(), [&](const auto& value) { return value.entity == *id; });
            GEOM08_REQUIRE(found != world->transforms.end(), "resolved child world matrix");
            auto bounds = TransformBounds(source->geometry.bounds, found->matrix);
            std::array<double, 3> minimum{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
            std::array<double, 3> maximum{-minimum[0], -minimum[1], -minimum[2]};
            for (int corner = 0; corner < 8; ++corner)
            {
                glm::dvec4 p(0, 0, 0, 1);
                for (int axis = 0; axis < 3; ++axis) p[axis] = (corner & (1 << axis)) ? source->geometry.bounds.maximum[axis] : source->geometry.bounds.minimum[axis];
                p = glm::dmat4(found->matrix) * p;
                for (int axis = 0; axis < 3; ++axis) { minimum[axis] = (std::min)(minimum[axis], p[axis]); maximum[axis] = (std::max)(maximum[axis], p[axis]); }
            }
            for (int axis = 0; axis < 3; ++axis)
                GEOM08_REQUIRE(AuthoredClose(bounds.minimum[axis], minimum[axis]) && AuthoredClose(bounds.maximum[axis], maximum[axis]), "world AABB uses affine reference");
            auto current = owner.Dimensions(*entity);
            GEOM08_REQUIRE((current && current->dimensions == std::array<double, 3>{6, 9, 8} && source->geometry.bounds.minimum == std::array<double, 3>{0, 0, 0}), "rotation parent and pivot do not redefine local dimensions");
            GEOM08_REQUIRE(entity->GetComponent<MeshRendererComponent>().mesh == *mesh && owner.GeometryWork().sourceRecords == work.sourceRecords &&
                AuthoredBytesEqual(*source->source, *owner.GeometrySource(*mesh)->source), "stretch publishes no geometry and does not recenter pivot");
            auto planeMesh = owner.PublishGeometry(GeometryTemplates::Plane{2, 4});
            GEOM08_REQUIRE(planeMesh && owner.AssignRenderable(*entity, {*planeMesh, material}), "planar Entity setup");
            entity->Transform().SetScale({0, -5, 1});
            auto planar = owner.SetDimensions(*entity, {4, 0, 8});
            GEOM08_REQUIRE(planar && entity->Transform().Scale == Vec3f(2, -5, 2), "planar zero axis and restored zero scale");
            GEOM08_REQUIRE(!owner.SetDimensions(*entity, {4, 1e-12, 8}), "planar thickness does not regenerate");
        }

        // Clone current authored state, independent modifications, and old frames.
        {
            GeometryTemplates::Sphere request; request.Options.Tangents = GeometryTemplates::TangentMode::Generate;
            auto mesh = owner.PublishGeometry(request);
            GEOM08_REQUIRE(mesh, "curved normal reference");
            auto source = owner.GeometrySource(*mesh);
            auto first = owner.CloneGeometry(*mesh), second = owner.CloneGeometry(*mesh);
            GEOM08_REQUIRE(source && first && second && first->handle != second->handle && first->handle != *mesh &&
                AuthoredBytesEqual(*source->source, *owner.GeometrySource(first->handle)->source) &&
                AuthoredBytesEqual(*source->source, *owner.GeometrySource(second->handle)->source), "three identities initially equal authored data");
            auto scene = CreateRefPtr<_Scene>(); auto entity = scene->CreateEntity("Bake-unique");
            GEOM08_REQUIRE(entity && owner.AssignRenderable(*entity, {first->handle, material}), "unique Bake assignment");
            entity->Transform().SetScale({2, 3, .5f});
            std::optional<RenderFrame> oldFrame;
            {
                auto access = owner.Publication().BeginFrame(); RenderExtractionStats stats;
                auto frame = ExtractRenderFrame(*scene, owner.ForFrame(access), stats);
                GEOM08_REQUIRE(frame && frame->Draws().size() == 1, "retained old frame");
                oldFrame.emplace(std::move(*frame));
            }
            const auto oldDraw = oldFrame->Draws()[0];
            const auto oldResource = oldFrame->Resources()[0].Mesh().Get();
            const auto oldBounds = TransformBounds(oldResource->Bounds(), oldDraw.worldTransform);
            auto cloneSource = owner.GeometrySource(first->handle);
            auto noClone = owner.MakeGeometryUnique(*entity);
            GEOM08_REQUIRE(noClone && !noClone->changed && noClone->geometry.handle == first->handle, "leases and source views do not make a unique geometry shared");
            auto baked = owner.BakeGeometry(*entity);
            GEOM08_REQUIRE(baked && baked->changed && baked->previous.handle == first->handle && baked->geometry.handle != first->handle &&
                baked->geometry.revision == 1 && entity->Transform().Scale == Vec3f(1), "changed Bake uses fresh identity plus scale reset");
            auto bakedSource = owner.GeometrySource(baked->geometry.handle);
            GEOM08_REQUIRE(bakedSource && AuthoredBytesEqual(*source->source, *cloneSource->source) &&
                AuthoredBytesEqual(*source->source, *owner.GeometrySource(second->handle)->source), "changed clone leaves source and second clone unchanged");
            const auto& original = *cloneSource->source; const auto& changed = *bakedSource->source;
            GEOM08_REQUIRE(std::ranges::equal(original.Indices(), changed.Indices()) && original.Submeshes().size() == changed.Submeshes().size(), "Bake winding and submeshes preserved");
            const auto position = std::ranges::find(original.Layout().attributes, VertexSemantic::Position, &VertexAttribute::semantic);
            const auto normal = std::ranges::find(original.Layout().attributes, VertexSemantic::Normal, &VertexAttribute::semantic);
            const auto tangent = std::ranges::find(original.Layout().attributes, VertexSemantic::Tangent, &VertexAttribute::semantic);
            GEOM08_REQUIRE(position != original.Layout().attributes.end() && normal != original.Layout().attributes.end() && tangent != original.Layout().attributes.end(), "normal tangent fixture attributes");
            const glm::dvec3 scale(2, 3, .5);
            const glm::dmat3 normalMatrix = glm::transpose(glm::inverse(glm::dmat3(glm::scale(glm::dmat4(1), scale))));
            for (std::size_t vertex = 0; vertex < original.VertexCount(); ++vertex)
            {
                const auto expectedPosition = AuthoredVector(original, vertex, *position) * scale;
                const auto actualPosition = AuthoredVector(changed, vertex, *position);
                const auto expectedNormal = glm::normalize(normalMatrix * AuthoredVector(original, vertex, *normal));
                const auto actualNormal = AuthoredVector(changed, vertex, *normal);
                auto expectedTangent = AuthoredVector(original, vertex, *tangent) * scale;
                expectedTangent = glm::normalize(expectedTangent - expectedNormal * glm::dot(expectedNormal, expectedTangent));
                const auto actualTangent = AuthoredVector(changed, vertex, *tangent);
                for (int axis = 0; axis < 3; ++axis)
                {
                    GEOM08_REQUIRE(AuthoredClose(actualPosition[axis], expectedPosition[axis]), "baked position reference");
                    GEOM08_REQUIRE(std::abs(actualNormal[axis] - expectedNormal[axis]) <= 1e-5 &&
                        std::abs(actualTangent[axis] - expectedTangent[axis]) <= 1e-5, "inverse transpose and tangent reference");
                    GEOM08_REQUIRE(changed.Bounds().minimum[axis] <= actualPosition[axis] && changed.Bounds().maximum[axis] >= actualPosition[axis], "baked bounds contain vertices");
                }
                GEOM08_REQUIRE(std::abs(glm::length(actualNormal) - 1) <= 1e-5 && std::abs(glm::length(actualTangent) - 1) <= 1e-5 &&
                    std::abs(glm::dot(actualNormal, actualTangent)) <= 1e-5, "unit orthogonal basis");
                for (const auto& attribute : original.Layout().attributes)
                    if (attribute.semantic == VertexSemantic::TexCoord0)
                    {
                        const auto offset = vertex * original.Layout().strideBytes + attribute.offsetBytes;
                        GEOM08_REQUIRE(std::memcmp(original.Vertices().data() + offset, changed.Vertices().data() + offset,
                            attribute.components * sizeof(float)) == 0, "UV bytes unchanged");
                    }
            }
            GEOM08_REQUIRE(oldFrame->Resources()[0].Mesh().Identity() == first->handle && oldFrame->Resources()[0].Mesh().Revision() == 1 &&
                oldFrame->Resources()[0].Mesh().Get() == oldResource &&
                TransformBounds(oldResource->Bounds(), oldFrame->Draws()[0].worldTransform) == oldBounds,
                "old frame exact resource and bounds remain valid");
            auto currentClone = owner.CloneGeometry(baked->geometry.handle);
            GEOM08_REQUIRE(currentClone && AuthoredBytesEqual(changed, *owner.GeometrySource(currentClone->handle)->source), "Clone uses changed current authored state");
            auto secondEntity = scene->CreateEntity("independent-source-edit");
            GEOM08_REQUIRE(secondEntity && owner.AssignRenderable(*secondEntity, {*mesh, material}), "canonical source owner");
            auto uniqueSource = owner.MakeGeometryUnique(*secondEntity);
            GEOM08_REQUIRE(uniqueSource && uniqueSource->changed, "canonical pin requires Clone even with one Entity owner");
            secondEntity->Transform().SetScale({.5f, 2, 1});
            GEOM08_REQUIRE(owner.BakeGeometry(*secondEntity) && AuthoredBytesEqual(changed, *owner.GeometrySource(baked->geometry.handle)->source), "independent source edit leaves changed clone unchanged");
            auto shared = entity->Duplicate();
            GEOM08_REQUIRE(shared, "sharing reintroduced");
            auto refused = owner.BakeGeometry(*entity);
            GEOM08_REQUIRE(!refused && GeometryFailure(refused.error(), GeometryAuthoringCode::GeometryNotUnique), "later sharing invalidates Bake uniqueness");
            GEOM08_REQUIRE(scene->DestroyEntity(*shared), "shared owner removal");
            auto identityNoOp = owner.BakeGeometry(*entity);
            GEOM08_REQUIRE(identityNoOp && !identityNoOp->changed && identityNoOp->geometry.handle == baked->geometry.handle, "exact identity Bake no-op");
            for (const Vec3f bad : {Vec3f(0, 1, 1), Vec3f(-1, 1, 1), Vec3f(std::numeric_limits<float>::infinity(), 1, 1)})
            {
                entity->Transform().SetScale(bad);
                const auto revision = scene->GetRenderAssignmentRevision(); const auto retained = owner.GeometryWork();
                auto failed = owner.BakeGeometry(*entity);
                GEOM08_REQUIRE(!failed && GeometryFailure(failed.error(), GeometryAuthoringCode::InvalidScale) &&
                    entity->GetComponent<MeshRendererComponent>().mesh == baked->geometry.handle &&
                    scene->GetRenderAssignmentRevision() == revision && owner.GeometryWork().sourceRecords == retained.sourceRecords,
                    "invalid Bake scale rollback");
            }
            entity->Transform().SetScale({2, 1, 1});
            std::size_t signals = 0; auto callback = [&](const Vec3f&) { ++signals; };
            auto connection = entity->Transform().OnScaleChanged.Connect<decltype(callback), &decltype(callback)::operator()>(callback);
            auto callbackFailure = owner.BakeGeometry(*entity);
            GEOM08_REQUIRE(!callbackFailure && GeometryFailure(callbackFailure.error(), GeometryAuthoringCode::ScaleCallbackUnsupported) && signals == 0,
                "unreviewed reentrant reset callback rejected before modification");
            entity->Transform().OnScaleChanged.Disconnect(connection);
        }

        // A raw mutable borrow explicitly invalidates proof; source and stale
        // receipt contributions are removed incrementally when its Scene retires.
        {
            auto scene = CreateRefPtr<_Scene>(); auto entity = scene->CreateEntity("unsafe-borrow");
            GEOM08_REQUIRE(entity && owner.AssignRenderable(*entity, {cube, material}), "raw escape setup");
            auto& raw = scene->Reg();
            raw.get<MeshRendererComponent>(*entity).mesh = {};
            auto unknown = owner.GeometryOwners(cube);
            GEOM08_REQUIRE(!unknown && GeometryFailure(unknown.error(), GeometryAuthoringCode::OwnershipUnavailable), "raw reference cannot falsely prove uniqueness");
        }
        GEOM08_REQUIRE(owner.GeometryOwners(cube)->entityOwners == baseOwners, "retired unsafe Scene restores complete coverage");

        // Capacity/publication and source-version failures use smaller limits and
        // existing diagnostic registry operations, never general source GC.
        {
            auto limited = SceneRenderResources::Create(root, {0, 0});
            GEOM08_REQUIRE(limited, "zero independent quota owner");
            auto mesh = (*limited)->PublishGeometry(GeometryTemplates::Cube{});
            GEOM08_REQUIRE(mesh, "quota canonical source");
            const auto before = (*limited)->GeometryWork();
            auto rejected = (*limited)->CloneGeometry(*mesh);
            GEOM08_REQUIRE(!rejected && GeometryFailure(rejected.error(), GeometryAuthoringCode::SourceCapacity) &&
                (*limited)->GeometryWork().sourceRecords == before.sourceRecords && (*limited)->Meshes().Size() == 1, "count capacity rollback");
            auto byteLimited = SceneRenderResources::Create(root, {64, 1});
            GEOM08_REQUIRE(byteLimited, "byte quota owner");
            auto byteMesh = (*byteLimited)->PublishGeometry(GeometryTemplates::Cube{});
            GEOM08_REQUIRE(byteMesh && !(*byteLimited)->CloneGeometry(*byteMesh), "byte capacity rejection");
            Asset::AssetRegistryLimits slots; slots.maxSlots = 1;
            auto slotLimited = SceneRenderResources::Create(root, {}, slots);
            GEOM08_REQUIRE(slotLimited, "existing registry slot limit owner");
            auto slotMesh = (*slotLimited)->PublishGeometry(GeometryTemplates::Cube{});
            GEOM08_REQUIRE(slotMesh, "slot source publication");
            auto slotClone = (*slotLimited)->CloneGeometry(*slotMesh);
            GEOM08_REQUIRE(!slotClone && std::holds_alternative<GpuMeshError>(slotClone.error().cause) &&
                std::get<GpuMeshError>(slotClone.error().cause).registry == Asset::RegistryError::SlotsExhausted &&
                (*slotLimited)->GeometryWork().sourceRecords == 1 && (*slotLimited)->Meshes().Size() == 1, "publication failure preserves source and budget");
        }
        {
            auto diagnostic = SceneRenderResources::Create(root);
            GEOM08_REQUIRE(diagnostic, "diagnostic source owner");
            auto mesh = (*diagnostic)->PublishGeometry(GeometryTemplates::Cube{});
            GEOM08_REQUIRE(mesh, "diagnostic canonical publication");
            auto cpu = (*diagnostic)->GeometrySource(*mesh);
            GEOM08_REQUIRE(cpu, "retained CPU view");
            Asset::MeshHandle rawHandle;
            {
                auto gpu = GpuMesh::Create(*cpu->source);
                GEOM08_REQUIRE(gpu, "unsupported raw mesh setup");
                auto publication = (*diagnostic)->Publication().BeginPublication();
                auto created = (*diagnostic)->Meshes().Create(publication, std::move(*gpu));
                GEOM08_REQUIRE(created, "raw publication"); rawHandle = *created;
            }
            auto absent = (*diagnostic)->CloneGeometry(rawHandle);
            GEOM08_REQUIRE(!absent && GeometryFailure(absent.error(), GeometryAuthoringCode::SourceUnavailable), "raw source unavailable has no regeneration fallback");
            auto foreign = owner.CloneGeometry(*mesh);
            GEOM08_REQUIRE(!foreign && std::holds_alternative<Asset::RegistryError>(foreign.error().cause), "foreign domain rejected");
            {
                auto replacement = GpuMesh::Create(*cpu->source);
                GEOM08_REQUIRE(replacement, "replacement setup");
                auto publication = (*diagnostic)->Publication().BeginPublication();
                GEOM08_REQUIRE((*diagnostic)->Meshes().Replace(publication, *mesh, std::move(*replacement)), "raw version replacement");
            }
            auto mismatch = (*diagnostic)->CloneGeometry(*mesh);
            GEOM08_REQUIRE(!mismatch && GeometryFailure(mismatch.error(), GeometryAuthoringCode::SourceVersionMismatch) &&
                cpu->geometry.revision == 1 && !cpu->source->Vertices().empty(), "retained old CPU source survives but stale pairing is rejected");
            {
                auto publication = (*diagnostic)->Publication().BeginPublication();
                GEOM08_REQUIRE((*diagnostic)->Meshes().Destroy(publication, rawHandle), "generational slot destroy");
                (*diagnostic)->Meshes().Collect(publication);
            }
            GeometryTemplates::Cube another{2, 2, 2};
            auto reused = (*diagnostic)->PublishGeometry(another);
            GEOM08_REQUIRE(reused && reused->index == rawHandle.index && reused->generation != rawHandle.generation &&
                !(*diagnostic)->CloneGeometry(rawHandle), "existing generational free list retains stale identity rejection");
            Asset::AssetRegistryLimits revisions; revisions.maxRevision = 1;
            auto exhausted = SceneRenderResources::Create(root, {}, revisions);
            GEOM08_REQUIRE(exhausted, "revision limit owner");
            auto revisionMesh = (*exhausted)->PublishGeometry(GeometryTemplates::Cube{});
            GEOM08_REQUIRE(revisionMesh, "revision source publication");
            auto revisionSource = (*exhausted)->GeometrySource(*revisionMesh);
            auto gpu = GpuMesh::Create(*revisionSource->source);
            GEOM08_REQUIRE(gpu, "revision replacement preparation");
            {
                auto publication = (*exhausted)->Publication().BeginPublication();
                auto failure = (*exhausted)->Meshes().Replace(publication, *revisionMesh, std::move(*gpu));
                GEOM08_REQUIRE(!failure && failure.error() == Asset::RegistryError::RevisionExhausted, "revision exhaustion preserves publication");
            }
            GEOM08_REQUIRE((*exhausted)->GeometrySource(*revisionMesh)->geometry.revision == 1, "failed replacement retains CPU pairing");
        }

        // Existing S22 callbacks, including the initial point-source scale policy.
        {
            auto scene = CreateRefPtr<_Scene>();
            auto sphere = scene->CreateEntity("sphere-bridge"); auto box = scene->CreateEntity("box-bridge");
            GEOM08_REQUIRE(sphere && box, "Physics bridge entities");
            auto sphereGeometry = owner.CloneGeometry(cube), boxGeometry = owner.CloneGeometry(cube);
            GEOM08_REQUIRE(sphereGeometry && boxGeometry && owner.AssignRenderable(*sphere, {sphereGeometry->handle, material}) &&
                owner.AssignRenderable(*box, {boxGeometry->handle, material}), "unique render geometry for bridge cases");
            GEOM08_REQUIRE(owner.AttachPhysicsShape(*sphere, "Sphere") && owner.AttachPhysicsShape(*box, "Box"), "existing CPU collider sources");
            sphere->AddOrReplaceComponent<RigidBody3DComponent>(); box->AddOrReplaceComponent<RigidBody3DComponent>();
            SphereFixture3DComponent sphereFixture; sphereFixture.Radius = 2;
            sphere->AddOrReplaceComponent<SphereFixture3DComponent>(sphereFixture);
            box->AddOrReplaceComponent<BoxFixture3DComponent>(); box->Transform().SetScale({2, 3, 4});
            auto started = scene->OnRuntimeStart();
            GEOM08_REQUIRE(started, "temporary Physics runtime");
            struct RuntimeCleanup
            {
                RefPtr<_Scene> scene;
                std::vector<std::unique_ptr<PhysicalShape>> shapes;
                ~RuntimeCleanup() { scene->OnRuntimeStop(); }
            } cleanup{scene, {}};
            for (auto* body : scene->GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies()) cleanup.shapes.emplace_back(body->m_Shape);
            auto* sphereShape = dynamic_cast<ShapeSphere*>(sphere->GetComponent<RigidBody3DComponent>().RuntimeBody->m_Shape);
            auto* boxShape = dynamic_cast<ShapeBox*>(box->GetComponent<RigidBody3DComponent>().RuntimeBody->m_Shape);
            GEOM08_REQUIRE(sphereShape && boxShape, "temporary shapes use existing providers");
            const auto sphereRevision = sphereShape->GetRevision();
            sphere->Transform().SetScale({1, 1, 1});
            GEOM08_REQUIRE(sphereShape->GetRadius() == 2 && sphereShape->GetRevision() == sphereRevision, "unchanged setter does not notify live bridge");
            auto resized = owner.SetDimensions(*sphere, {4, 9, 16}); // Cube local extents 2,3,4 -> scale 2,3,4.
            GEOM08_REQUIRE(resized && sphereShape->GetRadius() == 4, "dimension setter preserves sphere X-axis bridge policy");
            sphere->Transform().Scale = {3, 3, 3};
            GEOM08_REQUIRE(sphereShape->GetRadius() == 4, "direct Scale write does not invoke collider callback");
            sphere->Transform().SetScale({4, 1, 1});
            GEOM08_REQUIRE(sphereShape->GetRadius() == 8, "repeated sphere scale is absolute from base source");
            sphereShape->SetRadius(3); sphere->Transform().SetScale({2, 1, 1});
            GEOM08_REQUIRE(sphereShape->GetRadius() == 6, "explicit collider radius updates independent base source");
            const auto oldIntent = sphere->GetComponent<MeshRendererComponent>(); const auto oldScale = sphere->Transform().Scale;
            const auto oldAssignment = scene->GetRenderAssignmentRevision();
            auto denied = owner.BakeGeometry(*sphere);
            GEOM08_REQUIRE(!denied && GeometryFailure(denied.error(), GeometryAuthoringCode::LiveScaleBinding) &&
                sphere->GetComponent<MeshRendererComponent>() == oldIntent && sphere->Transform().Scale == oldScale &&
                scene->GetRenderAssignmentRevision() == oldAssignment && sphereShape->GetRadius() == 6, "Bake live bridge rejected before any reset");
            const auto base = boxShape->GetBounds();
            box->Transform().SetScale({.5f, 1.5f, 2}); const auto scaled = boxShape->GetBounds();
            GEOM08_REQUIRE(AuthoredClose(scaled.WidthX(), base.WidthX() * .5) && AuthoredClose(scaled.WidthY(), base.WidthY() * 1.5) &&
                AuthoredClose(scaled.WidthZ(), base.WidthZ() * 2), "box source includes initial Transform scale");
            box->Transform().SetScale({2, 2, 2}); box->Transform().SetScale({.5f, 1.5f, 2});
            GEOM08_REQUIRE(boxShape->GetBounds().mins == scaled.mins && boxShape->GetBounds().maxs == scaled.maxs, "repeated point scale uses source not previous output");
            const auto boxRevision = boxShape->GetRevision();
            box->Transform().SetScale({0, 1, 1});
            GEOM08_REQUIRE(boxShape->GetRevision() == boxRevision && boxShape->GetBounds().mins == scaled.mins, "invalid collider rebuild retains old shape while setter policy remains unchanged");
        }
        GEOM08_REQUIRE(active.GetAllEntitiesWith<RigidBody3DComponent>().size() == originalBodies &&
            owner.GeometryOwners(cube)->entityOwners == baseOwners, "normal Physics and authoring owner membership preserved");
        const auto work = owner.GeometryWork();
        GEOM08_REQUIRE(work.independentSources <= 64 && work.independentPayloadBytes <= 64 * 1024 * 1024 &&
            work.sourceRecords <= 224 && work.peakTemporaryBytes <= 24 * 1024 * 1024, "bounded source/work retention");
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_08_PASS checks={} sources={} payload_bytes={} independent_sources={} independent_bytes={} peak_temporary_bytes={} physics_bodies={}",
            checks, work.sourceRecords, work.sourcePayloadBytes, work.independentSources, work.independentPayloadBytes, work.peakTemporaryBytes, originalBodies);
#undef GEOM08_REQUIRE
        return {};
    }
}
