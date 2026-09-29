#pragma once

#include "Scene/_Entity.h"
#include "Renderer/SceneRenderResources.h"
#include "Renderer/RenderExtraction.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/ShapeSphere.h"
#include "Core/Log.h"
#include <algorithm>
#include <type_traits>
#include <vector>

// Opt-in checks executed by the real RBS application on its context thread.
// They borrow its existing Box/Sphere/material publications and create no assets.
namespace PreEditorValidation
{
    inline std::expected<void, ::GEngine::PlatformError>
    CheckSceneAuthoring(::GEngine::_Scene& active, ::GEngine::SceneRenderResources& owner,
                        ::GEngine::Asset::MeshHandle box, ::GEngine::Asset::MeshHandle sphere,
                        ::GEngine::Asset::MaterialInstanceHandle boxMaterial,
                        ::GEngine::Asset::MaterialInstanceHandle sphereMaterial)
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Component;
        static_assert(std::is_trivially_copyable_v<MeshRendererComponent>);
        static_assert(!std::is_copy_constructible_v<GpuMesh>);
        static_assert(!std::is_copy_constructible_v<MeshAsset>);
        std::size_t checks = 0;
        auto require = [&](bool condition, const char* name)
        {
            ++checks;
            if (!condition)
                Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_04_FAIL {}", name);
            return condition;
        };
#define PRE_EDITOR_REQUIRE(condition, name)                                                        \
    if (!require(bool(condition), name))                                                           \
    return std::unexpected(                                                                        \
        PlatformError{PlatformErrorCode::Initialization, "Phase 04 scene authoring", name})

        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_04_BEGIN");
        const auto meshCount = owner.Meshes().Size();
        const auto materialCount = owner.Materials().Size();
        {
            auto scene = CreateRefPtr<_Scene>();
            auto access = owner.Publication().BeginFrame();
            const auto resources = owner.ForFrame(access);
            auto meshBefore = owner.Meshes().Acquire(access, box);
            auto materialBefore = owner.Materials().Acquire(access, boxMaterial);
            PRE_EDITOR_REQUIRE(meshBefore && materialBefore, "reference publications");
            auto sourceResult = scene->CreateEntityWithUUID(UUID(0x4001), "authoring-source");
            auto sharedResult = scene->CreateEntityWithUUID(UUID(0x4002), "authoring-shared");
            PRE_EDITOR_REQUIRE(sourceResult && sharedResult, "create");
            auto source = *sourceResult;
            auto shared = *sharedResult;
            shared.Transform().SetTranslation({2.f, 0.f, 0.f});
            const MeshRendererComponent value{box, boxMaterial};
            auto first = source.AssignRenderable(value, resources);
            PRE_EDITOR_REQUIRE(first && first->changed && first->revision == 1, "first assignment");
            auto sharing = scene->AssignRenderable(shared, value, resources);
            PRE_EDITOR_REQUIRE(sharing && sharing->changed && sharing->revision == 2,
                               "shared assignment");
            auto noChange = source.AssignRenderable(value, resources);
            PRE_EDITOR_REQUIRE(noChange && !noChange->changed && noChange->revision == 2,
                               "no-op revision");

            auto duplicateResult = source.Duplicate();
            PRE_EDITOR_REQUIRE(duplicateResult, "duplicate");
            auto duplicate = *duplicateResult;
            auto sourceId = scene->RenderData().Identify(source);
            auto duplicateId = scene->RenderData().Identify(duplicate);
            PRE_EDITOR_REQUIRE(sourceId && duplicateId && *sourceId != *duplicateId &&
                                   source.GetUUID() != duplicate.GetUUID(),
                               "fresh duplicate identity");
            PRE_EDITOR_REQUIRE(duplicate.GetComponent<MeshRendererComponent>() == value &&
                                   shared.GetComponent<MeshRendererComponent>() == value,
                               "duplicate shares handles");
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_04_IDENTITY source={}:{}:{} duplicate={}:{}:{} shared_mesh={}:{}:{}",
                sourceId->registry, sourceId->index, sourceId->generation, duplicateId->registry,
                duplicateId->index, duplicateId->generation, box.registry, box.index,
                box.generation);

            RenderExtractionStats oldStats;
            auto oldFrame = ExtractRenderFrame(*scene, resources, oldStats);
            PRE_EDITOR_REQUIRE(oldFrame && oldFrame->Draws().size() == 3, "old frame");
            std::vector<DrawItem> retained(oldFrame->Draws().begin(), oldFrame->Draws().end());
            const auto* retainedMesh = oldFrame->Resources()[0].Mesh().Get();
            const auto* retainedMaterial = oldFrame->Resources()[0].Material().Source().Get();
            const auto retainedMeshRevision = oldFrame->Resources()[0].Mesh().Revision();
            const auto retainedMaterialRevision =
                oldFrame->Resources()[0].Material().PublicationRevision();
            const auto words = oldFrame->Resources()[0].Material().PackedWords();
            const std::vector<std::uint32_t> retainedWords(words.begin(), words.end());

            const auto unchangedRevision = scene->GetRenderAssignmentRevision();
            auto invalid = value;
            invalid.mesh.registry = boxMaterial.registry;
            auto wrongMesh = source.AssignRenderable(invalid, resources);
            PRE_EDITOR_REQUIRE(!wrongMesh && wrongMesh.error() == SceneAssignmentError::InvalidMesh,
                               "wrong mesh domain");
            invalid = value;
            ++invalid.mesh.generation;
            auto staleMesh = source.AssignRenderable(invalid, resources);
            PRE_EDITOR_REQUIRE(!staleMesh && staleMesh.error() == SceneAssignmentError::InvalidMesh,
                               "stale mesh generation");
            invalid = value;
            invalid.material.registry = box.registry;
            auto wrongMaterial = source.AssignRenderable(invalid, resources);
            PRE_EDITOR_REQUIRE(!wrongMaterial &&
                                   wrongMaterial.error() == SceneAssignmentError::InvalidMaterial,
                               "wrong material domain");
            invalid = value;
            ++invalid.material.generation;
            auto staleMaterial = source.AssignRenderable(invalid, resources);
            PRE_EDITOR_REQUIRE(!staleMaterial &&
                                   staleMaterial.error() == SceneAssignmentError::InvalidMaterial,
                               "stale material generation");
            invalid = value;
            invalid.submesh = UINT32_MAX;
            auto badSubmesh = source.AssignRenderable(invalid, resources);
            PRE_EDITOR_REQUIRE(!badSubmesh &&
                                   badSubmesh.error() == SceneAssignmentError::InvalidSubmesh,
                               "invalid submesh");
            auto foreign = CreateRefPtr<_Scene>();
            auto foreignEntity = foreign->CreateEntity("foreign");
            PRE_EDITOR_REQUIRE(foreignEntity, "foreign setup");
            auto foreignAssignment = scene->AssignRenderable(*foreignEntity, value, resources);
            PRE_EDITOR_REQUIRE(!foreignAssignment &&
                                   foreignAssignment.error() == SceneAssignmentError::ForeignEntity,
                               "foreign entity");
            auto foreignDuplicate = scene->DuplicateEntity(*foreignEntity);
            PRE_EDITOR_REQUIRE(!foreignDuplicate &&
                                   foreignDuplicate.error().code == SceneErrorCode::ForeignEntity,
                               "foreign duplicate");
            auto invalidEntity = _Entity{}.AssignRenderable(value, resources);
            PRE_EDITOR_REQUIRE(!invalidEntity &&
                                   invalidEntity.error() == SceneAssignmentError::InvalidEntity,
                               "null entity");
            auto invalidDuplicate = _Entity{}.Duplicate();
            PRE_EDITOR_REQUIRE(!invalidDuplicate &&
                                   invalidDuplicate.error().code == SceneErrorCode::InvalidScene,
                               "null duplicate");
            {
                auto freeze = scene->RenderData().BeginExtraction();
                PRE_EDITOR_REQUIRE(freeze, "extraction setup");
                auto frozenAssignment = source.AssignRenderable(value, resources);
                PRE_EDITOR_REQUIRE(!frozenAssignment && frozenAssignment.error() ==
                                                            SceneAssignmentError::ExtractionActive,
                                   "frozen assignment");
            }
            PRE_EDITOR_REQUIRE(scene->GetRenderAssignmentRevision() == unchangedRevision &&
                                   source.GetComponent<MeshRendererComponent>() == value,
                               "failed assignment unchanged");
            auto copiedScene = _Scene::Copy(scene);
            PRE_EDITOR_REQUIRE(copiedScene, "scene copy");
            auto copiedEntity = (*copiedScene)->GetEntityByUUID(source.GetUUID());
            auto copiedId = (*copiedScene)->RenderData().Identify(copiedEntity);
            PRE_EDITOR_REQUIRE(copiedId && copiedId->registry != sourceId->registry &&
                                   copiedEntity.GetComponent<MeshRendererComponent>() == value,
                               "scene copy domain and sharing");

            auto changed = duplicate.AssignRenderable({sphere, sphereMaterial}, resources);
            PRE_EDITOR_REQUIRE(changed && changed->changed &&
                                   changed->revision == unchangedRevision + 1,
                               "changed assignment revision");
            duplicate.Transform().SetTranslation({4.f, 0.f, 0.f});
            RenderExtractionStats newStats;
            auto newFrame = ExtractRenderFrame(*scene, resources, newStats);
            PRE_EDITOR_REQUIRE(newFrame && newFrame->Draws().size() == 3, "new frame");
            auto changedDraw = std::find_if(newFrame->Draws().begin(), newFrame->Draws().end(),
                                            [&](const DrawItem& draw)
                                            {
                                                return draw.entity == *duplicateId;
                                            });
            PRE_EDITOR_REQUIRE(changedDraw != newFrame->Draws().end() &&
                                   changedDraw->mesh == sphere &&
                                   changedDraw->material == sphereMaterial &&
                                   changedDraw->worldTransform[3][0] == 4.f,
                               "new frame observes change");
            PRE_EDITOR_REQUIRE(source.GetComponent<MeshRendererComponent>() == value &&
                                   shared.GetComponent<MeshRendererComponent>() == value,
                               "independent binding");
            auto destroyed = scene->DestroyEntity(source);
            PRE_EDITOR_REQUIRE(destroyed, "destroy source");
            auto staleEntity = source.AssignRenderable(value, resources);
            PRE_EDITOR_REQUIRE(!staleEntity &&
                                   staleEntity.error() == SceneAssignmentError::InvalidEntity,
                               "stale entity");
            auto staleDuplicate = source.Duplicate();
            PRE_EDITOR_REQUIRE(!staleDuplicate &&
                                   staleDuplicate.error().code == SceneErrorCode::InvalidIdentity,
                               "stale duplicate");
            PRE_EDITOR_REQUIRE(!scene->RenderData().Resolve(*sourceId), "old identity retired");
            bool frameUnchanged = oldFrame->Draws().size() == retained.size();
            for (std::size_t i = 0; i < retained.size(); ++i)
            {
                const auto& before = retained[i];
                const auto& after = oldFrame->Draws()[i];
                frameUnchanged =
                    frameUnchanged && before.mesh == after.mesh &&
                    before.material == after.material && before.entity == after.entity &&
                    before.pipeline == after.pipeline &&
                    before.worldTransform == after.worldTransform &&
                    before.resources == after.resources && before.sortKey == after.sortKey &&
                    before.layers == after.layers && before.castShadows == after.castShadows &&
                    before.receiveShadows == after.receiveShadows &&
                    before.pickable == after.pickable &&
                    before.submesh.firstElement == after.submesh.firstElement &&
                    before.submesh.elementCount == after.submesh.elementCount &&
                    before.submesh.materialSlot == after.submesh.materialSlot;
            }
            PRE_EDITOR_REQUIRE(
                frameUnchanged && oldFrame->Resources()[0].Mesh().Get() == retainedMesh &&
                    oldFrame->Resources()[0].Material().Source().Get() == retainedMaterial &&
                    oldFrame->Resources()[0].Mesh().Revision() == retainedMeshRevision &&
                    oldFrame->Resources()[0].Material().PublicationRevision() ==
                        retainedMaterialRevision &&
                    std::ranges::equal(oldFrame->Resources()[0].Material().PackedWords(),
                                       retainedWords),
                "old frame unchanged after edit and destroy");
            auto meshAfter = owner.Meshes().Acquire(access, box);
            auto materialAfter = owner.Materials().Acquire(access, boxMaterial);
            PRE_EDITOR_REQUIRE(meshAfter && materialAfter &&
                                   meshAfter->Get() == meshBefore->Get() &&
                                   materialAfter->Get() == materialBefore->Get() &&
                                   meshAfter->Revision() == meshBefore->Revision() &&
                                   materialAfter->Revision() == materialBefore->Revision(),
                               "resource owners and versions unchanged");
            auto missing = scene->CreateEntity("missing-identity");
            PRE_EDITOR_REQUIRE(missing, "missing identity setup");
            missing->RemoveComponent<IDComponent>();
            auto missingAssignment = missing->AssignRenderable(value, resources);
            PRE_EDITOR_REQUIRE(!missingAssignment &&
                                   missingAssignment.error() ==
                                       SceneAssignmentError::MissingIdentity &&
                                   !missing->HasAllComponents<MeshRendererComponent>(),
                               "missing identity unchanged");
            auto duplicateUuid = scene->CreateEntityWithUUID(shared.GetUUID(), "duplicate-uuid");
            PRE_EDITOR_REQUIRE(!duplicateUuid &&
                                   duplicateUuid.error().code == SceneErrorCode::InvalidIdentity,
                               "duplicate UUID rejected");
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_04_FRAME old_draws={} new_draws={} assignment_revision={} resource_versions={}/{}",
                oldFrame->Draws().size(), newFrame->Draws().size(),
                scene->GetRenderAssignmentRevision(), meshAfter->Revision(),
                materialAfter->Revision());
        }
        // Exercise the existing Physics clone cleanup against an actual live body.
        auto live = active.FindEntityByName("wood_sphere0");
        PRE_EDITOR_REQUIRE(live && live.HasAllComponents<RigidBody3DComponent>(), "live sphere");
        auto* body = live.GetComponent<RigidBody3DComponent>().RuntimeBody;
        PRE_EDITOR_REQUIRE(body && body->m_Shape, "live Physics body");
        auto* shape = dynamic_cast<ShapeSphere*>(body->m_Shape);
        PRE_EDITOR_REQUIRE(shape, "sphere shape");
        const auto radius = shape->GetRadius();
        const auto bodies = active.GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies().size();
        const auto liveMesh = live.GetComponent<MeshRendererComponent>();
        auto clone = live.Duplicate();
        PRE_EDITOR_REQUIRE(clone &&
                               clone->GetComponent<RigidBody3DComponent>().RuntimeBody == nullptr &&
                               clone->GetComponent<MeshRendererComponent>() == liveMesh,
                           "duplicate clears runtime body");
        clone->Transform().SetScale(2.f);
        PRE_EDITOR_REQUIRE(shape->GetRadius() == radius &&
                               live.GetComponent<RigidBody3DComponent>().RuntimeBody == body,
                           "scale callback detached");
        auto removed = active.DestroyEntity(*clone);
        PRE_EDITOR_REQUIRE(
            removed && shape->GetRadius() == radius &&
                active.GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies().size() == bodies,
            "duplicate cleanup preserves live Physics");
        PRE_EDITOR_REQUIRE(owner.Meshes().Size() == meshCount &&
                               owner.Materials().Size() == materialCount,
                           "no resource publication");
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_04_PASS checks={} meshes_before_after={} materials_before_after={} physics_bodies={}",
            checks, meshCount, materialCount, bodies);
#undef PRE_EDITOR_REQUIRE
        return {};
    }
}
