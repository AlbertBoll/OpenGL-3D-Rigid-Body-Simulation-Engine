#include "gepch.h"
#include "Renderer/RenderExtraction.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <new>
#include "RenderCpuBacking.h"

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
        const Component::VisibilityComponent DefaultVisibility;
        struct Candidate
        {
            const Component::MeshRendererComponent& mesh;
            const Component::VisibilityComponent& visibility;
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
        using LightValue = std::variant<DirectionalLightData, PointLightData, SpotLightData>;
        std::expected<LightValue, FrameError> MakeLight(const EntityRenderState& entry)
        {
            const auto& light = *entry.light;
            const glm::vec3 color{light.color[0], light.color[1], light.color[2]};
            const glm::vec3 position{entry.world[3]};
            const LightShadowSettings shadows{light.castShadows};
            if (light.kind == Component::RenderLightKind::Point)
                return PointLightData{entry.entity, entry.revisions.light, position,
                    color, light.intensity, light.range, shadows};
            const glm::dvec3 axis = -glm::dvec3(entry.world[2]);
            const double length = std::hypot(axis.x, axis.y, axis.z);
            if (!std::isfinite(length) || length == 0)
                return std::unexpected(FrameError{FrameErrorCode::InvalidLightDirection,
                    light.kind == Component::RenderLightKind::Directional ? FrameSection::DirectionalLights : FrameSection::SpotLights});
            const glm::vec3 direction{axis / length};
            if (light.kind == Component::RenderLightKind::Directional)
                return DirectionalLightData{entry.entity, entry.revisions.light, direction,
                    color, light.intensity, shadows};
            return SpotLightData{entry.entity, entry.revisions.light, position, direction,
                color, light.intensity, light.range, light.innerConeRadians, light.outerConeRadians, shadows};
        }
        std::expected<void, FrameError> EmitLight(RenderFrameBuilder& builder, const EntityRenderState& entry)
        {
            auto light = MakeLight(entry);
            if (!light) return std::unexpected(light.error());
            return std::visit([&](const auto& value) { return builder.AddLight(value); }, *light);
        }
        std::expected<std::optional<Candidate>,RenderEcsError> Read(const EntityRenderState& row) noexcept
        {
            if(!row.cpu.meshIntent)return std::nullopt;
            return Candidate{*row.cpu.meshIntent,row.cpu.visibilityIntent?*row.cpu.visibilityIntent:DefaultVisibility};
        }

        struct ExtractedEntity
        {
            std::optional<FrameError> contributionError;
            std::optional<FrameError> drawError;
            std::optional<std::expected<LightValue, FrameError>> light;
            std::optional<FrameDrawDesc> draw;
        };
        struct ExtractionLane
        {
            std::unique_ptr<ExtractedEntity[]> output;
            std::size_t first{}, count{};
        };
        std::expected<void, RenderWorkError> ExtractRange(const RenderTaskRange& range, void* user) noexcept
        {
            auto& lane = static_cast<ExtractionLane*>(user)[range.lane];
            for (std::size_t i = 0; i < range.entities.size(); ++i)
            {
                if (range.StopRequested()) return std::unexpected(RenderWorkError{RenderWorkCode::Cancelled, range.first + i});
                const auto& input = range.entities[i];
                const auto& entry = input.state;
                auto& output = lane.output[i];
                if (entry.light)
                {
                    auto contributes = Contributes(*entry.light);
                    if (!contributes) output.contributionError = contributes.error();
                    else if (*contributes) output.light.emplace(MakeLight(entry));
                }
                if (!input.mesh || !input.visibility.enabled) continue;
                if (entry.meshError || entry.materialError) continue; // Owner reports retained diagnostics after join.
                if (!entry.mesh || !entry.material)
                    output.drawError = FrameError{FrameErrorCode::InvalidResources, FrameSection::Resources};
                else if (input.mesh->submesh >= entry.mesh->Submeshes().size())
                    output.drawError = FrameError{FrameErrorCode::InvalidSubmesh, FrameSection::Draws, input.mesh->submesh};
                else
                {
                    FrameDrawDesc draw;
                    draw.submesh = input.mesh->submesh; draw.worldTransform = entry.world; draw.entity = entry.entity;
                    draw.layers = input.visibility.layers; draw.castShadows = input.mesh->castShadows;
                    draw.receiveShadows = input.mesh->receiveShadows; draw.pickable = input.mesh->pickable;
                    output.draw = draw;
                }
            }
            return {};
        }
        RenderExtractionError WorkFailure(const RenderWorkError& error, EntityRenderId entity = {})
        {
            // Preserve the existing public causes at migrated preparation/merge
            // boundaries; infrastructure errors retain lane/system diagnostics.
            return std::visit([&](const auto& cause) -> RenderExtractionError {
                using T = std::decay_t<decltype(cause)>;
                if constexpr (std::same_as<T, TransformError> || std::same_as<T, RenderEcsError>
                    || std::same_as<T, FrameError> || std::same_as<T, Asset::RegistryError>
                    || std::same_as<T, MaterialBindingError>) return {entity, cause};
                else return {entity, error};
            }, error.cause);
        }
        std::expected<RenderFrame, RenderExtractionError> ExtractTasks(
            _Scene& scene, const RenderStateResources& resources, RenderExtractionStats& stats,
            std::span<const FrameCamera> cameras, std::span<const FrameDebugLine> debugLines, RenderExtractionConfig config)
        {
            auto prepared = [&] {
                const Timer timer{stats.preparationMicroseconds};
                return RenderTaskFrame::Prepare(scene, resources);
            }();
            if (!prepared) return std::unexpected(WorkFailure(prepared.error()));
            auto& task = **prepared;
            const Timer timer{stats.extractionMicroseconds};
            const auto inputs = task.Inputs();
            stats.sceneEntities = inputs.size();
            if (inputs.size() < config.parallelThreshold && config.tasks.workers > 1)
            { config.tasks.workers = 1; stats.thresholdFallback = true; }
            const auto count = task.LaneCount(config.tasks);
            std::array<ExtractionLane, RenderTaskFrame::MaxWorkers> lanes;
            std::array<RenderTaskScratch, RenderTaskFrame::MaxWorkers> scratch{};
            for (std::size_t lane = 0; lane < count; ++lane)
            {
                const auto base = inputs.size() / count, extra = inputs.size() % count;
                auto& output = lanes[lane];
                output.first = lane * base + (std::min)(lane, extra);
                output.count = base + (lane < extra);
                if (output.count > static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)()) / sizeof(ExtractedEntity))
                    return std::unexpected(RenderExtractionError{{}, RenderWorkError{RenderWorkCode::Capacity, output.count}});
                output.output.reset(new (std::nothrow) ExtractedEntity[output.count]);
                if (!output.output) return std::unexpected(RenderExtractionError{{}, RenderWorkError{RenderWorkCode::Allocation, lane}});
            }
            auto ran = task.Run(config.tasks, {scratch.data(), count}, ExtractRange, lanes.data());
            if (!ran) return std::unexpected(WorkFailure(ran.error()));
            stats.tasks = *ran;
            FrameCapacity capacity{cameras.size(), 0, debugLines.size()};
            // Preserve the serial two-pass failure order: all contribution/resource
            // checks precede camera/light/draw emission validation. No shared counters.
            for (std::size_t lane = 0; lane < count; ++lane)
                for (std::size_t i = 0; i < lanes[lane].count; ++i)
                {
                    const auto& input = inputs[lanes[lane].first + i];
                    const auto& output = lanes[lane].output[i];
                    if (input.state.light)
                    {
                        ++stats.lightCandidates;
                        if (output.contributionError)
                            return std::unexpected(RenderExtractionError{input.state.entity, *output.contributionError});
                        if (!output.light) ++stats.nonContributingLights;
                        else
                        {
                            if (capacity.directionalLights + capacity.pointLights + capacity.spotLights == MaxFrameLights)
                                return std::unexpected(RenderExtractionError{input.state.entity,
                                    FrameError{FrameErrorCode::LightLimitExceeded, FrameSection::Lights, MaxFrameLights}});
                            switch (input.state.light->kind)
                            {
                            case Component::RenderLightKind::Directional: ++capacity.directionalLights; break;
                            case Component::RenderLightKind::Point: ++capacity.pointLights; break;
                            case Component::RenderLightKind::Spot: ++capacity.spotLights; break;
                            }
                        }
                    }
                    if (!input.mesh) continue;
                    ++stats.candidates;
                    if (!input.visibility.enabled) { ++stats.disabled; continue; }
                    if (input.state.meshError) return std::unexpected(RenderExtractionError{input.state.entity, *input.state.meshError});
                    if (input.state.materialError) return std::unexpected(RenderExtractionError{input.state.entity, *input.state.materialError});
                    if (output.drawError)
                    {
                        auto error = *output.drawError;
                        if (error.code == FrameErrorCode::InvalidResources) error.element = capacity.draws;
                        return std::unexpected(RenderExtractionError{input.state.entity, error});
                    }
                    ++capacity.draws;
                }
            capacity.resources = capacity.draws;
            auto builder = RenderCpu::FrameAccess::Create(capacity,task.Revisions().light,task.CpuDomain(),task.Target(),task.FullTransformChange());
            if (!builder) return std::unexpected(RenderExtractionError{{}, builder.error()});
            for (const auto& camera : cameras)
                if (auto added = builder->AddCamera(camera); !added)
                    return std::unexpected(RenderExtractionError{camera.entity, added.error()});
            for (std::size_t lane = 0; lane < count; ++lane)
                for (std::size_t i = 0; i < lanes[lane].count; ++i)
                {
                    auto& output = lanes[lane].output[i];
                    const auto index = lanes[lane].first + i;
                    const auto entity = inputs[index].state.entity;
                    if (output.light)
                    {
                        if (!*output.light) return std::unexpected(RenderExtractionError{entity, output.light->error()});
                        auto added = std::visit([&](const auto& value) { return builder->AddLight(value); }, **output.light);
                        if (!added) return std::unexpected(RenderExtractionError{entity, added.error()});
                    }
                    if (!output.draw) continue;
                    auto transferred = task.TransferResources(*builder, index);
                    if (!transferred) return std::unexpected(WorkFailure(transferred.error(), entity));
                    output.draw->resources = *transferred;
                    if (auto added = RenderCpu::FrameAccess::AddSceneDraw(*builder,inputs[index].state,*transferred); !added)
                        return std::unexpected(RenderExtractionError{entity, added.error()});
                }
            for (const auto& line : debugLines)
                if (auto added = builder->AddDebugLine(line); !added)
                    return std::unexpected(RenderExtractionError{line.entity, added.error()});
            auto frame = std::move(*builder).Finalize();
            if (!frame) return std::unexpected(RenderExtractionError{{}, frame.error()});
            stats.draws = frame->Draws().size(); stats.directionalLights = frame->DirectionalLights().size();
            stats.pointLights = frame->PointLights().size(); stats.spotLights = frame->SpotLights().size();
            stats.frameStorage = frame->Storage();
            return std::move(*frame);
        }
    }

    std::expected<RenderFrame, RenderExtractionError> ExtractRenderFrame(
        _Scene& scene, const RenderStateResources& resources, RenderExtractionStats& stats,
        std::span<const FrameCamera> cameras, std::span<const FrameDebugLine> debugLines, RenderExtractionConfig config)
    {
        stats = {};
        if (config.tasks.workers > RenderTaskFrame::MaxWorkers)
            return std::unexpected(RenderExtractionError{{}, RenderWorkError{RenderWorkCode::InvalidWorkers, config.tasks.workers}});
        if (config.tasks.workers) return ExtractTasks(scene, resources, stats, cameras, debugLines, config);
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
            auto candidate = Read(entry);
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
        auto builder = RenderCpu::FrameAccess::Create(capacity,state->revisions.light,state->cpuDomain,state->target,state->fullTransformChange);
        if (!builder) return std::unexpected(RenderExtractionError{{}, builder.error()});
        for (const auto& camera : cameras)
            if (auto added = builder->AddCamera(camera); !added)
                return std::unexpected(RenderExtractionError{camera.entity, added.error()});
        for (auto entry : state->entities)
        {
            if (entry.light && entry.light->intensity > 0)
                if (auto added = EmitLight(*builder, entry); !added)
                    return std::unexpected(RenderExtractionError{entry.entity, added.error()});
            auto candidate = Read(entry);
            if (!candidate) return std::unexpected(RenderExtractionError{entry.entity, candidate.error()});
            if (!*candidate || !(**candidate).visibility.enabled) continue;
            const auto& intent = **candidate;
            auto index = builder->AddSharedResources(entry.mesh, entry.material);
            if (!index) return std::unexpected(RenderExtractionError{entry.entity, index.error()});
            RW_COUNT(resourceRows,1);
            if (auto added = RenderCpu::FrameAccess::AddSceneDraw(*builder,entry,*index); !added)
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
