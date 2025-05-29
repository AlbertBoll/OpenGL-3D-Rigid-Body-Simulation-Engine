#pragma once
#include"ParametricSurface.h"

namespace GEngine::Shape
{
	class Ellipsoid : public ParametricSurface
	{

	public:
		Ellipsoid(float width = 1.0f, float height = 1.0f, float depth = 1.0f, int u_resolution = 32, int v_resolution = 32)
			: ParametricSurface(0, TwoPi, u_resolution, -PiOver2-0.1f, PiOver2+0.1f, v_resolution,
				[&](float phi, float theta) {return Vec3f{ width / 2 * glm::sin(phi) * glm::cos(theta),  height / 2 * glm::sin(theta), depth / 2 * glm::cos(phi) * glm::cos(theta) }; }) {}

	};

}
