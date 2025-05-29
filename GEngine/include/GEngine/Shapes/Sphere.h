#pragma once
#include "Ellipsoid.h"


namespace GEngine::Shape
{
	class Sphere : public Ellipsoid
	{
	public:
		Sphere(float radius = 1.f, int u_resolution = 64, int v_resolution = 64) :
			Ellipsoid(2 * radius, 2 * radius, 2 * radius, u_resolution, v_resolution) {}
	};
}
