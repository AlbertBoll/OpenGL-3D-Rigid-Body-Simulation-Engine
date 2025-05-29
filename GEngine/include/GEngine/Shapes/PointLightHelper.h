#pragma once

#include <Math/Math.h>
#include"Shapes/Sphere.h"

namespace GEngine::Shape
{

	class PointLightHelper : public Sphere
	{
	public:
		PointLightHelper(float size = 0.1f, float radius_segment = 4.f, float height_segment = 2.f) : Sphere(size, radius_segment, height_segment)
		{
	

		}


	};

}
