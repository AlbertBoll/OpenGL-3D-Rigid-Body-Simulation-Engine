#include "gepch.h"
#include "PhysicsProfile.h"

#include <chrono>

namespace GEngine
{
#ifdef GE_ENABLE_PHYSICS_PROFILING
	namespace
	{
		PhysicsProfileSnapshot s_PhysicsProfile;

		std::uint64_t NowNs()
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		}
	}

	namespace PhysicsProfileDetail
	{
		PhysicsProfileSnapshot& MutableSnapshot()
		{
			return s_PhysicsProfile;
		}

		ScopedTimer::ScopedTimer(std::uint64_t PhysicsProfileSnapshot::* field)
			: m_Field(field), m_StartNs(NowNs())
		{
		}

		ScopedTimer::~ScopedTimer()
		{
			Stop();
		}

		void ScopedTimer::Stop()
		{
			if (!m_Running)
			{
				return;
			}

			MutableSnapshot().*m_Field += NowNs() - m_StartNs;
			m_Running = false;
		}

		GjkCallProfiler::GjkCallProfiler()
			: m_Timer(&PhysicsProfileSnapshot::gjkTimeNs)
		{
			++MutableSnapshot().gjkCallCount;
		}

		GjkCallProfiler::~GjkCallProfiler()
		{
			auto& snapshot = MutableSnapshot();
			snapshot.gjkIterationCount += m_Iterations;
			if (m_Iterations > snapshot.gjkMaxIterations)
			{
				snapshot.gjkMaxIterations = m_Iterations;
			}
		}

		void GjkCallProfiler::Iteration()
		{
			++m_Iterations;
		}
	}
#endif

	void ResetPhysicsProfile()
	{
#ifdef GE_ENABLE_PHYSICS_PROFILING
		s_PhysicsProfile = {};
#endif
	}

	PhysicsProfileSnapshot GetPhysicsProfileSnapshot()
	{
#ifdef GE_ENABLE_PHYSICS_PROFILING
		return s_PhysicsProfile;
#else
		return {};
#endif
	}

	bool IsPhysicsProfilingEnabled()
	{
#ifdef GE_ENABLE_PHYSICS_PROFILING
		return true;
#else
		return false;
#endif
	}
}
