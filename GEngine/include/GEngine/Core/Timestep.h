#pragma once
#include <algorithm>
#include <chrono>
#include <limits>

namespace GEngine
{
	using Seconds = std::chrono::duration<double>;

	class Timestep
	{
	public:
		// Numeric seconds remain a compatibility boundary for existing consumers.
		Timestep(double seconds = 0.0): m_Duration{seconds} {}
		explicit Timestep(Seconds duration): m_Duration{duration} {}

		Seconds GetDuration() const { return m_Duration; }

		float GetSeconds()const { return FloatValue(m_Duration.count()); }

		double GetSecondsPrecise()const { return m_Duration.count(); }

		float GetMilliseconds()const { return FloatValue(std::chrono::duration<double, std::milli>(m_Duration).count()); }

		operator float() const
		{
			return GetSeconds();
		}


	private:
		static float FloatValue(double time)
		{
			constexpr double limit = std::numeric_limits<float>::max();
			// Bound the cast itself, even when MSVC folds a discarded overflow branch.
			const float finiteRange = static_cast<float>(std::clamp(time, -limit, limit));
			return time > limit ? std::numeric_limits<float>::infinity() :
				time < -limit ? -std::numeric_limits<float>::infinity() : finiteRange;
		}

		Seconds m_Duration;

	};
}
