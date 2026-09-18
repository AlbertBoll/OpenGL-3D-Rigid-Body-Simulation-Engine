#include "Renderer/RenderFrame.h"
#include "Renderer/RenderVisibility.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
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
static_assert(!std::is_copy_constructible_v<RenderVisibility> && !std::is_default_constructible_v<RenderVisibility>);
static_assert(std::is_nothrow_move_constructible_v<RenderVisibility> && std::is_nothrow_move_assignable_v<RenderVisibility>);
static_assert(std::same_as<decltype(std::declval<const RenderVisibility&>().Main()), std::span<const std::size_t>>);

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
    WorldBounds Box(glm::dvec3 lo, glm::dvec3 hi)
    {
        WorldBounds bounds; bounds.status = BoundsStatus::Valid;
        for (int i = 0; i < 3; ++i) { bounds.minimum[i] = lo[i]; bounds.maximum[i] = hi[i]; }
        return bounds;
    }
    void Frusta()
    {
        using V = BoundsVisibility;
        auto camera = Camera();
        auto frustum = CameraFrustum::FromCamera(camera);
        Check(frustum.Test(Box(glm::dvec3(-.5), glm::dvec3(.5))) == V::Inside, "AABB fully inside");
        Check(frustum.Test(Box(glm::dvec3(-2), glm::dvec3(2))) == V::Intersecting, "AABB encloses frustum");
        for (int axis = 0; axis < 3; ++axis) for (int sign : {-1, 1})
        {
            glm::dvec3 center(0), radius(.1); center[axis] = sign * 2.;
            Check(frustum.Test(Box(center-radius, center+radius)) == V::Outside, "Reject each of six planes");
            center[axis] = sign;
            Check(frustum.Test(Box(center-radius, center+radius)) == V::Intersecting, "Intersect each plane");
            Check(frustum.Test(Box(center, center)) == V::Intersecting, "Zero-size bound on plane survives");
        }
        for (auto status : {BoundsStatus::Invalid, BoundsStatus::Unavailable, BoundsStatus::Empty})
        {
            auto bounds = Box(glm::dvec3(100), glm::dvec3(101)); bounds.status = status;
            Check(frustum.Test(bounds) == V::Conservative, "Invalid/missing/empty metadata cannot reject");
        }
        auto malformed = Box(glm::dvec3(1), glm::dvec3(-1));
        Check(frustum.Test(malformed) == V::Conservative, "Reversed endpoints fallback");
        malformed = Box(glm::dvec3(2), glm::dvec3(3));
        malformed.minimum[0] = std::numeric_limits<double>::quiet_NaN();
        Check(frustum.Test(malformed) == V::Conservative, "NaN bounds fallback");
        malformed.minimum[0] = -std::numeric_limits<double>::infinity();
        Check(frustum.Test(malformed) == V::Conservative, "Infinite bounds fallback");
        camera.projection = glm::mat4(0);
        Check(CameraFrustum::FromCamera(camera).Test(Box(glm::dvec3(100), glm::dvec3(101))) == V::Conservative,
            "Singular projection retains candidate");
        camera.projection[0][0] = std::numeric_limits<float>::quiet_NaN();
        Check(CameraFrustum::FromCamera(camera).Test(malformed) == V::Conservative, "Nonfinite frustum fallback");
        camera.projection = glm::perspective(glm::radians(90.f), 2.f, 1.f, 10.f);
        frustum = CameraFrustum::FromCamera(camera);
        Check(frustum.Test(Box({2.9,-.1,-2.1}, {3.1,.1,-1.9})) == V::Inside, "Wide projection horizontal extent");
        Check(frustum.Test(Box({-.1,2.9,-2.1}, {.1,3.1,-1.9})) == V::Outside, "Wide projection vertical extent");
        Check(frustum.Test(Box({-.1,-.1,-.5}, {.1,.1,-.1})) == V::Outside, "Perspective near rejection");
        Check(frustum.Test(Box({-.1,-.1,-12}, {.1,.1,-11})) == V::Outside, "Perspective far rejection");
        Check(frustum.Test(Box({-.1,-.1,1}, {.1,.1,2})) == V::Outside, "Behind-camera rejection");
        camera.projection = glm::perspective(glm::radians(90.f), .5f, 1.f, 10.f);
        Check(CameraFrustum::FromCamera(camera).Test(Box({1.4,-.1,-2.1}, {1.6,.1,-1.9})) == V::Outside,
            "Tall projection horizontal extent");
        camera.projection = glm::ortho(-2.f, 2.f, -1.f, 1.f, 1.f, 10.f);
        camera.view = glm::lookAt(glm::vec3(10,0,0), glm::vec3(0,0,0), glm::vec3(0,1,0));
        frustum = CameraFrustum::FromCamera(camera);
        Check(frustum.Test(Box({4,-.1,-.1}, {6,.1,.1})) == V::Inside, "Translated rotated camera world bounds");
        Check(frustum.Test(Box({11,-.1,-.1}, {12,.1,.1})) == V::Outside, "Translated camera behind rejection");
        std::println("[PASS] visibility frustum inside/outside/intersection, aspect, pose and conservative bounds");
    }
    void VisibilityResources(SDL_Window* window, SDL_GLContext context)
    {
        AssetPublication publication;
        ShaderProgramRegistry programs(publication); TextureRegistry textures(publication); SamplerRegistry samplers(publication);
        PipelineRegistry pipelines(publication); MaterialTemplateRegistry templates(publication);
        MaterialInstanceRegistry materials(publication); MeshRegistry meshes(publication);
        ShaderProgramHandle program; MeshHandle mesh; MaterialInstanceHandle material[3];
        {
            auto p = publication.BeginPublication(); program = programs.Create(p, Program()).value();
            mesh = PublishMesh(meshes, p, MeshSource()).value();
        }
        for (int i = 0; i < 3; ++i)
        {
            PipelineDesc desc; desc.program = program; desc.programRevision = 1; desc.alpha = static_cast<AlphaMode>(i);
            PipelineHandle handle;
            { auto p = publication.BeginPublication(); handle = pipelines.Create(p, PipelineState::Create(desc).value()).value(); }
            PipelineView view;
            { auto f = publication.BeginFrame(); view = pipelines.Acquire(f, handle).value(); }
            auto definition = MaterialTemplate::Create({view, {}, {}, i != 2, i != 2}).value();
            MaterialTemplateHandle declaration;
            { auto p = publication.BeginPublication(); declaration = templates.Create(p, std::move(definition)).value(); }
            MaterialTemplateView templateView;
            { auto f = publication.BeginFrame(); templateView = templates.Acquire(f, declaration).value(); }
            auto instance = MaterialInstance::Create(templateView).value();
            { auto p = publication.BeginPublication(); material[i] = materials.Create(p, std::move(instance)).value(); }
        }
        auto builder = RenderFrameBuilder::Create({3,9,0,3}).value();
        auto camera = Camera(); camera.visibleLayers = 1;
        Check(builder.AddCamera(camera), "Visibility camera");
        camera.visibleLayers = 2; Check(builder.AddCamera(camera), "Independent second camera");
        camera.visibleLayers = ~0u; camera.projection = glm::mat4(0);
        Check(builder.AddCamera(camera), "Degenerate finite camera for fallback");
        {
            auto access = publication.BeginFrame();
            Check(SDL_GL_MakeCurrent(window, nullptr) == 0, "Detach for visibility preparation");
            for (int i = 0; i < 3; ++i)
            {
                auto prepared = PreparedMaterialBinding::Prepare(materials.Acquire(access, material[i]).value(),
                    access, {programs,textures,samplers}).value();
                Check(builder.AddResources(meshes.Acquire(access, mesh).value(), std::move(prepared)), "Visibility exact resources");
            }
        }
        for (std::size_t i = 0; i < 9; ++i)
        {
            FrameDrawDesc draw; draw.entity = {static_cast<std::uint32_t>(i),1,800};
            draw.resources = i % 3; draw.layers = 1; draw.sortKey = 9-i;
            if (i == 3) draw.worldTransform[3].x = 100; // Offscreen opaque caster.
            if (i == 4) draw.layers = 2; // Masked caster hidden only by camera layers.
            if (i == 6) draw.layers = 0; // Also remains a potential shadow caster.
            if (i == 7) { draw.castShadows = false; draw.receiveShadows = false; draw.pickable = false; }
            if (i == 8) draw.submesh = 1; // Proven empty range, no pass work.
            Check(builder.AddDraw(draw), "Visibility draw");
        }
        auto frame = std::move(builder).Finalize().value();
        const auto listIs = [](std::span<const std::size_t> list, std::initializer_list<std::size_t> expected)
        { return std::equal(list.begin(),list.end(),expected.begin(),expected.end()); };
        {
            auto lists = RenderVisibility::Build(frame).value();
            Check(listIs(lists.Main(),{0,1,2,5,7}), "Main source order without sorting");
            Check(listIs(lists.Opaque(),{0}) && listIs(lists.Masked(),{1,7}) && listIs(lists.Transparent(),{2,5}),
                "Exact opaque/masked/transparent classification from retained pipeline");
            Check(listIs(lists.Shadows(),{0,1,3,4,6}), "Offscreen and camera-layer-filtered casters remain independent");
            Check(listIs(lists.Picking(),{0,1,2,5}), "Picking visibility and flags; receiveShadows does not filter main");
            const auto stats = lists.Stats();
            Check(stats.inputDraws==9 && stats.emptyDraws==1 && stats.layerRejected==2 && stats.tested==6
                && stats.visible==5 && stats.culled==1 && stats.conservative==0, "Exact tested/visible/culled/filter counters");
            Check(lists.StorageBytes()==9*6*sizeof(std::size_t), "Bounded index storage, no heavy assets");
            auto second = RenderVisibility::Build(frame,1).value();
            Check(second.CameraIndex()==1 && listIs(second.Main(),{4}) && listIs(second.Shadows(),{0,1,3,4,6}),
                "Per-camera main and identical full shadow candidate set");
            auto fallback = RenderVisibility::Build(frame,2).value();
            Check(fallback.Stats().visible==7 && fallback.Stats().culled==0 && fallback.Stats().conservative==7,
                "Degenerate camera retains every layer-eligible candidate and counts fallback");
            auto failure = RenderVisibility::Build(frame,3);
            Check(!failure && failure.error().code==VisibilityErrorCode::InvalidCamera && failure.error().element==3,
                "Out-of-range camera is a typed error");
            const std::size_t exempt[]{3,4,8};
            auto conservative = RenderVisibility::Build(frame,0,exempt).value();
            Check(listIs(conservative.Main(),{0,1,2,3,5,7}) && conservative.Stats().culled==0
                && conservative.Stats().conservative==1 && listIs(conservative.Shadows(),{0,1,3,4,6}),
                "Shader-relative geometry bypasses frustum but still obeys layers/empty ranges");
            for (const std::array<std::size_t,2> invalid : {std::array<std::size_t,2>{3,3}, {3,2}, {3,9}})
            {
                auto bad = RenderVisibility::Build(frame,0,invalid);
                Check(!bad && bad.error().code==VisibilityErrorCode::InvalidConservativeDraw && bad.error().element==1,
                    "Unsorted/duplicate/out-of-range conservative draw is a typed failure");
            }
            const auto* address = lists.Main().data(); auto moved = std::move(lists);
            Check(lists.Main().empty() && lists.Shadows().empty() && lists.StorageBytes()==0 && moved.Main().data()==address,
                "Visibility move empties source and transfers index buffers");
            second = std::move(moved);
            Check(moved.Main().empty() && second.Main().data()==address, "Visibility move assignment");
        }
        {
            using namespace AllocationProbe;
            attempts=0; failAt=0; active=true;
            auto denied = RenderVisibility::Build(frame);
            Check(!denied && denied.error().code==VisibilityErrorCode::AllocationFailed && live==0 && attempts==1,
                "Typed visibility allocation denial leaves frame intact and leaks nothing");
            failAt=-1; attempts=0;
            { auto retry=RenderVisibility::Build(frame); Check(retry && attempts==1 && live==1, "Visibility allocation retry"); }
            Check(live==0, "Visibility storage released"); active=false;
        }
        auto emptyBuilder=RenderFrameBuilder::Create({1}).value(); Check(emptyBuilder.AddCamera(Camera()), "Empty camera");
        auto emptyFrame=std::move(emptyBuilder).Finalize().value();
        auto empty=RenderVisibility::Build(emptyFrame).value();
        Check(empty.Main().empty() && empty.Shadows().empty() && empty.StorageBytes()==0 && empty.Stats().inputDraws==0,
            "Empty frame visibility allocates nothing");
        Check(SDL_GL_MakeCurrent(window,context)==0, "Restore for version replacement");
        {
            auto p=publication.BeginPublication();
            Check(meshes.Replace(p,mesh,GpuMesh::Create(MeshSource(200)).value()), "Replace mesh after extraction");
            for (auto handle : material) Check(materials.Destroy(p,handle), "Destroy authoring material after extraction");
        }
        std::thread worker([&] {
            auto lists=RenderVisibility::Build(frame).value();
            Check(listIs(lists.Main(),{0,1,2,5,7}) && listIs(lists.Shadows(),{0,1,3,4,6}),
                "Worker visibility uses retained bounds/materials after publication without context");
        }); worker.join();
        Check(frame.Draws().size()==9 && frame.Draws()[3].worldTransform[3].x==100
            && frame.Resources()[0].Mesh().Revision()==1, "Visibility never mutates finalized frame");
        std::println("[PASS] visibility pass lists, flags, counters, failure/retry, moves, retained versions and worker CPU use");
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
    Frusta();
    Check(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL init");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window = SDL_CreateWindow("Frame schema validation",0,0,32,32,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    Check(window, "Hidden window"); auto context = SDL_GL_CreateContext(window); Check(context, "Context");
    Check(gladLoadGLLoader(SDL_GL_GetProcAddress), "GL loader");
    Resources(window, context);
    VisibilityResources(window, context);
    Check(glGetError() == GL_NO_ERROR, "No GL errors");
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    std::println("[PASS] render-frame checks={} sizes frame={} draw={} camera={} debug={} resources={}",
        checks, sizeof(RenderFrame), sizeof(DrawItem), sizeof(FrameCamera), sizeof(FrameDebugLine), sizeof(FrameResources));
}
#endif
