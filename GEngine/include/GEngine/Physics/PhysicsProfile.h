#pragma once

#include <cstdint>

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

		std::uint64_t generatedContactCount{};
		std::uint64_t manifoldCount{};
		std::uint64_t manifoldContactCount{};
		std::uint64_t manifoldTimeNs{};
		std::uint64_t solverConstraintCount{};
		std::uint64_t solverIterationCount{};
		std::uint64_t solverTimeNs{};
		std::uint64_t contactResolutionTimeNs{};

		std::uint64_t gravityTimeNs{};
		std::uint64_t integratedBodyCount{};
		std::uint64_t integrationTimeNs{};
		std::uint64_t physicsWorldTimeNs{};
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
