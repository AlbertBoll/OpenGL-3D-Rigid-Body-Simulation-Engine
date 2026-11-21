#include "gepch.h"
#include "Physics/JoltAuditObservation.h"
#include "EditorPlacementBroadphase.h"
#include "WorldBackend.h"
#include "EditorScale.h"
#include "InertiaValidation.h"
#include "Physics/ShapeSphere.h"
#include "Physics/ShapeCapsule.h"
#include "Physics/ShapeConvex.h"
#include <array>
#include <bitset>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <new>
#include <chrono>
#include <atomic>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <string_view>
#if defined(GENGINE_PHASE27_QUALIFICATION) || defined(GENGINE_PHASE28_QUALIFICATION)
#include <intrin.h>
#endif

#ifdef GENGINE_WITH_JOLT
#include <Jolt/Jolt.h>
#ifdef JPH_EXTERNAL_PROFILE
#include <Jolt/Core/Profiler.h>
#endif
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/Constraints/ConstraintPart/PointConstraintPart.h>
#include <Jolt/Physics/Constraints/ConstraintPart/AngleConstraintPart.h>
#include <Jolt/Physics/StateRecorder.h>
#include <Jolt/Core/StreamIn.h>
#include <Jolt/Core/StreamOut.h>
#include <Jolt/ObjectStream/TypeDeclarations.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRenderer.h>
#endif
#if defined(GENGINE_PHASE27_QUALIFICATION) || defined(GENGINE_PHASE28_QUALIFICATION)
#include <Jolt/Physics/PhysicsUpdateContext.h>
#include <Jolt/Physics/Constraints/ContactConstraintManager.h>
extern "C" const std::uint64_t g_Phase27TargetSizes[] = {
    sizeof(void*),sizeof(JPH::uint32),JPH_RVECTOR_ALIGNMENT,
    sizeof(JPH::PhysicsUpdateContext::Step),sizeof(JPH::PhysicsUpdateContext::Step::CCDBody),
    JPH::ContactConstraintManager::cMaxConstraintSize,sizeof(JPH::BodyPair),sizeof(JPH::BodyID)
};
#endif
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
#include <Jolt/Physics/QualificationObservation.h>
#endif
#include <glm/gtx/quaternion.hpp>
#endif

#ifdef GENGINE_PHASE28_QUALIFICATION
// Qualification-only counters consumed by the ordinary RBS process. They are
// reset at each fixed-tick boundary; concurrent Jolt workers publish atomically.
extern "C" {
    std::atomic<std::uint64_t> g_Phase28ObserverWrapperNs{};
    std::atomic<std::uint64_t> g_Phase28ObserverWrapperCalls{};
    std::atomic<std::uint64_t> g_Phase28JoltStageNs[8]{};
    std::atomic<std::uint64_t> g_Phase28JoltStageCalls[8]{};
    std::atomic<std::uint64_t> g_Phase28JoltProfileHookNs{};
    std::atomic<std::uint64_t> g_Phase28JoltProfileHookCalls{};
}
#endif

#if defined(GENGINE_WITH_JOLT) && defined(JPH_EXTERNAL_PROFILE) && !defined(JPH_SHARED_LIBRARY)
#if defined(GENGINE_PHASE28_QUALIFICATION) && defined(GENGINE_JOLT_FINAL_AUDIT)
namespace
{
    // Jolt's static-library profiler provides 64 bytes in each stack scope.
    // This payload has no lifetime outside its Jolt profile scope or Update.
    struct Phase28JoltScope
    {
        std::uint64_t beginNs{};
        std::uint8_t stage{8};
        std::uint8_t active{};
    };
    static_assert(sizeof(Phase28JoltScope)<=64);

    bool Phase28ScopeName(std::string_view name,std::string_view function) noexcept
    {
        // MSVC may append instantiated template arguments to __FUNCTION__.
        // The admitted SDK manager names themselves contain no '<'.
        if(const auto templateStart=name.find('<');templateStart!=std::string_view::npos)
            name=name.substr(0,templateStart);
        if(name==function)return true;
        if(name.size()<=function.size()+1 || name.substr(name.size()-function.size())!=function)return false;
        return name[name.size()-function.size()-1]==':';
    }

    std::uint8_t Phase28JoltStage(const char* name) noexcept
    {
        const std::string_view n{name?name:""};
        if(Phase28ScopeName(n,"sSetupVelocityConstraints"))return 0;
        if(Phase28ScopeName(n,"sWarmStartVelocityConstraints"))return 1;
        if(Phase28ScopeName(n,"sSolveVelocityConstraints"))return 2;
        if(Phase28ScopeName(n,"sSolvePositionConstraints"))return 3;
        if(Phase28ScopeName(n,"AddContactConstraint") || n=="Add Constraint From Cached Manifold")return 4;
        if(Phase28ScopeName(n,"WarmStartVelocityConstraints"))return 5;
        if(Phase28ScopeName(n,"SolveVelocityConstraints"))return 6;
        if(Phase28ScopeName(n,"SolvePositionConstraints"))return 7;
        return 8;
    }
}
#endif

JPH::ExternalProfileMeasurement::ExternalProfileMeasurement(const char* inName,JPH::uint32 inColor)
{
    (void)inColor;
#if defined(GENGINE_PHASE28_QUALIFICATION) && defined(GENGINE_JOLT_FINAL_AUDIT)
    const bool active=GEngine::JoltAudit::countAllocations.load(std::memory_order_relaxed);
    const auto hookBegin=active?GEngine::JoltAudit::Now():0;
    Phase28JoltScope scope{};
    scope.active=active?1:0;
    if(active)scope.stage=Phase28JoltStage(inName);
    std::memcpy(mUserData,&scope,sizeof scope);
    if(active){
        g_Phase28JoltProfileHookCalls.fetch_add(1,std::memory_order_relaxed);
        g_Phase28JoltProfileHookNs.fetch_add(GEngine::JoltAudit::Now()-hookBegin,std::memory_order_relaxed);
        const auto begin=GEngine::JoltAudit::Now();
        std::memcpy(mUserData,&begin,sizeof begin);
    }
#else
    (void)inName;
#endif
}

JPH::ExternalProfileMeasurement::~ExternalProfileMeasurement()
{
#if defined(GENGINE_PHASE28_QUALIFICATION) && defined(GENGINE_JOLT_FINAL_AUDIT)
    Phase28JoltScope scope{};
    std::memcpy(&scope,mUserData,sizeof scope);
    if(!scope.active)return;
    const auto hookBegin=GEngine::JoltAudit::Now();
    if(scope.stage<8){
        g_Phase28JoltStageNs[scope.stage].fetch_add(hookBegin-scope.beginNs,std::memory_order_relaxed);
        g_Phase28JoltStageCalls[scope.stage].fetch_add(1,std::memory_order_relaxed);
    }
    g_Phase28JoltProfileHookCalls.fetch_add(1,std::memory_order_relaxed);
    g_Phase28JoltProfileHookNs.fetch_add(GEngine::JoltAudit::Now()-hookBegin,std::memory_order_relaxed);
#endif
}
#endif

namespace GEngine
{
#if defined(GENGINE_WITH_JOLT) && defined(GENGINE_G8_RESISTANCE_CHECKS)
    // Isolated acceptance instrumentation; absent from ordinary application builds.
    namespace G8ResistanceObservation
    {
        static std::atomic<bool> enabled{false};
        static std::atomic<std::uint64_t> heapCalls{},heapBytes{},tempPeak{},tempCalls{},updateNs{},updateCalls{};
        static std::atomic<JPH::ContactResistanceObserver*> observer{nullptr};
        void Begin() noexcept {heapCalls=0;heapBytes=0;tempPeak=0;tempCalls=0;updateNs=0;updateCalls=0;enabled=true;}
        void End() noexcept {enabled=false;}
        std::uint64_t HeapCalls() noexcept {return heapCalls.load();}
        std::uint64_t HeapBytes() noexcept {return heapBytes.load();}
        std::uint64_t TempPeakBytes() noexcept {return tempPeak.load();}
        std::uint64_t TempCalls() noexcept {return tempCalls.load();}
        std::uint64_t UpdateNanoseconds() noexcept {return updateNs.load();}
        std::uint64_t UpdateCalls() noexcept {return updateCalls.load();}
        void SetObserver(JPH::ContactResistanceObserver* value) noexcept {observer.store(value);}
        static void Allocation(std::size_t bytes) noexcept {
            if(enabled.load(std::memory_order_relaxed)) {heapCalls.fetch_add(1,std::memory_order_relaxed);heapBytes.fetch_add(bytes,std::memory_order_relaxed);}
        }
        static void Temporary(std::uint64_t usage) noexcept {
            if(!enabled.load(std::memory_order_relaxed))return;
            tempCalls.fetch_add(1,std::memory_order_relaxed);
            auto peak=tempPeak.load(std::memory_order_relaxed);
            while(peak<usage && !tempPeak.compare_exchange_weak(peak,usage,std::memory_order_relaxed)){}
        }
    }
#endif
    bool PhysicsBackendAvailable(PhysicsBackend backend) noexcept
    {
        if (backend == PhysicsBackend::Native) return true;
#ifdef GENGINE_WITH_JOLT
        return backend == PhysicsBackend::Jolt;
#else
        return false;
#endif
    }
#ifdef GENGINE_WITH_JOLT
    namespace
    {
#if defined(GENGINE_PHASE28_QUALIFICATION) && defined(GENGINE_JOLT_FINAL_AUDIT)
        struct Phase28ObserverTimer
        {
            bool enabled;
            std::uint64_t start;
            explicit Phase28ObserverTimer(bool active) noexcept
                : enabled(active),start(active?JoltAudit::Now():0) {}
            ~Phase28ObserverTimer()
            {
                if(!enabled)return;
                // Whole-wrapper duration includes actual SDK allocator work.
                // This intentionally overstates observer-only cost; the audit
                // adds a per-call clock-quantum allowance on top.
                const auto elapsed=JoltAudit::Now()-start;
                g_Phase28ObserverWrapperNs.fetch_add(elapsed,std::memory_order_relaxed);
                g_Phase28ObserverWrapperCalls.fetch_add(1,std::memory_order_relaxed);
            }
        };
#endif
        constexpr std::size_t BodyCapacity = 4096;
        static_assert(BodyCapacity==PhysicsWorld::MaxImpulseBodies);
        std::mutex s_LifetimeMutex;
        unsigned s_WorldCount{};
        JPH::TraceFunction s_PreviousTrace{};
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
        namespace Phase26ObservationSDK=JPH::QualificationObservation;
        JPH::AllocateFunction s_PreviousAllocate{};
        JPH::ReallocateFunction s_PreviousReallocate{};
        JPH::FreeFunction s_PreviousFree{};
        JPH::AlignedAllocateFunction s_PreviousAlignedAllocate{};
        JPH::AlignedFreeFunction s_PreviousAlignedFree{};
        bool s_ObservationHooksOwned{};
        Phase26ObservationSDK::Context ObservedContext() noexcept
        {
            auto context=Phase26ObservationSDK::CurrentContext();
            if(auto* sink=Phase26ObservationSDK::GetSink();sink && sink->update && !context.update){
                context.world=sink->world;context.epoch=sink->epoch;context.update=sink->update;
                context.substep=UINT32_MAX;context.phase=Phase26ObservationSDK::Phase::SharedStepWork;
            }
            return context;
        }
        void ReportHeap(Phase26ObservationSDK::HeapAction action,void* pointer,void* old,std::size_t requested,std::size_t oldBytes=0) noexcept
        {
            if(!Phase26ObservationSDK::GetSink())return;
            Phase26ObservationSDK::HeapRecord record{ObservedContext(),std::uint64_t(reinterpret_cast<std::uintptr_t>(pointer)),
                std::uint64_t(reinterpret_cast<std::uintptr_t>(old)),requested,oldBytes,action};
            Phase26ObservationSDK::PublishSynthetic(record.context,Phase26ObservationSDK::EventKind::Heap,&record,sizeof(record));
        }
        void* ObservedAllocate(std::size_t n)
        {auto* p=s_PreviousAllocate(n);ReportHeap(Phase26ObservationSDK::HeapAction::Allocation,p,nullptr,n);return p;}
        void* ObservedReallocate(void* old,std::size_t oldBytes,std::size_t n)
        {auto* p=s_PreviousReallocate(old,oldBytes,n);ReportHeap(Phase26ObservationSDK::HeapAction::Reallocation,p,old,n,oldBytes);return p;}
        void ObservedFree(void* p)
        {ReportHeap(Phase26ObservationSDK::HeapAction::Free,p,nullptr,0);s_PreviousFree(p);}
        void* ObservedAlignedAllocate(std::size_t n,std::size_t alignment)
        {auto* p=s_PreviousAlignedAllocate(n,alignment);ReportHeap(Phase26ObservationSDK::HeapAction::AlignedAllocation,p,nullptr,n);return p;}
        void ObservedAlignedFree(void* p)
        {ReportHeap(Phase26ObservationSDK::HeapAction::AlignedFree,p,nullptr,0);s_PreviousAlignedFree(p);}
        void InstallObservationHooks() noexcept
        {
            s_PreviousAllocate=JPH::Allocate;s_PreviousReallocate=JPH::Reallocate;s_PreviousFree=JPH::Free;
            s_PreviousAlignedAllocate=JPH::AlignedAllocate;s_PreviousAlignedFree=JPH::AlignedFree;
            JPH::Allocate=&ObservedAllocate;JPH::Reallocate=&ObservedReallocate;JPH::Free=&ObservedFree;
            JPH::AlignedAllocate=&ObservedAlignedAllocate;JPH::AlignedFree=&ObservedAlignedFree;
            s_ObservationHooksOwned=true;
        }
        void RestoreObservationHooks() noexcept
        {
            if(!s_ObservationHooksOwned)return;
            if(JPH::Allocate!=&ObservedAllocate || JPH::Reallocate!=&ObservedReallocate || JPH::Free!=&ObservedFree ||
                JPH::AlignedAllocate!=&ObservedAlignedAllocate || JPH::AlignedFree!=&ObservedAlignedFree){
                Phase26ObservationSDK::ReportFailure(Phase26ObservationSDK::FailureCode::SinkMismatch);return;
            }
            JPH::Allocate=s_PreviousAllocate;JPH::Reallocate=s_PreviousReallocate;JPH::Free=s_PreviousFree;
            JPH::AlignedAllocate=s_PreviousAlignedAllocate;JPH::AlignedFree=s_PreviousAlignedFree;
            s_ObservationHooksOwned=false;
        }
#endif
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailedFunction s_PreviousAssert{};
#endif
        void Trace(const char* format,...)
        {
            char text[1024];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);
            if(auto logger=Log::GetCoreLogger())logger->error("Jolt: {}",text);
        }
        void RestoreCallbacks()
        {
            JPH::Trace=s_PreviousTrace;
#ifdef JPH_ENABLE_ASSERTS
            JPH::AssertFailed=s_PreviousAssert;
#endif
        }
        bool AcquireJolt()
        {
            std::lock_guard lock(s_LifetimeMutex);
            if (!s_WorldCount)
            {
                if (JPH::Factory::sInstance) return false; // Never replace another owner's factory.
                s_PreviousTrace=JPH::Trace;JPH::Trace=Trace;
#ifdef JPH_ENABLE_ASSERTS
                s_PreviousAssert=JPH::AssertFailed;
                JPH::AssertFailed=[](const char* expression,const char* message,const char* file,JPH::uint line) {
                    if(auto logger=Log::GetCoreLogger())logger->critical("Jolt invariant failed: {} {} {}:{}",expression,message?message:"",file,line);
                    return true;
                };
#endif
                if(!JPH::VerifyJoltVersionID()){RestoreCallbacks();return false;}
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
                if(Phase26ObservationSDK::kObservationAbiSentinel!=Phase26ObservationSDK::kExpectedObservationAbiSentinel){RestoreCallbacks();return false;}
#endif
                JPH::RegisterDefaultAllocator();
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
                InstallObservationHooks();
#endif
#ifdef GENGINE_G8_RESISTANCE_CHECKS
                static auto resistanceAllocate=JPH::Allocate;
                static auto resistanceReallocate=JPH::Reallocate;
                static auto resistanceAligned=JPH::AlignedAllocate;
                JPH::Allocate=[](size_t n)->void* {G8ResistanceObservation::Allocation(n);return resistanceAllocate(n);};
                JPH::Reallocate=[](void* p,size_t old,size_t n)->void* {G8ResistanceObservation::Allocation(n);return resistanceReallocate(p,old,n);};
                JPH::AlignedAllocate=[](size_t n,size_t a)->void* {G8ResistanceObservation::Allocation(n);return resistanceAligned(n,a);};
#endif
#ifdef GENGINE_JOLT_FINAL_AUDIT
                // Audit wrappers retain the SDK's allocator and lifetime policy.
                static auto allocate=JPH::Allocate;
                static auto reallocate=JPH::Reallocate;
                static auto aligned=JPH::AlignedAllocate;
                JPH::Allocate=[](size_t n)->void* {
#ifdef GENGINE_PHASE28_QUALIFICATION
                    Phase28ObserverTimer observerTimer(JoltAudit::countAllocations.load(std::memory_order_relaxed));
#endif
                    JoltAudit::Alloc(n,true);auto* p=allocate(n);
#if defined(GENGINE_PHASE27_QUALIFICATION) || defined(GENGINE_PHASE28_QUALIFICATION)
                    if(JoltAudit::countAllocations && Phase27Audit::heap)Phase27Audit::heap({reinterpret_cast<std::uintptr_t>(p),reinterpret_cast<std::uintptr_t>(_ReturnAddress()),n,true});
#endif
                    return p;};
                JPH::Reallocate=[](void* p,size_t old,size_t n)->void* {
#ifdef GENGINE_PHASE28_QUALIFICATION
                    Phase28ObserverTimer observerTimer(JoltAudit::countAllocations.load(std::memory_order_relaxed));
#endif
                    JoltAudit::Alloc(n,true);auto* result=reallocate(p,old,n);
#if defined(GENGINE_PHASE27_QUALIFICATION) || defined(GENGINE_PHASE28_QUALIFICATION)
                    if(JoltAudit::countAllocations && Phase27Audit::heap)Phase27Audit::heap({reinterpret_cast<std::uintptr_t>(result),reinterpret_cast<std::uintptr_t>(_ReturnAddress()),n,true});
#endif
                    return result;};
                JPH::AlignedAllocate=[](size_t n,size_t a)->void* {
#ifdef GENGINE_PHASE28_QUALIFICATION
                    Phase28ObserverTimer observerTimer(JoltAudit::countAllocations.load(std::memory_order_relaxed));
#endif
                    JoltAudit::Alloc(n,true);auto* p=aligned(n,a);
#if defined(GENGINE_PHASE27_QUALIFICATION) || defined(GENGINE_PHASE28_QUALIFICATION)
                    if(JoltAudit::countAllocations && Phase27Audit::heap)Phase27Audit::heap({reinterpret_cast<std::uintptr_t>(p),reinterpret_cast<std::uintptr_t>(_ReturnAddress()),n,true});
#endif
                    return p;};
#if defined(GENGINE_PHASE27_QUALIFICATION) || defined(GENGINE_PHASE28_QUALIFICATION)
                static const auto freeHeap=JPH::Free,freeAligned=JPH::AlignedFree;
                JPH::Free=[](void* p){
#ifdef GENGINE_PHASE28_QUALIFICATION
                    Phase28ObserverTimer observerTimer(JoltAudit::countAllocations.load(std::memory_order_relaxed));
#endif
                    if(JoltAudit::countAllocations && Phase27Audit::heap)Phase27Audit::heap({reinterpret_cast<std::uintptr_t>(p),reinterpret_cast<std::uintptr_t>(_ReturnAddress()),0,false});freeHeap(p);};
                JPH::AlignedFree=[](void* p){
#ifdef GENGINE_PHASE28_QUALIFICATION
                    Phase28ObserverTimer observerTimer(JoltAudit::countAllocations.load(std::memory_order_relaxed));
#endif
                    if(JoltAudit::countAllocations && Phase27Audit::heap)Phase27Audit::heap({reinterpret_cast<std::uintptr_t>(p),reinterpret_cast<std::uintptr_t>(_ReturnAddress()),0,false});freeAligned(p);};
#endif
#endif
                JPH::Factory::sInstance = new JPH::Factory;
                if (!JPH::Factory::sInstance){
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
                    RestoreObservationHooks();
#endif
                    RestoreCallbacks();return false;}
                JPH::RegisterTypes();
            }
            ++s_WorldCount;
            return true;
        }
        void ReleaseJolt()
        {
            std::lock_guard lock(s_LifetimeMutex);
            if (--s_WorldCount == 0)
            {
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
                RestoreObservationHooks();
                if(auto* sink=Phase26ObservationSDK::GetSink())Phase26ObservationSDK::RemoveSink(sink);
#endif
                RestoreCallbacks();
            }
        }
        struct Lifetime { ~Lifetime() { ReleaseJolt(); } };
        JPH::Vec3 V(const Vec3f& v) { return {v.x,v.y,v.z}; }
        JPH::Quat Q(const Quat& q) { return {q.x,q.y,q.z,q.w}; }
        Vec3f V(JPH::Vec3Arg v) { return {v.GetX(),v.GetY(),v.GetZ()}; }
        Quat Q(JPH::QuatArg q) { return {q.GetW(),q.GetX(),q.GetY(),q.GetZ()}; }

        // Keep Jolt's point, hinge-axis and position solver. Its built-in hard
        // limit becomes active only after the angle has crossed an authored
        // bound, so one transient SDK angle row catches an impending crossing
        // after the native point/axis velocity solve in the same iteration.
        class BookHingeConstraint final : public JPH::TwoBodyConstraint
        {
        public:
            JPH_OVERRIDE_NEW_DELETE
            BookHingeConstraint(JPH::Body& a,JPH::Body& b,const JPH::HingeConstraintSettings& settings)
                : JPH::TwoBodyConstraint(a,b,settings),mNative(a,b,settings) {}
            JPH::EConstraintSubType GetSubType() const override {return JPH::EConstraintSubType::User2;}
            void NotifyShapeChanged(const JPH::BodyID& id,JPH::Vec3Arg delta) override
            {mNative.NotifyShapeChanged(id,delta);}
            JPH::Mat44 GetConstraintToBody1Matrix() const override
            {return mNative.GetConstraintToBody1Matrix();}
            JPH::Mat44 GetConstraintToBody2Matrix() const override
            {return mNative.GetConstraintToBody2Matrix();}
            void ResetWarmStart() override
            {mNative.ResetWarmStart();mGuard.Deactivate();mSide=0;}
            void SetupVelocityConstraint(float dt) override
            {mStepDeltaTime=dt;mGuard.Deactivate();mSide=0;mNative.SetupVelocityConstraint(dt);}
            void WarmStartVelocityConstraint(float ratio) override
            {mNative.WarmStartVelocityConstraint(ratio);}
            bool SolveVelocityConstraint(float dt) override
            {
                bool changed=mNative.SolveVelocityConstraint(dt);
                const float angle=mNative.GetCurrentAngle();
                const float lower=mNative.GetLimitsMin(),upper=mNative.GetLimitsMax();
                if(angle>lower && angle<upper && mStepDeltaTime>0)
                {
                    if(mSide==0)
                    {
                        const auto axis=mBody1->GetRotation()*mNative.GetLocalSpaceHingeAxis1();
                        const float rate=axis.Dot(mBody2->GetAngularVelocity()-mBody1->GetAngularVelocity());
                        const float future=angle+mStepDeltaTime*rate;
                        mSide=future<lower?-1:future>upper?1:0;
                        if(mSide)
                        {
                            mAxis=axis;
                            const float bound=mSide<0?lower:upper;
                            mBias=(angle-bound)/mStepDeltaTime;
                            mGuard.CalculateConstraintProperties(*mBody1,*mBody2,mAxis,mBias);
                            if(!mGuard.IsActive())mSide=0;
                        }
                    }
                    if(mSide)
                        changed=mGuard.SolveVelocityConstraint(*mBody1,*mBody2,mAxis,
                            mSide<0?0.f:-FLT_MAX,mSide<0?FLT_MAX:0.f)||changed;
                }
                return changed;
            }
            bool SolvePositionConstraint(float dt,float baumgarte) override
            {return mNative.SolvePositionConstraint(dt,baumgarte);}
#ifdef JPH_DEBUG_RENDERER
            void DrawConstraint(JPH::DebugRenderer* renderer) const override
            {mNative.DrawConstraint(renderer);}
            void DrawConstraintLimits(JPH::DebugRenderer* renderer) const override
            {mNative.DrawConstraintLimits(renderer);}
#endif
            void SaveState(JPH::StateRecorder& stream) const override
            {
                JPH::TwoBodyConstraint::SaveState(stream);
                mNative.SaveState(stream);mGuard.SaveState(stream);
                stream.Write(mStepDeltaTime);stream.Write(mSide);
                stream.Write(mAxis);stream.Write(mBias);
            }
            void RestoreState(JPH::StateRecorder& stream) override
            {
                JPH::TwoBodyConstraint::RestoreState(stream);
                mNative.RestoreState(stream);mGuard.RestoreState(stream);
                stream.Read(mStepDeltaTime);stream.Read(mSide);
                stream.Read(mAxis);stream.Read(mBias);
            }
            JPH::Ref<JPH::ConstraintSettings> GetConstraintSettings() const override
            {
                auto settings=mNative.GetConstraintSettings();
                ToConstraintSettings(*settings);
                return settings;
            }
            const JPH::HingeConstraint& Native() const noexcept {return mNative;}
        private:
            JPH::HingeConstraint mNative;
            JPH::AngleConstraintPart mGuard;
            JPH::Vec3 mAxis{};
            float mStepDeltaTime{},mBias{};
            int mSide{};
        };

        const JPH::HingeConstraint* NativeHinge(const JPH::TwoBodyConstraint* constraint) noexcept
        {
            return constraint->GetSubType()==JPH::EConstraintSubType::User2?
                &static_cast<const BookHingeConstraint*>(constraint)->Native():
                static_cast<const JPH::HingeConstraint*>(constraint);
        }

        class BookCvConstraint;
        class BookCvConstraintSettings final : public JPH::TwoBodyConstraintSettings
        {
            JPH_DECLARE_SERIALIZABLE_VIRTUAL(, BookCvConstraintSettings)
        public:
            JPH::TwoBodyConstraint* Create(JPH::Body&,JPH::Body&) const override;
            void SaveBinaryState(JPH::StreamOut&) const override;
            JPH::Vec3 point1{},point2{}; // Body-local, relative to each center of mass.
            JPH::Quat frame1{JPH::Quat::sIdentity()},frame2{JPH::Quat::sIdentity()};
            float halfU{},halfV{};
            bool limited{};
        protected:
            void RestoreBinaryState(JPH::StreamIn&) override;
        };

        // Chapters 19/20: one pivot and the exact quaternion-component CV
        // relation, plus two independent unilateral pyramid limits. A Jolt
        // SwingTwist/Pyramid has a different finite-angle boundary and Jacobian.
        // This is one SDK two-body constraint in the existing island/scheduler.
        class BookCvConstraint final : public JPH::TwoBodyConstraint
        {
        public:
            JPH_OVERRIDE_NEW_DELETE
            BookCvConstraint(JPH::Body& a,JPH::Body& b,const BookCvConstraintSettings& s)
                : JPH::TwoBodyConstraint(a,b,s),mPoint1(s.point1),mPoint2(s.point2),
                  mFrame1(s.frame1),mFrame2(s.frame2),mHalfU(s.halfU),mHalfV(s.halfV),
                  mBoundU(std::sin(.5f*s.halfU)),mBoundV(std::sin(.5f*s.halfV)),
                  mLimited(s.limited) {}
            JPH::EConstraintSubType GetSubType() const override {return JPH::EConstraintSubType::User1;}
            void NotifyShapeChanged(const JPH::BodyID& id,JPH::Vec3Arg delta) override
            {
                if(mBody1->GetID()==id)mPoint1-=delta;
                else if(mBody2->GetID()==id)mPoint2-=delta;
            }
            JPH::Mat44 GetConstraintToBody1Matrix() const override
            {return JPH::Mat44::sRotationTranslation(mFrame1,mPoint1);}
            JPH::Mat44 GetConstraintToBody2Matrix() const override
            {return JPH::Mat44::sRotationTranslation(mFrame2,mPoint2);}
            void ResetWarmStart() override
            {
                mPoint.Deactivate();
                for(auto& row:mAngle)row.Deactivate();
                for(auto& row:mPositionAngle)row.Deactivate();
                mSide={};
            }
            void SetupVelocityConstraint(float dt) override
            {
                mStepDeltaTime=dt;
                UpdatePoint();
            }
            void WarmStartVelocityConstraint(float ratio) override
            {
                // Keep a row for an already violated bound. Choose an inside
                // prospective side after the point and q.x rows have changed
                // the angular velocity in SolveVelocityConstraint.
                UpdateAngles(mStepDeltaTime,false,mLimited);
                mPoint.WarmStart(*mBody1,*mBody2,ratio);
                for(auto& row:mAngle)if(row.IsActive())row.WarmStart(*mBody1,*mBody2,ratio);
            }
            bool SolveVelocityConstraint(float dt) override
            {
                bool changed=mPoint.SolveVelocityConstraint(*mBody1,*mBody2);
                if(mAngle[0].IsActive())
                    changed=mAngle[0].SolveVelocityConstraint(*mBody1,*mBody2,mAxis[0],-FLT_MAX,FLT_MAX)||changed;
                if(mLimited)ActivatePredictedLimits(dt);
                for(std::size_t i=1;i<mAngle.size();++i)if(mAngle[i].IsActive())
                {
                    // An upper-bound row can only apply a negative impulse;
                    // a lower-bound row can only apply a positive impulse.
                    const float low=mSide[i]>0?-FLT_MAX:0.f;
                    const float high=mSide[i]<0?FLT_MAX:0.f;
                    changed=mAngle[i].SolveVelocityConstraint(*mBody1,*mBody2,mAxis[i],low,high)||changed;
                }
                return changed;
            }
            bool SolvePositionConstraint(float,float baumgarte) override
            {
                UpdatePoint();
                bool changed=mPoint.SolvePositionConstraint(*mBody1,*mBody2,baumgarte);
                UpdateAngles(0.f,true);
                for(std::size_t i=0;i<mPositionAngle.size();++i)if(mPositionAngle[i].IsActive())
                    changed=mPositionAngle[i].SolvePositionConstraint(*mBody1,*mBody2,mC[i],baumgarte)||changed;
                return changed;
            }
#ifdef JPH_DEBUG_RENDERER
            void DrawConstraint(JPH::DebugRenderer* renderer) const override
            {
                renderer->DrawMarker(mBody1->GetCenterOfMassTransform()*mPoint1,JPH::Color::sRed,.1f);
                renderer->DrawMarker(mBody2->GetCenterOfMassTransform()*mPoint2,JPH::Color::sGreen,.1f);
            }
#endif
            void SaveState(JPH::StateRecorder& stream) const override
            {
                JPH::TwoBodyConstraint::SaveState(stream);
                mPoint.SaveState(stream);
                for(const auto& row:mAngle)row.SaveState(stream);
                stream.Write(mPreviousQ);
                for(int side:mSide)stream.Write(side);
            }
            void RestoreState(JPH::StateRecorder& stream) override
            {
                JPH::TwoBodyConstraint::RestoreState(stream);
                mPoint.RestoreState(stream);
                for(auto& row:mAngle)row.RestoreState(stream);
                stream.Read(mPreviousQ);
                for(int& side:mSide)stream.Read(side);
            }
            JPH::Ref<JPH::ConstraintSettings> GetConstraintSettings() const override
            {
                auto* settings=new BookCvConstraintSettings;
                ToConstraintSettings(*settings);
                settings->point1=mPoint1;settings->point2=mPoint2;
                settings->frame1=mFrame1;settings->frame2=mFrame2;
                settings->halfU=mHalfU;settings->halfV=mHalfV;settings->limited=mLimited;
                return settings;
            }
            Vec3f AngularImpulse() const noexcept
            {return {mAngle[0].GetTotalLambda(),mAngle[1].GetTotalLambda(),mAngle[2].GetTotalLambda()};}
            Vec3f PointImpulse() const noexcept
            {const auto value=mPoint.GetTotalLambda();return {value.GetX(),value.GetY(),value.GetZ()};}
            JPH::Vec3 LocalPoint1() const noexcept {return mPoint1;}
            JPH::Vec3 LocalPoint2() const noexcept {return mPoint2;}
            JPH::Quat LocalFrame1() const noexcept {return mFrame1;}
            JPH::Quat LocalFrame2() const noexcept {return mFrame2;}
            float HalfAngleU() const noexcept {return mHalfU;}
            float HalfAngleV() const noexcept {return mHalfV;}
            bool Limited() const noexcept {return mLimited;}
        private:
            void UpdatePoint()
            {
                mPoint.CalculateConstraintProperties(*mBody1,JPH::Mat44::sRotation(mBody1->GetRotation()),mPoint1,
                    *mBody2,JPH::Mat44::sRotation(mBody2->GetRotation()),mPoint2);
            }
            void ActivatePredictedLimits(float horizon)
            {
                if(!(horizon>0))return;
                const auto worldFrame1=mBody1->GetRotation()*mFrame1;
                auto q=worldFrame1.Conjugated()*(mBody2->GetRotation()*mFrame2);
                if(q.GetW()<0)q=-q;
                const float w=q.GetW(),x=q.GetX(),y=q.GetY(),z=q.GetZ();
                const JPH::Vec3 gradient[2]={{-z,w,x},{y,-x,w}};
                const float component[2]={y,z};
                const float bound[2]={mBoundU,mBoundV};
                for(std::size_t index=0;index<2;++index)
                {
                    const std::size_t i=index+1;
                    if(mAngle[i].IsActive())continue;
                    if(!(component[index]>-bound[index] && component[index]<bound[index]))continue;
                    const float lengthSquared=gradient[index].LengthSq();
                    if(lengthSquared<=1.e-10f)continue;
                    const float length=std::sqrt(lengthSquared);
                    const auto axis=worldFrame1*(gradient[index]/length);
                    const float rate=axis.Dot(mBody2->GetAngularVelocity()-mBody1->GetAngularVelocity());
                    const float upperGap=2.f*(component[index]-bound[index])/length;
                    const float lowerGap=2.f*(component[index]+bound[index])/length;
                    const int side=upperGap+horizon*rate>0?1:lowerGap+horizon*rate<0?-1:0;
                    if(!side)continue;
                    mSide[i]=side;mAxis[i]=axis;
                    mC[i]=2.f*(component[index]-side*bound[index])/length;
                    const float bias=mC[i]/horizon; // Strictly inside: finite-step gap.
                    mAngle[i].CalculateConstraintProperties(*mBody1,*mBody2,mAxis[i],bias);
                }
            }
            void UpdateAngles(float horizon,bool positionPass,bool deferInsidePrediction=false)
            {
                // q = (R_A C_A)^-1 (R_B C_B). For qdot = 1/2 (0,omega_rel_A) q,
                // each component derivative is 1/2 g_i dot omega_rel_A.
                // The normalized world g_i gives the exact angular Jacobian.
                const auto worldFrame1=mBody1->GetRotation()*mFrame1;
                auto q=worldFrame1.Conjugated()*(mBody2->GetRotation()*mFrame2);
                // Limited bounds use the principal quaternion. The free CV
                // manifold keeps a continuous sign across a half turn.
                if(mLimited ? q.GetW()<0 : q.Dot(mPreviousQ)<0)q=-q;
                auto& rows=positionPass?mPositionAngle:mAngle;
                if(!positionPass && q.Dot(mPreviousQ)<0)
                    for(auto& row:mAngle)row.Deactivate();
                if(!positionPass)mPreviousQ=q;
                const float w=q.GetW(),x=q.GetX(),y=q.GetY(),z=q.GetZ();
                const JPH::Vec3 gradient[3]={{w,z,-y},{-z,w,x},{y,-x,w}};
                const float component[3]={x,y,z};
                const float bound[3]={0.f,mBoundU,mBoundV};
                for(std::size_t i=0;i<rows.size();++i)
                {
                    const float lengthSquared=gradient[i].LengthSq();
                    if(lengthSquared<=1.e-10f)
                    {rows[i].Deactivate();mC[i]=0.f;if(!positionPass)mSide[i]=0;continue;}
                    const float length=std::sqrt(lengthSquared);
                    const auto axis=worldFrame1*(gradient[i]/length);
                    int side=0;
                    if(i==0)side=2; // bilateral relation q.x = 0.
                    else if(mLimited)
                    {
                        side=component[i]>=bound[i]?1:(component[i]<=-bound[i]?-1:0);
                        if(side==0 && horizon>0 && !deferInsidePrediction)
                        {
                            const float rate=axis.Dot(mBody2->GetAngularVelocity()-mBody1->GetAngularVelocity());
                            const float upperGap=2.f*(component[i]-bound[i])/length;
                            const float lowerGap=2.f*(component[i]+bound[i])/length;
                            if(upperGap+horizon*rate>0)side=1;
                            else if(lowerGap+horizon*rate<0)side=-1;
                        }
                    }
                    if(!positionPass && side!=mSide[i])rows[i].Deactivate();
                    if(!positionPass)mSide[i]=side;
                    if(side==0)
                    {rows[i].Deactivate();mC[i]=0.f;continue;}
                    mAxis[i]=axis;
                    mC[i]=2.f*(component[i]-(i==0?0.f:side*bound[i]))/length;
                    // Inside the boundary, the finite-step gap allows motion
                    // up to the surface. Outside it, Baumgarte corrects drift.
                    const bool violated=i>0 && side*mC[i]>0;
                    const float bias=horizon>0 && i>0?(violated?.2f:1.f)*mC[i]/horizon:0.f;
                    rows[i].CalculateConstraintProperties(*mBody1,*mBody2,mAxis[i],bias);
                }
            }
            JPH::Vec3 mPoint1,mPoint2;
            JPH::Quat mFrame1,mFrame2;
            float mHalfU,mHalfV,mBoundU,mBoundV;
            float mStepDeltaTime{};
            bool mLimited;
            JPH::PointConstraintPart mPoint;
            std::array<JPH::AngleConstraintPart,3> mAngle{};
            std::array<JPH::AngleConstraintPart,3> mPositionAngle{};
            std::array<JPH::Vec3,3> mAxis{};
            std::array<float,3> mC{};
            std::array<int,3> mSide{};
            JPH::Quat mPreviousQ=JPH::Quat::sIdentity();
        };

        JPH_IMPLEMENT_SERIALIZABLE_VIRTUAL(BookCvConstraintSettings)
        {
            JPH_ADD_BASE_CLASS(BookCvConstraintSettings,JPH::TwoBodyConstraintSettings)
            JPH_ADD_ATTRIBUTE(BookCvConstraintSettings,point1)
            JPH_ADD_ATTRIBUTE(BookCvConstraintSettings,point2)
            JPH_ADD_ATTRIBUTE(BookCvConstraintSettings,frame1)
            JPH_ADD_ATTRIBUTE(BookCvConstraintSettings,frame2)
            JPH_ADD_ATTRIBUTE(BookCvConstraintSettings,halfU)
            JPH_ADD_ATTRIBUTE(BookCvConstraintSettings,halfV)
            JPH_ADD_ATTRIBUTE(BookCvConstraintSettings,limited)
        }
        JPH::TwoBodyConstraint* BookCvConstraintSettings::Create(JPH::Body& a,JPH::Body& b) const
        {return new BookCvConstraint(a,b,*this);}
        void BookCvConstraintSettings::SaveBinaryState(JPH::StreamOut& stream) const
        {
            JPH::ConstraintSettings::SaveBinaryState(stream);
            stream.Write(point1);stream.Write(point2);stream.Write(frame1);stream.Write(frame2);
            stream.Write(halfU);stream.Write(halfV);stream.Write(limited);
        }
        void BookCvConstraintSettings::RestoreBinaryState(JPH::StreamIn& stream)
        {
            JPH::ConstraintSettings::RestoreBinaryState(stream);
            stream.Read(point1);stream.Read(point2);stream.Read(frame1);stream.Read(frame2);
            stream.Read(halfU);stream.Read(halfV);stream.Read(limited);
        }
        Quat FrameFromAxes(JPH::Vec3Arg primary,JPH::Vec3Arg normal) noexcept
        {
            const Vec3f x=V(primary),y=V(normal);
            return glm::normalize(glm::quat_cast(Mat3(x,y,glm::cross(x,y))));
        }
        auto Error(PhysicsBodyErrorCode code, PhysicsBodyHandle h, std::string_view operation)
        { return std::unexpected(PhysicsBodyError{code,h,operation}); }
        JPH::EMotionType Motion(Component::BodyType type)
        {
            return type == Component::BodyType::Static ? JPH::EMotionType::Static :
                type == Component::BodyType::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
        }
        Mat3 LocalInverse(const JPH::MotionProperties& motion)
        {
            const auto inverse=motion.GetLocalSpaceInverseInertiaUnchecked();
            return Mat3(V(inverse.GetColumn3(0)),V(inverse.GetColumn3(1)),V(inverse.GetColumn3(2)));
        }
        void PublishMass(JPH::MotionProperties& motion,const PreparedBackendMass& mass) noexcept
        {
            // The owner boundary has already validated the complete Float32 payload.
            // These supported setters do not decompose, allocate or invoke callbacks.
            motion.SetInverseMass(mass.inverseMass);
            motion.SetInverseInertia(V(mass.inversePrincipal),Q(mass.principalRotation));
        }
        PhysicsBodyResult<PreparedBackendMass> PrepareAuthoredMass(PhysicsBodyHandle h,
            const PhysicalShape& shape,Component::BodyType type,float inverseMass)
        {
            if(type!=Component::BodyType::Static && type!=Component::BodyType::Kinematic && type!=Component::BodyType::Dynamic)
                return Error(PhysicsBodyErrorCode::WrongBodyType,h,"Jolt mass preparation motion type");
            if(type==Component::BodyType::Dynamic && (!Math::IsFinite(inverseMass) || inverseMass<=0))
                return Error(PhysicsBodyErrorCode::InvalidMass,h,"Jolt Dynamic mass requires positive finite inverse mass");
            const auto tensor=PhysicsInertia::Prepare(shape.InertiaTensor(),type==Component::BodyType::Dynamic?inverseMass:0.f);
            if(!tensor)return Error(PhysicsBodyErrorCode::InvalidMass,h,PhysicsInertia::ErrorMessage(tensor.error()));
            const auto principal=PhysicsInertia::PreparePrincipal(*tensor);
            if(!principal)return Error(PhysicsBodyErrorCode::InvalidMass,h,PhysicsInertia::ErrorMessage(principal.error()));
            PreparedBackendMass result{principal->inverseMass,principal->inverseDiagonal,principal->rotation};
            // Exercise the actual SDK Float32 reconstruction on disposable values.
            // Never call its assertable user-tensor eigendecomposition.
            JPH::MotionProperties probe;
            PublishMass(probe,result);
            const auto inverse=LocalInverse(probe);
            if(!PhysicsInertia::MatricesAgree(inverse,tensor->inverse) ||
                (type==Component::BodyType::Dynamic && !PhysicsInertia::InverseResidualWithin(tensor->forward,inverse)))
                return Error(PhysicsBodyErrorCode::InvalidMass,h,"Jolt Float32 principal reconstruction is not a consistent inverse");
            return result;
        }
        bool ExactMassReadback(const JPH::Body& body,const PreparedBackendMass& mass) noexcept
        {
            const auto& motion=*body.GetMotionPropertiesUnchecked();
            if(motion.GetInverseMassUnchecked()!=mass.inverseMass || Q(motion.GetInertiaRotation())!=mass.principalRotation)
                return false;
            if(body.IsDynamic())return V(motion.GetInverseInertiaDiagonal())==mass.inversePrincipal;
            return mass.inverseMass==0.f && mass.inversePrincipal==Vec3f(0) &&
                mass.principalRotation==Quat(1,0,0,0) && LocalInverse(motion)==Mat3(0.f);
        }
        JPH::ShapeSettings::ShapeResult MakeShape(const PhysicalShape& source)
        {
            if (const auto* sphere=dynamic_cast<const ShapeSphere*>(&source))
                return JPH::SphereShapeSettings(sphere->GetRadius()).Create();
            if (const auto* capsule=dynamic_cast<const ShapeCapsule*>(&source))
            {
                // Jolt's capsule requires a positive segment; zero is exactly a sphere.
                if (capsule->GetHalfLength()==0) return JPH::SphereShapeSettings(capsule->GetRadius()).Create();
                return JPH::CapsuleShapeSettings(capsule->GetHalfLength(),capsule->GetRadius()).Create();
            }
            if (source.GetShapeType()==ShapeType::Box)
            {
                const auto b=source.GetBounds(); const auto center=(b.maxs+b.mins)*.5f;
                auto shape=JPH::BoxShapeSettings(V((b.maxs-b.mins)*.5f),0.f).Create();
                if (!shape.HasError() && center!=Vec3f(0))
                    shape=JPH::RotatedTranslatedShapeSettings(V(center),JPH::Quat::sIdentity(),shape.Get()).Create();
                return shape;
            }
            if (const auto* convex = dynamic_cast<const ShapeConvex*>(&source))
            {
                // Copy the validated, already-scaled local hull. Never borrow a
                // fixture span or recenter vertices (Jolt owns its internal copy).
                JPH::ConvexHullShapeSettings settings;
                settings.mMaxConvexRadius = 0.f; // Preserve the actual sharp hull.
                settings.mPoints.reserve(convex->GetPoints().size());
                for (const auto& point : convex->GetPoints()) settings.mPoints.push_back(V(point));
                auto shape = settings.Create();
                if (shape.HasError()) return shape;
                // GEngine's authored mass/inertia is about its sampled COM.
                // Preserve that same origin without moving the collision surface.
                const auto offset = V(source.GetCenterOfMass()) - shape.Get()->GetCenterOfMass();
                if (offset != JPH::Vec3::sZero())
                    shape = JPH::OffsetCenterOfMassShapeSettings(offset, shape.Get()).Create();
                return shape;
            }
            JPH::ShapeSettings::ShapeResult error; error.SetError("Unsupported collider"); return error;
        }
        struct BodyRecord
        {
            PhysicsBodyHandle m_Handle{};
            RigidBody3D* m_Native{};
            JPH::Body* m_Body{};
            JPH::BodyID m_Id;
            float m_SpinLength{},m_RollingLength{}; // Frozen contact material for one synchronous Update.
            Vec3f m_Position{}, m_Linear{}, m_Angular{};
            Quat m_Rotation{1,0,0,0};
            float m_InverseMass{}, m_Friction{}, m_Restitution{};
            bool m_Sleeping{};
            std::uint32_t m_Layer{}, m_Mask{};
            const PhysicalShape* m_Shape{};
            std::uint64_t m_ShapeRevision{};
            void Remember()
            {
                m_Position=m_Native->m_Position; m_Rotation=m_Native->m_Orientation;
                m_Linear=m_Native->m_LinearVelocity; m_Angular=m_Native->m_AngularVelocity;
                m_InverseMass=m_Native->GetInverseMass(); m_Friction=m_Native->m_Friction;
                m_Restitution=m_Native->m_Elasticity; m_Sleeping=m_Native->IsSleeping();
                m_Layer=m_Native->m_CollisionLayer; m_Mask=m_Native->m_CollisionMask;
                m_Shape=m_Native->m_Shape; m_ShapeRevision=m_Shape->GetRevision();
            }
        };
        struct Layers final : JPH::BroadPhaseLayerInterface
        {
            JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            { return JPH::BroadPhaseLayer(layer & 1); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            { return layer.GetValue() ? "Moving" : "Static"; }
#endif
        };
        struct BroadFilter final : JPH::ObjectVsBroadPhaseLayerFilter
        {
            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override
            { return (layer & 1) || broad.GetValue() != 0; }
        };
        struct PairFilter final : JPH::ObjectLayerPairFilter
        {
            const std::array<BodyRecord,BodyCapacity>& m_Bodies;
            const std::array<std::bitset<BodyCapacity>,BodyCapacity>& m_Suppressed;
            PairFilter(const auto& bodies,const auto& suppressed):m_Bodies(bodies),m_Suppressed(suppressed){}
            bool ShouldCollide(JPH::ObjectLayer a,JPH::ObjectLayer b) const override
            {
                if (!(a&1) && !(b&1)) return false;
                const auto ai=a>>1, bi=b>>1;
                const auto& x=m_Bodies[ai]; const auto& y=m_Bodies[bi];
                return (x.m_Layer & y.m_Mask) && (y.m_Layer & x.m_Mask) && !m_Suppressed[ai][bi];
            }
        };
        struct JointRecord
        {
            PhysicsJointHandle m_Handle{};
            DistanceJointDesc m_Desc;
            JointKind m_Kind{JointKind::Distance};
            JPH::Ref<JPH::TwoBodyConstraint> m_Constraint;
            bool m_AngularImpulseValid{};
        };

        class JoltWorld final : public WorldBackend
        {
            // Factory outlives every Jolt owner. Workers join before physics and
            // allocator destruction; Update is synchronous at the Scene tick.
            Lifetime m_Lifetime;
            std::array<BodyRecord,BodyCapacity> m_Bodies{};
            std::array<std::bitset<BodyCapacity>,BodyCapacity> m_Suppressed{};
            std::array<JointRecord,PhysicsWorld::MaxDistanceJoints> m_Joints{};
            struct Synchronization {
                BodyRecord* record{};
                PreparedBackendMass mass{};
                bool massChanged{},changed{};
            };
            // Reused command-boundary scratch: no allocation or extra full slot scan.
            std::array<Synchronization,BodyCapacity> m_Synchronization;
            static_assert(sizeof(Synchronization)==48);
            static_assert(sizeof(m_Synchronization)==196608);
            Layers m_Layers;
            BroadFilter m_BroadFilter;
            PairFilter m_PairFilter{m_Bodies,m_Suppressed};
            JPH::TempAllocatorImpl m_Temporary{32 * 1024 * 1024};
            JPH::PhysicsSystem m_Physics;
            JPH::JobSystemThreadPool m_Jobs{JPH::cMaxPhysicsJobs,JPH::cMaxPhysicsBarriers,2};
            bool m_AllowSleeping{true};
            bool m_Retiring{};
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
            void PublishIdentity(const BodyRecord& record,Phase26ObservationSDK::IdentityAction action) noexcept
            {
                if(!Phase26ObservationSDK::GetSink() || !record.m_Body)return;
                auto context=ObservedContext();
                Phase26ObservationSDK::IdentityRecord value{context,record.m_Id.GetIndexAndSequenceNumber(),
                    static_cast<JPH::uint32>(record.m_Handle.index),record.m_Handle.generation,
                    record.m_Handle.registry,action};
                Phase26ObservationSDK::PublishSynthetic(context,Phase26ObservationSDK::EventKind::Identity,&value,sizeof(value));
            }
#endif

            void Wake(PhysicsBodyHandle handle)
            {
                const auto& record=m_Bodies[handle.index];
                if (record.m_Body && !record.m_Body->IsStatic())
                    m_Physics.GetBodyInterface().ActivateBody(record.m_Body->GetID());
            }
            void RefreshPair(const DistanceJointDesc& desc)
            {
                bool suppressed=false;
                for (const auto& joint:m_Joints)
                    if (joint.m_Constraint && !joint.m_Desc.collideConnected &&
                        ((joint.m_Desc.bodyA==desc.bodyA && joint.m_Desc.bodyB==desc.bodyB) ||
                         (joint.m_Desc.bodyA==desc.bodyB && joint.m_Desc.bodyB==desc.bodyA))) suppressed=true;
                m_Suppressed[desc.bodyA.index].set(desc.bodyB.index,suppressed);
                m_Suppressed[desc.bodyB.index].set(desc.bodyA.index,suppressed);
                auto& api=m_Physics.GetBodyInterface();
                api.InvalidateContactCache(m_Bodies[desc.bodyA.index].m_Body->GetID());
                api.InvalidateContactCache(m_Bodies[desc.bodyB.index].m_Body->GetID());
                Wake(desc.bodyA); Wake(desc.bodyB);
            }
            PhysicsBodyResult<const BodyRecord*> FindMassRecord(PhysicsBodyHandle h) const
            {
                if(h.index>=m_Bodies.size())return Error(PhysicsBodyErrorCode::InvalidHandle,h,"Jolt mass identity");
                const auto& record=m_Bodies[h.index];
                if(record.m_Handle!=h || !record.m_Body || !record.m_Native ||
                    m_Physics.GetBodyLockInterface().TryGetBody(record.m_Body->GetID())!=record.m_Body)
                    return Error(PhysicsBodyErrorCode::StaleHandle,h,"Jolt mass stale body");
                return &record;
            }
            PhysicsBodyResult<Synchronization> PrepareSynchronization(BodyRecord& record) const
            {
                if(!ValidContactRecord(record))
                    return Error(PhysicsBodyErrorCode::StaleHandle,record.m_Handle,"Jolt contact material body identity");
                const auto& native=*record.m_Native;
                if(!ValidResistance(native))
                    return Error(PhysicsBodyErrorCode::InvalidMaterial,record.m_Handle,"Jolt authored resistance requires finite nonnegative lengths");
                if(!native.m_Shape || native.m_Shape!=record.m_Shape || native.m_Shape->GetRevision()!=record.m_ShapeRevision)
                    return Error(PhysicsBodyErrorCode::UnsupportedFeature,record.m_Handle,"Jolt live collider mutation requires world rebuild");
                if(Motion(native.Type)!=record.m_Body->GetMotionType())
                    return Error(PhysicsBodyErrorCode::WrongBodyType,record.m_Handle,"Jolt pending motion type requires a prepared property transaction");
                if(native.Type==Component::BodyType::Dynamic && native.GetInverseMass()<=0)
                    return Error(PhysicsBodyErrorCode::InvalidMass,record.m_Handle,"Jolt pending Dynamic mass is invalid");
                Synchronization result;result.record=&record;
                result.massChanged=native.GetInverseMass()!=record.m_InverseMass;
                if(result.massChanged) {
                    auto mass=PrepareAuthoredMass(record.m_Handle,*native.m_Shape,native.Type,native.GetInverseMass());
                    if(!mass)return std::unexpected(mass.error());
                    result.mass=*mass;
                }
                result.changed=result.massChanged || native.m_Position!=record.m_Position || native.m_Orientation!=record.m_Rotation ||
                    native.m_LinearVelocity!=record.m_Linear || native.m_AngularVelocity!=record.m_Angular ||
                    native.m_Friction!=record.m_Friction || native.m_Elasticity!=record.m_Restitution ||
                    native.GetSpinResistanceLength()!=record.m_SpinLength || native.GetRollingResistanceLength()!=record.m_RollingLength ||
                    native.IsSleeping()!=record.m_Sleeping || native.m_CollisionLayer!=record.m_Layer || native.m_CollisionMask!=record.m_Mask;
                return result;
            }
            void CommitSynchronization(const Synchronization& prepared) noexcept
            {
                if(!prepared.changed)return;
                auto& record=*prepared.record;auto& native=*record.m_Native;
                auto& api=m_Physics.GetBodyInterface();const auto id=record.m_Body->GetID();
                if(prepared.massChanged)PublishMass(*record.m_Body->GetMotionPropertiesUnchecked(),prepared.mass);
                if(native.m_Position!=record.m_Position || native.m_Orientation!=record.m_Rotation)
                    api.SetPositionAndRotation(id,V(native.m_Position),Q(native.m_Orientation),JPH::EActivation::Activate);
                if(native.m_LinearVelocity!=record.m_Linear || native.m_AngularVelocity!=record.m_Angular)
                    api.SetLinearAndAngularVelocity(id,V(native.m_LinearVelocity),V(native.m_AngularVelocity));
                if(native.m_Friction!=record.m_Friction)api.SetFriction(id,native.m_Friction);
                if(native.m_Elasticity!=record.m_Restitution)api.SetRestitution(id,native.m_Elasticity);
                PublishResistance(record,native.GetSpinResistanceLength(),native.GetRollingResistanceLength());
                if(record.m_Sleeping && !native.IsSleeping())Wake(record.m_Handle);
                if(record.m_Layer!=native.m_CollisionLayer || record.m_Mask!=native.m_CollisionMask)
                    api.InvalidateContactCache(id);
                record.Remember();
            }
        public:
            PhysicsBodyResult<PreparedBackendMass> PrepareMass(PhysicsBodyHandle h,const RigidBody3D& proposed) const override
            {
                if(auto record=FindMassRecord(h);!record)return std::unexpected(record.error());
                if(!proposed.m_Shape)return Error(PhysicsBodyErrorCode::InvalidShape,h,"Jolt mass candidate has no shape");
                return PrepareAuthoredMass(h,*proposed.m_Shape,proposed.Type,proposed.GetInverseMass());
            }
            PhysicsBodyResult<PreparedBackendMass> CaptureMass(PhysicsBodyHandle h) const override
            {
                const auto record=FindMassRecord(h);if(!record)return std::unexpected(record.error());
                const auto& body=*(*record)->m_Body;const auto& motion=*body.GetMotionPropertiesUnchecked();
                PreparedBackendMass result;
                if(body.IsDynamic()) {
                    result={motion.GetInverseMass(),V(motion.GetInverseInertiaDiagonal()),Q(motion.GetInertiaRotation())};
                    const float q2=glm::dot(result.principalRotation,result.principalRotation);
                    if(!Math::IsFinite(result.inverseMass) || result.inverseMass<=0 || !Math::IsFinite(result.inversePrincipal) ||
                        glm::any(glm::lessThanEqual(result.inversePrincipal,Vec3f(0))) || !Math::IsFinite(result.principalRotation) ||
                        !std::isfinite(q2) || std::abs(q2-1.f)>64.f*std::numeric_limits<float>::epsilon())
                        return Error(PhysicsBodyErrorCode::InvalidMass,h,"Jolt actual Dynamic mass payload is invalid");
                }
                if(!ExactMassReadback(body,result))return Error(PhysicsBodyErrorCode::InvalidMass,h,"Jolt actual fixed mass payload is not canonical");
                return result;
            }
#ifdef GENGINE_CHAIN_WAKE_CHECKS
            PhysicsBodyResult<MassSynchronizationCheck> SnapshotSynchronizationForChecks(PhysicsBodyHandle h) const override
            {
                const auto record=FindMassRecord(h);if(!record)return std::unexpected(record.error());
                const auto& body=*(*record)->m_Body;
                return MassSynchronizationCheck{V(body.GetPosition()),V(body.GetLinearVelocity()),V(body.GetAngularVelocity()),
                    V(m_Physics.GetGravity()),Q(body.GetRotation()),!body.IsStatic() && !body.IsActive()};
            }
            std::size_t SynchronizationStorageBytesForChecks() const noexcept override {return sizeof(m_Synchronization);}
#endif
            PhysicsBodyResult<> ValidateImpulseActivation(PhysicsBodyHandle h,Component::BodyType type) const override
            {
                if(h.index>=m_Bodies.size())return Error(PhysicsBodyErrorCode::InvalidHandle,h,"Jolt connected impulse identity");
                const auto& r=m_Bodies[h.index];
                if(r.m_Handle!=h || !r.m_Body || !r.m_Native ||
                   m_Physics.GetBodyLockInterface().TryGetBody(r.m_Body->GetID())!=r.m_Body)
                    return Error(PhysicsBodyErrorCode::StaleHandle,h,"Jolt connected impulse stale endpoint");
                const bool matches=type==Component::BodyType::Dynamic?r.m_Body->IsDynamic():
                    type==Component::BodyType::Static?r.m_Body->IsStatic():
                    type==Component::BodyType::Kinematic && r.m_Body->IsKinematic();
                if(!matches || r.m_Native->Type!=type)
                    return Error(PhysicsBodyErrorCode::WrongBodyType,h,"Jolt connected impulse endpoint motion mismatch");
                return {};
            }
            void CommitImpulseActivation(std::span<const PhysicsBodyHandle> handles) noexcept override
            {
                // Prevalidated owner-thread boundary; Update/retirement cannot interleave.
                // ActivateBody changes activation only, never velocity or joint ownership.
                for(const auto h:handles) {
                    auto& r=m_Bodies[h.index];
                    m_Physics.GetBodyInterface().ActivateBody(r.m_Body->GetID());
                    PublishSleep(*r.m_Native,!r.m_Body->IsActive(),0);
                    r.m_Sleeping=r.m_Native->IsSleeping();
                    // Deliberately NOT Remember(): preserve unsynchronized legacy writes.
                }
            }
#include "JoltEditorPlacement.inl"
#include "JoltImpulseEditor.inl"
#include "JoltParameterEditor.inl"
            JoltWorld()
            {
                m_Physics.Init(BodyCapacity,0,16384,16384,m_Layers,m_BroadFilter,m_PairFilter);
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
                Phase26ObservationSDK::ReportStorage(Phase26ObservationSDK::ArenaSite::AdapterJointStorage,Phase26ObservationSDK::StorageAction::Capacity,m_Joints.data(),
                    sizeof(m_Joints),sizeof(m_Joints),static_cast<JPH::uint32>(m_Joints.size()));
                Phase26ObservationSDK::ReportStorage(Phase26ObservationSDK::ArenaSite::AdapterFilterStorage,Phase26ObservationSDK::StorageAction::Capacity,m_Suppressed.data(),
                    sizeof(m_Suppressed),sizeof(m_Suppressed),static_cast<JPH::uint32>(m_Suppressed.size()));
#endif
                // Preserve authored scalar coefficients and Native product combine.
                // The separately pinned extension adds only authored spin/rolling resistance.
                m_Physics.SetCombineFriction([](const JPH::Body& a,const JPH::SubShapeID&,const JPH::Body& b,const JPH::SubShapeID&)
                    { return a.GetFriction()*b.GetFriction(); });
                m_Physics.SetCombineRestitution([](const JPH::Body& a,const JPH::SubShapeID&,const JPH::Body& b,const JPH::SubShapeID&)
                    { return a.GetRestitution()*b.GetRestitution(); });
            }
            void PrepareTerminalRetirement() noexcept override
            {
                m_Retiring = true;
                m_Physics.SetContactListener(nullptr);
                m_Diagnostic.reset();
            }
            ~JoltWorld() override
            {
                m_Physics.SetContactListener(nullptr);
                for (auto& joint:m_Joints) if(joint.m_Constraint) DestroyJoint(joint.m_Handle);
                for (auto& body:m_Bodies) if(body.m_Body) DestroyBody(body.m_Handle);
                m_Physics.SetContactListener(nullptr); // Retirement may have refreshed the composed listener.
            }
            PhysicsBodyResult<> CreateBody(PhysicsBodyHandle handle,RigidBody3D& body) override
            {
                if(handle.index>=BodyCapacity) return Error(PhysicsBodyErrorCode::Capacity,handle,"Jolt CreateBody");
                if(!ValidResistance(body))return Error(PhysicsBodyErrorCode::InvalidMaterial,handle,"Jolt authored resistance lengths");
                if(body.GetSleepSettings()!=RigidBody3D::SleepSettings{})
                    return Error(PhysicsBodyErrorCode::UnsupportedFeature,handle,"Jolt custom Native sleep settings");
                if(!body.m_Shape)return Error(PhysicsBodyErrorCode::InvalidShape,handle,"Jolt body has no shape");
                const auto mass=PrepareAuthoredMass(handle,*body.m_Shape,body.Type,body.GetInverseMass());
                if(!mass)return std::unexpected(mass.error());
                auto shape = MakeShape(*body.m_Shape);
                if(shape.HasError())
                {
                    if(auto logger=Log::GetCoreLogger()) logger->error(
                        "Jolt shape creation failed: body={} type={} detail={}",
                        handle.index, int(body.m_Shape->GetShapeType()), shape.GetError().c_str());
                    return Error(PhysicsBodyErrorCode::InvalidShape,handle,"Jolt shape creation");
                }
                JPH::BodyCreationSettings settings(shape.Get(),V(body.m_Position),Q(body.m_Orientation),Motion(body.Type),
                    JPH::ObjectLayer((handle.index<<1)|(body.Type!=Component::BodyType::Static)));
                settings.mAllowDynamicOrKinematic=true;
                settings.mLinearVelocity=V(body.m_LinearVelocity); settings.mAngularVelocity=V(body.m_AngularVelocity);
                settings.mFriction=body.m_Friction; settings.mRestitution=body.m_Elasticity;
                settings.mLinearDamping=0; settings.mAngularDamping=0;
                settings.mMaxAngularVelocity=30.f;
                settings.mMaxLinearVelocity=std::numeric_limits<float>::max();
                settings.mApplyGyroscopicForce=true;
                settings.mMotionQuality=JPH::EMotionQuality::LinearCast;
                // SDK construction always initializes motion mass, even for fixed
                // bodies. A known-safe scaffold cannot take its near-zero fallback.
                // No authored tensor reaches the SDK's assertable decomposition.
                settings.mOverrideMassProperties=JPH::EOverrideMassProperties::MassAndInertiaProvided;
                settings.mMassPropertiesOverride.mMass=1.f;
                settings.mMassPropertiesOverride.mInertia=JPH::Mat44::sIdentity();
                auto& record=m_Bodies[handle.index];
                auto& api=m_Physics.GetBodyInterface();
                auto* created=api.CreateBody(settings);
                if(!created) return Error(PhysicsBodyErrorCode::Capacity,handle,"Jolt body capacity");
                PublishMass(*created->GetMotionPropertiesUnchecked(),*mass);
                if(!ExactMassReadback(*created,*mass)) {
                    api.DestroyBody(created->GetID()); // Allocated ID, never added/activated.
                    return Error(PhysicsBodyErrorCode::InvalidMass,handle,"Jolt unadded body mass readback mismatch");
                }
                record.m_Handle=handle; record.m_Native=&body; record.m_Body=created; record.m_Id=created->GetID();record.Remember();
                PublishResistance(record,body.GetSpinResistanceLength(),body.GetRollingResistanceLength(),false);
                api.AddBody(created->GetID(),body.Type==Component::BodyType::Static?JPH::EActivation::DontActivate:JPH::EActivation::Activate);
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
                PublishIdentity(record,Phase26ObservationSDK::IdentityAction::Create);
#endif
                return {};
            }
            struct EditorShapeCache {
                PhysicsBodyHandle handle{};const PhysicalShape* source{};std::uint64_t revision{};Vec3f scale{},reference{1};
                std::unique_ptr<PhysicalShape> native;JPH::RefConst<JPH::Shape> shape;
            };
            mutable std::vector<EditorShapeCache> m_EditorShapes;
            mutable std::vector<const JPH::Shape*> m_EditorQueryShapes;
            mutable std::vector<const PhysicalShape*> m_EditorQueryNative;
            mutable std::vector<Vec3f> m_EditorQueryScales;
            mutable std::vector<Bounds> m_EditorQueryBounds;
            mutable std::vector<std::size_t> m_EditorQueryOrder;
            struct ShapeChange final : ColliderChange
            {
                JoltWorld& owner; BodyRecord& record; JPH::RefConst<JPH::Shape> shape;
                PreparedBackendMass mass;
                ShapeChange(JoltWorld& o,BodyRecord& r,const JPH::Shape* s,const PreparedBackendMass& m):owner(o),record(r),shape(s),mass(m){}
                void Commit() noexcept override
                {
                    auto& api=owner.m_Physics.GetBodyInterface(); const auto id=record.m_Body->GetID();
                    api.SetShape(id,shape,false,JPH::EActivation::DontActivate);
                    const auto& native=*record.m_Native;
                    PublishMass(*record.m_Body->GetMotionPropertiesUnchecked(),mass);
                    api.SetPositionAndRotation(id,V(native.m_Position),Q(native.m_Orientation),JPH::EActivation::Activate);
                    for(auto& joint:owner.m_Joints) if(joint.m_Constraint &&
                        (joint.m_Desc.bodyA==record.m_Handle || joint.m_Desc.bodyB==record.m_Handle))
                    {
                        // Public anchors are measured from the shape COM (BodySpaceToWorldSpace).
                        // Preserve that convention: local anchor coordinates stay unchanged.
                        joint.m_Constraint->ResetWarmStart();
                        joint.m_AngularImpulseValid=false;
                        owner.Wake(joint.m_Desc.bodyA); owner.Wake(joint.m_Desc.bodyB);
                    }
                    // Only these fields were published. In particular, retain any
                    // pending legacy velocity/material writes for the next Step.
                    record.m_Shape=native.m_Shape;record.m_ShapeRevision=native.m_Shape->GetRevision();
                    record.m_Position=native.m_Position;record.m_Rotation=native.m_Orientation;
                    record.m_InverseMass=mass.inverseMass;
                    record.m_Sleeping=!record.m_Body->IsStatic() && !record.m_Body->IsActive();
                }
            };
            PhysicsBodyResult<std::unique_ptr<ColliderChange>> PrepareCollider(PhysicsBodyHandle h,const PhysicalShape& source) override
            {
                const auto record=FindMassRecord(h);if(!record)return std::unexpected(record.error());
                const auto& native=*(*record)->m_Native;
                // Native still owns the old shape here. Use the candidate argument.
                const auto mass=PrepareAuthoredMass(h,source,native.Type,native.GetInverseMass());
                if(!mass)return std::unexpected(mass.error());
                auto shape=MakeShape(source);
                if(shape.HasError())return Error(PhysicsBodyErrorCode::InvalidShape,h,"Jolt collider staging");
                std::unique_ptr<ColliderChange> result(new(std::nothrow) ShapeChange(*this,m_Bodies[h.index],shape.Get(),*mass));
                if(!result)return Error(PhysicsBodyErrorCode::Allocation,h,"Jolt collider staging");
                return result;
            }
            void CommitPose(PhysicsBodyHandle h,const RigidBody3D& body) noexcept override
            {
                auto& record=m_Bodies[h.index];auto& api=m_Physics.GetBodyInterface();
                api.SetPositionAndRotation(record.m_Body->GetID(),V(body.m_Position),Q(body.m_Orientation),JPH::EActivation::Activate);
                api.InvalidateContactCache(record.m_Body->GetID());
                record.m_Position=body.m_Position;record.m_Rotation=body.m_Orientation;
            }
            void UpdateProperties(PhysicsBodyHandle h,const RigidBody3D& proposed,const PhysicsBodyProperties& p,
                const PreparedBackendMass& mass) noexcept override
            {
                auto& record=m_Bodies[h.index];auto& api=m_Physics.GetBodyInterface();auto id=record.m_Body->GetID();
                // A formerly fixed body must have real mass before it can activate.
                // Conversely, preserve Dynamic state until fixed transition completes.
                if(p.type==Component::BodyType::Dynamic)PublishMass(*record.m_Body->GetMotionPropertiesUnchecked(),mass);
                api.SetMotionType(id,Motion(p.type),JPH::EActivation::DontActivate);
                if(p.type!=Component::BodyType::Dynamic)PublishMass(*record.m_Body->GetMotionPropertiesUnchecked(),mass);
                api.SetObjectLayer(id,JPH::ObjectLayer((h.index<<1)|(p.type!=Component::BodyType::Static)));
                auto* motion=record.m_Body->GetMotionPropertiesUnchecked();
                motion->SetLinearDamping(p.linearDamping);motion->SetAngularDamping(p.angularDamping);
                motion->SetGravityFactor(p.gravityFactor);
                motion->SetNumVelocityStepsOverride(unsigned(p.velocityStepsOverride));
                motion->SetNumPositionStepsOverride(unsigned(p.positionStepsOverride));
                api.SetMotionQuality(id,p.continuousCollision?JPH::EMotionQuality::LinearCast:JPH::EMotionQuality::Discrete);
                record.m_Body->SetAllowSleeping(p.allowSleeping);
                api.SetFriction(id,p.material.friction);api.SetRestitution(id,p.material.restitution);
                PublishResistance(record,p.material.spinResistanceLength,p.material.rollingResistanceLength);
                if(p.type!=Component::BodyType::Static)
                    api.SetLinearAndAngularVelocity(id,V(proposed.m_LinearVelocity),V(proposed.m_AngularVelocity));
                api.InvalidateContactCache(id);Wake(h);
                for(auto& joint:m_Joints)if(joint.m_Constraint && (joint.m_Desc.bodyA==h || joint.m_Desc.bodyB==h))
                {joint.m_Constraint->ResetWarmStart();Wake(joint.m_Desc.bodyA);Wake(joint.m_Desc.bodyB);}
                // Do not Remember pending legacy fields. This cache describes the
                // actual published mass, including exact rollback of an older SDK mass.
                record.m_InverseMass=mass.inverseMass;
            }
            void ReadProperties(PhysicsBodyHandle h,PhysicsBodyProperties& p) const noexcept override
            {
                const auto& record=m_Bodies[h.index];const auto& b=*record.m_Body;
                p.material.friction=b.GetFriction();p.material.restitution=b.GetRestitution();
                p.material.spinResistanceLength=record.m_SpinLength;p.material.rollingResistanceLength=record.m_RollingLength;
                const auto* motion=b.GetMotionPropertiesUnchecked();
                p.linearDamping=motion->GetLinearDamping();p.angularDamping=motion->GetAngularDamping();
                p.gravityFactor=motion->GetGravityFactor();p.allowSleeping=b.GetAllowSleeping();
                p.velocityStepsOverride=int(motion->GetNumVelocityStepsOverride());p.positionStepsOverride=int(motion->GetNumPositionStepsOverride());
                p.continuousCollision=motion->GetMotionQuality()==JPH::EMotionQuality::LinearCast;
            }
            PhysicsBodyResult<> ReadEffectiveBody(PhysicsBodyState& state) const override
            {
                const auto record = FindMassRecord(state.handle);
                if (!record) return std::unexpected(record.error());
                const auto& body = *(*record)->m_Body;
                const auto* shape = body.GetShape();
                auto subtype = shape->GetSubType();
                if (subtype == JPH::EShapeSubType::RotatedTranslated || subtype == JPH::EShapeSubType::OffsetCenterOfMass) {
                    // MakeShape decorates only offset boxes and COM-adjusted convex hulls.
                    // Use the inner type for classification and the outer shape for geometry.
                    const auto* inner = static_cast<const JPH::DecoratedShape*>(shape)->GetInnerShape();
                    const auto expected = subtype == JPH::EShapeSubType::RotatedTranslated ?
                        JPH::EShapeSubType::Box : JPH::EShapeSubType::ConvexHull;
                    if (!inner || inner->GetSubType() != expected)
                        return Error(PhysicsBodyErrorCode::UnsupportedFeature, state.handle,
                            "Effective readback encountered an unsupported decorated collider");
                    subtype = inner->GetSubType();
                }
                switch (subtype) {
                case JPH::EShapeSubType::Box: state.shapeType = ShapeType::Box; break;
                case JPH::EShapeSubType::Sphere: state.shapeType = ShapeType::Sphere; break;
                case JPH::EShapeSubType::Capsule: state.shapeType = ShapeType::Capsule; break;
                case JPH::EShapeSubType::ConvexHull: state.shapeType = ShapeType::Convex; break;
                default: return Error(PhysicsBodyErrorCode::UnsupportedFeature, state.handle,
                    "Effective readback requires a supported primitive or convex hull");
                }
                state.type = body.IsDynamic() ? Component::BodyType::Dynamic :
                    body.IsStatic() ? Component::BodyType::Static : Component::BodyType::Kinematic;
                state.position = V(body.GetPosition());
                state.orientation = Q(body.GetRotation());
                state.linearVelocity = V(body.GetLinearVelocity());
                state.angularVelocity = V(body.GetAngularVelocity());
                state.centerOfMass = V(body.GetCenterOfMassPosition());
                const auto bounds = shape->GetLocalBounds();
                state.localBounds.mins = V(bounds.mMin + shape->GetCenterOfMass());
                state.localBounds.maxs = V(bounds.mMax + shape->GetCenterOfMass());
                const auto& worldBounds = body.GetWorldSpaceBounds();
                state.worldBounds.mins = V(worldBounds.mMin);
                state.worldBounds.maxs = V(worldBounds.mMax);
                const auto& motion = *body.GetMotionPropertiesUnchecked();
                state.inverseMass = motion.GetInverseMassUnchecked();
                state.inverseInertiaBody = LocalInverse(motion);
                state.inverseInertiaWorld = Mat3(0.f);
                if (body.IsDynamic()) {
                    const auto inverse = body.GetInverseInertia();
                    state.inverseInertiaWorld = Mat3(V(inverse.GetColumn3(0)),
                        V(inverse.GetColumn3(1)), V(inverse.GetColumn3(2)));
                }
                state.material.friction = body.GetFriction();
                state.material.restitution = body.GetRestitution();
                state.material.spinResistanceLength = (*record)->m_SpinLength;
                state.material.rollingResistanceLength = (*record)->m_RollingLength;
                state.sleeping = !body.IsStatic() && !body.IsActive();
                return {};
            }
            PhysicsWorldSleepSettings ReadWorldSleepSettings() const noexcept override
            {
                const auto& settings=m_Physics.GetPhysicsSettings();
                return {settings.mPointVelocitySleepThreshold,settings.mTimeBeforeSleep};
            }
            void UpdateWorldSleepSettings(const PhysicsWorldSleepSettings& value) noexcept override
            {
                auto settings=m_Physics.GetPhysicsSettings();
                settings.mPointVelocitySleepThreshold=value.pointVelocityThreshold;
                settings.mTimeBeforeSleep=value.timeBeforeSleep;
                m_Physics.SetPhysicsSettings(settings);
                // A new test starts at Apply. No body/joint recreation, contact
                // invalidation, solver change or scheduler step is needed.
                for(auto& record:m_Bodies)if(record.m_Body && !record.m_Body->IsStatic())
                {
                    Wake(record.m_Handle);record.m_Body->ResetSleepTimer();
                    PublishSleep(*record.m_Native,false,0);record.Remember();
                }
            }
            void DestroyBody(PhysicsBodyHandle handle) noexcept override
            {
                if(handle.index>=BodyCapacity) return;
                auto& record=m_Bodies[handle.index];
                if(record.m_Handle!=handle || !record.m_Body) return;
                for(auto& joint:m_Joints) if(joint.m_Constraint &&
                    (joint.m_Desc.bodyA==handle || joint.m_Desc.bodyB==handle)) DestroyJoint(joint.m_Handle);
                auto& api=m_Physics.GetBodyInterface(); const auto id=record.m_Body->GetID();
                if (!m_Retiring) PublishResistance(record,0,0,false);
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
                PublishIdentity(record,Phase26ObservationSDK::IdentityAction::Destroy);
#endif
                api.RemoveBody(id); api.DestroyBody(id); record={};
                m_Suppressed[handle.index].reset();
            }
#if defined(GENGINE_PHASE29_STEP_SAFETY)
            std::uint64_t PublicationFingerprintForTest(bool sdkState) const noexcept override
            {
                std::uint64_t hash = 14695981039346656037ULL;
                const auto add = [&](const auto& value) {
                    const auto bytes = std::as_bytes(std::span(&value, 1));
                    for (auto byte : bytes) { hash ^= std::to_integer<unsigned>(byte); hash *= 1099511628211ULL; }
                };
                if (sdkState) {
                    const auto gravity=V(m_Physics.GetGravity()); add(gravity);
                    add(m_Physics.GetPhysicsSettings().mAllowSleeping);
                    for (const auto& r:m_Bodies) if (r.m_Body) {
                        const auto& b=*r.m_Body;
                        add(V(b.GetPosition())); add(Q(b.GetRotation())); add(V(b.GetLinearVelocity()));
                        add(V(b.GetAngularVelocity())); add(b.IsActive());
                        if (b.IsDynamic()) add(b.GetMotionProperties()->GetInverseMass());
                    }
                    return hash;
                }
                for (const auto& r : m_Bodies) if (r.m_Body) {
                    const auto& b = *r.m_Native;
                    add(b.m_Position); add(b.m_Orientation); add(b.m_LinearVelocity); add(b.m_AngularVelocity);
                    add(b.IsSleeping()); add(b.GetInactiveSeconds()); add(r.m_Position); add(r.m_Rotation);
                    add(r.m_Linear); add(r.m_Angular); add(r.m_Sleeping); add(r.m_InverseMass);
                    add(r.m_Friction); add(r.m_Restitution); add(r.m_Layer); add(r.m_Mask); add(r.m_ShapeRevision);
                }
                for (const auto& j : m_Joints) if (j.m_Constraint) add(j.m_AngularImpulseValid);
                if (m_Diagnostic) add(m_Diagnostic->step);
                return hash;
            }
#endif
            BackendStepResult Step(float dt,const Vec3f& gravity,bool sleeping) override
            {
#ifdef GENGINE_JOLT_FINAL_AUDIT
                const auto auditStart=JoltAudit::Now();
#endif
                if(!Math::IsFinite(dt) || dt<0 || !Math::IsFinite(gravity))
                    return std::unexpected(PhysicsStepFailure{{PhysicsBodyErrorCode::InvalidDuration,{},"Jolt Step"},PhysicsStepStage::Preparation});
                if(dt==0) return {};
                std::size_t synchronizeCount{};
                // Complete preparation before ANY SDK gravity, sleeping, body or
                // cache publication. Reuse the original pre-sync slot traversal.
                for(auto& record:m_Bodies) if(record.m_Body) {
                    const auto prepared=PrepareSynchronization(record);
                    if(!prepared)return std::unexpected(PhysicsStepFailure{prepared.error(),PhysicsStepStage::Preparation});
                    if(prepared->changed)m_Synchronization[synchronizeCount++]=*prepared;
                }
                m_Physics.SetGravity(V(gravity));
                sleeping=sleeping && m_UserAllowSleeping; // Existing master sleep gate remains authoritative.
                if(sleeping!=m_AllowSleeping)
                { auto settings=m_Physics.GetPhysicsSettings();settings.mAllowSleeping=sleeping;m_Physics.SetPhysicsSettings(settings);
                  if(!sleeping)for(const auto& record:m_Bodies)if(record.m_Body)Wake(record.m_Handle);m_AllowSleeping=sleeping; }
                for(std::size_t i=0;i<synchronizeCount;++i)CommitSynchronization(m_Synchronization[i]);
                // Contact material is immutable throughout Update; diagnostics remain optional.
                m_ContactIdentityFailure.store(false,std::memory_order_relaxed);
                if(m_Diagnostic)m_Diagnostic->count.store(0);
                const auto timingStart=m_Diagnostic?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
                if(auto* sink=Phase26ObservationSDK::GetSink();sink && sink->update){
                    if(sink->world==nullptr)sink->world=&m_Physics;
                    else if(sink->world!=&m_Physics)Phase26ObservationSDK::ReportFailure(Phase26ObservationSDK::FailureCode::WrongOwner);
                }
                struct TempProxy final : JPH::TempAllocator {
                    JPH::TempAllocatorImpl& base;
                    explicit TempProxy(JPH::TempAllocatorImpl& value):base(value){}
                    void Report(JPH::uint n,void* p,Phase26ObservationSDK::StorageAction action) noexcept {
                        if(!Phase26ObservationSDK::GetSink())return;
                        auto context=Phase26ObservationSDK::CurrentContext();context.site=Phase26ObservationSDK::ArenaSite::Unknown;
                        Phase26ObservationSDK::StorageRecord record{context,std::uint64_t(reinterpret_cast<std::uintptr_t>(p)),n,
                            n?JPH::AlignUp(n,static_cast<JPH::uint>(JPH_RVECTOR_ALIGNMENT)):0,base.GetUsage(),1,action};
                        Phase26ObservationSDK::PublishSynthetic(context,Phase26ObservationSDK::EventKind::Storage,&record,sizeof(record));
                    }
                    void* Allocate(JPH::uint n) override {
#if defined(GENGINE_PHASE28_QUALIFICATION) && defined(GENGINE_JOLT_FINAL_AUDIT)
                        Phase28ObserverTimer observerTimer(JoltAudit::countAllocations.load(std::memory_order_relaxed));
#endif
                        auto* p=base.Allocate(n);Report(n,p,Phase26ObservationSDK::StorageAction::Reserve);return p;}
                    void Free(void* p,JPH::uint n) override {
#if defined(GENGINE_PHASE28_QUALIFICATION) && defined(GENGINE_JOLT_FINAL_AUDIT)
                        Phase28ObserverTimer observerTimer(JoltAudit::countAllocations.load(std::memory_order_relaxed));
#endif
                        base.Free(p,n);Report(n,p,Phase26ObservationSDK::StorageAction::Release);}
                } proxy(m_Temporary);
                const auto error=m_Physics.Update(dt,m_CollisionSteps,&proxy,&m_Jobs);
#elif defined(GENGINE_JOLT_FINAL_AUDIT) || defined(GENGINE_G8_RESISTANCE_CHECKS)
                struct TempProxy final : JPH::TempAllocator {
                    JPH::TempAllocatorImpl& base;
                    explicit TempProxy(JPH::TempAllocatorImpl& b):base(b){}
                    void* Allocate(JPH::uint n) override {
#if defined(GENGINE_PHASE28_QUALIFICATION) && defined(GENGINE_JOLT_FINAL_AUDIT)
                        Phase28ObserverTimer observerTimer(JoltAudit::countAllocations.load(std::memory_order_relaxed));
#endif
                        auto* p=base.Allocate(n);
#ifdef GENGINE_JOLT_FINAL_AUDIT
                        ++JoltAudit::tempCalls;JoltAudit::tempBytes+=n;JoltAudit::tempPeak=std::max(JoltAudit::tempPeak,std::uint64_t(base.GetUsage()));
#if defined(GENGINE_PHASE27_QUALIFICATION) || defined(GENGINE_PHASE28_QUALIFICATION)
                        if(Phase27Audit::arena)Phase27Audit::arena({reinterpret_cast<std::uintptr_t>(p),reinterpret_cast<std::uintptr_t>(_ReturnAddress()),n,n?JPH::AlignUp(n,JPH::uint(JPH_RVECTOR_ALIGNMENT)):0,base.GetUsage(),true});
#endif
#endif
#ifdef GENGINE_G8_RESISTANCE_CHECKS
                        G8ResistanceObservation::Temporary(base.GetUsage());
#endif
                        return p;
                    }
                    void Free(void* p,JPH::uint n) override {
#if defined(GENGINE_PHASE28_QUALIFICATION) && defined(GENGINE_JOLT_FINAL_AUDIT)
                        Phase28ObserverTimer observerTimer(JoltAudit::countAllocations.load(std::memory_order_relaxed));
#endif
                        base.Free(p,n);
#if defined(GENGINE_PHASE27_QUALIFICATION) || defined(GENGINE_PHASE28_QUALIFICATION)
                        if(Phase27Audit::arena)Phase27Audit::arena({reinterpret_cast<std::uintptr_t>(p),reinterpret_cast<std::uintptr_t>(_ReturnAddress()),n,n?JPH::AlignUp(n,JPH::uint(JPH_RVECTOR_ALIGNMENT)):0,base.GetUsage(),false});
#endif
                    }
                } proxy(m_Temporary);
#ifdef GENGINE_JOLT_FINAL_AUDIT
                const auto auditUpdate=JoltAudit::Now();
                JoltAudit::ns[2]+=auditUpdate-auditStart;
                const auto error=m_Physics.Update(dt,m_CollisionSteps,JoltAudit::countAllocations?static_cast<JPH::TempAllocator*>(&proxy):&m_Temporary,&m_Jobs);
                const auto auditPublish=JoltAudit::Now();
                JoltAudit::ns[3]+=auditPublish-auditUpdate;
#else
                const auto observationStart=std::chrono::steady_clock::now();
                const bool observed=G8ResistanceObservation::enabled.load(std::memory_order_relaxed);
                const auto error=m_Physics.Update(dt,m_CollisionSteps,observed?static_cast<JPH::TempAllocator*>(&proxy):&m_Temporary,&m_Jobs);
                if(observed) {
                    G8ResistanceObservation::updateNs.fetch_add(std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-observationStart).count()),std::memory_order_relaxed);
                    G8ResistanceObservation::updateCalls.fetch_add(1,std::memory_order_relaxed);
                }
#endif
#else
                const auto error=m_Physics.Update(dt,m_CollisionSteps,&m_Temporary,&m_Jobs);
#endif
                auto mask = static_cast<std::uint32_t>(error);
#if defined(GENGINE_PHASE29_STEP_SAFETY)
                const auto inject = std::exchange(nextTestFault, TestFault::None);
                const BodyRecord* invalidRecord = nullptr;
                if (inject == TestFault::LastMirror) {
                    for (auto it=m_Bodies.rbegin();it!=m_Bodies.rend();++it)
                        if (it->m_Body) { invalidRecord=&*it; break; }
                }
                if (inject == TestFault::Capacity)
                    mask |= static_cast<std::uint32_t>(JPH::EPhysicsUpdateError::ManifoldCacheFull);
#endif
                const auto sdkFault = [&](PhysicsBodyErrorCode code, std::string_view operation) {
                    return std::unexpected(PhysicsStepFailure{{code,{},operation},PhysicsStepStage::SdkUpdate,mask});
                };
                if (m_ContactIdentityFailure.load(std::memory_order_relaxed))
                    return sdkFault(PhysicsBodyErrorCode::StaleHandle,"Jolt contact callback identity failure");
                if (mask & static_cast<std::uint32_t>(JPH::EPhysicsUpdateError::ContactResistanceInvalid))
                    return sdkFault(PhysicsBodyErrorCode::NonFiniteResult,"Jolt authored contact resistance failed");
                if (mask) return sdkFault(PhysicsBodyErrorCode::Capacity,"Jolt collision capacity exceeded");
                const double observedMs = m_Diagnostic ?
                    std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-timingStart).count() : 0.0;
                std::size_t liveCount = 0;
                for (auto& record : m_Bodies) if (record.m_Body) {
                    const auto fault = [&](PhysicsBodyErrorCode code, std::string_view operation) {
                        return std::unexpected(PhysicsStepFailure{{code,record.m_Handle,operation},PhysicsStepStage::Publication});
                    };
                    if (!ValidContactRecord(record)) return fault(PhysicsBodyErrorCode::StaleHandle,"Jolt output identity");
                    const auto& body = *record.m_Body;
                    const auto& native = *record.m_Native;
                    const auto q = Q(body.GetRotation());
                    const double norm = std::sqrt(double(q.w)*q.w + double(q.x)*q.x + double(q.y)*q.y + double(q.z)*q.z);
                    if (!std::isfinite(norm)) return fault(PhysicsBodyErrorCode::NonFiniteResult,"Jolt output rotation");
                    if (std::abs(norm-1.0)>1.e-5) return fault(PhysicsBodyErrorCode::InvalidPose,"Jolt output rotation norm");
                    auto position = V(body.GetPosition());
#if defined(GENGINE_PHASE29_STEP_SAFETY)
                    if (&record == invalidRecord) position.x = std::numeric_limits<float>::quiet_NaN();
#endif
                    const double inactive = !body.IsStatic() && !body.IsActive() ? native.GetInactiveSeconds()+dt : 0.0;
                    if (!Math::IsFinite(position) || !Math::IsFinite(V(body.GetLinearVelocity())) ||
                        !Math::IsFinite(V(body.GetAngularVelocity())) || !Math::IsFinite(native.GetInverseMass()) ||
                        !Math::IsFinite(native.m_Friction) || !Math::IsFinite(native.m_Elasticity) || !std::isfinite(inactive))
                        return fault(PhysicsBodyErrorCode::NonFiniteResult,"Jolt body mirror publication");
                    m_Synchronization[liveCount++].record = &record;
                }
                // Synchronous Update has joined its workers. No caller can mutate bodies
                // between validation and this no-fail commit on the owning thread.
                for (std::size_t i=0;i<liveCount;++i) {
                    auto& record = *m_Synchronization[i].record;
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
                    PublishIdentity(record,Phase26ObservationSDK::IdentityAction::Snapshot);
#endif
                    auto& native=*record.m_Native;const auto& body=*record.m_Body;
                    native.m_Position=V(body.GetPosition());native.m_Orientation=Q(body.GetRotation());
                    native.m_LinearVelocity=V(body.GetLinearVelocity());native.m_AngularVelocity=V(body.GetAngularVelocity());
                    PublishSleep(native,!body.IsStatic() && !body.IsActive(),dt);
                    record.Remember();
                }
                if (m_Diagnostic) m_Diagnostic->Finish(observedMs);
                // Row lambdas are a provider measurement only after a completed
                // active update. Creation, reset, retirement, and sleeping do not
                // advertise stale or fabricated impulse samples.
                for(auto& joint:m_Joints)if(joint.m_Constraint &&
                    (joint.m_Kind==JointKind::ConstantVelocity ||
                     joint.m_Kind==JointKind::LimitedConstantVelocity))
                    joint.m_AngularImpulseValid=joint.m_Constraint->IsActive();
#ifdef GENGINE_JOLT_FINAL_AUDIT
                JoltAudit::ns[4]+=JoltAudit::Now()-auditPublish;
                JoltAudit::bodyVisits+=m_Bodies.size()*2+liveCount;
#endif
                return {};
            }
            JointResult<> CreateJoint(PhysicsJointHandle handle,const DistanceJointDesc& desc) override
            {
                if(handle.index>=m_Joints.size() || desc.bodyA.index>=BodyCapacity || desc.bodyB.index>=BodyCapacity)
                    return std::unexpected(JointError{JointErrorCode::Capacity,handle,"Jolt CreateJoint"});
                auto& a=m_Bodies[desc.bodyA.index]; auto& b=m_Bodies[desc.bodyB.index];
                if(a.m_Handle!=desc.bodyA || b.m_Handle!=desc.bodyB || !a.m_Body || !b.m_Body)
                    return std::unexpected(JointError{JointErrorCode::InvalidEndpoint,handle,"Jolt CreateJoint"});
                // Capture anchors against the latest API-authored poses, even
                // when creation occurs before the next simulation tick.
                const auto stagedA=PrepareSynchronization(a),stagedB=PrepareSynchronization(b);
                if(!stagedA || !stagedB)
                    return std::unexpected(JointError{JointErrorCode::InvalidEndpoint,handle,"Jolt endpoint synchronization"});
                CommitSynchronization(*stagedA);CommitSynchronization(*stagedB);
                JPH::DistanceConstraintSettings settings;
                settings.mSpace=JPH::EConstraintSpace::WorldSpace;
                settings.mPoint1=V(a.m_Native->BodySpaceToWorldSpace(desc.localAnchorA));
                settings.mPoint2=V(b.m_Native->BodySpaceToWorldSpace(desc.localAnchorB));
                settings.mMinDistance=settings.mMaxDistance=desc.length;
                auto& joint=m_Joints[handle.index];
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
                Phase26ObservationSDK::SiteScope phase26_joint_site(Phase26ObservationSDK::ArenaSite::JointContainer);
#endif
                joint.m_Constraint=new JPH::DistanceConstraint(*a.m_Body,*b.m_Body,settings);
                joint.m_Handle=handle;joint.m_Desc=desc;joint.m_Kind=JointKind::Distance;
                m_Physics.AddConstraint(joint.m_Constraint);RefreshPair(desc);
                return {};
            }
            JointResult<> CreateJoint(PhysicsJointHandle handle,const BallSocketJointDesc& desc) override
            {
                if(handle.index>=m_Joints.size() || desc.bodyA.index>=BodyCapacity || desc.bodyB.index>=BodyCapacity)
                    return std::unexpected(JointError{JointErrorCode::Capacity,handle,"Jolt CreatePointJoint"});
                auto& a=m_Bodies[desc.bodyA.index]; auto& b=m_Bodies[desc.bodyB.index];
                if(a.m_Handle!=desc.bodyA || b.m_Handle!=desc.bodyB || !a.m_Body || !b.m_Body)
                    return std::unexpected(JointError{JointErrorCode::InvalidEndpoint,handle,"Jolt CreatePointJoint"});
                const auto stagedA=PrepareSynchronization(a),stagedB=PrepareSynchronization(b);
                if(!stagedA || !stagedB)
                    return std::unexpected(JointError{JointErrorCode::InvalidEndpoint,handle,"Jolt point endpoint synchronization"});
                CommitSynchronization(*stagedA);CommitSynchronization(*stagedB);
                JPH::PointConstraintSettings settings;
                settings.mSpace=JPH::EConstraintSpace::WorldSpace;
                settings.mPoint1=V(a.m_Native->BodySpaceToWorldSpace(desc.localAnchorA));
                settings.mPoint2=V(b.m_Native->BodySpaceToWorldSpace(desc.localAnchorB));
                auto& joint=m_Joints[handle.index];
                joint.m_Constraint=new JPH::PointConstraint(*a.m_Body,*b.m_Body,settings);
                joint.m_Handle=handle; joint.m_Kind=JointKind::BallSocket;
                joint.m_Desc={desc.bodyA,desc.bodyB,desc.localAnchorA,desc.localAnchorB,0,desc.collideConnected};
                m_Physics.AddConstraint(joint.m_Constraint);RefreshPair(joint.m_Desc);
                return {};
            }
            JointResult<> CreateJoint(PhysicsJointHandle handle,const AngularJointDesc& desc) override
            {
                if(handle.index>=m_Joints.size() || desc.bodyA.index>=BodyCapacity || desc.bodyB.index>=BodyCapacity)
                    return std::unexpected(JointError{JointErrorCode::Capacity,handle,"Jolt CreateAngularJoint"});
                auto& a=m_Bodies[desc.bodyA.index];auto& b=m_Bodies[desc.bodyB.index];
                if(a.m_Handle!=desc.bodyA || b.m_Handle!=desc.bodyB || !a.m_Body || !b.m_Body)
                    return std::unexpected(JointError{JointErrorCode::InvalidEndpoint,handle,"Jolt CreateAngularJoint"});
                const auto stagedA=PrepareSynchronization(a),stagedB=PrepareSynchronization(b);
                if(!stagedA || !stagedB)
                    return std::unexpected(JointError{JointErrorCode::InvalidEndpoint,handle,"Jolt angular endpoint synchronization"});
                CommitSynchronization(*stagedA);CommitSynchronization(*stagedB);
                const auto worldA=V(a.m_Native->BodySpaceToWorldSpace(desc.localAnchorA));
                const auto worldB=V(b.m_Native->BodySpaceToWorldSpace(desc.localAnchorB));
                if(desc.kind==JointKind::ConstantVelocity || desc.kind==JointKind::LimitedConstantVelocity)
                {
                    const auto frameA=a.m_Body->GetRotation()*Q(desc.angular.localFrameA).Normalized();
                    const auto frameB=b.m_Body->GetRotation()*Q(desc.angular.localFrameB).Normalized();
                    const auto q=frameA.Conjugated()*frameB;
                    const float gradientSquared=q.GetW()*q.GetW()+q.GetY()*q.GetY()+q.GetZ()*q.GetZ();
                    if(!(gradientSquared>1.e-8f))
                        return std::unexpected(JointError{JointErrorCode::InvalidGeometry,handle,"Jolt CV singular initial frame"});
                }
                auto& joint=m_Joints[handle.index];
#ifdef JPH_GENGINE_PHASE26_OBSERVATION
                Phase26ObservationSDK::SiteScope phase26_joint_site(Phase26ObservationSDK::ArenaSite::JointContainer);
#endif
                if(desc.kind==JointKind::Hinge)
                {
                    JPH::HingeConstraintSettings settings;
                    settings.mSpace=JPH::EConstraintSpace::WorldSpace;
                    settings.mPoint1=worldA;settings.mPoint2=worldB;
                    const auto frameA=a.m_Body->GetRotation()*Q(desc.angular.localFrameA).Normalized();
                    const auto frameB=b.m_Body->GetRotation()*Q(desc.angular.localFrameB).Normalized();
                    settings.mHingeAxis1=frameA.RotateAxisX();settings.mNormalAxis1=frameA.RotateAxisY();
                    settings.mHingeAxis2=frameB.RotateAxisX();settings.mNormalAxis2=frameB.RotateAxisY();
                    if(desc.angular.limitsEnabled)
                    {settings.mLimitsMin=desc.angular.lower;settings.mLimitsMax=desc.angular.upper;}
                    joint.m_Constraint=desc.angular.limitsEnabled?
                        static_cast<JPH::TwoBodyConstraint*>(new BookHingeConstraint(*a.m_Body,*b.m_Body,settings)):
                        static_cast<JPH::TwoBodyConstraint*>(new JPH::HingeConstraint(*a.m_Body,*b.m_Body,settings));
                }
                else
                {
                    BookCvConstraintSettings settings;
                    settings.point1=JPH::Vec3(a.m_Body->GetInverseCenterOfMassTransform()*worldA);
                    settings.point2=JPH::Vec3(b.m_Body->GetInverseCenterOfMassTransform()*worldB);
                    settings.frame1=Q(desc.angular.localFrameA).Normalized();
                    settings.frame2=Q(desc.angular.localFrameB).Normalized();
                    settings.halfU=desc.angular.halfAngleU;settings.halfV=desc.angular.halfAngleV;
                    settings.limited=desc.kind==JointKind::LimitedConstantVelocity;
                    joint.m_Constraint=new BookCvConstraint(*a.m_Body,*b.m_Body,settings);
                }
                joint.m_Handle=handle;joint.m_Kind=desc.kind;
                joint.m_Desc={desc.bodyA,desc.bodyB,desc.localAnchorA,desc.localAnchorB,0,desc.collideConnected};
                joint.m_AngularImpulseValid=false;
                m_Physics.AddConstraint(joint.m_Constraint);RefreshPair(joint.m_Desc);
                return {};
            }
            JointResult<AngularJointDesc> ReadAngularJoint(PhysicsJointHandle handle) const override
            {
                if(handle.index>=m_Joints.size())
                    return std::unexpected(JointError{JointErrorCode::InvalidHandle,handle,"Jolt ReadAngularJoint"});
                const auto& joint=m_Joints[handle.index];
                if(joint.m_Handle!=handle || !joint.m_Constraint)
                    return std::unexpected(JointError{JointErrorCode::StaleHandle,handle,"Jolt ReadAngularJoint"});
                if(joint.m_Kind!=JointKind::Hinge && joint.m_Kind!=JointKind::ConstantVelocity &&
                   joint.m_Kind!=JointKind::LimitedConstantVelocity)
                    return std::unexpected(JointError{JointErrorCode::WrongKind,handle,"Jolt ReadAngularJoint"});
                // GEngine and Jolt both express these local points from the
                // body's center of mass. Read the actual SDK point rather than
                // remapping through a potentially pending authored world pose.
                AngularJointDesc result{joint.m_Kind,joint.m_Desc.bodyA,joint.m_Desc.bodyB,
                    {},{},joint.m_Desc.collideConnected,{}};
                if(joint.m_Kind==JointKind::Hinge)
                {
                    const auto* hinge=NativeHinge(joint.m_Constraint.GetPtr());
                    result.localAnchorA=V(hinge->GetLocalSpacePoint1());
                    result.localAnchorB=V(hinge->GetLocalSpacePoint2());
                    result.angular.localFrameA=FrameFromAxes(hinge->GetLocalSpaceHingeAxis1(),
                        hinge->GetLocalSpaceNormalAxis1());
                    result.angular.localFrameB=FrameFromAxes(hinge->GetLocalSpaceHingeAxis2(),
                        hinge->GetLocalSpaceNormalAxis2());
                    result.angular.lower=hinge->GetLimitsMin();
                    result.angular.upper=hinge->GetLimitsMax();
                    result.angular.limitsEnabled=hinge->HasLimits();
                }
                else
                {
                    const auto* cv=static_cast<const BookCvConstraint*>(joint.m_Constraint.GetPtr());
                    result.kind=cv->Limited()?JointKind::LimitedConstantVelocity:JointKind::ConstantVelocity;
                    result.localAnchorA=V(cv->LocalPoint1());result.localAnchorB=V(cv->LocalPoint2());
                    result.angular.localFrameA=Q(cv->LocalFrame1());
                    result.angular.localFrameB=Q(cv->LocalFrame2());
                    result.angular.halfAngleU=cv->HalfAngleU();
                    result.angular.halfAngleV=cv->HalfAngleV();
                    result.angular.limitsEnabled=cv->Limited();
                }
                if(!AngularJoint::Valid(result))
                    return std::unexpected(JointError{JointErrorCode::NonFinite,handle,"Jolt angular provider readback"});
                return result;
            }
            Vec3f JointVectorImpulse(PhysicsJointHandle handle) const noexcept override
            {
                if(handle.index>=m_Joints.size())return {};
                const auto& j=m_Joints[handle.index];
                if(j.m_Handle!=handle || !j.m_Constraint)return {};
                if(j.m_Kind==JointKind::BallSocket)
                {
                    const auto v=static_cast<const JPH::PointConstraint*>(j.m_Constraint.GetPtr())->GetTotalLambdaPosition();
                    return {v.GetX(),v.GetY(),v.GetZ()};
                }
                if(j.m_Kind==JointKind::Hinge)
                {
                    const auto v=NativeHinge(j.m_Constraint.GetPtr())->GetTotalLambdaPosition();
                    return {v.GetX(),v.GetY(),v.GetZ()};
                }
                if(j.m_Kind==JointKind::ConstantVelocity || j.m_Kind==JointKind::LimitedConstantVelocity)
                    return static_cast<const BookCvConstraint*>(j.m_Constraint.GetPtr())->PointImpulse();
                return {};
            }
            Vec3f AngularImpulse(PhysicsJointHandle handle) const noexcept override
            {
                if(handle.index>=m_Joints.size())return {};
                const auto& joint=m_Joints[handle.index];
                if(joint.m_Handle!=handle || !joint.m_Constraint || !joint.m_AngularImpulseValid)return {};
                // Hinge's SDK position pass may deactivate a velocity limit row
                // and clear its cached lambda before this public readback.
                // Report unavailable rather than claim that cache is the full
                // completed velocity-row impulse.
                if(joint.m_Kind==JointKind::ConstantVelocity || joint.m_Kind==JointKind::LimitedConstantVelocity)
                    return static_cast<const BookCvConstraint*>(joint.m_Constraint.GetPtr())->AngularImpulse();
                return {};
            }
            bool AngularImpulseAvailable(PhysicsJointHandle handle) const noexcept override
            {
                if(handle.index>=m_Joints.size())return false;
                const auto& joint=m_Joints[handle.index];
                return joint.m_Handle==handle && joint.m_Constraint && joint.m_AngularImpulseValid &&
                    (joint.m_Kind==JointKind::ConstantVelocity || joint.m_Kind==JointKind::LimitedConstantVelocity);
            }
            void DestroyJoint(PhysicsJointHandle handle) noexcept override
            {
                if(handle.index>=m_Joints.size())return;
                auto& joint=m_Joints[handle.index];if(joint.m_Handle!=handle || !joint.m_Constraint)return;
                auto desc=joint.m_Desc;m_Physics.RemoveConstraint(joint.m_Constraint);joint={};if (!m_Retiring) RefreshPair(desc);
            }
            void ResetJoint(PhysicsJointHandle handle) noexcept override
            { if(handle.index>=m_Joints.size())return;auto& joint=m_Joints[handle.index];if(joint.m_Handle==handle && joint.m_Constraint){joint.m_Constraint->ResetWarmStart();joint.m_AngularImpulseValid=false;} }
            float JointImpulse(PhysicsJointHandle handle) const noexcept override
            { const auto& j=m_Joints[handle.index];return j.m_Handle==handle && j.m_Constraint?(j.m_Kind==JointKind::Distance?static_cast<const JPH::DistanceConstraint*>(j.m_Constraint.GetPtr())->GetTotalLambdaPosition():glm::length(JointVectorImpulse(handle))):0; }
            bool JointActive(PhysicsJointHandle handle) const noexcept override
            { const auto& j=m_Joints[handle.index];return j.m_Handle==handle && j.m_Constraint && j.m_Constraint->IsActive(); }
        };
    }
#endif
    PhysicsBodyResult<std::unique_ptr<WorldBackend>> CreateJoltBackend()
    {
#ifdef GENGINE_WITH_JOLT
        if(!AcquireJolt())return std::unexpected(PhysicsBodyError{PhysicsBodyErrorCode::BackendInitialization,{},"Jolt factory ownership"});
        std::unique_ptr<WorldBackend> backend(new(std::nothrow) JoltWorld);
        if(!backend){ReleaseJolt();return std::unexpected(PhysicsBodyError{PhysicsBodyErrorCode::Allocation,{},"Jolt world"});}
        return backend;
#else
        return std::unexpected(PhysicsBodyError{PhysicsBodyErrorCode::BackendUnavailable,{},"Jolt not enabled in this build"});
#endif
    }
}
