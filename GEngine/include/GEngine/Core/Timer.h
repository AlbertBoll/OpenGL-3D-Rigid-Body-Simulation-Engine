#pragma once
#include <chrono>
#include <iostream>


namespace GEngine
{
	class Timer
	{
	public:
		Timer() { Reset(); }

		~Timer() { std::cout << ElapsedMilliSeconds() << "ms" << std::endl; }

		void Reset()
		{
			m_Start = std::chrono::high_resolution_clock::now();
		}

		float Elapsed() const
		{
			return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - m_Start).count() * 0.001f * 0.001f * 0.001f;
		}

		float ElapsedMilliSeconds() const
		{
			return Elapsed() * 1000.0f;
		}

		float ElapsedSeconds() const
		{
			return ElapsedMilliSeconds() * 1000.0f;
		}

	private:
		std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;

	};

#define Timeit(x) std::cout<<#x<<": ";\
Timer timer;

#define Timeit_()\
Timer timer;

}
