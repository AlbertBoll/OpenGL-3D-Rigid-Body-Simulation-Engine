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

#ifndef EXTRACTION_SCHEMA_ONLY
#include "Scene/_Entity.h"
#include "Core/GLContextThread.h"
#include <glad/glad.h>
#include <sdl2/SDL.h>
#include <cstdlib>
#include <limits>
#include <new>
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
    Basic(window,context); Many(window,context); Failures(); Retention(window,context);
    Check(glGetError() == GL_NO_ERROR, "No GL errors");
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    std::println("[PASS] render-extraction checks={}", checks);
}
#endif
