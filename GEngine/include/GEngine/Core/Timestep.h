#pragma once
#include <algorithm>
#include <limits>

namespace GEngine
{
	class Timestep
	{
	public:
		Timestep(double time = 0.0): m_Time{time}, m_FloatTime{FloatSeconds(time)} {}

		float GetSeconds()const { return m_FloatTime; }

		double GetSecondsPrecise()const { return m_Time; }

		float GetMilliseconds()const { return static_cast<float>(m_Time * 1000.0); }

		operator float() const
		{
			return m_FloatTime;
		}


	private:
		static float FloatSeconds(double time)
		{
			constexpr double limit = std::numeric_limits<float>::max();
			// Bound the cast itself, even when MSVC folds a discarded overflow branch.
			const float finiteRange = static_cast<float>(std::clamp(time, -limit, limit));
			return time > limit ? std::numeric_limits<float>::infinity() :
				time < -limit ? -std::numeric_limits<float>::infinity() : finiteRange;
		}

		double m_Time;
		float m_FloatTime;

	};
}
