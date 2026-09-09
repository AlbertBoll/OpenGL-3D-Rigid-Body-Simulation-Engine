#pragma once

namespace GEngine
{
	class Timestep
	{
	public:
		Timestep(double time = 0.0): m_Time{time}, m_FloatTime{static_cast<float>(time)}{}

		float GetSeconds()const { return m_FloatTime; }

		double GetSecondsPrecise()const { return m_Time; }

		float GetMilliseconds()const { return static_cast<float>(m_Time * 1000.0); }

		operator float() const
		{
			return m_FloatTime;
		}


	private:
		double m_Time;
		float m_FloatTime;

	};
}
