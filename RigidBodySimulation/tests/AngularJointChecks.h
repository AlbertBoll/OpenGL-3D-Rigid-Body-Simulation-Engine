#pragma once
#ifdef GENGINE_PHASE28_QUALIFICATION
#include "RigidBodySimulation.h"
#include "RbsNumericInput.h"
#include "RbsEditorWidgets.h"
#include "Renderer/SceneRenderResources.h"
#include "Renderer/RenderVisibility.h"
#include "Renderer/FrameSubmission.h"
#include "Managers/AssetsManager.h"
#include "Scene/_Scene.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsProfile.h"
#include "Physics/JoltAuditObservation.h"
#include "Physics/Constraints/AngularJoint.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <variant>
#include <vector>
#include <string_view>
#include <sdl2/SDL.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <stb_image/stb_image_write.h>
#include <glad/glad.h>

extern "C" const std::uint64_t g_Phase27TargetSizes[];
extern "C" std::atomic<std::uint64_t> g_Phase28ObserverWrapperNs;
extern "C" std::atomic<std::uint64_t> g_Phase28ObserverWrapperCalls;
extern "C" std::atomic<std::uint64_t> g_Phase28JoltStageNs[8];
extern "C" std::atomic<std::uint64_t> g_Phase28JoltStageCalls[8];
extern "C" std::atomic<std::uint64_t> g_Phase28JoltProfileHookNs;
extern "C" std::atomic<std::uint64_t> g_Phase28JoltProfileHookCalls;

namespace Rbs::RestDiagnosis { bool Phase28DefaultInitProbe(std::ostream&); }

namespace Rbs
{
    // Opt-in bounded evidence from the ordinary RBS process. The fresh wrap
    // control uses the same provider in this process, with a separate world.
    struct AngularJointChecks
    {
        using World=::GEngine::PhysicsWorld;
        using Body=::GEngine::PhysicsBodyHandle;
        using Joint=::GEngine::PhysicsJointHandle;
        using Vec=::GEngine::Math::Vec3f;
        using Quat=::GEngine::Math::Quat;
        struct Sample {
            ::GEngine::PhysicsBodyState bar{};
            ::GEngine::JointState joint{};
            double rawEnergy{},intendedEnergy{},work{};
            std::uint64_t inclusiveNs{},jointNs{},contactNs{},observerNs{};
            std::uint64_t minimalNs{},fullNs{};
            std::uint64_t wrapperNs{},wrapperCalls{},captureNs{};
            std::uint64_t observerUpperNs{},observerRefinedNs{};
            std::uint64_t cppCalls{},cppBytes{},jphCalls{},jphBytes{};
            std::array<std::uint64_t,3> scopedCppCalls{},scopedCppBytes{};
            std::uint64_t boundaryObserverNs{};
            std::array<std::uint64_t,8> joltStageNs{},joltStageCalls{};
            std::uint64_t sdkUpdateNs{},profileHookNs{},profileHookCalls{};
            std::uint64_t nativeContactPairs{};
            bool preStepActive{};
        };
        struct ArenaRow {::GEngine::Phase27Audit::ArenaEvent event{};unsigned tick{};bool wrap{};};
        struct HeapRow {::GEngine::Phase27Audit::HeapEvent event{};unsigned tick{};bool wrap{};};
        struct ImpulseRow {unsigned tick{};Vec impulse{},before{};double mass{},work{};};
        struct ImpulsePointSample { Vec com{},point{},angularBefore{},linearAfter{},angularAfter{}; };
        static inline std::array<Sample,601> normal{};
        static inline std::array<Sample,241> wrap{};
        static inline std::array<ArenaRow,40000> arena{};
        static inline std::array<HeapRow,8000> heap{};
        static inline std::array<ImpulseRow,2> impulses{};
        static inline std::array<ImpulsePointSample,2> impulsePoints{};
        static inline unsigned tick{},normalTarget{},wrapTicks{},unitTicks{},eventTick{},impulseCount{};
        static inline std::atomic<unsigned> arenaCount{},heapCount{};
        static inline bool started{},failed{},wrapEvent{},debug{};
        static inline const char* failure{};
        static inline const char* caseName{};
        static inline std::filesystem::path output;
        static inline Joint joint{},wrapJoint{};
        static inline Body anchor{},bar{},wrapBar{};
        static inline World* observedWorld{};
        // Cumulative global-new counters from the existing Native pipeline boundaries.
        // Capture to fixed storage and write the table only after the final Physics tick.
        struct NativeStageSample {
            std::uint64_t calls,bytes,queries,epa,storage;
        };
        static constexpr unsigned NativeStageCount=1+static_cast<unsigned>(::GEngine::PhysicsDiagnostic::Stage::End);
        static inline std::array<std::array<NativeStageSample,NativeStageCount>,601> nativeStages{};
        static inline std::array<unsigned,601> nativeStageVisits{};
        static inline ::GEngine::PhysicsDiagnostic::Observer nativeStageObserver{};
        static inline bool nativeStageCapture{};
        static inline bool contactV2{};
        static inline bool contactV3{};
        static inline bool contactV4{};
        static inline Vec contactFloorCenter{},contactFloorHalf{};
        static inline Quat contactFloorOrientation{1.f,0.f,0.f,0.f};
        static inline double work{},wrapWork{};
        static inline std::uint64_t clockQuantumNs{};
        static inline std::uint64_t completeUpdateNs{};
        static inline std::uint64_t boundaryObserverNs{};
        static inline std::array<std::uint64_t,3> scopeStartCalls{},scopeStartBytes{},scopeCalls{},scopeBytes{};
        static inline ::GEngine::Phase26Observation::Observer completeUpdateObserver{};
        static bool OwnsActions() { return std::getenv("GENGINE_PHASE28_OUTPUT") &&
            (!std::getenv("GENGINE_PHASE28_MODE") ||
             std::string_view(std::getenv("GENGINE_PHASE28_MODE"))!="editor"); }

        static void Check(bool ok,const char* message) {
            if(!ok&&!failed){failed=true;failure=message;}
        }
        static std::uint64_t Quantum() {
            std::uint64_t last=::GEngine::JoltAudit::Now(),q=std::numeric_limits<std::uint64_t>::max();
            for(unsigned i=0;i<10000;++i){auto now=::GEngine::JoltAudit::Now();if(now>last)q=std::min(q,now-last);last=now;}
            return q==std::numeric_limits<std::uint64_t>::max()?0:q;
        }
        static void ResetJoltProfile() noexcept {
            for(unsigned i=0;i<8;++i){g_Phase28JoltStageNs[i]=0;g_Phase28JoltStageCalls[i]=0;}
            g_Phase28JoltProfileHookNs=0;g_Phase28JoltProfileHookCalls=0;
        }
        static void CaptureJoltProfile(Sample& sample) noexcept {
            for(unsigned i=0;i<8;++i){
                sample.joltStageNs[i]=g_Phase28JoltStageNs[i].load(std::memory_order_relaxed);
                sample.joltStageCalls[i]=g_Phase28JoltStageCalls[i].load(std::memory_order_relaxed);
            }
            sample.sdkUpdateNs=::GEngine::JoltAudit::ns[3];
            sample.profileHookNs=g_Phase28JoltProfileHookNs.load(std::memory_order_relaxed);
            sample.profileHookCalls=g_Phase28JoltProfileHookCalls.load(std::memory_order_relaxed);
        }
        static std::uint64_t NativeContactPairs(const ::GEngine::PhysicsSystem& physics) noexcept {
            std::uint64_t count{};
            for(const auto& island:physics.GetContactIslands())count+=island.contactPairs.size();
            return count;
        }
        static Body MakeBox(World& world,Vec position,Vec half,bool fixed) {
            auto shape=::GEngine::CreateCollisionShape(::GEngine::BoxShapeDesc{half});
            if(!shape)return {};
            ::GEngine::PhysicsBodyDesc desc;desc.position=position;desc.mass=1;
            desc.type=fixed ? ::GEngine::Component::BodyType::Static :
                ::GEngine::Component::BodyType::Dynamic;
            desc.material.friction=.5f;desc.material.restitution=0;
            auto result=world.CreateBody(desc,*shape);return result?*result:Body{};
        }
        static Quat FrameForWorldAxis(Vec x,Vec y,Vec z) {
            // The authored frame uses X as the primary joint axis.
            ::GEngine::Math::Mat3 matrix(1);matrix[0]=x;matrix[1]=y;matrix[2]=z;
            return glm::quat_cast(matrix);
        }
        static ::GEngine::AngularJointDesc Desc(::GEngine::JointKind kind,Body a,Body b) {
            ::GEngine::AngularJointDesc d;d.kind=kind;d.bodyA=a;d.bodyB=b;
            d.localAnchorA=Vec(0,-.25f,0);d.localAnchorB=Vec(0,1,0);d.collideConnected=false;
            if(kind==::GEngine::JointKind::Hinge)
                d.angular.localFrameA=d.angular.localFrameB=FrameForWorldAxis(Vec(0,0,1),Vec(1,0,0),Vec(0,1,0));
            else
                d.angular.localFrameA=d.angular.localFrameB=FrameForWorldAxis(Vec(0,1,0),Vec(1,0,0),Vec(0,0,-1));
            d.angular.lower=-.7853981633974483f;d.angular.upper=.7853981633974483f;
            d.angular.halfAngleU=d.angular.halfAngleV=.7853981633974483f;
            d.angular.limitsEnabled=kind!=::GEngine::JointKind::ConstantVelocity;
            return d;
        }
        static void PureSemantics() {
            using namespace ::GEngine;
            constexpr float pi=3.14159265358979323846f;
            AngularJointDesc cv;cv.kind=JointKind::LimitedConstantVelocity;
            cv.angular.limitsEnabled=true;cv.angular.halfAngleU=cv.angular.halfAngleV=pi/4;
            const Quat id(1,0,0,0);
            const float w=std::sqrt(1.f-2*.38f*.38f);
            const Quat corner(w,0,.38f,.38f);
            const auto inside=AngularJoint::Observe(cv,id,corner),same=AngularJoint::Observe(cv,id,-corner);
            Check(AngularJoint::Valid(cv)&&inside.limitU==0&&inside.limitV==0&&
                inside.violationU==0&&inside.violationV==0,"book CV pyramid corner inside");
            Check(std::abs(inside.angleU-same.angleU)<1.e-6f&&
                std::abs(inside.angleV-same.angleV)<1.e-6f&&
                std::abs(inside.cvError-same.cvError)<1.e-6f,"CV q sign invariant");
            const Quat outside(std::sqrt(1.f-2*.4f*.4f),0,.4f,.4f);
            const auto beyond=AngularJoint::Observe(cv,id,outside);
            Check(beyond.limitU==1&&beyond.limitV==1&&beyond.violationU>0&&beyond.violationV>0,
                "independent CV U V upper limits");
            const auto twist=AngularJoint::Observe(cv,id,glm::angleAxis(.2f,Vec(1,0,0)));
            Check(twist.cvError>.19f,"CV angular relation excludes twist");
            cv.angular.halfAngleU=0;Check(!AngularJoint::Valid(cv),"zero CV half angle rejected");
            cv.angular.halfAngleU=pi/4;cv.angular.localFrameB=Quat(2,0,0,0);
            Check(!AngularJoint::Valid(cv),"nonunit frame rejected");
            AngularJointDesc hinge;hinge.kind=JointKind::Hinge;hinge.angular.limitsEnabled=true;
            hinge.angular.lower=-pi/4;hinge.angular.upper=pi/4;
            const auto upper=AngularJoint::Observe(hinge,id,glm::angleAxis(.9f,Vec(1,0,0)));
            const auto lower=AngularJoint::Observe(hinge,id,glm::angleAxis(-.9f,Vec(1,0,0)));
            Check(upper.limitU==1&&lower.limitU==-1&&upper.violationU>0&&lower.violationU>0,
                "hinge signed lower and upper");
            const auto q=glm::angleAxis(.6f,Vec(1,0,0));
            Check(std::abs(AngularJoint::Observe(hinge,id,q).signedAngle-
                AngularJoint::Observe(hinge,id,-q).signedAngle)<1.e-6f,"hinge q sign invariant");
            hinge.angular.lower=.1f;Check(!AngularJoint::Valid(hinge),"invalid signed interval rejected");
            // Quaternion component gradients are the exact local Jacobians of
            // 2*q_{x,y,z} under left angular perturbation; check all three at
            // four nontrivial states before any provider step.
            for(const Quat initial:{Quat(.9f,.1f,.2f,.3f),Quat(.7f,-.4f,.2f,.1f),
                                    Quat(.8f,.2f,-.3f,.1f),Quat(.6f,.2f,.3f,-.4f)}) {
                const Quat q=glm::normalize(initial);const float h=.01f;
                const std::array<Vec,3> gradients{Vec(q.w,q.z,-q.y),Vec(-q.z,q.w,q.x),Vec(q.y,-q.x,q.w)};
                for(int axis=0;axis<3;++axis){Vec e(0);e[axis]=1;
                    const auto plus=glm::normalize(Quat(1,h*e.x/2,h*e.y/2,h*e.z/2)*q);
                    const auto minus=glm::normalize(Quat(1,-h*e.x/2,-h*e.y/2,-h*e.z/2)*q);
                    const std::array<float,3> plusXYZ{plus.x,plus.y,plus.z};
                    const std::array<float,3> minusXYZ{minus.x,minus.y,minus.z};
                    for(int c=0;c<3;++c)Check(std::abs((2*plusXYZ[c]-2*minusXYZ[c])/(2*h)-gradients[c][axis])<1.e-3f,
                        "book quaternion Jacobian finite difference");
                }
            }
        }
        static void Focused(::GEngine::PhysicsBackend backend) {
            using namespace ::GEngine;
            PureSemantics();
            for(auto kind:{JointKind::Hinge,JointKind::ConstantVelocity,JointKind::LimitedConstantVelocity}) {
                PhysicsSystem system;system.SetPhysicsWorld(new World(backend,Vec(0)));
                auto& w=*system.GetPhysicsWorld();Check(bool(w.ConfigureBackend(backend)),"focused backend setup");
                const auto a=MakeBox(w,Vec(0,3,0),Vec(.25f),true);
                const auto b=MakeBox(w,Vec(0,1.75f,0),Vec(.15f,1,.15f),false);
                Check(bool(a)&&bool(b),"focused body creation");if(!a||!b){system.OnExit();continue;}
                auto d=Desc(kind,a,b);
                const auto h=w.CreateAngularJoint(d);
                Check(bool(h)&&w.GetJointCount()==1,"typed angular creation");
                if(h){const auto state=w.ReadJoint(*h);Check(state&&state->descriptor.kind==kind&&
                    state->descriptor.bodyA==a&&state->descriptor.bodyB==b,"typed descriptor readback");}
                auto bad=d;bad.bodyB=a;
                Check(!w.CreateAngularJoint(bad)&&w.GetJointCount()==1,"self joint atomic rejection");
                bad=d;bad.localAnchorA.x=std::numeric_limits<float>::quiet_NaN();
                Check(!w.CreateAngularJoint(bad)&&w.GetJointCount()==1,"nonfinite anchor atomic rejection");
                bad=d;bad.angular.localFrameB=Quat(2,0,0,0);
                Check(!w.CreateAngularJoint(bad)&&w.GetJointCount()==1,"nonunit frame atomic rejection");
                World foreign(backend);bad=d;bad.bodyB=MakeBox(foreign,Vec(0),Vec(.25f),false);
                Check(!w.CreateAngularJoint(bad)&&w.GetJointCount()==1,"foreign endpoint rejection");
                system.Update(::GEngine::Timestep(1.0/60.0));++unitTicks;
                Check(!system.HasJointFailure()&&!system.GetBackendFailure(),"focused finite step");
                if(h){Check(bool(w.SetBodyMass(b,2)),"mass mutation");
                    const auto afterMass=w.ReadJoint(*h);
                    Check(afterMass && !afterMass->angular.impulseAvailable,
                        "joint survives mass mutation with angular impulse cache invalidated");
                    Check(bool(w.DestroyBody(b))&&!w.ReadJoint(*h)&&w.GetJointCount()==0,
                        "dependent retirement and stale read rejection");}
                system.OnExit();
            }
        }
        static double Kinetic(const ::GEngine::PhysicsBodyState& b,double scale) {
            if(!(b.inverseMass>0))return 0;
            const double m=1.0/b.inverseMass;
            const Vec local=glm::conjugate(b.orientation)*b.angularVelocity;
            const double iPerp=m*scale*scale*(1+.15*.15)/3;
            const double iLong=m*scale*scale*(.15*.15+.15*.15)/3;
            return .5*m*double(glm::dot(b.linearVelocity,b.linearVelocity))+
                .5*(iPerp*(double(local.x)*local.x+double(local.z)*local.z)+iLong*double(local.y)*local.y);
        }
        static void Capture(World& w,Joint h,Body dynamic,Sample& out,double cumulativeWork) {
            auto j=w.ReadJoint(h);auto b=w.ReadBody(dynamic);
            Check(bool(j)&&bool(b),"authoritative body and joint readback");
            if(!j||!b)return;
            out.joint=*j;out.bar=*b;out.work=cumulativeWork;
            const double m=1.0/b->inverseMass,g=-double(w.GetGravity().y);
            const double k=Kinetic(*b,1);
            const Vec desired=j->worldAnchorA-b->orientation*j->descriptor.localAnchorB;
            out.rawEnergy=k+m*g*b->position.y;
            out.intendedEnergy=k+m*g*desired.y;
            Check(std::isfinite(out.rawEnergy)&&std::isfinite(out.intendedEnergy)&&
                std::isfinite(j->error)&&::GEngine::Math::IsFinite(b->position),"finite physical state");
        }
        static void SetObservers() {
            completeUpdateObserver.context=nullptr;
            completeUpdateObserver.begin=[](void*) noexcept {
                const auto start=::GEngine::JoltAudit::Now();
                ::GEngine::JoltAudit::Reset();
                scopeCalls={};scopeBytes={};scopeStartCalls={};scopeStartBytes={};
                ::GEngine::JoltAudit::countAllocations=true;
                boundaryObserverNs=::GEngine::JoltAudit::Now()-start+2*clockQuantumNs;
            };
            completeUpdateObserver.end=[](void*,std::uint64_t ns) noexcept {
                const auto start=::GEngine::JoltAudit::Now();
                ::GEngine::JoltAudit::countAllocations=false;
                completeUpdateNs=ns;
                boundaryObserverNs+=::GEngine::JoltAudit::Now()-start+2*clockQuantumNs;
            };
            completeUpdateObserver.allocation=[](void*,unsigned kind,bool begin) noexcept {
                if(kind>=scopeCalls.size() || !::GEngine::JoltAudit::countAllocations.load(std::memory_order_relaxed))return;
                const auto calls=::GEngine::JoltAudit::cppCalls.load(std::memory_order_relaxed);
                const auto bytes=::GEngine::JoltAudit::cppBytes.load(std::memory_order_relaxed);
                if(begin){scopeStartCalls[kind]=calls;scopeStartBytes[kind]=bytes;}
                else {scopeCalls[kind]+=calls-scopeStartCalls[kind];scopeBytes[kind]+=bytes-scopeStartBytes[kind];}
            };
            ::GEngine::Phase26Observation::observer=&completeUpdateObserver;
            if(nativeStageCapture) {
                using namespace ::GEngine;
                if(PhysicsDiagnostic::observer) {
                    Check(false,"another observer owns the Native stage boundary");
                } else {
                    nativeStageObserver.boundary=[](void*,PhysicsDiagnostic::Stage stage,const World& world,
                        ManifoldCollector&,std::span<const ContactIsland>,std::size_t storage,int) noexcept {
                        const auto start=JoltAudit::Now();
                        const auto index=static_cast<unsigned>(stage);
                        if(&world!=observedWorld || eventTick==0 || eventTick>=nativeStages.size() ||
                           index>=NativeStageCount || index!=nativeStageVisits[eventTick] ||
                           !JoltAudit::countAllocations.load(std::memory_order_relaxed)) {
                            Check(false,"Native allocation stage order, world or measurement scope changed");
                            return;
                        }
                        const auto profile=GetPhysicsProfileSnapshot();
                        nativeStages[eventTick][index]={JoltAudit::cppCalls.load(std::memory_order_relaxed),
                            JoltAudit::cppBytes.load(std::memory_order_relaxed),profile.narrowphaseCallCount,
                            profile.epaCallCount,static_cast<std::uint64_t>(storage)};
                        ++nativeStageVisits[eventTick];
                        boundaryObserverNs+=JoltAudit::Now()-start+2*clockQuantumNs;
                    };
                    PhysicsDiagnostic::observer=&nativeStageObserver;
                }
            }
            ::GEngine::Phase27Audit::arena=[](const auto& e) noexcept {
                const auto i=arenaCount++;if(i<arena.size())arena[i]={e,eventTick,wrapEvent};
            };
            ::GEngine::Phase27Audit::heap=[](const auto& e) noexcept {
                const auto i=heapCount++;if(i<heap.size())heap[i]={e,eventTick,wrapEvent};
            };
        }
        static void TakeImpulse(World& w,Body dynamic,Vec impulse,unsigned afterTick) {
            auto state=w.ReadBody(dynamic);Check(bool(state),"pre impulse body readback");if(!state)return;
            const bool cvStrike=contactV4 && afterTick==300;
            if(cvStrike)impulse=Vec(-.767537787f,.383768893f,2.874649054f);
            const double mass=1.0/state->inverseMass;
            double delta=glm::dot(impulse,state->linearVelocity)+
                .5*glm::dot(impulse,impulse)*state->inverseMass;
            const bool hingeStrike=contactV3 && afterTick==300 && std::string_view(caseName)=="H_CONTACT";
            const bool pointHit=hingeStrike || cvStrike;
            Vec point=state->centerOfMass;
            if(pointHit)point+=state->orientation*Vec(0.f,cvStrike?-.25f:-.75f,0.f);
            const Vec angularMomentum=glm::cross(point-state->centerOfMass,impulse);
            if(pointHit) {
                delta+=glm::dot(angularMomentum,state->angularVelocity)+
                       .5*glm::dot(angularMomentum,state->inverseInertiaWorld*angularMomentum);
                Check(bool(w.ApplyImpulse(dynamic,point,impulse)),"admitted material-point impulse applied");
            } else {
                Check(bool(w.ApplyLinearImpulse(dynamic,impulse)),"admitted central impulse applied");
            }
            if(contactV3 && impulseCount<impulsePoints.size()) {
                const auto after=w.ReadBody(dynamic);
                Check(bool(after),"post impulse command readback");
                if(after)impulsePoints[impulseCount]={state->centerOfMass,point,state->angularVelocity,
                                                      after->linearVelocity,after->angularVelocity};
            }
            if(impulseCount<impulses.size())impulses[impulseCount++]={afterTick,impulse,state->linearVelocity,mass,delta};
            else Check(false,"impulse recorder overflow");
            work+=delta;
        }
        static bool WrapControl(::GEngine::PhysicsBackend backend) {
            using namespace ::GEngine;
            PhysicsSystem system;system.SetPhysicsWorld(new World(backend,Vec(0)));
            auto& w=*system.GetPhysicsWorld();Check(bool(w.ConfigureBackend(backend)),"wrap backend setup");
            const auto a=MakeBox(w,Vec(0,3,0),Vec(.25f),true);
            const auto b=MakeBox(w,Vec(0,1.75f,0),Vec(.15f,1,.15f),false);
            auto d=Desc(JointKind::Hinge,a,b);d.angular.limitsEnabled=false;
            const auto h=w.CreateAngularJoint(d);
            Check(bool(h),"fresh free hinge wrap joint");if(!h){system.OnExit();return false;}
            wrapJoint=*h;wrapBar=b;
            Check(bool(w.SetBodyVelocity(b,Vec(2,0,0),Vec(0,0,2))),"consistent wrap initial velocity");
            Capture(w,*h,b,wrap[0],0);
            wrapTicks=0;
            for(unsigned t=1;t<=240&&!failed;++t) {
                wrapEvent=true;eventTick=t;
                const auto pre=w.ReadBody(b);
                Check(bool(pre),"wrap pre-step body readback");
                wrap[t].preStepActive=pre && !pre->sleeping;
                g_Phase28ObserverWrapperNs=0;g_Phase28ObserverWrapperCalls=0;
                ResetJoltProfile();
                completeUpdateNs=0;
                system.Update(::GEngine::Timestep(1.0/60.0));
                Check(completeUpdateNs>0,"complete wrap PhysicsSystem Update observation");
                wrapTicks=t;auto& s=wrap[t];
                s.inclusiveNs=completeUpdateNs;
                s.jointNs=system.GetJointSolveNanoseconds();s.contactNs=system.GetContactSolveNanoseconds();
                s.cppCalls=::GEngine::JoltAudit::cppCalls;s.cppBytes=::GEngine::JoltAudit::cppBytes;
                s.jphCalls=::GEngine::JoltAudit::joltCalls;s.jphBytes=::GEngine::JoltAudit::joltBytes;
                CaptureJoltProfile(s);
                s.scopedCppCalls=scopeCalls;s.scopedCppBytes=scopeBytes;s.boundaryObserverNs=boundaryObserverNs;
                const auto observerStartNs=::GEngine::JoltAudit::Now();
                Capture(w,*h,b,s,0);
                if(backend==PhysicsBackend::Native)s.nativeContactPairs=NativeContactPairs(system);
                s.captureNs=::GEngine::JoltAudit::Now()-observerStartNs;
                s.wrapperNs=g_Phase28ObserverWrapperNs.load();s.wrapperCalls=g_Phase28ObserverWrapperCalls.load();
                s.observerNs=s.captureNs;s.minimalNs=s.inclusiveNs;
                s.fullNs=s.minimalNs+s.captureNs;
                s.observerUpperNs=s.fullNs+s.boundaryObserverNs;
                s.observerRefinedNs=s.captureNs+s.boundaryObserverNs+s.wrapperNs+s.profileHookNs+
                    2*clockQuantumNs*(s.wrapperCalls+s.profileHookCalls);
                const auto parity=w.ReadBody(b);Check(parity&&parity->position==s.bar.position,
                    "wrap same-step physical parity outside timing scopes");
                Check(!system.HasJointFailure()&&!system.GetBackendFailure(),"wrap finite solver state");
                Check(system.GetCompletedSteps()==t,"wrap one fixed step per sample");
            }
            Check(bool(w.DestroyJoint(*h))&&!w.ReadJoint(*h),"wrap joint retirement");
            system.OnExit();
            return !failed;
        }
        enum class EditorOp { Click, Open, Type, Expect, Image, RoomImage, UiImage, Keyboard, Viewport, PickMesh, DragMesh, Negative, InjectFailure, End, Choose, Inspect, Pointer, Wheel, Resize, Dpi, NativeImage, Wait, Stimulus, Performance, PilotLayout };
        static constexpr auto editorOpNames=std::to_array<std::string_view>({"Click","Open","Type","Expect","Image","RoomImage","UiImage","Keyboard","Viewport","PickMesh","DragMesh","Negative","InjectFailure","End","Choose","Inspect","Pointer","Wheel","Resize","Dpi","NativeImage","Wait","Stimulus","Performance","PilotLayout"});
        struct EditorAction { EditorOp op; const char* key; const char* value{}; };
        struct WidgetRect { ImVec2 lo{},hi{};ImGuiID id{};unsigned frame{};bool visible{},open{},disabled{}; };
        static inline std::vector<EditorAction> editorActions;
        static inline std::array<WidgetRect,512> editorWidgets{};
        static inline std::deque<std::string> componentScriptStrings;
        static inline unsigned editorFrame{},editorAction{},editorPhase{},editorAge{},editorTicks{},editorImages{};
        static inline unsigned editorRoomImages{};
        static inline unsigned editorLastStep{};
        static inline std::uint64_t editorGeneration{},editorResetBefore{};
        static inline bool editorMode{},editorStarted{},editorClicked{},editorInvalidSeen{};
        static inline const char* editorSession{};
        static inline const char* editorFocusRequest{};
        static inline std::ofstream editorEvents;
        struct EditorResetSnapshot {
            std::optional<AngularPacket> packet;
            std::uint64_t generation{},revision{};
            Body bar{};
            Joint joint{};
            ::GEngine::PhysicsBackend backend{::GEngine::DefaultPhysicsBackend};
        };
        static inline EditorResetSnapshot editorReset{};
        struct EditorFailureSnapshot {
            ::GEngine::_Scene* scene{};
            World* world{};
            std::optional<AngularPacket> packet;
            std::uint64_t generation{},revision{};
            Body anchor{},bar{};
            Joint joint{};
        };
        static inline EditorFailureSnapshot editorFailure{};
        static inline ::GEngine::EntityRenderId generalOldSelection{};
        static inline Vec meshBeforeDrag{},meshAfterDrag{};
        static inline ImVec2 dragStart{};
        static inline float initialMeshRoughness{};
        static bool GeneralRole(std::string_view role)
        {return role=="general-default-jolt" || role=="general-native" || ComponentRole(role);}
        static bool ComponentRole(std::string_view role)
        {return role=="component-angular-jolt" || role=="component-general-jolt" || role=="component-view-jolt" || role=="component-native";}

        static void LoadComponentScript() {
            const char* path=std::getenv("GENGINE_PHASE28_EDITOR_SCRIPT");
            Check(path && std::filesystem::path(path).is_absolute(),"explicit frozen component script input");
            if(failed)return;
            std::ifstream source(path);Check(source.good(),"read frozen component action packet");
            std::string line;
            while(std::getline(source,line) && !failed) {
                if(!line.empty() && line.back()=='\r')line.pop_back();
                const auto a=line.find('\t'),b=a==std::string::npos?a:line.find('\t',a+1);
                Check(a!=std::string::npos && b!=std::string::npos && line.find('\t',b+1)==std::string::npos,"exact three-field component action");
                if(failed)break;
                const auto op=std::find(editorOpNames.begin(),editorOpNames.end(),line.substr(0,a));
                Check(op!=editorOpNames.end() && editorActions.size()<1800,"bounded admitted component action");
                if(failed)break;
                const auto keep=[](std::string value)->const char* {
                    if(value.empty())return nullptr;
                    componentScriptStrings.push_back(std::move(value));return componentScriptStrings.back().c_str();
                };
                editorActions.push_back({EditorOp(op-editorOpNames.begin()),keep(line.substr(a+1,b-a-1)),keep(line.substr(b+1))});
            }
            Check(!editorActions.empty() && editorActions.back().op==EditorOp::End,"component script explicit end");
            std::ofstream actual(output/"script.json");actual<<"{\"actions\":[";
            for(std::size_t i=0;i<editorActions.size();++i) {
                const auto& action=editorActions[i];if(i)actual<<',';
                actual<<"{\"op\":"<<std::quoted(std::string(editorOpNames[unsigned(action.op)]))<<",\"key\":";
                if(action.key)actual<<std::quoted(action.key);else actual<<"null";
                actual<<",\"value\":";if(action.value)actual<<std::quoted(action.value);else actual<<"null";actual<<'}';
            }
            actual<<"]}\n";Check(actual.good(),"actual component script export");
        }

        static const char* BackendKey(::GEngine::PhysicsBackend backend) noexcept
        {return backend==::GEngine::PhysicsBackend::Jolt?"jolt":"native";}
        static const char* SavedBackendKey() noexcept
        {return Rbs::Editor::SavedBackend?BackendKey(*Rbs::Editor::SavedBackend):"none";}

        static int WidgetIndex(std::string_view key) {
            constexpr auto keys=std::to_array<std::string_view>({
                "pilot-overlay-commit","pilot-overlay-cancel",
                "preset.hinge-basic","preset.hinge-quaternion","preset.hinge-limited","preset.cv-free","preset.cv-limited",
                "content-tab","advanced","authored-section","book-world-section","angular-damping-readonly",
                "world-physics-section","mass","scale","gravity","limit-lower","limit-upper","limit-u","limit-v",
                "apply","revert","restore-defaults","reset","scene-combo","variant","scene-load","backend","backend.native",
                "backend.jolt","backend-apply","impulse","mode-toggle",
                "hierarchy.anchor","hierarchy.bar","hierarchy.floor","hierarchy.wall",
                "preset.geometry-gallery","preset.box-stack","preset.sphere-box-stack","preset.sphere-lattice",
                "preset.distance-chain","preset.ball-socket-p","hierarchy.reference-sphere","reference-position-x",
                "position-x","rotation-y","size-x","friction","restitution","linear-damping","angular-damping",
                "joint-frame","motion","motion.kinematic","shape","shape.sphere","radius","scale-x","size-z",
                "hierarchy.cameras-group","hierarchy.lights-group","hierarchy.entities-group","hierarchy.environment-group",
                "hierarchy.wall-px","hierarchy.wall-nz","hierarchy.wall-pz","hierarchy.directional","hierarchy.point",
                "hierarchy.camera","hierarchy.camera-b","hierarchy.mesh","hierarchy.sky","sky-visible",
                "camera-fov","camera-apply","camera-revert","camera-preview","camera-edit-transform","camera-match-editor","light-intensity","light-apply","light-revert",
                "material-roughness","material-apply","material-revert","general-reset","generic-position-y","generic-rotation-y",
                "generic-scale-x","generic-rigidbody","generic-friction","generic-body-apply",
                "camera-active","camera-far","camera-layers","camera-near","camera-ortho-size","camera-projection","collider-apply","collider-center-x","collider-center-y","collider-center-z","collider-fit","collider-half-length","collider-half-x","collider-half-y","collider-half-z","collider-kind","collider-radius","collider-revert","generic-allow-sleeping","generic-angular-damping","generic-angular-velocity-x","generic-body-revert","generic-ccd","generic-collider","generic-gravity-factor","generic-linear-damping","generic-linear-velocity-x","generic-mass","generic-motion","generic-position-steps","generic-position-x","generic-position-z","generic-resistance","generic-restitution","generic-rolling-readback","generic-rotation-x","generic-rotation-z","generic-sleep","generic-sleep-angular","generic-sleep-dwell","generic-sleep-linear","generic-solver","generic-spin-readback","generic-velocity-steps","hierarchy.axis","hierarchy.axis-eye","hierarchy.grid","hierarchy.point-eye","hierarchy.rename","hierarchy.selected","light-cast-shadows","light-color-b","light-color-g","light-color-r","light-inner","light-kind","light-outer","light-range","light-shadow-bias","light-shadow-filter","light-shadow-resolution","material-ao","material-color-b","material-color-g","material-color-r","material-metallic","material-normal","material-uv-x","material-uv-y","mesh-apply","mesh-cast-shadows","mesh-pickable","mesh-receive-shadows","mesh-section","visibility-apply","visibility-enabled","visibility-layers","visibility-revert",
                "collider-cancel","collider-confirm","collider-current","collider-force","collider-kind.box","collider-kind.capsule","collider-kind.sphere","generic-body-baseline","mesh-identity",
                "outline.select-tool","angular-shape-readonly","angular-motion-readonly","angular-mass-readonly","angular-kind-readonly","angular-free-limits-readonly",
                "effective-material","effective-mass","effective-damping","effective-inverse-mass","effective-linear-velocity","effective-angular-velocity","effective-position","effective-orientation","effective-com","effective-body-inertia","effective-world-inertia","effective-anchor-residual"});
            static_assert(keys.size()<=editorWidgets.size());
            for(unsigned i=0;i<keys.size();++i)if(keys[i]==key)return int(i);
            return -1;
        }
        static void EditorLog(const char* event,const char* key,ImGuiID id=0,ImVec2 lo={},ImVec2 hi={}) {
            if(editorEvents)editorEvents<<editorFrame<<','<<editorAction<<','<<event<<','<<(key?key:"")<<','<<id<<','
                <<lo.x<<','<<lo.y<<','<<hi.x<<','<<hi.y<<','<<editorTicks<<'\n'<<std::flush;
        }
        static inline unsigned pilotLayoutCases{},pilotOverlayFrames{};
        static void PilotLayoutUi(RigidBodySimulationApp& app) {
            const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
            Check(lo.x>=app.m_ViewportBounds[0].x && lo.y>=app.m_ViewportBounds[0].y &&
                  hi.x<=app.m_ViewportBounds[1].x && hi.y<=app.m_ViewportBounds[1].y,"Pilot overlay group contained by actual Viewport");
            ++pilotOverlayFrames;
            if(editorAction>=editorActions.size() || editorActions[editorAction].op!=EditorOp::PilotLayout ||
               std::string_view(editorActions[editorAction].key)!="matrix" || pilotLayoutCases>=32)return;
            namespace E=Rbs::Editor;
            const unsigned index=pilotLayoutCases,scale=index/8,zoom=(index/4)%2,widthCase=index%4;
            const float savedScale=E::Scale;const auto savedStyle=ImGui::GetStyle();E::Scale=E::Scales[scale];
            ImGui::GetStyle().ScaleAllSizes(E::Scale/savedScale);
            ImGui::PushFont(E::Regular[scale]);
            ImGui::SetNextWindowPos({40,80},ImGuiCond_Always);
            ImGui::SetNextWindowSize({1200,700},ImGuiCond_Always);
            ImGui::Begin("Pilot layout regression",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|
                ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::SetWindowFontScale(zoom?1.5f:1.f);
            const auto full=E::MeasurePilotControls(2000.f);
            const float width=widthCase==0?full.minimumWidth:widthCase==1?full.size.x-1.f:widthCase==2?full.size.x:full.size.x+160.f;
            const auto layout=E::MeasurePilotControls(width);
            const auto start=ImGui::GetCursorScreenPos();
            const ImVec2 p{start.x+17.f,start.y+13.f}; // Reproduce non-parent overlay indentation.
            ImGui::SetCursorScreenPos(p);app.DrawCameraPilotControls(width);
            const auto actualLo=ImGui::GetItemRectMin(),actualHi=ImGui::GetItemRectMax();
            Check(actualLo.x>=p.x && actualLo.y>=p.y && actualHi.x<=p.x+layout.size.x+.01f &&
                actualHi.y<=p.y+layout.size.y+.01f,"Pilot measured bounds contain actual ImGui group at font/width scale");
            const auto& a=editorWidgets[WidgetIndex("pilot-overlay-commit")];
            const auto& b=editorWidgets[WidgetIndex("pilot-overlay-cancel")];
            for(const auto* button:{&a,&b})Check(button->lo.x>=p.x && button->lo.y>=p.y &&
                button->hi.x<=p.x+layout.size.x+.01f && button->hi.y<=p.y+layout.size.y+.01f &&
                ImGui::GetCurrentWindow()->ClipRect.Contains(button->lo) &&
                ImGui::GetCurrentWindow()->ClipRect.Contains(ImVec2{button->hi.x-.01f,button->hi.y-.01f}),"Pilot complete button inside panel and clip rectangle");
            Check(layout.stacked?a.hi.y<b.lo.y:a.hi.x<b.lo.x,"Pilot buttons separated without overlap");
            std::ofstream out(output/"pilot-layout.csv",std::ios::app);
            if(!index)out<<"case,font_scale,font_zoom,width,height,stacked,actual_width,actual_height\n";
            out<<index<<','<<E::Scale<<','<<(zoom?1.5f:1.f)<<','<<layout.size.x<<','<<layout.size.y<<','<<layout.stacked<<','<<actualHi.x-p.x<<','<<actualHi.y-p.y<<'\n';
            ImGui::SetWindowFontScale(1.f);ImGui::End();ImGui::PopFont();E::Scale=savedScale;ImGui::GetStyle()=savedStyle;++pilotLayoutCases;
        }
        static void Widget(RigidBodySimulationApp& app,const char* key) {
            if(editorMode && key && std::string_view(key)=="pilot-overlay-bounds")PilotLayoutUi(app);
            else WidgetKey(key);
        }
        static void WidgetKey(const char* key) {
            if(!editorMode || !key)return;
            if(editorFocusRequest) {
                ImGui::SetWindowFocus(editorFocusRequest);
                editorFocusRequest=nullptr;
            }
            const auto index=WidgetIndex(key);if(index<0)return;
            auto& w=editorWidgets[unsigned(index)];
            w={ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),ImGui::GetItemID(),editorFrame,ImGui::IsItemVisible(),false,
                (ImGui::GetCurrentContext()->LastItemData.InFlags&ImGuiItemFlags_Disabled)!=0};
            // IsItemVisible accepts partial clipping; the real click target must
            // also lie inside this window's clip rectangle before input dispatch.
            const ImVec2 center{(w.lo.x+w.hi.x)*.5f,(w.lo.y+w.hi.y)*.5f};
            w.visible=w.visible && ImGui::GetCurrentWindow()->ClipRect.Contains(center);
            if(editorAction<editorActions.size()) {
                const auto& active=editorActions[editorAction];
                if(active.op==EditorOp::Open && std::string_view(key)==active.key)
                    w.open=ImGui::GetStateStorage()->GetInt(w.id,0)!=0;
                if(active.op==EditorOp::UiImage && !w.visible) {
                    const std::string_view image(active.key);
                    if((image=="editable-properties-jolt" && std::string_view(key)=="mass") ||
                       (image=="general-nonphysics" && std::string_view(key)=="material-roughness") ||
                       (image=="general-light-camera" && std::string_view(key)=="camera-fov"))ImGui::SetScrollHereY(.4f);
                }
            }
            if(!editorClicked && editorAction<editorActions.size() && editorPhase>=2 &&
               editorActions[editorAction].op==EditorOp::Click && editorActions[editorAction].key &&
               ((std::string_view(editorActions[editorAction].key)=="backend" &&
                 (std::string_view(key)=="backend.native" || std::string_view(key)=="backend.jolt")) ||
                (std::string_view(editorActions[editorAction].key)=="shape" && std::string_view(key)=="shape.sphere") ||
                (std::string_view(editorActions[editorAction].key)=="motion" && std::string_view(key)=="motion.kinematic")) &&
               w.visible) {
                editorClicked=true;EditorLog("combo-options-visible",editorActions[editorAction].key,w.id,w.lo,w.hi);
            }
            if(editorAction<editorActions.size() && editorActions[editorAction].key &&
               std::string_view(key)==editorActions[editorAction].key) {
                if(std::string_view(key)=="mass" && Rbs::Editor::Numeric::state.error)
                    editorInvalidSeen=true;
                if(!w.visible)ImGui::SetScrollHereY(.5f);
                if(ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    editorClicked=true;EditorLog("item-clicked",key,w.id,w.lo,w.hi);
                }
            }
        }
        static void NextEditorAction() {
            ++editorAction;editorPhase=0;editorAge=0;editorClicked=false;
        }
        static void EditorScript(std::string_view role) {
            if(ComponentRole(role)){LoadComponentScript();return;}
            auto add=[](EditorOp op,const char* key,const char* value=nullptr){editorActions.push_back({op,key,value});};
            const auto load=[&](const char* card,const char* scene) {
                add(EditorOp::Click,card);add(EditorOp::Click,"scene-load");
                add(EditorOp::Expect,scene);
            };
            if(role=="properties-jolt" || role=="properties-default-jolt" || role=="properties-native" || role=="direct-default-jolt" || GeneralRole(role)) {
                const bool jolt=role!="properties-native" && role!="general-native";
                add(EditorOp::Click,"content-tab");
                add(EditorOp::Expect,"thumbnails-loaded");
                load("preset.sphere-box-stack","sphere-box-stack");
                add(EditorOp::Click,"hierarchy.reference-sphere");
                add(EditorOp::Type,"reference-position-x","4");
                add(EditorOp::Expect,"sphere-reference-applied");
                struct SceneCase {const char* card;const char* name;RbsScenePreset preset;};
                constexpr SceneCase cases[]{
                    {"preset.hinge-basic","hinge-basic",RbsScenePreset::HingeBasic},
                    {"preset.hinge-quaternion","hinge-quaternion",RbsScenePreset::HingeQuaternion},
                    {"preset.hinge-limited","hinge-limited",RbsScenePreset::HingeLimited},
                    {"preset.cv-free","cv-free",RbsScenePreset::CvFree},
                    {"preset.cv-limited","cv-limited",RbsScenePreset::CvLimited}};
                for(const auto& c:cases) {
                    if(!jolt && c.preset!=RbsScenePreset::HingeLimited)continue;
                    load(c.card,c.name);
                    add(EditorOp::Click,"hierarchy.bar");add(EditorOp::Expect,"direct-editable");
                    add(EditorOp::Expect,"selected-bar");
                    add(EditorOp::Type,"position-x","1");add(EditorOp::Type,"rotation-y","10");
                    add(EditorOp::Type,"scale-x","0.34");add(EditorOp::Expect,"scale-staged");
                    add(EditorOp::Type,"size-x","0.18");
                    add(EditorOp::Type,"mass","1.25");add(EditorOp::Type,"friction","0.35");
                    add(EditorOp::Type,"restitution","0.2");
                    if(jolt)add(EditorOp::Type,"linear-damping","0.1");
                    add(EditorOp::Type,"joint-frame","15");
                    if(c.preset==RbsScenePreset::HingeLimited)add(EditorOp::Type,"limit-upper","30");
                    if(c.preset==RbsScenePreset::CvLimited)add(EditorOp::Type,"limit-u","30");
                    add(EditorOp::Click,"hierarchy.anchor");add(EditorOp::Expect,"selected-anchor");
                    add(EditorOp::Click,"shape");add(EditorOp::Click,"shape.sphere");
                    add(EditorOp::Type,"radius","0.28");
                    add(EditorOp::Click,"motion");add(EditorOp::Click,"motion.kinematic");
                    add(EditorOp::Click,"hierarchy.bar");add(EditorOp::Expect,"selected-bar");
                    add(EditorOp::Expect,"draft-isolated");add(EditorOp::Expect,"dirty-switch-protected");
                    add(EditorOp::Click,"apply");add(EditorOp::Expect,"body-properties-applied");
                    add(EditorOp::Click,"advanced");add(EditorOp::Click,"advanced");
                    if(jolt && c.preset==RbsScenePreset::HingeLimited)add(EditorOp::UiImage,"editable-properties-jolt");
                    add(EditorOp::Type,"mass","1.5");
                    if(c.preset==RbsScenePreset::HingeLimited) {
                        add(EditorOp::Negative,nullptr);add(EditorOp::Type,"mass","2.5");
                        add(EditorOp::Expect,"invalid-mass");
                    }
                    add(EditorOp::Click,"revert");add(EditorOp::Expect,"body-properties-applied");
                    if(c.preset==RbsScenePreset::HingeLimited) {
                        add(EditorOp::Click,"mode-toggle");add(EditorOp::Expect,"simulation-mode");
                        add(EditorOp::Viewport,nullptr);add(EditorOp::Keyboard,"S");add(EditorOp::Expect,"running");
                        add(EditorOp::Click,"impulse");add(EditorOp::Expect,"impulse");
                        add(EditorOp::Viewport,nullptr);add(EditorOp::Keyboard,"P");add(EditorOp::Expect,"paused");
                    }
                    add(EditorOp::Click,"reset");add(EditorOp::Expect,"reset-retained");
                    add(EditorOp::Click,"reset");add(EditorOp::Expect,"reset-retained");
                    if(c.preset==RbsScenePreset::HingeLimited) {
                        add(EditorOp::Expect,"edit-mode");
                        add(EditorOp::Click,"hierarchy.bar");
                    }
                    add(EditorOp::Expect,"body-properties-applied");
                    if(jolt && c.preset!=RbsScenePreset::CvLimited) {
                        add(EditorOp::Click,"restore-defaults");add(EditorOp::Expect,"defaults-staged");
                        add(EditorOp::Click,"apply");add(EditorOp::Expect,"defaults");
                        add(EditorOp::Click,"reset");add(EditorOp::Expect,"reset");
                    }
                }
                if(jolt) {
                    add(EditorOp::Type,"linear-damping","0");
                    add(EditorOp::Click,"apply");add(EditorOp::Expect,"portable-properties");
                }
                add(EditorOp::Click,"backend");
                add(EditorOp::Click,jolt?"backend.native":"backend.jolt");
                add(EditorOp::Click,"backend-apply");add(EditorOp::Expect,jolt?"native":"jolt");
                add(EditorOp::Expect,"portable-properties");
                add(EditorOp::Click,"backend");
                add(EditorOp::Click,jolt?"backend.jolt":"backend.native");
                add(EditorOp::Click,"backend-apply");add(EditorOp::Expect,jolt?"jolt":"native");
                add(EditorOp::Expect,"portable-properties");
                add(EditorOp::Click,"restore-defaults");add(EditorOp::Expect,"defaults-staged");
                add(EditorOp::Click,"apply");add(EditorOp::Expect,"defaults");
                add(EditorOp::Click,"reset");add(EditorOp::Expect,"reset");
                if(GeneralRole(role)) {
                    add(EditorOp::Open,"hierarchy.cameras-group");add(EditorOp::Click,"hierarchy.camera");
                    add(EditorOp::Type,"camera-fov","60");add(EditorOp::Click,"camera-apply");
                    add(EditorOp::Type,"camera-fov","70");add(EditorOp::Click,"camera-revert");add(EditorOp::Expect,"general-camera");
                    add(EditorOp::Click,"camera-preview");add(EditorOp::Expect,"general-camera-preview");
                    add(EditorOp::UiImage,"general-light-camera");add(EditorOp::Click,"camera-preview");
                    add(EditorOp::Open,"hierarchy.lights-group");add(EditorOp::Click,"hierarchy.directional");
                    add(EditorOp::Type,"light-intensity","2.5");add(EditorOp::Click,"light-apply");
                    add(EditorOp::Type,"light-intensity","1.5");add(EditorOp::Click,"light-revert");add(EditorOp::Expect,"general-directional");
                    add(EditorOp::Click,"hierarchy.point");add(EditorOp::Type,"light-intensity","1.75");
                    add(EditorOp::Click,"light-apply");add(EditorOp::Expect,"general-point");
                    constexpr const char* roomRows[]{"hierarchy.floor","hierarchy.wall","hierarchy.wall-px","hierarchy.wall-nz","hierarchy.wall-pz"};
                    constexpr const char* heights[]{"-0.1","6.6","6.7","6.8","6.9"};
                    for(unsigned i=0;i<5;++i) {
                        add(EditorOp::Click,roomRows[i]);add(EditorOp::Type,"generic-position-y",heights[i]);
                        if(!i) {
                            add(EditorOp::Open,"generic-rigidbody");add(EditorOp::Type,"generic-friction","0.42");
                            add(EditorOp::Click,"generic-body-apply");
                        }
                    }
                    add(EditorOp::Expect,"general-room");add(EditorOp::Click,"general-reset");
                    add(EditorOp::Expect,"general-room-reset");
                    add(EditorOp::Click,"backend");add(EditorOp::Click,jolt?"backend.native":"backend.jolt");
                    add(EditorOp::Click,"backend-apply");add(EditorOp::Expect,jolt?"native":"jolt");add(EditorOp::Expect,"general-room");
                    add(EditorOp::Click,"backend");add(EditorOp::Click,jolt?"backend.jolt":"backend.native");
                    add(EditorOp::Click,"backend-apply");add(EditorOp::Expect,jolt?"jolt":"native");add(EditorOp::Expect,"general-room");
                    add(EditorOp::Click,"restore-defaults");add(EditorOp::Expect,"general-room");add(EditorOp::Click,"apply");add(EditorOp::Expect,"general-room-defaults");
                    load("preset.geometry-gallery","geometry-gallery");
                    add(EditorOp::Open,"hierarchy.entities-group");add(EditorOp::Click,"hierarchy.mesh");add(EditorOp::Expect,"general-mesh-initial");
                    add(EditorOp::Type,"generic-position-y","2");add(EditorOp::Type,"generic-scale-x","1.25");
                    add(EditorOp::Type,"material-roughness","0.37");add(EditorOp::Click,"material-apply");
                    add(EditorOp::Type,"material-roughness","0.62");add(EditorOp::Click,"material-revert");
                    add(EditorOp::Expect,"general-mesh");add(EditorOp::UiImage,"general-nonphysics");
                    add(EditorOp::Open,"hierarchy.cameras-group");add(EditorOp::Click,"hierarchy.camera");
                    add(EditorOp::PickMesh,nullptr);add(EditorOp::Keyboard,"T");add(EditorOp::Expect,"general-translate");
                    add(EditorOp::DragMesh,nullptr);add(EditorOp::Click,"general-reset");add(EditorOp::Expect,"general-mesh-reset");
                    add(EditorOp::Click,"restore-defaults");add(EditorOp::Click,"apply");add(EditorOp::Expect,"general-mesh-defaults");
                }
            } else if(role=="saved-native") {
                add(EditorOp::Click,"content-tab");add(EditorOp::Expect,"thumbnails-loaded");
                add(EditorOp::UiImage,"editor-native-with-cards");
                add(EditorOp::Click,"preset.hinge-basic");add(EditorOp::Click,"scene-load");
                add(EditorOp::Expect,"hinge-basic");
                add(EditorOp::Image,"hinge-basic");
                for(const auto& pair:std::array<std::pair<const char*,const char*>,4>{{
                    {"preset.hinge-quaternion","hinge-quaternion"},{"preset.hinge-limited","hinge-limited"},
                    {"preset.cv-free","cv-free"},{"preset.cv-limited","cv-limited"}}}) {
                    add(EditorOp::Click,pair.first);add(EditorOp::Click,"scene-load");
                    add(EditorOp::Expect,pair.second);add(EditorOp::Image,pair.second);
                }
                add(EditorOp::Expect,"direct-editable");
                add(EditorOp::Click,"hierarchy.bar");add(EditorOp::Expect,"selected-bar");
                add(EditorOp::Type,"mass","1.25");add(EditorOp::Type,"size-x","0.135");
                add(EditorOp::Type,"scale","0.9");add(EditorOp::Type,"size-z","0.135");
                add(EditorOp::Type,"gravity","-8");add(EditorOp::Type,"limit-u","30");
                add(EditorOp::Type,"limit-v","25");add(EditorOp::Click,"apply");
                add(EditorOp::Expect,"applied-cv");
                add(EditorOp::Click,"hierarchy.bar");add(EditorOp::Expect,"selected-bar");
                add(EditorOp::Click,"hierarchy.floor");add(EditorOp::Expect,"selected-floor");
                add(EditorOp::Click,"hierarchy.wall");add(EditorOp::Expect,"selected-wall");
                add(EditorOp::Click,"hierarchy.anchor");add(EditorOp::Expect,"selected-anchor");
                add(EditorOp::Click,"hierarchy.bar");add(EditorOp::Expect,"selected-bar");
                add(EditorOp::Type,"mass","1.5");add(EditorOp::Negative,nullptr);
                add(EditorOp::Type,"mass","2.5");
                add(EditorOp::Expect,"invalid-mass");add(EditorOp::Click,"revert");add(EditorOp::Expect,"reverted");
                add(EditorOp::Click,"restore-defaults");add(EditorOp::Click,"apply");
                add(EditorOp::Expect,"defaults");add(EditorOp::Click,"reset");add(EditorOp::Expect,"reset");
                add(EditorOp::Click,"mode-toggle");add(EditorOp::Expect,"simulation-mode");
                add(EditorOp::Viewport,nullptr);
                add(EditorOp::Keyboard,"S");add(EditorOp::Expect,"running");
                add(EditorOp::Click,"impulse");add(EditorOp::Expect,"impulse");
                add(EditorOp::Viewport,nullptr);
                add(EditorOp::Keyboard,"P");add(EditorOp::Expect,"paused");
                add(EditorOp::Keyboard,"R");add(EditorOp::Expect,"resumed");
                add(EditorOp::Keyboard,"P");add(EditorOp::Expect,"paused");
                add(EditorOp::Click,"backend");add(EditorOp::Click,"backend.jolt");
                add(EditorOp::Click,"backend-apply");add(EditorOp::Expect,"jolt");
            } else if(role=="fresh-jolt") {
                add(EditorOp::Click,"content-tab");
                struct Case {const char* card;const char* scene;bool hingeLimited;bool cvLimited;};
                constexpr std::array<Case,5> cases{{
                    {"preset.hinge-basic","hinge-basic",false,false},
                    {"preset.hinge-quaternion","hinge-quaternion",false,false},
                    {"preset.hinge-limited","hinge-limited",true,false},
                    {"preset.cv-free","cv-free",false,false},
                    {"preset.cv-limited","cv-limited",false,true}}};
                for(const auto& c:cases) {
                    load(c.card,c.scene);add(EditorOp::Image,c.scene);
                    add(EditorOp::Click,"hierarchy.bar");add(EditorOp::Expect,"selected-bar");
                    add(EditorOp::Type,"mass","1.25");
                    if(c.hingeLimited || c.cvLimited) {
                        add(EditorOp::Type,"size-x","0.135");add(EditorOp::Type,"scale","0.9");
                        add(EditorOp::Type,"size-z","0.135");add(EditorOp::Type,"gravity","-8");
                        if(c.hingeLimited) {
                            add(EditorOp::Type,"limit-lower","-30");
                            add(EditorOp::Type,"limit-upper","30");
                        } else {
                            add(EditorOp::Type,"limit-u","30");
                            add(EditorOp::Type,"limit-v","25");
                        }
                    }
                    add(EditorOp::Click,"apply");
                    add(EditorOp::Expect,c.hingeLimited?"applied-hinge":c.cvLimited?"applied-cv":"applied-current");
                    add(EditorOp::Click,"mode-toggle");add(EditorOp::Expect,"simulation-mode");
                    add(EditorOp::Viewport,nullptr);
                    add(EditorOp::Keyboard,"S");add(EditorOp::Expect,"running");
                    add(EditorOp::Click,"impulse");add(EditorOp::Expect,"impulse");
                    if(std::string_view(c.scene)=="hinge-basic") {
                        add(EditorOp::Click,"reset");add(EditorOp::Expect,"reset-retained");
                        add(EditorOp::Expect,"edit-mode");add(EditorOp::Click,"mode-toggle");add(EditorOp::Expect,"simulation-mode");
                        add(EditorOp::Viewport,nullptr);
                        add(EditorOp::Keyboard,"S");add(EditorOp::Expect,"running");
                    }
                    add(EditorOp::Viewport,nullptr);
                    add(EditorOp::Keyboard,"P");add(EditorOp::Expect,"paused");
                    if(c.hingeLimited) {
                        add(EditorOp::Keyboard,"R");add(EditorOp::Expect,"resumed");
                        add(EditorOp::Keyboard,"P");add(EditorOp::Expect,"paused");
                    }
                    add(EditorOp::Click,"reset");add(EditorOp::Expect,"reset-retained");
                    add(EditorOp::Click,"reset");add(EditorOp::Expect,"reset-retained");
                    add(EditorOp::Expect,"edit-mode");
                    if(c.hingeLimited) {
                        add(EditorOp::Click,"hierarchy.bar");add(EditorOp::Expect,"selected-bar");
                        add(EditorOp::Type,"mass","1.5");add(EditorOp::Negative,nullptr);
                        add(EditorOp::Type,"mass","2.5");add(EditorOp::Expect,"invalid-mass");
                        add(EditorOp::Click,"revert");add(EditorOp::Expect,"reverted");
                        add(EditorOp::Click,"restore-defaults");add(EditorOp::Click,"apply");
                        add(EditorOp::Expect,"defaults");add(EditorOp::Click,"reset");add(EditorOp::Expect,"reset");
                    }
                }
                add(EditorOp::Click,"backend");add(EditorOp::Click,"backend.native");
                add(EditorOp::Click,"backend-apply");add(EditorOp::Expect,"native");
            } else {
                add(EditorOp::Click,"content-tab");add(EditorOp::Expect,"thumbnails-loaded");
                add(EditorOp::UiImage,"editor-native-override-with-cards");
                struct Legacy {const char* card;const char* scene;};
                constexpr std::array<Legacy,6> legacy{{
                    {"preset.geometry-gallery","geometry-gallery"},
                    {"preset.box-stack","box-stack"},
                    {"preset.sphere-box-stack","sphere-box-stack"},
                    {"preset.sphere-lattice","sphere-lattice"},
                    {"preset.distance-chain","distance-chain"},
                    {"preset.ball-socket-p","ball-socket-p"}}};
                for(const auto& c:legacy) {
                    load(c.card,c.scene);add(EditorOp::RoomImage,c.scene);
                }
                load("preset.hinge-limited","hinge-limited");
                add(EditorOp::Click,"backend");add(EditorOp::Click,"backend.jolt");
                add(EditorOp::Click,"backend-apply");add(EditorOp::Expect,"jolt");
                add(EditorOp::UiImage,"editor-jolt-with-cards");
                for(const auto& c:legacy) {
                    load(c.card,c.scene);add(EditorOp::RoomImage,c.scene);
                }
                load("preset.hinge-limited","hinge-limited");
                add(EditorOp::Click,"reset");add(EditorOp::Expect,"reset-reference");
                add(EditorOp::Click,"hierarchy.bar");add(EditorOp::Expect,"selected-bar");
                add(EditorOp::Click,"hierarchy.floor");add(EditorOp::Expect,"selected-floor");
                add(EditorOp::Click,"hierarchy.wall");add(EditorOp::Expect,"selected-wall");
                add(EditorOp::Click,"hierarchy.anchor");add(EditorOp::Expect,"selected-anchor");
                add(EditorOp::InjectFailure,nullptr);
                add(EditorOp::Click,"backend");add(EditorOp::Click,"backend.native");
                add(EditorOp::Click,"backend-apply");add(EditorOp::Expect,"failed-switch");
            }
            add(EditorOp::End,nullptr);
        }
        static const char* EditorWindow(std::string_view key) {
            if(key=="outline.select-tool" || key.starts_with("pilot-overlay-"))return "Viewport";
            if(key.starts_with("preset.") || key=="content-tab")return "Content Browser / Diagnostics";
            if(key.starts_with("hierarchy."))return "Hierarchy";
            if(key=="gravity" || key=="book-world-section" || key=="world-physics-section" ||
               key=="backend" || key.starts_with("backend.") || key=="backend-apply" || key=="impulse")return "World Settings";
            if(key=="scene-load" || key=="scene-combo" || key=="variant" || key=="mode-toggle" || key=="general-reset")return nullptr;
            return "Properties";
        }
        static void EditorMovePointer(RigidBodySimulationApp& app,float x,float y) {
            auto& io=ImGui::GetIO();
            if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
                Check(SDL_WarpMouseGlobal(int(std::lround(x)),int(std::lround(y)))==0,
                      "global SDL cursor warp for ImGui viewport");
            else if(auto* window=SDL_GetWindowFromID(app.GetWindow()->GetWindowID()))
                SDL_WarpMouseInWindow(window,int(std::lround(x)),int(std::lround(y)));
            else Check(false,"ordinary Editor SDL window");
            io.AddMousePosEvent(x,y);
        }
        static void EditorPointerEvent(RigidBodySimulationApp& app,ImVec2 p,Uint32 type) {
            EditorMovePointer(app,p.x,p.y);
            const auto id=app.GetWindow()->GetWindowID();
            auto* window=SDL_GetWindowFromID(id);
            if(!window){Check(false,"ordinary Editor pointer window");return;}
            int ox{},oy{};
            if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)SDL_GetWindowPosition(window,&ox,&oy);
            const int x=int(std::lround(p.x))-ox,y=int(std::lround(p.y))-oy;
            SDL_Event e{};e.type=type;
            if(type==SDL_MOUSEMOTION) {
                e.motion.windowID=id;e.motion.x=x;e.motion.y=y;e.motion.state=SDL_BUTTON_LMASK;
            } else {
                e.button.windowID=id;e.button.button=SDL_BUTTON_LEFT;e.button.clicks=1;
                e.button.state=type==SDL_MOUSEBUTTONDOWN?SDL_PRESSED:SDL_RELEASED;e.button.x=x;e.button.y=y;
            }
            Check(SDL_PushEvent(&e)==1,"enqueue ordinary viewport pointer input");
        }
        // Bounded outline validation uses the ordinary SDL route and actual render
        // target. It never supplies selected/hover identities to the renderer.
        static inline bool outlineActive{};
        static inline float outlineDpi=1.f;
        static inline unsigned outlineObservedFrame{};
        static inline std::uint64_t outlineStationaryRequests{},outlineStationaryReads{};
        static inline std::size_t outlineIdleAllocations{};
        static inline ::GEngine::EntityRenderId outlinePointerSelection{};
        static inline ::GEngine::FrameSubmissionStats outlineStats{};
        static inline std::chrono::steady_clock::time_point outlineSubmitBegin{};
        static inline double outlineSubmitUs{};
        static inline glm::vec2 outlinePixelRatio{1.f};
        static inline std::array<int,4> outlineWindow{};
        static inline ::GEngine::_Entity outlineProbeEntity{},outlineOccluder{};
        static inline ::GEngine::MaterialHandle outlineOpaque{},outlineMasked{},outlineTransparent{};
        // D28-POINTER-PERFORMANCE-01: opt-in measurement only. Reuse the existing
        // nonblocking pass timers and inclusive Physics observer; buffer all rows
        // before writing. Nothing here is an ordinary Editor control or a gate waiver.
        struct PerformanceFrame {
            unsigned frame{},action{},tick{},physicsCalls{}; char window{'-'};
            double time{},interval{}; std::uint64_t physicsNs{},requests{},reads{};
            unsigned selected{},hover{},passes{},draws{},allocations{},width{},height{};
            glm::vec3 camera{}; glm::vec3 body{};
            unsigned panelCount{},visiblePanels{},readyPanels{},panelTargets{},panelWidth{},panelHeight{},platformViews{};
            std::uint64_t panelFrames{},panelReallocations{},mainReallocations{};
            int swap{};unsigned cap{};
        };
        struct PerformancePass {unsigned frame{}; bool collected{}; ::GEngine::PassTimingSample sample;};
        static inline bool performanceMode{};
        static inline char performanceWindow='-';
        static inline std::vector<PerformanceFrame> performanceFrames;
        static inline std::vector<PerformancePass> performancePasses;
        static inline double performancePrevious{};
        static inline std::uint64_t performancePhysicsNs{};
        static inline unsigned performancePhysicsCalls{};
        static inline ::GEngine::Phase26Observation::Observer performanceObserver{};
        static inline ::GEngine::Phase26Observation::Observer* performancePreviousObserver{};
        static void PerformanceBegin(void*) noexcept {
            if(performancePreviousObserver && performancePreviousObserver->begin)
                performancePreviousObserver->begin(performancePreviousObserver->context);
        }
        static void PerformanceEnd(void*,std::uint64_t ns) noexcept {
            performancePhysicsNs+=ns;++performancePhysicsCalls;
            if(performancePreviousObserver && performancePreviousObserver->end)
                performancePreviousObserver->end(performancePreviousObserver->context,ns);
        }
        static void PerformanceAllocation(void*,unsigned scope,bool begin) noexcept {
            if(performancePreviousObserver && performancePreviousObserver->allocation)
                performancePreviousObserver->allocation(performancePreviousObserver->context,scope,begin);
        }
        static void PerformanceFlush() {
            if(!performanceMode)return;
            ::GEngine::Phase26Observation::observer=performancePreviousObserver;
            std::ofstream f(output/"performance-frames.csv");
            f<<"frame,action,window,tick,time,interval_ms,physics_ns,physics_calls,requests,readbacks,selected,hover,passes,draws,allocations,width,height,camera_x,camera_y,camera_z,body_x,body_y,body_z,panels,visible_panels,ready_panels,panel_targets,panel_width,panel_height,platform_views,panel_frames,panel_reallocations,main_reallocations,swap,cap\n"<<std::setprecision(17);
            for(const auto& s:performanceFrames)f<<s.frame<<','<<s.action<<','<<s.window<<','<<s.tick<<','<<s.time<<','<<s.interval*1000<<','<<s.physicsNs<<','<<s.physicsCalls<<','<<s.requests<<','<<s.reads<<','<<s.selected<<','<<s.hover<<','<<s.passes<<','<<s.draws<<','<<s.allocations<<','<<s.width<<','<<s.height<<','<<s.camera.x<<','<<s.camera.y<<','<<s.camera.z<<','<<s.body.x<<','<<s.body.y<<','<<s.body.z<<','<<s.panelCount<<','<<s.visiblePanels<<','<<s.readyPanels<<','<<s.panelTargets<<','<<s.panelWidth<<','<<s.panelHeight<<','<<s.platformViews<<','<<s.panelFrames<<','<<s.panelReallocations<<','<<s.mainReallocations<<','<<s.swap<<','<<s.cap<<'\n';
            std::ofstream p(output/"performance-passes.csv");
            p<<"editor_frame,collected,timer_frame,collected_frame,pass,cpu_ns,gpu_ns,gpu_status,draws\n";
            for(const auto& r:performancePasses){const auto& s=r.sample;p<<r.frame<<','<<r.collected<<','<<s.frame<<','<<s.collectedFrame<<','<<::GEngine::PassLabel(s.pass)<<','<<s.cpuNanoseconds<<',';if(s.gpuNanoseconds)p<<*s.gpuNanoseconds;p<<','<<::GEngine::TimingLabel(s.gpu)<<','<<s.submittedDraws<<'\n';}
            Check(f.good()&&p.good(),"buffered performance output");
        }
        static void PerformanceObserve(RigidBodySimulationApp& app,const ::GEngine::FrameSubmissionStats& stats) {
            if(!performanceMode)return;
            Check(performanceFrames.size()<1800 && performancePasses.size()+96<=180000,"fixed performance sample capacity");if(failed)return;
            const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            PerformanceFrame s; s.frame=editorFrame;s.action=editorAction;s.window=performanceWindow;
            s.tick=unsigned(app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps());s.time=now;s.interval=performancePrevious?now-performancePrevious:0;performancePrevious=now;
            s.physicsNs=performancePhysicsNs;s.physicsCalls=performancePhysicsCalls;performancePhysicsNs=0;performancePhysicsCalls=0;
            s.requests=app.m_OutlineHoverRequests;s.reads=app.m_OutlineHoverReadbacks;s.selected=app.m_EditorSelection.count;
            s.hover=stats.selectionHoverDraws;s.passes=stats.selectionPasses;s.draws=stats.selectionDrawCalls;s.allocations=unsigned(stats.selectionScratchAllocations);
            if(app.m_RenderTarget){const auto& d=app.m_RenderTarget->Buffer().Description();s.width=d.Width;s.height=d.Height;}
            if(app.m_CameraSnapshot)s.camera=app.m_CameraSnapshot->Description().position;
            s.panelCount=unsigned(app.m_CameraPanels.size());s.platformViews=ImGui::GetPlatformIO().Viewports.Size;
            s.swap=app.GetWindow()->GetSwapInterval();s.cap=app.GetManualFrameRateLimit();
            if(app.m_RenderTarget)s.mainReallocations=app.m_RenderTarget->ReallocationCount();
            for(const auto& p:app.m_CameraPanels){s.visiblePanels+=p.visible;s.readyPanels+=p.ready;s.panelTargets+=bool(p.color)+bool(p.cascade);s.panelWidth=p.width;s.panelHeight=p.height;s.panelFrames+=p.renderedFrames;if(p.color)s.panelReallocations+=p.color->ReallocationCount();}
            auto e=app.m_ActiveScene->FindEntityByName("Chain link 8");if(e)if(auto h=app.m_ActiveScene->GetBodyBinding(e))if(auto body=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->ReadBody(*h))s.body=body->position;
            performanceFrames.push_back(s);
            const auto& timings=app.GetWindow()->Timings();
            for(const auto& p:timings.Current())performancePasses.push_back({editorFrame,false,p});
            for(const auto& p:timings.Collected())performancePasses.push_back({editorFrame,true,p});
        }
        static void PerformancePointer(RigidBodySimulationApp& app,float x,float y) {
            const auto lo=app.m_ViewportBounds[0],hi=app.m_ViewportBounds[1];
            const ImVec2 point{lo.x+(hi.x-lo.x)*x,lo.y+(hi.y-lo.y)*y};
            EditorMovePointer(app,point.x,point.y);
            int ox{},oy{};if(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_ViewportsEnable)
                SDL_GetWindowPosition(SDL_GetWindowFromID(app.GetWindow()->GetWindowID()),&ox,&oy);
            SDL_Event e{};e.type=SDL_MOUSEMOTION;e.motion.windowID=app.GetWindow()->GetWindowID();e.motion.x=int(point.x)-ox;e.motion.y=int(point.y)-oy;
            Check(SDL_PushEvent(&e)==1,"performance real SDL pointer motion");
        }
        static void PerformanceAction(RigidBodySimulationApp& app,const EditorAction& action) {
            const std::string_view key=action.key?action.key:"";
            if(key=="setup") {
                if(editorAge==1) {
                    Check(!performanceMode && std::getenv("GENGINE_PHASE28_POINTER_PERFORMANCE"),"explicit bounded performance admission");
                    performanceMode=true;performanceFrames.reserve(1800);performancePasses.reserve(180000);
                    performancePreviousObserver=::GEngine::Phase26Observation::observer;
                    performanceObserver={nullptr,PerformanceBegin,PerformanceEnd,PerformanceAllocation};
                    ::GEngine::Phase26Observation::observer=&performanceObserver;
                    Check(bool(app.GetWindow()->BeginRender()),"performance setup primary GL context");
                    app.SetManualFrameRateLimit(165);Check(SDL_GL_SetSwapInterval(0)==0,"matched swap disabled");
                    Check(bool(app.GetWindow()->Timings().Initialize(true)),"existing pass timer explicit setup outside frame");
                }
                if(editorAge==8) {
                    Check(app.GetWindow()->Timings().Enabled() && app.GetWindow()->GetSwapInterval()==0 && app.GetManualFrameRateLimit()==165,"matched existing timers and pacing");
                    std::ofstream o(output/"performance-settings.json");o<<"{\"swap\":"<<app.GetWindow()->GetSwapInterval()<<",\"cap\":"<<app.GetManualFrameRateLimit()<<",\"logical\":["<<(app.m_ViewportBounds[1].x-app.m_ViewportBounds[0].x)<<','<<(app.m_ViewportBounds[1].y-app.m_ViewportBounds[0].y)<<"],\"dpi\":"<<outlineDpi<<"}\n";
                    NextEditorAction();
                }return;
            }
            Check(performanceMode,"performance setup before stimulus");if(failed)return;
            if(key=="camera-sequence") {
                Check(app.m_ActiveSelection.scene==RbsScenePreset::DistanceChain && app.m_ActiveSelection.chainLinks==8 && app.m_ActiveScene->IsPaused(),"matched paused DistanceChain8 camera sequence");
                if(editorAge==1){Check(app.m_CameraPanels.empty(),"single-view baseline");PerformancePointer(app,-.02f,.5f);}
                performanceWindow=editorAge<=450?'A':editorAge<=1050?'B':'C';
                if(editorAge==451)app.OpenCameraPanel(app.m_SceneCameras[0]);
                if(editorAge==1051)for(auto& p:app.m_CameraPanels)p.open=false;
                if(editorAge==1500){performanceWindow='-';Check(app.m_CameraPanels.empty(),"closed Camera View retired");CaptureEditorUi(app,"camera-closed-ui");NextEditorAction();}
                return;
            }
            if(key=="camera-transform-readback") {
                const auto e=app.m_HoveredEntity;
                Check(e && e.HasAllComponents<::GEngine::Component::RenderCameraComponent>() && std::abs(e.GetComponent<::GEngine::Component::Transform3DComponent>().Translation.x-std::atof(action.value))<.001f,"real selected Camera Transform applied");
                ComponentReadback(app,action.value);CaptureEditorUi(app,action.value[0]=='-'?"camera-b-transform":"camera-a-transform");NextEditorAction();return;
            }
            if(key=="camera-match-readback") {
                const auto e=app.m_HoveredEntity;
                Check(e && e.HasAllComponents<::GEngine::Component::RenderCameraComponent>(),"matched Camera identity");
                if(e){const auto& t=e.GetComponent<::GEngine::Component::Transform3DComponent>();
                    Check(glm::length(t.Translation-app.m_EditorCamera_.GetPosition())<.001f && std::abs(glm::dot(t.QuatRotation,app.m_EditorCamera_.GetOrientation()))>.99999f,"atomic Match Editor View actual pose");}
                ComponentReadback(app,"camera-match-applied");NextEditorAction();return;
            }
            if(key=="pointer") {
                float x{},y{};Check(action.value && std::sscanf(action.value,"%f,%f",&x,&y)==2,"frozen normalized pointer coordinate");
                if(editorAge==1)PerformancePointer(app,x,y);
                if(editorAge==8){ComponentReadback(app,"performance-pointer");NextEditorAction();}return;
            }
            if(key=="visualization-off") {
                if(editorAge==1 && app.m_IsShowDebugBoundingBox) {
                    for(auto type:{SDL_KEYDOWN,SDL_KEYUP}) {SDL_Event e{};e.type=type;e.key.windowID=app.GetWindow()->GetWindowID();e.key.keysym.scancode=SDL_SCANCODE_SPACE;e.key.keysym.sym=SDLK_SPACE;e.key.state=type==SDL_KEYDOWN?SDL_PRESSED:SDL_RELEASED;Check(SDL_PushEvent(&e)==1,"actual Physics visualization shortcut");}
                }
                if(editorAge==8){Check(!app.m_IsShowDebugBoundingBox&&!app.m_ShowSelectedColliderOutlines,"actual visualization Off");NextEditorAction();}return;
            }
            Check(key.size()==1 && key[0]>='A' && key[0]<='E',"frozen performance window");if(failed)return;
            const unsigned steps=unsigned(app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps());
            if(editorAge==1) {
                const auto* w=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
                Check(steps==0 && w->GetBodyCount()==14 && w->GetJointCount()==8 && app.m_Backend==::GEngine::PhysicsBackend::Jolt && !app.m_IsShowDebugBoundingBox && !app.m_ShowSelectedColliderOutlines,"matched Tick0 DistanceChain8 Jolt input");
                Check((key=="A"||key=="D")?app.m_EditorSelection.count==0:app.m_HoveredEntity && app.m_HoveredEntity.Name()=="Chain link 8","matched selected identity");
                performanceWindow=key[0];
                app.SetEditorMode(RigidBodySimulationApp::EditorMode::Simulation);app.SetSimulationState(SimulationState::Running);
                // Identical existing command queue, consumed before the first step.
                app.m_ChainImpulseMagnitude=.5f;app.m_ChainImpulseDirection=0;
                Check(app.RequestDistanceChainImpulse(),"frozen 0.5 Ns positive-X terminal impulse");
            }
            if(steps>=87) {
                app.SetSimulationState(SimulationState::Paused);performanceWindow='-';
                Check(steps==87,"same exact completed Physics interval");
                EditorLog("performance-window-complete",action.key);NextEditorAction();return;
            }
            if(key=="C"||key=="D") {
                // Motion cadence is tied to Physics time, identical across variants.
                if(editorPhase!=steps/6+1){editorPhase=steps/6+1;const std::array<glm::vec2,5> points{{{.5f,.75f},{.5f,.5f},{.3f,.85f},{.9f,.1f},{-.02f,.5f}}};const auto p=points[(steps/6)%points.size()];PerformancePointer(app,p.x,p.y);}
            } else if(key=="E") {
                if(editorPhase!=steps/15+1){editorPhase=steps/15+1;PerformancePointer(app,.5f,.5f);SDL_Event e{};e.type=SDL_MOUSEWHEEL;e.wheel.windowID=app.GetWindow()->GetWindowID();e.wheel.y=(steps/15)%2?1:-1;e.wheel.preciseY=float(e.wheel.y);Check(SDL_PushEvent(&e)==1,"matched real SDL zoom input");}
            }
        }
        static float OutlineViewportScale() {return editorMode?outlineDpi:1.f;}
        static void PrepareOutlineDescriptor(RigidBodySimulationApp&,::GEngine::FrameSubmissionDesc& desc) {
            if(!editorMode || !outlineActive)return;
            desc.measureSubmission=true;outlinePixelRatio=desc.selectionPixelsPerDip;
            outlineSubmitBegin=std::chrono::steady_clock::now();
        }
        static void ObserveOutlineSubmission(RigidBodySimulationApp& app,const ::GEngine::FrameSubmissionStats& stats) {
            if(!editorMode || !outlineActive)return;
            outlineStats=stats;outlineObservedFrame=editorFrame;
            if(performanceMode){PerformanceObserve(app,stats);return;}
            outlineSubmitUs=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-outlineSubmitBegin).count();
            std::ofstream o(output/"outline-frames.jsonl",std::ios::app);
            o<<"{\"frame\":"<<editorFrame<<",\"action\":"<<editorAction<<",\"draws\":"<<stats.selectionDrawCalls
             <<",\"monotonic_seconds\":"<<std::setprecision(17)<<std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count()
             <<",\"passes\":"<<stats.selectionPasses<<",\"active\":"<<stats.selectionActiveDraws<<",\"secondary\":"<<stats.selectionSecondaryDraws
             <<",\"hover\":"<<stats.selectionHoverDraws<<",\"shared\":"<<(stats.selectionSharedVisibility?"true":"false")
             <<",\"scratch_bytes\":"<<stats.selectionCurrentScratchBytes<<",\"scratch_allocations\":"<<stats.selectionScratchAllocations
             <<",\"hover_requests\":"<<app.m_OutlineHoverRequests<<",\"hover_readbacks\":"<<app.m_OutlineHoverReadbacks
             <<",\"submission_cpu_us\":"<<outlineSubmitUs<<"}\n";
            Check(o.good(),"outline actual submission observations");
        }
        static void OutlineRecord(RigidBodySimulationApp& app,std::ostream& o) {
            if(!outlineActive)return;
            const auto& s=outlineStats;
            o<<",\"outline.active\":"<<s.selectionActiveDraws<<",\"outline.secondary\":"<<s.selectionSecondaryDraws
             <<",\"outline.hover\":"<<s.selectionHoverDraws<<",\"outline.passes\":"<<s.selectionPasses
             <<",\"outline.shared\":"<<s.selectionSharedVisibility<<",\"outline.scratch_allocations\":"<<s.selectionScratchAllocations
             <<",\"outline.scratch_bytes\":"<<s.selectionCurrentScratchBytes
             <<",\"outline.dpi\":"<<outlineDpi<<",\"outline.pixel_ratio\":["<<outlinePixelRatio.x<<','<<outlinePixelRatio.y<<']'
             <<",\"outline.hover_requests\":"<<app.m_OutlineHoverRequests<<",\"outline.hover_readbacks\":"<<app.m_OutlineHoverReadbacks;
            auto hover=app.m_OutlineHoverIcon?app.m_OutlineHoverIcon:app.m_OutlineHoverEntity;
            auto resolved=app.m_ActiveScene->RenderData().Resolve(hover);
            o<<",\"outline.hover_name\":"<<std::quoted(resolved?::GEngine::_Entity{*resolved,app.m_ActiveScene.get()}.GetName():std::string{});
            if(app.m_RenderTarget) {const auto& d=app.m_RenderTarget->Buffer().Description();o<<",\"outline.target_size\":["<<d.Width<<','<<d.Height<<']';}
            if(app.m_CameraSnapshot) {
                const auto& d=app.m_CameraSnapshot->Description();
                o<<",\"outline.camera_position\":["<<d.position.x<<','<<d.position.y<<','<<d.position.z<<']'
                 <<",\"outline.camera_orientation\":["<<d.orientation.w<<','<<d.orientation.x<<','<<d.orientation.y<<','<<d.orientation.z<<']'
                 <<",\"outline.logical_size\":["<<d.viewport.logicalSize.x<<','<<d.viewport.logicalSize.y<<']';
            }
        }
        static std::optional<bool> OutlineExpected(RigidBodySimulationApp& app,std::string_view key) {
            if(!key.starts_with("outline."))return std::nullopt;
            if(!outlineActive || outlineObservedFrame+1<editorFrame || app.m_SceneRequest || app.m_BackendRequest)return false;
            if(key.ends_with("idle"))return !app.m_HoveredEntity && !app.m_OutlineHoverEntity && !app.m_OutlineHoverIcon &&
                outlineStats.selectionPasses==0 && outlineStats.selectionDrawCalls==0 && outlineStats.selectionCurrentScratchBytes==0 &&
                outlineStats.selectionScratchAllocations==outlineIdleAllocations;
            if(key.ends_with("stationary"))return app.m_OutlineHoverRequests==outlineStationaryRequests && app.m_OutlineHoverReadbacks==outlineStationaryReads;
            if(key.ends_with("multi"))return app.m_EditorSelection.count>=2 && outlineStats.selectionActiveDraws>0 && outlineStats.selectionSecondaryDraws>0;
            if(key.ends_with("fallback"))return outlineStats.selectionPasses>0 && !outlineStats.selectionSharedVisibility;
            if(key.ends_with("shared"))return outlineStats.selectionPasses>0 && outlineStats.selectionSharedVisibility;
            if(key.ends_with("dpi2"))return outlineDpi==2.f && outlinePixelRatio.x>=1.99f && outlinePixelRatio.y>=1.99f;
            if(key.ends_with("glyph"))return bool(app.m_HoveredEntity) && (app.m_HoveredEntity.HasAllComponents<::GEngine::Component::RenderCameraComponent>() || app.m_HoveredEntity.HasAllComponents<::GEngine::Component::RenderLightComponent>());
            return bool(app.m_HoveredEntity) && outlineStats.selectionActiveDraws>0;
        }
        static void OutlineStimulus(RigidBodySimulationApp& app,std::string_view mode) {
            using namespace ::GEngine;using namespace ::GEngine::Component;
            // Explicitly isolated renderer qualification stimulus, never a claim
            // that material alpha-mode creation exists in ordinary Properties.
            if(editorAge==1) {
                auto& scene=*app.m_ActiveScene;const auto count=scene.GetPhysicsSystem()->GetPhysicsWorld()->GetBodyCount();
                auto source=scene.FindEntityByName("Sandbox Floor");
                Check(source && app.m_CameraSnapshot,"isolated renderer fixture prerequisites");if(failed)return;
                const auto mesh=source.GetComponent<MeshRendererComponent>().mesh;
                if(mode=="restore") {
                    if(outlineProbeEntity)Check(bool(scene.DestroyEntity(outlineProbeEntity)),"retire isolated outline target");
                    if(outlineOccluder)Check(bool(scene.DestroyEntity(outlineOccluder)),"retire isolated outline occluder");
                    outlineProbeEntity={};outlineOccluder={};
                } else {
                    if(!outlineProbeEntity) {
                        Check(mode=="opaque","isolated fixture explicit initial identity");if(failed)return;
                        auto p=scene.CreateEntity("Outline Probe"),b=scene.CreateEntity("Outline Occluder");
                        Check(p && b,"bounded isolated render-only identities");if(failed)return;outlineProbeEntity=*p;outlineOccluder=*b;
                        MaterialAuthoringDesc material;material.baseColor={.6f,.55f,.5f,1.f};auto opaque=app.m_SceneResources->CreateMaterial(material);
                        const auto path=output/"outline-alpha.tga";std::array<unsigned char,18+16*16*4> texels{};texels[2]=2;texels[12]=16;texels[14]=16;texels[16]=32;texels[17]=8;
                        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){const auto i=18+(y*16+x)*4;texels[i]=texels[i+1]=texels[i+2]=180;texels[i+3]=((x/4+y/4)%2)?0:255;}
                        Check(!std::filesystem::exists(path),"create-only alpha input");std::ofstream textureFile(path,std::ios::binary);textureFile.write(reinterpret_cast<const char*>(texels.data()),texels.size());textureFile.close();
                        auto texture=Manager::AssetsManager::LoadTexture(path.string());Asset::SamplerDesc samplerDesc;samplerDesc.minFilter=samplerDesc.magFilter=Asset::SamplerFilter::Nearest;
                        auto sampler=Manager::AssetsManager::GetSampler(samplerDesc);Check(opaque && texture && sampler,"bounded real alpha texture and material");if(failed)return;
                        outlineOpaque=*opaque;material.kind=MaterialKind::Masked;material.textures[0]=MaterialTextureValue{*texture,*sampler};auto masked=app.m_SceneResources->CreateMaterial(material);
                        material.kind=MaterialKind::Transparent;material.opacity=.5f;auto transparent=app.m_SceneResources->CreateMaterial(material);
                        Check(masked && transparent,"actual masked and transparent material states");if(failed)return;outlineMasked=*masked;outlineTransparent=*transparent;
                        const auto& camera=app.m_CameraSnapshot->Description();const auto rotation=Quat(camera.orientation);
                        const Vec center=Vec(camera.position)+rotation*Vec{0,0,-12};
                        Check(bool(scene.SetLocalPose(outlineProbeEntity,center,rotation)) &&
                            bool(scene.SetLocalScale(outlineProbeEntity,{2,2,.5f})),"render-only target placement");
                        Check(bool(app.m_SceneResources->AssignRenderable(outlineOccluder,{mesh,outlineOpaque})) && bool(scene.SetLocalPose(outlineOccluder,{10000,10000,10000},rotation)) &&
                            bool(scene.SetLocalScale(outlineOccluder,{1.25f,3,.2f})),"bounded real occluder renderable");
                    }
                    Check(mode=="opaque" || mode=="masked" || mode=="transparent" || mode=="occluded","explicit alpha/depth fixture protocol");
                    const auto material=mode=="masked"?outlineMasked:mode=="transparent"?outlineTransparent:outlineOpaque;
                    Check(bool(app.m_SceneResources->AssignRenderable(outlineProbeEntity,{mesh,material})),"real material coverage assignment");
                    const auto& camera=app.m_CameraSnapshot->Description();const Vec blocker=mode=="occluded"?
                        Vec(camera.position)+Quat(camera.orientation)*Vec{-.65f,0,-10}:Vec{10000,10000,10000};
                    Check(bool(scene.SetLocalTranslation(outlineOccluder,blocker)),"real partial depth occlusion");
                }
                Check(scene.GetPhysicsSystem()->GetPhysicsWorld()->GetBodyCount()==count,"isolated render stimulus creates no physical bodies");
            }
            if(editorAge>=8){EditorLog("outline-stimulus",mode.data());NextEditorAction();}
        }
        static bool OutlineAction(RigidBodySimulationApp& app,const EditorAction& action) {
            if(action.op<EditorOp::Pointer)return false;
            outlineActive=true;
            if(action.op==EditorOp::Performance){PerformanceAction(app,action);return true;}
            if(action.op==EditorOp::NativeImage)return true;
            const std::string_view mode=action.value?action.value:"";
            auto* window=SDL_GetWindowFromID(app.GetWindow()->GetWindowID());
            Check(window!=nullptr,"outline ordinary SDL window");if(!window)return true;
            auto& io=ImGui::GetIO();
            if(action.op==EditorOp::Wait) {
                const int frames=action.value?std::atoi(action.value):0;
                Check(frames>=8 && frames<=40,"bounded outline observation interval");
                if(editorAge==1){outlineStationaryRequests=app.m_OutlineHoverRequests;outlineStationaryReads=app.m_OutlineHoverReadbacks;outlineIdleAllocations=outlineStats.selectionScratchAllocations;}
                if(editorAge>=unsigned(frames)){EditorLog("outline-wait",action.key);NextEditorAction();}return true;
            }
            if(action.op==EditorOp::Dpi) {
                Check(mode=="1" || mode=="2","bounded actual framebuffer scale stimulus");
                outlineDpi=mode=="2"?2.f:1.f;
                if(editorAge>=16 && outlineObservedFrame+1>=editorFrame){EditorLog("outline-dpi",action.key);NextEditorAction();}return true;
            }
            if(action.op==EditorOp::Resize) {
                if(editorAge==1) {
                    if(mode=="restore") {SDL_SetWindowPosition(window,outlineWindow[0],outlineWindow[1]);SDL_SetWindowSize(window,outlineWindow[2],outlineWindow[3]);}
                    else {SDL_GetWindowPosition(window,&outlineWindow[0],&outlineWindow[1]);SDL_GetWindowSize(window,&outlineWindow[2],&outlineWindow[3]);
                        Check(mode=="1280x800-offset","frozen real resize and primary-window origin");SDL_RestoreWindow(window);SDL_SetWindowPosition(window,96,72);SDL_SetWindowSize(window,1280,800);}
                }
                if(editorAge>=16){EditorLog("outline-resized",action.key);NextEditorAction();}return true;
            }
            if(action.op==EditorOp::Wheel) {
                if(editorAge==1) {
                    const auto p=(app.m_ViewportBounds[0]+app.m_ViewportBounds[1])*.5f;EditorMovePointer(app,p.x,p.y);
                    SDL_Event e{};e.type=SDL_MOUSEWHEEL;e.wheel.windowID=app.GetWindow()->GetWindowID();e.wheel.y=std::atoi(action.value);e.wheel.preciseY=float(e.wheel.y);
                    Check(e.wheel.y!=0 && std::abs(e.wheel.y)<=3 && SDL_PushEvent(&e)==1,"actual bounded SDL camera wheel");
                }
                if(editorAge>=12){EditorLog("outline-wheel",action.key);NextEditorAction();}return true;
            }
            if(action.op==EditorOp::Stimulus) {OutlineStimulus(app,mode);return true;}
            if(action.op!=EditorOp::Pointer)return false;
            const bool outside=mode=="outside" || mode=="clear";
            const bool hover=mode=="hover",control=mode=="ctrl";
            Check(outside || hover || control || mode=="click","explicit outline pointer operation");
            auto entity=outside?::GEngine::_Entity{}:app.m_ActiveScene->FindEntityByName(action.key);
            ImVec2 point{app.m_ViewportBounds[0].x-12.f,app.m_ViewportBounds[0].y+60.f};
            if(!outside) {
                if(!entity || !app.m_CameraSnapshot)return true;
                const auto id=app.m_ActiveScene->RenderData().Identify(entity);if(!id)return true;
                bool icon=false;for(std::size_t i=0;i<app.m_VisibleSceneIconCount;++i)if(app.m_VisibleSceneIcons[i].id==*id) {
                    const auto& hit=app.m_VisibleSceneIcons[i];point={(hit.lo.x+hit.hi.x)*.5f,(hit.lo.y+hit.hi.y)*.5f};icon=true;break;
                }
                if(!icon) {
                    auto rendered=app.m_ActiveScene->GetRenderTransform(entity);if(!rendered)return true;
                    auto matrix=rendered->matrix;auto parent=entity;
                    while(!parent.HasAllComponents<::GEngine::Component::RigidBody3DComponent>()) {
                        parent=parent.GetParent();if(!parent)break;
                        const auto transform=app.m_ActiveScene->GetRenderTransform(parent);if(!transform)return true;
                        matrix=transform->matrix*matrix;
                    }
                    const auto projected=app.m_CameraSnapshot->WorldToNdc(glm::dvec3(matrix[3]));
                    if(!projected || !projected->insideFrustum)return true;
                    const auto lo=app.m_ViewportBounds[0],hi=app.m_ViewportBounds[1];
                    point={lo.x+(hi.x-lo.x)*float(.5+.5*projected->value.x),lo.y+(hi.y-lo.y)*float(.5-.5*projected->value.y)};
                }
            }
            if(editorPhase==0) {
                outlinePointerSelection=app.m_ActiveScene->RenderData().Identify(app.m_HoveredEntity).value_or(::GEngine::EntityRenderId{});
                EditorMovePointer(app,point.x,point.y);int ox{},oy{};
                if(io.ConfigFlags&ImGuiConfigFlags_ViewportsEnable)SDL_GetWindowPosition(window,&ox,&oy);
                SDL_Event e{};e.type=SDL_MOUSEMOTION;e.motion.windowID=app.GetWindow()->GetWindowID();e.motion.x=int(point.x)-ox;e.motion.y=int(point.y)-oy;
                Check(SDL_PushEvent(&e)==1,"real hover motion with no held button");
                if(mode=="clear") {for(auto type:{SDL_KEYDOWN,SDL_KEYUP}){SDL_Event k{};k.type=type;k.key.windowID=app.GetWindow()->GetWindowID();k.key.keysym.scancode=SDL_SCANCODE_ESCAPE;k.key.keysym.sym=SDLK_ESCAPE;k.key.state=type==SDL_KEYDOWN?SDL_PRESSED:SDL_RELEASED;Check(SDL_PushEvent(&k)==1,"actual clear selection shortcut");}}
                ++editorPhase;return true;
            }
            if(!hover && !outside && (editorPhase==1 || editorPhase==2)) {
                if(control && editorPhase==1) {
                    SDL_SetModState(KMOD_CTRL);io.AddKeyEvent(ImGuiKey_ModCtrl,true);
                    SDL_Event k{};k.type=SDL_KEYDOWN;k.key.windowID=app.GetWindow()->GetWindowID();k.key.state=SDL_PRESSED;
                    k.key.keysym.scancode=SDL_SCANCODE_LCTRL;k.key.keysym.sym=SDLK_LCTRL;k.key.keysym.mod=KMOD_CTRL;
                    Check(SDL_PushEvent(&k)==1,"actual additive selection modifier down");
                }
                EditorPointerEvent(app,point,editorPhase==1?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP);++editorPhase;return true;
            }
            if(control && editorPhase==3){SDL_SetModState(KMOD_NONE);io.AddKeyEvent(ImGuiKey_ModCtrl,false);
                SDL_Event k{};k.type=SDL_KEYUP;k.key.windowID=app.GetWindow()->GetWindowID();k.key.state=SDL_RELEASED;
                k.key.keysym.scancode=SDL_SCANCODE_LCTRL;k.key.keysym.sym=SDLK_LCTRL;Check(SDL_PushEvent(&k)==1,"actual additive selection modifier up");++editorPhase;}
            if(editorAge<12)return true;
            const auto selected=app.m_ActiveScene->RenderData().Identify(app.m_HoveredEntity).value_or(::GEngine::EntityRenderId{});
            const auto id=entity?app.m_ActiveScene->RenderData().Identify(entity).value_or(::GEngine::EntityRenderId{}): ::GEngine::EntityRenderId{};
            const bool observed=outside?(!app.m_OutlineHoverEntity && !app.m_OutlineHoverIcon && (mode!="clear" || !selected)):
                hover?((app.m_OutlineHoverEntity==id || app.m_OutlineHoverIcon==id) && selected==outlinePointerSelection):selected==id;
            if(observed){EditorLog("outline-pointer",action.key,0,point,point);NextEditorAction();}return true;
        }
        static void GeneralViewportGesture(RigidBodySimulationApp& app,EditorOp op) {
            auto& scene=*app.m_ActiveScene;
            auto mesh=scene.FindEntityByName("Template Plane");
            if(!mesh || !app.m_CameraSnapshot)return;
            const auto pos=mesh.Transform().Translation;
            const auto lo=app.m_ViewportBounds[0],hi=app.m_ViewportBounds[1];
            const auto projected=app.m_CameraSnapshot->WorldToNdc(glm::dvec3(pos));
            if(!projected || !projected->insideFrustum)return;
            const ImVec2 center{lo.x+(hi.x-lo.x)*float(.5+.5*projected->value.x),
                                lo.y+(hi.y-lo.y)*float(.5-.5*projected->value.y)};
            if(op==EditorOp::PickMesh) {
                if(editorPhase==0) {EditorMovePointer(app,center.x,center.y);++editorPhase;return;}
                if(editorPhase==1 || editorPhase==2) {
                    EditorPointerEvent(app,center,editorPhase==1?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP);++editorPhase;return;
                }
                if(app.m_ViewportActive && app.m_HoveredEntity==mesh) {
                    EditorLog("viewport-picked","Template Plane",0,center,center);NextEditorAction();
                } else if(editorPhase==3 && editorAge==10 && app.m_ViewportActive) {
                    // Some viewport layouts consume the first press for focus.
                    editorPhase=1;
                }
                return;
            }
            if(app.m_HoveredEntity!=mesh)return;
            if(editorPhase==0) {
                meshBeforeDrag=pos;dragStart=center;
                EditorMovePointer(app,center.x,center.y);++editorPhase;return;
            }
            if(editorPhase==1) {
                if(!app.m_GizmoHovered)return;
                EditorPointerEvent(app,dragStart,SDL_MOUSEBUTTONDOWN);++editorPhase;return;
            }
            if(editorPhase==2 && !app.m_GizmoUsing)return;
            if(editorPhase>=2 && editorPhase<=4) {
                ImVec2 destination{dragStart.x+float(14*(editorPhase-1)),dragStart.y-float(4*(editorPhase-1))};
                EditorPointerEvent(app,destination,SDL_MOUSEMOTION);++editorPhase;return;
            }
            if(editorPhase==5) {
                EditorPointerEvent(app,{dragStart.x+42.f,dragStart.y-12.f},SDL_MOUSEBUTTONUP);++editorPhase;return;
            }
            auto published=scene.GetRenderTransform(mesh);
            if(app.m_GizmoUsing || !published || glm::length(pos-meshBeforeDrag)<.01f ||
               !::GEngine::Math::IsFinite(pos) || glm::length(Vec(published->matrix[3])-pos)>1.e-4f)return;
            meshAfterDrag=pos;
            EditorLog("gizmo-drag-applied","Template Plane",0,dragStart,{dragStart.x+42.f,dragStart.y-12.f});
            NextEditorAction();
        }
        static bool EditorRoomReadback(RigidBodySimulationApp& app) {
            auto& scene=*app.m_ActiveScene;
            auto* physics=scene.GetPhysicsSystem();
            auto* world=physics?physics->GetPhysicsWorld():nullptr;
            if(!world || world->GetBackend()!=app.m_Backend)return false;
            for(const auto& box:SandboxEnvironment::Boxes()) {
                auto entity=scene.FindEntityByName(box.name);
                if(!entity || !scene.RenderData().Identify(entity))return false;
                auto binding=scene.GetBodyBinding(entity);
                if(!binding)return false;
                auto state=world->ReadBody(*binding);
                auto collider=physics->ReadBodyCollider(*binding);
                if(!state || !collider || state->type!=::GEngine::Component::BodyType::Static ||
                    state->shapeType!=::GEngine::ShapeType::Box ||
                    !std::holds_alternative<::GEngine::BoxShapeDesc>(collider->dimensions))return false;
                const Vec center{box.center[0],box.center[1],box.center[2]};
                const Vec half=.5f*Vec{box.size[0],box.size[1],box.size[2]};
                if(glm::length(state->worldBounds.mins-(center-half))>1.e-4f ||
                   glm::length(state->worldBounds.maxs-(center+half))>1.e-4f ||
                   std::abs(state->material.friction-.5f)>1.e-5f ||
                   std::abs(state->material.restitution-.5f)>1.e-5f)return false;
            }
            return !app.m_ActiveSelection.IsAngularScene() || world->GetBodyCount()==7;
        }
        static bool EditorSelectedReadback(RigidBodySimulationApp& app,std::string_view key) {
            const char* name=key=="selected-bar"?"Angular Bar":key=="selected-anchor"?"Angular Anchor":
                key=="selected-floor"?"Sandbox Floor":"Sandbox Wall -X";
            if(!app.m_HoveredEntity || app.m_HoveredEntity.GetSceneContext()!=app.m_ActiveScene.get() ||
                app.m_HoveredEntity.GetName()!=name)return false;
            auto binding=app.m_ActiveScene->GetBodyBinding(app.m_HoveredEntity);
            auto* world=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            if(!binding || !world)return false;
            const auto body=world->ReadBody(*binding);
            const auto properties=world->ReadBodyProperties(*binding);
            if(!body || !properties || !::GEngine::Math::IsFinite(body->centerOfMass))return false;
            for(int axis=0;axis<3;++axis)
                if(!::GEngine::Math::IsFinite(body->inverseInertiaBody[axis]) ||
                   !::GEngine::Math::IsFinite(body->inverseInertiaWorld[axis]))return false;
            const auto joint=world->ReadJoint(app.m_AngularBindings.joint);
            if(!joint)return false;
            const bool endpoint=key=="selected-bar" || key=="selected-anchor";
            return endpoint?(*binding==joint->descriptor.bodyA || *binding==joint->descriptor.bodyB):
                (*binding!=joint->descriptor.bodyA && *binding!=joint->descriptor.bodyB);
        }
        static bool SameAngularPacket(const AngularPacket& a,const AngularPacket& b) {
            if(a.scene!=b.scene || a.variant!=b.variant ||
               a.referenceRevision!=b.referenceRevision ||
               a.sourceSha256!=b.sourceSha256 || a.fixtureSha256!=b.fixtureSha256 ||
               a.bodyCount!=b.bodyCount || a.gravity!=b.gravity ||
               a.fixedStepSeconds!=b.fixedStepSeconds)return false;
            for(std::size_t i=0;i<a.bodyCount;++i) {
                const auto& x=a.bodies[i];const auto& y=b.bodies[i];
                if(x.key!=y.key || x.center!=y.center || x.halfExtents!=y.halfExtents ||
                   x.orientation!=y.orientation ||
                   x.initialLinearVelocity!=y.initialLinearVelocity ||
                   x.initialAngularVelocity!=y.initialAngularVelocity ||
                   x.authoredMass!=y.authoredMass || x.friction!=y.friction ||
                   x.restitution!=y.restitution || x.sphere!=y.sphere || x.kinematic!=y.kinematic ||
                   x.linearDamping!=y.linearDamping || x.angularDamping!=y.angularDamping)return false;
            }
            const auto& x=a.joint;const auto& y=b.joint;
            if(x.key!=y.key || x.bodyA!=y.bodyA || x.bodyB!=y.bodyB ||
               x.localAnchorA!=y.localAnchorA || x.localAnchorB!=y.localAnchorB ||
               x.localFrameA!=y.localFrameA || x.localFrameB!=y.localFrameB ||
               x.lower!=y.lower || x.upper!=y.upper ||
               x.halfAngleU!=y.halfAngleU || x.halfAngleV!=y.halfAngleV ||
               x.limitsEnabled!=y.limitsEnabled ||
               x.collideConnected!=y.collideConnected)return false;
            for(std::size_t i=0;i<a.scheduledImpulses.size();++i)
                if(a.scheduledImpulses[i]!=b.scheduledImpulses[i])return false;
            return true;
        }
        static bool EditorResetReadback(RigidBodySimulationApp& app) {
            if(!editorReset.packet || app.m_SceneGeneration<=editorReset.generation ||
               app.m_SceneRequest || app.m_SimulationState!=SimulationState::Ready ||
               app.m_Backend!=editorReset.backend || app.m_Angular.revision!=editorReset.revision || app.m_Angular.dirty ||
               app.m_Angular.interactive || !app.m_Angular.committed ||
               app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()!=0 ||
               (app.m_HoveredEntity &&
                app.m_HoveredEntity.GetSceneContext()!=app.m_ActiveScene.get()))return false;
            const auto& expected=*editorReset.packet;
            const auto& actual=*app.m_Angular.committed;
            if(!SameAngularPacket(actual,expected))return false;
            auto* world=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            if(!world || world->GetBackend()!=editorReset.backend ||
               world->ReadBody(editorReset.bar) || world->ReadJoint(editorReset.joint) ||
               app.m_ChainImpulse.AppliedCount()!=0 || !EditorRoomReadback(app))return false;
            return bool(app.VerifyAngularCandidate(actual));
        }
        static bool EditorFailedSwitchReadback(RigidBodySimulationApp& app) {
            if(!editorFailure.scene || !editorFailure.world || !editorFailure.packet ||
               app.m_ActiveScene.get()!=editorFailure.scene ||
               app.m_SceneGeneration!=editorFailure.generation ||
               app.m_Angular.revision!=editorFailure.revision ||
               !app.m_Angular.committed ||
               !SameAngularPacket(*app.m_Angular.committed,*editorFailure.packet) ||
               app.m_AngularBindings.bodies[0]!=editorFailure.anchor ||
               app.m_AngularBindings.bodies[1]!=editorFailure.bar ||
               app.m_AngularBindings.joint!=editorFailure.joint)return false;
            auto* world=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            return world==editorFailure.world &&
                bool(world->ReadBody(editorFailure.anchor)) &&
                bool(world->ReadBody(editorFailure.bar)) &&
                bool(world->ReadJoint(editorFailure.joint)) &&
                bool(app.VerifyAngularCandidate(*editorFailure.packet));
        }
        static bool EditorAuthoredBodyReadback(RigidBodySimulationApp& app,bool pending,bool portable) {
            const auto& session=app.m_Angular;
            if(app.m_SceneRequest || !session.draft || !session.committed ||
               session.dirty!=pending || !EditorSelectedReadback(app,"selected-bar"))return false;
            const auto& authored=(pending?*session.draft:*session.committed);
            const auto& bar=authored.bodies[1];
            const auto closeValue=[](float lhs,float rhs){return std::abs(lhs-rhs)<1.e-4f;};
            const Quat rotation=glm::quat(glm::radians(Vec{0.f,10.f,0.f}));
            if(!closeValue(bar.center.x,1.f) || !closeValue(std::abs(glm::dot(bar.orientation,rotation)),1.f) ||
               !closeValue(bar.halfExtents.x,.18f) || !closeValue(bar.authoredMass,1.25f) ||
               !closeValue(bar.friction,.35f) || !closeValue(bar.restitution,.2f))return false;
            const auto& anchor=authored.bodies[0];
            if(!anchor.sphere || !anchor.kinematic || !closeValue(anchor.halfExtents.x,.28f))return false;
            const auto reference=ReferenceAngularPacket(authored.scene,authored.variant);
            if(!reference)return false;
            const auto expectedFrame=glm::angleAxis(glm::radians(15.f),Vec{0.f,0.f,1.f})*reference->joint.localFrameA;
            if(!closeValue(std::abs(glm::dot(authored.joint.localFrameA,expectedFrame)),1.f) ||
               !closeValue(std::abs(glm::dot(authored.joint.localFrameB,expectedFrame)),1.f))return false;
            if(authored.scene==RbsScenePreset::HingeLimited && !closeValue(glm::degrees(authored.joint.upper),30.f))return false;
            if(authored.scene==RbsScenePreset::CvLimited && !closeValue(glm::degrees(authored.joint.halfAngleU),30.f))return false;
            auto& scene=*app.m_ActiveScene;
            auto* world=scene.GetPhysicsSystem()->GetPhysicsWorld();
            const auto handle=app.m_AngularBindings.bodies[1];
            const auto body=world->ReadEffectiveBody(handle);
            const auto properties=world->ReadBodyProperties(handle);
            const auto joint=world->ReadJoint(app.m_AngularBindings.joint);
            if(!body || !joint || !properties || joint->descriptor.bodyB!=handle || joint->error>1.e-4f)return false;
            const auto& committed=session.committed->bodies[1];
            const auto& committedAnchor=session.committed->bodies[0];
            const auto providerAnchor=world->ReadEffectiveBody(app.m_AngularBindings.bodies[0]);
            auto anchorEntity=scene.FindEntityByName("Angular Anchor");
            if(!providerAnchor || !anchorEntity || !anchorEntity.HasAllComponents<::GEngine::Component::MeshRendererComponent>() ||
               providerAnchor->shapeType!=(committedAnchor.sphere?::GEngine::ShapeType::Sphere: ::GEngine::ShapeType::Box) ||
               providerAnchor->type!=(committedAnchor.kinematic?::GEngine::Component::BodyType::Kinematic: ::GEngine::Component::BodyType::Static) ||
               !closeValue(providerAnchor->inverseMass,0.f) || !closeValue(std::abs(glm::dot(joint->descriptor.angular.localFrameA,
                   session.committed->joint.localFrameA)),1.f) ||
               glm::length(.5f*(providerAnchor->localBounds.maxs-providerAnchor->localBounds.mins)-committedAnchor.halfExtents)>1.e-4f ||
               glm::length(anchorEntity.Transform().Translation-providerAnchor->position)>1.e-4f ||
               glm::length(anchorEntity.Transform().Scale-(committedAnchor.sphere?1.f:2.f)*committedAnchor.halfExtents)>1.e-4f)return false;
            const auto& render=app.m_HoveredEntity.Transform();
            const float expectedDamping=!pending && !portable && app.m_Backend==::GEngine::PhysicsBackend::Jolt?.1f:0.f;
            // ShapeBox::InertiaTensor uses the rectangular solid about its center of mass.
            for(int axis=0;axis<3;++axis) {
                const float h1=committed.halfExtents[(axis+1)%3],h2=committed.halfExtents[(axis+2)%3];
                const float expected=3.f/(committed.authoredMass*(h1*h1+h2*h2));
                if(std::abs(body->inverseInertiaBody[axis][axis]-expected)>
                   32.f*std::numeric_limits<float>::epsilon()*(std::max)(1.f,expected))return false;
            }
            if(!closeValue(properties->linearDamping,expectedDamping) ||
               !closeValue(body->inverseMass,1.f/committed.authoredMass) ||
               !closeValue(body->material.friction,committed.friction) || !closeValue(body->material.restitution,committed.restitution) ||
               !closeValue(std::abs(glm::dot(body->orientation,committed.orientation)),1.f) ||
               glm::length(body->position-committed.center)>1.e-4f ||
               glm::length(.5f*(body->localBounds.maxs-body->localBounds.mins)-committed.halfExtents)>1.e-4f ||
               glm::length(render.Translation-body->position)>1.e-4f ||
               glm::length(render.Scale-2.f*committed.halfExtents)>1.e-4f ||
               !closeValue(std::abs(glm::dot(render.QuatRotation,body->orientation)),1.f))return false;
            if(!app.VerifyAngularCandidate(*session.committed))return false;
            return pending?SameAngularPacket(*session.committed,*reference):
                SameAngularPacket(*session.committed,*session.draft);
        }
        static bool GeneralExpected(RigidBodySimulationApp& app,std::string_view key) {
            using namespace ::GEngine;
            using namespace ::GEngine::Component;
            auto& scene=*app.m_ActiveScene;
            auto& selected=app.m_HoveredEntity;
            if(app.m_SceneRequest || app.m_BackendRequest || !selected || selected.GetSceneContext()!=&scene)return false;
            const auto closeValue=[](float a,float b){return std::abs(a-b)<1.e-4f;};
            auto* world=scene.GetPhysicsSystem()->GetPhysicsWorld();
            if(!world || world->GetBackend()!=app.m_Backend)return false;
            auto camera=scene.FindEntityByName("Scene Camera A");
            auto directional=scene.FindEntityByName("ambient_light"),point=scene.FindEntityByName("point_light");
            if(!camera || !directional || !point || !camera.HasAllComponents<RenderCameraComponent>() ||
               !directional.HasAllComponents<RenderLightComponent>() || !point.HasAllComponents<RenderLightComponent>())return false;
            const auto& cam=camera.GetComponent<RenderCameraComponent>();
            const auto& sun=directional.GetComponent<RenderLightComponent>();
            const auto& lamp=point.GetComponent<RenderLightComponent>();
            if(key=="general-camera")return selected==camera && closeValue(glm::degrees(cam.verticalFovRadians),60.f) && !app.m_ContextEditor.cameraDirty;
            if(key=="general-camera-preview")return selected==camera && app.m_CameraView==RigidBodySimulationApp::CameraView::Preview &&
                app.m_PreviewCamera==scene.RenderData().Identify(camera).value_or(EntityRenderId{}) && app.m_CameraSnapshot &&
                closeValue(float(glm::degrees(app.m_CameraSnapshot->Description().verticalFovRadians)),60.f);
            if(key=="general-directional")return selected==directional && closeValue(sun.intensity,2.5f) && !app.m_ContextEditor.lightDirty;
            if(key=="general-point")return selected==point && closeValue(lamp.intensity,1.75f) && !app.m_ContextEditor.lightDirty;
            if(key.starts_with("general-room")) {
                const bool defaults=key=="general-room-defaults";
                if(!closeValue(glm::degrees(cam.verticalFovRadians),defaults?45.f:60.f) ||
                   !closeValue(sun.intensity,defaults?3.f:2.5f) || !closeValue(lamp.intensity,defaults?1.f:1.75f))return false;
                const auto room=SandboxEnvironment::Boxes();
                if(world->GetBodyCount()!=7 || world->GetJointCount()!=1 || !world->ReadJoint(app.m_AngularBindings.joint))return false;
                std::array<Body,5> roomHandles{};
                for(std::size_t i=0;i<room.size();++i) {
                    auto entity=scene.FindEntityByName(room[i].name);
                    if(!entity)return false;
                    auto binding=scene.GetBodyBinding(entity);
                    if(!binding)return false;
                    if(*binding==app.m_AngularBindings.bodies[0] || *binding==app.m_AngularBindings.bodies[1] ||
                       std::find(roomHandles.begin(),roomHandles.begin()+i,*binding)!=roomHandles.begin()+i)return false;
                    roomHandles[i]=*binding;
                    auto state=world->ReadEffectiveBody(*binding);
                    const float y=defaults?room[i].center[1]:i==0?-.1f:6.5f+.1f*float(i);
                    if(!state || !closeValue(state->position.y,y) || !closeValue(entity.Transform().Translation.y,y) ||
                       state->type!=BodyType::Static || state->shapeType!=ShapeType::Box ||
                       !closeValue(state->material.friction,!defaults && !i?.42f:.5f))return false;
                }
                const auto id=scene.RenderData().Identify(selected);
                return selected.GetName()=="Sandbox Wall +Z" && id && (key!="general-room-reset" ||
                    (app.m_SceneGeneration>editorResetBefore && *id!=generalOldSelection &&
                     !scene.RenderData().Resolve(generalOldSelection) && scene.GetPhysicsSystem()->GetCompletedSteps()==0));
            }
            auto mesh=scene.FindEntityByName("Template Plane");
            if(!mesh || selected!=mesh || !mesh.HasAllComponents<MeshRendererComponent>() || scene.GetBodyBinding(mesh))return false;
            auto material=app.m_SceneResources->DescribeMaterial(mesh.GetComponent<MeshRendererComponent>().material);
            auto render=scene.GetRenderTransform(mesh);
            if(!material || !render)return false;
            const auto& t=mesh.Transform();
            const auto id=scene.RenderData().Identify(mesh);
            if(!id || glm::length(Vec(render->matrix[3])-t.Translation)>1.e-4f)return false;
            if(key=="general-mesh-initial"){initialMeshRoughness=material->roughness;return true;}
            if(key=="general-translate")return app.m_TransformTool==RigidBodySimulationApp::TransformTool::Translate &&
                !app.TransformToolDisabled(app.m_TransformTool) && app.m_ShowGizmo;
            if(key=="general-mesh-defaults")return glm::length(t.Translation-Vec{-8,1,0})<1.e-4f &&
                t.Scale==Vec{1} && closeValue(material->roughness,initialMeshRoughness);
            if(!closeValue(material->roughness,.37f) || !closeValue(t.Scale.x,1.25f) || app.m_ContextEditor.materialDirty)return false;
            if(key=="general-mesh-reset")return app.m_SceneGeneration>editorResetBefore &&
                *id!=generalOldSelection && !scene.RenderData().Resolve(generalOldSelection) &&
                glm::length(t.Translation-meshAfterDrag)<1.e-4f && scene.GetPhysicsSystem()->GetCompletedSteps()==0;
            return key=="general-mesh" && closeValue(t.Translation.y,2.f);
        }
        // Qualification-only fragment for AngularJointChecks; no production mutation.
        // Requires Renderer/RenderVisibility.h. Frame resources are copied, never retained.
        struct ComponentViewProbe {
            unsigned frame{}; std::uint64_t generation{};
            ::GEngine::EntityRenderId selected{}, camera{};
            bool draw{}, cast{}, receive{}, pickable{}, main{}, picking{}, shadows{};
            bool axis{}, grid{}, material{}, cameraValid{}, light{};
            std::uint32_t layers{}, axisLayers{}, gridLayers{};
            std::array<float,4> color{}; std::array<float,2> uv{};
            float metallic{}, roughness{}, normal{}, ao{};
            ::GEngine::CameraSnapshotDesc cameraDesc;
            ::GEngine::Component::RenderLightComponent lightValue;
            glm::vec3 lightPosition{}, lightDirection{};
        };
        static inline ComponentViewProbe componentView;
        static void ObserveComponentFrame(RigidBodySimulationApp& app,const ::GEngine::RenderFrame& frame) noexcept {
            using namespace ::GEngine; using namespace ::GEngine::Component;
            if(!editorMode || !editorSession || !ComponentRole(editorSession) || !app.m_ActiveScene)return;
            ComponentViewProbe next{}; next.frame=editorFrame;next.generation=app.m_SceneGeneration;
            auto& scene=*app.m_ActiveScene;
            const auto identify=[&](const char* name) {auto e=scene.FindEntityByName(name);return e?scene.RenderData().Identify(e).value_or(EntityRenderId{}):EntityRenderId{};};
            if(app.m_HoveredEntity)next.selected=scene.RenderData().Identify(app.m_HoveredEntity).value_or(EntityRenderId{});
            const auto axis=identify("axis"),grid=identify("grid");
            const auto visibility=RenderVisibility::Build(frame);
            const auto contains=[](auto values,std::size_t i){return std::find(values.begin(),values.end(),i)!=values.end();};
            const auto draws=frame.Draws();
            for(std::size_t i=0;i<draws.size();++i) {
                const auto& d=draws[i];
                if(d.entity==axis){next.axis=true;next.axisLayers=d.layers;}
                if(d.entity==grid){next.grid=true;next.gridLayers=d.layers;}
                if(d.entity!=next.selected)continue;
                next.draw=true;next.cast=d.castShadows;next.receive=d.receiveShadows;next.pickable=d.pickable;next.layers=d.layers;
                if(visibility){next.main|=contains(visibility->Main(),i);next.picking|=contains(visibility->Picking(),i);next.shadows|=contains(visibility->Shadows(),i);}
                const auto& source=frame.Resources()[d.resources].Material().Source();
                const auto params=source->Declaration()->Parameters(); const auto values=source->Values();
                unsigned fields=0;
                for(std::size_t j=0;j<params.size();++j) {
                    const auto& name=params[j].declaration.name;
                    if(name=="baseColor"){if(auto v=std::get_if<std::array<float,4>>(&values[j])){next.color=*v;++fields;}}
                    else if(name=="u_tiling"){if(auto v=std::get_if<std::array<float,2>>(&values[j])){next.uv=*v;++fields;}}
                    else if(auto v=std::get_if<float>(&values[j])) {
                        if(name=="metallicFactor"){next.metallic=*v;++fields;}
                        else if(name=="roughnessScale"){next.roughness=*v;++fields;}
                        else if(name=="normalStrength"){next.normal=*v;++fields;}
                        else if(name=="aoStrength"){next.ao=*v;++fields;}
                    }
                }
                next.material=fields==6;
            }
            if(!frame.Cameras().empty()) {const auto& camera=frame.Cameras()[0];next.camera=camera.entity;
                if(auto snapshot=camera.Snapshot()){next.cameraDesc=snapshot->Description();next.cameraValid=camera.MatchesSnapshot();}}
            const auto light=[&](const auto& value,RenderLightKind kind) {
                if(value.entity!=next.selected)return;
                next.light=true;next.lightValue.kind=kind;next.lightValue.color={value.color.x,value.color.y,value.color.z};
                next.lightValue.intensity=value.intensity;next.lightValue.castShadows=value.shadows.castShadows;next.lightValue.spotShadow=value.shadows.spot;
                if constexpr(requires{value.position;})next.lightPosition=value.position;
                if constexpr(requires{value.direction;})next.lightDirection=value.direction;
                if constexpr(requires{value.range;})next.lightValue.range=value.range;
                if constexpr(requires{value.innerConeRadians;}){next.lightValue.innerConeRadians=value.innerConeRadians;next.lightValue.outerConeRadians=value.outerConeRadians;}
            };
            for(const auto& v:frame.DirectionalLights())light(v,RenderLightKind::Directional);
            for(const auto& v:frame.PointLights())light(v,RenderLightKind::Point);
            for(const auto& v:frame.SpotLights())light(v,RenderLightKind::Spot);
            componentView=next;
        }
        static void ComponentViewRecord(RigidBodySimulationApp& app,std::ostream& o) {
            const auto& p=componentView;
            const auto b=[&](const char* k,bool v){o<<",\""<<k<<"\":"<<(v?"true":"false");};
            const auto n=[&](const char* k,auto v){o<<",\""<<k<<"\":"<<v;};
            const auto a=[&](const char* k,const auto& v){o<<",\""<<k<<"\":[";for(std::size_t i=0;i<v.size();++i){if(i)o<<',';o<<v[i];}o<<']';};
            b("frame.fresh",p.generation==app.m_SceneGeneration && editorFrame>=p.frame && editorFrame-p.frame<=4);
            b("frame.draw",p.draw);b("frame.cast",p.cast);b("frame.receive",p.receive);b("frame.pickable",p.pickable);
            b("frame.main",p.main);b("frame.picking",p.picking);b("frame.shadows",p.shadows);n("frame.layers",p.layers);
            b("frame.axis",p.axis);n("frame.axis_layers",p.axisLayers);b("frame.grid",p.grid);n("frame.grid_layers",p.gridLayers);
            b("frame.material",p.material);a("frame.material.color",p.color);a("frame.material.uv",p.uv);
            n("frame.material.metallic",p.metallic);n("frame.material.roughness",p.roughness);n("frame.material.normal",p.normal);n("frame.material.ao",p.ao);
            b("frame.camera.valid",p.cameraValid);n("frame.camera.projection",int(p.cameraDesc.projection));n("frame.camera.fov",p.cameraDesc.verticalFovRadians);
            n("frame.camera.ortho",p.cameraDesc.orthographicHeight);n("frame.camera.near",p.cameraDesc.nearPlane);n("frame.camera.far",p.cameraDesc.farPlane);n("frame.camera.layers",p.cameraDesc.visibleLayers);
            b("frame.light",p.light);n("frame.light.kind",int(p.lightValue.kind));a("frame.light.color",p.lightValue.color);n("frame.light.intensity",p.lightValue.intensity);n("frame.light.range",p.lightValue.range);
            n("frame.light.inner",p.lightValue.innerConeRadians);n("frame.light.outer",p.lightValue.outerConeRadians);b("frame.light.cast",p.lightValue.castShadows);
            n("frame.light.shadow_resolution",p.lightValue.spotShadow.resolution);n("frame.light.bias",p.lightValue.spotShadow.bias);n("frame.light.pcf",p.lightValue.spotShadow.pcfRadius);
            o<<",\"frame.light.position\":["<<p.lightPosition.x<<','<<p.lightPosition.y<<','<<p.lightPosition.z<<']';
            o<<",\"frame.light.direction\":["<<p.lightDirection.x<<','<<p.lightDirection.y<<','<<p.lightDirection.z<<']';
            bool active=false,visible=false;
            if(app.m_ActiveScene && app.m_HoveredEntity)if(auto id=app.m_ActiveScene->RenderData().Identify(app.m_HoveredEntity)) {
                visible=app.EditorEntityVisible(*id);auto selectedCamera=app.m_ActiveScene->ActiveSceneCamera();active=selectedCamera && *selectedCamera && **selectedCamera==*id;
            }
            b("view.active_camera",active);b("view.editor_visible",visible);
            // PreviewBoundary is outside ImGui Begin/End; HasError is window-scoped.
            b("view.numeric_error",Rbs::Editor::Numeric::state.error!=nullptr);
            b("view.material_dirty",app.m_ContextEditor.materialDirty);b("view.camera_dirty",app.m_ContextEditor.cameraDirty);
            b("view.light_dirty",app.m_ContextEditor.lightDirty);b("view.mesh_dirty",app.m_ContextEditor.meshDirty);b("view.visibility_dirty",app.m_ContextEditor.visibilityDirty);
        }
        // These conditions wait for the actual frame to catch up. Exact expected scalar,
        // vector, component and renderer values are additionally frozen in the JSON files.
        static std::optional<bool> ComponentViewExpected(RigidBodySimulationApp& app,std::string_view key) {
            using namespace ::GEngine; using namespace ::GEngine::Component;
            if(!key.starts_with("component.surface-") && !key.starts_with("component.view-"))return std::nullopt;
            if(!app.m_ActiveScene || !app.m_HoveredEntity || app.m_SceneRequest || app.m_BackendRequest || app.m_ContextEditor.queued)return false;
            auto& selected=app.m_HoveredEntity;auto& scene=*app.m_ActiveScene;const auto id=scene.RenderData().Identify(selected);
            const auto& p=componentView;
            if(!id || p.selected!=*id || p.generation!=app.m_SceneGeneration || editorFrame<p.frame || editorFrame-p.frame>4)return false;
            const auto close=[](double a,double b){return std::abs(a-b)<1.e-4;};
            if(key.find("invalid")!=std::string_view::npos && !Rbs::Editor::Numeric::state.error)return false;
            if(key.starts_with("component.surface-")) {
                if(!selected.HasAllComponents<MeshRendererComponent>() || !p.draw)return false;
                const auto& mesh=selected.GetComponent<MeshRendererComponent>();
                if(mesh.receiveShadows!=p.receive || mesh.pickable!=p.pickable || (!mesh.castShadows && p.cast))return false;
                if(!mesh.pickable && p.picking)return false;if(!mesh.castShadows && p.shadows)return false;
                auto material=app.m_SceneResources->DescribeMaterial(mesh.material);
                if(!material || !p.material || !close(material->metallic,p.metallic) || !close(material->roughness,p.roughness) ||
                   !close(material->normalStrength,p.normal) || !close(material->aoStrength,p.ao) || material->baseColor!=p.color || material->uvTiling!=p.uv)return false;
            }
            if(selected.HasAllComponents<RenderLightComponent>()) {
                const auto& light=selected.GetComponent<RenderLightComponent>();
                if(!p.light || light.kind!=p.lightValue.kind || light.color!=p.lightValue.color || !close(light.intensity,p.lightValue.intensity) || light.castShadows!=p.lightValue.castShadows)return false;
                if(light.kind!=RenderLightKind::Directional && !close(light.range,p.lightValue.range))return false;
                if(light.kind==RenderLightKind::Spot && (!close(light.innerConeRadians,p.lightValue.innerConeRadians) || !close(light.outerConeRadians,p.lightValue.outerConeRadians) || light.spotShadow!=p.lightValue.spotShadow))return false;
            }
            if(key.find("preview")!=std::string_view::npos) {
                if(!selected.HasAllComponents<RenderCameraComponent>() || app.m_CameraView!=RigidBodySimulationApp::CameraView::Preview || p.camera!=*id || !p.cameraValid)return false;
                const auto& c=selected.GetComponent<RenderCameraComponent>();const auto& d=p.cameraDesc;
                if(c.projection!=d.projection || !close(c.verticalFovRadians,d.verticalFovRadians) || !close(c.orthographicHeight,d.orthographicHeight) ||
                   !close(c.nearPlane,d.nearPlane) || !close(c.farPlane,d.farPlane) || c.visibleLayers!=d.visibleLayers)return false;
            }
            if(key.find("reset")!=std::string_view::npos && (app.m_SceneGeneration<=editorResetBefore || scene.GetPhysicsSystem()->GetCompletedSteps()!=0 || scene.RenderData().Resolve(generalOldSelection)))return false;
            return true;
        }

        // Integrate inside Rbs::AngularJointChecks. Observer only; never mutates Scene,
        // body, collider or draft state. Numeric expected values are frozen separately.
        static std::optional<bool> ComponentBodyExpected(RigidBodySimulationApp& app,std::string_view key) {
            const bool room=key=="component.room-committed" || key=="component.room-reset";
            const bool reset=key=="component.body-reset-lifetime" || key=="component.room-reset";
            if(!room && !reset)return std::nullopt;
            using namespace ::GEngine;
            using namespace ::GEngine::Component;
            auto& scene=*app.m_ActiveScene;
            auto* physics=scene.GetPhysicsSystem();
            auto* world=physics?physics->GetPhysicsWorld():nullptr;
            auto selected=app.m_HoveredEntity;
            if(app.m_SceneRequest || app.m_BackendRequest || !world || world->GetBackend()!=app.m_Backend ||
               !selected || selected.GetSceneContext()!=&scene || app.m_SimulationState!=SimulationState::Ready)return false;
            const auto id=scene.RenderData().Identify(selected);
            if(!id)return false;
            if(reset && (app.m_SceneGeneration<=editorResetBefore || *id==generalOldSelection ||
               scene.RenderData().Resolve(generalOldSelection) || physics->GetCompletedSteps()!=0))return false;
            if(!room)return selected.GetName()=="wood_sphere_0";
            if(selected.GetName()!="Sandbox Wall +Z" || world->GetBodyCount()!=22 || world->GetJointCount()!=0)return false;
            constexpr float tolerance=1.e-4f;
            const auto close=[](float a,float b){return std::abs(a-b)<tolerance;};
            std::array<Body,5> handles{};
            const auto boxes=SandboxEnvironment::Boxes();
            for(std::size_t i=0;i<boxes.size();++i) {
                auto entity=scene.FindEntityByName(boxes[i].name);
                if(!entity || !entity.HasAllComponents<Transform3DComponent,MeshRendererComponent>())return false;
                const auto binding=scene.GetBodyBinding(entity);
                if(!binding || std::find(handles.begin(),handles.begin()+i,*binding)!=handles.begin()+i)return false;
                handles[i]=*binding;
                const auto state=world->ReadEffectiveBody(*binding);
                const auto properties=world->ReadBodyProperties(*binding);
                const auto shape=physics->ReadBodyCollider(*binding);
                const auto transform=scene.GetRenderTransform(entity);
                const Vec position{boxes[i].center[0],i==0?-.1f:6.5f+.1f*float(i),boxes[i].center[2]};
                const Vec scale=i==0?Vec{159,1,160}:Vec{boxes[i].size[0],boxes[i].size[1],boxes[i].size[2]};
                const Quat orientation=i==0?glm::quat(glm::radians(Vec{0,90,0})):Quat{1,0,0,0};
                if(!state || !properties || !shape || !transform || shape->type!=ShapeType::Box ||
                   state->type!=BodyType::Static || !close(state->inverseMass,0) ||
                   glm::length(state->position-position)>tolerance ||
                   glm::length(entity.Transform().Translation-position)>tolerance ||
                   glm::length(Vec(transform->matrix[3])-position)>tolerance ||
                   glm::length(entity.Transform().Scale-scale)>tolerance ||
                   !close(std::abs(glm::dot(state->orientation,orientation)),1) ||
                   !close(properties->material.friction,i==0?.42f:.5f) ||
                   !close(properties->material.restitution,i==0?.2f:.5f))return false;
                const auto* box=std::get_if<BoxShapeDesc>(&shape->dimensions);
                const Vec half=i==0?Vec{79.4f,.4f,79.9f}:.5f*scale;
                const Vec center=i==0?Vec{.05f,0,0}:Vec{0};
                if(!box || glm::length(box->halfExtents-half)>tolerance || glm::length(box->center-center)>tolerance)return false;
            }
            return true;
        }
        static bool EditorExpected(RigidBodySimulationApp& app,std::string_view key) {
            using P=RbsScenePreset;using S=SimulationState;
            auto& session=app.m_Angular;
            if(auto result=OutlineExpected(app,key))return *result;
            if(auto result=ComponentViewExpected(app,key))return *result;
            if(auto result=ComponentBodyExpected(app,key))return *result;
            if(key.starts_with("component.readback.")) {
                const auto& entity=app.m_HoveredEntity;
                return !app.m_SceneRequest && !app.m_BackendRequest && !app.m_ContextEditor.queued &&
                    !app.m_BodyPropertyEditor.queued && !app.m_TransformEdit && !app.m_VelocityEdit &&
                    entity && entity.GetSceneContext()==app.m_ActiveScene.get() &&
                    app.m_ActiveScene->RenderData().Identify(entity).has_value();
            }
            if(key.starts_with("general-"))return GeneralExpected(app,key);
            const bool loaded=app.m_SceneGeneration>editorResetBefore && !app.m_SceneRequest;
            if(key=="scale-staged") {
                if(!session.draft || !session.committed || !session.dirty || !EditorSelectedReadback(app,"selected-bar"))return false;
                const auto live=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->ReadEffectiveBody(app.m_AngularBindings.bodies[1]);
                return live && std::abs(session.draft->bodies[1].halfExtents.x-.17f)<1.e-5f &&
                    std::abs((live->localBounds.maxs.x-live->localBounds.mins.x)*.5f-.15f)<1.e-4f;
            }
            if(key=="draft-isolated" || key=="body-properties-applied" || key=="portable-properties")
                return EditorAuthoredBodyReadback(app,key=="draft-isolated",key=="portable-properties");
            if(key=="sphere-reference-applied") {
                auto& scene=*app.m_ActiveScene;
                if(app.m_ActiveSelection.scene!=P::SphereBoxStack || !app.m_HoveredEntity ||
                   app.m_HoveredEntity.GetSceneContext()!=&scene || app.m_HoveredEntity.GetName()!="wood_sphere_0")return false;
                const auto handle=scene.GetBodyBinding(app.m_HoveredEntity);
                if(!handle)return false;
                const auto body=scene.GetPhysicsSystem()->GetPhysicsWorld()->ReadEffectiveBody(*handle);
                return body && body->shapeType==::GEngine::ShapeType::Sphere &&
                    std::abs(body->position.x-4.f)<1.e-4f &&
                    glm::length(body->position-app.m_HoveredEntity.Transform().Translation)<1.e-4f;
            }
            if(key=="direct-editable") {
                if(!EditorSelectedReadback(app,"selected-bar") ||
                   !session.committed || !session.draft || session.dirty)return false;
                const auto reference=ReferenceAngularPacket(session.committed->scene,session.committed->variant);
                return reference && SameAngularPacket(*session.committed,*reference) &&
                    SameAngularPacket(*session.draft,*reference);
            }
            if(key=="defaults-staged") {
                if(!session.committed || !session.draft || !session.dirty)return false;
                const auto reference=ReferenceAngularPacket(session.committed->scene,session.committed->variant);
                return reference && SameAngularPacket(*session.draft,*reference) &&
                    !SameAngularPacket(*session.committed,*reference) &&
                    bool(app.VerifyAngularCandidate(*session.committed));
            }
            if(key=="dirty-switch-protected") {
                if(!session.dirty || !session.committed || !session.draft || app.m_SceneRequest)return false;
                auto& scene=*app.m_ActiveScene;
                const auto before=*session.draft;
                const auto other=app.m_Backend==::GEngine::PhysicsBackend::Jolt?
                    ::GEngine::PhysicsBackend::Native: ::GEngine::PhysicsBackend::Jolt;
                return !app.RequestScene({P::SphereBoxStack,8}) && !app.RequestBackend(other) &&
                    !app.m_SceneRequest && !app.m_BackendRequest && app.m_ActiveScene.get()==&scene &&
                    session.dirty && SameAngularPacket(*session.draft,before);
            }
            if(key=="hinge-basic")return loaded && app.m_ActiveSelection.scene==P::HingeBasic &&
                app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()==0;
            if(key=="hinge-quaternion")return loaded && app.m_ActiveSelection.scene==P::HingeQuaternion;
            if(key=="hinge-limited")return loaded && app.m_ActiveSelection.scene==P::HingeLimited;
            if(key=="cv-free")return loaded && app.m_ActiveSelection.scene==P::CvFree;
            if(key=="cv-limited")return loaded && app.m_ActiveSelection.scene==P::CvLimited;
            if(key=="geometry-gallery" || key=="box-stack" || key=="sphere-box-stack" ||
               key=="sphere-lattice" || key=="distance-chain" || key=="ball-socket-p")
                return loaded && key==SceneName(app.m_ActiveSelection.scene) &&
                    app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()==0 &&
                    EditorRoomReadback(app);
            if(key.starts_with("selected-"))return EditorSelectedReadback(app,key);
            if(key=="simulation-mode")return app.m_EditorMode==RigidBodySimulationApp::EditorMode::Simulation;
            if(key=="edit-mode")return app.m_EditorMode==RigidBodySimulationApp::EditorMode::Edit;
            if(key=="thumbnails-loaded")return app.m_PresetThumbnailsLoaded &&
                app.m_PresetThumbnails.count(P::HingeBasic) &&
                app.m_PresetThumbnails.count(P::HingeQuaternion) &&
                app.m_PresetThumbnails.count(P::HingeLimited) &&
                app.m_PresetThumbnails.count(P::CvFree) &&
                app.m_PresetThumbnails.count(P::CvLimited);
            if(key=="enhanced")return session.committed && !session.dirty;
            if(key=="applied-current") {
                if(!session.committed || session.dirty)return false;
                auto* world=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
                auto barState=world->ReadBody(app.m_AngularBindings.bodies[1]);
                return barState && std::abs(session.committed->bodies[1].authoredMass-1.25f)<1.e-3f &&
                    std::abs(barState->inverseMass-.8f)<1.e-3f &&
                    bool(app.VerifyAngularCandidate(*session.committed));
            }
            if(key=="applied-cv" || key=="applied-hinge") {
                if(!session.committed || !session.draft || session.dirty)return false;
                const auto& p=*session.committed;
                if(std::abs(p.bodies[1].authoredMass-1.25f)>1.e-3f ||
                   std::abs(p.bodies[1].halfExtents.y-.9f)>1.e-3f ||
                   std::abs(p.gravity.y+8.f)>1.e-3f)return false;
                if(key=="applied-cv" ?
                   (std::abs(glm::degrees(p.joint.halfAngleU)-30)>1.e-2f ||
                    std::abs(glm::degrees(p.joint.halfAngleV)-25)>1.e-2f):
                   (std::abs(glm::degrees(p.joint.lower)+30)>1.e-2f ||
                    std::abs(glm::degrees(p.joint.upper)-30)>1.e-2f))return false;
                auto* w=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
                auto j=w->ReadJoint(app.m_AngularBindings.joint);
                auto b=w->ReadBody(app.m_AngularBindings.bodies[1]);
                return j && b && std::abs(b->inverseMass-.8f)<1.e-3f &&
                    std::abs(w->GetGravity().y+8.f)<1.e-3f &&
                    (key=="applied-cv"?std::abs(glm::degrees(j->descriptor.angular.halfAngleU)-30)<1.e-2f:
                                        std::abs(glm::degrees(j->descriptor.angular.lower)+30)<1.e-2f);
            }
            if(key=="reverted")return session.committed && session.draft && !session.dirty &&
                std::abs(session.draft->bodies[1].authoredMass-session.committed->bodies[1].authoredMass)<1.e-6f;
            if(key=="invalid-mass")return editorInvalidSeen && session.committed && session.draft &&
                std::abs(session.committed->bodies[1].authoredMass-1.25f)<1.e-6f &&
                std::abs(session.draft->bodies[1].authoredMass-1.5f)<1.e-6f;
            if(key=="defaults") {
                if(!session.committed || session.dirty ||
                   app.m_ActiveSelection.scene!=session.committed->scene)return false;
                const auto reference=ReferenceAngularPacket(session.committed->scene,
                    session.committed->variant);
                if(!reference)return false;
                const auto& p=*session.committed;const auto& r=*reference;
                const auto approximately=[](float a,float b){return std::abs(a-b)<1.e-5f;};
                if(!approximately(p.bodies[1].authoredMass,r.bodies[1].authoredMass) ||
                   !approximately(p.bodies[1].halfExtents.y,r.bodies[1].halfExtents.y) ||
                   !approximately(p.gravity.y,r.gravity.y) ||
                   !approximately(p.joint.lower,r.joint.lower) || !approximately(p.joint.upper,r.joint.upper) ||
                   !approximately(p.joint.halfAngleU,r.joint.halfAngleU) ||
                   !approximately(p.joint.halfAngleV,r.joint.halfAngleV) ||
                   p.joint.limitsEnabled!=r.joint.limitsEnabled)return false;
                auto* w=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
                auto j=w->ReadJoint(app.m_AngularBindings.joint);
                auto b=w->ReadBody(app.m_AngularBindings.bodies[1]);
                if(!j || !b || !approximately(b->inverseMass,1.f/r.bodies[1].authoredMass) ||
                   !approximately(w->GetGravity().y,r.gravity.y) ||
                   j->descriptor.angular.limitsEnabled!=r.joint.limitsEnabled)return false;
                const bool hinge=app.m_ActiveSelection.scene==P::HingeBasic ||
                    app.m_ActiveSelection.scene==P::HingeQuaternion ||
                    app.m_ActiveSelection.scene==P::HingeLimited;
                return hinge?
                    approximately(j->descriptor.angular.lower,r.joint.lower) &&
                    approximately(j->descriptor.angular.upper,r.joint.upper):
                    approximately(j->descriptor.angular.halfAngleU,r.joint.halfAngleU) &&
                    approximately(j->descriptor.angular.halfAngleV,r.joint.halfAngleV);
            }
            if(key=="reset-retained")return EditorResetReadback(app);
            if(key=="reset-reference")return EditorResetReadback(app);
            if(key=="reset")return EditorResetReadback(app) &&
                EditorExpected(app,"defaults");
            if(key=="running")return app.m_SimulationState==S::Running &&
                app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()>0;
            if(key=="paused")return app.m_SimulationState==S::Paused;
            if(key=="resumed")return app.m_SimulationState==S::Running;
            if(key=="impulse")return app.m_ChainImpulse.AppliedCount()>0;
            if(key=="jolt")return app.m_Backend==::GEngine::PhysicsBackend::Jolt &&
                app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->GetBackend()==::GEngine::PhysicsBackend::Jolt;
            if(key=="native")return app.m_Backend==::GEngine::PhysicsBackend::Native &&
                app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->GetBackend()==::GEngine::PhysicsBackend::Native;
            if(key=="failed-switch")return !app.m_SceneRequest &&
                app.m_SimulationState==S::Paused &&
                app.m_Backend==::GEngine::PhysicsBackend::Jolt &&
                app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld()->GetBackend()==::GEngine::PhysicsBackend::Jolt &&
                std::string_view(SavedBackendKey())=="jolt" &&
                app.m_SceneDiagnostic.find("previous scene/backend retained")!=std::string::npos &&
                EditorFailedSwitchReadback(app);
            return false;
        }
        static void ComponentReadback(RigidBodySimulationApp& app,const char* key) {
            if(!ComponentRole(editorSession))return;
            using namespace ::GEngine;using namespace ::GEngine::Component;
            std::ofstream o(output/"component-readback.jsonl",std::ios::app);
            o<<std::boolalpha<<std::setprecision(9)<<"{\"key\":"<<std::quoted(key)<<",\"passed\":true,\"values\":{\"scene\":"
             <<std::quoted(SceneName(app.m_ActiveSelection.scene))<<",\"backend\":"<<std::quoted(BackendKey(app.m_Backend));
            const auto n=[&](const char* name,auto value){o<<','<<std::quoted(name)<<':'<<value;};
            const auto v=[&](const char* name,const auto& value){o<<','<<std::quoted(name)<<":["<<value[0]<<','<<value[1]<<','<<value[2]<<']';};
            const auto q=[&](const char* name,const auto& value){o<<','<<std::quoted(name)<<":["<<value.w<<','<<value.x<<','<<value.y<<','<<value.z<<']';};
            auto entity=app.m_HoveredEntity;
            n("generation",app.m_SceneGeneration);n("selection_count",app.m_EditorSelection.count);
            n("pending.any",app.HasPendingObjectProperties());n("pending.angular",app.m_Angular.dirty);
            n("pending.camera",app.m_ContextEditor.cameraDirty);n("pending.light",app.m_ContextEditor.lightDirty);
            n("pending.material",app.m_ContextEditor.materialDirty);n("pending.mesh",app.m_ContextEditor.meshDirty);n("pending.visibility",app.m_ContextEditor.visibilityDirty);
            n("numeric.error",Rbs::Editor::Numeric::state.error!=nullptr);
            n("tick",app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps());
            o<<",\"selected\":"<<std::quoted(entity?entity.GetName():std::string{});
            if(entity && entity.GetSceneContext()==app.m_ActiveScene.get()) {
                n("has.visibility",entity.HasAllComponents<VisibilityComponent>());
                n("has.rigidbody",bool(app.m_ActiveScene->GetBodyBinding(entity)));
                n("has.camera",entity.HasAllComponents<RenderCameraComponent>());
                n("has.light",entity.HasAllComponents<RenderLightComponent>());
                if(auto id=app.m_ActiveScene->RenderData().Identify(entity)){n("identity.registry",id->registry);n("identity.index",id->index);n("identity.generation",id->generation);}
                if(entity.HasAllComponents<Transform3DComponent>()) {
                    const auto& t=entity.Transform();v("local.position",t.Translation);n("local.position.y",t.Translation.y);v("local.scale",t.Scale);q("local.rotation",t.QuatRotation);
                    v("local.euler",glm::degrees(glm::eulerAngles(t.QuatRotation)));
                }
                if(entity.HasAllComponents<MeshRendererComponent>()) {
                    const auto& m=entity.GetComponent<MeshRendererComponent>();n("mesh.cast",m.castShadows);n("mesh.receive",m.receiveShadows);n("mesh.pickable",m.pickable);
                    n("mesh.handle",m.mesh.index);n("mesh.submesh",m.submesh);
                    if(auto d=app.m_SceneResources->DescribeMaterial(m.material)) {
                        v("material.base_color",d->baseColor);n("material.metallic",d->metallic);n("material.roughness",d->roughness);
                        n("material.normal",d->normalStrength);n("material.ao",d->aoStrength);
                        o<<",\"material.uv\":["<<d->uvTiling[0]<<','<<d->uvTiling[1]<<']';
                    }
                }
                if(entity.HasAllComponents<VisibilityComponent>()) {const auto& c=entity.GetComponent<VisibilityComponent>();n("visibility.enabled",c.enabled);n("visibility.layers",c.layers);}
                if(entity.HasAllComponents<RenderCameraComponent>()) {
                    const auto& c=entity.GetComponent<RenderCameraComponent>();n("camera.projection",int(c.projection));n("camera.fov",c.verticalFovRadians);
                    n("camera.ortho",c.orthographicHeight);n("camera.near",c.nearPlane);n("camera.far",c.farPlane);n("camera.layers",c.visibleLayers);
                }
                if(entity.HasAllComponents<RenderLightComponent>()) {
                    const auto& c=entity.GetComponent<RenderLightComponent>();n("light.kind",int(c.kind));v("light.color",c.color);n("light.intensity",c.intensity);
                    n("light.range",c.range);n("light.inner",c.innerConeRadians);n("light.outer",c.outerConeRadians);n("light.cast",c.castShadows);
                    n("light.shadow_resolution",c.spotShadow.resolution);n("light.shadow_bias",c.spotShadow.bias);n("light.shadow_pcf",c.spotShadow.pcfRadius);
                }
                auto* world=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
                n("has_physics",bool(app.m_ActiveScene->GetBodyBinding(entity)));
                if(auto h=app.m_ActiveScene->GetBodyBinding(entity)) {
                    if(auto collider=app.m_ActiveScene->GetPhysicsSystem()->ReadBodyCollider(*h)) {
                        const char* kind=collider->type==ShapeType::Sphere?"Sphere":collider->type==ShapeType::Box?"Box":collider->type==ShapeType::Capsule?"Capsule":"Convex";
                        o<<",\"collider.kind\":"<<std::quoted(kind);
                        if(auto sphere=std::get_if<SphereShapeDesc>(&collider->dimensions))n("collider.radius",sphere->radius);
                        if(auto capsule=std::get_if<CapsuleShapeDesc>(&collider->dimensions)){n("collider.radius",capsule->radius);n("collider.half_length",capsule->halfLength);}
                    }
                    if(auto body=world->ReadEffectiveBody(*h)) {
                        n("provider.type",int(body->type));n("provider.shape",int(body->shapeType));n("provider.inverse_mass",body->inverseMass);
                        v("provider.position",body->position);n("provider.position.y",body->position.y);q("provider.orientation",body->orientation);v("provider.linear_velocity",body->linearVelocity);v("provider.angular_velocity",body->angularVelocity);
                        v("provider.half_extents",.5f*(body->localBounds.maxs-body->localBounds.mins));v("provider.bounds_center",.5f*(body->localBounds.maxs+body->localBounds.mins));
                        v("provider.com",body->centerOfMass);v("provider.inverse_inertia_body",Vec{body->inverseInertiaBody[0][0],body->inverseInertiaBody[1][1],body->inverseInertiaBody[2][2]});
                        v("provider.inverse_inertia_world",Vec{body->inverseInertiaWorld[0][0],body->inverseInertiaWorld[1][1],body->inverseInertiaWorld[2][2]});
                        n("provider.friction",body->material.friction);n("provider.restitution",body->material.restitution);
                        n("provider.spin_resistance",body->material.spinResistanceLength);n("provider.rolling_resistance",body->material.rollingResistanceLength);
                    }
                    if(auto p=world->ReadBodyProperties(*h)) {
                        n("provider.mass",p->mass);n("provider.linear_damping",p->linearDamping);n("provider.angular_damping",p->angularDamping);n("provider.gravity",p->gravityFactor);
                        n("provider.ccd",p->continuousCollision);n("provider.sleep",p->allowSleeping);n("provider.velocity_steps",p->velocityStepsOverride);n("provider.position_steps",p->positionStepsOverride);
                        n("provider.sleep_linear",p->sleep.linearSpeedThreshold);n("provider.sleep_angular",p->sleep.angularSpeedThreshold);n("provider.sleep_dwell",p->sleep.inactivitySeconds);
                    }
                    for(std::size_t i=0;i<world->GetJointCount();++i)if(auto jh=world->JointAt(i))if(auto j=world->ReadJoint(*jh))
                        if(j->descriptor.bodyA==*h || j->descriptor.bodyB==*h) {
                            n("joint.kind",int(j->descriptor.kind));n("joint.error",j->error);v("joint.anchor_a",j->worldAnchorA);v("joint.anchor_b",j->worldAnchorB);
                            v("joint.local_anchor_a",j->descriptor.localAnchorA);v("joint.local_anchor_b",j->descriptor.localAnchorB);
                            const auto& a=j->descriptor.angular;q("joint.frame_a",a.localFrameA);q("joint.frame_b",a.localFrameB);
                            n("joint.lower",a.lower);n("joint.upper",a.upper);n("joint.u",a.halfAngleU);n("joint.v",a.halfAngleV);n("joint.limits",a.limitsEnabled);break;
                        }
                }
            }
            ComponentViewRecord(app,o);
            OutlineRecord(app,o);
            o<<"}}\n";Check(o.good(),"component readback evidence write");
        }
        static void FinishEditor(RigidBodySimulationApp& app,bool complete) {
            if(!editorMode)return;
            PerformanceFlush();
            std::ofstream o(output/"result.json");
            o<<"{\"complete\":"<<(complete?"true":"false")<<",\"physics_ticks\":"<<editorTicks
             <<",\"images\":"<<editorImages<<",\"room_images\":"<<editorRoomImages
             <<",\"actions\":"<<editorAction
             <<",\"editor_session\":\""<<(editorSession?editorSession:"")<<"\""
             <<",\"saved_backend_at_end\":\""<<SavedBackendKey()<<"\""
             <<",\"target_sizes_runtime_address\":"<<reinterpret_cast<std::uintptr_t>(g_Phase27TargetSizes)
             <<",\"failure\":\""<<(failure?failure:"")<<"\"}\n";
            editorEvents.close();editorMode=false;app.ShutDown();
        }
        static bool BeforeEditorSceneUpdate(RigidBodySimulationApp& app,::GEngine::Timestep simulationTime) {
            if(!editorMode)return true;
            auto& scene=*app.m_ActiveScene;
            const double elapsed=simulationTime.GetSecondsPrecise();
            if(scene.IsPaused() || !scene.GetPhysicsSystem()->GetPhysicsWorld() ||
                !std::isfinite(elapsed) || elapsed<=0)return true;
            double pending=scene.GetPhysicsTiming().pendingSeconds;
            pending+=std::min(elapsed,::GEngine::_Scene::MaxPendingPhysicsSeconds-pending);
            constexpr double roundoff=8.0*std::numeric_limits<double>::epsilon()*
                ::GEngine::_Scene::MaxPendingPhysicsSeconds;
            const double step=scene.GetPhysicsStep().count();
            unsigned projected=0;
            while(projected<::GEngine::_Scene::MaxPhysicsStepsPerUpdate && pending+roundoff>=step) {
                pending=std::max(0.0,pending-step);
                ++projected;
            }
            if(editorTicks+projected<=600)return true;
            Check(false,"Editor tick cap: next Scene Update would exceed 600 before required interactions complete");
            EditorLog("pre-step-budget-stop","actual-plus-projected-over-600");
            FinishEditor(app,false);
            return false;
        }
        static void AfterEditorSceneUpdate(RigidBodySimulationApp& app) {
            if(!editorMode)return;
            Check(app.m_SceneGeneration==editorGeneration,"Editor Scene generation changed inside Scene Update");
            const auto steps=unsigned(app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps());
            Check(steps>=editorLastStep,"Editor completed-step counter regressed");
            if(failed)return;
            editorTicks+=steps-editorLastStep;
            editorLastStep=steps;
            Check(editorTicks<=600,"Editor aggregate fixed tick cap");
        }
        static void PreviewBoundary(RigidBodySimulationApp& app) {
            if(!editorMode)return;
            ++editorFrame;++editorAge;
            auto* p=app.m_ActiveScene->GetPhysicsSystem();
            const auto steps=unsigned(p->GetCompletedSteps());
            if(app.m_SceneGeneration!=editorGeneration) {editorGeneration=app.m_SceneGeneration;editorLastStep=steps;}
            else if(steps>=editorLastStep){editorTicks+=steps-editorLastStep;editorLastStep=steps;}
            Check(editorTicks<=600 && editorFrame<=1800,"bounded Editor physical ticks/frames");
            Check(!p->HasJointFailure()&&!p->GetBackendFailure(),"Editor finite provider state");
            if(failed || editorAction>=editorActions.size()){FinishEditor(app,false);return;}
            const auto& action=editorActions[editorAction];
            if(editorAge>(action.op==EditorOp::Performance?(std::string_view(action.key?action.key:"")=="camera-sequence"?1510u:300u):50u)) {
                std::ofstream trace(output/"editor-timeout.txt");
                trace<<std::setprecision(9)<<"action="<<editorAction<<" key="<<(action.key?action.key:"")
                     <<" phase="<<editorPhase<<" frame="<<editorFrame<<" clicked="<<editorClicked
                     <<" input="<<(action.value?action.value:"")<<" ticks="<<editorTicks<<"\n";
                trace<<"scene="<<SceneName(app.m_ActiveSelection.scene)<<" backend="<<BackendKey(app.m_Backend)
                     <<" generation="<<app.m_SceneGeneration<<" simulation="<<int(app.m_SimulationState)
                     <<" editor_mode="<<int(app.m_EditorMode)<<" selected_count="<<app.m_EditorSelection.count
                     <<" queued_transform="<<bool(app.m_TransformEdit)<<"\n";
                trace<<"property="<<app.m_PropertyDiagnostic<<"\nscene_status="<<app.m_SceneDiagnostic
                     <<"\nangular_status="<<app.m_Angular.diagnostic<<"\n";
                const auto& numeric=Rbs::Editor::Numeric::state;
                trace<<"numeric_id="<<numeric.editing<<" text="<<numeric.text.data()
                     <<" error="<<(numeric.error?numeric.error:"")<<"\n";
                for(const char* key:{action.key,editorAction?editorActions[editorAction-1].key:nullptr}) {
                    if(!key)continue;
                    const int index=WidgetIndex(key);if(index<0)continue;
                    const auto& w=editorWidgets[index];
                    trace<<"widget="<<key<<" id="<<w.id<<" frame="<<w.frame<<" visible="<<w.visible
                         <<" rect="<<w.lo.x<<","<<w.lo.y<<".."<<w.hi.x<<","<<w.hi.y<<"\n";
                }
                const auto vec=[&](const char* name,const Vec& v){trace<<name<<"="<<v.x<<","<<v.y<<","<<v.z<<"\n";};
                const auto quat=[&](const char* name,const Quat& q){trace<<name<<"="<<q.w<<","<<q.x<<","<<q.y<<","<<q.z<<"\n";};
                auto* world=p->GetPhysicsWorld();
                if(world && app.m_HoveredEntity.GetSceneContext()==app.m_ActiveScene.get() && app.m_HoveredEntity) {
                    auto selected=app.m_HoveredEntity;
                    trace<<"selected="<<selected.GetName()<<"\n";
                    if(selected.HasAllComponents<::GEngine::Component::Transform3DComponent>()) {
                        vec("render_position",selected.Transform().Translation);vec("render_scale",selected.Transform().Scale);
                        quat("render_rotation",selected.Transform().QuatRotation);
                    }
                    if(auto h=app.m_ActiveScene->GetBodyBinding(selected)) {
                        trace<<"selected_handle="<<h->index<<"/"<<h->generation<<"\n";
                        if(auto facade=world->ReadBody(*h))vec("facade_position",facade->position);
                        if(auto effective=world->ReadEffectiveBody(*h)) {
                            vec("provider_position",effective->position);quat("provider_rotation",effective->orientation);
                            vec("provider_half_extents",.5f*(effective->localBounds.maxs-effective->localBounds.mins));
                            vec("provider_inverse_inertia",{effective->inverseInertiaBody[0][0],effective->inverseInertiaBody[1][1],effective->inverseInertiaBody[2][2]});
                            trace<<"provider_inverse_mass="<<effective->inverseMass<<" material="
                                 <<effective->material.friction<<","<<effective->material.restitution<<"\n";
                        } else trace<<"provider_error="<<effective.error().operation<<"\n";
                    }
                }
                const auto dump=[&](const char* name,const std::optional<AngularPacket>& packet) {
                    if(!packet)return;
                    trace<<name<<"\n";
                    for(unsigned i=0;i<2;++i) {
                        const auto& b=packet->bodies[i];
                        trace<<"endpoint="<<i<<" sphere="<<b.sphere<<" kinematic="<<b.kinematic
                             <<" mass="<<b.authoredMass<<" friction="<<b.friction<<" restitution="<<b.restitution
                             <<" damping="<<b.linearDamping<<","<<b.angularDamping<<"\n";
                        vec("position",b.center);quat("rotation",b.orientation);vec("half_extents",b.halfExtents);
                    }
                    quat("frame_a",packet->joint.localFrameA);quat("frame_b",packet->joint.localFrameB);
                };
                trace<<"dirty="<<app.m_Angular.dirty<<"\n";
                dump("committed",app.m_Angular.committed);dump("draft",app.m_Angular.draft);
                Check(false,"Editor widget/action timed out");FinishEditor(app,false);return;
            }
            auto& io=ImGui::GetIO();
            if(action.op==EditorOp::PilotLayout) {
                const std::string_view key=action.key;
                if(key=="begin-a" || key=="begin-b") {
                    if(editorAge==1)Check(app.BeginCameraPilot(app.m_SceneCameras[key=="begin-b"?1:0]),"Pilot layout uses actual Scene Camera session");
                    if(editorAge>=8){Check(bool(app.m_CameraPilot),"Pilot active before layout probe");NextEditorAction();}
                } else if(key=="matrix") {
                    if(pilotLayoutCases==32){Check(pilotOverlayFrames>0,"actual Viewport overlay observed");EditorLog("pilot-layout-matrix-pass",action.key);NextEditorAction();}
                } else if(key=="ended") {
                    Check(!app.m_CameraPilot && app.m_CameraView==RigidBodySimulationApp::CameraView::Editor,"Pilot action restores independent Editor view");
                    EditorLog("pilot-action-ended",action.key);NextEditorAction();
                } else Check(false,"known frozen Pilot layout action");
                return;
            }
            if(OutlineAction(app,action))return;
            if(action.op==EditorOp::Expect) {
                if(EditorExpected(app,action.key)) {
                    EditorLog("observed",action.key);
                    ComponentReadback(app,action.key);
                    const std::string_view key(action.key);
                    if(key=="reset-retained" || key=="reset-reference" || key=="reset")
                        EditorLog("reset-provider-readback",SceneName(app.m_ActiveSelection.scene));
                    else if(key=="applied-current" || key=="applied-hinge" || key=="applied-cv")
                        EditorLog("enhanced-provider-readback",SceneName(app.m_ActiveSelection.scene));
                    else if(key.starts_with("selected-"))
                        EditorLog("selected-provider-readback",action.key);
                    else if(key=="geometry-gallery" || key=="box-stack" || key=="sphere-box-stack" ||
                            key=="sphere-lattice" || key=="distance-chain" || key=="ball-socket-p")
                        EditorLog("shared-room-readback",SceneName(app.m_ActiveSelection.scene));
                    NextEditorAction();
                }
                return;
            }
            if(action.op==EditorOp::End){FinishEditor(app,!failed);return;}
            if(action.op==EditorOp::InjectFailure) {
                Check(std::string_view(editorSession)=="override-native" &&
                      app.m_Backend==::GEngine::PhysicsBackend::Jolt &&
                      std::string_view(SavedBackendKey())=="jolt" &&
                      app.m_Angular.committed &&
                      app.m_SimulationState==SimulationState::Ready &&
                      app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()==0,
                      "failed-switch preference precondition");
                if(failed){FinishEditor(app,false);return;}
                editorFailure.scene=app.m_ActiveScene.get();
                editorFailure.world=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
                editorFailure.packet=app.m_Angular.committed;
                editorFailure.generation=app.m_SceneGeneration;
                editorFailure.revision=app.m_Angular.revision;
                editorFailure.anchor=app.m_AngularBindings.bodies[0];
                editorFailure.bar=app.m_AngularBindings.bodies[1];
                editorFailure.joint=app.m_AngularBindings.joint;
                Check(bool(editorFailure.world->ReadBody(editorFailure.bar)) &&
                      bool(editorFailure.world->ReadJoint(editorFailure.joint)),
                      "failed-switch retained provider baseline");
                if(failed){FinishEditor(app,false);return;}
                app.m_InjectSceneConstructionFailure=true;
                EditorLog("arm-candidate-failure","backend.native");
                NextEditorAction();return;
            }
            if(action.op==EditorOp::Negative) {
                auto& session=app.m_Angular;
                Check(session.dirty && session.committed && session.draft && !app.m_SceneRequest,
                    "negative authoring precondition");
                if(!failed) {
                    const auto revision=session.revision;
                    const auto generation=app.m_SceneGeneration;
                    const auto oldJoint=app.m_AngularBindings.joint;
                    ++session.draftBaseRevision;
                    Check(!app.RequestAngularApply() && !app.m_SceneRequest &&
                          session.revision==revision && app.m_SceneGeneration==generation &&
                          app.m_AngularBindings.joint==oldJoint,"stale draft atomic rejection");
                    --session.draftBaseRevision;
                    const auto mass=session.draft->bodies[1].authoredMass;
                    session.draft->bodies[1].authoredMass=0;
                    Check(!app.RequestAngularApply() && !app.m_SceneRequest &&
                          session.revision==revision && app.m_SceneGeneration==generation &&
                          app.m_AngularBindings.joint==oldJoint,"invalid packet atomic rejection");
                    session.draft->bodies[1].authoredMass=mass;
                }
                EditorLog("stale-invalid-atomic-rejection",nullptr);
                if(failed){FinishEditor(app,false);return;}
                NextEditorAction();return;
            }
            if(action.op==EditorOp::PickMesh || action.op==EditorOp::DragMesh) {
                GeneralViewportGesture(app,action.op);return;
            }
            if(action.op==EditorOp::Viewport) {
                auto mid=(app.m_ViewportBounds[0]+app.m_ViewportBounds[1])*.5f;
                if(editorPhase==0){EditorMovePointer(app,mid.x,mid.y);++editorPhase;return;}
                if(editorPhase==1 || editorPhase==2) {
                    const auto windowID=app.GetWindow()->GetWindowID();
                    auto* window=SDL_GetWindowFromID(windowID);
                    Check(window!=nullptr,"ordinary Editor SDL viewport window");
                    if(!window){FinishEditor(app,false);return;}
                    int originX=0,originY=0;
                    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
                        SDL_GetWindowPosition(window,&originX,&originY);
                    SDL_Event event{};
                    event.type=editorPhase==1?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
                    event.button.windowID=windowID;event.button.button=SDL_BUTTON_LEFT;
                    event.button.state=editorPhase==1?SDL_PRESSED:SDL_RELEASED;
                    event.button.clicks=1;
                    event.button.x=int(std::lround(mid.x))-originX;
                    event.button.y=int(std::lround(mid.y))-originY;
                    Check(SDL_PushEvent(&event)==1,"SDL Editor viewport button enqueue");
                    ++editorPhase;return;
                }
                if(editorPhase>=3 && editorAge>=5 && app.m_ViewportActive && app.m_ViewportForcused){
                    EditorLog("viewport-click-observed",nullptr);NextEditorAction();}return;
            }
            if(action.op==EditorOp::Keyboard) {
                const auto scan=std::string_view(action.key)=="F2"?SDL_SCANCODE_F2:action.key[0]=='S'?SDL_SCANCODE_S:action.key[0]=='P'?SDL_SCANCODE_P:
                    action.key[0]=='T'?SDL_SCANCODE_T:SDL_SCANCODE_R;
                for(const auto type:{SDL_KEYDOWN,SDL_KEYUP}){SDL_Event e{};e.type=type;
                    e.key.windowID=app.GetWindow()->GetWindowID();e.key.keysym.scancode=scan;
                    e.key.keysym.sym=SDL_GetKeyFromScancode(scan);
                    e.key.state=type==SDL_KEYDOWN?SDL_PRESSED:SDL_RELEASED;
                    Check(SDL_PushEvent(&e)==1,"SDL Editor shortcut enqueue");}
                EditorLog("host-key",action.key);NextEditorAction();return;
            }
            if(action.op==EditorOp::Image || action.op==EditorOp::RoomImage ||
               action.op==EditorOp::UiImage)return;
            const std::string_view key(action.key);
            if(editorPhase==0 && key!="backend.native" && key!="backend.jolt" &&
               key!="shape.sphere" && key!="motion.kinematic")
                editorFocusRequest=EditorWindow(key);
            const auto index=WidgetIndex(key);Check(index>=0,"known Editor widget key");
            if(index<0){FinishEditor(app,false);return;}
            const auto& w=editorWidgets[unsigned(index)];
            if(action.op==EditorOp::Inspect) {
                if(!w.visible || w.frame+1<editorFrame)return;
                if(action.value && std::string_view(action.value)=="visible") {
                    EditorLog("visible-observed",action.key,w.id,w.lo,w.hi);NextEditorAction();return;
                }
                const bool wantDisabled=action.value && std::string_view(action.value)=="disabled";
                Check(action.value && (wantDisabled || std::string_view(action.value)=="enabled"),"explicit capability observation");
                Check(w.disabled==wantDisabled,"actual ImGui capability matches frozen expectation");
                EditorLog(w.disabled?"disabled-observed":"enabled-observed",action.key,w.id,w.lo,w.hi);
                if(failed){FinishEditor(app,false);return;}NextEditorAction();return;
            }
            if(editorPhase==0) {
                if(!w.visible || w.frame+1<editorFrame)return;
                if(action.op==EditorOp::Open && w.open) {
                    EditorLog("already-open",action.key,w.id,w.lo,w.hi);NextEditorAction();return;
                }
                EditorMovePointer(app,(w.lo.x+w.hi.x)*.5f,(w.lo.y+w.hi.y)*.5f);
                ++editorPhase;return;
            }
            if(editorPhase==1) {
                if(key=="scene-load" || key=="reset" || key=="general-reset")editorResetBefore=app.m_SceneGeneration;
                if(key=="general-reset")
                    generalOldSelection=app.m_ActiveScene->RenderData().Identify(app.m_HoveredEntity).value_or(::GEngine::EntityRenderId{});
                if(key=="reset") {
                    if(app.m_SimulationState==SimulationState::Running)
                        EditorLog("reset-started-running",SceneName(app.m_ActiveSelection.scene));
                    editorReset.packet=app.m_Angular.committed;
                    editorReset.generation=app.m_SceneGeneration;
                    editorReset.revision=app.m_Angular.revision;
                    editorReset.bar=app.m_AngularBindings.bodies[1];
                    editorReset.joint=app.m_AngularBindings.joint;
                    editorReset.backend=app.m_Backend;
                }
                if(action.op==EditorOp::Type)io.AddKeyEvent(ImGuiKey_ModCtrl,true);
                io.AddMouseButtonEvent(0,true);++editorPhase;return;
            }
            if(editorPhase==2) {
                io.AddMouseButtonEvent(0,false);
                if(action.op==EditorOp::Type)io.AddKeyEvent(ImGuiKey_ModCtrl,false);
                ++editorPhase;return;
            }
            if(action.op==EditorOp::Type) {
                if(editorPhase==3){io.AddKeyEvent(ImGuiKey_ModCtrl,true);io.AddKeyEvent(ImGuiKey_A,true);++editorPhase;return;}
                if(editorPhase==4){io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiKey_ModCtrl,false);++editorPhase;return;}
                if(editorPhase==5){io.AddInputCharactersUTF8(action.value);++editorPhase;return;}
                if(editorPhase==6){io.AddKeyEvent(ImGuiKey_Enter,true);++editorPhase;return;}
                if(editorPhase==7){io.AddKeyEvent(ImGuiKey_Enter,false);++editorPhase;return;}
            }
            if(action.op==EditorOp::Choose) {
                const int choice=action.value?std::atoi(action.value):-1;
                Check(choice>=0 && choice<=16,"bounded existing combo choice index");
                if(failed){FinishEditor(app,false);return;}
                if(editorPhase==3){io.AddKeyEvent(ImGuiKey_Home,true);++editorPhase;return;}
                if(editorPhase==4){io.AddKeyEvent(ImGuiKey_Home,false);++editorPhase;return;}
                const auto offset=editorPhase-5;
                if(offset<unsigned(2*choice)) {io.AddKeyEvent(ImGuiKey_DownArrow,offset%2==0);++editorPhase;return;}
                if(offset==unsigned(2*choice)){io.AddKeyEvent(ImGuiKey_Space,true);++editorPhase;return;}
                if(offset==unsigned(2*choice+1)){io.AddKeyEvent(ImGuiKey_Space,false);++editorPhase;return;}
                if(editorAge>=unsigned(12+2*choice) && editorClicked && ImGui::GetCurrentContext()->OpenPopupStack.empty()) {
                    EditorLog("chosen",action.key,w.id,w.lo,w.hi);NextEditorAction();
                }
                return;
            }
            const unsigned settleFrames=GeneralRole(editorSession)?(action.op==EditorOp::Type?9u:4u):10u;
            if(editorAge>=settleFrames && editorClicked && (action.op!=EditorOp::Open || w.open)) {
                EditorLog(action.op==EditorOp::Type?"edited":"clicked",action.key,w.id,w.lo,w.hi);
                NextEditorAction();
            }
        }
        static void CaptureEditorUi(RigidBodySimulationApp& app,const char* name) {
            const auto extent=app.GetWindow()->GetFramebufferPixelSize();
            Check(extent.Width>0 && extent.Height>0 && extent.Width<=4096 && extent.Height<=4096,
                  "actual Editor UI framebuffer extent");
            Check(bool(app.GetWindow()->BeginRender()),"actual Editor primary GL context");
            if(failed)return;
            std::vector<unsigned char> ui(std::size_t(extent.Width)*extent.Height*4);
            GLint previous{},previousReadBuffer{};
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous);
            glGetIntegerv(GL_READ_BUFFER,&previousReadBuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
            glReadBuffer(GL_FRONT);
            glReadPixels(0,0,int(extent.Width),int(extent.Height),GL_RGBA,GL_UNSIGNED_BYTE,ui.data());
            glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(previous));
            glReadBuffer(GLenum(previousReadBuffer));
            for(unsigned row=0;row<extent.Height/2;++row)
                for(unsigned col=0;col<extent.Width*4;++col)
                    std::swap(ui[row*extent.Width*4+col],
                              ui[(extent.Height-1-row)*extent.Width*4+col]);
            const auto path=output/(std::string(name)+".png");
            Check(!std::filesystem::exists(path) &&
                  stbi_write_png(path.string().c_str(),int(extent.Width),int(extent.Height),4,
                      ui.data(),int(extent.Width*4))!=0,
                  "create-only actual full Editor UI PNG");
            if(!failed)EditorLog("full-editor-png",name);
        }
        static void PreviewFrame(RigidBodySimulationApp& app) {
            if(!editorMode || failed || editorAction>=editorActions.size())return;
            const auto& action=editorActions[editorAction];
            if((action.op!=EditorOp::Image && action.op!=EditorOp::RoomImage &&
                action.op!=EditorOp::UiImage && action.op!=EditorOp::NativeImage) || editorAge<8)return;
            if(action.op==EditorOp::NativeImage) {
                Check(app.m_RenderTarget && !app.m_IsShowDebugBoundingBox && !app.m_ShowSelectedColliderOutlines,"clean native ordinary viewport capture");if(failed)return;
                const auto& target=*app.m_RenderTarget;const auto& size=target.Buffer().Description();
                Check(size.Width>0 && size.Height>0 && size.Width<=4096 && size.Height<=4096,"bounded native viewport framebuffer");if(failed)return;
                std::vector<std::byte> pixels(std::size_t(size.Width)*size.Height*4);
                Check(bool(target.ReadColor(pixels)),"native unscaled viewport readback");if(failed)return;
                for(unsigned row=0;row<size.Height/2;++row)for(unsigned col=0;col<size.Width*4;++col)
                    std::swap(pixels[row*size.Width*4+col],pixels[(size.Height-1-row)*size.Width*4+col]);
                const auto path=output/(std::string(action.key)+".png");
                Check(!std::filesystem::exists(path) && stbi_write_png(path.string().c_str(),int(size.Width),int(size.Height),4,pixels.data(),int(size.Width*4))!=0,"create-only native viewport PNG");
                if(!failed){EditorLog("native-viewport-png",action.key);NextEditorAction();}return;
            }
            if(action.op==EditorOp::UiImage) {
                Check(app.m_PresetThumbnailsLoaded,"full Editor UI thumbnail discovery");
                if(std::string_view(action.key)=="editable-properties-jolt") {
                    const auto& mass=editorWidgets[WidgetIndex("mass")];
                    if(!mass.visible || mass.frame+1<editorFrame)return;
                    Check(EditorAuthoredBodyReadback(app,false,false),"visible editable bar and provider state for capture");
                }
                if(std::string_view(action.key)=="general-nonphysics" || std::string_view(action.key)=="general-light-camera") {
                    const bool mesh=std::string_view(action.key)=="general-nonphysics";
                    const auto& field=editorWidgets[WidgetIndex(mesh?"material-roughness":"camera-fov")];
                    if(!field.visible || field.frame+1<editorFrame ||
                       !GeneralExpected(app,mesh?"general-mesh":"general-camera-preview"))return;
                }
                CaptureEditorUi(app,action.key);
                if(!failed)NextEditorAction();
                return;
            }
            auto& physics=*app.m_ActiveScene->GetPhysicsSystem();auto& w=*physics.GetPhysicsWorld();
            Check(physics.GetCompletedSteps()==0 &&
                  (action.op==EditorOp::RoomImage?EditorRoomReadback(app):w.GetJointCount()==1) &&
                  !app.m_IsShowDebugBoundingBox && !app.m_ShowSelectedColliderOutlines,
                  "clean actual Editor viewport at Tick 0");
            Check(app.m_RenderTarget!=nullptr,"Editor viewport render target");
            if(failed)return;
            const auto& target=*app.m_RenderTarget;const auto& size=target.Buffer().Description();
            Check(size.Width>0&&size.Height>0&&size.Width<=4096&&size.Height<=4096,"Editor viewport extent");
            if(failed)return;
            std::vector<std::byte> pixels(std::size_t(size.Width)*size.Height*4);
            Check(bool(target.ReadColor(pixels)),"actual ordinary Editor render target readback");if(failed)return;
            constexpr unsigned width=512,height=288;
            std::array<unsigned char,width*height*4> image{};
            const double cropWidth=std::min(double(size.Width),double(size.Height)*width/height);
            const double cropHeight=cropWidth*height/width;
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                auto* dst=image.data()+(y*width+x)*4;
                const auto sx=std::min(size.Width-1,unsigned((size.Width-cropWidth)*.5+(x+.5)*cropWidth/width));
                const auto sy=std::min(size.Height-1,unsigned((size.Height-cropHeight)*.5+(y+.5)*cropHeight/height));
                const auto* src=pixels.data()+((size.Height-1-sy)*size.Width+sx)*4;
                for(unsigned c=0;c<3;++c)dst[c]=std::to_integer<unsigned char>(src[c]);dst[3]=255;
            }
            const std::string imageName=action.op==EditorOp::RoomImage?
                "room-"+std::string(BackendKey(w.GetBackend()))+"-"+action.key:
                std::string(action.key);
            const auto path=output/(imageName+".png");
            Check(!std::filesystem::exists(path) && stbi_write_png(path.string().c_str(),width,height,4,
                  image.data(),width*4)!=0,"create-only actual viewport PNG");
            if(failed)return;
            if(action.op==EditorOp::RoomImage)++editorRoomImages;else ++editorImages;
            EditorLog(action.op==EditorOp::RoomImage?"room-viewport-png":"viewport-png",imageName.c_str());
            std::ofstream meta(output/(imageName+".txt"));
            meta<<"source=ordinary RBS render target\nscene="<<Rbs::SceneName(app.m_ActiveSelection.scene)
                <<"\nbackend="<<::GEngine::PhysicsBackendName(w.GetBackend())
                <<"\nsource_width="<<size.Width<<"\nsource_height="<<size.Height
                <<"\noutput=512x288\nphysics_ticks="<<physics.GetCompletedSteps()
                <<"\ndefault_visualization=OFF\n";
            NextEditorAction();
        }
        static bool Begin(RigidBodySimulationApp& app) {
            if(const char* mode=std::getenv("GENGINE_PHASE28_MODE");mode&&std::string_view(mode)=="editor") {
                if(editorStarted)return false;
                const char* directory=std::getenv("GENGINE_PHASE28_OUTPUT");
                const char* identity=std::getenv("GENGINE_PHASE28_CASE");
                const char* role=std::getenv("GENGINE_PHASE28_EDITOR_SESSION");
                Check(directory&&identity&&role,"explicit Editor evidence directory/case/session");
                if(failed)return false;
                output=directory;caseName=identity;editorSession=role;
                Check(output.is_absolute()&&std::filesystem::is_directory(output)&&
                      !std::filesystem::exists(output/"result.json") &&
                      !std::filesystem::exists(output/"editor-startup.json"),
                      "create-only Editor evidence directory");
                const std::string_view sessionRole(role);
                const bool freshProperties = sessionRole == "properties-default-jolt" || sessionRole == "direct-default-jolt" || sessionRole == "general-default-jolt" || (ComponentRole(sessionRole) && sessionRole!="component-native");
                const bool properties = freshProperties || sessionRole == "properties-jolt" || sessionRole == "properties-native" || sessionRole == "general-native" || ComponentRole(sessionRole);
                const bool jolt = sessionRole == "fresh-jolt" || sessionRole == "properties-jolt" || freshProperties;
                Check(sessionRole=="fresh-jolt" || sessionRole=="saved-native" ||
                      sessionRole=="override-native" || properties,"admitted ordinary Editor session");
                Check(std::string_view(identity)=="EDITOR_NATIVE" ||
                      std::string_view(identity)=="EDITOR_JOLT","admitted ordinary Editor identity");
                Check(app.m_Backend==(jolt ?
                      ::GEngine::PhysicsBackend::Jolt : ::GEngine::PhysicsBackend::Native),
                      "ordinary Editor initial app backend");
                auto* startupWorld=app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
                Check(startupWorld && startupWorld->GetBackend()==app.m_Backend,
                      "ordinary Editor actual startup provider");
                Check(std::string_view(identity)==(jolt?"EDITOR_JOLT":"EDITOR_NATIVE"),
                      "ordinary Editor session/case pairing");
                Check(app.m_Launch.m_BackendExplicit==((properties && !freshProperties) || sessionRole=="override-native"),
                      "ordinary Editor CLI backend presence");
                Check(!freshProperties || std::string_view(SavedBackendKey())=="none","fresh default Properties session");
                Check(properties || std::string_view(SavedBackendKey())==
                      (sessionRole=="fresh-jolt"?"none":sessionRole=="saved-native"?"native":"jolt"),
                      "ordinary Editor saved preference precedence");
                Check(app.m_ActiveSelection.scene==(sessionRole=="saved-native"?
                      RbsScenePreset::HingeBasic:RbsScenePreset::HingeLimited),
                      "ordinary Editor initial scene");
                if(failed)return false;
                {
                    std::ofstream startup(output/"editor-startup.json",std::ios::out|std::ios::trunc);
                    startup<<"{\"session\":\""<<role<<"\",\"cli_backend_explicit\":"
                           <<(app.m_Launch.m_BackendExplicit?"true":"false")
                           <<",\"saved_backend\":\""<<SavedBackendKey()
                           <<"\",\"app_backend\":\""<<BackendKey(app.m_Backend)
                           <<"\",\"world_backend\":\""<<BackendKey(startupWorld->GetBackend())
                           <<"\",\"scene\":\""<<Rbs::SceneName(app.m_ActiveSelection.scene)
                           <<"\"}\n";
                }
                editorEvents.open(output/"editor-events.csv",std::ios::out|std::ios::trunc);
                editorEvents<<"frame,action,event,key,item_id,x0,y0,x1,y1,physics_ticks\n";
                EditorLog("startup-precedence",role);
                editorActions.clear();EditorScript(sessionRole);editorMode=true;editorStarted=true;
                editorGeneration=app.m_SceneGeneration;
                editorLastStep=unsigned(app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps());
                if (sessionRole == "fresh-jolt" || sessionRole == "component-general-jolt") {
                    const auto path = output / "diagnostic-defaults.json";
                    Check(!std::filesystem::exists(path), "create-only diagnostic default evidence");
                    if (!failed) {
                        std::ofstream proof(path, std::ios::out | std::ios::trunc);
                        const bool valid = proof && RestDiagnosis::Phase28DefaultInitProbe(proof);
                        proof.flush();
                        Check(valid && proof.good(), "Phase21 diagnostic defaults after array initialization");
                    }
                    if (failed) { FinishEditor(app, false); return false; }
                    EditorLog("diagnostic-defaults", "pass");
                }
                return false;
            }
            const char* directory=std::getenv("GENGINE_PHASE28_OUTPUT");if(!directory)return false;
            if(started)return true;
            output=directory;caseName=std::getenv("GENGINE_PHASE28_CASE");
            debug=std::getenv("GENGINE_PHASE28_DEBUG")!=nullptr;
            Check(output.is_absolute()&&std::filesystem::is_directory(output)&&
                !std::filesystem::exists(output/"result.json"),"create-only absolute evidence directory");
            Check(caseName!=nullptr,"explicit admitted case identity");
            clockQuantumNs=Quantum();Check(clockQuantumNs>0&&clockQuantumNs<=1000,"clock quantum");
            auto& physics=*app.m_ActiveScene->GetPhysicsSystem();auto& w=*physics.GetPhysicsWorld();
            observedWorld=&w;
            contactV2=std::getenv("GENGINE_PHASE28_CONTACT_FIXTURE")!=nullptr;
            const std::string_view version=contactV2 ? std::getenv("GENGINE_PHASE28_CONTACT_FIXTURE") : "";
            contactV4=version=="v4";
            contactV3=contactV4 || version=="v3";
            if(contactV2) {
                using namespace ::GEngine;
                const auto floor=w.ReadBody(app.m_AngularBindings.bodies[2]);
                Check(!debug && caseName && std::string_view(caseName).ends_with("_CONTACT") &&
                      app.m_AngularBindings.bodyCount==3 && w.GetBodyCount()==3 &&
                      floor && floor->type==Component::BodyType::Static && floor->inverseMass==0 &&
                      floor->shapeType==ShapeType::Box,"isolated v2 static contact floor");
                if(floor) {
                    contactFloorCenter=Vec(floor->position);
                    contactFloorHalf=(floor->localBounds.maxs-floor->localBounds.mins)*.5f;
                    contactFloorOrientation=floor->orientation;
                }
            }
            Check(w.GetJointCount()==1,"one ordinary angular joint");
            if(auto h=w.JointAt(0);h){joint=*h;
                if(auto state=w.ReadJoint(joint);state){
                    anchor=state->descriptor.bodyA;bar=state->descriptor.bodyB;
                    if(auto a=w.ReadBody(anchor),b=w.ReadBody(bar);a&&b&&a->inverseMass>0&&b->inverseMass==0)
                        std::swap(anchor,bar);
                }else Check(false,"initial angular joint readback");
            }else Check(false,"initial angular joint handle");
            if(debug)Focused(w.GetBackend());
            nativeStageCapture=std::getenv("GENGINE_PHASE28_NATIVE_CONTACT_STAGES")!=nullptr;
            if(nativeStageCapture) {
                using namespace ::GEngine;
                const auto j=w.ReadJoint(joint);
                const auto a=w.ReadBody(anchor),b=w.ReadBody(bar);
                Check(!debug && w.GetBackend()==PhysicsBackend::Native && w.GetBodyCount()==3 &&
                      caseName && std::string_view(caseName).ends_with("_CONTACT") &&
                      j && !j->descriptor.collideConnected &&
                      (j->descriptor.kind==JointKind::Hinge || j->descriptor.kind==JointKind::LimitedConstantVelocity) &&
                      a && a->type==Component::BodyType::Static && b && b->type==Component::BodyType::Dynamic &&
                      IsPhysicsProfilingEnabled(),"Native stages require the isolated angular contact topology and profiler");
            }
            normalTarget=debug?240-unitTicks:caseName&&std::string_view(caseName)=="H_FREE"?360:600;
            Check(unitTicks<=240&&normalTarget<=600,"debug and physical tick budget");
            Capture(w,joint,bar,normal[0],0);
            SetObservers();started=true;return true;
        }
        static void BeforeTick() {
            wrapEvent=false;eventTick=tick+1;
            Check(observedWorld!=nullptr,"normal pre-step world identity");
            if(observedWorld){const auto pre=observedWorld->ReadBody(bar);
                Check(bool(pre),"normal pre-step body readback");
                if(tick+1<normal.size())normal[tick+1].preStepActive=pre && !pre->sleeping;}
            g_Phase28ObserverWrapperNs=0;g_Phase28ObserverWrapperCalls=0;
            ResetJoltProfile();
            completeUpdateNs=0;
            ::GEngine::JoltAudit::countAllocations=false;
        }
        static bool AfterTick(RigidBodySimulationApp& app) {
            using namespace ::GEngine;
            ::GEngine::JoltAudit::countAllocations=false;
            Check(completeUpdateNs>0,"complete ordinary PhysicsSystem Update observation");
            ++tick;Check(tick<=normalTarget&&tick<normal.size(),"ordinary sample bound");
            if(tick>=normal.size())return Finish(false);
            auto& physics=*app.m_ActiveScene->GetPhysicsSystem();auto& w=*physics.GetPhysicsWorld();
            auto& s=normal[tick];
            s.inclusiveNs=completeUpdateNs;
            s.jointNs=physics.GetJointSolveNanoseconds();s.contactNs=physics.GetContactSolveNanoseconds();
            s.cppCalls=::GEngine::JoltAudit::cppCalls;s.cppBytes=::GEngine::JoltAudit::cppBytes;
            s.jphCalls=::GEngine::JoltAudit::joltCalls;s.jphBytes=::GEngine::JoltAudit::joltBytes;
            CaptureJoltProfile(s);
            s.scopedCppCalls=scopeCalls;s.scopedCppBytes=scopeBytes;s.boundaryObserverNs=boundaryObserverNs;
            const auto observerStartNs=::GEngine::JoltAudit::Now();
            Capture(w,joint,bar,s,work);
            if(w.GetBackend()==PhysicsBackend::Native)s.nativeContactPairs=NativeContactPairs(physics);
            s.captureNs=::GEngine::JoltAudit::Now()-observerStartNs;
            s.wrapperNs=g_Phase28ObserverWrapperNs.load();s.wrapperCalls=g_Phase28ObserverWrapperCalls.load();
            s.observerNs=s.captureNs;s.minimalNs=s.inclusiveNs;
            s.fullNs=s.minimalNs+s.captureNs;
            s.observerUpperNs=s.fullNs+s.boundaryObserverNs;
            s.observerRefinedNs=s.captureNs+s.boundaryObserverNs+s.wrapperNs+s.profileHookNs+
                2*clockQuantumNs*(s.wrapperCalls+s.profileHookCalls);
            const auto parity=w.ReadBody(bar);Check(parity&&parity->position==s.bar.position,
                "normal same-step physical parity outside timing scopes");
            Check(!physics.HasJointFailure()&&!physics.GetBackendFailure(),"ordinary finite solver state");
            Check(physics.GetCompletedSteps()==tick,"one ordinary fixed step per sample");
            if(tick==60)TakeImpulse(w,bar,Vec(3,0,0),tick);
            if(tick==300)TakeImpulse(w,bar,(caseName&&std::string_view(caseName).starts_with("CV"))?
                Vec(0,0,3):Vec(-3,0,0),tick);
            Check(arenaCount<=arena.size()&&heapCount<=heap.size(),"bounded allocation recorder");
            if(failed)return Finish(false);
            if(tick==normalTarget) {
                if(!debug&&caseName&&std::string_view(caseName)=="H_FREE")WrapControl(w.GetBackend());
                return Finish(!failed);
            }
            return false;
        }
        static bool Finish(bool complete) {
            ::GEngine::JoltAudit::countAllocations=false;
            if(::GEngine::PhysicsDiagnostic::observer==&nativeStageObserver)
                ::GEngine::PhysicsDiagnostic::observer=nullptr;
            if(nativeStageCapture) {
                std::ofstream stages(output/"native-contact-stages.csv");
                stages<<"tick,stage,calls,bytes,queries,epa,storage\n";
                for(unsigned t=1;t<=tick && t<nativeStages.size();++t) {
                    Check(nativeStageVisits[t]==NativeStageCount,"Native stage record incomplete");
                    for(unsigned i=0;i<nativeStageVisits[t];++i) {
                        const auto& row=nativeStages[t][i];
                        stages<<t<<','<<i<<','<<row.calls<<','<<row.bytes<<','<<row.queries<<','<<row.epa<<','<<row.storage<<'\n';
                    }
                }
                stages.close();
                Check(bool(stages),"Native stage capture write failed");
                complete=complete && !failed;
            }
            ::GEngine::Phase26Observation::observer=nullptr;
            ::GEngine::Phase27Audit::arena=nullptr;::GEngine::Phase27Audit::heap=nullptr;
            auto emit=[&](std::ostream& out,const char* segment,unsigned t,const Sample& s) {
                const auto& j=s.joint;
                out<<segment<<','<<t<<','<<s.rawEnergy<<','<<s.intendedEnergy<<','<<s.work<<','<<j.error<<','
                   <<j.angular.axisError<<','<<j.angular.cvError<<','<<j.angular.signedAngle<<','
                   <<j.angular.angleU<<','<<j.angular.angleV<<','<<j.angular.violationU<<','
                   <<j.angular.violationV<<','<<j.angular.limitU<<','<<j.angular.limitV<<','
                   <<s.inclusiveNs<<','<<s.jointNs<<','<<s.contactNs<<','<<s.observerNs<<','
                   <<s.minimalNs<<','<<s.fullNs<<','<<s.wrapperNs<<','<<s.wrapperCalls<<','<<s.captureNs<<','
                   <<s.observerUpperNs<<','<<s.observerRefinedNs<<','
                   <<s.cppCalls<<','<<s.cppBytes<<','<<s.jphCalls<<','<<s.jphBytes<<','<<s.preStepActive<<','
                   <<s.scopedCppCalls[0]<<','<<s.scopedCppBytes[0]<<','
                   <<s.scopedCppCalls[1]<<','<<s.scopedCppBytes[1]<<','
                   <<s.scopedCppCalls[2]<<','<<s.scopedCppBytes[2]<<','<<s.boundaryObserverNs<<','
                   <<s.sdkUpdateNs<<','<<s.profileHookNs<<','<<s.profileHookCalls;
                for(unsigned i=0;i<8;++i)out<<','<<s.joltStageNs[i]<<','<<s.joltStageCalls[i];
                out<<','<<s.nativeContactPairs;
                out<<'\n';
            };
            std::ofstream o(output/"samples.csv");o<<std::setprecision(17);
            o<<"segment,tick,energy_raw,energy_intended,work,anchor_error,axis_error,cv_error,angle,angle_u,angle_v,violation_u,violation_v,limit_u,limit_v,inclusive_ns,joint_ns,contact_ns,observer_ns,minimal_ns,full_ns,wrapper_ns,wrapper_calls,capture_ns,observer_upper_ns,observer_refined_ns,cpp_calls,cpp_bytes,jph_calls,jph_bytes,pre_step_active,joint_cpp_calls,joint_cpp_bytes,graph_cpp_calls,graph_cpp_bytes,broadphase_cpp_calls,broadphase_cpp_bytes,boundary_observer_ns,sdk_update_ns,profile_hook_ns,profile_hook_calls";
            for(unsigned i=0;i<8;++i)o<<",jolt_stage_"<<i<<"_ns,jolt_stage_"<<i<<"_calls";
            o<<",native_contact_pairs\n";
            for(unsigned t=0;t<=tick&&t<normal.size();++t)emit(o,"normal",t,normal[t]);
            if(wrapTicks)for(unsigned t=0;t<=wrapTicks;++t)emit(o,"wrap",t,wrap[t]);
            o.close();o.open(output/"bodies.csv");o<<std::setprecision(17);
            o<<"segment,tick,x,y,z,vx,vy,vz,wx,wy,wz,qw,qx,qy,qz,inverse_mass,sleeping\n";
            auto body=[&](const char* segment,unsigned t,const Sample& s){const auto& b=s.bar;
                o<<segment<<','<<t<<','<<b.position.x<<','<<b.position.y<<','<<b.position.z<<','
                 <<b.linearVelocity.x<<','<<b.linearVelocity.y<<','<<b.linearVelocity.z<<','
                 <<b.angularVelocity.x<<','<<b.angularVelocity.y<<','<<b.angularVelocity.z<<','
                 <<b.orientation.w<<','<<b.orientation.x<<','<<b.orientation.y<<','<<b.orientation.z<<','
                 <<b.inverseMass<<','<<b.sleeping<<'\n';};
            for(unsigned t=0;t<=tick&&t<normal.size();++t)body("normal",t,normal[t]);
            if(wrapTicks)for(unsigned t=0;t<=wrapTicks;++t)body("wrap",t,wrap[t]);
            o.close();o.open(output/"arena.csv");
            o<<"segment,tick,reserve,pointer,caller,requested,aligned,used\n";
            for(unsigned i=0;i<std::min<unsigned>(arenaCount,arena.size());++i){const auto& r=arena[i];
                o<<(r.wrap?"wrap":"normal")<<','<<r.tick<<','<<r.event.reserve<<','<<r.event.address<<','
                 <<r.event.caller<<','<<r.event.requested<<','<<r.event.aligned<<','<<r.event.used<<'\n';}
            o.close();o.open(output/"heap.csv");o<<"segment,tick,reserve,pointer,caller,requested\n";
            for(unsigned i=0;i<std::min<unsigned>(heapCount,heap.size());++i){const auto& r=heap[i];
                o<<(r.wrap?"wrap":"normal")<<','<<r.tick<<','<<r.event.reserve<<','<<r.event.address<<','
                 <<r.event.caller<<','<<r.event.requested<<'\n';}
            o.close();o.open(output/"impulses.csv");o<<std::setprecision(17);
            o<<"after_tick,jx,jy,jz,vx_before,vy_before,vz_before,mass,work";
            if(contactV3)o<<",com_x,com_y,com_z,point_x,point_y,point_z,wx_before,wy_before,wz_before,"
                            "vx_after,vy_after,vz_after,wx_after,wy_after,wz_after";
            o<<'\n';
            for(unsigned i=0;i<impulseCount;++i){const auto& r=impulses[i];
                o<<r.tick<<','<<r.impulse.x<<','<<r.impulse.y<<','<<r.impulse.z<<','
                 <<r.before.x<<','<<r.before.y<<','<<r.before.z<<','<<r.mass<<','<<r.work;
                if(contactV3) {
                    const auto& pt=impulsePoints[i];
                    for(const Vec v:{pt.com,pt.point,pt.angularBefore,pt.linearAfter,pt.angularAfter})
                        o<<','<<v.x<<','<<v.y<<','<<v.z;
                }
                o<<'\n';}
            o.close();o.open(output/"fixture.json");
            o<<std::setprecision(17)<<'{';
            if(contactV2) {
                o<<"\"contact_version\":"<<(contactV4?4:contactV3?3:2)<<",\"floor_center\":["<<contactFloorCenter.x<<','<<contactFloorCenter.y<<','<<contactFloorCenter.z
                 <<"],\"floor_half\":["<<contactFloorHalf.x<<','<<contactFloorHalf.y<<','<<contactFloorHalf.z
                 <<"],\"floor_orientation\":["<<contactFloorOrientation.w<<','<<contactFloorOrientation.x<<','
                 <<contactFloorOrientation.y<<','<<contactFloorOrientation.z<<"],";
            }
            o<<"\"mass_kg\":1,\"scale\":1,\"gravity_y\":-10,\"pivot_y\":"
             <<normal[0].joint.worldAnchorA.y<<",\"floor\":"
             <<(caseName&&std::string_view(caseName).ends_with("CONTACT")?"true":"false")
             <<",\"normal_ticks\":"<<tick<<",\"wrap_ticks\":"<<wrapTicks<<"}\n";
            o.close();o.open(output/"result.json");
            o<<"{\"complete\":"<<(complete?"true":"false")<<",\"ticks\":"<<(tick+wrapTicks+unitTicks)
             <<",\"normal_ticks\":"<<tick<<",\"wrap_ticks\":"<<wrapTicks
             <<",\"unit_ticks\":"<<unitTicks<<",\"clock_quantum_ns\":"<<clockQuantumNs
             <<",\"arena_events\":"<<arenaCount<<",\"heap_events\":"<<heapCount
             <<",\"target_sizes_runtime_address\":"<<reinterpret_cast<std::uintptr_t>(g_Phase27TargetSizes)
             <<",\"target_sizes\":[";
            for(unsigned i=0;i<8;++i){if(i)o<<',';o<<g_Phase27TargetSizes[i];}
            o<<"],\"failure\":\""<<(failure?failure:"")<<"\"}\n";
            return true;
        }
    };
}
#endif
