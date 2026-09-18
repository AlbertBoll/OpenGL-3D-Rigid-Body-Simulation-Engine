#include "gepch.h"
#include "Renderer/RenderExtraction.h"
#include <chrono>

namespace GEngine
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        struct Timer
        {
            double& result;
            Clock::time_point start = Clock::now();
            ~Timer() { result = std::chrono::duration<double, std::micro>(Clock::now() - start).count(); }
        };
        struct Candidate
        {
            Component::MeshRendererComponent mesh;
            Component::VisibilityComponent visibility;
        };
        std::expected<std::optional<Candidate>, RenderEcsError> Read(const RenderEcs& ecs, EntityRenderId entity)
        {
            auto mesh = ecs.Get<Component::MeshRendererComponent>(entity);
            if (!mesh)
            {
                if (mesh.error() == RenderEcsError::MissingComponent) return std::nullopt;
                return std::unexpected(mesh.error());
            }
            auto visibility = ecs.Get<Component::VisibilityComponent>(entity);
            if (!visibility && visibility.error() != RenderEcsError::MissingComponent)
                return std::unexpected(visibility.error());
            return Candidate{*mesh, visibility.value_or(Component::VisibilityComponent{})};
        }
    }

    std::expected<RenderFrame, RenderExtractionError> ExtractRenderFrame(
        _Scene& scene, const RenderStateResources& resources, RenderExtractionStats& stats,
        std::span<const FrameCamera> cameras, std::span<const FrameDebugLine> debugLines)
    {
        stats = {};
        auto state = [&] {
            const Timer timer{stats.preparationMicroseconds};
            return scene.UpdateRenderState(resources);
        }();
        if (!state) return std::unexpected(RenderExtractionError{{}, state.error()});

        const Timer timer{stats.extractionMicroseconds};
        auto& ecs = scene.RenderData();
        auto frozen = ecs.BeginExtraction();
        if (!frozen) return std::unexpected(RenderExtractionError{{}, frozen.error()});
        stats.sceneEntities = state->entities.size();
        std::size_t count = 0;
        // Count and validate before allocating or transferring any packet. Both passes
        // use this frozen scene and the same prepared hierarchy order, never hash order.
        for (const auto& entry : state->entities)
        {
            auto candidate = Read(ecs, entry.entity);
            if (!candidate) return std::unexpected(RenderExtractionError{entry.entity, candidate.error()});
            if (!*candidate) continue;
            ++stats.candidates;
            if (!(**candidate).visibility.enabled) { ++stats.disabled; continue; }
            if (entry.meshError) return std::unexpected(RenderExtractionError{entry.entity, *entry.meshError});
            if (entry.materialError) return std::unexpected(RenderExtractionError{entry.entity, *entry.materialError});
            if (!entry.mesh || !entry.material)
                return std::unexpected(RenderExtractionError{entry.entity,
                    FrameError{FrameErrorCode::InvalidResources, FrameSection::Resources, count}});
            const auto submesh = (**candidate).mesh.submesh;
            if (submesh >= entry.mesh->Submeshes().size())
                return std::unexpected(RenderExtractionError{entry.entity,
                    FrameError{FrameErrorCode::InvalidSubmesh, FrameSection::Draws, submesh}});
            ++count;
        }
        auto builder = RenderFrameBuilder::Create({cameras.size(), count, debugLines.size(), count});
        if (!builder) return std::unexpected(RenderExtractionError{{}, builder.error()});
        for (const auto& camera : cameras)
            if (auto added = builder->AddCamera(camera); !added)
                return std::unexpected(RenderExtractionError{camera.entity, added.error()});
        for (auto& entry : state->entities)
        {
            auto candidate = Read(ecs, entry.entity);
            if (!candidate) return std::unexpected(RenderExtractionError{entry.entity, candidate.error()});
            if (!*candidate || !(**candidate).visibility.enabled) continue;
            const auto& intent = **candidate;
            auto index = builder->AddResources(entry.mesh, std::move(*entry.material));
            if (!index) return std::unexpected(RenderExtractionError{entry.entity, index.error()});
            FrameDrawDesc draw;
            draw.resources = *index; draw.submesh = intent.mesh.submesh;
            draw.worldTransform = entry.world; draw.entity = entry.entity;
            draw.layers = intent.visibility.layers;
            draw.castShadows = intent.mesh.castShadows; draw.receiveShadows = intent.mesh.receiveShadows;
            draw.pickable = intent.mesh.pickable;
            if (auto added = builder->AddDraw(draw); !added)
                return std::unexpected(RenderExtractionError{entry.entity, added.error()});
        }
        for (const auto& line : debugLines)
            if (auto added = builder->AddDebugLine(line); !added)
                return std::unexpected(RenderExtractionError{line.entity, added.error()});
        auto frame = std::move(*builder).Finalize();
        if (!frame) return std::unexpected(RenderExtractionError{{}, frame.error()});
        stats.draws = frame->Draws().size();
        stats.frameStorage = frame->Storage();
        return std::move(*frame);
    }
}
