#pragma once

#include <cstdint>
#include <array>
#include <chrono>
#include <span>
#include "PhysicsBody.h"
#include "GJK.h"
#include "Contact.h"

namespace GEngine { class PhysicsWorld; class ManifoldCollector; struct ContactIsland; }

// Owner-thread, opt-in observation. No callback is installed by normal consumers.
// Borrowed bodies/worlds are valid only during the callback; observers never step
// or mutate Physics. Boundary recording is identical with detailed tracing off/on.
namespace GEngine::PhysicsDiagnostic
{
    enum class Stage { Begin, Wake, Gravity, Broadphase, Narrowphase, WarmStart,
        Islands, Solve, Integration, Correction, Sleep, End };
    enum class Operation { WarmStart, NormalSliding, Spin, Rolling, Position,
        BlockNormal, ToiNormal, ToiFriction, ToiPosition, ContactNew, ContactRefresh, ContactRemove,
        PairBegin, PairEnd, PairReset, PatchBegin, PatchEnd, PointMap, ContactSnapshot,
        SleepDecision, GravityImpulse, Integrate, ToiQuery };
    struct BodyState
    {
        Vec3f position{}, linear{}, angular{};
        Quat orientation{1,0,0,0};
        double inactive{};
        bool sleeping{};
    };
    inline BodyState Capture(const RigidBody3D& b) noexcept
    {
        return {b.m_Position,b.m_LinearVelocity,b.m_AngularVelocity,b.m_Orientation,
                b.GetInactiveSeconds(),b.IsSleeping()};
    }
    struct Event
    {
        Operation operation{};
        const RigidBody3D* a{};
        const RigidBody3D* b{};
        BodyState beforeA{}, beforeB{}, afterA{}, afterB{};
        Vec3f normal{}, anchorA{}, anchorB{}, linearImpulseB{}, angularImpulseB{};
        std::array<double,3> accumulatedBefore{}, accumulatedAfter{};
        double restitution{}, closingSpeed{}, penetration{};
        // Targeted observation metadata only. No value is consumed by Physics.
        Vec3f linearImpulseA{}, angularImpulseA{}, localA{}, localB{}, tangentU{}, tangentV{};
        std::array<double,12> normalRow{}, tangentRowU{}, tangentRowV{};
        std::uint64_t featureA{}, featureB{};
        int slot{-1}, previousSlot{-1}, reason{};
        double time{}, duration{}, bias{};
        std::uintptr_t constraintToken{};
        bool appliedImpulsesKnown{};
    };
    struct Observer
    {
        void* context{};
        bool detailed{};
        bool (*select)(void*,const RigidBody3D*,const RigidBody3D*) noexcept{};
        void (*event)(void*,const Event&) noexcept{};
        void (*boundary)(void*,Stage,const PhysicsWorld&,ManifoldCollector&,
                         std::span<const ContactIsland>,std::size_t,int) noexcept{};
        bool targeted{};
        bool (*valid)(void*,const RigidBody3D*,const RigidBody3D*) noexcept{};
        void (*violation)(void*) noexcept{};
        std::uint64_t observationNs{};
        int contactSlot{-1};
    };
    inline thread_local Observer* observer{};
    inline std::uint64_t Nanoseconds() noexcept
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    inline bool SameState(const BodyState& a,const BodyState& b) noexcept
    {
        return a.position==b.position && a.linear==b.linear && a.angular==b.angular &&
            a.orientation==b.orientation && a.inactive==b.inactive && a.sleeping==b.sleeping;
    }
    // P20_QUERY_OBSERVATION_BEGIN
    // Separate value-only query evidence. No sink is installed by normal consumers;
    // the caller never consumes these fields to decide a Physics result.
    enum class QueryKind { Begin, ContactSearch, ClosestPoints, Result, SpeedTerms, Speed, Time, Exit };
    enum class QueryExit { None, Contact, IterationLimit, InvalidOrNonclosingSpeed,
        InvalidTime, TimeBeyondRemaining, IntervalExhausted,
        ContactSearchFailed, ClosestDistanceFailed, InvalidDistance, NoProgress };
    struct CaContractObservation
    {
        int queryOutcome{-1};
        QueryExit queryReason{}, exit{};
        GjkDiagnostics diagnostics{};
        unsigned queries{}, advances{};
        float remaining{}, toi{};
        contact_t contact{};
        BodyState predictedA{}, predictedB{};
    };
    CaContractObservation EvaluateCaContract(unsigned scenario,
        RigidBody3D* seedA, RigidBody3D* seedB);
    struct ProjectionPrimitiveObservation
    {
        EpaTermination termination{EpaTermination::NotRun};
        int selectedFace{-1};
        Vec3f normal{}, lambdas{}, projectedPoint{}, expectedProjection{}, witnessA{}, witnessB{};
        float originDistance{}, lambdaSum{}, projectionErrorSquared{}, projectionScaleSquared{};
        float penetrationDistance{};
        std::uint32_t failedPredicates{};
    };
    ProjectionPrimitiveObservation EvaluateProjectionPrimitive(unsigned scenario);
    struct QueryEvent
    {
        QueryKind kind{};
        QueryExit exit{};
        unsigned iteration{};
        int contactStatus{-1};
        GjkDiagnostics diagnostics{};
        BodyState a{}, b{};
        contact_t contact{};
        Vec3f witnessA{}, witnessB{}, direction{}, relativeVelocity{};
        float remaining{}, toi{}, projectedSpeed{}, angularA{}, angularB{}, speed{}, timeToGo{};
        bool hit{};
    };
    // P20_EPA_OBSERVATION_BEGIN
    // Ephemeral final-projection operands. Spans are borrowed for one synchronous
    // callback only; the fixture owns any bounded persistent copy.
    inline constexpr std::size_t EpaCaptureMaxPoints = 68;
    inline constexpr std::size_t EpaCaptureMaxFaces = 256;
    struct EpaPoint { Vec3f xyz{}, ptA{}, ptB{}; };
    struct EpaFace { int a{}, b{}, c{}; };
    struct EpaProjectionEvent
    {
        unsigned iteration{}, epaIterations{};
        std::size_t pointCount{}, faceCount{};
        int selectedFace{-1};
        bool overflow{};
        std::span<const EpaPoint> points{};
        std::span<const EpaFace> faces{};
        EpaTermination convergenceReason{EpaTermination::NotRun};
        float bias{}, originDistance{}, lambdaSum{};
        float projectionErrorSquared{}, projectionScaleSquared{};
        Vec3f normal{}, lambdas{}, projectedPoint{}, expectedProjection{};
        // Bits 0..7: nonfinite weights, nonfinite sum, sum error, negative
        // X/Y/Z weight, nonfinite residual, residual exceeds existing bound.
        std::uint32_t failedPredicates{};
        EpaPoint lastSupport{};
        Vec3f lastSupportNormal{};
        float lastOriginDistance{}, lastExpansionDistance{}, lastConvergenceTolerance{};
        int lastSelectedFace{-1};
    };
    enum class BarycentricExit { Weights, NormalFallback, AreaFallback, NonfiniteWeightsFallback };
    struct BarycentricEvent
    {
        unsigned iteration{};
        BarycentricExit exit{BarycentricExit::Weights};
        // Bits 0..4 identify operands actually computed by the original path:
        // normal squared, projected origin, dominant area, subareas, raw weights.
        std::uint32_t validFields{};
        float normalLengthSquared{}, dominantArea{};
        int dominantAxis{-1};
        Vec3f projectedOrigin{}, subareas{}, rawWeights{}, returnedWeights{};
    };
    // P20_EPA_OBSERVATION_END
    struct QueryObserver
    {
        void* context{};
        void (*event)(void*,const QueryEvent&) noexcept{};
        unsigned iteration{};
        std::uint64_t observationNs{};
        // P20_EPA_OBSERVATION_BEGIN
        void (*epaProjection)(void*, const EpaProjectionEvent&) noexcept{};
        void (*barycentric)(void*, const BarycentricEvent&) noexcept{};
        // P20_EPA_OBSERVATION_END
    };
    inline thread_local QueryObserver* queryObserver{};
    inline void QueryIteration(unsigned iteration) noexcept
    { if (queryObserver) queryObserver->iteration=iteration; }
    template<class Fill> inline void QueryRecord(Fill&& fill) noexcept
    {
        if (!queryObserver || !queryObserver->event) return;
        auto* sink=queryObserver;
        const auto start=Nanoseconds();
        QueryEvent value{}; value.iteration=sink->iteration;
        fill(value);
        sink->event(sink->context,value);
        sink->observationNs+=Nanoseconds()-start;
    }
    // P20_EPA_OBSERVATION_BEGIN
    template<class Fill> inline void BarycentricRecord(Fill&& fill) noexcept
    {
        if (!queryObserver || !queryObserver->barycentric) return;
        auto* sink = queryObserver;
        const auto start = Nanoseconds();
        BarycentricEvent value{};
        value.iteration = sink->iteration;
        fill(value);
        sink->barycentric(sink->context, value);
        sink->observationNs += Nanoseconds() - start;
    }
    // P20_EPA_OBSERVATION_END
    // P20_QUERY_OBSERVATION_END
    class Observation
    {
        Observer* m_Observer{};
    public:
        Event value{};
        Observation(Operation op,const RigidBody3D* a,const RigidBody3D* b,bool targetedOnly=false) noexcept
        {
            if (!observer || !observer->detailed || !a || !b ||
                (targetedOnly && !observer->targeted) ||
                (observer->select && !observer->select(observer->context,a,b))) return;
            const auto start=observer->targeted?Nanoseconds():0;
            if (observer->valid && !observer->valid(observer->context,a,b)) return;
            m_Observer=observer;
            value.operation=op; value.a=a; value.b=b;
            if (m_Observer->targeted && op<=Operation::BlockNormal) value.slot=m_Observer->contactSlot;
            value.beforeA=Capture(*a); value.beforeB=Capture(*b);
            if (m_Observer->targeted) m_Observer->observationNs+=Nanoseconds()-start;
        }
        explicit operator bool() const noexcept { return m_Observer!=nullptr; }
        void Finish() noexcept
        {
            if (!m_Observer) return;
            const auto start=m_Observer->targeted?Nanoseconds():0;
            if (m_Observer->valid && !m_Observer->valid(m_Observer->context,value.a,value.b)) { m_Observer=nullptr; return; }
            value.afterA=Capture(*value.a); value.afterB=Capture(*value.b);
            m_Observer->event(m_Observer->context,value);
            if (m_Observer->targeted) {
                if ((!SameState(value.afterA,Capture(*value.a)) || !SameState(value.afterB,Capture(*value.b))) &&
                    m_Observer->violation) m_Observer->violation(m_Observer->context);
                m_Observer->observationNs+=Nanoseconds()-start;
            }
            m_Observer=nullptr;
        }
        ~Observation() { Finish(); }
        Observation(const Observation&)=delete;
        Observation& operator=(const Observation&)=delete;
    };
    // No retained pointer, allocation or Physics decision; immediately copied by the sink.
    inline void Mark(Operation op,const RigidBody3D* a,const RigidBody3D* b,
                     int slot=-1,int previous=-1,int reason=0) noexcept
    {
        Observation record(op,a,b,true);
        if (record) { record.value.slot=slot; record.value.previousSlot=previous; record.value.reason=reason; }
    }
    inline void SetSlot(int slot) noexcept
    {
        if (observer && observer->targeted) observer->contactSlot=slot;
    }
}

namespace GEngine
{
	struct PhysicsProfileSnapshot
	{
		std::uint64_t stepCount{};

		std::uint64_t bodyCount{};
		std::uint64_t dynamicBodyCount{};
		std::uint64_t activeBodyCount{};
		std::uint64_t sleepingBodyCount{};

		std::uint64_t candidatePairCount{};
		std::uint64_t broadphaseTimeNs{};
		std::uint64_t broadphaseAxisOverlapCount{};
		std::uint64_t broadphaseAabbRejectedCount{};
		std::uint64_t broadphaseStaticPairRejectedCount{};
		std::uint64_t broadphaseMaskRejectedCount{};
		std::uint64_t broadphaseInsertionSortSwapCount{};
		std::uint64_t broadphaseFullSortCount{};
		std::uint64_t pairFilterCheckCount{};
		std::uint64_t pairFilterRejectedCount{};
		std::uint64_t pairFilterTimeNs{};

		std::uint64_t narrowphaseCallCount{};
		std::uint64_t narrowphaseTimeNs{};
		std::uint64_t gjkCallCount{};
		std::uint64_t gjkTimeNs{};
		std::uint64_t gjkIterationCount{};
		std::uint64_t gjkMaxIterations{};
		std::uint64_t supportCallCount{};
		std::uint64_t supportTimeNs{};
		std::uint64_t epaCallCount{};
		std::uint64_t epaTimeNs{};
		// Counts attempted EPA expansions only; GJK separation/seeding is excluded.
		std::uint64_t epaIterationCount{};
		std::uint64_t epaMaxIterations{};
		std::uint64_t epaFailureCount{};

		std::uint64_t generatedContactCount{};
		std::uint64_t manifoldCount{};
		std::uint64_t manifoldContactCount{};
		std::uint64_t manifoldTimeNs{};
		std::uint64_t solverConstraintCount{};
		std::uint64_t solverIterationCount{};
		std::uint64_t solverTimeNs{};
		std::uint64_t contactResolutionTimeNs{};
		// Enabled torsional rows visited and nonzero incremental angular impulses applied.
		// Inclusive subset of solver time; zero-coefficient rows do not start a timer.
		std::uint64_t spinResistanceTimeNs{};
		std::uint64_t spinResistanceSolveCount{};
		std::uint64_t spinResistanceImpulseCount{};
		// Enabled rolling rows and nonzero incremental angular impulses; subset of solver time.
		std::uint64_t rollingResistanceTimeNs{};
		std::uint64_t rollingResistanceSolveCount{};
		std::uint64_t rollingResistanceImpulseCount{};

		std::uint64_t gravityTimeNs{};
		std::uint64_t integratedBodyCount{};
		std::uint64_t integrationTimeNs{};
		std::uint64_t physicsWorldTimeNs{};

		// Graph-only inclusive timing; growth counts vector reallocations, not all heap calls.
		std::uint64_t contactIslandBuildTimeNs{};
		std::uint64_t contactIslandNodesTimeNs{};
		std::uint64_t contactIslandEdgesTimeNs{};
		std::uint64_t contactIslandOutputTimeNs{};
		std::uint64_t contactIslandCapacityGrowthCount{};
		std::uint64_t contactIslandNodeSortCount{};
		std::uint64_t contactIslandEdgeSortCount{};
	};

	void ResetPhysicsProfile();
	PhysicsProfileSnapshot GetPhysicsProfileSnapshot();
	bool IsPhysicsProfilingEnabled();

#ifdef GE_ENABLE_PHYSICS_PROFILING
	namespace PhysicsProfileDetail
	{
		PhysicsProfileSnapshot& MutableSnapshot();

		class ScopedTimer
		{
		public:
			explicit ScopedTimer(std::uint64_t PhysicsProfileSnapshot::* field);
			~ScopedTimer();

			void Stop();

		private:
			std::uint64_t PhysicsProfileSnapshot::* m_Field;
			std::uint64_t m_StartNs;
			bool m_Running{ true };
		};

		class GjkCallProfiler
		{
		public:
			GjkCallProfiler();
			~GjkCallProfiler();

			void Iteration();

		private:
			ScopedTimer m_Timer;
			std::uint64_t m_Iterations{};
		};
	}
#endif
}

#define GE_PHYSICS_PROFILE_JOIN_IMPL(a, b) a##b
#define GE_PHYSICS_PROFILE_JOIN(a, b) GE_PHYSICS_PROFILE_JOIN_IMPL(a, b)

#ifdef GE_ENABLE_PHYSICS_PROFILING
	#define GE_PHYSICS_PROFILE_SCOPE(field) \
		::GEngine::PhysicsProfileDetail::ScopedTimer GE_PHYSICS_PROFILE_JOIN(gePhysicsProfileScope_, __LINE__)(&::GEngine::PhysicsProfileSnapshot::field)
	#define GE_PHYSICS_PROFILE_SCOPE_NAMED(name, field) \
		::GEngine::PhysicsProfileDetail::ScopedTimer name(&::GEngine::PhysicsProfileSnapshot::field)
	#define GE_PHYSICS_PROFILE_STOP(name) name.Stop()
	#define GE_PHYSICS_PROFILE_ADD(field, value) \
		(::GEngine::PhysicsProfileDetail::MutableSnapshot().field += static_cast<std::uint64_t>(value))
	#define GE_PHYSICS_PROFILE_SET(field, value) \
		(::GEngine::PhysicsProfileDetail::MutableSnapshot().field = static_cast<std::uint64_t>(value))
	#define GE_PHYSICS_PROFILE_MAX(field, value) \
		do { \
			auto& gePhysicsProfileValue = ::GEngine::PhysicsProfileDetail::MutableSnapshot().field; \
			const auto gePhysicsProfileCandidate = static_cast<std::uint64_t>(value); \
			if (gePhysicsProfileCandidate > gePhysicsProfileValue) gePhysicsProfileValue = gePhysicsProfileCandidate; \
		} while (false)
	#define GE_PHYSICS_PROFILE_GJK_CALL() \
		::GEngine::PhysicsProfileDetail::GjkCallProfiler gePhysicsGjkCallProfiler
	#define GE_PHYSICS_PROFILE_GJK_ITERATION() gePhysicsGjkCallProfiler.Iteration()
#else
	#define GE_PHYSICS_PROFILE_SCOPE(field) ((void)0)
	#define GE_PHYSICS_PROFILE_SCOPE_NAMED(name, field) ((void)0)
	#define GE_PHYSICS_PROFILE_STOP(name) ((void)0)
	#define GE_PHYSICS_PROFILE_ADD(field, value) ((void)0)
	#define GE_PHYSICS_PROFILE_SET(field, value) ((void)0)
	#define GE_PHYSICS_PROFILE_MAX(field, value) ((void)0)
	#define GE_PHYSICS_PROFILE_GJK_CALL() ((void)0)
	#define GE_PHYSICS_PROFILE_GJK_ITERATION() ((void)0)
#endif
