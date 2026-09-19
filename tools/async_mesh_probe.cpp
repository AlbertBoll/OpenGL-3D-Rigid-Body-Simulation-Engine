#include "Mesh/AsyncMesh.h"
#include "Renderer/FrameScheduler.h"
#include <type_traits>
using namespace GEngine;
using namespace GEngine::Asset;
static_assert(!std::is_copy_constructible_v<AsyncMeshLoader>);
static_assert(std::same_as<decltype(std::declval<AsyncMeshLoader&>().Request("file")), std::expected<UploadTicket, AsyncMeshError>>);
#ifndef MESH_SCHEMA_ONLY
#include "Core/GEngine.h"
#include "Core/RuntimeAssets.h"
#include "Core/GLContextThread.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <print>
#include <thread>
#include <unordered_set>
#include <crtdbg.h>
using namespace std::chrono_literals;

// A deterministic allocation gate proves Request and frames progress while the
// worker is still preparing the import. It does not change production scheduling.
static std::thread::id ownerThread;
static std::atomic<bool> gate{}, reached{};
static std::atomic<std::size_t> workerAllocated{};
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept
{
    if (std::this_thread::get_id() != ownerThread) {
        workerAllocated += bytes;
        if (gate.load()) { reached = true; while (gate.load()) std::this_thread::yield(); }
    }
    return std::malloc(bytes ? bytes : 1);
}
void* operator new[](std::size_t bytes, const std::nothrow_t& tag) noexcept { return ::operator new(bytes, tag); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace
{
    std::atomic<int> checks{};
    template<class T> void Check(const T& value, const char* label)
    { ++checks; if (!static_cast<bool>(value)) { std::println(stderr,"[FAIL] {}",label); std::exit(1); } }
    template<class T,class E> T Take(std::expected<T,E> value,const char* label)
    {
        if (!value) {
            if constexpr (std::same_as<E,AsyncMeshError>) std::println(stderr,"{}",DescribeAsyncMeshError(value.error()));
            if constexpr (std::same_as<E,ScheduleError>) std::println(stderr,"{}",DescribeScheduleError(value.error()));
        }
        Check(value,label); return std::move(*value);
    }
    template<class F> void Wait(F predicate,const char* label="worker deadline")
    {
        const auto deadline=std::chrono::steady_clock::now()+15s;
        while (!predicate()) { Check(std::chrono::steady_clock::now()<deadline,label); std::this_thread::yield(); }
    }
    AsyncMeshLimits Limits()
    {
        AsyncMeshLimits limits; limits.requestBytes=1024*1024;
        limits.queue={32,2,16,16*1024*1024,16,16*1024*1024,4*1024*1024,1s}; return limits;
    }
    AsyncMeshStatus Await(AsyncMeshLoader& loader,UploadTicket ticket,AsyncAssetState state)
    {
        const auto deadline=std::chrono::steady_clock::now()+15s;
        for (;;) {
            auto value=Take(loader.Status(ticket),"mesh status");
            if (value.state==state) return value;
            if (value.state==AsyncAssetState::Failed || std::chrono::steady_clock::now()>=deadline) {
                std::println(stderr,"state={} expected={} source={}",int(value.state),int(state),value.source.string());
                if(value.error) std::println(stderr,"{}",DescribeAsyncMeshError(*value.error));
                Check(false,"async state transition");
            }
            std::this_thread::yield();
        }
    }
    void Queued(AsyncMeshLoader& loader,std::size_t count)
    { Wait([&] { return loader.Queue().Stats().queuedRequests==count; },"CPU queue deadline"); }
    void Drain(EngineContext& root,AsyncMeshLoader& loader)
    {
        RenderContext context{*root.MainWindow(),*root.LegacyEngine().GetWindowManager()};
        context.visible=false; context.uploads=&loader.Queue();
        Take(FrameScheduler::Render(context),"mesh scheduler drain");
    }
    MeshView View(AssetPublication& pub,MeshRegistry& registry,MeshHandle handle)
    { auto frame=pub.BeginFrame(); return Take(registry.Acquire(frame,handle),"mesh view"); }
    struct Observer
    {
        inline static PFNGLGENBUFFERSPROC gen;
        inline static PFNGLDELETEBUFFERSPROC del;
        inline static PFNGLGENVERTEXARRAYSPROC genArray;
        inline static PFNGLDELETEVERTEXARRAYSPROC delArray;
        inline static PFNGLBUFFERDATAPROC data;
        inline static std::unordered_set<GLuint> buffers,arrays;
        inline static GLuint vertex{},index{};
        inline static bool denyArray{};
        inline static std::size_t uploads{};
        static void Owner() { Check(std::this_thread::get_id()==ownerThread && GLContextThread::IsCurrentOwner(),"no worker GL"); }
        static void APIENTRY Gen(GLsizei n,GLuint* names)
        { Owner(); gen(n,names); for(int i=0;i<n;++i) Check(buffers.insert(names[i]).second,"unique buffer owner"); }
        static void APIENTRY Del(GLsizei n,const GLuint* names)
        { Owner(); for(int i=0;i<n;++i) Check(buffers.erase(names[i])==1,"buffer retired exactly once"); del(n,names); }
        static void APIENTRY GenArray(GLsizei n,GLuint* names)
        {
            Owner(); if(std::exchange(denyArray,false)) { std::fill_n(names,n,0); return; }
            genArray(n,names); for(int i=0;i<n;++i) Check(arrays.insert(names[i]).second,"unique VAO owner");
        }
        static void APIENTRY DelArray(GLsizei n,const GLuint* names)
        { Owner(); for(int i=0;i<n;++i) Check(arrays.erase(names[i])==1,"VAO retired exactly once"); delArray(n,names); }
        static void APIENTRY Data(GLenum target,GLsizeiptr bytes,const void* payload,GLenum usage)
        {
            Owner(); ++uploads; GLint name{};
            if(target==GL_ARRAY_BUFFER) {glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&name);vertex=GLuint(name);}
            if(target==GL_ELEMENT_ARRAY_BUFFER) {glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&name);index=GLuint(name);}
            data(target,bytes,payload,usage);
        }
        Observer()
        {
            gen=glad_glGenBuffers;glad_glGenBuffers=Gen; del=glad_glDeleteBuffers;glad_glDeleteBuffers=Del;
            genArray=glad_glGenVertexArrays;glad_glGenVertexArrays=GenArray;
            delArray=glad_glDeleteVertexArrays;glad_glDeleteVertexArrays=DelArray;
            data=glad_glBufferData;glad_glBufferData=Data;
        }
        ~Observer()
        {
            Check(buffers.empty() && arrays.empty(),"all async GPU owners retired before context teardown");
            glad_glGenBuffers=gen;glad_glDeleteBuffers=del;glad_glGenVertexArrays=genArray;
            glad_glDeleteVertexArrays=delArray;glad_glBufferData=data;
        }
    };
    void EqualBuffer(GLuint name,std::span<const std::byte> expected)
    {
        GLint prior{};glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&prior);glBindBuffer(GL_ARRAY_BUFFER,name);
        std::vector<std::byte> actual(expected.size());
        glGetBufferSubData(GL_ARRAY_BUFFER,0,static_cast<GLsizeiptr>(actual.size()),actual.data());
        glBindBuffer(GL_ARRAY_BUFFER,GLuint(prior));
        Check(std::equal(actual.begin(),actual.end(),expected.begin(),expected.end()),"uploaded bytes match immutable CPU reference");
    }
    void CpuBudget(const std::filesystem::path& folder)
    {
        const auto path=(folder/"single.obj").string();
        auto denied=ImportMeshFile(path.c_str(),{},MeshImportControl{1});
        Check(!denied && denied.error().code==MeshImportErrorCode::MemoryBudgetExceeded,"adapter budget before allocation");
        const MeshImportVector positions[]{{0,0,0},{1,0,0},{0,1,0}};
        const std::uint32_t indices[]{0,1,2};
        MeshImportPart part;part.positions=positions;part.indices=indices;
        denied=NormalizeImportedMesh({&part,1},1,{},MeshImportControl{1});
        Check(!denied && denied.error().code==MeshImportErrorCode::MemoryBudgetExceeded,"normalization scratch budget before allocation");
        denied=NormalizeImportedMesh({&part,1},1,{},MeshImportControl{SIZE_MAX,nullptr,[](const void*) noexcept {return true;}});
        Check(!denied && denied.error().code==MeshImportErrorCode::Cancelled,"typed CPU cancellation");
        auto asset=Take(NormalizeImportedMesh({&part,1},1),"missing attributes generated");
        Check(asset.Layout().attributes.size()==5 && !asset.Bounds().empty,"complete generated frames and bounds");
        for(const auto& attribute:asset.Layout().attributes) {
            if(attribute.semantic==VertexSemantic::Position || attribute.semantic==VertexSemantic::TexCoord0) continue;
            std::array<float,3> v{};std::memcpy(v.data(),asset.Vertices().data()+attribute.offsetBytes,sizeof(v));
            Check(std::abs(std::hypot(v[0],v[1],v[2])-1.f)<1e-5f,"generated normal/tangent/bitangent normalized");
        }
    }
    void Run(EngineContext& root,const std::filesystem::path& folder)
    {
        CpuBudget(folder);
        auto& pub=root.AssetPublications(); MeshRegistry registry(pub);
        Check(!AsyncMeshLoader::Create(pub,registry,"relative",Limits()),"invalid root rejected");
        auto loader=Take(AsyncMeshLoader::Create(pub,registry,folder,Limits()),"mesh loader");
        Check(!loader->Request({}) && !loader->Status({}),"invalid request/ticket rejected");
        workerAllocated=0;gate=true;reached=false;
        const auto single=Take(loader->Request("single.obj"),"single request");
        Check(single==Take(loader->Request("./single.obj"),"lexical request"),"lexical coalescing");
        Wait([] {return reached.load();},"initial allocation gate deadline");
        Check(Take(loader->Status(single),"loading status").state==AsyncAssetState::Loading,"worker blocked in CPU preparation");
        for(int i=0;i<3;++i) Drain(root,*loader);
        Check(Observer::uploads==0 && registry.Size()==0,"frame owner progresses without waiting for import or creating GPU mesh");
        gate=false;Queued(*loader,1);
        Check(workerAllocated<=Limits().requestBytes,"observed engine allocations fit reservation");
        Check(!Take(loader->Status(single),"CPU-ready handle").mesh,"no early resource publication");
        {
            auto frame=pub.BeginFrame();
            RenderContext context{*root.MainWindow(),*root.LegacyEngine().GetWindowManager()};
            context.visible=false;context.uploads=&loader->Queue();
            Check(!FrameScheduler::Render(context),"frame access forbids queue publication");
            Check(registry.Size()==0,"read interval kept immutable");
        }
        Drain(root,*loader); const auto first=Await(*loader,single,AsyncAssetState::Ready);
        auto reference=Take(ImportMeshFile((folder/"single.obj").string().c_str()),"single CPU reference");
        EqualBuffer(Observer::vertex,reference.Vertices());EqualBuffer(Observer::index,reference.Indices());
        auto held=View(pub,registry,first.mesh);
        Check(held->Submeshes().size()==1 && held->IndexCount()==3 && held->Bounds().maximum[0]==1,"single mesh metadata");
        Check(!loader->Cancel(single),"ready cancellation is too late");
        auto alias=Take(loader->Request("alias.obj"),"file identity alias");Queued(*loader,1);Drain(root,*loader);
        Check(Await(*loader,alias,AsyncAssetState::Ready).mesh==first.mesh && loader->Stats().imports==1,"canonical duplicate one import/upload");

        auto multi=Take(loader->Request("multi.obj"),"multi request");Queued(*loader,1);Drain(root,*loader);
        auto multiState=Await(*loader,multi,AsyncAssetState::Ready);
        auto multiCpu=Take(ImportMeshFile((folder/"multi.obj").string().c_str()),"multi CPU reference");
        EqualBuffer(Observer::vertex,multiCpu.Vertices());EqualBuffer(Observer::index,multiCpu.Indices());
        {auto view=View(pub,registry,multiState.mesh);
            Check(view->Submeshes().size()==2 && view->IndexCount()==6,"multi submeshes preserved");
            for(std::size_t i=0;i<2;++i) Check(view->Submeshes()[i].materialSlot==multiCpu.Submeshes()[i].materialSlot,"material ordinals preserved");
        }
        MeshImportOptions flipped;flipped.flipV=true;
        auto distinct=Take(loader->Request("single.obj",flipped),"different options");Queued(*loader,1);Drain(root,*loader);
        Check(Await(*loader,distinct,AsyncAssetState::Ready).mesh!=first.mesh,"options separate cache entries");
        for(int i=0;i<3;++i) {
            MeshImportOptions options;
            if(i==0) options.normals=MissingMeshNormals::Reject;
            if(i==1) options.uvs=MissingMeshUVs::Reject;
            if(i==2) options.tangents=MissingMeshTangents::Reject;
            const auto ticket=Take(loader->Request("single.obj",options),"missing attribute request");
            const auto state=Await(*loader,ticket,AsyncAssetState::Failed);
            const auto& error=std::get<MeshImportError>(*state.error);
            Check(error.code==MeshImportErrorCode::MissingAttribute && error.field==
                (i==0?MeshImportField::Normal:i==1?MeshImportField::UV:MeshImportField::Tangent),"typed missing attribute cause");
        }
        const auto missing=Take(loader->Request("missing.obj"),"missing file request");
        Check(std::get<UploadError>(*Await(*loader,missing,AsyncAssetState::Failed).error).system,"missing filesystem diagnostic");
        const auto corrupt=Take(loader->Request("corrupt.obj"),"corrupt request");
        Check(std::get<MeshImportError>(*Await(*loader,corrupt,AsyncAssetState::Failed).error).code==MeshImportErrorCode::ReadFailed,"corrupt import diagnostic");
        Check(loader->Cancel(corrupt),"failed cancellation idempotent");
        const auto before=registry.Size();const auto oldBuffers=Observer::buffers.size(),oldArrays=Observer::arrays.size();
        auto gpuFailure=Take(loader->Request("failure.obj"),"GPU failure request");Queued(*loader,1);
        Observer::denyArray=true;Drain(root,*loader);
        auto failed=Await(*loader,gpuFailure,AsyncAssetState::Failed);
        Check(std::get<GpuMeshError>(*failed.error).code==GpuMeshErrorCode::Allocation,"GPU typed error retained across drain");
        Check(registry.Size()==before && Observer::buffers.size()==oldBuffers && Observer::arrays.size()==oldArrays && held->IndexCount()==3,"GPU failure transactional and old lease intact");

        {
            MeshRegistry full(pub,AssetRegistryLimits{1});
            auto publishing=Take(AsyncMeshLoader::Create(pub,full,folder,Limits()),"registry capacity loader");
            auto firstTicket=Take(publishing->Request("single.obj"),"first registry slot");Queued(*publishing,1);Drain(root,*publishing);
            const auto firstHandle=Await(*publishing,firstTicket,AsyncAssetState::Ready).mesh;
            const auto priorBuffers=Observer::buffers.size(),priorArrays=Observer::arrays.size();
            auto secondTicket=Take(publishing->Request("multi.obj"),"exhausted registry request");Queued(*publishing,1);Drain(root,*publishing);
            const auto error=std::get<GpuMeshError>(*Await(*publishing,secondTicket,AsyncAssetState::Failed).error);
            Check(error.code==GpuMeshErrorCode::Registry && error.registry==RegistryError::SlotsExhausted,"publication preserves typed registry cause");
            Check(full.Size()==1 && Observer::buffers.size()==priorBuffers && Observer::arrays.size()==priorArrays,
                "failed publication retires temporary GPU mesh");
            Check(View(pub,full,firstHandle)->IndexCount()==3,"prior registry version intact after failure");
            publishing->Shutdown();
        }

        auto smallLimits=Limits();smallLimits.requestBytes=1024;
        auto small=Take(AsyncMeshLoader::Create(pub,registry,folder,smallLimits),"bounded import loader");
        const auto tooLarge=Take(small->Request("single.obj"),"small budget request");
        Check(std::get<MeshImportError>(*Await(*small,tooLarge,AsyncAssetState::Failed).error).code==MeshImportErrorCode::MemoryBudgetExceeded,"typed engine-memory limit across worker");
        auto cancelled=Take(AsyncMeshLoader::Create(pub,registry,folder,Limits()),"cancel loader");
        gate=true;reached=false;
        auto loading=Take(cancelled->Request("single.obj"),"cancel loading request");Wait([]{return reached.load();});
        Check(cancelled->Cancel(loading),"cancel active CPU work");gate=false;cancelled->Shutdown();
        Await(*cancelled,loading,AsyncAssetState::Cancelled);
        Check(cancelled->Queue().Stats().reservedBytes==0 && cancelled->Stats().uploads==0,"cancel frees CPU storage");
        {
            auto serialLimits=Limits();serialLimits.queue.workers=1;
            auto serial=Take(AsyncMeshLoader::Create(pub,registry,folder,serialLimits),"pending cancellation loader");
            gate=true;reached=false;
            auto active=Take(serial->Request("single.obj"),"active request");Wait([]{return reached.load();});
            auto pending=Take(serial->Request("multi.obj"),"pending request");
            Check(Take(serial->Status(pending),"pending status").state==AsyncAssetState::Requested,"queued before worker admission");
            Check(serial->Cancel(pending),"cancel not-yet-started request");gate=false;
            serial->Shutdown();Await(*serial,pending,AsyncAssetState::Cancelled);Await(*serial,active,AsyncAssetState::Cancelled);
            Check(serial->Stats().imports==1,"pending cancellation skips importer");
        }
        auto aliases=Take(AsyncMeshLoader::Create(pub,registry,folder,Limits()),"alias cancel loader");
        auto leader=Take(aliases->Request("single.obj"),"cancel leader");
        auto follower=Take(aliases->Request("alias.obj"),"cancel follower");Queued(*aliases,2);
        Check(aliases->Cancel(follower),"cancel shared canonical operation");
        Await(*aliases,leader,AsyncAssetState::Cancelled);Drain(root,*aliases);
        Check(aliases->Stats().uploads==0,"cancelled aliases never publish");
        auto closingLimits=Limits();closingLimits.queue.queuedRequests=1;
        auto closing=Take(AsyncMeshLoader::Create(pub,registry,folder,closingLimits),"shutdown loader");
        auto closeA=Take(closing->Request("single.obj"),"close queued");Queued(*closing,1);
        auto closeB=Take(closing->Request("multi.obj"),"close blocked producer");Await(*closing,closeB,AsyncAssetState::CpuReady);
        closing->Shutdown();Await(*closing,closeA,AsyncAssetState::Cancelled);Await(*closing,closeB,AsyncAssetState::Cancelled);
        Check(closing->Queue().Stats().reservedBytes==0 && !closing->Request("single.obj"),"shutdown closes wakes and joins without render drain");

        // Selected shipped static model: workers import it through the normal API;
        // scheduler publication makes its exact data available to frame consumers.
        const auto barrel=Take(RuntimeAssets::TryFile("Models/barrel.obj"),"selected barrel path");
        auto selected=Take(AsyncMeshLoader::Create(pub,registry,folder),"selected loader");
        auto barrelTicket=Take(selected->Request(barrel),"selected nonblocking request");Queued(*selected,1);Drain(root,*selected);
        auto barrelState=Await(*selected,barrelTicket,AsyncAssetState::Ready);
        auto barrelCpu=Take(ImportMeshFile(barrel.c_str()),"selected CPU reference");
        EqualBuffer(Observer::vertex,barrelCpu.Vertices());EqualBuffer(Observer::index,barrelCpu.Indices());
        {auto view=View(pub,registry,barrelState.mesh);
            Check(view->Bounds().minimum==barrelCpu.Bounds().minimum && view->Bounds().maximum==barrelCpu.Bounds().maximum,"selected bounds parity");
            std::println("[PASS] selected-barrel worker-import scheduler-publication vertices={} submeshes={}",view->VertexCount(),view->Submeshes().size());
        }
        selected->Shutdown();loader->Shutdown();small->Shutdown();aliases->Shutdown();held={};
        {auto publication=pub.BeginPublication();Check(registry.Close(publication),"retire registry resources");}
        Check(glGetError()==GL_NO_ERROR,"no unexpected GL errors");
        std::println("[PASS] async-mesh checks={}",checks.load());
    }
}
int main(int argc,char** argv)
{
    Check(argc==2,"fixture folder argument");ownerThread=std::this_thread::get_id();
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    RuntimeAssets::Initialize("RigidBodySimulation");EngineContext root;
    WindowProperties properties;properties.m_Title="Phase 51 async mesh validation";properties.flag={WindowFlags::INVISIBLE};
    properties.m_Width=properties.m_Height=properties.m_MinWidth=properties.m_MinHeight=64;properties.m_IsVsync=false;
    Check(root.Initialize({properties}),"root context");
    {Observer observer;Run(root,std::filesystem::path(argv[1]));}
    std::println("[PASS] async-mesh-retirement");
}
#endif
