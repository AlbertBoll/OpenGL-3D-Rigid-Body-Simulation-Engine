#pragma once
#if defined(GENGINE_PHASE26_QUALIFICATION) || defined(GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS)
#include "RigidBodySimulation.h"
#include "Scene/_Scene.h"
#include <algorithm>
#include <cstdio>
#include <limits>
#ifdef GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS
#include "RestingContactDiagnosis.h" // Existing read-only full-handle adapter for contact witnesses.
#include "LaunchConfigChecks.h"
#endif

namespace Rbs
{
    // Tests share the application command path. No test writes enter the measured world.
    struct DistanceChainImpulseChecks
    {
        using Control = DistanceChainImpulseControl;
        using Direction = Control::Direction;
        static bool Unit()
        {
            using namespace GEngine;
            const auto create = [](PhysicsWorld& world, bool dynamic = true) {
                auto shape = CreateCollisionShape(SphereShapeDesc{1});
                PhysicsBodyDesc desc;
                desc.type = dynamic ? Component::BodyType::Dynamic : Component::BodyType::Static;
                if (!shape) return PhysicsBodyHandle{};
                auto body = world.CreateBody(desc, *shape);
                return body ? *body : PhysicsBodyHandle{};
            };
            static_assert(Control::MinMagnitude == 0 && Control::MaxMagnitude == 5 && Control::DefaultMagnitude == 2);
            for (auto selected : {SceneSelection{RbsScenePreset::DistanceChain, 4},
                {RbsScenePreset::DistanceChain, 8}, {RbsScenePreset::DistanceChain, 16},
                {RbsScenePreset::DistanceChainContact, 8}})
            {
                const unsigned n = selected.chainLinks;
                PhysicsWorld world, foreign;
                const auto root = create(world, false);
                std::array<PhysicsBodyHandle, 16> links{};
                for (unsigned i = 0; i < n; ++i) if (!(links[i] = create(world))) return false;
                PhysicsBodyHandle ground{};
                if (selected.scene == RbsScenePreset::DistanceChainContact)
                {
                    auto shape = CreateCollisionShape(BoxShapeDesc{{50,.5f,50}});
                    PhysicsBodyDesc desc; desc.type = Component::BodyType::Static;
                    if (!shape) return false;
                    auto body = world.CreateBody(desc, *shape);
                    if (!body) return false;
                    ground = *body;
                }
                Control c;
                c.Bind(links[n-1], true);
                if (!root || c.Target() != *world.BodyAt(n)) return false;
                for (float bad : {-0.01f, 5.01f, std::numeric_limits<float>::infinity(),
                                  std::numeric_limits<float>::quiet_NaN()})
                    if (c.Request(world, bad, Direction::PositiveX) || c.Pending()) return false;
                if (c.Request(world, 1, static_cast<Direction>(999))) return false;
                if (!c.Request(world, 0, Direction::PositiveX) || c.Pending() || c.AppliedCount()) return false;
                if (!c.Request(world, 2, Direction::PositiveX) || c.Request(world, 5, Direction::NegativeZ)) return false;
                c.Consume(world, true);
                if (!c.Pending() || world.ReadBody(links[n-1])->linearVelocity != Math::Vec3f(0)) return false;
                c.Consume(world, false);
                c.Consume(world, false);
                if (c.Pending() || c.AppliedCount() != 1 || c.LastApplied() != Math::Vec3f(2,0,0) ||
                    world.ReadBody(links[n-1])->linearVelocity != Math::Vec3f(2,0,0)) return false;
                for (unsigned i = 0; i + 1 < n; ++i)
                    if (world.ReadBody(links[i])->linearVelocity != Math::Vec3f(0)) return false;
                for (auto dir : {Direction::NegativeX, Direction::PositiveZ, Direction::NegativeZ})
                {
                    if (!c.Request(world, 5, dir)) return false;
                    c.Consume(world, false);
                }
                if (c.AppliedCount() != 4 || world.ReadBody(links[n-1])->linearVelocity != Math::Vec3f(-3,0,0)) return false;
                if (ground && (c.Target() == ground || c.Target() == root ||
                    world.ReadBody(ground)->linearVelocity != Math::Vec3f(0) ||
                    world.ReadBody(root)->linearVelocity != Math::Vec3f(0))) return false;
                const auto last = c.LastApplied();
                if (!c.Request(world, 1, Direction::PositiveX) || !world.DestroyBody(links[n-1])) return false;
                auto replacement = create(world);
                if (!replacement || replacement.index != links[n-1].index ||
                    replacement.generation == links[n-1].generation) return false;
                c.Consume(world, false);
                c.Consume(world, false);
                if (c.Pending() || c.AppliedCount() != 4 || c.LastApplied() != last ||
                    c.GetStatus() != Control::Status::InvalidTarget ||
                    world.ReadBody(replacement)->linearVelocity != Math::Vec3f(0)) return false;
                if (c.Request(world, 1, Direction::PositiveX)) return false;
                for (auto invalid : {PhysicsBodyHandle{}, root, ground, create(foreign)})
                {
                    c.Bind(invalid, true);
                    if (c.Request(world, 1, Direction::PositiveX)) return false;
                }
                c.Bind(replacement, false);
                if (c.Request(world, 2, Direction::PositiveX) || c.Pending()) return false;
                c.Bind(replacement, true);
                if (!c.Request(world, 2, Direction::PositiveX) || !world.Close()) return false;
                c.Consume(world, false);
                if (c.Pending() || c.AppliedCount() || c.GetStatus() != Control::Status::InvalidTarget) return false;
            }
            std::puts("Phase26 impulse controller C4/C8/C16/G8: bounds/directions/identity/terminal/static-ground/pause/single-shot/close PASS");
            return true;
        }
        static bool QualificationIsolation(RigidBodySimulationApp& app)
        {
            const auto* world = app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            const auto before = world->ReadBody(app.m_ChainImpulse.Target());
            if (!before || app.m_ChainImpulse.Enabled() || app.RequestDistanceChainImpulse() ||
                app.m_ChainImpulse.Pending() || app.m_ChainImpulse.AppliedCount()) return false;
            const auto after = world->ReadBody(app.m_ChainImpulse.Target());
            const bool ok = after && before->linearVelocity == after->linearVelocity && before->sleeping == after->sleeping;
            if (ok) std::puts("Phase26 measured-world manual impulse isolation PASS");
            return ok;
        }
#ifdef GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS
        inline static std::filesystem::path output;
        inline static unsigned frames{}, panels{}, boundaries{}, pauseFrame{};
        inline static std::uint64_t pausedSteps{};
        inline static bool queuedAtBoundary{}, applied{}, reported{};
        inline static const char* failure{};
        inline static std::array<GEngine::Math::Vec3f,18> velocities{};
        inline static GEngine::Math::Vec3f pausedVelocity{0};
        inline static bool g8{}, slept{}, wakeObserved{}, awakeAfterKick{};
        inline static unsigned groundMask{}, groundContacts{}, groundContactsAfter{};
        inline static std::uint64_t sleepTick{}, kickTick{}, wakeTick{};
        inline static GEngine::PhysicsDiagnostic::Observer contactObserver;
        inline static std::array<GEngine::PhysicsBodyHandle,10> g8Bodies{};
        struct G8Tick { std::uint64_t tick{}; unsigned asleep{}, contacts{}, mask{}, applications{}; };
        inline static std::array<G8Tick,120> g8Trace{};
        inline static unsigned traceCount{};
        static bool Configure(std::vector<std::string_view>& args)
        {
            auto found = std::find(args.begin(), args.end(), "--phase26-impulse-output");
            if (found == args.end() || found+1 == args.end()) return false;
            output = std::string(*(found+1)); args.erase(found, found+2);
            return output.is_absolute() && std::filesystem::is_directory(output) &&
                !std::filesystem::exists(output/"result.json") && PreEditorValidation::CheckLaunchConfig() && Unit();
        }
        static void Check(bool ok, const char* why) { if (!ok && !failure) failure = why; }
        static void PanelDrawn() { ++panels; }
        static void BeforeBoundary(RigidBodySimulationApp& app)
        {
            auto& w = *app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            queuedAtBoundary = app.m_ChainImpulse.Pending();
            for (unsigned i=0; i<w.GetBodyCount(); ++i)
                velocities[i] = w.ReadBody(*w.BodyAt(i))->linearVelocity;
        }
        static void AfterBoundary(RigidBodySimulationApp& app)
        {
            using GEngine::Math::Vec3f;
            auto& w = *app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            for (unsigned i=0; i<w.GetBodyCount(); ++i)
            {
                const auto body = w.ReadBody(*w.BodyAt(i));
                const auto expected = velocities[i] + ((queuedAtBoundary && i==app.m_Launch.chainLinks)
                    ? (g8 ? Vec3f(0,0,5) : Vec3f(2,0,0))*body->inverseMass : Vec3f(0));
                Check(glm::length(body->linearVelocity-expected) < 1e-6f, "boundary target or impulse mismatch");
            }
            if (queuedAtBoundary)
            {
                applied = true;
                if (g8) kickTick = app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()+1;
            }
            if (applied) ++boundaries;
            Check(!app.m_ChainImpulse.Pending(), "command repeated");
        }
        static void ObserveG8(RigidBodySimulationApp& app, GEngine::PhysicsDiagnostic::Stage stage,
            const GEngine::PhysicsWorld& w, GEngine::ManifoldCollector& manifolds)
        {
            using namespace GEngine;
            using Stage = PhysicsDiagnostic::Stage;
            if (stage != Stage::Wake && stage != Stage::End) return;
            unsigned asleep=0;
            for (unsigned i=0; i<g8Bodies.size(); ++i)
            {
                const auto body=w.ReadBody(g8Bodies[i]);
                Check(bool(body), "G8 live body identity");
                if (!body) return;
                Check(Math::IsFinite(body->position) && Math::IsFinite(body->linearVelocity) &&
                    Math::IsFinite(body->angularVelocity) && std::isfinite(body->orientation.w) &&
                    std::isfinite(body->orientation.x) && std::isfinite(body->orientation.y) &&
                    std::isfinite(body->orientation.z), "G8 nonfinite physical state");
                if (i==0 || i==9)
                    Check(body->type==Component::BodyType::Static && body->linearVelocity==Math::Vec3f(0) &&
                        body->angularVelocity==Math::Vec3f(0) && body->position==(i==0?Math::Vec3f(0,1.5f,0):Math::Vec3f(0)) &&
                        body->orientation==Math::Quat(1,0,0,0), "G8 static root or ground changed");
                else asleep+=body->sleeping;
            }
            const auto tick=app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps();
            if (stage==Stage::Wake)
            {
                if (applied && tick+1==kickTick)
                {
                    Check(slept && asleep==0, "G8 connected wake did not reach every link");
                    wakeObserved=asleep==0; wakeTick=tick+1;
                }
                return;
            }
            groundMask=groundContacts=0;
            for (auto& manifold:manifolds.m_Manifolds)
                for (int point=0; point<manifold.GetNumContacts(); ++point)
                {
                    const auto c=manifold.GetContact(point);
                    Check(c.m_BodyA && c.m_BodyB, "G8 null contact body");
                    if (!c.m_BodyA || !c.m_BodyB) return;
                    Check(w.IsBodyIdentityValid(c.m_BodyA->GetIdentity()) &&
                        w.IsBodyIdentityValid(c.m_BodyB->GetIdentity()) &&
                        c.m_BodyA->GetIdentity()==manifold.GetBodyAIdentity() &&
                        c.m_BodyB->GetIdentity()==manifold.GetBodyBIdentity(), "G8 contact pair identity");
                    const auto a=PhysicsTestAccess::Handle(w,*c.m_BodyA),b=PhysicsTestAccess::Handle(w,*c.m_BodyB);
                    Check(w.IsBodyValid(a) && w.IsBodyValid(b) && a!=b &&
                        std::isfinite(c.separationDistance), "G8 invalid contact handle/state");
                    const auto other=a==g8Bodies[9]?b:b==g8Bodies[9]?a:PhysicsBodyHandle{};
                    if (other)
                    {
                        unsigned link=1;
                        for (;link<=8 && g8Bodies[link]!=other;++link) {}
                        Check(link<=8, "G8 ground contact does not identify a dynamic link");
                        if (link<=8) { ++groundContacts; groundMask|=1u<<(link-1); }
                    }
                }
            if (!applied && asleep==8 && groundMask==255 && !slept) { slept=true; sleepTick=tick; }
            if (applied)
            {
                groundContactsAfter=(std::max)(groundContactsAfter,groundContacts);
                if (tick==kickTick) { awakeAfterKick=asleep==0; Check(awakeAfterKick,"G8 post-step wake"); }
            }
            Check(traceCount<g8Trace.size(), "G8 presentation trace overflow");
            if (traceCount<g8Trace.size()) g8Trace[traceCount++]={tick,asleep,groundContacts,groundMask,app.m_ChainImpulse.AppliedCount()};
        }
        static void BeginG8(RigidBodySimulationApp& app)
        {
            using namespace GEngine;
            auto& w=*app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            Check(app.m_Launch.chainLinks==8 && w.GetBodyCount()==10 && w.GetJointCount()==8,"G8 membership");
            if (failure) return;
            for (unsigned i=0;i<g8Bodies.size();++i) g8Bodies[i]=*w.BodyAt(i);
            Check(app.m_ChainImpulse.Enabled() && app.m_ChainImpulse.Target()==g8Bodies[8],"G8 terminal binding");
            const auto ground=w.ReadBody(g8Bodies[9]);
            Check(ground && ground->type==Component::BodyType::Static && ground->shapeType==ShapeType::Box &&
                ground->worldBounds.mins==Math::Vec3f(-50,-.5f,-50) &&
                ground->worldBounds.maxs==Math::Vec3f(50,.5f,50),"G8 ground geometry");
            Check(PhysicsDiagnostic::observer==nullptr,"G8 diagnostic observer conflict");
            if (failure) return;
            contactObserver.context=&app;
            contactObserver.boundary=[](void* context,PhysicsDiagnostic::Stage stage,const PhysicsWorld& world,
                ManifoldCollector& manifolds,std::span<const ContactIsland>,std::size_t,int) noexcept {
                ObserveG8(*static_cast<RigidBodySimulationApp*>(context),stage,world,manifolds);
            };
            PhysicsDiagnostic::observer=&contactObserver;
        }
        static void WriteG8Trace()
        {
            auto* file=std::fopen((output/"g8-ticks.csv").string().c_str(),"wx");
            Check(file!=nullptr,"G8 trace output");
            if (!file) return;
            std::fprintf(file,"tick,asleep,ground_contacts,ground_link_mask,manual_applications\n");
            for (unsigned i=0;i<traceCount;++i)
            {
                const auto& t=g8Trace[i];
                std::fprintf(file,"%llu,%u,%u,%u,%u\n",static_cast<unsigned long long>(t.tick),t.asleep,t.contacts,t.mask,t.applications);
            }
            std::fclose(file);
            file=std::fopen((output/"g8-handles.csv").string().c_str(),"wx");
            Check(file!=nullptr,"G8 identity output");
            if (!file) return;
            std::fprintf(file,"role,ordinal,index,generation,registry\n");
            for (unsigned i=0;i<g8Bodies.size();++i)
                std::fprintf(file,"%s,%u,%u,%llu,%llu\n",i==0?"static_root":i==9?"static_ground":"dynamic_link",i,
                    g8Bodies[i].index,static_cast<unsigned long long>(g8Bodies[i].generation),static_cast<unsigned long long>(g8Bodies[i].registry));
            std::fclose(file);
        }
        static void OnUI(RigidBodySimulationApp& app)
        {
            if (reported) return;
            ++frames;
            auto* physics = app.m_ActiveScene->GetPhysicsSystem();
            auto& w = *physics->GetPhysicsWorld();
            if (frames==1) g8=app.m_Launch.scene==RbsScenePreset::DistanceChainContact;
            Check(frames<=(g8?1800u:240u) && physics->GetCompletedSteps()<=120, "presentation budget exceeded");
            if (frames==1)
            {
                Check(g8 || (app.m_Launch.scene==RbsScenePreset::DistanceChain && app.m_Launch.chainLinks==16),
                    "expected Owner C16 enum default");
                Check(app.m_ChainImpulseMagnitude==2 && app.m_ChainImpulseDirection==0 &&
                    app.m_ChainImpulse.Enabled() && app.m_ChainImpulse.Target()==*w.BodyAt(app.m_Launch.chainLinks), "initial controls/target");
                if (g8) BeginG8(app);
            }
            if (g8 && !slept) Check(physics->GetCompletedSteps()<80,"G8 did not naturally settle with ground contacts");
            if ((!g8 && frames==1) || (g8 && slept && pauseFrame==0))
            {
                app.m_IsPause=true; SetColliderUserPause(*app.m_ActiveScene,true);
                pausedSteps=physics->GetCompletedSteps();
                pausedVelocity=w.ReadBody(app.m_ChainImpulse.Target())->linearVelocity;
                if (g8) { app.m_ChainImpulseMagnitude=5; app.m_ChainImpulseDirection=2; }
                Check(app.RequestDistanceChainImpulse(), "UI request rejected");
                Check(!app.RequestDistanceChainImpulse(), "duplicate UI request accepted");
                app.m_ChainImpulseMagnitude=g8?2.f:5.f; app.m_ChainImpulseDirection=3;
                pauseFrame=frames;
            }
            else if (pauseFrame && frames<=pauseFrame+3)
            {
                Check(physics->GetCompletedSteps()==pausedSteps && app.m_ChainImpulse.Pending() &&
                    w.ReadBody(app.m_ChainImpulse.Target())->linearVelocity==pausedVelocity, "paused world changed");
                if (frames==pauseFrame+3)
                { app.m_IsPause=false; SetColliderUserPause(*app.m_ActiveScene,false); }
            }
            const bool complete=boundaries>=(g8?30u:3u) && panels;
            if (complete)
            {
                Check(app.m_ChainImpulse.AppliedCount()==1 &&
                    app.m_ChainImpulse.LastApplied()==(g8?GEngine::Math::Vec3f(0,0,5):GEngine::Math::Vec3f(2,0,0)), "single-shot or captured payload");
                if (g8) Check(slept && wakeObserved && awakeAfterKick && groundContactsAfter>0,"G8 sleep/wake/contact evidence");
            }
            if (failure || complete)
            {
                if (g8)
                {
                    if (GEngine::PhysicsDiagnostic::observer==&contactObserver) GEngine::PhysicsDiagnostic::observer=nullptr;
                    WriteG8Trace();
                }
                auto* file=std::fopen((output/"result.json").string().c_str(),"wx");
                if (file)
                {
                    std::fprintf(file,"{\"presentation_only\":true,\"pass\":%s,\"failure\":\"%s\",\"frames\":%u,\"panels\":%u,\"ticks\":%llu,\"boundaries\":%u,\"applied_count\":%u,\"g8\":%s,\"sleep_tick\":%llu,\"kick_tick\":%llu,\"wake_tick\":%llu,\"all_awake_at_wake\":%s,\"all_awake_after_kick\":%s,\"post_kick_ground_contacts\":%u}\n",
                        failure?"false":"true",failure?failure:"",frames,panels,
                        static_cast<unsigned long long>(physics->GetCompletedSteps()),boundaries,app.m_ChainImpulse.AppliedCount(),g8?"true":"false",
                        static_cast<unsigned long long>(sleepTick),static_cast<unsigned long long>(kickTick),
                        static_cast<unsigned long long>(wakeTick),wakeObserved?"true":"false",awakeAfterKick?"true":"false",groundContactsAfter);
                    std::fclose(file);
                }
                reported=true; app.ShutDown();
            }
        }
#endif
    };
}
#endif
#ifdef GENGINE_PHASE26_QUALIFICATION
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsProfile.h"
#include "Scene/_Scene.h"
#include "Core/BaseApp.h"
#include "LaunchConfigChecks.h"
#include "RestingContactDiagnosis.h" // Reuse the existing authoritative test-only body adapter.
#include <cstdio>
#include <filesystem>
#include <memory>
#include <array>
#include <Windows.h>
#include <malloc.h>
#include <new.h>
#include <cstddef>
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
#include <Jolt/Physics/QualificationObservation.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Math/Vec3.h>
#include <atomic>
#include <cstring>
#endif

#ifdef JPH_GENGINE_PHASE26_OBSERVATION
#if defined(GENGINE_G8_RESISTANCE_CHECKS)
namespace Rbs::BackendChecks::ResistanceChecks {extern std::atomic<std::uint64_t> AllocCalls,AllocBytes;}
#endif
namespace Rbs::Phase26JoltRecorder
{
    namespace Q = JPH::QualificationObservation;

    // One update is drained on the owner thread before these bounded slots are reused.
    // All worker callbacks reserve a slot atomically and copy a POD, without a lock,
    // allocation, retained SDK pointer, exception, or output operation.
    struct Recorder final
    {
        std::array<Q::PointRecord,4096> points{};
        std::array<Q::EventKind,4096> pointKind{};
        std::array<std::uint32_t,4096> pointWorker{};
        std::array<Q::CCDRecord,2048> ccd{};
        std::array<Q::EventKind,2048> ccdKind{};
        std::array<std::uint32_t,2048> ccdWorker{};
        std::array<Q::LifetimeRecord,4096> lifetimes{};
        std::array<std::uint32_t,4096> lifetimeWorker{};
        std::array<Q::WorkRecord,2048> work{};
        std::array<std::uint32_t,2048> workWorker{};
        std::array<Q::PhaseRecord,2048> phases{};
        std::array<std::uint32_t,2048> phaseWorker{};
        std::array<Q::StorageRecord,2048> storage{};
        std::array<std::uint32_t,2048> storageWorker{};
        std::array<Q::HeapRecord,1024> heap{};
        std::array<std::uint32_t,1024> heapWorker{};
        std::array<Q::FailureRecord,64> failures{};
        std::array<std::uint32_t,64> failureWorker{};
        std::array<Q::IdentityRecord,64> identities{};
        std::array<std::uint32_t,64> identityWorker{};
        struct LiveHeapEntry {std::uint64_t pointer;std::uint32_t bytes,flags;};
        std::array<LiveHeapEntry,4096> liveHeap{};
        std::uint64_t heapLiveBytes{},heapPeakBytes{},heapAllocationCalls{},heapAllocationBytes{},heapFreeCalls{},heapFreeBytes{},unmatchedLifetimeFrees{};
        std::uint64_t arenaReserveCalls{},arenaReserveBytes{},arenaReleaseCalls{},arenaReleaseBytes{},arenaHighWater{};
        std::uint32_t unclassifiedArenaCount{};
        std::uint32_t processedHeapCount{};
        std::array<std::uint64_t,32> storageCapacityBytes{};
        std::array<std::uint32_t,32> storageCapacityCounts{};
        std::uint32_t processedStorageCount{};
        std::uint64_t baseJobPoolEpoch{},baseJobPoolPointer{},baseJobPoolTotalRequests{},heapValidatedUpdates{},heapOwnershipFailures{};
        std::uint32_t baseJobPoolEpochRequests{},baseJobPoolWorker{},heapBodySlotsMax{};
        bool heapOwnershipValid{};
        std::atomic<std::uint32_t> pointCount{},ccdCount{},lifetimeCount{},workCount{},phaseCount{},storageCount{},heapCount{},failureCount{},identityCount{};
        std::array<std::atomic<std::uint32_t>,32> workerIds{};
        std::atomic<bool> overflow{},invalid{};
        Q::Sink sink{};
        std::uint64_t lastEpoch{},lastUpdate{};
        bool installed{};

        Recorder() noexcept {sink.user=this;sink.publish=&Publish;}
        Recorder(const Recorder&)=delete;
        Recorder& operator=(const Recorder&)=delete;
        static_assert(sizeof(LiveHeapEntry)==16);

        template<class T,std::size_t N>
        void Append(std::array<T,N>& slots,std::array<std::uint32_t,N>& workers,
            std::atomic<std::uint32_t>& count,const void* data,std::uint32_t bytes,std::uint32_t worker) noexcept
        {
            if(data==nullptr || bytes!=sizeof(T)){invalid.store(true,std::memory_order_relaxed);return;}
            const auto index=count.fetch_add(1,std::memory_order_relaxed);
            if(index>=N){overflow.store(true,std::memory_order_relaxed);return;}
            std::memcpy(&slots[index],data,sizeof(T));
            workers[index]=worker;
        }
        std::uint32_t Worker() noexcept
        {
            const auto id=GetCurrentThreadId();
            for(auto& slot:workerIds){
                auto existing=slot.load(std::memory_order_relaxed);
                if(existing==id)return id;
                if(existing==0 && slot.compare_exchange_strong(existing,id,std::memory_order_relaxed))return id;
            }
            overflow.store(true,std::memory_order_relaxed);
            return id;
        }
        static std::uint64_t HashContext(const Q::Context& context,std::uint32_t worker,std::uint32_t kind) noexcept
        {
            std::uint64_t hash=14695981039346656037ull;
            const auto mix=[&](std::uint64_t value){hash^=value;hash*=1099511628211ull;};
            mix(std::uint64_t(reinterpret_cast<std::uintptr_t>(context.world)));
            mix(context.epoch);mix(context.update);mix(context.substep);
            mix(static_cast<std::uint32_t>(context.phase));mix(static_cast<std::uint32_t>(context.site));
            mix(worker);mix(kind);
            return hash;
        }
        static bool SameContext(const Q::Context& a,const Q::Context& b) noexcept
        {return a.world==b.world && a.epoch==b.epoch && a.update==b.update &&
            a.substep==b.substep && a.phase==b.phase && a.site==b.site;}
        void AppendWork(const void* data,std::uint32_t bytes,std::uint32_t worker) noexcept
        {
            if(data==nullptr || bytes!=sizeof(Q::WorkRecord)){invalid=true;return;}
            Q::WorkRecord incoming;std::memcpy(&incoming,data,sizeof(incoming));
            std::size_t slot=HashContext(incoming.context,worker,static_cast<std::uint32_t>(incoming.kind))&(work.size()-1);
            for(std::size_t probe=0;probe<work.size();++probe,slot=(slot+1)&(work.size()-1)){
                std::atomic_ref<std::uint32_t> owner(workWorker[slot]);
                const auto existing=owner.load(std::memory_order_acquire);
                if(existing==worker){
                    auto& previous=work[slot];
                    if(previous.kind!=incoming.kind || !SameContext(previous.context,incoming.context))continue;
                    if(UINT32_MAX-previous.count<incoming.count || UINT32_MAX-previous.changed<incoming.changed ||
                        UINT32_MAX-previous.root_iterations<incoming.root_iterations){invalid=true;return;}
                    previous.count+=incoming.count;previous.changed+=incoming.changed;
                    previous.root_iterations+=incoming.root_iterations;return;
                }
                if(existing==0){
                    std::uint32_t empty=0;
                    if(owner.compare_exchange_strong(empty,worker,std::memory_order_acq_rel,std::memory_order_relaxed)){
                        work[slot]=incoming;workCount.fetch_add(1,std::memory_order_relaxed);return;
                    }
                    continue;
                }
            }
            overflow=true;
        }
        void AppendPhase(const void* data,std::uint32_t bytes,std::uint32_t worker) noexcept
        {
            if(data==nullptr || bytes!=sizeof(Q::PhaseRecord)){invalid=true;return;}
            Q::PhaseRecord incoming;std::memcpy(&incoming,data,sizeof(incoming));
            std::size_t slot=HashContext(incoming.context,worker,static_cast<std::uint32_t>(incoming.phase))&(phases.size()-1);
            for(std::size_t probe=0;probe<phases.size();++probe,slot=(slot+1)&(phases.size()-1)){
                std::atomic_ref<std::uint32_t> owner(phaseWorker[slot]);
                const auto existing=owner.load(std::memory_order_acquire);
                if(existing==worker){
                    auto& previous=phases[slot];
                    if(previous.phase!=incoming.phase || !SameContext(previous.context,incoming.context))continue;
                    if(UINT32_MAX-previous.calls<incoming.calls || UINT64_MAX-previous.elapsed_ns<incoming.elapsed_ns){
                        invalid=true;return;
                    }
                    previous.calls+=incoming.calls;previous.elapsed_ns+=incoming.elapsed_ns;return;
                }
                if(existing==0){
                    std::uint32_t empty=0;
                    if(owner.compare_exchange_strong(empty,worker,std::memory_order_acq_rel,std::memory_order_relaxed)){
                        phases[slot]=incoming;phaseCount.fetch_add(1,std::memory_order_relaxed);return;
                    }
                    continue;
                }
            }
            overflow=true;
        }
        template<class T,std::size_t N>
        static void CompactAggregates(std::array<T,N>& records,std::array<std::uint32_t,N>& workers,
            std::atomic<std::uint32_t>& count) noexcept
        {
            std::uint32_t dense=0;
            for(std::size_t i=0;i<N;++i){
                const auto worker=std::atomic_ref<std::uint32_t>(workers[i]).load(std::memory_order_acquire);
                if(!worker)continue;
                if(dense!=i){records[dense]=records[i];workers[dense]=worker;}
                ++dense;
            }
            count.store(dense,std::memory_order_release);
        }
        static void Publish(void* user,Q::EventKind kind,const void* data,JPH::uint32 bytes) noexcept
        {
            auto& r=*static_cast<Recorder*>(user);
            const auto worker=r.Worker();
            switch(kind){
            case Q::EventKind::Point:
            case Q::EventKind::CCDManifoldPoint:{
                if(data==nullptr || bytes!=sizeof(Q::PointRecord)){r.invalid.store(true,std::memory_order_relaxed);break;}
                const auto index=r.pointCount.fetch_add(1,std::memory_order_relaxed);
                if(index>=r.points.size()){r.overflow.store(true,std::memory_order_relaxed);break;}
                std::memcpy(&r.points[index],data,bytes);r.pointKind[index]=kind;r.pointWorker[index]=worker;break;
            }
            case Q::EventKind::CCDCandidate:
            case Q::EventKind::CCDAccepted:{
                if(data==nullptr || bytes!=sizeof(Q::CCDRecord)){r.invalid.store(true,std::memory_order_relaxed);break;}
                const auto index=r.ccdCount.fetch_add(1,std::memory_order_relaxed);
                if(index>=r.ccd.size()){r.overflow.store(true,std::memory_order_relaxed);break;}
                std::memcpy(&r.ccd[index],data,bytes);r.ccdKind[index]=kind;r.ccdWorker[index]=worker;break;
            }
            case Q::EventKind::RetiredUnpersisted:
            case Q::EventKind::CacheLifecycle:r.Append(r.lifetimes,r.lifetimeWorker,r.lifetimeCount,data,bytes,worker);break;
            case Q::EventKind::Work:r.AppendWork(data,bytes,worker);break;
            case Q::EventKind::Phase:r.AppendPhase(data,bytes,worker);break;
            case Q::EventKind::Storage:r.Append(r.storage,r.storageWorker,r.storageCount,data,bytes,worker);break;
            case Q::EventKind::Heap:r.Append(r.heap,r.heapWorker,r.heapCount,data,bytes,worker);break;
            case Q::EventKind::Failure:r.Append(r.failures,r.failureWorker,r.failureCount,data,bytes,worker);r.invalid.store(true,std::memory_order_relaxed);break;
            case Q::EventKind::Identity:r.Append(r.identities,r.identityWorker,r.identityCount,data,bytes,worker);break;
            }
        }
        void ResetUpdate() noexcept
        {
            pointCount=ccdCount=lifetimeCount=workCount=phaseCount=storageCount=heapCount=failureCount=identityCount=0;
            workWorker.fill(0);phaseWorker.fill(0);
            for(auto& slot:workerIds)slot.store(0,std::memory_order_relaxed);
            overflow=false;invalid=false;
            processedHeapCount=0;
            processedStorageCount=0;heapOwnershipValid=false;
        }
        bool ProcessCapacity() noexcept
        {
            const auto end=storageCount.load();
            if(end>storage.size()){invalid=true;return false;}
            for(std::uint32_t i=processedStorageCount;i<end;++i){
                const auto& event=storage[i];
                if(event.action!=Q::StorageAction::Capacity)continue;
                const auto site=static_cast<std::size_t>(event.context.site);
                if(site>=storageCapacityBytes.size()){invalid=true;return false;}
                storageCapacityBytes[site]=(std::max)(storageCapacityBytes[site],event.aligned_bytes);
                storageCapacityCounts[site]=(std::max)(storageCapacityCounts[site],event.count);
            }
            processedStorageCount=end;
            return true;
        }
        bool ValidateHeapOwnership() noexcept
        {
            heapOwnershipValid=false;
            const auto count=heapCount.load(),capacities=storageCount.load();
            const auto fail=[this]() noexcept {++heapOwnershipFailures;invalid=true;return false;};
            if(count>heap.size() || capacities>storage.size() || !sink.substeps ||
                !Q::kBaseJobSlotBytes || Q::kBaseJobPoolBytes!=Q::kBaseJobSlotBytes*JPH::cMaxPhysicsJobs ||
                Q::kBroadPhaseNodeBytes!=sizeof(JPH::BodyID) || Q::kBroadPhaseCenterBytes!=sizeof(JPH::Vec3))return fail();
            const auto current=[this](const Q::Context& context) noexcept {
                return context.world==sink.world && context.epoch==sink.epoch &&
                    context.update==sink.update && context.substep<sink.substeps;
            };
            bool sourceUpdate=false;std::uint32_t baseCapacities=0;
            for(std::uint32_t i=0;i<capacities;++i){
                const auto& cap=storage[i];
                if(cap.action!=Q::StorageAction::Capacity ||
                    (cap.context.site!=Q::ArenaSite::BaseJobPool && cap.context.site!=Q::ArenaSite::BroadPhaseBodyStorage))continue;
                if(!current(cap.context) || !storageWorker[i] || cap.pointer || cap.requested_bytes!=cap.aligned_bytes)return fail();
                if(cap.context.site==Q::ArenaSite::BaseJobPool){
                    if(cap.context.phase!=Q::Phase::Update || cap.context.substep ||
                        cap.count!=static_cast<std::uint32_t>(JPH::cMaxPhysicsJobs) || cap.requested_bytes!=Q::kBaseJobPoolBytes ||
                        ++baseCapacities>sink.substeps)return fail();
                }else{
                    if(cap.context.phase!=Q::Phase::SharedBroadPhase || cap.count>32 ||
                        cap.requested_bytes!=std::uint64_t(cap.count)*(Q::kBroadPhaseNodeBytes+Q::kBroadPhaseCenterBytes))return fail();
                    for(std::uint32_t j=0;j<i;++j)if(storage[j].action==Q::StorageAction::Capacity &&
                        storage[j].context.site==Q::ArenaSite::BroadPhaseBodyStorage &&
                        storage[j].context.substep==cap.context.substep)return fail();
                    heapBodySlotsMax=(std::max)(heapBodySlotsMax,cap.count);
                }
                sourceUpdate=true;
            }
            for(std::uint32_t i=0;i<count;++i){
                const auto& event=heap[i];const auto phase=event.context.phase;const auto site=event.context.site;
                const bool request=event.action==Q::HeapAction::Allocation || event.action==Q::HeapAction::Reallocation ||
                    event.action==Q::HeapAction::AlignedAllocation;
                if(!current(event.context) || !heapWorker[i])return fail();
                const bool jointPhase=phase>=Q::Phase::JointFilter && phase<=Q::Phase::JointPositionSolve;
                const bool jointSite=site==Q::ArenaSite::JointContainer || site==Q::ArenaSite::AdapterJointStorage ||
                    site==Q::ArenaSite::AdapterFilterStorage || site==Q::ArenaSite::IslandJointLinks ||
                    site==Q::ArenaSite::IslandJointIslands || site==Q::ArenaSite::IslandJointIslandEnds;
                if(request && (jointPhase||jointSite))return fail();
                if(site==Q::ArenaSite::BaseJobPool || site==Q::ArenaSite::BroadPhaseBodyStorage){
                    const Q::StorageRecord* capacity=nullptr;
                    for(std::uint32_t j=0;j<capacities;++j){
                        const auto& cap=storage[j];
                        if(cap.action==Q::StorageAction::Capacity && SameContext(cap.context,event.context) && storageWorker[j]==heapWorker[i]){
                            if(capacity && (capacity->count!=cap.count || capacity->requested_bytes!=cap.requested_bytes))return fail();
                            capacity=&cap;
                        }
                    }
                    if(!capacity)return fail();
                    if(site==Q::ArenaSite::BaseJobPool){
                        if(event.action!=Q::HeapAction::AlignedAllocation || !event.pointer || event.old_pointer || event.old_bytes ||
                            event.requested_bytes!=Q::kBaseJobPoolBytes || event.pointer%JPH_CACHE_LINE_SIZE ||
                            baseJobPoolEpoch!=event.context.epoch ||
                            baseJobPoolEpochRequests || baseJobPoolPointer || heapWorker[i]!=GetCurrentThreadId())return fail();
                        ++baseJobPoolEpochRequests;++baseJobPoolTotalRequests;
                        baseJobPoolPointer=event.pointer;baseJobPoolWorker=heapWorker[i];
                    }else if(request){
                        if(event.action!=Q::HeapAction::Allocation || !event.pointer || event.old_pointer || event.old_bytes ||
                            !event.requested_bytes || !capacity->count || event.requested_bytes>
                            std::uint64_t(capacity->count)*(std::max)(Q::kBroadPhaseNodeBytes,Q::kBroadPhaseCenterBytes))return fail();
                        std::uint32_t requests=0;std::uint64_t bytes=0,nodeRequest=0,centerRequest=0;
                        for(std::uint32_t j=0;j<count;++j){
                            const auto& other=heap[j];
                            if(other.context.site==site && other.context.substep==event.context.substep &&
                                (other.action==Q::HeapAction::Allocation || other.action==Q::HeapAction::Reallocation ||
                                 other.action==Q::HeapAction::AlignedAllocation)){
                                if(!SameContext(other.context,event.context) || heapWorker[j]!=heapWorker[i])return fail();
                                ++requests;bytes+=other.requested_bytes;
                                if(requests==1)nodeRequest=other.requested_bytes;
                                else if(requests==2)centerRequest=other.requested_bytes;
                            }
                        }
                        if(requests>2 || bytes>capacity->requested_bytes || !nodeRequest ||
                            nodeRequest%Q::kBroadPhaseNodeBytes || nodeRequest/Q::kBroadPhaseNodeBytes>capacity->count ||
                            (requests==2 && (centerRequest<2*Q::kBroadPhaseCenterBytes ||
                             centerRequest%Q::kBroadPhaseCenterBytes ||
                             centerRequest/Q::kBroadPhaseCenterBytes>nodeRequest/Q::kBroadPhaseNodeBytes)))return fail();
                        bool released=false;
                        for(std::uint32_t j=i+1;j<count;++j)if(heap[j].pointer==event.pointer){
                            const auto& release=heap[j];
                            if(release.action!=Q::HeapAction::Free || release.requested_bytes || release.old_pointer || release.old_bytes ||
                                !SameContext(release.context,event.context) || heapWorker[j]!=heapWorker[i])return fail();
                            released=true;break;
                        }
                        if(!released)return fail();
                    }else{
                        if(event.action!=Q::HeapAction::Free || !event.pointer || event.requested_bytes || event.old_pointer || event.old_bytes)return fail();
                        bool matched=false;
                        for(std::uint32_t j=i;j>0;){--j;if(heap[j].pointer==event.pointer){
                            if(heap[j].action!=Q::HeapAction::Allocation || !SameContext(heap[j].context,event.context) ||
                                heapWorker[j]!=heapWorker[i])return fail();
                            matched=true;break;
                        }}
                        if(!matched)return fail();
                    }
                }else if(request && (phase==Q::Phase::None || phase==Q::Phase::Update || phase==Q::Phase::SharedBroadPhase ||
                    phase==Q::Phase::SharedNarrowPhase || phase==Q::Phase::SharedIslandBuild || phase==Q::Phase::SharedStepWork))return fail();
            }
            if(sourceUpdate)++heapValidatedUpdates;
            heapOwnershipValid=true;return true;
        }
        bool ProcessHeap() noexcept
        {
            const auto end=heapCount.load();
            if(end>heap.size()){invalid=true;return false;}
            const auto find=[this](std::uint64_t pointer)->LiveHeapEntry* {
                for(auto& entry:liveHeap)if(entry.pointer==pointer && pointer)return &entry;
                return nullptr;
            };
            const auto allocate=[&](std::uint64_t pointer,std::uint64_t bytes,Q::ArenaSite site)->bool {
                if(!pointer)return true;
                if(bytes>UINT32_MAX || find(pointer))return false;
                for(auto& entry:liveHeap)if(!entry.pointer){entry={pointer,std::uint32_t(bytes),static_cast<std::uint32_t>(site)};heapLiveBytes+=bytes;
                    heapPeakBytes=(std::max)(heapPeakBytes,heapLiveBytes);return true;}
                return false;
            };
            const auto release=[&](std::uint64_t pointer,bool lifetime,Q::HeapAction action,std::uint32_t worker,const Q::Context& context)->bool {
                if(!pointer)return true;
                auto* entry=find(pointer);
                if(!entry){if(lifetime)++unmatchedLifetimeFrees;return false;}
                if(entry->flags==static_cast<std::uint32_t>(Q::ArenaSite::BaseJobPool)){
                    if(!lifetime || action!=Q::HeapAction::AlignedFree || context.epoch!=baseJobPoolEpoch ||
                        context.world!=sink.world || context.substep!=~std::uint32_t(0) ||
                        worker!=baseJobPoolWorker || pointer!=baseJobPoolPointer)return false;
                    baseJobPoolPointer=0;
                }
                heapLiveBytes-=entry->bytes;heapFreeBytes+=entry->bytes;*entry={};return true;
            };
            for(std::uint32_t i=processedHeapCount;i<end;++i){
                const auto& event=heap[i];
                const bool lifetime=event.context.update==0 && event.context.phase==Q::Phase::Lifetime;
                switch(event.action){
                case Q::HeapAction::Allocation:
                case Q::HeapAction::AlignedAllocation:
                    ++heapAllocationCalls;heapAllocationBytes+=event.requested_bytes;
                    if(!allocate(event.pointer,event.requested_bytes,event.context.site)){invalid=true;return false;}
                    break;
                case Q::HeapAction::Reallocation:
                    ++heapAllocationCalls;heapAllocationBytes+=event.requested_bytes;
                    if(event.pointer){
                        if(!release(event.old_pointer,lifetime,event.action,heapWorker[i],event.context) || !allocate(event.pointer,event.requested_bytes,event.context.site)){invalid=true;return false;}
                    }
                    break;
                case Q::HeapAction::Free:
                case Q::HeapAction::AlignedFree:
                    ++heapFreeCalls;
                    if(!release(event.pointer,lifetime,event.action,heapWorker[i],event.context)){invalid=true;return false;}
                    break;
                }
            }
            processedHeapCount=end;
            return true;
        }
        bool ValidateArena() noexcept
        {
            struct Entry {std::uint64_t pointer,requested,aligned;};
            std::array<Entry,512> stack{};
            std::size_t depth=0;
            std::uint64_t usage=0;
            const auto count=storageCount.load();
            if(count>storage.size()){invalid=true;return false;}
            for(std::uint32_t i=0;i<count;++i){
                const auto& event=storage[i];
                if(event.context.site!=Q::ArenaSite::Unknown || event.action==Q::StorageAction::Capacity)continue;
                const auto rounded=event.requested_bytes?((event.requested_bytes+15u)&~std::uint64_t(15u)):0;
                if(event.aligned_bytes!=rounded){invalid=true;return false;}
                if(event.action==Q::StorageAction::Reserve){
                    ++arenaReserveCalls;arenaReserveBytes+=event.aligned_bytes;
                    if(event.requested_bytes){
                        if(!event.pointer || depth==stack.size()){invalid=true;return false;}
                        stack[depth++]={event.pointer,event.requested_bytes,event.aligned_bytes};
                        usage+=event.aligned_bytes;
                    }else if(event.pointer){invalid=true;return false;}
                }else if(event.action==Q::StorageAction::Release){
                    ++arenaReleaseCalls;arenaReleaseBytes+=event.aligned_bytes;
                    if(event.requested_bytes){
                        if(!depth || stack[depth-1].pointer!=event.pointer ||
                            stack[depth-1].requested!=event.requested_bytes ||
                            stack[depth-1].aligned!=event.aligned_bytes){invalid=true;return false;}
                        --depth;usage-=event.aligned_bytes;
                    }else if(event.pointer){invalid=true;return false;}
                }else {invalid=true;return false;}
                if(usage!=event.arena_usage_after || usage>33554432){invalid=true;return false;}
                arenaHighWater=(std::max)(arenaHighWater,usage);
                if(event.requested_bytes){
                    bool classified=false;
                    for(std::uint32_t k=0;k<count;++k){
                        const auto& named=storage[k];
                        if(named.context.site==Q::ArenaSite::Unknown || named.action!=event.action)continue;
                        if(named.pointer==event.pointer && named.requested_bytes==event.requested_bytes &&
                            named.aligned_bytes==event.aligned_bytes && named.context.update==event.context.update &&
                            named.context.substep==event.context.substep){classified=true;break;}
                    }
                    if(!classified){++unclassifiedArenaCount;invalid=true;return false;}
                }
            }
            if(depth || usage){invalid=true;return false;}
            return true;
        }
        bool InstallLifetime(std::uint64_t epoch) noexcept
        {
            if(!epoch)return false;
            if(installed && Q::GetSink()==nullptr){installed=false;sink.world=nullptr;}
            if(installed && Q::GetSink()!=&sink)return false;
            if(!installed){
                if(baseJobPoolPointer)return false;
                baseJobPoolEpoch=epoch;baseJobPoolEpochRequests=0;baseJobPoolWorker=0;
                sink.epoch=epoch;sink.update=0;sink.substeps=0;
                if(!Q::InstallSink(&sink))return false;installed=true;}
            return sink.epoch==epoch;
        }
        bool Prepare(const JPH::PhysicsSystem* world,std::uint64_t epoch,std::uint64_t update,std::uint32_t substeps) noexcept
        {
            if(!epoch || !update || !substeps || !InstallLifetime(epoch))return false;
            if(world){if(sink.world && sink.world!=world)return false;sink.world=world;}
            if(lastEpoch==epoch && update<=lastUpdate)return false;
            if(!ProcessHeap() || !ProcessCapacity())return false;
            ResetUpdate();sink.update=update;sink.substeps=substeps;lastEpoch=epoch;lastUpdate=update;
            return true;
        }
        bool FinishUpdate() noexcept
        {
            if(!installed || Q::GetSink()!=&sink || !sink.update){invalid=true;return false;}
            CompactAggregates(work,workWorker,workCount);
            CompactAggregates(phases,phaseWorker,phaseCount);
            ValidateArena();
            ProcessCapacity();
            ValidateHeapOwnership();
            ProcessHeap();
            sink.update=0;sink.substeps=0;
            return Valid();
        }
        bool Valid() const noexcept {return !overflow.load() && !invalid.load() && failureCount.load()==0;}
        std::uint64_t JointLiveBytes() const noexcept
        {std::uint64_t bytes=0;for(const auto& entry:liveHeap)if(entry.pointer &&
            entry.flags==static_cast<std::uint32_t>(Q::ArenaSite::JointContainer))bytes+=entry.bytes;return bytes;}
        std::uint32_t JointLiveCount() const noexcept
        {std::uint32_t count=0;for(const auto& entry:liveHeap)if(entry.pointer &&
            entry.flags==static_cast<std::uint32_t>(Q::ArenaSite::JointContainer))++count;return count;}
        std::uint32_t WorkerCount() const noexcept
        {std::uint32_t n=0;for(const auto& slot:workerIds)n+=slot.load()!=0;return n;}
        bool Detach() noexcept
        {
            if(!installed)return Valid();
            ProcessHeap();ProcessCapacity();
            if(baseJobPoolPointer)invalid=true;
            const bool owned=Q::GetSink()==&sink;
            const bool removed=owned && Q::RemoveSink(&sink);
            installed=false;sink.world=nullptr;sink.update=0;sink.substeps=0;
            return owned && removed && Valid();
        }
    };
    inline Recorder& Get() noexcept {static Recorder value;return value;}
    static_assert(sizeof(Recorder)+4096*96<=2*1024*1024);

    // Direct SDK fixtures own a separate RegisterDefaultAllocator lifetime. The
    // wrappers remain installed until its world, workers and Factory have retired.
#if defined(GENGINE_G8_RESISTANCE_CHECKS)
    inline JPH::AllocateFunction priorAllocate{};
    inline JPH::ReallocateFunction priorReallocate{};
    inline JPH::FreeFunction priorFree{};
    inline JPH::AlignedAllocateFunction priorAlignedAllocate{};
    inline JPH::AlignedFreeFunction priorAlignedFree{};
    inline std::atomic<bool> countDirect{};
    inline std::atomic<bool> callbacksRestored{true};
#endif
    inline void EmitHeap(Q::HeapAction action,void* value,void* old,std::size_t bytes,std::size_t oldBytes=0) noexcept
    {
        auto context=Q::CurrentContext();
        if(auto* sink=Q::GetSink();sink && sink->update && !context.update){
            context.world=sink->world;context.epoch=sink->epoch;context.update=sink->update;
            context.substep=UINT32_MAX;context.phase=Q::Phase::SharedStepWork;
        }
        Q::HeapRecord record{context,std::uint64_t(reinterpret_cast<std::uintptr_t>(value)),
            std::uint64_t(reinterpret_cast<std::uintptr_t>(old)),bytes,oldBytes,action};
        if(Q::GetSink())Q::PublishSynthetic(record.context,Q::EventKind::Heap,&record,sizeof(record));
    }
#if defined(GENGINE_G8_RESISTANCE_CHECKS)
    inline void* Allocate(std::size_t n)
    {void* p=priorAllocate(n);if(countDirect){++::Rbs::BackendChecks::ResistanceChecks::AllocCalls;::Rbs::BackendChecks::ResistanceChecks::AllocBytes+=n;}EmitHeap(Q::HeapAction::Allocation,p,nullptr,n);return p;}
    inline void* Reallocate(void* old,std::size_t oldBytes,std::size_t n)
    {void* p=priorReallocate(old,oldBytes,n);if(countDirect){++::Rbs::BackendChecks::ResistanceChecks::AllocCalls;::Rbs::BackendChecks::ResistanceChecks::AllocBytes+=n;}EmitHeap(Q::HeapAction::Reallocation,p,old,n,oldBytes);return p;}
    inline void Free(void* p) noexcept {EmitHeap(Q::HeapAction::Free,p,nullptr,0);priorFree(p);}
    inline void* AlignedAllocate(std::size_t n,std::size_t alignment)
    {void* p=priorAlignedAllocate(n,alignment);if(countDirect){++::Rbs::BackendChecks::ResistanceChecks::AllocCalls;::Rbs::BackendChecks::ResistanceChecks::AllocBytes+=n;}EmitHeap(Q::HeapAction::AlignedAllocation,p,nullptr,n);return p;}
    inline void AlignedFree(void* p) noexcept {EmitHeap(Q::HeapAction::AlignedFree,p,nullptr,0);priorAlignedFree(p);}
    struct AllocatorScope
    {
        bool owned{};
        AllocatorScope() noexcept
        {
            callbacksRestored=false;
            priorAllocate=JPH::Allocate;priorReallocate=JPH::Reallocate;priorFree=JPH::Free;
            priorAlignedAllocate=JPH::AlignedAllocate;priorAlignedFree=JPH::AlignedFree;
            JPH::Allocate=&Allocate;JPH::Reallocate=&Reallocate;JPH::Free=&Free;
            JPH::AlignedAllocate=&AlignedAllocate;JPH::AlignedFree=&AlignedFree;owned=true;
        }
        void Begin() noexcept {::Rbs::BackendChecks::ResistanceChecks::AllocCalls=0;::Rbs::BackendChecks::ResistanceChecks::AllocBytes=0;countDirect=true;}
        void End() noexcept {countDirect=false;}
        ~AllocatorScope()
        {
            countDirect=false;
            if(owned && JPH::Allocate==&Allocate && JPH::Reallocate==&Reallocate && JPH::Free==&Free &&
                JPH::AlignedAllocate==&AlignedAllocate && JPH::AlignedFree==&AlignedFree){
                JPH::Allocate=priorAllocate;JPH::Reallocate=priorReallocate;JPH::Free=priorFree;
                JPH::AlignedAllocate=priorAlignedAllocate;JPH::AlignedFree=priorAlignedFree;
                callbacksRestored=true;
            }else Get().invalid=true;
        }
        static bool CallbacksRestored() noexcept {return callbacksRestored.load();}
    };
#endif
}
#endif

namespace Rbs::Phase26Memory {
    struct Event { unsigned tick{},lastTick{},stage{},depth{}; std::uint64_t occurrences{}; std::size_t bytes{}; bool joint{}; void* stack[12]{}; };
    inline std::array<Event,1024> events;
    inline unsigned eventCount{};
    inline bool overflow{};
    inline thread_local bool enabled{};
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    inline std::atomic<bool> captureAll{};
#endif
    inline thread_local unsigned tick{},stage{};
    inline thread_local std::uint64_t count{},bytes{},jointCount{};
    inline void Record(std::size_t n) noexcept {
        if(!enabled)return;
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        ++count;bytes+=n;
#else
        ++count;bytes+=n;if(::GEngine::Phase26Observation::jointHot)++jointCount;
        Event next;next.tick=next.lastTick=tick;next.stage=stage;next.bytes=n;next.occurrences=1;
        next.joint=::GEngine::Phase26Observation::jointHot;
        next.depth=CaptureStackBackTrace(2,12,next.stack,nullptr);
        for(unsigned i=0;i<eventCount;++i){auto& e=events[i];
            if(e.stage!=next.stage || e.bytes!=next.bytes || e.joint!=next.joint || e.depth!=next.depth)continue;
            bool same=true;for(unsigned k=0;k<e.depth;++k)same &= e.stack[k]==next.stack[k];
            if(same){++e.occurrences;e.lastTick=tick;return;}}
        if(eventCount==events.size()){overflow=true;return;}
        events[eventCount++]=next;
#endif
    }
    inline void* Allocate(std::size_t n,std::size_t alignment=0) {
        for(;;){auto* p=alignment?_aligned_malloc((std::max)(n,std::size_t(1)),alignment):std::malloc((std::max)(n,std::size_t(1)));
            if(p){Record(n);
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
                if(captureAll.load(std::memory_order_relaxed))
                    ::Rbs::Phase26JoltRecorder::EmitHeap(::Rbs::Phase26JoltRecorder::Q::HeapAction::Allocation,p,nullptr,n);
#endif
                return p;}if(!_callnewh(n))throw std::bad_alloc();}
    }
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    inline void RecordFree(void* p,bool aligned) noexcept {
        if(p && captureAll.load(std::memory_order_relaxed))::Rbs::Phase26JoltRecorder::EmitHeap(aligned?::Rbs::Phase26JoltRecorder::Q::HeapAction::AlignedFree:
            ::Rbs::Phase26JoltRecorder::Q::HeapAction::Free,p,nullptr,0);
    }
#endif
}
#ifdef GENGINE_PHASE26_ALLOC_IMPLEMENTATION
__declspec(noinline) void* operator new(std::size_t n){return Rbs::Phase26Memory::Allocate(n);}
__declspec(noinline) void* operator new[](std::size_t n){return Rbs::Phase26Memory::Allocate(n);}
void operator delete(void* p) noexcept{
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    Rbs::Phase26Memory::RecordFree(p,false);
#endif
    std::free(p);}
void operator delete[](void* p) noexcept{
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    Rbs::Phase26Memory::RecordFree(p,false);
#endif
    std::free(p);}
void operator delete(void* p,std::size_t) noexcept{
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    Rbs::Phase26Memory::RecordFree(p,false);
#endif
    std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    Rbs::Phase26Memory::RecordFree(p,false);
#endif
    std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept{
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    Rbs::Phase26Memory::RecordFree(p,false);
#endif
    std::free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept{
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    Rbs::Phase26Memory::RecordFree(p,false);
#endif
    std::free(p);}
void* operator new(std::size_t n,std::align_val_t a){return Rbs::Phase26Memory::Allocate(n,std::size_t(a));}
void* operator new[](std::size_t n,std::align_val_t a){return Rbs::Phase26Memory::Allocate(n,std::size_t(a));}
void operator delete(void* p,std::align_val_t) noexcept{
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    Rbs::Phase26Memory::RecordFree(p,true);
#endif
    _aligned_free(p);}
void operator delete[](void* p,std::align_val_t) noexcept{
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    Rbs::Phase26Memory::RecordFree(p,true);
#endif
    _aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept{
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    Rbs::Phase26Memory::RecordFree(p,true);
#endif
    _aligned_free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept{
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    Rbs::Phase26Memory::RecordFree(p,true);
#endif
    _aligned_free(p);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept{try{return ::operator new[](n,a);}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept{
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    Rbs::Phase26Memory::RecordFree(p,true);
#endif
    _aligned_free(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept{
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    Rbs::Phase26Memory::RecordFree(p,true);
#endif
    _aligned_free(p);}
#endif

namespace Rbs::Phase26
{
    using namespace ::GEngine;
    enum class ContactKind { Invalid, Ground, NonGround };
    inline ContactKind ClassifyContact(const PhysicsWorld& world, PhysicsBodyHandle ground,
                                      RigidBodyIdentity a, RigidBodyIdentity b)
    {
        const auto groundState=world.ReadBody(ground);
        if(!groundState || groundState->type!=Component::BodyType::Static || a==b ||
            !world.IsBodyIdentityValid(a) || !world.IsBodyIdentityValid(b))return ContactKind::Invalid;
        PhysicsBodyHandle ha{},hb{};
        for(const auto* body:PhysicsTestAccess::Bodies(world)) {
            if(body->GetIdentity()==a)ha=PhysicsTestAccess::Handle(world,*body);
            if(body->GetIdentity()==b)hb=PhysicsTestAccess::Handle(world,*body);
        }
        if(!world.IsBodyValid(ha) || !world.IsBodyValid(hb))return ContactKind::Invalid;
        return ha==ground || hb==ground ? ContactKind::Ground : ContactKind::NonGround;
    }
    inline bool CheckContactIdentities()
    {
        PhysicsWorld world,foreign;
        const auto create=[](PhysicsWorld& w,Component::BodyType type) {
            auto shape=CreateCollisionShape(SphereShapeDesc{1});
            if(!shape)return PhysicsBodyHandle{};
            PhysicsBodyDesc desc;desc.type=type;
            auto body=w.CreateBody(desc,*shape);return body?*body:PhysicsBodyHandle{};
        };
        const auto ground=create(world,Component::BodyType::Static);
        const auto linkA=create(world,Component::BodyType::Dynamic);
        const auto linkB=create(world,Component::BodyType::Dynamic);
        const auto other=create(foreign,Component::BodyType::Static);
        if(!ground || !linkA || !linkB || !other)return false;
        const auto identity=[](PhysicsWorld& w,PhysicsBodyHandle h) {
            const auto* body=PhysicsTestAccess::Resolve(w,h);
            return body?body->GetIdentity():RigidBodyIdentity{};
        };
        const auto g=identity(world,ground),a=identity(world,linkA),b=identity(world,linkB),f=identity(foreign,other);
        if(ClassifyContact(world,ground,g,a)!=ContactKind::Ground ||
            ClassifyContact(world,ground,a,g)!=ContactKind::Ground ||
            ClassifyContact(world,ground,a,b)!=ContactKind::NonGround ||
            ClassifyContact(world,ground,{},a)!=ContactKind::Invalid ||
            ClassifyContact(world,ground,f,a)!=ContactKind::Invalid ||
            ClassifyContact(world,other,g,a)!=ContactKind::Invalid ||
            ClassifyContact(world,linkA,g,b)!=ContactKind::Invalid)return false;
        if(!world.DestroyBody(linkB) || ClassifyContact(world,ground,g,b)!=ContactKind::Invalid)return false;
        const auto replacement=create(world,Component::BodyType::Dynamic);
        if(!replacement || replacement.index!=linkB.index || replacement.generation==linkB.generation ||
            ClassifyContact(world,ground,g,b)!=ContactKind::Invalid ||
            ClassifyContact(world,ground,g,identity(world,replacement))!=ContactKind::Ground)return false;
        if(!world.DestroyBody(ground) || ClassifyContact(world,ground,g,a)!=ContactKind::Invalid)return false;
        return bool(world.Close()) && bool(foreign.Close());
    }
    struct ContactWitness
    {
        double depth{};
        std::uint16_t bodyA{},bodyB{}; // Ordinals into this sample's full world-owned handles.
    };
    static_assert(sizeof(ContactWitness)==16);
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    struct CompactBody
    {
        PhysicsBodyHandle handle{};
        Component::BodyType type{};
        Vec3f position{},linearVelocity{},angularVelocity{};
        Quat orientation{};
        float inverseMass{};
        Vec3f centerOfMass{};
        bool sleeping{};
        static CompactBody From(const PhysicsBodyState& b) noexcept
        {return {b.handle,b.type,b.position,b.linearVelocity,b.angularVelocity,b.orientation,b.inverseMass,b.centerOfMass,b.sleeping};}
    };
#endif
    struct Sample
    {
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        std::array<CompactBody,18> bodies;
#else
        std::array<PhysicsBodyState,18> bodies;
#endif
        std::array<float,16> errors{};
        std::array<float,16> impulses{};
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        struct WorkTriple {std::uint64_t count{},changed{},rootIterations{};};
        static constexpr std::size_t WorkKinds=static_cast<std::size_t>(Phase26JoltRecorder::Q::WorkKind::SolverBatch)+1;
        std::array<WorkTriple,WorkKinds> work{};
#else
        std::array<std::uint64_t,12> work{};
#endif
        double rawEnergy{}, fidelity{}, geometry{}, witness{}, maxError{};
        std::uint64_t inclusiveNs{}, jointNs{}, contactNs{};
        std::uint64_t updateNs{}, observationNs{}, allocations{}, allocationBytes{}, jointAllocations{};
        unsigned asleep{};
        unsigned contacts{}, nonGroundContacts{};
        // G8 admits eight ground pairs; the existing manifold retains at most
        // four witnesses per pair. Never truncate evidence on cap overflow.
        std::array<ContactWitness,32> witnesses{};
    };
    struct Session
    {
        std::array<Sample,1201> samples;
        unsigned tick{}, target{1200}, count{}, links{};
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        std::uint32_t substeps{};
#endif
        bool contact{}, ordinary{}, minimal{}, failed{}, attached{};
        const char* failure{"none"};
        std::filesystem::path output;
        PhysicsSystem* physics{};
        BaseApp* app{};
        double work{};
        PhysicsDiagnostic::Observer observer;
        Phase26Observation::Observer completeObserver;
        ManifoldCollector* manifolds{};
        std::size_t jointStorageBytes{};
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        std::size_t adapterJointStorageBytes{},adapterFilterStorageBytes{},joltJointLiveBytes{};
        std::uint32_t joltJointLiveCount{};
#endif
        bool lifecycleAfter{};
        FILE* csv{};
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        FILE* jolt{};
        std::uint32_t exportedJoltHeap{},exportedJoltStorage{},exportedJoltLifetime{},exportedJoltFailure{},exportedJoltIdentity{};
#endif
        ~Session(){ if(csv)std::fclose(csv);
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
            if(jolt)std::fclose(jolt);
#endif
        }
        void Fail(const char* why) { if(!failed){failed=true;failure=why;} if(app)app->ShutDown(); }
    };
    inline std::unique_ptr<Session> session;
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
    inline FILE* OpenObservationEvidenceFile(const char* path,const char* mode) noexcept
    {
        FILE* file=nullptr;
        return ::fopen_s(&file,path,mode)==0?file:nullptr;
    }
    enum class JoltFrameKind : std::uint32_t {Construction,Update,Interstice,Teardown};
    struct JoltFrameHeader
    {
        std::uint64_t magic{0x3142534F36325048ull}; // HP26OSB1, little endian
        std::uint64_t epoch{},update{};
        std::uint32_t schema{2},substeps{},workerThreads{},valid{},kind{};
        std::uint32_t pointCount{},ccdCount{},lifetimeCount{},workCount{},phaseCount{},storageCount{},heapCount{},failureCount{},identityCount{};
    };
    static_assert(std::is_trivially_copyable_v<JoltFrameHeader>);
    inline bool WriteJoltFrame(Session& s,JoltFrameKind kind) noexcept
    {
        auto& r=Phase26JoltRecorder::Get();
        const bool update=kind==JoltFrameKind::Update;
        const bool delta=kind==JoltFrameKind::Interstice || kind==JoltFrameKind::Teardown;
        JoltFrameHeader h;h.epoch=r.sink.epoch;h.update=update?s.tick:0;
        h.kind=static_cast<std::uint32_t>(kind);
        h.substeps=update?s.substeps:0;
        h.workerThreads=r.WorkerCount();h.valid=r.Valid();
        h.pointCount=update?r.pointCount.load():0;h.ccdCount=update?r.ccdCount.load():0;
        h.lifetimeCount=r.lifetimeCount.load()-(delta?s.exportedJoltLifetime:0);
        h.workCount=update?r.workCount.load():0;h.phaseCount=update?r.phaseCount.load():0;
        h.storageCount=r.storageCount.load()-(delta?s.exportedJoltStorage:0);
        h.heapCount=r.heapCount.load()-(delta?s.exportedJoltHeap:0);
        h.failureCount=r.failureCount.load()-(delta?s.exportedJoltFailure:0);
        h.identityCount=r.identityCount.load()-(delta?s.exportedJoltIdentity:0);
        if(delta && kind==JoltFrameKind::Interstice && !h.lifetimeCount && !h.storageCount &&
            !h.heapCount && !h.failureCount && !h.identityCount)return true;
        if(h.pointCount>r.points.size() || h.ccdCount>r.ccd.size() || h.lifetimeCount>r.lifetimes.size() ||
            h.workCount>r.work.size() || h.phaseCount>r.phases.size() || h.storageCount>r.storage.size() ||
            h.heapCount>r.heap.size() || h.failureCount>r.failures.size() || h.identityCount>r.identities.size())return false;
        const std::uint64_t extra=sizeof(h)+h.pointCount*(sizeof(r.points[0])+sizeof(r.pointKind[0])+sizeof(r.pointWorker[0]))+
            h.ccdCount*(sizeof(r.ccd[0])+sizeof(r.ccdKind[0])+sizeof(r.ccdWorker[0]))+
            h.lifetimeCount*(sizeof(r.lifetimes[0])+sizeof(r.lifetimeWorker[0]))+
            h.workCount*(sizeof(r.work[0])+sizeof(r.workWorker[0]))+
            h.phaseCount*(sizeof(r.phases[0])+sizeof(r.phaseWorker[0]))+
            h.storageCount*(sizeof(r.storage[0])+sizeof(r.storageWorker[0]))+
            h.heapCount*(sizeof(r.heap[0])+sizeof(r.heapWorker[0]))+
            h.failureCount*(sizeof(r.failures[0])+sizeof(r.failureWorker[0]))+
            h.identityCount*(sizeof(r.identities[0])+sizeof(r.identityWorker[0]));
        const auto before=std::ftell(s.jolt);
        if(before<0 || std::uint64_t(before)+extra>64ull*1024*1024)return false;
        const auto write=[&](const void* data,std::size_t stride,std::size_t count){return std::fwrite(data,stride,count,s.jolt)==count;};
        const bool ok=write(&h,sizeof(h),1) && write(r.points.data(),sizeof(r.points[0]),h.pointCount) &&
            write(r.pointKind.data(),sizeof(r.pointKind[0]),h.pointCount) && write(r.pointWorker.data(),sizeof(r.pointWorker[0]),h.pointCount) &&
            write(r.ccd.data(),sizeof(r.ccd[0]),h.ccdCount) && write(r.ccdKind.data(),sizeof(r.ccdKind[0]),h.ccdCount) &&
            write(r.ccdWorker.data(),sizeof(r.ccdWorker[0]),h.ccdCount) &&
            write(r.lifetimes.data()+(delta?s.exportedJoltLifetime:0),sizeof(r.lifetimes[0]),h.lifetimeCount) &&
            write(r.lifetimeWorker.data()+(delta?s.exportedJoltLifetime:0),sizeof(r.lifetimeWorker[0]),h.lifetimeCount) &&
            write(r.work.data(),sizeof(r.work[0]),h.workCount) && write(r.workWorker.data(),sizeof(r.workWorker[0]),h.workCount) &&
            write(r.phases.data(),sizeof(r.phases[0]),h.phaseCount) && write(r.phaseWorker.data(),sizeof(r.phaseWorker[0]),h.phaseCount) &&
            write(r.storage.data()+(delta?s.exportedJoltStorage:0),sizeof(r.storage[0]),h.storageCount) &&
            write(r.storageWorker.data()+(delta?s.exportedJoltStorage:0),sizeof(r.storageWorker[0]),h.storageCount) &&
            write(r.heap.data()+(delta?s.exportedJoltHeap:0),sizeof(r.heap[0]),h.heapCount) &&
            write(r.heapWorker.data()+(delta?s.exportedJoltHeap:0),sizeof(r.heapWorker[0]),h.heapCount) &&
            write(r.failures.data()+(delta?s.exportedJoltFailure:0),sizeof(r.failures[0]),h.failureCount) &&
            write(r.failureWorker.data()+(delta?s.exportedJoltFailure:0),sizeof(r.failureWorker[0]),h.failureCount) &&
            write(r.identities.data()+(delta?s.exportedJoltIdentity:0),sizeof(r.identities[0]),h.identityCount) &&
            write(r.identityWorker.data()+(delta?s.exportedJoltIdentity:0),sizeof(r.identityWorker[0]),h.identityCount);
        if(ok){s.exportedJoltLifetime=r.lifetimeCount.load();s.exportedJoltStorage=r.storageCount.load();
            s.exportedJoltHeap=r.heapCount.load();s.exportedJoltFailure=r.failureCount.load();
            s.exportedJoltIdentity=r.identityCount.load();}
        return ok;
    }
#endif
    inline bool Configure(std::vector<std::string_view>& args)
    {
        // Only the isolated qualification binary admits these explicit options.
        auto found=std::find(args.begin(),args.end(),"--phase26-output");
        if(found==args.end() || found+1==args.end())return false;
        auto owned=std::make_unique<Session>(); owned->output=std::string(*(found+1));
        args.erase(found,found+2);
        if(!owned->output.is_absolute() || !std::filesystem::is_directory(owned->output) ||
            std::filesystem::exists(owned->output/"result.json"))return false;
        auto mode=std::find(args.begin(),args.end(),"--phase26-mode");
        if(mode==args.end() || mode+1==args.end())return false;
        if(*(mode+1)=="debug")owned->target=120;
        else if(*(mode+1)=="ordinary") {owned->target=420;owned->ordinary=true;}
        else if(*(mode+1)=="minimal")owned->minimal=true;
        else if(*(mode+1)!="full")return false;
        args.erase(mode,mode+2);
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        owned->csv=OpenObservationEvidenceFile((owned->output/"ticks.csv").string().c_str(),"wx");
#else
        owned->csv=std::fopen((owned->output/"ticks.csv").string().c_str(),"wx");
#endif
        if(!owned->csv)return false;
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        owned->jolt=OpenObservationEvidenceFile((owned->output/"jolt-observation.bin").string().c_str(),"wbx");
        if(!owned->jolt)return false;
        if(!Phase26JoltRecorder::Get().InstallLifetime(1))return false;
#endif
        std::fprintf(owned->csv,"tick,raw_energy,fidelity,geometry,witness,max_error,asleep,joint_ns,contact_ns\n");
        std::fflush(owned->csv);
        session=std::move(owned); return true;
    }
    inline void CaptureWork(PhysicsWorld& w,PhysicsBodyHandle h,const Vec3f& impulse)
    {
        if(!session)return;
        auto b=w.ReadBody(h);if(!b){session->Fail("impulse state missing");return;}
        session->work=glm::dot(glm::dvec3(impulse),glm::dvec3(b->linearVelocity))+
            .5*glm::dot(glm::dvec3(impulse),glm::dvec3(impulse))*b->inverseMass;
    }
    inline bool Lifecycle()
    {
        auto parser=PreEditorValidation::CheckLaunchConfig(); if(!parser)return false;
        if (!DistanceChainImpulseChecks::Unit()) return false;
        Phase26Memory::enabled=true;
        void* allocation=::operator new(37);
        void* aligned=::operator new(65,std::align_val_t(64));
        Phase26Memory::enabled=false;
        ::operator delete(allocation);::operator delete(aligned,std::align_val_t(64));
        if(Phase26Memory::count!=2 || Phase26Memory::bytes!=102)return false;
        Phase26Memory::count=Phase26Memory::bytes=Phase26Memory::jointCount=0;Phase26Memory::eventCount=0;
        for(unsigned cycle=0;cycle<2;++cycle) {
        PhysicsWorld world, foreign;
        auto shapeA=CreateCollisionShape(SphereShapeDesc{1}),shapeB=CreateCollisionShape(SphereShapeDesc{1});
        if(!shapeA || !shapeB)return false;
        PhysicsBodyDesc a; a.type=Component::BodyType::Static;
        PhysicsBodyDesc b; b.position={0,-2.25f,0};
        auto ha=world.CreateBody(a,*shapeA),hb=world.CreateBody(b,*shapeB);if(!ha || !hb)return false;
        DistanceJointDesc d{*ha,*hb,{},{},2.25f,false};
        auto joint=world.CreateDistanceJoint(d);if(!joint || !world.ReadDistanceJoint(*joint))return false;
        if(foreign.ReadDistanceJoint(*joint) || foreign.DestroyJoint(*joint))return false;
        auto invalid=d; invalid.bodyB=*ha;if(world.CreateDistanceJoint(invalid))return false;
        invalid=d;invalid.length=0;if(world.CreateDistanceJoint(invalid))return false;
        invalid=d;invalid.length=std::numeric_limits<float>::quiet_NaN();if(world.CreateDistanceJoint(invalid))return false;
        if(!world.DestroyJoint(*joint) || world.ReadDistanceJoint(*joint) || world.DestroyJoint(*joint))return false;
        auto next=world.CreateDistanceJoint(d);if(!next || *next==*joint)return false;
        auto duplicate=world.CreateDistanceJoint(d);if(!duplicate)return false;
        if(!world.DestroyJoint(*next) || !world.ReadDistanceJoint(*duplicate))return false;
        if(!world.DestroyBody(*hb) || world.GetJointCount()!=0 || world.ReadDistanceJoint(*duplicate))return false;
        if(!world.Close() || world.CreateDistanceJoint(d))return false;
        }
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        // The focused lifecycle worlds have retired; arm the later canonical
        // scene's independent Jolt acquisition before its construction begins.
        if(!Phase26JoltRecorder::Get().InstallLifetime(1))return false;
#endif
        return true;
    }
    inline void Evaluate(unsigned tick)
    {
        auto& s=*session;auto& out=s.samples[tick];
        if(s.ordinary)return;
        out.rawEnergy=out.fidelity=out.geometry=out.maxError=0;out.asleep=0;
        for(unsigned i=1;i<=s.links;++i) {
            const auto& b=out.bodies[i];out.asleep+=b.sleeping;
            const glm::dvec3 v(b.linearVelocity),omega(b.angularVelocity);
            const double k=.5*glm::dot(v,v)+.2*glm::dot(omega,omega);
            out.rawEnergy+=k+12*double(b.position.y);out.fidelity+=k;
            const auto difference=glm::dvec3(b.position)-glm::dvec3(out.bodies[i-1].position);
            out.maxError=std::max(out.maxError,std::abs(glm::length(difference)-2.25));
            if(s.contact) {
                out.fidelity+=12*std::max(double(b.position.y)-1.5,0.0);
                const auto& ground=out.bodies[s.links+1];
                const auto local=glm::conjugate(glm::dquat(ground.orientation))*(glm::dvec3(b.position)-glm::dvec3(ground.position));
                const auto q=glm::abs(local)-glm::dvec3(50,.5,50);
                const double signedDistance=glm::length(glm::max(q,glm::dvec3(0)))+std::min(std::max({q.x,q.y,q.z}),0.);
                out.geometry=std::max(out.geometry,std::max(0.,1.-signedDistance));
            } else out.fidelity+=27*(s.links-i+1)*(1+glm::normalize(difference).y);
        }
        for(unsigned i=0;i<=s.links;++i)for(unsigned j=i+2;j<=s.links;++j)
            if(glm::length(glm::dvec3(out.bodies[i].position)-glm::dvec3(out.bodies[j].position))<=2.)s.Fail("nonadjacent overlap");
        const bool rest=tick<=300 || (s.contact && tick>=420);
        const double allowance=tick<=300?.00125:.0128125,work=tick>300?s.work:0;
        if(out.maxError>(rest?.01125:.0225))s.Fail("joint length limit");
        if(out.geometry>.04 || ((tick>=121 && tick<=300 || s.contact && tick>=421) && out.geometry>.02))s.Fail("geometric penetration");
        if(out.rawEnergy-s.samples[0].rawEnergy-work>allowance)s.Fail("raw mechanical energy");
        if((s.contact || tick>120) && out.fidelity-work>allowance)s.Fail("intended geometry energy");
        if((tick>=120 && tick<=300 || s.contact && tick>=420) && out.asleep!=s.links)s.Fail("settling/sleep deadline");
        if(tick==301 && out.asleep!=0)s.Fail("connected wake");
        if(out.nonGroundContacts)s.Fail("contact domain");
        if(out.jointAllocations)s.Fail("joint hot allocation");
    }
    inline void Capture(const PhysicsWorld& w
#ifndef JPH_GENGINE_PHASE26_OBSERVATION
        ,ManifoldCollector& manifolds
#endif
        )
    {
        auto& s=*session;
        if(s.tick>=s.target){s.Fail("too many ticks");return;}
        ++s.tick; auto& out=s.samples[s.tick];
        if(s.physics->HasJointFailure()){s.Fail("joint solver fault");return;}
        if(w.GetBodyCount()!=s.count || (!s.ordinary && w.GetJointCount()!=s.links)) {s.Fail("membership");return;}
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        auto& recorder=Phase26JoltRecorder::Get();
        for(std::uint32_t i=0;i<recorder.workCount.load() && i<recorder.work.size();++i){
            const auto& event=recorder.work[i];const auto index=static_cast<std::size_t>(event.kind);
            if(index>=out.work.size()){s.Fail("Jolt work kind range");return;}
            out.work[index].count+=event.count;out.work[index].changed+=event.changed;
            out.work[index].rootIterations+=event.root_iterations;
        }
#else
        const auto p=GetPhysicsProfileSnapshot();
        out.work={p.stepCount,p.candidatePairCount,p.pairFilterCheckCount,p.pairFilterRejectedCount,p.narrowphaseCallCount,
            p.generatedContactCount,p.manifoldContactCount,p.solverIterationCount,p.integratedBodyCount,
            p.spinResistanceSolveCount,p.rollingResistanceSolveCount,p.contactIslandCapacityGrowthCount};
#endif
        for(unsigned i=0;i<s.count;++i) {
            auto h=w.BodyAt(i);if(!h){s.Fail("body identity");return;}
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
            auto b=w.ReadBody(*h);if(!b){s.Fail("body finite state");return;} if(!s.ordinary){
                out.bodies[i]=CompactBody::From(*b);
            }
#else
            auto b=w.ReadBody(*h);if(!b){s.Fail("body finite state");return;} if(!s.ordinary)
                out.bodies[i]=*b;
#endif
        }
        if(!s.ordinary)for(unsigned i=0;i<s.links;++i){
            auto h=w.JointAt(i);if(!h){s.Fail("joint identity");return;}
            auto j=w.ReadDistanceJoint(*h);if(!j){s.Fail("joint finite state");return;}
            out.errors[i]=j->error;out.impulses[i]=j->accumulatedImpulse;
        }
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        // The SDK publishes the solver's cached COM-local anchors. Resolve its
        // complete BodyID values against the adapter's post-Update managed map,
        // then transform with the completed physical body state used by Evaluate.
        const auto findBody=[&](std::uint32_t sdkId)->unsigned {
            for(std::uint32_t j=0;j<recorder.identityCount.load() && j<recorder.identities.size();++j){
                const auto& map=recorder.identities[j];
                if(map.action!=Phase26JoltRecorder::Q::IdentityAction::Snapshot || map.sdk_body_id!=sdkId)continue;
                for(unsigned k=0;k<s.count;++k)if(out.bodies[k].handle.index==map.managed_index &&
                    out.bodies[k].handle.generation==map.managed_generation &&
                    out.bodies[k].handle.registry==map.managed_registry)return k;
            }
            return s.count;
        };
        for(std::uint32_t i=0;i<recorder.pointCount.load() && i<recorder.points.size();++i){
            const auto& point=recorder.points[i];
            const auto kind=recorder.pointKind[i];
            if(kind==Phase26JoltRecorder::Q::EventKind::CCDManifoldPoint &&
                point.space==Phase26JoltRecorder::Q::AnchorSpace::WorldAtImpact)continue;
            if(point.space!=Phase26JoltRecorder::Q::AnchorSpace::COMLocal ||
                (kind==Phase26JoltRecorder::Q::EventKind::CCDManifoldPoint && point.ordinal!=UINT32_MAX) ||
                point.context.update!=s.tick || point.context.epoch!=1 ||
                point.context.world!=recorder.sink.world || point.context.substep>=s.substeps){
                s.Fail("Jolt solved contact point context");return;
            }
            ++out.contacts;
            if(s.ordinary)continue;
            if(out.contacts>out.witnesses.size()){s.Fail("Jolt contact witness recorder cap");return;}
            const unsigned ia=findBody(point.body1),ib=findBody(point.body2);
            if(ia==s.count || ib==s.count || ia==ib ||
                !w.IsBodyValid(out.bodies[ia].handle) || !w.IsBodyValid(out.bodies[ib].handle)){
                s.Fail("Jolt contact point body map");return;
            }
            const glm::dvec3 local1(point.anchor1[0],point.anchor1[1],point.anchor1[2]);
            const glm::dvec3 local2(point.anchor2[0],point.anchor2[1],point.anchor2[2]);
            const glm::dvec3 normal(point.solver_world_normal[0],point.solver_world_normal[1],point.solver_world_normal[2]);
            if(!std::isfinite(local1.x) || !std::isfinite(local1.y) || !std::isfinite(local1.z) ||
                !std::isfinite(local2.x) || !std::isfinite(local2.y) || !std::isfinite(local2.z) ||
                !std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) ||
                !std::isfinite(point.normal_lambda)){
                s.Fail("Jolt contact point nonfinite");return;
            }
            const glm::dvec3 a=glm::dvec3(out.bodies[ia].centerOfMass)+glm::dquat(out.bodies[ia].orientation)*local1;
            const glm::dvec3 b=glm::dvec3(out.bodies[ib].centerOfMass)+glm::dquat(out.bodies[ib].orientation)*local2;
            const double depth=glm::dot(a-b,normal);
            if(!std::isfinite(depth)){s.Fail("Jolt completed witness nonfinite");return;}
            out.witness=(std::max)(out.witness,depth);
            out.witnesses[out.contacts-1]={depth,static_cast<std::uint16_t>(ia),static_cast<std::uint16_t>(ib)};
            const bool ground=(ia==s.links+1 && ib>=1 && ib<=s.links) ||
                (ib==s.links+1 && ia>=1 && ia<=s.links);
            if(!s.contact || !ground)++out.nonGroundContacts;
        }
        if(!s.ordinary)std::sort(out.witnesses.begin(),out.witnesses.begin()+out.contacts,
            [](const ContactWitness& a,const ContactWitness& b) noexcept {
                if(a.bodyA!=b.bodyA)return a.bodyA<b.bodyA;
                if(a.bodyB!=b.bodyB)return a.bodyB<b.bodyB;
                return a.depth<b.depth;
            });
#else
        for(auto& m:manifolds.m_Manifolds)for(int i=0;i<m.GetNumContacts();++i){
            auto c=m.GetContact(i);
            ++out.contacts;
            if(!s.ordinary) {
                if(!w.IsBodyIdentityValid(m.GetBodyAIdentity()) || !w.IsBodyIdentityValid(m.GetBodyBIdentity())) {
                    s.Fail("invalid contact body identity");return;
                }
                const auto kind=s.contact?ClassifyContact(w,out.bodies[s.links+1].handle,
                    m.GetBodyAIdentity(),m.GetBodyBIdentity()):ContactKind::NonGround;
                if(kind==ContactKind::Invalid){s.Fail("invalid ground/contact identity");return;}
                if(kind==ContactKind::NonGround)++out.nonGroundContacts;
            }
            const auto a=c.m_BodyA->BodySpaceToWorldSpace(c.ptOnA_LocalSpace),b=c.m_BodyB->BodySpaceToWorldSpace(c.ptOnB_LocalSpace);
            const double depth=double(-glm::dot(a-b,c.normal));
            out.witness=std::max(out.witness,depth);
            if(!s.ordinary) {
                if(out.contacts>out.witnesses.size()){s.Fail("contact witness recorder cap");return;}
                const auto ha=PhysicsTestAccess::Handle(w,*c.m_BodyA),hb=PhysicsTestAccess::Handle(w,*c.m_BodyB);
                unsigned ia=s.count,ib=s.count;
                for(unsigned k=0;k<s.count;++k) {
                    if(out.bodies[k].handle==ha)ia=k;
                    if(out.bodies[k].handle==hb)ib=k;
                }
                if(ia==s.count || ib==s.count){s.Fail("contact witness identity missing");return;}
                out.witnesses[out.contacts-1]={depth,static_cast<std::uint16_t>(ia),static_cast<std::uint16_t>(ib)};
            }
        }
#endif
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        for(std::uint32_t i=0;i<recorder.phaseCount.load() && i<recorder.phases.size();++i){
            const auto& phase=recorder.phases[i];
            if(phase.phase>=Phase26JoltRecorder::Q::Phase::JointFilter &&
                phase.phase<=Phase26JoltRecorder::Q::Phase::JointPositionSolve)out.jointNs+=phase.elapsed_ns;
            if((phase.phase>=Phase26JoltRecorder::Q::Phase::ContactCachedPreparation &&
                phase.phase<=Phase26JoltRecorder::Q::Phase::ContactRetirement) ||
                (phase.phase>=Phase26JoltRecorder::Q::Phase::CCDPreparation &&
                 phase.phase<=Phase26JoltRecorder::Q::Phase::CCDAcceptance))out.contactNs+=phase.elapsed_ns;
        }
#else
        out.jointNs=s.physics->GetJointSolveNanoseconds();
        out.contactNs=s.physics->GetContactSolveNanoseconds();
#endif
    }
    inline void Attach(_Scene& scene,PhysicsSystem& physics,BaseApp& app,const LaunchConfig& launch)
    {
        if(!session)return;
        auto& s=*session;s.physics=&physics;s.app=&app;s.links=launch.chainLinks;
        if(!CheckContactIdentities()){s.Fail("contact identity classifier checks");return;}
        s.contact=launch.scene==RbsScenePreset::DistanceChainContact;
        s.ordinary=!launch.IsDistanceScene();
        auto& w=*physics.GetPhysicsWorld();s.count=static_cast<unsigned>(w.GetBodyCount());s.attached=true;
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        const auto settings=w.ReadJoltSettings();
        if(!settings || settings->collisionSteps<1){s.Fail("Jolt observation settings");return;}
        s.substeps=static_cast<std::uint32_t>(settings->collisionSteps);
#endif
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        static_assert(sizeof(Session)+sizeof(Phase26Memory::events)+sizeof(Phase26JoltRecorder::Recorder)+4096*96<=8388608);
#else
        s.jointStorageBytes=physics.QualificationJointStorageBytes();
        static_assert(sizeof(Session)+sizeof(Phase26Memory::events)<8388608);
#endif
        if(physics.GetSolverIterations()!=1 || scene.GetPhysicsStep().count()!=1.0/60.0) {s.Fail("fixed scheduler/pass policy");return;}
        if(!s.ordinary && (s.count!=s.links+1+(s.contact?1:0) || w.GetJointCount()!=s.links)) {s.Fail("initial membership");return;}
        if(!s.ordinary)for(unsigned i=0;i<s.count;++i){
            auto h=w.BodyAt(i);if(!h){s.Fail("initial identity");return;}
            auto b=w.ReadBody(*h);if(!b){s.Fail("initial state");return;}
            if(i>0 && i<=s.links && (b->inverseMass!=1.f || b->shapeType!=ShapeType::Sphere ||
                b->material!=PhysicsMaterial{.5f,.5f,.05f,.05f} ||
                b->localBounds.mins!=Vec3f(-1) || b->localBounds.maxs!=Vec3f(1))) {s.Fail("initial physical configuration");return;}
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
            s.samples[0].bodies[i]=CompactBody::From(*b);
#else
            s.samples[0].bodies[i]=*b;
#endif
            if(i>0 && i<=s.links)s.samples[0].rawEnergy+=12*double(b->position.y);
        }
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        auto& initialRecorder=Phase26JoltRecorder::Get();
        if(!initialRecorder.ProcessHeap() || !initialRecorder.ProcessCapacity() ||
            !WriteJoltFrame(s,JoltFrameKind::Construction) || !initialRecorder.Valid()){
            s.Fail("Jolt construction lifetime export");return;
        }
        s.adapterJointStorageBytes=initialRecorder.storageCapacityBytes[static_cast<std::size_t>(Phase26JoltRecorder::Q::ArenaSite::AdapterJointStorage)];
        s.adapterFilterStorageBytes=initialRecorder.storageCapacityBytes[static_cast<std::size_t>(Phase26JoltRecorder::Q::ArenaSite::AdapterFilterStorage)];
        s.joltJointLiveBytes=initialRecorder.JointLiveBytes();
        s.joltJointLiveCount=initialRecorder.JointLiveCount();
        s.jointStorageBytes=s.adapterJointStorageBytes+s.adapterFilterStorageBytes+s.joltJointLiveBytes;
#endif
        s.observer.context=&s;
        s.observer.boundary=[](void*,PhysicsDiagnostic::Stage stage,const PhysicsWorld& world,ManifoldCollector& m,
            std::span<const ContactIsland>,std::size_t,int) noexcept {
            Phase26Memory::stage=static_cast<unsigned>(stage);
            if(stage==PhysicsDiagnostic::Stage::End)session->manifolds=&m;
        };
        PhysicsDiagnostic::observer=&s.observer;
        s.completeObserver.context=&s;
        s.completeObserver.begin=[](void*) noexcept {
            ResetPhysicsProfile();Phase26Memory::tick=session->tick+1;Phase26Memory::stage=0;
            Phase26Memory::count=Phase26Memory::bytes=Phase26Memory::jointCount=0;Phase26Memory::enabled=true;
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
            auto& s=*session;
            auto& recorder=Phase26JoltRecorder::Get();
            if(s.tick && !WriteJoltFrame(s,JoltFrameKind::Interstice))s.Fail("Jolt interstep lifetime export");
            if(!recorder.Prepare(nullptr,1,s.tick+1,s.substeps))s.Fail("Jolt observation arm");
            s.exportedJoltLifetime=s.exportedJoltStorage=s.exportedJoltHeap=s.exportedJoltFailure=s.exportedJoltIdentity=0;
            Phase26Memory::captureAll.store(true,std::memory_order_release);
#endif
        };
        s.completeObserver.end=[](void*,std::uint64_t ns) noexcept {
            Phase26Memory::enabled=false;
            auto& s=*session;
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
            Phase26Memory::captureAll.store(false,std::memory_order_release);
            if(!Phase26JoltRecorder::Get().FinishUpdate())s.Fail("Jolt observation context or overflow");
#endif
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
            if(s.physics->GetCompletedSteps()!=s.tick+1){s.Fail("incomplete Jolt Update");return;}
            Capture(*s.physics->GetPhysicsWorld());
#else
            if(!s.manifolds || s.physics->GetCompletedSteps()!=s.tick+1){s.Fail("incomplete Update");return;}
            Capture(*s.physics->GetPhysicsWorld(),*s.manifolds);
#endif
            auto& out=s.samples[s.tick];
            out.updateNs=ns;
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
            const auto& recorder=Phase26JoltRecorder::Get();
            for(std::uint32_t i=0;i<recorder.heapCount.load() && i<recorder.heap.size();++i){
                const auto& event=recorder.heap[i];
                if(event.action!=Phase26JoltRecorder::Q::HeapAction::Allocation &&
                    event.action!=Phase26JoltRecorder::Q::HeapAction::Reallocation &&
                    event.action!=Phase26JoltRecorder::Q::HeapAction::AlignedAllocation)continue;
                ++out.allocations;out.allocationBytes+=event.requested_bytes;
                const auto phase=event.context.phase;
                if(event.context.site==Phase26JoltRecorder::Q::ArenaSite::JointContainer ||
                    (phase>=Phase26JoltRecorder::Q::Phase::JointFilter &&
                     phase<=Phase26JoltRecorder::Q::Phase::JointPositionSolve))++out.jointAllocations;
            }
            if(!WriteJoltFrame(s,JoltFrameKind::Update))s.Fail("Jolt observation export or 64 MiB cap");
#else
            out.allocations=Phase26Memory::count;out.allocationBytes=Phase26Memory::bytes;
            out.jointAllocations=Phase26Memory::jointCount;
#endif
            if(!s.minimal) {
                const auto start=std::chrono::steady_clock::now();Evaluate(s.tick);
                out.observationNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            }
            out.inclusiveNs=ns+out.observationNs;
            if(Phase26Memory::overflow)s.Fail("allocation trace overflow");
            if(s.tick==s.target)s.app->ShutDown();
        };
        Phase26Observation::observer=&s.completeObserver;
    }
    inline void BeforeShutdown()
    {
        if(!session)return;
        PhysicsDiagnostic::observer=nullptr;
        Phase26Observation::observer=nullptr;Phase26Memory::enabled=false;
        auto& s=*session;
        if(s.minimal)for(unsigned tick=1;tick<=s.tick;++tick)Evaluate(tick);
        for(unsigned tick=1;tick<=s.tick;++tick) {
            const auto& out=s.samples[tick];
            std::fprintf(s.csv,"%u,%.17g,%.17g,%.17g,%.17g,%.17g,%u,%llu,%llu\n",tick,out.rawEnergy,out.fidelity,out.geometry,out.witness,out.maxError,out.asleep,
                static_cast<unsigned long long>(out.jointNs),static_cast<unsigned long long>(out.contactNs));
        }
        if(s.csv)std::fflush(s.csv);
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        auto* binary=OpenObservationEvidenceFile((session->output/"samples.bin").string().c_str(),"wbx");
#else
        auto* binary=std::fopen((session->output/"samples.bin").string().c_str(),"wbx");
#endif
        if(!binary){session->Fail("binary evidence open");return;}
        if(std::fwrite(s.samples.data(),sizeof(Sample),s.tick+1,binary)!=s.tick+1)s.Fail("binary evidence write");
        if(std::fclose(binary)!=0)s.Fail("binary evidence close");
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        auto* trace=OpenObservationEvidenceFile((s.output/"allocations.json").string().c_str(),"wx");
#else
        auto* trace=std::fopen((s.output/"allocations.json").string().c_str(),"wx");
#endif
        if(!trace){s.Fail("allocation evidence open");return;}
        std::fprintf(trace,"{\"image_base\":%llu,\"events\":[",static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))));
        for(unsigned i=0;i<Phase26Memory::eventCount;++i){const auto& e=Phase26Memory::events[i];
            std::fprintf(trace,"%s{\"tick\":%u,\"last_tick\":%u,\"occurrences\":%llu,\"stage\":%u,\"bytes\":%zu,\"joint\":%s,\"stack\":[",i?",":"",e.tick,e.lastTick,static_cast<unsigned long long>(e.occurrences),e.stage,e.bytes,e.joint?"true":"false");
            for(unsigned k=0;k<e.depth;++k)std::fprintf(trace,"%s%llu",k?",":"",static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(e.stack[k])));
            std::fprintf(trace,"]}");}
        std::fprintf(trace,"]}\n");std::fclose(trace);

        // Post-measurement lifecycle checks consume no extra tick and never feed
        // the qualification workload. Use real solved caches, not seeded impulses.
        if(!s.failed && s.target==120 && !s.ordinary) {
            auto& w=*s.physics->GetPhysicsWorld();
            std::array<PhysicsJointHandle,16> joints;std::array<PhysicsBodyHandle,18> bodies;
            for(unsigned i=0;i<s.count;++i)bodies[i]=*w.BodyAt(i);
            for(unsigned i=0;i<s.links;++i){joints[i]=*w.JointAt(i);auto j=w.ReadDistanceJoint(joints[i]);
                if(!j || std::abs(j->accumulatedImpulse)<.01f)s.Fail("solved warm cache missing");}
            std::array<FrameDebugLine,256> lines;
            DebugGeometry geometry(lines);
            if(!s.physics->AppendDebugGeometry(geometry,{false,false,false,false,true},{},true) ||
                geometry.Lines().size()<s.links*7)s.Fail("scoped joint debug geometry");
            const auto tip=w.ReadBody(bodies[s.links]);
            if(!tip || !s.physics->SetBodyPose(bodies[s.links],tip->position+Vec3f(.001f,0,0),tip->orientation) ||
                w.ReadDistanceJoint(joints[s.links-1])->accumulatedImpulse!=0)s.Fail("teleport cache reset");
            for(unsigned i=1;i<=s.links;++i)if(w.ReadBody(bodies[i])->sleeping)s.Fail("teleport wake propagation");
            if(!w.SetBodyMass(bodies[1],1.5f) || w.ReadDistanceJoint(joints[0])->accumulatedImpulse!=0)s.Fail("mass cache reset");
            if(!s.physics->SetBodyScale(bodies[3],Vec3f(1.01f)) || w.ReadDistanceJoint(joints[2])->accumulatedImpulse!=0)s.Fail("collider cache reset");
            s.lifecycleAfter=!s.failed;
        }
    }
    inline bool Finish(bool clean)
    {
        if(!session)return false;auto& s=*session;
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        if(s.jolt){
            if(!Phase26JoltRecorder::Get().ProcessHeap() || !Phase26JoltRecorder::Get().ProcessCapacity()){
                s.failed=true;s.failure="Jolt teardown heap ownership";
            }
            const bool written=WriteJoltFrame(s,JoltFrameKind::Teardown);
            const bool flushed=std::fflush(s.jolt)==0;
            const bool closed=std::fclose(s.jolt)==0;
            if(!written || !flushed || !closed){
                s.failed=true;s.failure="Jolt teardown export";
            }
            s.jolt=nullptr;
        }
        const auto& finalRecorder=Phase26JoltRecorder::Get();
        if(!finalRecorder.Valid() || finalRecorder.heapLiveBytes || finalRecorder.unmatchedLifetimeFrees ||
            finalRecorder.arenaReserveBytes!=finalRecorder.arenaReleaseBytes || finalRecorder.unclassifiedArenaCount ||
            Phase26JoltRecorder::Q::GetSink()!=nullptr){
            s.failed=true;s.failure="Jolt teardown sink or recorder";
        }
#endif
        const bool complete=clean && s.attached && !s.failed && s.tick==s.target;
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        auto* f=OpenObservationEvidenceFile((s.output/"result.json").string().c_str(),"wx");if(!f)return false;
#else
        auto* f=std::fopen((s.output/"result.json").string().c_str(),"wx");if(!f)return false;
#endif
        std::fprintf(f,"{\"complete\":%s,\"failure\":\"%s\",\"ticks\":%u,\"target\":%u,\"clean_shutdown\":%s,\"sample_bytes\":%zu,\"work\":%.17g,\"links\":%u,\"body_count\":%u,\"contact\":%s,\"minimal\":%s,\"lifecycle_after\":%s,\"joint_storage_bytes\":%zu,\"recorder_bytes\":%zu,",
            complete?"true":"false",s.failure,s.tick,s.target,clean?"true":"false",sizeof(Sample),s.work,s.links,s.count,s.contact?"true":"false",s.minimal?"true":"false",s.lifecycleAfter?"true":"false",s.jointStorageBytes,sizeof(Session));
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        auto& recorder=Phase26JoltRecorder::Get();
        const auto candidateBytes=std::size_t(Phase26JoltRecorder::Q::kObservationTypeSizes.ccd_candidate_slot)*
            Phase26JoltRecorder::Q::kObservationTypeSizes.ccd_candidate_slots;
        std::fprintf(f,"\"phase_time_semantics\":\"nested_worker_elapsed_sum\",");
        std::fprintf(f,"\"quaternion_layout\":{\"w\":%zu,\"x\":%zu,\"y\":%zu,\"z\":%zu},",
            offsetof(Quat,w),offsetof(Quat,x),offsetof(Quat,y),offsetof(Quat,z));
        std::fprintf(f,"\"allocation_recorder_bytes\":%zu,\"sdk_candidate_table_bytes\":%zu,\"total_recorder_bytes\":%zu,\"identity_layout\":{\"handle\":%zu,\"index\":%zu,\"generation\":%zu,\"registry\":%zu,\"type\":%zu,\"static_type\":%d,\"dynamic_type\":%d},",
            sizeof(Phase26Memory::events)+sizeof(Phase26JoltRecorder::Recorder),
            candidateBytes,sizeof(Session)+sizeof(Phase26Memory::events)+sizeof(Phase26JoltRecorder::Recorder)+candidateBytes,
            offsetof(CompactBody,handle),offsetof(PhysicsBodyHandle,index),offsetof(PhysicsBodyHandle,generation),
            offsetof(PhysicsBodyHandle,registry),offsetof(CompactBody,type),int(Component::BodyType::Static),int(Component::BodyType::Dynamic));
        std::fprintf(f,"\"offsets\":{\"body_stride\":%zu,\"position\":%zu,\"velocity\":%zu,\"angular\":%zu,\"orientation\":%zu,\"inverse_mass\":%zu,\"center_of_mass\":%zu,\"sleeping\":%zu,\"errors\":%zu,\"impulses\":%zu,\"executed_work\":%zu,\"raw_energy\":%zu,\"inclusive\":%zu,\"update\":%zu,\"asleep\":%zu,\"contacts\":%zu,\"non_ground_contacts\":%zu,\"witnesses\":%zu,\"witness_stride\":%zu},",
            sizeof(CompactBody),offsetof(CompactBody,position),offsetof(CompactBody,linearVelocity),
            offsetof(CompactBody,angularVelocity),offsetof(CompactBody,orientation),offsetof(CompactBody,inverseMass),
            offsetof(CompactBody,centerOfMass),offsetof(CompactBody,sleeping),
            offsetof(Sample,errors),offsetof(Sample,impulses),offsetof(Sample,work),offsetof(Sample,rawEnergy),
            offsetof(Sample,inclusiveNs),offsetof(Sample,updateNs),offsetof(Sample,asleep),offsetof(Sample,contacts),
            offsetof(Sample,nonGroundContacts),offsetof(Sample,witnesses),sizeof(ContactWitness));
        std::fprintf(f,"\"joint_storage_layout\":{\"adapter_joint_bytes\":%zu,\"adapter_filter_bytes\":%zu,\"jolt_joint_live_heap_bytes\":%zu,\"jolt_joint_live_allocations\":%u},",
            s.adapterJointStorageBytes,s.adapterFilterStorageBytes,s.joltJointLiveBytes,s.joltJointLiveCount);
        std::fprintf(f,"\"heap_source_bounds\":{\"schema\":1,\"base_job_pool_bytes\":%llu,\"base_job_slot_bytes\":%llu,\"base_job_slots\":%u,\"broadphase_node_bytes\":%llu,\"broadphase_center_bytes\":%llu,\"body_slot_count_max\":%u,\"base_job_pool_requests\":%llu,\"ownership_validated_updates\":%llu,\"ownership_failures\":%llu},",
            static_cast<unsigned long long>(Phase26JoltRecorder::Q::kBaseJobPoolBytes),
            static_cast<unsigned long long>(Phase26JoltRecorder::Q::kBaseJobSlotBytes),unsigned(JPH::cMaxPhysicsJobs),
            static_cast<unsigned long long>(Phase26JoltRecorder::Q::kBroadPhaseNodeBytes),
            static_cast<unsigned long long>(Phase26JoltRecorder::Q::kBroadPhaseCenterBytes),recorder.heapBodySlotsMax,
            static_cast<unsigned long long>(recorder.baseJobPoolTotalRequests),
            static_cast<unsigned long long>(recorder.heapValidatedUpdates),
            static_cast<unsigned long long>(recorder.heapOwnershipFailures));
        std::fprintf(f,"\"jolt_observation_layout\":{\"file\":\"jolt-observation.bin\",\"schema\":2,\"frame_header_bytes\":%zu,\"frame_kind_offset\":%zu,\"frame_counts_offset\":%zu,\"point_bytes\":%zu,\"ccd_bytes\":%zu,\"lifetime_bytes\":%zu,\"work_bytes\":%zu,\"phase_bytes\":%zu,\"storage_bytes\":%zu,\"heap_bytes\":%zu,\"failure_bytes\":%zu,\"identity_bytes\":%zu,\"kind_bytes\":%zu,\"worker_id_bytes\":%zu,",
            sizeof(JoltFrameHeader),offsetof(JoltFrameHeader,kind),offsetof(JoltFrameHeader,pointCount),
            sizeof(Phase26JoltRecorder::Q::PointRecord),sizeof(Phase26JoltRecorder::Q::CCDRecord),
            sizeof(Phase26JoltRecorder::Q::LifetimeRecord),sizeof(Phase26JoltRecorder::Q::WorkRecord),
            sizeof(Phase26JoltRecorder::Q::PhaseRecord),sizeof(Phase26JoltRecorder::Q::StorageRecord),
            sizeof(Phase26JoltRecorder::Q::HeapRecord),sizeof(Phase26JoltRecorder::Q::FailureRecord),
            sizeof(Phase26JoltRecorder::Q::IdentityRecord),sizeof(Phase26JoltRecorder::Q::EventKind),sizeof(std::uint32_t));
        std::fprintf(f,"\"work_kind_count\":%zu,\"work_entry_bytes\":%zu,\"work_count_offset\":%zu,\"work_changed_offset\":%zu,\"work_roots_offset\":%zu,\"point_capacity\":%zu,\"ccd_capacity\":%zu,\"lifetime_capacity\":%zu,\"work_capacity\":%zu,\"phase_capacity\":%zu,\"storage_capacity\":%zu,\"heap_capacity\":%zu,\"failure_capacity\":%zu,\"identity_capacity\":%zu,",
            Sample::WorkKinds,sizeof(Sample::WorkTriple),
            offsetof(Sample::WorkTriple,count),offsetof(Sample::WorkTriple,changed),offsetof(Sample::WorkTriple,rootIterations),
            recorder.points.size(),recorder.ccd.size(),
            recorder.lifetimes.size(),recorder.work.size(),recorder.phases.size(),recorder.storage.size(),
            recorder.heap.size(),recorder.failures.size(),recorder.identities.size());
        using QContext=Phase26JoltRecorder::Q::Context;
        using QPoint=Phase26JoltRecorder::Q::PointRecord;
        using QIdentity=Phase26JoltRecorder::Q::IdentityRecord;
        using QStorage=Phase26JoltRecorder::Q::StorageRecord;
        using QHeap=Phase26JoltRecorder::Q::HeapRecord;
        std::fprintf(f,"\"context_layout\":{\"bytes\":%zu,\"world\":%zu,\"epoch\":%zu,\"update\":%zu,\"substep\":%zu,\"phase\":%zu,\"site\":%zu},",
            sizeof(QContext),offsetof(QContext,world),offsetof(QContext,epoch),offsetof(QContext,update),
            offsetof(QContext,substep),offsetof(QContext,phase),offsetof(QContext,site));
        std::fprintf(f,"\"point_offsets\":{\"body1\":%zu,\"subshape1\":%zu,\"body2\":%zu,\"subshape2\":%zu,\"ordinal\":%zu,\"space\":%zu,\"anchor1\":%zu,\"anchor2\":%zu,\"cached_normal\":%zu,\"solver_normal\":%zu,\"lambda\":%zu},",
            offsetof(QPoint,body1),offsetof(QPoint,subshape1),offsetof(QPoint,body2),offsetof(QPoint,subshape2),
            offsetof(QPoint,ordinal),offsetof(QPoint,space),offsetof(QPoint,anchor1),offsetof(QPoint,anchor2),
            offsetof(QPoint,cached_local_normal),offsetof(QPoint,solver_world_normal),offsetof(QPoint,normal_lambda));
        std::fprintf(f,"\"identity_offsets\":{\"sdk_body_id\":%zu,\"managed_index\":%zu,\"managed_generation\":%zu,\"managed_registry\":%zu,\"action\":%zu},",
            offsetof(QIdentity,sdk_body_id),offsetof(QIdentity,managed_index),offsetof(QIdentity,managed_generation),
            offsetof(QIdentity,managed_registry),offsetof(QIdentity,action));
        std::fprintf(f,"\"storage_offsets\":{\"pointer\":%zu,\"requested\":%zu,\"aligned\":%zu,\"usage_after\":%zu,\"count\":%zu,\"action\":%zu},",
            offsetof(QStorage,pointer),offsetof(QStorage,requested_bytes),offsetof(QStorage,aligned_bytes),
            offsetof(QStorage,arena_usage_after),offsetof(QStorage,count),offsetof(QStorage,action));
        std::fprintf(f,"\"heap_offsets\":{\"pointer\":%zu,\"old_pointer\":%zu,\"requested\":%zu,\"old_bytes\":%zu,\"action\":%zu},",
            offsetof(QHeap,pointer),offsetof(QHeap,old_pointer),offsetof(QHeap,requested_bytes),
            offsetof(QHeap,old_bytes),offsetof(QHeap,action));
        std::fprintf(f,"\"heap_live_bytes\":%llu,\"heap_peak_bytes\":%llu,\"heap_allocation_calls\":%llu,\"heap_allocation_bytes\":%llu,\"heap_free_calls\":%llu,\"heap_free_bytes\":%llu,\"unmatched_lifetime_frees\":%llu,",
            static_cast<unsigned long long>(recorder.heapLiveBytes),static_cast<unsigned long long>(recorder.heapPeakBytes),
            static_cast<unsigned long long>(recorder.heapAllocationCalls),static_cast<unsigned long long>(recorder.heapAllocationBytes),
            static_cast<unsigned long long>(recorder.heapFreeCalls),static_cast<unsigned long long>(recorder.heapFreeBytes),
            static_cast<unsigned long long>(recorder.unmatchedLifetimeFrees));
        std::fprintf(f,"\"arena_reserve_calls\":%llu,\"arena_reserve_bytes\":%llu,\"arena_release_calls\":%llu,\"arena_release_bytes\":%llu,\"arena_high_water\":%llu,\"unclassified_arena_count\":%u,\"recorder_valid\":%s,\"sink_detached\":%s},",
            static_cast<unsigned long long>(recorder.arenaReserveCalls),static_cast<unsigned long long>(recorder.arenaReserveBytes),
            static_cast<unsigned long long>(recorder.arenaReleaseCalls),static_cast<unsigned long long>(recorder.arenaReleaseBytes),
            static_cast<unsigned long long>(recorder.arenaHighWater),recorder.unclassifiedArenaCount,recorder.Valid()?"true":"false",
            Phase26JoltRecorder::Q::GetSink()==nullptr?"true":"false");
        std::fprintf(f,"\"arena_capacities\":[");
        for(std::size_t i=0;i<recorder.storageCapacityBytes.size();++i)
            std::fprintf(f,"%s{\"site\":%zu,\"bytes\":%llu,\"count\":%u}",i?",":"",i,
                static_cast<unsigned long long>(recorder.storageCapacityBytes[i]),recorder.storageCapacityCounts[i]);
        std::fprintf(f,"]}\n");
#else
        std::fprintf(f,"\"allocation_recorder_bytes\":%zu,\"total_recorder_bytes\":%zu,\"identity_layout\":{\"handle\":%zu,\"index\":%zu,\"generation\":%zu,\"registry\":%zu,\"type\":%zu,\"static_type\":%d,\"dynamic_type\":%d},",
            sizeof(Phase26Memory::events),sizeof(Session)+sizeof(Phase26Memory::events),offsetof(PhysicsBodyState,handle),offsetof(PhysicsBodyHandle,index),offsetof(PhysicsBodyHandle,generation),offsetof(PhysicsBodyHandle,registry),offsetof(PhysicsBodyState,type),int(Component::BodyType::Static),int(Component::BodyType::Dynamic));
        std::fprintf(f,"\"offsets\":{\"body_stride\":%zu,\"position\":%zu,\"velocity\":%zu,\"angular\":%zu,\"orientation\":%zu,\"sleeping\":%zu,\"errors\":%zu,\"impulses\":%zu,\"executed_work\":%zu,\"raw_energy\":%zu,\"inclusive\":%zu,\"update\":%zu,\"asleep\":%zu,\"witnesses\":%zu,\"witness_stride\":%zu}}\n",
            sizeof(PhysicsBodyState),offsetof(PhysicsBodyState,position),offsetof(PhysicsBodyState,linearVelocity),offsetof(PhysicsBodyState,angularVelocity),offsetof(PhysicsBodyState,orientation),offsetof(PhysicsBodyState,sleeping),offsetof(Sample,errors),offsetof(Sample,impulses),offsetof(Sample,work),offsetof(Sample,rawEnergy),offsetof(Sample,inclusiveNs),offsetof(Sample,updateNs),offsetof(Sample,asleep),offsetof(Sample,witnesses),sizeof(ContactWitness));
#endif
        std::fclose(f);session.reset();return complete;
    }
}
#endif
