#pragma once
#include"Geometry/Geometry.h"

namespace GEngine::Geometry
{
	class SpriteQuad: public Geometry
	{
	public:
		SpriteQuad(): Geometry()
		{
			Vec2f p0{ -0.5f, -0.5f };
			Vec2f p1{ 0.5f, -0.5f };
			Vec2f p2{ -0.5f,  0.5f };
			Vec2f p3{ 0.5f,  0.5f };


			std::vector<Vec2f> positionData{ p0, p1, p3, p0, p3, p2 };
	

			AddAttributes(positionData);
			UnBindVAO();
		}
	};
}
