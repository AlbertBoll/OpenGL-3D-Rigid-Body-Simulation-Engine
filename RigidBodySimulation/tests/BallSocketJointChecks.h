#pragma once
#ifdef GENGINE_PHASE27_QUALIFICATION
#include "RigidBodySimulation.h"
#include "Scene/_Scene.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/JoltAuditObservation.h"
#include "Core/RenderTarget.h"
#include <stb_image/stb_image_write.h>
#include <imgui/imgui.h>
#include "LaunchConfigChecks.h"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
extern "C" const std::uint64_t g_Phase27TargetSizes[];

namespace GEngine
{
    struct Phase27GraphAccess
    {
        struct Snapshot {
            std::size_t edges{},nodes{},counts{},contactIslands{},activationIslands{},contactBytes{},activationBytes{};
            std::uintptr_t edgeStorage{};
            friend bool operator==(const Snapshot&,const Snapshot&)=default;
        };
        static Snapshot Read(const PhysicsSystem& system)
        {
            const auto bytes=[](const auto& islands) {
                std::size_t total=0;
                for(const auto& island:islands)total+=(island.dynamicBodies.capacity()+island.boundaryBodies.capacity())*
                    sizeof(RigidBodyIdentity)+island.contactPairs.capacity()*sizeof(ContactIslandPair);
                return total;
            };
            const auto& s=system.m_ContactIslandScratch;
            return {s.edges.capacity(),s.nodes.capacity(),s.counts.capacity(),system.m_ContactIslands.size(),
                system.m_ActivationIslands.size(),bytes(system.m_ContactIslands),bytes(system.m_ActivationIslands),
                reinterpret_cast<std::uintptr_t>(s.edges.data())};
        }
        static bool Prepared(const PhysicsSystem& system,std::size_t bodies,std::size_t joints)
        {
            if(system.m_ContactIslandScratch.edges.capacity()<bodies*2+joints)return false;
            for(const auto* islands:{&system.m_ContactIslands,&system.m_ActivationIslands}) {
                if(islands->empty())return false;
                for(const auto& island:*islands)if(island.dynamicBodies.capacity()<bodies ||
                    island.boundaryBodies.capacity()<bodies || island.contactPairs.capacity()<bodies*2)return false;
            }
            return true;
        }
        static bool Retired(const PhysicsSystem& system)
        {return !system.m_PhysicsWorld && system.m_ContactIslands.empty() && system.m_ActivationIslands.empty();}
        struct PointSnapshot {
            Vec3f residual{},armA{},armB{},bias{},impulse{};
            Mat3 effectiveMass{0},inverseInertiaA{0},inverseInertiaB{0};
            PhysicsDiagnostic::BodyState a{},b{};
            float dt{},inverseMassA{},inverseMassB{};bool active{};
            std::size_t manifolds{},pendingTois{};
        };
        static PointSnapshot Point(const PhysicsSystem& system)
        {
            PointSnapshot out;const auto& w=*system.m_PhysicsWorld;
            for(std::size_t i=0;i<PhysicsWorld::MaxDistanceJoints;++i) {
                const auto& slot=w.m_Joints[i];
                if(!slot.live || slot.kind!=JointKind::BallSocket)continue;
                const auto& row=slot.point;
                out.armA=row.armA;out.armB=row.armB;out.bias=row.bias;out.impulse=row.impulse;
                out.effectiveMass=row.effectiveMass;out.inverseInertiaA=row.inverseInertiaA;out.inverseInertiaB=row.inverseInertiaB;
                out.dt=row.previousDt;out.active=row.active;
                if(row.a && row.b){
                    out.residual=row.b->BodySpaceToWorldSpace(row.desc.localAnchorB)-row.a->BodySpaceToWorldSpace(row.desc.localAnchorA);
                    out.a=PhysicsDiagnostic::Capture(*row.a);out.b=PhysicsDiagnostic::Capture(*row.b);
                    out.inverseMassA=row.a->GetInverseMass();out.inverseMassB=row.b->GetInverseMass();
                }
                break;
            }
            out.manifolds=system.m_Manifolds.m_Manifolds.size();out.pendingTois=system.m_Contacts.size();return out;
        }
        static void Contacts(PhysicsSystem& system,std::ostream& out,unsigned tick)
        {
            for(auto& m:system.m_Manifolds.m_Manifolds)for(int i=0;i<m.GetNumContacts();++i) {
                const auto c=m.GetContact(i);
                out<<tick<<','<<m.GetBodyAIdentity().GetSlot()-1<<','<<m.GetBodyBIdentity().GetSlot()-1<<','
                    <<c.separationDistance<<'\n';
            }
        }
    };
}

namespace Rbs
{
    struct BallSocketChecks
    {
        using World = GEngine::PhysicsWorld;
        using Handle = GEngine::PhysicsBodyHandle;
        using Vec = GEngine::Math::Vec3f;
        struct Sample {
            std::array<GEngine::PhysicsBodyState,2> bodies{};
            GEngine::JointState joint{};
            std::uint64_t inclusive{},jointNs{},contactNs{},cppCalls{},cppBytes{},heapCalls{},heapBytes{},observerNs{};
            double energy{},work{};
        };
        struct ArenaRow { GEngine::Phase27Audit::ArenaEvent event; std::uint32_t tick{}; };
        struct HeapRow { GEngine::Phase27Audit::HeapEvent event; std::uint32_t tick{}; };
        struct NativeStage {
            std::uint64_t calls{},bytes{};
            std::size_t storage{},manifolds{},islands{},dynamicCapacity{},boundaryCapacity{},pairCapacity{};
            unsigned visits{};
        };
        static inline std::array<std::array<NativeStage,12>,1201> nativeStages{};
        static inline GEngine::PhysicsDiagnostic::Observer nativeObserver{};
        static inline bool nativeAttribution{};
        struct TraceRow {unsigned tick{},stage{};::GEngine::Phase27GraphAccess::PointSnapshot point;};
        struct TraceEvent {unsigned tick{};::GEngine::PhysicsDiagnostic::Event event;::GEngine::Phase27GraphAccess::PointSnapshot point;};
        static inline std::array<TraceRow,252> traceRows{};
        static inline std::array<TraceEvent,2048> traceEvents{};
        static inline unsigned traceRowCount{},traceEventCount{};
        static inline bool nativeTrace{};
        static inline ::GEngine::PhysicsSystem* tracePhysics{};
        static inline ::GEngine::PhysicsDiagnostic::Observer traceObserver{};
        static inline GEngine::Phase27GraphAccess::Snapshot graphInitial{};
        struct Construction {std::uint64_t calls{},bytes{},replacementCalls{},replacementBytes{};};
        static inline std::array<Construction,2> construction{};
        static inline std::array<Sample,1201> samples{};
        static inline std::array<ArenaRow,65536> arena{};
        static inline std::array<HeapRow,4096> heap{};
        static inline std::atomic<unsigned> heapCount{};
        static inline std::atomic<unsigned> arenaCount{};
        static inline unsigned tick{},target{},unitTicks{};
        static inline bool started{},minimal{},failed{};
        static inline bool visualProbe{},repeatProbe{},gc{},gcDebug{},gcTransition{};
        static inline unsigned worldStartTick{};
        static inline std::weak_ptr<GEngine::_Scene> previousScene;
        static inline std::ofstream lifecycle,impulses;
        static inline GEngine::PhysicsBackend expectedBackend{};
        static inline std::ofstream visualStates,visualContacts;
        static inline const char* failure{};
        static inline std::filesystem::path output;
        static inline std::array<Handle,2> bodies{};
        static inline GEngine::PhysicsJointHandle joint{};
        static inline std::uint64_t frameStart{};
        static inline double work{};
        static void Check(bool ok,const char* text) { if(!ok && !failed){failed=true;failure=text;} }
        static Handle Body(World& world,Vec position={},float mass=1,bool fixed=false)
        {
            auto shape=GEngine::CreateCollisionShape(GEngine::SphereShapeDesc{1});
            GEngine::PhysicsBodyDesc d;d.position=position;d.mass=mass;
            d.type=fixed?GEngine::Component::BodyType::Static:GEngine::Component::BodyType::Dynamic;
            if(!shape)return {};
            const auto h=world.CreateBody(d,*shape);return h?*h:Handle{};
        }
        static bool Unit()
        {
            using namespace GEngine;
            Check(bool(PreEditorValidation::CheckLaunchConfig()),"typed launch compatibility");
            for(auto backend:{PhysicsBackend::Native}) {
                for(int axis=0;axis<3;++axis) {
                    PhysicsSystem system;system.SetPhysicsWorld(new World(Vec(0)));
                    auto& w=*system.GetPhysicsWorld();
                    Check(bool(w.ConfigureBackend(backend)),"unit backend");
                    const auto a=Body(w),b=Body(w,{},2);
                    const BallSocketJointDesc d{a,b,{},{},false};
                    const auto h=w.CreateBallSocketJoint(d);
                    Check(bool(h),"centre-anchor creation"); if(!h)return false;
                    Vec angular(0);angular[axis]=1;
                    Check(bool(w.SetBodyVelocity(a,{},angular)),"angular freedom setup");
                    system.Update(Timestep(1.0/60.0));++unitTicks;
                    const auto state=w.ReadBody(a);
                    Check(state && state->angularVelocity==angular,"free relative rotation on canonical axis");
                    Check(!system.HasJointFailure() && !system.GetBackendFailure(),"unit solve finite");
                    Check(!w.ReadDistanceJoint(*h) && w.ReadDistanceJoint(*h).error().code==JointErrorCode::WrongKind,"typed wrong-kind read");
                    auto bad=d;bad.bodyB=a;
                    Check(!w.CreateBallSocketJoint(bad) && w.GetJointCount()==1,"self-joint transactional rejection");
                    bad=d;bad.localAnchorA.x=std::numeric_limits<float>::quiet_NaN();
                    Check(!w.CreateBallSocketJoint(bad) && w.GetJointCount()==1,"nonfinite anchor rejection");
                    World foreign;bad=d;bad.bodyB=Body(foreign);
                    Check(!w.CreateBallSocketJoint(bad) && w.GetJointCount()==1,"foreign endpoint rejection");
                    Check(bool(w.SetBodyMass(a,2)) && w.ReadBallSocketJoint(*h)->accumulatedImpulse==Vec(0),"mass cache reset");
                    Check(bool(system.SetBodyPose(a,Vec(0),Math::Quat(1,0,0,0))) &&
                        w.ReadBallSocketJoint(*h)->accumulatedImpulse==Vec(0),"pose cache reset");
                    const auto duplicate=w.CreateBallSocketJoint(d);
                    Check(duplicate && bool(w.DestroyJoint(*duplicate)),"independent filter contribution retirement");
                    system.Update(Timestep(1.0/60.0));++unitTicks;
                    Check(w.ReadBody(a)->angularVelocity==angular && glm::length(w.ReadBallSocketJoint(*h)->residual)==0,
                        "remaining point joint retains connected collision suppression");
                    const auto fixedA=Body(w,Vec(10,0,0),1,true),fixedB=Body(w,Vec(20,0,0),1,true);
                    const auto fixedJoint=w.CreateBallSocketJoint({fixedA,fixedB,{},{},false});
                    Check(!fixedJoint && fixedJoint.error().code==JointErrorCode::NoResponse,"singular fixed endpoints rejected");
                    auto rejectedShape=CreateCollisionShape(SphereShapeDesc{1});
                    PhysicsBodyDesc invalidBody;invalidBody.mass=-1;
                    const auto countBefore=w.GetBodyCount();
                    Check(rejectedShape && !w.CreateBody(invalidBody,*rejectedShape) && bool(*rejectedShape) &&
                        w.GetBodyCount()==countBefore,"body creation rollback retains caller shape");
                    Check(bool(w.DestroyBody(b)) && !w.ReadJoint(*h) && w.GetJointCount()==0,"dependent retirement");
                    const auto replacement=Body(w);
                    Check(replacement!=b && !w.CreateBallSocketJoint(d),"stale body rejection");
                    auto fresh=w.CreateBallSocketJoint({a,replacement,{},{},false});
                    Check(fresh && *fresh!=*h && !w.DestroyJoint(*h),"joint generation reuse");
                    Check(fresh && bool(w.DestroyJoint(*fresh)) && w.GetJointCount()==0,"joint deletion");
                    Check(bool(system.SetBodyPose(replacement,Vec(3,0,0),Math::Quat(1,0,0,0))),"distance reuse pose");
                    const auto distance=w.CreateDistanceJoint({a,replacement,{},{},3,false});
                    Check(bool(distance),"distance creation in retired point slot");
                    system.Update(Timestep(1.0/60.0));++unitTicks;
                    Check(distance && w.ReadDistanceJoint(*distance) && !w.ReadBallSocketJoint(*distance),"distance dispatch preservation");
                    Check(distance && bool(w.DestroyJoint(*distance)),"distance retirement");
                    system.OnExit();
                }
            }
            return !failed;
        }
        static void GraphLifecycle()
        {
            using namespace GEngine;
            PhysicsSystem system;
            PhysicsJointHandle previous{};
            for(unsigned cycle=0;cycle<2;++cycle) {
                system.SetPhysicsWorld(new World(Vec(0)));auto& w=*system.GetPhysicsWorld();
                for(unsigned i=0;i<5;++i)Body(w,Vec(20+4.f*i,0,0),1,true);
                const auto a=Body(w,Vec(-1.125f,0,0)),b=Body(w,Vec(1.125f,0,0),2);
                const BallSocketJointDesc desc{a,b,Vec(1.125f,0,0),Vec(-1.125f,0,0),false};
                JoltAudit::Reset();JoltAudit::countAllocations=true;
                const auto h=w.CreateBallSocketJoint(desc);
                JoltAudit::countAllocations=false;
                construction[cycle].calls=JoltAudit::cppCalls;construction[cycle].bytes=JoltAudit::cppBytes;
                Check(h && *h!=previous && w.GetJointCount()==1,"graph construction ownership");
                Check(Phase27GraphAccess::Prepared(system,7,1),"graph capacity prepared at construction");
                const auto initial=Phase27GraphAccess::Read(system);
                system.Update(Timestep(1.0/60.0));++unitTicks;
                Check(Phase27GraphAccess::Read(system)==initial,"prepared graph storage stable through step");
                Check(h && bool(w.DestroyJoint(*h)) && !w.ReadJoint(*h),"graph joint deletion");
                JoltAudit::Reset();JoltAudit::countAllocations=true;
                const auto replacement=w.CreateBallSocketJoint(desc);
                JoltAudit::countAllocations=false;
                construction[cycle].replacementCalls=JoltAudit::cppCalls;construction[cycle].replacementBytes=JoltAudit::cppBytes;
                Check(replacement && h && *replacement!=*h && Phase27GraphAccess::Prepared(system,7,1),
                    "replacement graph capacity and generation");
                Check(bool(w.DestroyBody(b)) && w.GetJointCount()==0,"graph dependent joint retirement");
                previous=replacement?*replacement:PhysicsJointHandle{};
                system.OnExit();
                Check(Phase27GraphAccess::Retired(system),"world reset retires graph outputs and owners");
            }
        }
        static void Capture(World& world,Sample& s)
        {
            const auto j=world.ReadJoint(joint);
            Check(bool(j),"joint readback");if(!j)return;s.joint=*j;s.work=work;
            const auto gravity=world.GetGravity();
            for(unsigned i=0;i<2;++i) {
                auto b=world.ReadBody(bodies[i]);Check(bool(b),"body readback");if(!b)return;
                s.bodies[i]=*b;
                if(b->inverseMass>0) {
                    const double m=1.0/b->inverseMass;
                    // Every admitted dynamic shape is a radius1 homogeneous sphere.
                    s.energy+=.5*m*glm::dot(b->linearVelocity,b->linearVelocity)+
                        .2*m*glm::dot(b->angularVelocity,b->angularVelocity)-m*glm::dot(gravity,b->position);
                }
            }
        }
        // Owner-finding diagnosis only. Existing G geometry/filter/automatic kick
        // remain untouched; manual requests use the ordinary Editor controller.
        static void VisualCapture(RigidBodySimulationApp& app)
        {
            auto& scene=*app.m_ActiveScene;auto& physics=*scene.GetPhysicsSystem();auto& w=*physics.GetPhysicsWorld();
            const auto& s=samples[tick];
            for(unsigned i=0;i<2;++i) {
                auto shape=physics.ReadBodyCollider(bodies[i]);auto properties=w.ReadBodyProperties(bodies[i]);
                Check(shape && properties,"visual collider/properties readback");if(!shape || !properties)return;
                const auto* sphere=std::get_if<GEngine::SphereShapeDesc>(&shape->dimensions);
                Check(sphere!=nullptr,"visual sphere shape");if(!sphere)return;
                Vec current{},previous{},present{},scale{};unsigned matches=0;
                for(auto entity:scene.ReadRegistry().view<GEngine::Component::RigidBody3DComponent>()) {
                    GEngine::_Entity e(entity,&scene);const auto binding=scene.GetBodyBinding(e);
                    if(!binding || *binding!=bodies[i])continue;
                    auto pose=scene.ReadBodyTransform(e);auto render=scene.GetRenderTransform(e);
                    Check(pose && render,"visual transform readback");if(!pose || !render)return;
                    current=pose->current.translation;previous=pose->previous.translation;present=Vec(render->matrix[3]);
                    scale=e.GetComponent<GEngine::Component::Transform3DComponent>().Scale;++matches;
                }
                Check(matches==1,"visual unique body/entity binding");
                const auto& b=s.bodies[i];const auto& j=s.joint;
                visualStates<<tick<<','<<i<<','<<bodies[i].index<<','<<sphere->radius<<','<<b.position.x<<','<<b.position.y<<','<<b.position.z<<','
                    <<current.x<<','<<current.y<<','<<current.z<<','<<previous.x<<','<<previous.y<<','<<previous.z<<','
                    <<present.x<<','<<present.y<<','<<present.z<<','<<scale.x<<','<<scale.y<<','<<scale.z<<','
                    <<properties->continuousCollision<<','<<j.descriptor.collideConnected<<','<<j.worldAnchorA.x<<','<<j.worldAnchorA.y<<','<<j.worldAnchorA.z<<','
                    <<j.worldAnchorB.x<<','<<j.worldAnchorB.y<<','<<j.worldAnchorB.z<<','<<app.m_ChainImpulse.AppliedCount()<<'\n';
            }
            if(w.GetBackend()==GEngine::PhysicsBackend::Jolt) {
                auto contacts=w.ReadJoltDiagnostics();Check(contacts && !contacts->overflow,"visual Jolt contact capture");
                if(contacts)for(const auto& c:contacts->contacts)
                    visualContacts<<tick<<','<<c.bodyA.index<<','<<c.bodyB.index<<','<<c.penetration<<'\n';
            } else GEngine::Phase27GraphAccess::Contacts(physics,visualContacts,tick);
            if(tick==420 || (repeatProbe && (tick==480 || tick==540))) {
                app.m_ChainImpulseMagnitude=5.f;app.m_ChainImpulseDirection=0;
                Check(app.RequestDistanceChainImpulse(),"visual ordinary +X5Ns impulse request");
            }
        }
        static void GcBind(RigidBodySimulationApp& app)
        {
            auto& physics=*app.m_ActiveScene->GetPhysicsSystem();auto& w=*physics.GetPhysicsWorld();
            if(gcTransition) {
                Check(previousScene.expired(),"G_C old scene released");
                Check(!w.ReadJoint(joint) && !w.ReadBody(bodies[0]) && !w.ReadBody(bodies[1]),"G_C old handles rejected");
                Check(app.m_Backend==expectedBackend && w.GetBackend()==expectedBackend &&
                    physics.GetCompletedSteps()==0 && app.m_SimulationState==SimulationState::Ready,"G_C transaction reset");
                Check(!app.m_ChainImpulse.Pending() && app.m_ChainImpulse.AppliedCount()==0,"G_C impulse reset");
                gcTransition=false;worldStartTick=tick;
                lifecycle<<tick<<",reconstruction,"<<GEngine::PhysicsBackendName(w.GetBackend())<<'\n';
            }
            Check(w.GetBodyCount()==7 && w.GetJointCount()==1,"G_C exact topology");
            const auto h=w.JointAt(0);Check(bool(h),"G_C joint handle");if(!h)return;
            joint=*h;const auto j=w.ReadJoint(joint);Check(bool(j),"G_C joint state");if(!j)return;
            bodies={j->descriptor.bodyA,j->descriptor.bodyB};
            Check(j->descriptor.kind==GEngine::JointKind::BallSocket && j->descriptor.collideConnected,
                "G_C pair contact eligibility retained");
            Check(w.ReadBody(bodies[0])->position==Vec(-1.125f,1.5f,0) &&
                w.ReadBody(bodies[1])->position==Vec(1.125f,1.5f,0),"G_C authored positions restored");
            if(w.GetBackend()==GEngine::PhysicsBackend::Jolt)
                Check(bool(w.EnableJoltDiagnostics(true)),"G_C actual contact diagnostics");
        }
        static void GcLifecycle(RigidBodySimulationApp& app)
        {
            auto& physics=*app.m_ActiveScene->GetPhysicsSystem();auto& w=*physics.GetPhysicsWorld();
            if(tick==20 || tick==60) {
                const auto state=w.ReadJoint(joint);Check(bool(state),"G_C pre-retirement state");if(!state)return;
                const auto d=state->descriptor;const auto old=joint;
                Check(bool(w.DestroyJoint(old)) && !w.ReadJoint(old) && w.GetJointCount()==0,"G_C joint retirement");
                GEngine::JoltAudit::Reset();GEngine::JoltAudit::countAllocations=true;
                const auto fresh=w.CreateBallSocketJoint({d.bodyA,d.bodyB,d.localAnchorA,d.localAnchorB,d.collideConnected});
                GEngine::JoltAudit::countAllocations=false;
                Check(fresh && *fresh!=old && !w.ReadJoint(old),"G_C joint replacement generation");
                if(fresh)joint=*fresh;
                lifecycle<<tick<<",joint-replacement,"<<GEngine::PhysicsBackendName(w.GetBackend())<<','
                    <<GEngine::JoltAudit::cppCalls<<','<<GEngine::JoltAudit::cppBytes<<'\n';
                if(w.GetBackend()==GEngine::PhysicsBackend::Native)
                    Check(GEngine::Phase27GraphAccess::Prepared(physics,7,1),"G_C mutation prepares graph");
            }
            if(tick==40 || tick==80 || tick==120) {
                previousScene=app.m_ActiveScene;gcTransition=true;
                app.SetSimulationState(SimulationState::Paused);
                expectedBackend=tick==120?GEngine::PhysicsBackend::Native:GEngine::PhysicsBackend::Jolt;
                Check(tick==80?app.RequestScene(app.m_ActiveSelection):app.RequestBackend(expectedBackend),"G_C queued reset/backend request");
            }
            if(tick==target) {
                Check(bool(w.DestroyBody(bodies[1])) && w.GetJointCount()==0 && !w.ReadJoint(joint),"G_C endpoint retirement");
                lifecycle<<tick<<",endpoint-retirement,"<<GEngine::PhysicsBackendName(w.GetBackend())<<'\n';
            }
        }
        static inline unsigned previewIndex{},previewFrames{};
        static inline std::size_t previewOnLines{};
        static inline std::uint64_t previewGeneration=~std::uint64_t(0);
        static inline std::filesystem::path previewOutput;
        static constexpr std::array previewPresets{RbsScenePreset::BallSocketSwing,RbsScenePreset::BallSocketPair,
            RbsScenePreset::BallSocketContact,RbsScenePreset::BallSocketContactEnabled};
        static void PreviewBoundary(RigidBodySimulationApp& app)
        {
            const auto* directory=std::getenv("GENGINE_PHASE27_PREVIEW_OUTPUT");if(!directory)return;
            previewOutput=directory;
            Check(previewOutput.is_absolute() && std::filesystem::is_directory(previewOutput),"existing preview output directory");
            Check(previewIndex<previewPresets.size() && app.m_ActiveSelection.scene==previewPresets[previewIndex],"preview scene identity");
            app.SetSimulationState(SimulationState::Ready);
            if(previewGeneration!=app.m_SceneGeneration) {
                previewGeneration=app.m_SceneGeneration;previewFrames=0;
                Check(!app.m_IsShowDebugBoundingBox && !app.m_ShowSelectedColliderOutlines,"ordinary scene defaults visualization OFF");
                // The world-axis render helper is independent of Physics debug geometry.
                // Hide it only in this opt-in thumbnail session; keep ordinary Editor controls intact.
                app.m_AxisEntity.AddOrReplaceComponent<::GEngine::Component::VisibilityComponent>(
                    ::GEngine::Component::VisibilityComponent{false});
                const float y=previewIndex==0?6.125f:previewIndex==1?6.f:1.5f;
                app.FocusMaterialCamera(app.m_EditorCamera_,Vec(0,y,0),.25f,-.3f,9.f);
            }
            Check(app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()==0,"authored preview never advances Physics");
            if(failed)app.ShutDown();
        }
        static void PreviewFrame(RigidBodySimulationApp& app,std::size_t debugLines)
        {
            if(previewOutput.empty() || failed)return;
            ++previewFrames;
            if(previewFrames==9) {
                previewOnLines=debugLines;
                Check(app.m_IsShowDebugBoundingBox && debugLines>0,"visualization ON submits debug geometry");
                app.m_IsShowDebugBoundingBox=false;
                if(failed)app.ShutDown();
                return;
            }
            if(previewFrames==10) {
                Check(!app.m_IsShowDebugBoundingBox && debugLines==0,"visualization OFF removes debug geometry");
                std::ofstream info(previewOutput/(std::string(SceneName(previewPresets[previewIndex]))+".txt"),std::ios::app);
                info<<"toggle_on_debug_lines="<<previewOnLines<<"\ntoggle_off_debug_lines="<<debugLines<<"\n";
                if(failed || ++previewIndex==previewPresets.size()) {
                    std::ofstream result(previewOutput/"result.json");result<<"{\"complete\":"<<(failed?"false":"true")<<",\"images\":"<<previewIndex
                        <<",\"physics_ticks\":0,\"failure\":\""<<(failure?failure:"")<<"\"}\n";
                    app.ShutDown();return;
                }
                Check(app.RequestScene({previewPresets[previewIndex],8}),"preview existing scene transaction");
                return;
            }
            if(previewFrames!=8)return;
            Check(!app.m_IsShowDebugBoundingBox && !app.m_ShowSelectedColliderOutlines && debugLines==0,"clean preview has no debug overlays");
            auto& physics=*app.m_ActiveScene->GetPhysicsSystem();auto& w=*physics.GetPhysicsWorld();
            Check(physics.GetCompletedSteps()==0 && w.GetJointCount()==1,"preview authored geometry");
            Check(app.m_RenderTarget!=nullptr,"preview render target");if(failed){app.ShutDown();return;}
            const auto& target=*app.m_RenderTarget;const auto& size=target.Buffer().Description();
            Check(size.Width>0 && size.Height>0 && size.Width<=4096 && size.Height<=4096,"preview extent");
            if(failed){app.ShutDown();return;}
            std::vector<std::byte> pixels(std::size_t(size.Width)*size.Height*4);
            Check(bool(target.ReadColor(pixels)),"actual GEngine preview readback");
            constexpr unsigned width=512,height=288;
            std::array<unsigned char,width*height*4> image{};
            // Center crop and sample the actual rendered target; do not synthesize body geometry.
            const double cropWidth=std::min(double(size.Width),double(size.Height)*width/height);
            const double cropHeight=cropWidth*height/width;
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                auto* dst=image.data()+(y*width+x)*4;
                const auto sx=std::min(size.Width-1,unsigned((size.Width-cropWidth)*.5+(x+.5)*cropWidth/width));
                const auto sy=std::min(size.Height-1,unsigned((size.Height-cropHeight)*.5+(y+.5)*cropHeight/height));
                const auto* src=pixels.data()+((size.Height-1-sy)*size.Width+sx)*4;
                for(unsigned c=0;c<3;++c)dst[c]=std::to_integer<unsigned char>(src[c]);
                dst[3]=255;
            }
            const auto path=previewOutput/(std::string(SceneName(previewPresets[previewIndex]))+".png");
            Check(!std::filesystem::exists(path),"create-only preview image");
            if(!failed)Check(stbi_write_png(path.string().c_str(),width,height,4,image.data(),width*4)!=0,"preview PNG write");
            std::ofstream info(previewOutput/(std::string(SceneName(previewPresets[previewIndex]))+".txt"));
            const auto j=w.ReadJoint(*w.JointAt(0));
            info<<"source=actual ordinary GEngine render target\nscene="<<SceneName(previewPresets[previewIndex])
                <<"\nsource_width="<<size.Width<<"\nsource_height="<<size.Height<<"\noutput=512x288\nphysics_ticks="<<physics.GetCompletedSteps()
                <<"\ndefault_visualization=OFF\ncapture_debug_lines="<<debugLines
                <<"\ncollideConnected="<<j->descriptor.collideConnected<<"\nanchor_error="<<j->error<<"\n";
            // Exercise the same state written by the unchanged ordinary All/Off control.
            // This renderer check is not reported as manual UI input evidence.
            app.m_IsShowDebugBoundingBox=true;
        }
        static bool TraceTick(){return tick+1>=480 && tick+1<=500;}
        static void TraceBegin(RigidBodySimulationApp& app)
        {
            tracePhysics=app.m_ActiveScene->GetPhysicsSystem();
            Check(tracePhysics->GetPhysicsWorld()->GetBackend()==::GEngine::PhysicsBackend::Native && gc && !gcDebug,"Native G_C trace configuration");
            Check(::GEngine::PhysicsDiagnostic::observer==nullptr,"exclusive trace observer");
            traceObserver.detailed=true;
            traceObserver.select=[](void*,const ::GEngine::RigidBody3D*,const ::GEngine::RigidBody3D*) noexcept {return TraceTick();};
            traceObserver.boundary=[](void*,::GEngine::PhysicsDiagnostic::Stage stage,const World&,::GEngine::ManifoldCollector&,
                std::span<const ::GEngine::ContactIsland>,std::size_t,int) noexcept {
                if(!TraceTick())return;
                if(traceRowCount<traceRows.size())traceRows[traceRowCount]={tick+1,static_cast<unsigned>(stage),::GEngine::Phase27GraphAccess::Point(*tracePhysics)};
                ++traceRowCount;
            };
            traceObserver.event=[](void*,const ::GEngine::PhysicsDiagnostic::Event& e) noexcept {
                using O=::GEngine::PhysicsDiagnostic::Operation;
                if(!TraceTick())return;
                switch(e.operation) {
                case O::ToiNormal:case O::ToiFriction:case O::ToiPosition:case O::ToiQuery:case O::Integrate:
                case O::ContactNew:case O::ContactRefresh:case O::ContactRemove:case O::PairReset:case O::WarmStart:case O::Position:break;
                default:return;
                }
                if(traceEventCount<traceEvents.size())traceEvents[traceEventCount]={tick+1,e,::GEngine::Phase27GraphAccess::Point(*tracePhysics)};
                ++traceEventCount;
            };
            ::GEngine::PhysicsDiagnostic::observer=&traceObserver;
        }
        static void TraceFinish()
        {
            const auto header=[](std::ostream& o){
                for(const char* name:{"residual","arm_a","arm_b","bias","impulse"})for(char axis:std::string_view("xyz"))o<<','<<name<<'_'<<axis;
                for(const char* name:{"mass","inertia_a","inertia_b"})for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)o<<','<<name<<'_'<<c<<r;
                for(char who:std::string_view("ab"))for(const char* name:{"px","py","pz","vx","vy","vz","wx","wy","wz","qw","qx","qy","qz"})o<<','<<who<<'_'<<name;
                o<<",dt,inverse_mass_a,inverse_mass_b,active,manifolds,pending_tois\n";
            };
            const auto values=[](std::ostream& o,const auto& p){
                for(auto v:{p.residual,p.armA,p.armB,p.bias,p.impulse})o<<','<<v.x<<','<<v.y<<','<<v.z;
                for(auto m:{p.effectiveMass,p.inverseInertiaA,p.inverseInertiaB})for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)o<<','<<m[c][r];
                for(const auto& b:{p.a,p.b})o<<','<<b.position.x<<','<<b.position.y<<','<<b.position.z<<','<<b.linear.x<<','<<b.linear.y<<','<<b.linear.z
                    <<','<<b.angular.x<<','<<b.angular.y<<','<<b.angular.z<<','<<b.orientation.w<<','<<b.orientation.x<<','<<b.orientation.y<<','<<b.orientation.z;
                o<<','<<p.dt<<','<<p.inverseMassA<<','<<p.inverseMassB<<','<<p.active<<','<<p.manifolds<<','<<p.pendingTois<<'\n';
            };
            std::ofstream o(output/"native-trace-stages.csv");o<<std::setprecision(17)<<"tick,stage";header(o);
            for(unsigned i=0;i<std::min<unsigned>(traceRowCount,traceRows.size());++i){const auto& r=traceRows[i];o<<r.tick<<','<<r.stage;values(o,r.point);}
            o.close();o.open(output/"native-trace-events.csv");o<<std::setprecision(17)<<"tick,operation,time,duration,penetration,normal_x,normal_y,normal_z,impulse_x,impulse_y,impulse_z";header(o);
            for(unsigned i=0;i<std::min<unsigned>(traceEventCount,traceEvents.size());++i){const auto& r=traceEvents[i];const auto& e=r.event;
                o<<r.tick<<','<<static_cast<unsigned>(e.operation)<<','<<e.time<<','<<e.duration<<','<<e.penetration<<','<<e.normal.x<<','<<e.normal.y<<','<<e.normal.z
                    <<','<<e.linearImpulseB.x<<','<<e.linearImpulseB.y<<','<<e.linearImpulseB.z;values(o,r.point);}
        }
        static bool Begin(RigidBodySimulationApp& app)
        {
            const char* directory=std::getenv("GENGINE_PHASE27_OUTPUT");if(!directory)return false;
            if(started){if(gcDebug && gcTransition)GcBind(app);return true;}
            output=directory;target=std::getenv("GENGINE_PHASE27_DEBUG")?88:1200;
            minimal=std::getenv("GENGINE_PHASE27_MINIMAL")!=nullptr;
            visualProbe=std::getenv("GENGINE_PHASE27_VISUAL_PROBE")!=nullptr;
            repeatProbe=std::getenv("GENGINE_PHASE27_VISUAL_REPEAT")!=nullptr;
            if(visualProbe){target=600;minimal=true;}
            nativeTrace=std::getenv("GENGINE_PHASE27_NATIVE_TRACE")!=nullptr;
            gc=app.m_ActiveSelection.scene==RbsScenePreset::BallSocketContactEnabled;
            gcDebug=gc && target==88;
            if(gc){repeatProbe=true;if(gcDebug){target=160;minimal=true;}
                lifecycle.open(std::filesystem::path(directory)/"gc-lifecycle.csv");
                lifecycle<<"tick,event,backend,construction_cpp_calls,construction_cpp_bytes\n";
                impulses.open(std::filesystem::path(directory)/"gc-impulses.csv");impulses<<std::setprecision(17);
                impulses<<"consumed_tick,target,point_x,point_y,point_z,vx,vy,vz,jx,jy,jz,work\n";
            }
            Check(output.is_absolute() && std::filesystem::is_directory(output),"existing absolute evidence directory");
            Check(!std::filesystem::exists(output/"result.json"),"create-only evidence");
            Check(app.m_ActiveSelection.IsBallSocketScene(),"S/P/G selection");
            if(target==88){Unit();GraphLifecycle();}
            auto& w=*app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            Check(w.GetJointCount()==1,"one point joint");
            const auto h=w.JointAt(0);Check(bool(h),"point handle");
            if(h) {joint=*h;const auto j=w.ReadJoint(joint);if(j)bodies={j->descriptor.bodyA,j->descriptor.bodyB};else Check(false,"initial point state");}
            Capture(w,samples[0]);
            if(visualProbe || gc) {
                Check(app.m_ActiveSelection.scene==RbsScenePreset::BallSocketContact || gc,"visual G/G_C only");
                if(gc)GcBind(app);
                if(w.GetBackend()==GEngine::PhysicsBackend::Jolt)Check(bool(w.EnableJoltDiagnostics(true)),"visual contact diagnostics enable");
                visualStates.open(output/"visual-states.csv");visualStates<<std::setprecision(17);
                visualStates<<"tick,body,handle,radius,x,y,z,current_x,current_y,current_z,previous_x,previous_y,previous_z,render_x,render_y,render_z,scale_x,scale_y,scale_z,ccd,collide_connected,anchor_ax,anchor_ay,anchor_az,anchor_bx,anchor_by,anchor_bz,manual_count\n";
                visualContacts.open(output/"visual-contacts.csv");visualContacts<<std::setprecision(17);
                visualContacts<<"tick,handle_a,handle_b,reported_depth_native_separation\n";
                VisualCapture(app);
            }
            if(w.GetBackend()==GEngine::PhysicsBackend::Native) {
                Check(GEngine::Phase27GraphAccess::Prepared(*app.m_ActiveScene->GetPhysicsSystem(),w.GetBodyCount(),1),
                    "ordinary scene prepared graph capacities before first tick");
                graphInitial=GEngine::Phase27GraphAccess::Read(*app.m_ActiveScene->GetPhysicsSystem());
            }
            GEngine::Phase27Audit::arena=[](const auto& event) noexcept {
                const unsigned n=arenaCount.fetch_add(1,std::memory_order_relaxed);
                if(n<arena.size())arena[n]={event,tick+1};
            };
            if(minimal)GEngine::Phase27Audit::arena=nullptr;
            GEngine::Phase27Audit::heap=[](const auto& event) noexcept {
                const unsigned n=heapCount.fetch_add(1,std::memory_order_relaxed);
                if(n<heap.size())heap[n]={event,tick+1};
            };
            if(minimal)GEngine::Phase27Audit::heap=nullptr;
            nativeAttribution=std::getenv("GENGINE_PHASE27_NATIVE_ALLOCATION")!=nullptr;
            if(nativeAttribution) {
                Check(w.GetBackend()==GEngine::PhysicsBackend::Native && !minimal && target==1200,
                    "Native allocation observation configuration");
                Check(GEngine::PhysicsDiagnostic::observer==nullptr,"exclusive stage observer");
                nativeObserver.boundary=[](void*,GEngine::PhysicsDiagnostic::Stage stage,const World&,
                    GEngine::ManifoldCollector& manifolds,std::span<const GEngine::ContactIsland> islands,
                    std::size_t storage,int) noexcept {
                    if(tick+1>=nativeStages.size())return;
                    auto& r=nativeStages[tick+1][static_cast<unsigned>(stage)];
                    r.calls=GEngine::JoltAudit::cppCalls.load(std::memory_order_relaxed);
                    r.bytes=GEngine::JoltAudit::cppBytes.load(std::memory_order_relaxed);
                    r.storage=storage;r.manifolds=manifolds.m_Manifolds.size();r.islands=islands.size();++r.visits;
                    for(const auto& island:islands) {
                        r.dynamicCapacity+=island.dynamicBodies.capacity();
                        r.boundaryCapacity+=island.boundaryBodies.capacity();
                        r.pairCapacity+=island.contactPairs.capacity();
                    }
                };
                GEngine::PhysicsDiagnostic::observer=&nativeObserver;
            }
            if(nativeTrace){Check(!nativeAttribution,"trace separate from cost observer");target=500;minimal=true;
                ::GEngine::Phase27Audit::arena=nullptr;::GEngine::Phase27Audit::heap=nullptr;TraceBegin(app);}
            started=true;
            return true;
        }
        static void BeforeTick()
        {
            GEngine::JoltAudit::Reset();GEngine::JoltAudit::countAllocations=true;
            frameStart=GEngine::JoltAudit::Now();
        }
        static bool AfterTick(RigidBodySimulationApp& app)
        {
            using namespace GEngine;
            JoltAudit::countAllocations=false;
            auto& physics=*app.m_ActiveScene->GetPhysicsSystem();auto& w=*physics.GetPhysicsWorld();
            ++tick;Check(tick<=target && tick<samples.size(),"sample bound");
            if(tick>=samples.size())return Finish(false);
            auto& s=samples[tick];
            s.inclusive=physics.GetLastUpdateNanoseconds();s.jointNs=physics.GetJointSolveNanoseconds();s.contactNs=physics.GetContactSolveNanoseconds();
            s.cppCalls=JoltAudit::cppCalls;s.cppBytes=JoltAudit::cppBytes;s.heapCalls=JoltAudit::joltCalls;s.heapBytes=JoltAudit::joltBytes;
            if(nativeAttribution) {
                for(const auto& stage:nativeStages[tick])Check(stage.visits==1,"one observation per Native stage");
                Check(nativeStages[tick].back().calls==s.cppCalls && nativeStages[tick].back().bytes==s.cppBytes,
                    "stage totals cover complete Update allocation boundary");
                Check(::GEngine::Phase27GraphAccess::Read(physics)==graphInitial,"no graph capacity/storage growth during qualification");
            }
            if(tick==301) {
                const bool swing=app.m_ActiveSelection.scene==RbsScenePreset::BallSocketSwing;
                const auto& b=samples[300].bodies[swing?1:0];
                const Vec impulse=swing?Vec(.3f,0,.4f):Vec(0,0,.5f);
                const double added=glm::dot(impulse,b.linearVelocity)+.5*glm::dot(impulse,impulse)*b.inverseMass;
                work+=added;
                if(gc)impulses<<tick<<','<<bodies[0].index<<','<<b.position.x<<','<<b.position.y<<','<<b.position.z<<','
                    <<b.linearVelocity.x<<','<<b.linearVelocity.y<<','<<b.linearVelocity.z<<",0,0,0.5,"<<added<<'\n';
            }
            if(gc && (tick==421 || tick==481 || tick==541)) {
                const auto& b=samples[tick-1].bodies[0];const Vec impulse(5,0,0);
                const double added=glm::dot(impulse,b.linearVelocity)+.5*glm::dot(impulse,impulse)*b.inverseMass;
                work+=added;
                Check(app.m_ChainImpulse.AppliedCount()==(tick-421)/60+1,"G_C exact ordinary impulse consumed");
                impulses<<tick<<','<<bodies[0].index<<','<<b.position.x<<','<<b.position.y<<','<<b.position.z<<','
                    <<b.linearVelocity.x<<','<<b.linearVelocity.y<<','<<b.linearVelocity.z<<",5,0,0,"<<added<<'\n';
            }
            const auto observerStart=JoltAudit::Now();Capture(w,s);
            if(visualProbe || gc)VisualCapture(app);
            s.observerNs=JoltAudit::Now()-observerStart;
            Check(!physics.HasJointFailure() && !physics.GetBackendFailure(),"runtime solver failure");
            Check(physics.GetCompletedSteps()==tick-worldStartTick,"one ordinary fixed tick per frame");
            Check(arenaCount<=arena.size(),"arena recorder overflow");
            Check(heapCount<=heap.size(),"heap recorder overflow");
            Check(traceRowCount<=traceRows.size() && traceEventCount<=traceEvents.size(),"Native focused trace overflow");
            Check(std::isfinite(s.energy) && std::isfinite(s.joint.error),"finite energy/residual");
            if(gcDebug)GcLifecycle(app);
            // Full quantitative audit uses every exported state, including failures.
            if(failed || tick==target)return Finish(!failed);
            return false;
        }
        static bool Finish(bool complete)
        {
            using namespace GEngine;
            if(visualProbe || gc){visualStates.close();visualContacts.close();}
            if(gc){lifecycle.close();impulses.close();}
            JoltAudit::countAllocations=false;Phase27Audit::arena=nullptr;Phase27Audit::heap=nullptr;
            if(nativeAttribution || nativeTrace)PhysicsDiagnostic::observer=nullptr;
            if(nativeTrace)TraceFinish();
            std::ofstream o(output/"samples.csv");o<<std::setprecision(17);
            o<<"tick,energy,work,error,rx,ry,rz,inclusive_ns,joint_ns,contact_ns,cpp_calls,cpp_bytes,heap_calls,heap_bytes,observer_ns\n";
            for(unsigned t=0;t<=tick && t<samples.size();++t){const auto& s=samples[t];o<<t<<','<<s.energy<<','<<s.work<<','<<s.joint.error<<','<<s.joint.residual.x<<','<<s.joint.residual.y<<','<<s.joint.residual.z<<','<<s.inclusive<<','<<s.jointNs<<','<<s.contactNs<<','<<s.cppCalls<<','<<s.cppBytes<<','<<s.heapCalls<<','<<s.heapBytes<<','<<s.observerNs<<'\n';}
            o.close();o.open(output/"bodies.csv");o<<std::setprecision(17)<<"tick,body,x,y,z,vx,vy,vz,wx,wy,wz,qw,qx,qy,qz,inverse_mass,sleeping\n";
            for(unsigned t=0;t<=tick && t<samples.size();++t)for(unsigned i=0;i<2;++i){const auto& b=samples[t].bodies[i];o<<t<<','<<i<<','<<b.position.x<<','<<b.position.y<<','<<b.position.z<<','<<b.linearVelocity.x<<','<<b.linearVelocity.y<<','<<b.linearVelocity.z<<','<<b.angularVelocity.x<<','<<b.angularVelocity.y<<','<<b.angularVelocity.z<<','<<b.orientation.w<<','<<b.orientation.x<<','<<b.orientation.y<<','<<b.orientation.z<<','<<b.inverseMass<<','<<b.sleeping<<'\n';}
            o.close();o.open(output/"arena.csv");o<<"tick,reserve,pointer,caller,requested,aligned,used\n";
            for(unsigned i=0;i<std::min<unsigned>(arenaCount,arena.size());++i){const auto& r=arena[i];const auto& e=r.event;o<<r.tick<<','<<e.reserve<<','<<e.address<<','<<e.caller<<','<<e.requested<<','<<e.aligned<<','<<e.used<<'\n';}
            o.close();o.open(output/"heap.csv");o<<"tick,reserve,pointer,caller,requested\n";
            for(unsigned i=0;i<std::min<unsigned>(heapCount,heap.size());++i){const auto& r=heap[i];const auto& e=r.event;o<<r.tick<<','<<e.reserve<<','<<e.address<<','<<e.caller<<','<<e.requested<<'\n';}
            if(nativeAttribution) {
                o.close();o.open(output/"native-stages.csv");
                o<<"tick,stage,calls,bytes,named_storage,manifolds,islands,dynamic_capacity,boundary_capacity,pair_capacity,visits\n";
                for(unsigned t=1;t<=tick && t<nativeStages.size();++t)for(unsigned stage=0;stage<12;++stage) {
                    const auto& r=nativeStages[t][stage];
                    o<<t<<','<<stage<<','<<r.calls<<','<<r.bytes<<','<<r.storage<<','<<r.manifolds<<','<<r.islands<<','
                        <<r.dynamicCapacity<<','<<r.boundaryCapacity<<','<<r.pairCapacity<<','<<r.visits<<'\n';
                }
            }
            o.close();o.open(output/"construction.csv");
            o<<"cycle,calls,bytes,replacement_calls,replacement_bytes\n";
            if(target==88)for(unsigned i=0;i<construction.size();++i) {
                const auto& c=construction[i];o<<i<<','<<c.calls<<','<<c.bytes<<','<<c.replacementCalls<<','<<c.replacementBytes<<'\n';
            }
            o.close();o.open(output/"graph-capacity.csv");
            o<<"edges,nodes,counts,contact_islands,activation_islands,contact_nested_bytes,activation_nested_bytes,edge_pointer\n";
            o<<graphInitial.edges<<','<<graphInitial.nodes<<','<<graphInitial.counts<<','<<graphInitial.contactIslands<<','
                <<graphInitial.activationIslands<<','<<graphInitial.contactBytes<<','<<graphInitial.activationBytes<<','<<graphInitial.edgeStorage<<'\n';
            o.close();o.open(output/"result.json");o<<"{\"complete\":"<<(complete?"true":"false")<<",\"ticks\":"<<tick<<",\"unit_ticks\":"<<unitTicks<<",\"arena_events\":"<<arenaCount<<",\"heap_events\":"<<heapCount<<",\"map_anchor\":"<<reinterpret_cast<std::uintptr_t>(g_Phase27TargetSizes)<<",\"failure\":\""<<(failure?failure:"")<<"\"}\n";
            return true;
        }
    };
}
#endif
