#include "gepch.h"
#include "Renderer/RenderExtraction.h"
#include <algorithm>
#include <chrono>
#include <cmath>

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
        std::expected<bool, FrameError> Contributes(const Component::RenderLightComponent& light)
        {
            using Kind = Component::RenderLightKind;
            if ((light.kind != Kind::Directional && light.kind != Kind::Point && light.kind != Kind::Spot)
                || !std::isfinite(light.intensity) || light.intensity < 0
                || !std::all_of(light.color.begin(), light.color.end(), [](float v) { return std::isfinite(v) && v >= 0; }))
                return std::unexpected(FrameError{FrameErrorCode::InvalidLight, FrameSection::Lights});
            return light.intensity > 0;
        }
        std::expected<void, FrameError> EmitLight(RenderFrameBuilder& builder, const EntityRenderState& entry)
        {
            const auto& light = *entry.light;
            const glm::vec3 color{light.color[0], light.color[1], light.color[2]};
            const glm::vec3 position{entry.world[3]};
            const LightShadowSettings shadows{light.castShadows};
            if (light.kind == Component::RenderLightKind::Point)
                return builder.AddLight(PointLightData{entry.entity, entry.revisions.light, position,
                    color, light.intensity, light.range, shadows});
            const glm::dvec3 axis = -glm::dvec3(entry.world[2]);
            const double length = std::hypot(axis.x, axis.y, axis.z);
            if (!std::isfinite(length) || length == 0)
                return std::unexpected(FrameError{FrameErrorCode::InvalidLightDirection,
                    light.kind == Component::RenderLightKind::Directional ? FrameSection::DirectionalLights : FrameSection::SpotLights});
            const glm::vec3 direction{axis / length};
            if (light.kind == Component::RenderLightKind::Directional)
                return builder.AddLight(DirectionalLightData{entry.entity, entry.revisions.light, direction,
                    color, light.intensity, shadows});
            return builder.AddLight(SpotLightData{entry.entity, entry.revisions.light, position, direction,
                color, light.intensity, light.range, light.innerConeRadians, light.outerConeRadians, shadows});
        }
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
        FrameCapacity capacity{cameras.size(), 0, debugLines.size()};
        // Count and validate before allocating or transferring any packet. Both passes
        // use this frozen scene and the same prepared hierarchy order, never hash order.
        for (const auto& entry : state->entities)
        {
            if (entry.light)
            {
                ++stats.lightCandidates;
                auto contributes = Contributes(*entry.light);
                if (!contributes) return std::unexpected(RenderExtractionError{entry.entity, contributes.error()});
                if (!*contributes) ++stats.nonContributingLights;
                else
                {
                    if (capacity.directionalLights + capacity.pointLights + capacity.spotLights == MaxFrameLights)
                        return std::unexpected(RenderExtractionError{entry.entity,
                            FrameError{FrameErrorCode::LightLimitExceeded, FrameSection::Lights, MaxFrameLights}});
                    switch (entry.light->kind)
                    {
                    case Component::RenderLightKind::Directional: ++capacity.directionalLights; break;
                    case Component::RenderLightKind::Point: ++capacity.pointLights; break;
                    case Component::RenderLightKind::Spot: ++capacity.spotLights; break;
                    }
                }
            }
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
        capacity.draws = capacity.resources = count;
        auto builder = RenderFrameBuilder::Create(capacity, state->revisions.light);
        if (!builder) return std::unexpected(RenderExtractionError{{}, builder.error()});
        for (const auto& camera : cameras)
            if (auto added = builder->AddCamera(camera); !added)
                return std::unexpected(RenderExtractionError{camera.entity, added.error()});
        for (auto& entry : state->entities)
        {
            if (entry.light && entry.light->intensity > 0)
                if (auto added = EmitLight(*builder, entry); !added)
                    return std::unexpected(RenderExtractionError{entry.entity, added.error()});
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
        stats.directionalLights = frame->DirectionalLights().size();
        stats.pointLights = frame->PointLights().size();
        stats.spotLights = frame->SpotLights().size();
        stats.frameStorage = frame->Storage();
        return std::move(*frame);
    }
}
