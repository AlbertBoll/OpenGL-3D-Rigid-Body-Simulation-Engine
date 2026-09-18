#include "Renderer/RenderExtraction.h"
#include <type_traits>

using namespace GEngine;
using namespace GEngine::Asset;
using namespace GEngine::Component;
static_assert(std::same_as<decltype(ExtractRenderFrame(std::declval<_Scene&>(),
    std::declval<const RenderStateResources&>(), std::declval<RenderExtractionStats&>())),
    std::expected<RenderFrame, RenderExtractionError>>);
static_assert(!std::is_copy_constructible_v<RenderFrame>);
static_assert(!std::is_copy_constructible_v<GpuMesh>);
static_assert(std::is_trivially_copyable_v<DirectionalLightData> && std::is_trivially_copyable_v<PointLightData>
    && std::is_trivially_copyable_v<SpotLightData>);
static_assert(std::same_as<decltype(std::declval<const RenderFrame&>().SpotLights()), std::span<const SpotLightData>>);
static_assert(std::same_as<decltype(std::declval<RenderFrame&>().DirectionalLights()), std::span<const DirectionalLightData>>);
static_assert(std::same_as<decltype(std::declval<RenderFrame&>().PointLights()), std::span<const PointLightData>>);

// Minimal serial submission fixture: only a frame enters, so authoritative ECS
// lookup is impossible. This copies typed upload values, without a lighting equation.
struct LightUpload
{
    std::size_t directional{}, point{}, spot{};
    glm::vec3 directionalColor{}, pointPosition{}, spotDirection{};
    float pointRange{}, spotInner{}, spotOuter{}, intensity{};
    bool shadow{};
};
LightUpload ConsumeLights(const RenderFrame& frame)
{
    LightUpload packet{frame.DirectionalLights().size(), frame.PointLights().size(), frame.SpotLights().size()};
    for (const auto& light : frame.DirectionalLights()) packet.directionalColor = light.color;
    for (const auto& light : frame.PointLights()) { packet.pointPosition = light.position; packet.pointRange = light.range; }
    for (const auto& light : frame.SpotLights())
    {
        packet.spotDirection = light.direction; packet.spotInner = light.innerConeRadians;
        packet.spotOuter = light.outerConeRadians; packet.intensity = light.intensity; packet.shadow = light.shadows.castShadows;
    }
    return packet;
}

#ifndef EXTRACTION_SCHEMA_ONLY
#include "Scene/_Entity.h"
#include "Core/GLContextThread.h"
#include <glad/glad.h>
#include <sdl2/SDL.h>
#include <cstdlib>
#include <limits>
#include <new>
#include <numbers>
#include <print>
#include <thread>

namespace Allocations
{
    bool active{};
    std::size_t count{}, bytes{};
    int failArray = -1, arrays{};
}
void* operator new(std::size_t bytes)
{
    if (Allocations::active) { ++Allocations::count; Allocations::bytes += bytes; }
    if (auto* p = std::malloc(bytes ? bytes : 1)) return p;
    std::abort(); // Test allocator only; fault injection below targets the frame's typed boundary.
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept
{
    if (Allocations::active && Allocations::arrays++ == Allocations::failArray) return nullptr;
    return ::operator new(bytes);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }

namespace
{
    int checks{};
    const char* lastCheck = "startup";
    template<class T> void Check(const T& value, const char* message)
    {
        lastCheck = message; ++checks;
        if (!static_cast<bool>(value)) { std::println(stderr, "[FAIL] {}", message); std::exit(1); }
    }
    struct Vertex { float x,y,z; };
    MeshAsset MeshSource(float scale = 1)
    {
        const Vertex vertices[]{{0,0,0},{scale,0,0},{0,scale,0},{0,0,scale},{scale,0,scale},{0,scale,scale}};
        const VertexAttribute attributes[]{{VertexSemantic::Position,0,VertexScalarFormat::Float32,3,VertexInterpretation::Floating,0}};
        const SubmeshRange ranges[]{{0,3,0},{3,3,0}};
        auto source = MeshSourceData::FromVertices<Vertex>(vertices, attributes); source.submeshes = ranges;
        return MeshAsset::Create(source).value();
    }
    ShaderProgram Program()
    {
        const ShaderSource sources[]{{VERTEX,"#version 460 core\nvoid main(){gl_Position=vec4(0,0,0,1);}","extraction vertex"},
            {FRAGMENT,"#version 460 core\nlayout(location=0) out vec4 color;void main(){color=vec4(1);}","extraction fragment"}};
        return ShaderProgram::Create({sources}).value();
    }
    TextureResource Image()
    {
        TextureDesc desc; desc.width = desc.height = 1; desc.mips = TextureMipIntent::None;
        const std::array<std::byte,4> pixels{std::byte{20},std::byte{40},std::byte{60},std::byte{255}};
        return TextureResource::Create(desc, {pixels}).value();
    }
    struct Fixture
    {
        AssetPublication publication;
        ShaderProgramRegistry programs{publication}; TextureRegistry textures{publication}; SamplerRegistry samplers{publication};
        PipelineRegistry pipelines{publication}; MaterialTemplateRegistry templates{publication};
        MaterialInstanceRegistry materials{publication}; MeshRegistry meshes{publication};
        _Scene scene;
        ShaderProgramHandle program; TextureHandle texture; SamplerHandle sampler;
        MeshHandle mesh; MaterialInstanceHandle material; MaterialTemplateView declaration;
        Fixture()
        {
            PipelineHandle pipeline;
            {
                auto p = publication.BeginPublication();
                program = programs.Create(p, Program()).value(); texture = textures.Create(p, Image()).value();
                sampler = samplers.Create(p, GpuSampler::Create({}).value()).value();
                PipelineDesc desc; desc.program = program; desc.programRevision = 1; desc.alpha = AlphaMode::Transparent;
                pipeline = pipelines.Create(p, PipelineState::Create(desc).value()).value();
                mesh = PublishMesh(meshes, p, MeshSource()).value();
            }
            PipelineView pipelineView;
            { auto f = publication.BeginFrame(); pipelineView = pipelines.Acquire(f, pipeline).value(); }
            const MaterialParameterDecl parameters[]{{"value",MaterialParameterType::Float,.5f}};
            const MaterialTextureSlotDecl slots[]{{"image",true,MaterialTextureValue{texture,sampler}}};
            auto definition = MaterialTemplate::Create({pipelineView, parameters, slots, false, false}).value();
            MaterialTemplateHandle handle;
            { auto p = publication.BeginPublication(); handle = templates.Create(p, std::move(definition)).value(); }
            { auto f = publication.BeginFrame(); declaration = templates.Acquire(f, handle).value(); }
            { auto p = publication.BeginPublication(); material = materials.Create(p, MaterialInstance::Create(declaration).value()).value(); }
        }
        std::pair<_Entity, EntityRenderId> Entity(std::uint64_t uuid, std::uint32_t submesh = 0)
        {
            auto entity = scene.CreateEntityWithUUID(UUID(uuid));
            auto id = scene.RenderData().Identify(entity).value();
            Check(scene.RenderData().Add(id, MeshRendererComponent{mesh, material, submesh}), "Author mesh intent");
            return {entity, id};
        }
        std::pair<_Entity, EntityRenderId> Light(std::uint64_t uuid, RenderLightKind kind)
        {
            auto entity = scene.CreateEntityWithUUID(UUID(uuid));
            auto id = scene.RenderData().Identify(entity).value();
            RenderLightComponent intent; intent.kind = kind;
            Check(scene.RenderData().Add(id, intent), "Author typed light intent without mesh");
            return {entity, id};
        }
        std::expected<RenderFrame, RenderExtractionError> Extract(RenderExtractionStats& stats,
            std::span<const FrameCamera> cameras = {}, std::span<const FrameDebugLine> lines = {})
        {
            auto access = publication.BeginFrame();
            return ExtractRenderFrame(scene, {access, meshes, materials, {programs,textures,samplers}, {}}, stats, cameras, lines);
        }
    };
    void Basic(SDL_Window* window, SDL_GLContext context)
    {
        Fixture f; RenderExtractionStats stats;
        Check(SDL_GL_MakeCurrent(window, nullptr) == 0, "Detach context before extraction");
        auto empty = f.Extract(stats);
        Check(empty && empty->Draws().empty() && stats.candidates == 0 && stats.frameStorage.storageAllocations == 0,
            "Empty scene has no frame allocations");
        auto [entity, id] = f.Entity(30);
        entity.Transform().Translation = {2,3,4};
        FrameCamera camera; camera.entity = id; camera.viewportWidth = 320; camera.viewportHeight = 200;
        FrameDebugLine line; line.entity = id; line.end = {1,2,3};
        auto one = f.Extract(stats, {&camera,1}, {&line,1}); Check(one, "One entity extraction");
        Check(one->Draws().size() == 1 && one->Draws()[0].entity == id && one->Draws()[0].worldTransform[3] == glm::vec4(2,3,4,1),
            "Frame-local transform and full entity identity");
        Check(one->Draws()[0].layers == UINT32_MAX && one->Draws()[0].pickable && one->Draws()[0].receiveShadows
            && !one->Draws()[0].castShadows, "Default flags and template shadow intersection");
        Check(one->Cameras()[0].viewportWidth == 320 && one->DebugLines()[0].end.z == 3, "Caller camera/debug values copied");
        Check(stats.sceneEntities == 1 && stats.candidates == 1 && stats.draws == 1 && stats.disabled == 0
            && stats.frameStorage.storageAllocations == 4 && stats.extractionMicroseconds >= 0
            && stats.preparationMicroseconds >= 0, "Item, stage time and frame allocation telemetry");
        {
            auto access = f.publication.BeginFrame();
            auto mesh = f.meshes.Acquire(access, f.mesh).value();
            auto material = f.materials.Acquire(access, f.material).value();
            Check(&*one->Resources()[0].Mesh() == &*mesh && &*one->Resources()[0].Material().Source() == &*material
                && &*one->Resources()[0].Material().Source()->Declaration() == &*f.declaration,
                "Frame shares exact mesh/material/template owners without deep copies");
        }
        entity.Transform().Translation = {20,30,40}; camera.viewportWidth = 10; line.end.z = 9;
        Check(one->Draws()[0].worldTransform[3].x == 2 && one->Cameras()[0].viewportWidth == 320
            && one->DebugLines()[0].end.z == 3, "Finalized frame independent of later authoring");
        auto [child, childId] = f.Entity(10, 1);
        child.Transform().Translation = {1,0,0}; Check(child.SetParent(entity), "Hierarchy authoring");
        Check(f.scene.RenderData().Add(childId, VisibilityComponent{true, 0}), "Author zero layers");
        Check(f.scene.RenderData().Replace(childId, MeshRendererComponent{f.mesh,f.material,1,false,false,false}), "Author draw flags");
        auto two = f.Extract(stats); Check(two, "Multi-submesh extraction");
        Check(two->Draws().size() == 2 && two->Draws()[0].entity == id && two->Draws()[1].entity == childId
            && two->Draws()[0].submesh.firstElement == 0 && two->Draws()[1].submesh.firstElement == 3
            && two->Draws()[1].worldTransform[3].x == 21, "Parent-before-child order and exact selected submeshes");
        Check(two->Draws()[1].layers == 0 && !two->Draws()[1].pickable && !two->Draws()[1].receiveShadows,
            "No premature layer culling; authored flags copied");
        Check(f.scene.RenderData().Add(id, VisibilityComponent{false, 1}), "Disable parent draw");
        auto disabled = f.Extract(stats);
        Check(disabled && disabled->Draws().size() == 1 && disabled->Draws()[0].entity == childId
            && stats.candidates == 2 && stats.disabled == 1, "Disabled renderable skipped without disabling descendants");
        Check(SDL_GL_MakeCurrent(window, context) == 0, "Restore context before owner retirement");
    }
    void Many(SDL_Window* window, SDL_GLContext context)
    {
        Fixture f;
        for (int i = 128; i > 0; --i)
        {
            auto [entity, id] = f.Entity(static_cast<std::uint64_t>(i), i % 2);
            entity.Transform().Translation.x = float(i);
        }
        Check(SDL_GL_MakeCurrent(window, nullptr) == 0, "Detach many-entity context");
        RenderExtractionStats stats;
        auto first = f.Extract(stats); Check(first, "Many entities");
        Check(first->Draws().size() == 128 && first->Resources().size() == 128, "All many-entity candidates emitted");
        for (int sample = 0; sample != 3; ++sample)
        {
            Allocations::count = Allocations::bytes = 0; Allocations::arrays = 0; Allocations::active = true;
            auto repeated = f.Extract(stats);
            Allocations::active = false;
            Check(repeated, "Repeat extraction");
            for (std::size_t i = 0; i < first->Draws().size(); ++i)
            {
                const auto& a = first->Draws()[i]; const auto& b = repeated->Draws()[i];
                Check(a.entity == b.entity && a.mesh == b.mesh && a.material == b.material && a.pipeline == b.pipeline
                    && a.resources == b.resources && a.submesh.firstElement == b.submesh.firstElement
                    && a.submesh.elementCount == b.submesh.elementCount && a.submesh.materialSlot == b.submesh.materialSlot
                    && a.worldTransform == b.worldTransform
                    && b.worldTransform[3].x == float(i+1) && a.sortKey == b.sortKey,
                    "Stable UUID hierarchy order and deterministic semantic output");
            }
            Check(stats.frameStorage.storageAllocations == 2 && Allocations::arrays == 2,
                "Frozen emission uses exactly two frame arrays without growth");
            std::println("[METRIC] items={} preparation_us={:.3f} extraction_us={:.3f} frame_allocations={} total_call_allocations={} total_requested_bytes={}",
                stats.draws, stats.preparationMicroseconds, stats.extractionMicroseconds,
                stats.frameStorage.storageAllocations, Allocations::count, Allocations::bytes);
        }
        Check(SDL_GL_MakeCurrent(window, context) == 0, "Restore many-entity context");
    }
    void Failures()
    {
        Fixture f; auto [entity, id] = f.Entity(1); RenderExtractionStats stats;
        auto intent = MeshRendererComponent{f.mesh,f.material,2};
        Check(f.scene.RenderData().Replace(id, intent), "Set invalid submesh");
        auto result = f.Extract(stats);
        Check(!result && result.error().entity == id && std::get<FrameError>(result.error().cause).code == FrameErrorCode::InvalidSubmesh,
            "Out-of-range submesh fails with entity and typed frame error");
        intent.submesh = 0; intent.mesh.generation += 1;
        Check(f.scene.RenderData().Replace(id, intent), "Set stale mesh generation"); result = f.Extract(stats);
        Check(!result && std::get<RegistryError>(result.error().cause) == RegistryError::InvalidHandle, "Stale mesh fails without fallback");
        intent.mesh = {}; Check(f.scene.RenderData().Replace(id, intent), "Set null mesh"); result = f.Extract(stats);
        Check(!result && result.error().entity == id, "Null mesh rejected");
        intent.mesh = f.mesh; ++intent.mesh.registry;
        Check(f.scene.RenderData().Replace(id, intent), "Set foreign mesh"); result = f.Extract(stats);
        Check(!result && std::get<RegistryError>(result.error().cause) == RegistryError::InvalidHandle, "Foreign registry identity rejected");
        Check(f.scene.RenderData().Add(id, VisibilityComponent{false}), "Disable invalid resource candidate"); result = f.Extract(stats);
        Check(result && result->Draws().empty(), "Disabled stale resource produces no draw");
        Check(f.scene.RenderData().Remove<VisibilityComponent>(id), "Enable candidate");
        intent.mesh = f.mesh; ++intent.material.generation;
        Check(f.scene.RenderData().Replace(id, intent), "Set stale material"); result = f.Extract(stats);
        Check(!result && std::get<MaterialBindingError>(result.error().cause).code == MaterialBindingCode::InvalidInstance,
            "Material resolution retains structured binding cause");
        intent.material = f.material; Check(f.scene.RenderData().Replace(id, intent), "Restore valid intent");
        entity.Transform().Translation.x = std::numeric_limits<float>::infinity(); result = f.Extract(stats);
        Check(!result && std::holds_alternative<TransformError>(result.error().cause), "Nonfinite presentation propagates transform error");
        entity.Transform().Translation.x = 0;
        FrameCamera camera; camera.entity = id; camera.viewportWidth = 32; camera.viewportHeight = 32;
        FrameDebugLine line;
        for (int failure = 0; failure < 4; ++failure)
        {
            Allocations::arrays = 0; Allocations::failArray = failure; Allocations::active = true;
            result = f.Extract(stats, {&camera,1}, {&line,1}); Allocations::active = false;
            Check(!result && std::get<FrameError>(result.error().cause).code == FrameErrorCode::AllocationFailed,
                "Frame array allocation failure propagates without a partial frame");
            Check(f.scene.RenderData().Replace(id, intent), "Failure releases extraction freeze");
        }
        Allocations::failArray = -1;
        result = f.Extract(stats); Check(result && result->Draws().size() == 1, "Retry after failure succeeds");
        { auto p = f.publication.BeginPublication(); Check(f.textures.Destroy(p, f.texture), "Remove texture"); }
        result = f.Extract(stats);
        Check(!result && std::get<MaterialBindingError>(result.error().cause).code == MaterialBindingCode::InvalidTexture,
            "Stale texture dependency fails with binding diagnostic");
    }
    void Lights(SDL_Window* window, SDL_GLContext context)
    {
        Fixture f; RenderExtractionStats stats;
        Check(SDL_GL_MakeCurrent(window, nullptr) == 0, "Detach context for typed light extraction");
        auto empty = f.Extract(stats);
        Check(empty && empty->DirectionalLights().empty() && empty->PointLights().empty() && empty->SpotLights().empty()
            && empty->LightRevision() == 0 && stats.lightCandidates == 0, "Zero lights");
        auto [spot, spotId] = f.Light(30, RenderLightKind::Spot);
        auto intent = f.scene.RenderData().Get<RenderLightComponent>(spotId).value();
        for (auto kind : {RenderLightKind::Directional, RenderLightKind::Point, RenderLightKind::Spot})
        {
            intent.kind = kind;
            Check(f.scene.RenderData().Replace(spotId, intent), "Change individual type");
            auto one = f.Extract(stats); Check(one, "Individual light extracts");
            Check(one->DirectionalLights().size() == std::size_t(kind == RenderLightKind::Directional)
                && one->PointLights().size() == std::size_t(kind == RenderLightKind::Point)
                && one->SpotLights().size() == std::size_t(kind == RenderLightKind::Spot), "Phase 11 type-correct dispatch");
        }
        intent.color = {.2f,.4f,.6f}; intent.intensity = 3; intent.range = 23;
        intent.innerConeRadians = .2f; intent.outerConeRadians = .8f; intent.castShadows = true;
        Check(f.scene.RenderData().Replace(spotId, intent), "Distinct spot intent");
        // Legacy uniform components cannot override the migrated source or impose their old priority.
        spot.AddComponent<DirectionalLightComponent>(); spot.AddComponent<PointLightComponent>(); spot.AddComponent<SpotLightComponent>();
        auto [point, pointId] = f.Light(20, RenderLightKind::Point);
        auto [directional, directionalId] = f.Light(10, RenderLightKind::Directional);
        directional.Transform().Translation = {5,6,7};
        directional.Transform().SetRotation(glm::angleAxis(std::numbers::pi_v<float> / 2, glm::vec3(0,1,0)));
        directional.Transform().Scale = {2,3,-4};
        point.Transform().Translation = {1,2,3}; spot.Transform().Translation = {1,0,0};
        Check(spot.SetParent(directional), "Light pose follows presentation hierarchy");
        Check(f.scene.RenderData().Add(spotId, VisibilityComponent{false,0}), "Actor invisibility does not disable light");
        auto mixed = f.Extract(stats); Check(mixed, "Mixed typed lights");
        const auto& s = mixed->SpotLights()[0]; const auto& d = mixed->DirectionalLights()[0];
        Check(mixed->DirectionalLights().size() == 1 && mixed->PointLights().size() == 1 && mixed->SpotLights().size() == 1
            && mixed->Draws().empty() && stats.lightCandidates == 3, "Mixed light-only entities ignore legacy uniform records");
        Check(glm::length(d.direction - glm::vec3(1,0,0)) < 1.e-5f && glm::length(s.direction - d.direction) < 1.e-5f
            && glm::length(s.position - glm::vec3(5,6,5)) < 1.e-5f && s.range == 23,
            "Normalized reflected/rotated world direction, hierarchical position, unscaled world range");
        Check(s.color == glm::vec3(.2f,.4f,.6f) && s.intensity == 3 && s.innerConeRadians == .2f
            && s.outerConeRadians == .8f && s.shadows.castShadows && s.entity == spotId && s.revision,
            "Spot values, shadow intent and revision are compact frame data");
        Check(stats.frameStorage.storageAllocations == 3 && stats.frameStorage.usedBytes ==
            sizeof(DirectionalLightData) + sizeof(PointLightData) + sizeof(SpotLightData), "Exact typed light array accounting");
        auto repeat = f.Extract(stats);
        Check(repeat && repeat->LightRevision() == mixed->LightRevision() && repeat->SpotLights()[0].revision == s.revision,
            "No-op light revision stable");
        Check(f.scene.RenderData().Replace(spotId, VisibilityComponent{true,7}), "Change only actor visibility");
        repeat = f.Extract(stats);
        Check(repeat && repeat->LightRevision() == mixed->LightRevision() && repeat->SpotLights()[0].revision == s.revision,
            "Actor visibility is independent of light contribution/revision");
        for (int failure = 0; failure < 3; ++failure)
        {
            Allocations::arrays = 0; Allocations::failArray = failure; Allocations::active = true;
            auto failed = f.Extract(stats); Allocations::active = false;
            Check(!failed && std::get<FrameError>(failed.error().cause).code == FrameErrorCode::AllocationFailed,
                "Light extraction allocation failure returns no partial frame");
            Check(f.scene.RenderData().Replace(spotId, intent), "Light allocation failure releases ECS freeze");
        }
        Allocations::failArray = -1;
        auto revision = s.revision; auto aggregate = mixed->LightRevision();
        auto changed = [&] {
            auto next = f.Extract(stats); Check(next, "Changed light extraction");
            Check(next->SpotLights()[0].revision > revision && next->LightRevision() > aggregate, "Light and aggregate revisions advance");
            revision = next->SpotLights()[0].revision; aggregate = next->LightRevision();
        };
        spot.Transform().Translation.x += 1; changed();
        spot.Transform().SetRotation(glm::angleAxis(.2f,glm::vec3(1,0,0))); changed();
        for (int field = 0; field < 6; ++field)
        {
            if (field == 0) intent.color[0] += .1f;
            if (field == 1) intent.intensity += 1;
            if (field == 2) intent.range += 1;
            if (field == 3) intent.innerConeRadians += .1f;
            if (field == 4) intent.outerConeRadians += .1f;
            if (field == 5) intent.castShadows = false;
            Check(f.scene.RenderData().Replace(spotId, intent), "Edit renderer-relevant light intent"); changed();
        }
        intent.intensity = 0; Check(f.scene.RenderData().Replace(spotId, intent), "Zero intensity contribution");
        auto off = f.Extract(stats);
        Check(off && off->SpotLights().empty() && stats.nonContributingLights == 1 && off->LightRevision() > aggregate,
            "Zero intensity filtered once with contribution revision");
        intent.intensity = 1; Check(f.scene.RenderData().Replace(spotId, intent), "Restore contribution"); changed();
        Check(f.scene.RenderData().Remove<RenderLightComponent>(spotId), "Remove migrated light intent");
        auto removed = f.Extract(stats);
        Check(removed && removed->SpotLights().empty() && removed->LightRevision() > aggregate,
            "Legacy light components alone do not become a second extraction source");
        f.scene.DestroyEntity(spot); f.scene.DestroyEntity(point); f.scene.DestroyEntity(directional);
        auto gone = f.Extract(stats); Check(gone && gone->LightRevision() > removed->LightRevision()
            && gone->DirectionalLights().empty() && gone->PointLights().empty(), "Entity removal visible in empty-frame revision");
        const auto packet = ConsumeLights(*mixed);
        Check(packet.directional == 1 && packet.point == 1 && packet.spot == 1 && packet.pointPosition == glm::vec3(1,2,3)
            && packet.pointRange == 10 && packet.spotInner == .2f && packet.spotOuter == .8f && packet.intensity == 3 && packet.shadow,
            "Serial consumer reads original typed payload after all ECS lights are destroyed");
        std::thread reader([frame = std::move(*mixed)] {
            Check(ConsumeLights(frame).intensity == 3, "Immutable light values survive worker read/destruction without GL");
        }); reader.join();
        Check(SDL_GL_MakeCurrent(window, context) == 0, "Restore context after typed light extraction");
    }
    void LightValidation()
    {
        Fixture f; RenderExtractionStats stats;
        auto [entity, id] = f.Light(1, RenderLightKind::Spot);
        RenderLightComponent valid; valid.kind = RenderLightKind::Spot;
        const float nan = std::numeric_limits<float>::quiet_NaN(), infinity = std::numeric_limits<float>::infinity();
        auto fail = [&](RenderLightComponent intent, FrameErrorCode code, bool preparation = false) {
            Check(f.scene.RenderData().Replace(id, intent), "Author invalid light");
            auto result = f.Extract(stats); Check(!result, "Invalid light fails entire extraction");
            if (preparation)
                Check(std::get<TransformError>(result.error().cause).code == TransformErrorCode::NonFiniteRenderData,
                    "Preparation preserves typed nonfinite diagnostic");
            else Check(result.error().entity == id && std::get<FrameError>(result.error().cause).code == code,
                "Light error retains entity and precise typed cause");
            Check(f.scene.RenderData().Replace(id, valid), "Failure releases mutation boundary");
        };
        for (float range : {0.f,-1.f,nan,infinity})
        {
            auto bad = valid; bad.range = range; fail(bad, FrameErrorCode::InvalidLightRange, !std::isfinite(range));
            bad.kind = RenderLightKind::Point; fail(bad, FrameErrorCode::InvalidLightRange, !std::isfinite(range));
        }
        for (const auto cone : {glm::vec2(-.1f,.6f),glm::vec2(.7f,.6f),glm::vec2(0,std::numbers::pi_v<float>),
            glm::vec2(nan,.6f),glm::vec2(.4f,infinity)})
        {
            auto bad = valid; bad.innerConeRadians = cone.x; bad.outerConeRadians = cone.y;
            fail(bad,FrameErrorCode::InvalidLightCone,!std::isfinite(cone.x) || !std::isfinite(cone.y));
        }
        for (float value : {-.1f,nan,infinity})
        {
            auto bad = valid; bad.intensity = value; fail(bad,FrameErrorCode::InvalidLight,!std::isfinite(value));
            bad = valid; bad.color[1] = value; fail(bad,FrameErrorCode::InvalidLight,!std::isfinite(value));
        }
        auto bad = valid; bad.kind = static_cast<RenderLightKind>(99); fail(bad,FrameErrorCode::InvalidLight);
        for (float cone : {0.f,.5f})
        {
            auto edge = valid; edge.innerConeRadians = edge.outerConeRadians = cone;
            Check(f.scene.RenderData().Replace(id, edge), "Hard-edge cone intent");
            auto frame = f.Extract(stats); Check(frame && frame->SpotLights()[0].innerConeRadians == cone
                && frame->SpotLights()[0].outerConeRadians == cone, "Equal cones including zero preserved as hard edge");
        }
        Check(f.scene.RenderData().Replace(id,valid), "Restore valid spot");
        for (auto kind : {RenderLightKind::Directional,RenderLightKind::Spot})
        {
            valid.kind = kind; Check(f.scene.RenderData().Replace(id,valid), "Direction-bearing kind");
            entity.Transform().Scale.z = 0;
            auto zero = f.Extract(stats); Check(!zero && std::get<FrameError>(zero.error().cause).code == FrameErrorCode::InvalidLightDirection,
                "Collapsed direction is a typed error");
            for (float scale : {1.e-30f,1.e30f,-2.f})
            {
                entity.Transform().Scale.z = scale;
                auto frame = f.Extract(stats); Check(frame, "Extreme finite direction normalized without float square overflow");
                const auto direction = kind == RenderLightKind::Directional ? frame->DirectionalLights()[0].direction : frame->SpotLights()[0].direction;
                Check(direction == glm::vec3(0,0,scale < 0 ? 1 : -1), "Direction sign and unit length preserved");
            }
            entity.Transform().Scale.z = infinity;
            auto nonfinite = f.Extract(stats); Check(!nonfinite && std::holds_alternative<TransformError>(nonfinite.error().cause),
                "Nonfinite direction source rejected by presentation validation");
        }
        entity.Transform().Scale.z = 1;
        valid.kind = RenderLightKind::Point; Check(f.scene.RenderData().Replace(id,valid), "Point needs no direction");
        entity.Transform().Scale = {0,0,0}; Check(f.Extract(stats), "Point position valid under zero scale");
        valid.intensity = 0; valid.range = 0; valid.innerConeRadians = 2; valid.outerConeRadians = 1;
        Check(f.scene.RenderData().Replace(id,valid), "Noncontributing light needs no range or cone");
        auto off = f.Extract(stats); Check(off && off->PointLights().empty(), "Finite noncontributing intent skipped deterministically");
    }
    void LightLimitsAndBuilder()
    {
        Fixture f; RenderExtractionStats stats;
        std::array<EntityRenderId, MaxFrameLights> sourceIds;
        for (std::size_t i = MaxFrameLights; i > 0; --i) sourceIds[i-1] = f.Light(i, RenderLightKind::Point).second;
        auto full = f.Extract(stats); Check(full && full->PointLights().size() == MaxFrameLights, "Exact supported maximum");
        auto again = f.Extract(stats); Check(again, "Repeat maximum");
        for (std::size_t i = 0; i < MaxFrameLights; ++i)
            Check(full->PointLights()[i].entity == again->PointLights()[i].entity
                && full->PointLights()[i].entity == sourceIds[i], "Multiple same-type lights retain deterministic order");
        auto [extra, extraId] = f.Light(MaxFrameLights+1, RenderLightKind::Spot);
        auto overflow = f.Extract(stats); Check(!overflow && overflow.error().entity == extraId
            && std::get<FrameError>(overflow.error().cause).code == FrameErrorCode::LightLimitExceeded, "Mixed-type overflow fails without truncation");
        auto intent = f.scene.RenderData().Get<RenderLightComponent>(extraId).value(); intent.intensity = 0;
        Check(f.scene.RenderData().Replace(extraId,intent), "Disable overflow light by contribution");
        Check(f.Extract(stats), "Noncontributing lights do not consume limit");
        FrameCapacity capacity; capacity.directionalLights = capacity.pointLights = capacity.spotLights = 1;
        for (int failure = 0; failure < 3; ++failure)
        {
            Allocations::arrays = 0; Allocations::failArray = failure; Allocations::active = true;
            auto builder = RenderFrameBuilder::Create(capacity); Allocations::active = false;
            Check(!builder && builder.error().code == FrameErrorCode::AllocationFailed, "Each typed light array allocation rolls back");
        }
        Allocations::failArray = -1;
        auto builder = RenderFrameBuilder::Create(capacity).value();
        DirectionalLightData d; d.entity = extraId; d.revision = 1;
        d.direction = {0,0,0}; Check(!builder.AddLight(d), "Direct builder rejects zero direction");
        d.direction = {std::numeric_limits<float>::quiet_NaN(),0,0}; Check(!builder.AddLight(d), "Direct builder rejects nonfinite direction");
        d.direction = {0,0,-2}; Check(!builder.AddLight(d), "Direct builder rejects nonunit direction");
        d.direction = {0,0,-1}; Check(builder.AddLight(d), "Valid directional append after rejection");
        Check(!builder.AddLight(d), "Typed section capacity enforced");
        PointLightData p; p.entity = extraId; p.revision = 1;
        p.position.x = std::numeric_limits<float>::infinity(); Check(!builder.AddLight(p), "Builder rejects nonfinite position");
        p.position.x = 0; p.range = 0; Check(!builder.AddLight(p), "Builder rejects nonpositive range");
        p.range = 10; p.color.x = -1; Check(!builder.AddLight(p), "Builder rejects negative color");
        p.color.x = 1; p.intensity = 0; Check(!builder.AddLight(p), "Builder accepts only contributing intensity");
        p.intensity = 1; Check(builder.AddLight(p), "Point builder append");
        SpotLightData s; s.entity = extraId; s.revision = 1;
        s.outerConeRadians = std::numeric_limits<float>::quiet_NaN(); Check(!builder.AddLight(s), "Builder rejects nonfinite cone");
        s.outerConeRadians = .2f; Check(!builder.AddLight(s), "Builder rejects reversed cone");
        s.outerConeRadians = .6f; Check(builder.AddLight(s), "Spot builder append");
        auto frame = std::move(builder).Finalize().value();
        Check(!builder.AddLight(d) && !builder.AddLight(p) && !builder.AddLight(s), "All appends rejected after publication");
        auto moved = std::move(frame);
        Check(frame.DirectionalLights().empty() && frame.PointLights().empty() && frame.SpotLights().empty()
            && moved.DirectionalLights().size() == 1 && moved.PointLights().size() == 1 && moved.SpotLights().size() == 1,
            "All typed light owners participate in frame moves");
        capacity.directionalLights = MaxFrameLights;
        auto invalidCapacity = RenderFrameBuilder::Create(capacity);
        Check(!invalidCapacity && invalidCapacity.error().code == FrameErrorCode::LightLimitExceeded, "Builder enforces combined capacity maximum");
    }
    void Retention(SDL_Window* window, SDL_GLContext context)
    {
        Fixture f; auto [entity, id] = f.Entity(1); RenderExtractionStats stats;
        Check(SDL_GL_MakeCurrent(window, nullptr) == 0, "Detach retention context");
        auto old = f.Extract(stats); Check(old, "Extract retained frame");
        const auto* packet = old->Resources()[0].Material().PackedWords().data();
        Check(SDL_GL_MakeCurrent(window, context) == 0, "Restore publication context");
        {
            auto p = f.publication.BeginPublication();
            Check(f.meshes.Replace(p, f.mesh, GpuMesh::Create(MeshSource(2)).value()), "Publish newer mesh");
            Check(f.textures.Replace(p, f.texture, Image()), "Publish newer texture");
            Check(f.samplers.Replace(p, f.sampler, GpuSampler::Create({}).value()), "Publish newer sampler");
            Check(f.materials.Replace(p, f.material, MaterialInstance::Create(f.declaration).value()), "Publish newer instance");
        }
        auto current = f.Extract(stats); Check(current, "Next frame resolves current publication");
        Check(current->Resources()[0].Mesh().Revision() == 2 && current->Resources()[0].Material().PublicationRevision() == 2
            && current->Resources()[0].Material().Textures()[0].texture.Revision() == 2
            && old->Resources()[0].Mesh().Revision() == 1 && old->Resources()[0].Material().Textures()[0].sampler.Revision() == 1,
            "Frames retain their own publication snapshots");
        current = std::unexpected(RenderExtractionError{{}, RegistryError::InvalidHandle});
        {
            auto p = f.publication.BeginPublication();
            Check(f.programs.Replace(p, f.program, Program()), "Publish newer program");
            Check(f.meshes.Destroy(p, f.mesh) && f.materials.Destroy(p, f.material), "Invalidate published identities");
            f.meshes.Collect(p); f.materials.Collect(p); f.textures.Collect(p); f.samplers.Collect(p);
        }
        Check(old->Resources()[0].Mesh()->Bounds().maximum[0] == 1 && old->Resources()[0].Material().Program().Revision() == 1
            && old->Resources()[0].Material().PackedWords().data() == packet, "Old resources and packet buffer survive replacement/removal");
        auto stale = f.Extract(stats);
        Check(!stale && std::get<RegistryError>(stale.error().cause) == RegistryError::InvalidHandle, "Removed identity rejected next frame");
        std::thread worker([frame = std::move(*old)] {
            Check(frame.Draws().size() == 1 && frame.Resources()[0].Material().PackedWords()[0] != 0,
                "Worker reads and releases finalized frame without GL");
        }); worker.join();
        { auto p = f.publication.BeginPublication();
            Check(f.meshes.Collect(p) == 1 && f.materials.Collect(p) == 1 && f.programs.Collect(p) == 1
                && f.textures.Collect(p) == 1 && f.samplers.Collect(p) == 1, "Owner retires old GPU resources after frame release"); }
    }
}
int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::set_terminate([] { std::println(stderr, "[FAIL] unexpected termination after: {}", lastCheck); std::_Exit(70); });
    Check(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL init");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window = SDL_CreateWindow("Serial extraction validation",0,0,32,32,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    Check(window, "Hidden window"); auto context = SDL_GL_CreateContext(window); Check(context, "Context");
    Check(gladLoadGLLoader(SDL_GL_GetProcAddress), "GL loader");
    Basic(window,context); Many(window,context); Failures(); Lights(window,context); LightValidation(); LightLimitsAndBuilder(); Retention(window,context);
    Check(glGetError() == GL_NO_ERROR, "No GL errors");
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    std::println("[PASS] render-extraction checks={}", checks);
}
#endif
