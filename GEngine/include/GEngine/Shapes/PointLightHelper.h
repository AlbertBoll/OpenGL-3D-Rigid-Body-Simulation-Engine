#pragma once

#include <Math/Math.h>
#include"Shapes/Sphere.h"

namespace GEngine::Shape
{

	class PointLightHelper : public Sphere
	{
	public:
		PointLightHelper(float size = 0.5f, float radius_segment = 32.f, float height_segment = 32.f) : Sphere(size, radius_segment, height_segment)
		{
	

		}


	};

}
