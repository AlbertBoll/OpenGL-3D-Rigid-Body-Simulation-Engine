#pragma once
#include <Math/Math.h>
#include"Geometry/Geometry.h"

namespace GEngine::Shape
{

	class AxisHelper : public Geometry
	{
	public:
		AxisHelper(float axis_length = 1.0f) : Geometry()
		{
			const std::vector<Vec3f> positionData = { {0.0f, 0.0f, 0.0f}, {axis_length, 0.0f, 0.0f},
												  {0.0f, 0.0f, 0.0f}, {0.0f, axis_length, 0.0f},
												  {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, axis_length} };


			const std::vector<Vec3f> colorData = { {1.0f, 0.0f, 0.0f},  {1.0f, 0.0f, 0.0f},
												   {0.0f, 1.0f, 0.0f},  {0.0f, 1.0f, 0.0f},
												   {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, 1.0f} };

			AddAttributes(positionData, colorData);
			UnBindVAO();

		}


	};

}