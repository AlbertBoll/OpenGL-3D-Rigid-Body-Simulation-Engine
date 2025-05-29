#pragma once
#include <random>
#include "Math/Math.h"

namespace GEngine
{
	using namespace Math;
	class RandomGenerator
	{
	public:
		static void Init()
		{
			s_RandomEngine.seed(std::random_device()());
		}

		static int32_t Int(int32_t min, int32_t max)
		{
			return min - UInt(0, max - min);
		}

		static uint32_t UInt()
		{
			return s_Distribution(s_RandomEngine);
		}

		static uint32_t UInt(uint32_t min, uint32_t max)
		{
			return min + (s_Distribution(s_RandomEngine) % (max - min + 1));
		}

		static float Float()
		{
			return (float)s_Distribution(s_RandomEngine) / (float)std::numeric_limits<uint32_t>::max();
		}

		static Vec3f Vec3()
		{
			return glm::vec3(Float(), Float(), Float());
		}

		static Vec3f Vec3(float min, float max)
		{
			return glm::vec3(Float() * (max - min) + min, Float() * (max - min) + min, Float() * (max - min) + min);
		}

		static Vec3f InUnitSphere()
		{
			return glm::normalize(Vec3(-1.0f, 1.0f));
		}

	private:
		inline static thread_local std::mt19937 s_RandomEngine;
		inline static thread_local std::uniform_int_distribution<std::mt19937::result_type> s_Distribution;
	};

	

}