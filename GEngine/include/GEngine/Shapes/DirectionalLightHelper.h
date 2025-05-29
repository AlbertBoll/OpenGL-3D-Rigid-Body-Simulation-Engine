#pragma once

#include"Shapes/GridHelper.h"


namespace GEngine::Shape
{

	class DirectionalLightHelper : public GridHelper
	{
	public:
		DirectionalLightHelper(const Vec3f& color, float size = 1.f, int divisions = 4, const Vec3f& center_color = { 1.0f, 1.0f, 1.0f }): GridHelper(size, divisions, color, center_color)
		{

		}

	};


}