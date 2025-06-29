#pragma once

#include <Math/Math.h>
#include"Geometry/Geometry.h"

namespace GEngine::Shape
{
	class AABBBoundingBox : public Geometry
	{
	public:
		AABBBoundingBox() : Geometry()
		{
			const std::vector<Vec3f> positionData{};
			AddAttributes(positionData);
			UnBindVAO();
		}
	};


}
