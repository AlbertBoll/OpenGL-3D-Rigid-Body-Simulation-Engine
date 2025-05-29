#pragma once
#include "Geometry/Geometry.h"

namespace GEngine
{
	struct SpriteParam
	{
		float PosXBegin = 0.f;
		float PosXEnd = 1.f;
		float PosYBegin = 0.f;
		float PosYEnd = 1.f;
		float texXBegin = 0.f;
		float texXEnd = 1.0f;
		float texYBegin = 0.f;
		float texYEnd = 1.0f;
	};

	class SpriteGeometry : public Geometry
	{
	public:
		SpriteGeometry(const SpriteParam& param = SpriteParam{}) : Geometry()
		{
			std::vector<Vec4f> vertices(6);
			vertices[0] = { param.PosXBegin, param.PosYEnd,   param.texXBegin, param.texYEnd };
			vertices[1] = { param.PosXEnd,   param.PosYBegin, param.texXEnd,   param.texYBegin };
			vertices[2] = { param.PosXBegin, param.PosYBegin, param.texXBegin, param.texYBegin };
			vertices[3] = { param.PosXBegin, param.PosYEnd,   param.texXBegin, param.texYEnd };
			vertices[4] = { param.PosXEnd,   param.PosYEnd,   param.texXEnd,   param.texYEnd };
			vertices[5] = { param.PosXEnd,   param.PosYBegin, param.texXEnd,   param.texYBegin };

			AddAttributes(vertices);

		}
	};
}
