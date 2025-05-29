#pragma once
#include"ParametricSurface.h"

namespace GEngine::Shape
{
	class Torus : public ParametricSurface
	{
	public:
		Torus(float centralRadius = 1.f, float tubeRadius = 0.4f, int tubularSegments = 16, int radialSegments = 16, float scale = 1.0f) :
			ParametricSurface(0, TwoPi, tubularSegments, 0, TwoPi, radialSegments,
				[&](float u, float v)
				{
					return Vec3f((centralRadius + tubeRadius * glm::cos(v)) * glm::cos(u) * scale,
						(centralRadius + tubeRadius * glm::cos(v)) * glm::sin(u) * scale,
						tubeRadius * glm::sin(v) * scale);
				}){}
	};
}
