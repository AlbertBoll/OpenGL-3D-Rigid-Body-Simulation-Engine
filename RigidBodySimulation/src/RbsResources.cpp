#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Scene/_Scene.h"
#include <format>

#include "Managers/AssetsManager.h"
#include <utility>

using namespace ::GEngine;
using namespace ::GEngine::Asset;
using namespace ::GEngine::Component;
using namespace ::GEngine::Manager;
using namespace ::GEngine::Math;
using namespace ::GEngine::Camera;

ScheduleResult RigidBodySimulationApp::ApplyAsyncTextureEdit(SceneRenderResources& resources,
                                                             AsyncTextureLoader& loader,
                                                             MaterialHandle material)
{
    auto& edit = m_AsyncTextureGallery;
    if (std::exchange(edit.request, false))
    {
        if (edit.pending)
            (void)loader.Cancel(*edit.pending);
        edit.pending.reset(); // Late completion of an abandoned request cannot assign a material.
        auto requested = loader.Request(edit.source);
        if (!requested)
        {
            edit.state = AsyncAssetState::Failed;
            edit.message = DescribeAsyncTextureError(requested.error());
        }
        else
        {
            edit.pending = *requested;
            edit.state = AsyncAssetState::Requested;
            edit.message = "Loading; current material retained.";
        }
    }
    if (std::exchange(edit.cancel, false) && edit.pending)
    {
        auto cancelled = loader.Cancel(*edit.pending);
        if (!cancelled)
            edit.message = DescribeAsyncTextureError(AsyncTextureError{cancelled.error()});
    }
    if (!edit.pending)
        return {};
    auto status = loader.Status(*edit.pending);
    if (!status)
        return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources, status.error()});
    edit.state = status->state;
    if (status->state == AsyncAssetState::Failed || status->state == AsyncAssetState::Cancelled)
    {
        edit.message =
            status->error ? DescribeAsyncTextureError(*status->error) : "Request cancelled.";
        edit.pending.reset();
        return {};
    }
    if (status->state != AsyncAssetState::Ready)
        return {};
    const auto fail = [](const auto& cause) -> ScheduleResult
    {
        return std::unexpected(
            ScheduleError{FrameStage::UpdateFrameResources,
                          SceneResourceError{"async material replacement", cause}});
    };
    auto view = AssetsManager::ResolveTexture(status->image);
    if (!view)
        return fail(view.error());
    auto sampled = AssetsManager::SampleTexture(*view);
    if (!sampled)
        return std::visit(fail, sampled.error().cause);
    auto assigned = resources.SetMaterialTexture(
        material, MaterialTextureSemantic::BaseColor,
        MaterialTextureValue{sampled->TextureIdentity(), sampled->SamplerIdentity()});
    if (!assigned)
        return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources, assigned.error()});
    edit.message =
        std::format("Applied {}x{}; shared material updated. Independent clone preserved.",
                    status->description.width, status->description.height);
    edit.pending.reset();
    Log::GetCoreLogger()->info("Async material replacement: {}", edit.message);
    return {};
}

void RigidBodySimulationApp::ApplyShaderReloadRequest(SceneRenderResources& resources)
{
    auto& gallery = m_ShaderReloadGallery;
    const int request = std::exchange(gallery.request, 0);
    if (!request)
        return;
    if (request == 5)
    {
        auto prepared = resources.PrepareShaderReload(gallery.material, *gallery.edited);
        if (prepared)
        {
            gallery.ticket = *prepared;
            gallery.message = "Prepared; live materials are unchanged.";
        }
        else
            gallery.message = DescribeSceneResourceError(prepared.error());
    }
    else if (request == 7)
    {
        if (!gallery.ticket)
        {
            gallery.message = "No prepared reload.";
            return;
        }
        auto cancelled = resources.CancelShaderReload(*gallery.ticket);
        gallery.message = cancelled ? "Cancelled; live materials are unchanged."
                                    : DescribeSceneResourceError(cancelled.error());
        if (cancelled)
            gallery.ticket.reset();
    }
    else
    {
        if (request == 6 && !gallery.ticket)
        {
            gallery.message = "No prepared reload.";
            return;
        }
        auto result = request == 6 ? resources.CommitShaderReload(*gallery.ticket)
                                   : resources.ReloadShader(gallery.material,
                                                            request == 1   ? *gallery.edited
                                                            : request == 2 ? *gallery.original
                                                            : request == 3 ? *gallery.invalid
                                                                           : *gallery.incompatible);
        if (request == 6)
            gallery.ticket.reset();
        gallery.message = result
                              ? std::format("{}; {} materials. Independent values preserved.",
                                            result->changed ? "Reload applied" : "Already current",
                                            result->materials)
                              : DescribeSceneResourceError(result.error());
    }
    Log::GetCoreLogger()->info("Custom shader reload: {}", gallery.message);
}

std::expected<void, SceneError> RigidBodySimulationApp::UpdateImportedMesh()
{
    if (m_Launch.scene != Rbs::RbsScenePreset::GeometryGallery || m_BarrelSettled)
        return {};
    auto failed = [&](const AsyncMeshError& error)
    {
        Log::GetCoreLogger()->warn("Barrel mesh: {}", DescribeAsyncMeshError(error));
        m_BarrelSettled = true;
    };
    if (!m_BarrelRequest)
    {
        auto request = m_MeshLoads->Request("barrel.obj");
        if (!request)
        {
            failed(request.error());
            return {};
        }
        m_BarrelRequest = *request;
        return {};
    }
    auto status = m_MeshLoads->Status(m_BarrelRequest);
    if (!status)
    {
        failed(status.error());
        return {};
    }
    if (status->state == AsyncAssetState::Failed || status->state == AsyncAssetState::Cancelled)
    {
        if (status->error)
            failed(*status->error);
        m_BarrelSettled = true;
        return {};
    }
    if (status->state != AsyncAssetState::Ready)
        return {};
    auto metadata = m_SceneResources->MeshMetadata(status->mesh);
    if (!metadata)
    {
        failed(UploadError{UploadCode::UploadFailed, {}, metadata.error()});
        return {};
    }
    const auto submeshes = metadata->submeshCount;
    // This callback runs after upload and before frame extraction. No physics
    // component is authored; imported submeshes share the existing lit material.
    for (std::size_t part = 0; part < submeshes; ++part)
    {
        auto createdSceneEntity17 =
            m_ActiveScene->CreateEntity(std::format("Imported barrel {}", part));
        if (!createdSceneEntity17)
            return std::unexpected(createdSceneEntity17.error());
        auto entity = *createdSceneEntity17;
        {
            auto assigned = m_SceneResources->AssignRenderable(
                entity, {status->mesh, m_AsyncBoxMaterial, static_cast<std::uint32_t>(part)});
            if (!assigned)
                return std::unexpected(SceneError{SceneErrorCode::InvalidIdentity,
                                                  "imported mesh assignment",
                                                  "Imported renderable assignment failed"});
        }
        entity.AddOrReplaceComponent<Transform3DComponent>(Vec3f{-6.f, 0.f, 0.f});
    }
    m_BarrelSettled = true;
    Log::GetCoreLogger()->info("Async barrel mesh published: {} submeshes", submeshes);
    return {};
}

ScheduleResult RigidBodySimulationApp::PrepareFrameResources(RenderContext& context)
{
    auto loads = AssetsManager::AsyncTextures();
    if (!loads)
        return std::visit(
            [](const auto& cause) -> std::unexpected<ScheduleError>
            {
                if constexpr (std::same_as<std::decay_t<decltype(cause)>, UploadError>)
                    return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources, cause});
                else
                    return std::unexpected(
                        ScheduleError{FrameStage::UpdateFrameResources,
                                      SceneResourceError{"async wood texture", cause}});
            },
            loads.error());
    m_TextureLoads = *loads;
    // The optional mesh import starts after the texture bootstrap.
    if (m_Launch.scene == Rbs::RbsScenePreset::GeometryGallery && m_LoadBarrel && m_WoodSettled &&
        !m_MeshLoads && !m_BarrelSettled)
    {
        auto meshLoads = m_SceneResources->CreateMeshLoader(m_MeshRoot);
        if (meshLoads)
            m_MeshLoads = std::move(*meshLoads);
        else
        {
            Log::GetCoreLogger()->warn("Barrel mesh: {}",
                                       DescribeAsyncMeshError(meshLoads.error()));
            m_BarrelSettled = true;
        }
    }
    // One existing upload safe point per frame, bounded fair selection.
    // A retained mesh loader must not starve later texture edits.
    m_AsyncTextureGallery.meshTurn = !m_AsyncTextureGallery.meshTurn;
    context.uploads = m_MeshLoads && m_AsyncTextureGallery.meshTurn ? &m_MeshLoads->Queue()
                                                                    : &m_TextureLoads->Queue();
    return {};
}

ScheduleResult RigidBodySimulationApp::UpdateFrameResources()
{
    ApplyShaderReloadRequest(*m_SceneResources);
    if (m_MeshLoads)
    {
        if (auto updated = UpdateImportedMesh(); !updated)
            return std::unexpected(
                ScheduleError{FrameStage::UpdateFrameResources, updated.error()});
    }
    if (m_WoodSettled)
    {
        if (m_AsyncTextureGallery.clone)
            return ApplyAsyncTextureEdit(*m_SceneResources, *m_TextureLoads, m_AsyncBoxMaterial);
        return {};
    }
    auto failure = [](const auto& cause) -> ScheduleResult
    {
        if constexpr (std::same_as<std::decay_t<decltype(cause)>, UploadError>)
            return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources, cause});
        else
            return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources,
                                                 SceneResourceError{"async wood texture", cause}});
    };
    if (!m_WoodRequest)
    {
        auto requested = m_TextureLoads->Request("Sphere/wood_diffuse");
        if (!requested)
            return std::visit(failure, requested.error());
        m_WoodRequest = *requested;
        return {};
    }
    auto status = m_TextureLoads->Status(m_WoodRequest);
    if (!status)
        return failure(status.error());
    if (status->state == AsyncAssetState::Failed || status->state == AsyncAssetState::Cancelled)
    {
        if (status->error)
            Log::GetCoreLogger()->warn("{}; retaining wood fallback",
                                       DescribeAsyncTextureError(*status->error));
        m_WoodSettled = true;
        return {};
    }
    if (status->state != AsyncAssetState::Ready)
        return {};
    auto view = AssetsManager::ResolveTexture(status->image);
    if (!view)
        return failure(view.error());
    auto sampled = AssetsManager::SampleTexture(*view);
    if (!sampled)
        return std::visit(failure, sampled.error().cause);
    auto changed = m_SceneResources->SetMaterialTexture(
        m_AsyncBoxMaterial, MaterialTextureSemantic::BaseColor,
        MaterialTextureValue{sampled->TextureIdentity(), sampled->SamplerIdentity()});
    if (!changed)
        return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources, changed.error()});
    m_WoodSettled = true;
    Log::GetCoreLogger()->info("Async wood texture published: {}x{}", status->description.width,
                               status->description.height);
    return {};
}
