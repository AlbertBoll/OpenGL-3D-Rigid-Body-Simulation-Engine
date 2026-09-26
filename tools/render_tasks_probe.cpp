#include "Renderer/RenderTasks.h"
#include <type_traits>
using namespace GEngine;
using namespace GEngine::Asset;
using namespace GEngine::Component;
static_assert(!std::is_copy_constructible_v<RenderTaskFrame> && !std::is_move_constructible_v<RenderTaskFrame>);
static_assert(!std::is_copy_constructible_v<RenderMutationQueue>);
static_assert(std::same_as<decltype(std::declval<const RenderTaskFrame&>().Inputs()), std::span<const FrozenRenderEntity>>);
#ifndef TASK_SCHEMA_ONLY
#include <concepts>
#include <functional>
#include <iostream>
#include "Scene/_Entity.h"
#include "Core/GEngine.h"
#include "Core/RuntimeAssets.h"
#include "Core/Window.h"
#include "Core/GLContextThread.h"
#include <glad/glad.h>
#include <sdl2/SDL.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <print>
#include <new>
#include <thread>
#include <cstring>
#undef _beginthreadex
#include <process.h>
namespace Injection { bool denyArray{}, trackArrays{}; int arrayCalls=0, failArray=-1; std::atomic<int> launchCount{0}; int failLaunch=-1; }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    if (Injection::denyArray || (Injection::trackArrays && Injection::arrayCalls++ == Injection::failArray)) return nullptr;
    return ::operator new(size, std::nothrow);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
#ifdef TASK_LAUNCH_FAULTS
// <thread> may have included process.h while the test-only redirection macro
// was active. Declare the original CRT entry point for the forwarding wrapper.
extern "C" uintptr_t __cdecl _beginthreadex(void*,unsigned,unsigned (__stdcall *)(void*),void*,unsigned,unsigned*);
extern "C" uintptr_t __cdecl TaskProbeBeginThread(void* security, unsigned stack,
    unsigned (__stdcall *entry)(void*), void* data, unsigned flags, unsigned* id)
{
    if (Injection::launchCount++ == Injection::failLaunch) { errno=EAGAIN; return 0; }
    return _beginthreadex(security,stack,entry,data,flags,id);
}
#endif
namespace
{
    // Test-only preparation for bounded Scene value/void result migrations.
    template<std::invocable Operation>
    auto SceneOperationChecked(Operation&& operation)
    {
        using Result = std::remove_cvref_t<std::invoke_result_t<Operation>>;
        if constexpr (std::is_void_v<Result>) {
            std::invoke(std::forward<Operation>(operation));
        } else {
            auto result = std::invoke(std::forward<Operation>(operation));
            if constexpr (requires { typename Result::error_type; typename Result::value_type; }) {
                if (!result) {
                    const auto& error = result.error();
                    std::cerr << "[FAIL] Valid Scene fixture: operation=" << error.operation
                        << " code=" << static_cast<unsigned>(error.code) << " entity=" << error.entity
                        << ": " << error.message << '\n';
                    std::exit(1);
                }
                if constexpr (std::is_void_v<typename Result::value_type>) return;
                else return std::move(*result);
            } else return result;
        }
    }

    int checks{};
    template<class T> void Check(const T& value, const char* message)
    { ++checks; if (!static_cast<bool>(value)) { std::println(stderr,"[FAIL] {}",message); std::exit(1); } }
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
        AssetPublication& publication;
        ShaderProgramRegistry programs{publication}; TextureRegistry textures{publication}; SamplerRegistry samplers{publication};
        PipelineRegistry pipelines{publication}; MaterialTemplateRegistry templates{publication};
        MaterialInstanceRegistry materials{publication}; MeshRegistry meshes{publication};
        _Scene scene;
        ShaderProgramHandle program; TextureHandle texture; SamplerHandle sampler;
        MeshHandle mesh; MaterialInstanceHandle material; MaterialTemplateView declaration;
        explicit Fixture(AssetPublication& domain) : publication(domain)
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
            auto entity = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(UUID(uuid)); });
            auto id = scene.RenderData().Identify(entity).value();
            Check(scene.RenderData().Add(id, MeshRendererComponent{mesh, material, submesh}), "Author mesh intent");
            return {entity, id};
        }
        std::pair<_Entity, EntityRenderId> Light(std::uint64_t uuid, RenderLightKind kind)
        {
            auto entity = SceneOperationChecked([&] { return scene.CreateEntityWithUUID(UUID(uuid)); });
            auto id = scene.RenderData().Identify(entity).value();
            RenderLightComponent intent; intent.kind = kind;
            Check(scene.RenderData().Add(id, intent), "Author typed light intent without mesh");
            return {entity, id};
        }
    };
    struct Command final : RenderMutation
    {
        using Function = std::function<std::expected<void,RenderWorkError>(_Scene&,const AssetPublication::Publication&)>;
        Function function;
        explicit Command(Function fn) : function(std::move(fn)) {}
        std::expected<void,RenderWorkError> Apply(_Scene& scene,const AssetPublication::Publication& token) noexcept override
        { return function(scene,token); }
    };
    void Queue(RenderMutationQueue& queue,Command::Function function)
    {
        std::unique_ptr<RenderMutation> command=std::make_unique<Command>(std::move(function));
        Check(queue.Enqueue(command) && !command,"Queue transfers CPU payload only on success");
    }
    auto Prepare(Fixture& f,const AssetPublication::FrameAccess& access)
    { return RenderTaskFrame::Prepare(f.scene,{access,f.meshes,f.materials,{f.programs,f.textures,f.samplers},{}}); }
    struct Work
    {
        std::vector<std::size_t> output;
        std::array<std::size_t,64> laneCounts{};
        std::atomic<int> active{0};
        std::atomic<bool> entered{false}, bad{false};
        bool cancelMode{}, failMode{}, nested{};
        RenderTaskFrame* frame{};
        std::thread::id owner=std::this_thread::get_id();
    };
    std::expected<void,RenderWorkError> Process(const RenderTaskRange& range,void* user) noexcept
    {
        auto& work=*static_cast<Work*>(user);
        ++work.active;
        struct Exit { Work& work; ~Exit(){--work.active;} } exit{work};
        if (std::this_thread::get_id()!=work.owner && GLContextThread::IsCurrentOwner()) work.bad=true;
        work.entered.store(true,std::memory_order_release); work.entered.notify_all();
        if (work.cancelMode)
            while (!range.StopRequested()) std::this_thread::yield();
        if (range.StopRequested()) return {};
        if (work.nested)
        {
            RenderTaskScratch inner;
            auto result=work.frame->Run({0},{&inner,1},Process,user);
            if (result || result.error().code!=RenderWorkCode::FrameActive) work.bad=true;
        }
        if (work.failMode && (range.lane==0 || range.lane==2))
            return std::unexpected(RenderWorkError{RenderWorkCode::TaskFailed,77,
                FrameError{FrameErrorCode::InvalidDraw,FrameSection::Draws,42+range.lane}});
        work.laneCounts[range.lane]=range.entities.size();
        for (std::size_t i=0;i<range.entities.size();++i)
        {
            const auto& entry=range.entities[i];
            if (!entry.mesh || !entry.state.mesh || !entry.state.material || entry.state.mesh.Revision()!=1
                || entry.state.mesh->Bounds().maximum[0]!=1) work.bad=true;
            work.output[range.first+i]=entry.state.entity.index;
        }
        std::fill(range.scratch.begin(),range.scratch.end(),std::byte(range.lane+1));
        return {};
    }
    void RunContract(Fixture& f)
    {
        auto emptyAccess=f.publication.BeginFrame();
        auto empty=Prepare(f,emptyAccess).value();
        Work work;
        Check(empty->Run({64},{},Process,&work)->lanes==0,"Empty snapshot launches no workers");
    }
    void Tasks(Fixture& f)
    {
        for (std::uint64_t i=0;i<129;++i) f.Entity(i+1);
        auto first=f.scene.GetEntityByUUID(UUID(1));
        first.AddComponent<RenderCameraComponent>();
        auto id=f.scene.RenderData().Identify(first).value();
        Check(f.scene.RenderData().Add(id,VisibilityComponent{}),"Author replaceable visibility value");
        auto queue=RenderMutationQueue::Create(8).value();
        std::vector<int> applied;
        {
            auto access=f.publication.BeginFrame();
            auto frame=Prepare(f,access).value();
            Check(frame->Inputs().size()==129 && frame->Inputs()[0].camera.has_value(),"Complete copied camera/mesh/value snapshot");
            Check(f.scene.RenderData().IsExtracting() && !f.publication.CanPublish(),"Both frame input domains are frozen");
            Check(!Prepare(f,access),"Nested snapshot rejected before cache work");
            Check(!f.scene.RenderData().Replace(id,VisibilityComponent{false,0}),"Component value mutation rejected while frozen");
            Check(!f.scene.RenderData().Remove<MeshRendererComponent>(id),"Structural component mutation rejected while frozen");
            std::array<std::array<std::byte,32>,64> bytes{};
            std::array<RenderTaskScratch,64> scratch{};
            for (std::size_t i=0;i<64;++i) scratch[i].bytes=bytes[i];
            Work work;work.frame=frame.get();work.output.resize(129);
            for (unsigned workers:{0u,1u,2u,4u,64u})
            {
                const auto lanes=frame->LaneCount({workers});
                auto result=frame->Run({workers},std::span(scratch).first(lanes),Process,&work);
                Check(result && result->lanes==lanes && result->executionThreads==lanes && result->entities==129
                    && !result->serialFallback && work.active==0 && !work.bad,"Zero/one/many execution and joined merge barrier");
                std::size_t offset=0;
                for (std::size_t lane=0;lane<lanes;++lane)
                {
                    Check(bytes[lane].front()==std::byte(lane+1) && bytes[lane].back()==std::byte(lane+1),"Disjoint lane scratch");
                    for (std::size_t i=0;i<work.laneCounts[lane];++i)
                    { Check(work.output[offset]==frame->Inputs()[offset].state.entity.index,"Deterministic lane-order merge"); ++offset; }
                }
                Check(offset==129,"Every input processed exactly once");
            }
            auto bad=frame->Run({65},{},Process,&work);
            Check(!bad && bad.error().code==RenderWorkCode::InvalidWorkers,"Unsupported worker count typed failure");
            bad=frame->Run({2},{},Process,&work);
            Check(!bad && bad.error().code==RenderWorkCode::InvalidScratch,"Missing scratch typed failure");
            RenderTaskScratch overlap[]{scratch[0],scratch[0]};
            bad=frame->Run({2},overlap,Process,&work);
            Check(!bad && bad.error().code==RenderWorkCode::InvalidScratch,"Overlapping scratch rejected");
            bad=frame->Run({0},std::span(scratch).first(1),nullptr,&work);
            Check(!bad && bad.error().code==RenderWorkCode::InvalidCallback,"Missing callback typed failure");
            work.failMode=true;
            bad=frame->Run({4},std::span(scratch).first(4),Process,&work);
            Check(!bad && bad.error().code==RenderWorkCode::TaskFailed && bad.error().element==77
                && bad.error().boundary==0 && std::get<FrameError>(bad.error().cause).element==42 && work.active==0,
                "Typed worker failure preserves cause and selects deterministic lane after join");
            work.failMode=false;work.nested=true;
            Check(frame->Run({0},std::span(scratch).first(1),Process,&work) && !work.bad,"Nested scheduling rejected");
            work.nested=false;
            std::thread producer([&] {
                std::unique_ptr<RenderMutation> command=std::make_unique<Command>([&](auto& scene,const auto& token)->std::expected<void,RenderWorkError> {
                    applied.push_back(1);
                    auto changed=scene.RenderData().Replace(id,VisibilityComponent{false,2});
                    if (!changed) return std::unexpected(RenderWorkError{RenderWorkCode::MutationFailed,0,changed.error()});
                    auto uploaded=f.meshes.Replace(token,f.mesh,GpuMesh::Create(MeshSource(2)).value());
                    if (!uploaded) return std::unexpected(RenderWorkError{RenderWorkCode::MutationFailed,0,uploaded.error()});
                    return {};
                });
                if (!queue->Enqueue(command)) work.bad=true;
            });producer.join();
            Check(!work.bad && queue->Pending()==1 && !queue->Drain(f.scene,f.publication),"Completion cannot publish during extraction");
            Check(frame->Inputs()[0].visibility.enabled && frame->Inputs()[0].state.mesh.Revision()==1,
                "Queued value and upload replacement leave current snapshot unchanged");
            work.cancelMode=true;work.entered=false;
            std::thread cancel([&]{work.entered.wait(false,std::memory_order_acquire);frame->RequestCancel();});
            bad=frame->Run({4},std::span(scratch).first(4),Process,&work);cancel.join();
            Check(!bad && bad.error().code==RenderWorkCode::Cancelled && work.active==0,"Mid-batch cancellation joins every callback");
            Check(!frame->Run({0},std::span(scratch).first(1),Process,&work),"Cancelled frame cannot start another batch");
            frame.reset();
            Check(!f.scene.RenderData().IsExtracting() && !queue->Drain(f.scene,f.publication),
                "Resource publication stays frozen through CPU submission after ECS snapshot release");
        }
        auto published=queue->Drain(f.scene,f.publication);
        if (!published) std::println(stderr,"drain error code={} element={} cause={} boundary={}",
            int(published.error().code),published.error().element,published.error().cause.index(),published.error().boundary);
        Check(published==1 && applied==std::vector<int>{1},"Next safe point applies queued upload/value edits");
        {
            auto access=f.publication.BeginFrame();auto frame=Prepare(f,access).value();
            Check(!frame->Inputs()[0].visibility.enabled && frame->Inputs()[0].visibility.layers==2
                && frame->Inputs()[0].state.mesh.Revision()==2 && frame->Inputs()[0].state.mesh->Bounds().maximum[0]==2,
                "Next snapshot sees complete replacement publication and component values");
            frame->RequestCancel();Work work;
            RenderTaskScratch scratch;
            auto result=frame->Run({1},{&scratch,1},Process,&work);
            Check(!result && result.error().code==RenderWorkCode::Cancelled && !work.entered,"Pre-start cancellation launches no callback");
        }
        Queue(*queue,[&](auto& scene,const auto&)->std::expected<void,RenderWorkError>{
            SceneOperationChecked([&] { return scene.DestroyEntity(scene.GetEntityByUUID(UUID(1))); });applied.push_back(2);
            std::thread completion([&]{
                std::unique_ptr<RenderMutation> later=std::make_unique<Command>([&](auto& laterScene,const auto&)->std::expected<void,RenderWorkError>{
                    SceneOperationChecked([&] { return laterScene.CreateEntityWithUUID(UUID(999)); });applied.push_back(3);return {};
                });if (!queue->Enqueue(later)) std::abort();
            });completion.join();return {};
        });
        Check(queue->Drain(f.scene,f.publication)==1 && queue->Pending()==1 && applied==std::vector<int>({1,2}),
            "Completion after drain cutoff waits for next frame");
        Check(queue->Drain(f.scene,f.publication)==1 && applied==std::vector<int>({1,2,3}),"Deferred structural creation applied once");
        Queue(*queue,[](auto&,const auto&)->std::expected<void,RenderWorkError>{
            return std::unexpected(RenderWorkError{RenderWorkCode::MutationFailed,91,RenderEcsError::StaleId});
        });
        Queue(*queue,[&](auto&,const auto&)->std::expected<void,RenderWorkError>{applied.push_back(4);return {};});
        auto failure=queue->Drain(f.scene,f.publication);
        Check(!failure && failure.error().boundary==0 && failure.error().element==91
            && std::get<RenderEcsError>(failure.error().cause)==RenderEcsError::StaleId && queue->Pending()==1,
            "Failed command preserves diagnostics and untouched suffix");
        queue->CancelPending();Check(queue->Pending()==0 && applied.back()==3,"Cancel queued commands without applying them");
        auto zero=RenderMutationQueue::Create(0).value();
        std::unique_ptr<RenderMutation> command=std::make_unique<Command>([](auto&,const auto&)->std::expected<void,RenderWorkError>{return {};});
        Check(!zero->Enqueue(command) && command,"Full queue preserves caller payload");
        Injection::denyArray=true;
        Check(!RenderMutationQueue::Create(2),"Queue storage allocation failure is typed");
        Injection::denyArray=false;
        Injection::trackArrays=true;Injection::arrayCalls=0;
        { auto access=f.publication.BeginFrame();auto ready=Prepare(f,access);Check(ready,"Prepare array fault fixture"); }
        const int sites=Injection::arrayCalls;Check(sites>0,"Preparation has faultable array owners");
        for(int site=0;site<sites;++site) {
            Injection::arrayCalls=0;Injection::failArray=site;
            { auto access=f.publication.BeginFrame();auto failed=Prepare(f,access);
              Check(!failed,"Every preparation array failure returns no partial task frame");
              const auto* cause=std::get_if<TransformError>(&failed.error().cause);
              Check(failed.error().code==RenderWorkCode::Allocation
                  || (failed.error().code==RenderWorkCode::InvalidInput && cause
                      && cause->code==TransformErrorCode::AllocationFailed),"Complete typed preparation allocation error");
              Check(!f.scene.RenderData().IsExtracting(),"Snapshot allocation failure releases ECS freeze"); }
            Check(f.publication.CanPublish(),"Failed preparation releases publication pin");
        }
        Injection::failArray=-1;Injection::trackArrays=false;
        { auto access=f.publication.BeginFrame();Check(Prepare(f,access),"Retry after preparation allocation failure"); }
    }
    void Faults(Fixture& f)
    {
        for (std::uint64_t i=0;i<16;++i) f.Entity(i+1);
        auto access=f.publication.BeginFrame();auto frame=Prepare(f,access).value();
        Work work;work.output.resize(16);std::array<RenderTaskScratch,4> scratch{};
        for (int fail : {0,1,2})
        {
            Injection::failLaunch=fail;Injection::launchCount=0;work.entered=false;
            auto denied=frame->Run({4,false},scratch,Process,&work);
            Check(!denied && denied.error().code==RenderWorkCode::WorkerLaunch && !work.entered && work.active==0,
                "Partial worker launch failure joins gated threads before any callback");
            Injection::launchCount=0;
            auto fallback=frame->Run({4,true},scratch,Process,&work);
            Check(fallback && fallback->serialFallback && fallback->executionThreads==1 && fallback->launchDiagnostic
                && !work.bad && work.active==0,"Explicit serial fallback reports launch diagnostic and keeps lane contract");
        }
        Injection::failLaunch=-1;
    }
    void Negative(Fixture& f,const char* mode,std::unique_ptr<EngineContext>& root)
    {
        auto [entity,id]=f.Entity(1);
        std::unique_ptr<AssetPublication::FrameAccess> access(new AssetPublication::FrameAccess(f.publication.BeginFrame()));
        auto frame=Prepare(f,*access).value();
        std::set_terminate([]{std::println("[PASS] freeze-invariant");std::_Exit(71);});
        if (!std::strcmp(mode,"lazy")) (void)f.scene.GetRenderTransform(entity);
        if (!std::strcmp(mode,"view")) (void)f.scene.GetAllEntitiesWith<Transform3DComponent>();
        if (!std::strcmp(mode,"value")) (void)entity.GetComponent<Transform3DComponent>();
        if (!std::strcmp(mode,"structure")) SceneOperationChecked([&] { return f.scene.CreateEntity(); });
        if (!std::strcmp(mode,"settings")) f.scene.SetRenderInterpolationEnabled(false);
        if (!std::strcmp(mode,"physics")) f.scene.SetPaused(true);
        if (!std::strcmp(mode,"publication")) (void)f.publication.BeginPublication();
        if (!std::strcmp(mode,"pin")) access.reset();
        if (!std::strcmp(mode,"root")) root.reset();
        if (!std::strcmp(mode,"worker-registry"))
        {
            std::thread worker([&]{
                std::set_terminate([]{std::println("[PASS] freeze-invariant");std::_Exit(71);});
                (void)f.scene.RenderData().Get<MeshRendererComponent>(id);
            });worker.join();
        }
        std::println(stderr,"[FAIL] invariant was not enforced");std::exit(2);
    }
}
int main(int argc,char** argv)
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    RuntimeAssets::Initialize("RigidBodySimulation");
    for (int cycle=0;cycle<2;++cycle)
    {
        auto root=std::make_unique<EngineContext>();
        WindowProperties properties;properties.m_Title="Phase 44 hidden freeze validation";
        properties.flag={WindowFlags::INVISIBLE};properties.m_Width=properties.m_Height=64;
        properties.m_MinWidth=properties.m_MinHeight=64;properties.m_IsVsync=false;
        Check(root->Initialize({properties}),"Root initialization");
        {
            Fixture fixture(root->SceneServices().value().publication);
            if (argc>1) Negative(fixture,argv[1],root);
#ifdef TASK_LAUNCH_FAULTS
            Faults(fixture);
#else
            RunContract(fixture);Tasks(fixture);
#endif
            Check(glGetError()==GL_NO_ERROR,"No worker GL or context errors");
        }
        root.reset();
        Check(!EngineContext::TryGet(),"All tasks and snapshots ended before EngineContext teardown");
    }
    std::println("[PASS] render-tasks checks={}",checks);
}
#endif
