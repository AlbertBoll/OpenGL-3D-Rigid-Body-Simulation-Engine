#pragma once
#include "Timestep.h"
#include <chrono>
#include <iostream>


namespace GEngine
{
	class Timer
	{
	public:
		using Clock = std::chrono::steady_clock;
		using TimePoint = Clock::time_point;
		explicit Timer(TimePoint now = Clock::now()) : m_Start(now) {}

		~Timer() { std::cout << ElapsedMilliSeconds() << "ms" << std::endl; }

		void Reset(TimePoint now = Clock::now())
		{
			m_Start = now;
		}

		Seconds ElapsedDuration(TimePoint now = Clock::now()) const
		{
			return now >= m_Start ? Seconds(now - m_Start) : Seconds::zero();
		}

		// Legacy float API returns seconds; new measurements can retain the duration.
		float Elapsed(TimePoint now = Clock::now()) const { return ElapsedSeconds(now); }

		float ElapsedMilliSeconds(TimePoint now = Clock::now()) const
		{
			return static_cast<float>(std::chrono::duration<double, std::milli>(ElapsedDuration(now)).count());
		}

		float ElapsedSeconds(TimePoint now = Clock::now()) const
		{
			return static_cast<float>(ElapsedDuration(now).count());
		}

	private:
		TimePoint m_Start;

	};

#define Timeit(x) std::cout<<#x<<": ";\
Timer timer;

#define Timeit_()\
Timer timer;

}
