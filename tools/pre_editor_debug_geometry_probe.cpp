// Focused Phase18 checks against the production library; no production test hooks.
#include "Renderer/DebugGeometry.h"
#include "Renderer/FrameSubmission.h"
#include "Renderer/RenderExtraction.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/ShapeBox.h"
#include "Physics/ShapeConvex.h"
#include "Physics/detail/ConvexHull.h"
#include "Scene/_Scene.h"
#include "Scene/_Entity.h"
#include "Core/GEngine.h"
#include "Core/Window.h"
#include "Core/RuntimeAssets.h"
#include "Core/RenderTarget.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <print>
#include <chrono>
#include <thread>
#include <limits>
#include <cstdlib>
#include <set>

using namespace ::GEngine;
using namespace ::GEngine::Component;
static unsigned checks;
template<class T> void Check(const T& ok,const char* text)
{ ++checks; if(!bool(ok)) {std::println(stderr,"[FAIL] {}",text);std::exit(1);} }
template<class T,class E> T Take(std::expected<T,E> result,const char* text)
{ Check(result,text);return std::move(*result); }
bool Near(glm::vec3 a,glm::vec3 b) { return glm::length(a-b)<1e-5f; }

void Convex()
{
    std::vector<glm::vec3> cube;
    for(unsigned i=0;i<8;++i) cube.push_back({i&1?1.f:-1.f,i&2?1.f:-1.f,i&4?1.f:-1.f});
    ShapeConvex shape(cube);Check(shape.IsValid(),"convex cube valid");
    Check(shape.GetBoundaryEdges().size()==12,"cube has12 unique boundary edges, no face diagonals");
    for(const auto& edge:shape.GetBoundaryEdges()) {
        const auto d=glm::abs(shape.GetPoints()[edge[0]]-shape.GetPoints()[edge[1]]);
        Check((d.x>0)+(d.y>0)+(d.z>0)==1,"cube edge changes exactly one coordinate");
    }
    shape.HandleScaleChanged({2,.5f,1.5f});
    const auto scaled=shape.GetPoints();shape.HandleScaleChanged({2,.5f,1.5f});
    Check(shape.GetPoints()==scaled && shape.GetBoundaryEdges().size()==12,"absolute scale is not compounded");
    const auto edges=shape.GetBoundaryEdges();const auto revision=shape.GetRevision();
    shape.Build({{0,0,0},{1,0,0},{2,0,0}});
    Check(shape.GetPoints()==scaled && shape.GetBoundaryEdges()==edges && shape.GetRevision()==revision,
        "invalid rebuild preserves hull/topology/revision");

    ShapeConvex diamond(Math::GenerateDiamond());Check(diamond.IsValid(),"authoritative Diamond hull");
    diamond.HandleScaleChanged({1.5f,.75f,2.f});
    PhysicsSystem physics;physics.SetPhysicsWorld(new PhysicsWorld({0,0,0}));
    auto* body=physics.GetPhysicsWorld()->CreateRigidBody3D();body->m_Shape=&diamond;
    body->m_Position={3,4,5};body->m_Orientation=glm::angleAxis(.7f,glm::normalize(glm::vec3(1,2,3)));
    const auto points=diamond.GetPoints();const auto topology=diamond.GetBoundaryEdges();
    const auto bounds=body->GetWorldBounds();const auto inertia=diamond.InertiaTensor();
    const auto com=diamond.GetCenterOfMass();const auto rev=diamond.GetRevision();
    const auto support=diamond.Support({1,2,3},body->m_Position,body->m_Orientation,0);
    std::vector<FrameDebugLine> storage(topology.size()+15);DebugGeometry geometry(storage);
    Check(physics.AppendDebugGeometry(geometry),"actual transformed Physics Diamond adapter");
    Check(geometry.Lines().size()==topology.size()+15,"hull plus12 actual world AABB plus3 COM axes");
    std::set<std::array<std::size_t,2>> unique;
    Bounds tight;
    for(const auto& p:points) tight.Expand(body->m_Position+body->m_Orientation*p);
    for(std::size_t i=0;i<topology.size();++i) {
        const auto& edge=topology[i];Check(edge[0]<edge[1] && unique.insert(edge).second,"unique canonical boundary edge");
        const auto& line=geometry.Lines()[i];
        Check(Near(line.start,body->m_Position+body->m_Orientation*points[edge[0]]) &&
            Near(line.end,body->m_Position+body->m_Orientation*points[edge[1]]),"already-scaled hull rigid transform exactly once");
        Check(line.color==glm::vec4(0,1,1,1) && line.depth==DebugDepth::Tested,"convex uses collider color/depth");
    }
    const auto aabb=geometry.Lines().subspan(topology.size(),12);
    Bounds observed;for(auto& line:aabb){observed.Expand(line.start);observed.Expand(line.end);}
    Check(Near(observed.mins,bounds.mins) && Near(observed.maxs,bounds.maxs),"yellow bounds equal actual Physics broadphase bounds");
    Check(glm::all(glm::lessThanEqual(bounds.mins,tight.mins+glm::vec3(1e-5f))) &&
        glm::all(glm::greaterThanEqual(bounds.maxs,tight.maxs-glm::vec3(1e-5f))) &&
        glm::length((bounds.maxs-bounds.mins)-(tight.maxs-tight.mins))>.1f,"conservative rotated AABB remains larger than exact hull envelope");
    Check(diamond.GetPoints()==points && diamond.GetBoundaryEdges()==topology && diamond.InertiaTensor()==inertia &&
        diamond.GetCenterOfMass()==com && diamond.GetRevision()==rev &&
        diamond.Support({1,2,3},body->m_Position,body->m_Orientation,0)==support,"read-only overlay preserves collision/mass/identity data");
    auto builder=Take(RenderFrameBuilder::Create({0,0,geometry.Lines().size()}),"convex retained builder");
    for(auto& line:geometry.Lines()) Check(builder.AddDebugLine(line),"copy convex values");
    auto retained=Take(std::move(builder).Finalize(),"convex retained frame");const auto first=retained.DebugLines()[0];
    geometry.Clear();diamond.HandleScaleChanged({.5f,2,1});
    Check(retained.DebugLines()[0].start==first.start && retained.DebugLines()[0].end==first.end,"retained frame survives hull rebuild");
    std::array<FrameDebugLine,1> small;DebugGeometry bounded(small);
    Check(bounded.Line({},{1,0,0},{1,1,1,1}),"capacity sentinel");
    auto full=bounded.IndexedLines(points,topology,{},glm::quat(1,0,0,0),{1,1,1,1});
    Check(!full && full.error()==DebugGeometryCode::Capacity && bounded.Lines().size()==1,"convex capacity failure atomic");
    bounded.Clear();const std::array<std::size_t,2> bad[]{ {0,points.size()} };
    Check(!bounded.IndexedLines(points,bad,{},glm::quat(1,0,0,0),{1,1,1,1}) && bounded.Lines().empty(),"invalid hull index rejected atomically");
    std::vector<glm::vec3> hull;std::vector<tri_t> triangles;
    Check(detail::BuildConvexHull(points,hull,triangles),"count original hull triangulation");
    std::println("CONVEX Diamond vertices={} triangles={} boundary_edges={} pose=(3,4,5),angle=.7axis=normalize(1,2,3) scale=(1.5,.75,2) world_AABB_min=({},{},{}) max=({},{},{}) tight_hull_min=({},{},{}) max=({},{},{})",
        points.size(),triangles.size(),topology.size(),bounds.mins.x,bounds.mins.y,bounds.mins.z,bounds.maxs.x,bounds.maxs.y,bounds.maxs.z,
        tight.mins.x,tight.mins.y,tight.mins.z,tight.maxs.x,tight.maxs.y,tight.maxs.z);
    std::println("[PASS] convex unique-boundary/coplanar-filter/absolute-scale/pose/broadphase-preservation/retained/capacity");
}

void Cpu()
{
    std::array<FrameDebugLine,64> storage;
    DebugGeometry geometry(storage);
    Check(geometry.Ray({1,2,3},{2,0,0},{1,1,1,1}),"ray");
    Check(Near(geometry.Lines()[0].end,{3,2,3}),"ray length/world endpoint");
    const auto orientation=glm::angleAxis(glm::radians(90.f),glm::vec3(0,0,1));
    Check(geometry.Box({-1,-2,-3},{1,2,3},{3,4,5},orientation,{0,1,1,1}),"oriented box");
    Check(geometry.Lines().size()==13 && Near(geometry.Lines()[1].start,{5,3,2}) &&
        Near(geometry.Lines()[1].end,{5,5,2}),"rotated box coordinates");
    Check(geometry.Axes({3,4,5},orientation,2),"axes");
    Check(Near(geometry.Lines()[13].end,{3,6,5}),"axis rotation/length");
    Check(geometry.Sphere({1,2,3},2,{1,1,1,1}),"sphere");
    Check(geometry.Lines().size()==64,"bounded exact capacity");
    for(auto& line:geometry.Lines().subspan(16))
        Check(std::abs(glm::length(line.start-glm::vec3(1,2,3))-2)<1e-5f,"sphere radius");
    auto overflow=geometry.Line({},{1,0,0},{1,1,1,1});
    Check(!overflow && overflow.error()==DebugGeometryCode::Capacity && geometry.Lines().size()==64,"typed capacity/no partial append");
    auto builder=Take(RenderFrameBuilder::Create({0,0,64}),"retained builder");
    for(auto& line:geometry.Lines()) Check(builder.AddDebugLine(line),"copy line");
    auto retained=Take(std::move(builder).Finalize(),"retained frame");
    geometry.Clear();
    Check(geometry.Line({9,9,9},{10,9,9},{1,1,1,1}),"reuse producer");
    Check(Near(retained.DebugLines()[0].start,{1,2,3}),"retained frame independent");
    auto invalid=geometry.Sphere({},-1,{1,1,1,1});
    Check(!invalid && invalid.error()==DebugGeometryCode::InvalidInput && geometry.Lines().size()==1,"typed invalid primitive/atomicity");
    Check(!geometry.Ray({}, {std::numeric_limits<float>::infinity(),0,0},{1,1,1,1}) &&
        geometry.Lines().size()==1,"nonfinite rejected");
    std::thread retire([frame=std::move(retained)]{});retire.join();

    ShapeSphere sphere;
    auto box=Take(ShapeBox::Create({{-1,-2,-3},{1,2,3}}),"physics box");
    PhysicsSystem physics;
    physics.SetPhysicsWorld(new PhysicsWorld({0,0,0}));
    auto* a=physics.GetPhysicsWorld()->CreateRigidBody3D();
    a->m_Shape=&box;a->m_Position={3,4,5};a->m_Orientation=orientation;
    std::array<FrameDebugLine,512> physicsStorage;
    DebugGeometry lines(physicsStorage);
    Check(physics.AppendDebugGeometry(lines),"physics rotated box adapter");
    Check(lines.Lines().size()==27 && Near(lines.Lines()[0].start,{5,3,2}),"actual collider geometry");
    Check(Near(lines.Lines()[12].start,{1,3,2}) && Near(lines.Lines()[24].start,a->GetCenterOfMassWorldSpace()),"world AABB/current COM");
    physics.GetPhysicsWorld()->RemoveRigidBody3D(a);
    a=physics.GetPhysicsWorld()->CreateRigidBody3D();
    auto* b=physics.GetPhysicsWorld()->CreateRigidBody3D();
    a->m_Shape=b->m_Shape=&sphere;a->m_Position={0,0,0};b->m_Position={1.9f,0,0};
    Check(a->SetBodyTypeAndInverseMass(BodyType::Dynamic,1),"dynamic A");
    Check(b->SetBodyTypeAndInverseMass(BodyType::Static,0),"static B");
    physics.Update(Timestep(1.f/120.f));
    const auto beforeA=a->m_Position,beforeB=b->m_Position;
    lines.Clear();Check(physics.AppendDebugGeometry(lines),"physics contacts adapter");
    Check(lines.Lines().size()>=176,"actual sphere contacts present");
    const auto& anchor=lines.Lines()[126];const auto& normal=lines.Lines()[127];
    Check(Near(anchor.start,normal.start) && std::abs(glm::length(normal.end-normal.start)-.4f)<1e-5f &&
        std::abs(normal.end.x-normal.start.x)>.399f,"contact anchor/world unit normal length");
    Check(Near(a->m_Position,beforeA) && Near(b->m_Position,beforeB),"adapter preserves physics poses");
    std::println("[PASS] CPU geometry/capacity/retained-frame/Physics collider-AABB-COM-contact");
}

void Gpu(EngineContext& root,bool measure)
{
    auto resources=Take(SceneRenderResources::Create(root),"scene resources");
    _Scene scene;
    auto cameraEntity=Take(scene.CreateEntity("camera"),"camera entity");
    cameraEntity.AddComponent<RenderCameraComponent>();
    const auto cameraId=Take(scene.RenderData().Identify(cameraEntity),"camera identity");
    const FrameCamera camera{cameraId,glm::lookAt(glm::vec3(0,0,3),glm::vec3(0),glm::vec3(0,1,0)),
        glm::ortho(-1.f,1.f,-1.f,1.f,.1f,20.f),{0,0,3},0,0,64,64};
    auto box=Take(resources->PublishShape("Box"),"render box");
    MaterialAuthoringDesc white;white.kind=MaterialKind::Unlit;
    auto material=Take(resources->CreateMaterial(white),"white unlit material");
    auto occluder=Take(scene.CreateEntity("occluder"),"occluder entity");
    occluder.AddComponent<MeshRendererComponent>(MeshRendererComponent{box,material});
    occluder.GetComponent<Transform3DComponent>().Scale={.5f,.5f,.5f};
    auto submission=Take(FrameSubmission::Create(),"submission owner");
    RenderTargetDesc td;td.Storage.Width=td.Storage.Height=64;
    td.Storage.Colors[0]=FramebufferFormat::RGBA8;td.Storage.ColorCount=1;td.Storage.Depth=FramebufferFormat::Depth24Stencil8;
    auto target=Take(RenderTarget::Create(td),"target");
    auto picking=Take(MousePickFrameBuffer::Create(64,64),"picking");
    auto point=Take(PointShadowFrameBuffer::Create(32,32),"point target");
    auto cascade=Take(CascadeShadowFrameBuffer::Create(32,32,5),"cascade target");
    const float splits[]{.5f,1,2,5,20};
    FrameSubmissionDesc desc{target,picking,point,cascade,resources->Pipelines(),splits,glm::radians(45.f),1,.1f,20,.1f,100};
    desc.pickingEnabled=false;
    EntityPickTable picks;
    auto submit=[&](std::span<const FrameDebugLine> lines) {
        auto access=resources->Publication().BeginFrame(); RenderExtractionStats extraction;
        auto frame=Take(ExtractRenderFrame(scene,resources->ForFrame(access),extraction,{&camera,1},lines),"extract immutable frame");
        auto result=submission.Submit(frame,desc,picks);
        if(!result) std::println(stderr,"{}",DescribeSubmissionError(result.error()));
        return Take(std::move(result),"submit debug frame");
    };
    auto pixels=[&] {std::vector<std::byte> out(64*64*4);Check(target.ReadColor(out),"color readback");return out;};
    auto depth=[&] {std::vector<float> out(64*64);target.Bind();glReadPixels(0,0,64,64,GL_DEPTH_COMPONENT,GL_FLOAT,out.data());return out;};
    auto red=[](const auto& bytes) {unsigned n=0;for(std::size_t i=0;i<bytes.size();i+=4)
        if(std::to_integer<unsigned>(bytes[i])>200 && std::to_integer<unsigned>(bytes[i+1])<50) ++n;return n;};
    auto off=submit({});const auto baseline=pixels();const auto baselineDepth=depth();
    Check(off.helperDraws==0 && red(baseline)==0,"off has no debug draws/pixels");
    FrameDebugLine line{{-.2f,0,1},{.2f,0,1},{1,0,0,1},{},DebugDepth::Tested};
    auto visible=submit({&line,1});Check(visible.helperDraws==1 && red(pixels())>0,"tested line visible");
    Check(depth()==baselineDepth,"tested debug does not write depth");
    line.start.z=line.end.z=-1;submit({&line,1});Check(pixels()==baseline,"tested line occluded by actual box");
    line.depth=DebugDepth::Overlay;submit({&line,1});Check(red(pixels())>0,"overlay bypasses occlusion");
    Check(depth()==baselineDepth,"overlay does not write depth");
    submit({});Check(pixels()==baseline,"next empty frame removes overlay");
    ShapeConvex diamond(Math::GenerateDiamond());diamond.HandleScaleChanged({.15f,.15f,.15f});
    std::vector<FrameDebugLine> hullStorage(diamond.GetBoundaryEdges().size());DebugGeometry hullLines(hullStorage);
    const auto rotation=glm::angleAxis(.7f,glm::normalize(glm::vec3(1,2,3)));
    Check(hullLines.IndexedLines(diamond.GetPoints(),diamond.GetBoundaryEdges(),{0,0,-1},rotation,{1,0,0,1}),"GPU exact hull payload");
    submit(hullLines.Lines());Check(pixels()==baseline,"rotated convex tested hull occluded");
    hullLines.Clear();Check(hullLines.IndexedLines(diamond.GetPoints(),diamond.GetBoundaryEdges(),{0,0,-1},rotation,{1,0,0,1},DebugDepth::Overlay),"GPU convex overlay payload");
    const auto hullStats=submit(hullLines.Lines());
    Check(hullStats.helperDraws==diamond.GetBoundaryEdges().size() && red(pixels())>0 && depth()==baselineDepth,"actual convex edges rendered without depth writes");
    submit({});Check(pixels()==baseline,"convex toggle-off removes all edges");
    Check(glGetError()==GL_NO_ERROR,"no driver errors");
    std::println("[PASS] GPU off/on/depth/occlusion/no-depth-write/one-frame-lifetime");
    if(measure) {
        std::vector<FrameDebugLine> scratch(256*48);DebugGeometry geometry(scratch);
        for(bool enabled:{false,true}) {
            double prepSum=0,submitSum=0,completionSum=0,minimum=1e30,maximum=0;
            for(unsigned sample=0;sample<48;++sample) {
                using Clock=std::chrono::steady_clock;auto t0=Clock::now();geometry.Clear();
                if(enabled) for(unsigned i=0;i<256;++i) Check(geometry.Sphere(
                    {-.8f+.1f*float(i%16),-.8f+.1f*float(i/16),1},.025f,{0,1,1,1}),"overhead sphere");
                auto t1=Clock::now();auto stats=submit(geometry.Lines());auto t2=Clock::now();glFinish();auto t3=Clock::now();
                if(sample>=16) {
                    auto us=[](auto a,auto b){return std::chrono::duration<double,std::micro>(b-a).count();};
                    const auto prep=us(t0,t1),submissionUs=us(t1,t2),completion=us(t2,t3);
                    prepSum+=prep;submitSum+=submissionUs;completionSum+=completion;
                    minimum=std::min(minimum,prep+submissionUs);maximum=std::max(maximum,prep+submissionUs);
                }
            }
            std::println("MEASURE overlay={} samples=32 warmup=16 lines={} scratch_bytes={} prep_mean_us={} extraction_submit_mean_us={} gpu_completion_wait_mean_us={} cpu_min_us={} cpu_max_us={}",
                enabled,geometry.Lines().size(),scratch.size()*sizeof(FrameDebugLine),prepSum/32,submitSum/32,completionSum/32,minimum,maximum);
        }
    }
    std::println("[PASS] owner GPU resources retire before EngineContext");
}

int main(int argc,char** argv)
{
    Log::Initialize();Cpu();Convex();RuntimeAssets::Initialize("RigidBodySimulation");
    {
        EngineContext root;WindowProperties p;p.m_Title="Phase18 focused checks";
        p.m_Width=p.m_Height=p.m_MinWidth=p.m_MinHeight=64;p.flag={WindowFlags::INVISIBLE};p.m_IsVsync=false;
        Check(root.Initialize({p}),"engine initialization");
        std::println("DRIVER {} / {}",reinterpret_cast<const char*>(glGetString(GL_RENDERER)),reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        Gpu(root,argc==2 && std::string_view(argv[1])=="--measure");
    }
    std::println("[PASS] Phase18 focused checks={} clean shutdown",checks);
}
