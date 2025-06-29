#pragma once
#include <Geometry/Geometry.h>
#include <Math/Math.h>

namespace GEngine
{
	class KDTreeVisualizer : public Geometry
	{
	public:
		KDTreeVisualizer() : Geometry()
		{
			const std::vector<Vec3f> positionData{};
			AddAttributes(positionData);
			UnBindVAO();
		}
	};
}
