#include "Renderer/RenderFrame.h"
#include <cstdlib>
#include <limits>
#include <new>
#include <print>
#include <thread>
#include <type_traits>

using namespace GEngine;
using namespace GEngine::Asset;

static_assert(!std::is_copy_constructible_v<RenderFrame> && !std::is_copy_assignable_v<RenderFrame>);
static_assert(!std::is_default_constructible_v<RenderFrame>);
static_assert(!std::is_copy_constructible_v<RenderFrameBuilder>);
static_assert(!std::is_copy_constructible_v<FrameResources>);
static_assert(std::is_nothrow_move_constructible_v<RenderFrame> && std::is_nothrow_move_assignable_v<RenderFrame>);
static_assert(std::is_nothrow_move_constructible_v<PreparedMaterialBinding>);
static_assert(std::same_as<decltype(std::declval<RenderFrame&>().Draws()), std::span<const DrawItem>>);
static_assert(std::same_as<decltype(std::declval<RenderFrame&>().Cameras()), std::span<const FrameCamera>>);
static_assert(std::same_as<decltype(std::declval<RenderFrame&>().DebugLines()), std::span<const FrameDebugLine>>);
static_assert(std::same_as<decltype(std::declval<RenderFrame&>().Resources()), std::span<const FrameResources>>);
static_assert(std::same_as<decltype(std::declval<const FrameResources&>().Material()), const PreparedMaterialBinding&>);
static_assert(std::is_trivially_copyable_v<DrawItem> && std::is_trivially_copyable_v<FrameCamera>
    && std::is_trivially_copyable_v<FrameDebugLine>);
static_assert(sizeof(DrawItem) <= 224 && sizeof(FrameCamera) <= 192 && sizeof(FrameDebugLine) <= 80);
// Seven array owners, two seven-section count records and the aggregate light
// revision fit in 176 bytes on the maintained x64 target (formerly four arrays).
static_assert(sizeof(RenderFrame) <= 176 && sizeof(FrameResources) <= 256);
static_assert(sizeof(DirectionalLightData) <= 64 && sizeof(PointLightData) <= 72 && sizeof(SpotLightData) <= 88);

#ifndef FRAME_SCHEMA_ONLY
#include "Core/GLContextThread.h"
#include <glad/glad.h>
#include <sdl2/SDL.h>

namespace AllocationProbe
{
    bool active{};
    int failAt = -1, attempts{}, live{};
    void* pointers[16]{};
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    using namespace AllocationProbe;
    if (active && attempts++ == failAt) return nullptr;
    void* pointer = ::operator new(size, std::nothrow);
    if (active && pointer)
    {
        if (live == 16) std::abort();
        pointers[live++] = pointer;
    }
    return pointer;
}
void operator delete[](void* pointer) noexcept
{
    using namespace AllocationProbe;
    for (int i = 0; i < live; ++i)
        if (pointers[i] == pointer) { pointers[i] = pointers[--live]; break; }
    ::operator delete(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete[](pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { ::operator delete[](pointer); }

namespace
{
    int checks{};
    const char* lastCheck = "startup";
    template<class T> void Check(const T& value, const char* message)
    { lastCheck = message; ++checks; if (!static_cast<bool>(value)) { std::println(stderr, "[FAIL] {}", message); std::exit(1); } }
    template<class T> void ErrorIs(const std::expected<T, FrameError>& result, FrameErrorCode code, const char* message)
    { Check(!result && result.error().code == code, message); }
    FrameCamera Camera(std::uint32_t slot = 1)
    {
        FrameCamera camera;
        camera.entity = {slot, 1, 7}; camera.viewportWidth = 640; camera.viewportHeight = 480;
        return camera;
    }
    void Cpu()
    {
        using namespace AllocationProbe;
        active = true;
        {
            auto builder = RenderFrameBuilder::Create(); Check(builder, "Empty builder");
            auto frame = std::move(*builder).Finalize(); Check(frame, "Empty finalization");
            Check(frame->Draws().empty() && frame->Cameras().empty() && frame->DebugLines().empty()
                && frame->Resources().empty(), "Empty frame views");
            Check(attempts == 0 && frame->Storage().storageAllocations == 0
                && frame->Storage().capacityBytes == 0, "Empty frame has no storage allocations");
            ErrorIs(std::move(*builder).Finalize(), FrameErrorCode::Finalized, "Double finalization rejected");
            ErrorIs(builder->AddCamera(Camera()), FrameErrorCode::Finalized, "No camera append after finalization");
            ErrorIs(builder->AddDraw({}), FrameErrorCode::Finalized, "No draw append after finalization");
            ErrorIs(builder->AddDebugLine({}), FrameErrorCode::Finalized, "No debug append after finalization");
        }
        const FrameSection sections[]{FrameSection::Cameras, FrameSection::Draws, FrameSection::DebugLines, FrameSection::Resources};
        for (int i = 0; i < 4; ++i)
        {
            attempts = 0; failAt = i;
            auto failed = RenderFrameBuilder::Create({2, 4, 2, 1});
            ErrorIs(failed, FrameErrorCode::AllocationFailed, "Injected allocation failure is typed");
            Check(failed.error().section == sections[i] && live == 0, "Partial allocations cleaned transactionally");
        }
        failAt = -1; attempts = 0;
        const auto huge = (std::numeric_limits<std::size_t>::max)();
        for (const auto capacity : {FrameCapacity{huge,0,0,0}, FrameCapacity{0,huge,0,0},
                                   FrameCapacity{0,0,huge,0}, FrameCapacity{0,0,0,huge}})
            ErrorIs(RenderFrameBuilder::Create(capacity), FrameErrorCode::CapacityOverflow, "Overflow rejected before allocation");
        Check(attempts == 0, "Overflow makes no allocations");
        {
            auto builder = RenderFrameBuilder::Create({2, 4, 2, 1}).value();
            Check(attempts == 4 && live == 4, "One allocation per nonempty section");
            auto invalid = Camera(); invalid.projection[0][0] = std::numeric_limits<float>::infinity();
            ErrorIs(builder.AddCamera(invalid), FrameErrorCode::InvalidCamera, "Nonfinite camera rejected");
            invalid = Camera(); invalid.viewportX = (std::numeric_limits<std::uint32_t>::max)();
            ErrorIs(builder.AddCamera(invalid), FrameErrorCode::InvalidCamera, "Viewport overflow rejected");
            invalid = Camera(); invalid.entity = {};
            ErrorIs(builder.AddCamera(invalid), FrameErrorCode::InvalidCamera, "Invalid entity rejected");
            auto camera = Camera(9); Check(builder.AddCamera(camera), "First camera");
            camera.view[3].x = 500; Check(builder.AddCamera(Camera(2)), "Second camera");
            ErrorIs(builder.AddCamera(Camera()), FrameErrorCode::CapacityExceeded, "Camera capacity enforced");
            ErrorIs(builder.AddDraw({}), FrameErrorCode::InvalidResources, "Missing resource ordinal rejected");
            FrameDebugLine line; line.color.a = 2;
            ErrorIs(builder.AddDebugLine(line), FrameErrorCode::InvalidDebugLine, "Invalid debug alpha rejected");
            line.color.a = 1; line.depth = static_cast<DebugDepth>(500);
            ErrorIs(builder.AddDebugLine(line), FrameErrorCode::InvalidDebugLine, "Invalid debug mode rejected");
            line.depth = DebugDepth::Overlay; Check(builder.AddDebugLine(line), "World debug line");
            line.start.x = 12; Check(builder.AddDebugLine(line), "Second debug line");
            ErrorIs(builder.AddDebugLine(line), FrameErrorCode::CapacityExceeded, "Debug capacity enforced");
            auto moved = std::move(builder);
            ErrorIs(std::move(builder).Finalize(), FrameErrorCode::Finalized, "Moved-from builder is closed");
            auto frame = std::move(moved).Finalize().value();
            Check(attempts == 4, "Append and finalize allocate no frame storage");
            Check(frame.Cameras()[0].entity.index == 9 && frame.Cameras()[1].entity.index == 2
                && frame.Cameras()[0].view[3].x == 0, "Camera order and copied values independent of source");
            Check(frame.DebugLines()[0].start.x == 0 && frame.DebugLines()[1].start.x == 12, "Debug order preserved");
            const auto stats = frame.Storage();
            Check(stats.storageAllocations == 4 && stats.usedBytes == 2*sizeof(FrameCamera)+2*sizeof(FrameDebugLine)
                && stats.capacityBytes == 2*sizeof(FrameCamera)+4*sizeof(DrawItem)+2*sizeof(FrameDebugLine)+sizeof(FrameResources),
                "Exact frame array accounting");
            const auto* address = frame.Cameras().data();
            auto relocated = std::move(frame);
            Check(frame.Cameras().empty() && frame.Storage().capacityBytes == 0 && relocated.Cameras().data() == address,
                "Move transfers buffers and empties source");
            auto destination = std::move(RenderFrameBuilder::Create().value()).Finalize().value();
            destination = std::move(relocated);
            Check(relocated.Cameras().empty() && destination.Cameras().data() == address, "Move assignment transfers storage");
        }
        Check(live == 0, "All frame storage released"); active = false;
        std::println("[PASS] frame CPU immutability, empty, ordering, moves, capacities, failures and accounting");
    }

    struct Vertex { float x,y,z; };
    MeshAsset MeshSource(float scale = 1)
    {
        const Vertex vertices[]{{0,0,0},{scale,0,0},{0,scale,0}};
        const VertexAttribute attributes[]{{VertexSemantic::Position,0,VertexScalarFormat::Float32,3,VertexInterpretation::Floating,0}};
        const SubmeshRange ranges[]{{0,3,0},{0,0,0}};
        auto source = MeshSourceData::FromVertices<Vertex>(vertices, attributes); source.submeshes = ranges;
        return MeshAsset::Create(source).value();
    }
    ShaderProgram Program()
    {
        const ShaderSource sources[]{{VERTEX,"#version 460 core\nvoid main(){gl_Position=vec4(0,0,0,1);}","frame vertex"},
            {FRAGMENT,"#version 460 core\nlayout(location=0) out vec4 color;void main(){color=vec4(1);}","frame fragment"}};
        return ShaderProgram::Create({sources}).value();
    }
    TextureResource Image()
    {
        TextureDesc desc; desc.width = desc.height = 1; desc.mips = TextureMipIntent::None;
        const std::array<std::byte,4> pixels{std::byte{20},std::byte{40},std::byte{60},std::byte{255}};
        return TextureResource::Create(desc, {pixels}).value();
    }
    void Resources(SDL_Window* window, SDL_GLContext context)
    {
        AssetPublication publication;
        ShaderProgramRegistry programs(publication); TextureRegistry textures(publication); SamplerRegistry samplers(publication);
        PipelineRegistry pipelines(publication);
        MaterialTemplateRegistry templates(publication); MaterialInstanceRegistry materials(publication);
        MeshRegistry meshes(publication);
        ShaderProgramHandle program; PipelineHandle pipeline; MeshHandle mesh; MaterialInstanceHandle material;
        TextureHandle texture; SamplerHandle sampler;
        MaterialTemplateHandle declaration;
        {
            auto p = publication.BeginPublication(); program = programs.Create(p, Program()).value();
            texture = textures.Create(p, Image()).value(); sampler = samplers.Create(p, GpuSampler::Create({}).value()).value();
            PipelineDesc desc; desc.program = program; desc.programRevision = 1; desc.alpha = AlphaMode::Transparent;
            pipeline = pipelines.Create(p, PipelineState::Create(desc).value()).value();
            mesh = PublishMesh(meshes, p, MeshSource()).value();
        }
        {
            PipelineView view;
            { auto f = publication.BeginFrame(); view = pipelines.Acquire(f, pipeline).value(); }
            const MaterialParameterDecl parameters[]{{"value",MaterialParameterType::Float,.5f}};
            const MaterialTextureSlotDecl slots[]{{"image", true, MaterialTextureValue{texture, sampler}}};
            auto definition = MaterialTemplate::Create({view, parameters, slots, false, false}).value();
            auto p = publication.BeginPublication(); declaration = templates.Create(p, std::move(definition)).value();
        }
        {
            MaterialTemplateView view;
            { auto f = publication.BeginFrame(); view = templates.Acquire(f, declaration).value(); }
            auto instance = MaterialInstance::Create(view).value();
            auto p = publication.BeginPublication(); material = materials.Create(p, std::move(instance)).value();
        }
        auto builder = RenderFrameBuilder::Create({0,3,0,1}).value();
        const std::uint32_t* packed = nullptr;
        {
            auto access = publication.BeginFrame();
            auto instance = materials.Acquire(access, material).value();
            auto prepared = PreparedMaterialBinding::Prepare(instance, access, {programs,textures,samplers}).value();
            packed = prepared.PackedWords().data();
            Check(SDL_GL_MakeCurrent(window, nullptr) == 0, "Detach GL for frame building");
            ErrorIs(builder.AddResources({}, std::move(prepared)), FrameErrorCode::InvalidResources, "Invalid lease rejected without consuming packet");
            Check(prepared.Source() && prepared.PackedWords().data() == packed, "Failed append preserves resource packet");
            auto index = builder.AddResources(meshes.Acquire(access, mesh).value(), std::move(prepared));
            Check(index && *index == 0, "Transfer exact prepared resource versions");
            auto extra = PreparedMaterialBinding::Prepare(instance, access, {programs,textures,samplers}).value();
            ErrorIs(builder.AddResources(meshes.Acquire(access, mesh).value(), std::move(extra)),
                FrameErrorCode::CapacityExceeded, "Resource capacity enforced");
            Check(extra.Source(), "Capacity failure preserves packet");
            FrameDrawDesc draw; draw.entity = {4,1,8};
            draw.submesh = 2;
            ErrorIs(builder.AddDraw(draw), FrameErrorCode::InvalidSubmesh, "Submesh ordinal validated");
            draw.submesh = 0; draw.worldTransform[0][3] = 1;
            ErrorIs(builder.AddDraw(draw), FrameErrorCode::InvalidDraw, "Projective model transform rejected");
            draw.worldTransform = glm::mat4(1.f); draw.sortKey = 999;
            Check(builder.AddDraw(draw), "First draw");
            draw.entity.index = 1; draw.sortKey = 1; draw.worldTransform[3].x = 5;
            Check(builder.AddDraw(draw), "Second draw");
            draw.entity.index = 2; draw.submesh = 1;
            Check(builder.AddDraw(draw), "Third draw with equal key and empty range");
            ErrorIs(builder.AddDraw(draw), FrameErrorCode::CapacityExceeded, "Draw capacity enforced");
        }
        auto frame = std::move(builder).Finalize().value();
        Check(frame.Resources()[0].Material().PackedWords().data() == packed, "Prepared buffers transferred without copies");
        Check(frame.Draws()[0].mesh == mesh && frame.Draws()[0].pipeline == pipeline && frame.Draws()[0].material == material,
            "Draw identities derive from retained versions");
        Check(frame.Draws()[0].entity.index == 4 && frame.Draws()[1].entity.index == 1 && frame.Draws()[2].entity.index == 2
            && frame.Draws()[0].sortKey == 999 && frame.Draws()[2].submesh.elementCount == 0,
            "No opaque or transparent reorder; ties and empty ranges preserve input order");
        Check(frame.Resources()[0].Material().Pipeline().Alpha() == AlphaMode::Transparent
            && !frame.Resources()[0].Material().Pipeline().Depth().write && !frame.Draws()[0].castShadows,
            "Retained alpha/depth/shadow policy agrees");
        Check(SDL_GL_MakeCurrent(window, context) == 0, "Restore owner context");
        {
            auto p = publication.BeginPublication();
            Check(meshes.Replace(p, mesh, GpuMesh::Create(MeshSource(2)).value()), "Publish replacement mesh");
            Check(programs.Replace(p, program, Program()), "Publish replacement program");
            Check(textures.Replace(p, texture, Image()), "Publish replacement texture");
            Check(samplers.Replace(p, sampler, GpuSampler::Create({}).value()), "Publish replacement sampler");
            Check(materials.Destroy(p, material), "Remove material after finalization");
            Check(meshes.Collect(p) == 0 && programs.Collect(p) == 0 && materials.Collect(p) == 0
                && textures.Collect(p) == 0 && samplers.Collect(p) == 0, "Frame pins retired versions");
        }
        Check(frame.Resources()[0].Mesh().Revision() == 1 && frame.Resources()[0].Material().Program().Revision() == 1
            && frame.Resources()[0].Mesh()->Bounds().maximum[0] == 1
            && frame.Resources()[0].Material().Textures()[0].texture.Revision() == 1
            && frame.Resources()[0].Material().Textures()[0].sampler.Revision() == 1, "Frame remains on exact original versions");
        auto worker = std::thread([owned = std::move(frame)]() mutable
        {
            Check(owned.Draws().size() == 3 && owned.Resources()[0].Material().PackedWords()[0] != 0,
                "Worker reads finalized frame without a context");
            // CPU packet/lease destruction on this worker must not retire GPU owners.
        });
        worker.join();
        {
            auto p = publication.BeginPublication();
            Check(meshes.Collect(p) == 1 && materials.Collect(p) == 1 && programs.Collect(p) == 1
                && textures.Collect(p) == 1 && samplers.Collect(p) == 1,
                "Owner collects retired resources after worker releases frame");
        }
        std::println("[PASS] frame retained versions, no heavy copies, detached-context build and worker release");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::set_terminate([] { std::println(stderr, "[FAIL] unexpected termination after: {}", lastCheck); std::_Exit(70); });
    Cpu();
    Check(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL init");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window = SDL_CreateWindow("Frame schema validation",0,0,32,32,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    Check(window, "Hidden window"); auto context = SDL_GL_CreateContext(window); Check(context, "Context");
    Check(gladLoadGLLoader(SDL_GL_GetProcAddress), "GL loader");
    Resources(window, context);
    Check(glGetError() == GL_NO_ERROR, "No GL errors");
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    std::println("[PASS] render-frame checks={} sizes frame={} draw={} camera={} debug={} resources={}",
        checks, sizeof(RenderFrame), sizeof(DrawItem), sizeof(FrameCamera), sizeof(FrameDebugLine), sizeof(FrameResources));
}
#endif
