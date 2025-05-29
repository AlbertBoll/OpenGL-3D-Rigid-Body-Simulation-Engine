#pragma once
#include <Math/Math.h>
#include"Geometry/Geometry.h"
#include <algorithm>

namespace GEngine::Shape
{

	class GridHelper: public Geometry
	{
	public:
		GridHelper(float size = 50.f, int divisions = 50,
			const Vec3f& grid_color = { .2f, .2f, .2f },
			const Vec3f& center_color_x = { .0f, 0.0f, 0.4f },
			const Vec3f& center_color_z = { 0.4f, .0f, 0.0f }) : Geometry()
		{
			std::vector<Vec3f> positionData;

			std::vector<Vec3f> colorData;
			std::vector<Vec2f> uvData;
			std::vector<Vec3f> vertexNormalData;
			//std::vector<Vec3f> faceNormalData;

			positionData.reserve(4 * (divisions + 1));

			colorData.reserve(4 * (divisions + 1));

			float delta_size = size / static_cast<float>(divisions);

			std::vector<float> range_values(divisions + 1, 0);
			std::generate(range_values.begin(), range_values.end(), [n = 0, &size, &delta_size]() mutable {return -size / 2.0f + n++ * delta_size; });

			for (auto& x : range_values)
			{
				positionData.emplace_back(x, -size / 2.0f, 0);
				positionData.emplace_back(x, size / 2.0f, 0);

				if (x == 0.f)
				{
					colorData.emplace_back(center_color_x);
					colorData.emplace_back(center_color_x);
				}

				else
				{

					colorData.emplace_back(grid_color);
					colorData.emplace_back(grid_color);
				}
			}

			for (auto& y : range_values)
			{
				positionData.emplace_back(-size / 2.0f, y, 0);
				positionData.emplace_back(size / 2.0f, y, 0);

				if (y == 0.f)
				{
					colorData.emplace_back(center_color_z);
					colorData.emplace_back(center_color_z);
				}

				else
				{

					colorData.emplace_back(grid_color);
					colorData.emplace_back(grid_color);
				}
			}

			AddAttributes(positionData, colorData, uvData, vertexNormalData);
			UnBindVAO();
		}
	};
	
}