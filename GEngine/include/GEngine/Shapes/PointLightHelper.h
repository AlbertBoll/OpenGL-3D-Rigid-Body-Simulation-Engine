#pragma once

#include <Math/Math.h>
#include"Shapes/Sphere.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace GEngine::Shape
{

	class PointLightHelper : public Sphere
	{
	public:
		PointLightHelper(float size = 0.5f, float radius_segment = 32.f, float height_segment = 32.f) :
			Sphere(size, SegmentCount(radius_segment), SegmentCount(height_segment))
		{
	

		}
	private:
		static int SegmentCount(float value)
		{
			if (!std::isfinite(value) || static_cast<double>(value) < std::numeric_limits<int>::min() ||
				static_cast<double>(value) > std::numeric_limits<int>::max()) {
				throw std::out_of_range("Sphere segment count is not representable as int");
			}
			// Sphere takes whole segments; retain the existing truncation toward zero.
			return static_cast<int>(value);
		}


	};

}
