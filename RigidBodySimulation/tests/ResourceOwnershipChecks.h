#pragma once

#include "Mesh/MeshAsset.h"
#include "Assets/AssetRegistry.h"
#include <array>
#include <type_traits>

namespace PreEditorValidation
{
    // Invoked before BaseApp creates its platform/context. The CPU-only compile
    // mode below also proves this provider needs no GpuMesh or backend headers.
    inline std::expected<std::size_t, const char*> CheckCpuMeshMetadata()
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Asset;
        struct Position
        {
            float xyz[3];
        };
        const std::array<Position, 3> vertices{{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}}};
        const std::array<VertexAttribute, 1> layout{
            {{VertexSemantic::Position, 0, VertexScalarFormat::Float32, 3,
              VertexInterpretation::Floating, 0}}};
        const std::array<SubmeshRange, 2> ranges{{{0, 3, 0}, {0, 3, 0}}};
        auto make = [&](std::size_t count)
        {
            auto source = MeshSourceData::FromVertices<Position>(vertices, layout);
            source.submeshes = std::span(ranges).first(count);
            return MeshAsset::Create(source);
        };
        std::size_t checks = 0;
#define CPU_REQUIRE(condition, name)                                                               \
    ++checks;                                                                                      \
    if (!(condition))                                                                              \
    return std::unexpected(name)
        AssetPublication publication;
        AssetRegistry<MeshHandle, MeshAsset> registry(publication, {1});
        AssetRegistry<MeshHandle, MeshAsset> foreign(publication);
        MeshHandle handle;
        {
            auto source = make(1);
            CPU_REQUIRE(source && source->AuthoringMetadata().submeshCount == 1, "CPU source");
            auto token = publication.BeginPublication();
            auto created = registry.Create(token, std::move(*source));
            CPU_REQUIRE(created, "CPU metadata create");
            handle = *created;
            auto extra = make(1);
            CPU_REQUIRE(extra, "capacity fixture");
            auto full = registry.Create(token, std::move(*extra));
            CPU_REQUIRE(!full && full.error() == RegistryError::SlotsExhausted,
                        "capacity rollback");
        }
        CPU_REQUIRE(registry.ReadMetadata(handle)->submeshCount == 1, "CPU lookup without frame");
        CPU_REQUIRE(!foreign.ReadMetadata(handle), "foreign registry rejected");
        auto stale = handle;
        ++stale.generation;
        CPU_REQUIRE(!registry.ReadMetadata(stale), "stale generation rejected");
        AssetRegistry<MeshHandle, MeshAsset>::Lease retained;
        {
            auto access = publication.BeginFrame();
            auto acquired = registry.Acquire(access, handle);
            CPU_REQUIRE(acquired, "CPU retained version");
            retained = *acquired;
            CPU_REQUIRE(registry.ReadMetadata(handle)->submeshCount == 1,
                        "CPU metadata does not open nested frame");
        }
        {
            auto source = make(2);
            CPU_REQUIRE(source, "replacement source");
            auto token = publication.BeginPublication();
            CPU_REQUIRE(registry.Replace(token, handle, std::move(*source)), "CPU replacement");
        }
        CPU_REQUIRE(registry.ReadMetadata(handle)->submeshCount == 2 &&
                        retained.ReadMetadata().submeshCount == 1 && retained.Revision() == 1,
                    "metadata paired with retained and current versions");
        {
            auto token = publication.BeginPublication();
            CPU_REQUIRE(registry.Destroy(token, handle), "CPU destroy");
            CPU_REQUIRE(registry.Collect(token) == 1, "only unretained version collected");
        }
        CPU_REQUIRE(!registry.ReadMetadata(handle), "destroyed metadata rejected");
        {
            auto source = make(1);
            CPU_REQUIRE(source, "reuse fixture");
            auto token = publication.BeginPublication();
            auto reused = registry.Create(token, std::move(*source));
            CPU_REQUIRE(reused && reused->index == handle.index &&
                            reused->generation != handle.generation,
                        "slot generation reused");
            CPU_REQUIRE(!registry.ReadMetadata(handle), "old identity stays invalid");
        }
        retained = {};
        {
            auto token = publication.BeginPublication();
            CPU_REQUIRE(registry.Close(token), "CPU registry drains");
        }
#undef CPU_REQUIRE
        return checks;
    }
}

#ifndef PRE_EDITOR_CPU_METADATA_ONLY
#include "Renderer/SceneRenderResources.h"
#include "Renderer/RenderExtraction.h"
#include "Scene/_Entity.h"
#include "Core/Log.h"

namespace PreEditorValidation
{
    inline std::expected<void, ::GEngine::PlatformError>
    CheckResourceOwnership(::GEngine::SceneRenderResources& owner, ::GEngine::Asset::MeshHandle box,
                           ::GEngine::Asset::MeshHandle sphere,
                           ::GEngine::Asset::MaterialInstanceHandle boxMaterial,
                           ::GEngine::Asset::MaterialInstanceHandle sphereMaterial)
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Asset;
        static_assert(std::is_trivially_copyable_v<MeshAuthoringMetadata>);
        static_assert(!std::is_copy_constructible_v<GpuMesh>);
        std::size_t checks = 0;
        auto require = [&](bool success, const char* name)
        {
            ++checks;
            if (!success)
                Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_05_FAIL {}", name);
            return success;
        };
#define RESOURCE_REQUIRE(condition, name)                                                          \
    if (!require(bool(condition), name))                                                           \
    return std::unexpected(                                                                        \
        PlatformError{PlatformErrorCode::Initialization, "Phase 05 resource ownership", name})
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_05_BEGIN");
        const auto meshCount = owner.Meshes().Size();
        const auto materialCount = owner.Materials().Size();
        auto metadata = owner.MeshMetadata(box);
        RESOURCE_REQUIRE(metadata && metadata->submeshCount > 0, "normal CPU mesh metadata");
        auto staleMesh = box;
        ++staleMesh.generation;
        RESOURCE_REQUIRE(!owner.MeshMetadata(staleMesh), "stale mesh metadata");
        auto wrongMesh = box;
        wrongMesh.registry = boxMaterial.registry;
        RESOURCE_REQUIRE(!owner.MeshMetadata(wrongMesh), "wrong-domain mesh metadata");
        auto scene = CreateRefPtr<_Scene>();
        auto created = scene->CreateEntity("phase05-resource-check");
        RESOURCE_REQUIRE(created, "fixture entity");
        auto entity = *created;
        Component::MeshRendererComponent intent{box, boxMaterial};
        auto assigned = owner.AssignRenderable(entity, intent);
        RESOURCE_REQUIRE(assigned && assigned->changed, "semantic assignment");
        auto unchanged = owner.AssignRenderable(entity, intent);
        RESOURCE_REQUIRE(unchanged && !unchanged->changed &&
                             unchanged->revision == assigned->revision,
                         "assignment no-op");
        auto invalid = intent;
        invalid.submesh = static_cast<std::uint32_t>(metadata->submeshCount);
        auto badRange = owner.AssignRenderable(entity, invalid);
        RESOURCE_REQUIRE(!badRange && std::get<SceneAssignmentError>(badRange.error().cause) ==
                                          SceneAssignmentError::InvalidSubmesh,
                         "CPU submesh range rejection");
        invalid = intent;
        invalid.mesh = staleMesh;
        RESOURCE_REQUIRE(!owner.AssignRenderable(entity, invalid), "stale assignment rejected");
        invalid = intent;
        ++invalid.material.generation;
        RESOURCE_REQUIRE(!owner.AssignRenderable(entity, invalid), "stale material rejected");
        RESOURCE_REQUIRE(!owner.AssignRenderable({}, intent), "invalid entity rejected");
        RESOURCE_REQUIRE(entity.GetComponent<Component::MeshRendererComponent>() == intent &&
                             scene->GetRenderAssignmentRevision() == assigned->revision,
                         "failed assignment preserves entity and revision");
        auto textureValue = [](const MaterialInstance& material,
                               std::string_view name) -> std::optional<MaterialTextureValue>
        {
            const auto slots = material.Declaration()->Textures();
            for (std::size_t i = 0; i < slots.size(); ++i)
                if (slots[i].declaration.name == name)
                    return material.Textures()[i];
            return {};
        };
        MaterialTextureValue original, replacement;
        MaterialInstanceView oldMaterial;
        std::optional<RenderFrame> oldFrame;
        const GpuMesh* oldMesh = nullptr;
        {
            auto access = owner.Publication().BeginFrame(); // Deliberate diagnostic compatibility.
            auto before = owner.Materials().Acquire(access, boxMaterial);
            auto alternate = owner.Materials().Acquire(access, sphereMaterial);
            RESOURCE_REQUIRE(before && alternate, "material fixture versions");
            oldMaterial = *before;
            auto a = textureValue(**before, "albedoMap");
            auto b = textureValue(**alternate, "albedoMap");
            RESOURCE_REQUIRE(a && b && *a != *b, "distinct existing texture fixtures");
            original = *a;
            replacement = *b;
            RenderExtractionStats stats;
            auto frame = ExtractRenderFrame(*scene, owner.ForFrame(access), stats);
            RESOURCE_REQUIRE(frame && frame->Draws().size() == 1, "old frame extraction");
            oldMesh = frame->Resources()[0].Mesh().Get();
            oldFrame.emplace(std::move(*frame));
            RESOURCE_REQUIRE(owner.MeshMetadata(box) == metadata, "metadata during renderer read");
            auto blocked = owner.SetMaterialTexture(boxMaterial, "albedoMap", replacement);
            RESOURCE_REQUIRE(!blocked && std::get<SceneResourceCode>(blocked.error().cause) ==
                                             SceneResourceCode::PublicationBusy,
                             "publication exclusion");
            RESOURCE_REQUIRE(!owner.AssignRenderable(entity, intent), "assignment excludes frame");
            RESOURCE_REQUIRE(!owner.PublishShape("Box"), "shape publication excludes frame");
            RESOURCE_REQUIRE(!owner.PublishMaterial({}), "material publication excludes frame");
        }
        const auto revision = oldMaterial.Revision();
        auto noChange = owner.SetMaterialTexture(boxMaterial, "albedoMap", original);
        RESOURCE_REQUIRE(noChange && !*noChange, "texture no-op does not publish");
        auto unknown = owner.SetMaterialTexture(boxMaterial, "missing-slot", replacement);
        RESOURCE_REQUIRE(!unknown && std::get<MaterialInstanceError>(unknown.error().cause).code ==
                                         MaterialInstanceCode::UnknownTexture,
                         "unknown slot rollback");
        auto invalidTexture = replacement;
        ++invalidTexture.texture.generation;
        RESOURCE_REQUIRE(!owner.SetMaterialTexture(boxMaterial, "albedoMap", invalidTexture),
                         "stale texture rollback");
        auto invalidSampler = replacement;
        invalidSampler.sampler.registry = replacement.texture.registry;
        RESOURCE_REQUIRE(!owner.SetMaterialTexture(boxMaterial, "albedoMap", invalidSampler),
                         "wrong sampler domain rollback");
        auto invalidMaterial = boxMaterial;
        ++invalidMaterial.generation;
        RESOURCE_REQUIRE(!owner.SetMaterialTexture(invalidMaterial, "albedoMap", replacement),
                         "stale material rollback");
        {
            auto access = owner.Publication().BeginFrame();
            auto current = owner.Materials().Acquire(access, boxMaterial);
            RESOURCE_REQUIRE(current && current->Revision() == revision &&
                                 current->Get() == oldMaterial.Get(),
                             "failed edits preserve version");
        }
        auto changed = owner.SetMaterialTexture(boxMaterial, "albedoMap", replacement);
        RESOURCE_REQUIRE(changed && *changed, "semantic texture replacement");
        {
            auto access = owner.Publication().BeginFrame();
            RenderExtractionStats stats;
            auto frame = ExtractRenderFrame(*scene, owner.ForFrame(access), stats);
            RESOURCE_REQUIRE(frame && frame->Draws().size() == 1, "replacement frame extraction");
            const auto& material = frame->Resources()[0].Material();
            RESOURCE_REQUIRE(material.PublicationRevision() == revision + 1 &&
                                 textureValue(*material.Source(), "albedoMap") == replacement,
                             "new frame sees replacement version");
            RESOURCE_REQUIRE(oldFrame->Resources()[0].Material().PublicationRevision() ==
                                     revision &&
                                 textureValue(*oldMaterial, "albedoMap") == original &&
                                 oldFrame->Resources()[0].Mesh().Get() == oldMesh &&
                                 frame->Resources()[0].Mesh().Get() == oldMesh,
                             "old frame and shared heavy mesh unchanged");
        }
        RESOURCE_REQUIRE(owner.SetMaterialTexture(boxMaterial, "albedoMap", original),
                         "restore fixture material through semantic operation");
        oldFrame.reset();
        oldMaterial = {};
        scene.reset();
        {
            auto publication = owner.Publication().BeginPublication();
            owner.Materials().Collect(publication);
        }
        RESOURCE_REQUIRE(owner.Meshes().Size() == meshCount &&
                             owner.Materials().Size() == materialCount &&
                             owner.MeshMetadata(sphere),
                         "no extra mesh/material owners");
        RESOURCE_REQUIRE(owner.Publication().CanPublish(), "safe point restored before rendering");
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_05_PASS checks={} meshes={} materials={}",
                                   checks, meshCount, materialCount);
#undef RESOURCE_REQUIRE
        return {};
    }
}
#endif
