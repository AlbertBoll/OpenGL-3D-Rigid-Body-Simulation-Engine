#pragma once

#include"Geometry/Geometry.h"
#include <Math/Math.h>
#include <vector>

namespace GEngine::Shape
{
	//using namespace GEngine::Math;

	struct BoxParams
	{
		float width = 1.0f;
		float height = 1.0f;
		float depth = 1.0f;
	};

	class Box: public Geometry
	{
	public:
		Box(float width = 1.0f, float height = 1.0f, float depth = 1.0f): Geometry()
		{
			//position
			Vec3f p0 = { -width / 2.f, -height / 2.f, -depth / 2.f };
			Vec3f p1 = { width / 2.f, -height / 2.f, -depth / 2.f };
			Vec3f p2 = { -width / 2.f,  height / 2.f, -depth / 2.f };
			Vec3f p3 = { width / 2.f,  height / 2.f, -depth / 2.f };
			Vec3f p4 = { -width / 2.f, -height / 2.f,  depth / 2.f };
			Vec3f p5 = { width / 2.f, -height / 2.f,  depth / 2.f };
			Vec3f p6 = { -width / 2.f,  height / 2.f,  depth / 2.f };
			Vec3f p7 = { width / 2.f,  height / 2.f,  depth / 2.f };

			//Color 
			Vec3f c1 = { 1.0f, 0.0f, 0.0f };
			Vec3f c2 = { 0.5f, 0.0f, 0.0f };
			Vec3f c3 = { 1.0f, 1.0f, 1.0f };
			Vec3f c4 = { 0.0f, 0.5f, 0.0f };
			Vec3f c5 = { 0.0f, 0.0f, 1.0f };
			Vec3f c6 = { 0.0f, 0.0f, 0.5f };
			/*Vec3f c1 = { .5f, .5f, .5f };
			Vec3f c2 = { .5f, .5f, .5f };
			Vec3f c3 = { .5f, .5f, .5f };
			Vec3f c4 = { .5f, .5f, .5f };
			Vec3f c5 = { .5f, .5f, .5f };
			Vec3f c6 = { .5f, .5f, .5f }; */

			//texture coords
			Vec2f t0 = { 0.f, 0.f };
			Vec2f t1 = { 1.f, 0.f };
			Vec2f t2 = { 0.f, 1.f };
			Vec2f t3 = { 1.f, 1.f };

			//Normal
			Vec3f nx = { 1.0f, 0.0f, 0.0f };
			Vec3f nx_ = { -1.f, 0.0f, 0.0f };
			Vec3f ny = { 0.0f, 1.0f, 0.0f };
			Vec3f ny_ = { 0.0f, -1.f, 0.0f };
			Vec3f nz = { 0.0f, 0.0f, 1.0f };
			Vec3f nz_ = { 0.0f, 0.0f, -1.f };

			std::vector<Vec3f> positionData = { p5, p1, p3, p5, p3, p7, p0, p4, p6, p0, p6, p2,
											   p6, p7, p3, p6, p3, p2, p0, p1, p5, p0, p5, p4,
											   p4, p5, p7, p4, p7, p6, p1, p0, p2, p1, p2, p3 };

			std::vector<Vec3f> colorData
				= { c1, c1, c1 ,c1, c1, c1,
					c2, c2, c2 ,c2, c2, c2,
					c3, c3, c3 ,c3, c3, c3,
					c4, c4, c4 ,c4, c4, c4,
					c5, c5, c5 ,c5, c5, c5,
					c6, c6, c6 ,c6, c6, c6 };


			std::vector<Vec2f> uvData = { t0, t1, t3, t0, t3, t2 ,
										  t0, t1, t3, t0, t3, t2 ,
										  t0, t1, t3, t0, t3, t2 ,
										  t0, t1, t3, t0, t3, t2 ,
										  t0, t1, t3, t0, t3, t2 ,
										  t0, t1, t3, t0, t3, t2 };
			
			
			std::vector<Vec3f> normalDataX(6, nx);
			std::vector<Vec3f> normalDataX_(6, nx_);
			std::vector<Vec3f> normalDataY(6, ny);
			std::vector<Vec3f> normalDataY_(6, ny_);
			std::vector<Vec3f> normalDataZ(6, nz);
			std::vector<Vec3f> normalDataZ_(6, nz_);

			std::vector<Vec3f> normalData;
			normalData.reserve(36);
			//move data
			normalData.insert(
				normalData.end(),
				std::make_move_iterator(normalDataX.begin()),
				std::make_move_iterator(normalDataX.end())
			);

			normalData.insert(
				normalData.end(),
				std::make_move_iterator(normalDataX_.begin()),
				std::make_move_iterator(normalDataX_.end())
			);

			normalData.insert(
				normalData.end(),
				std::make_move_iterator(normalDataY.begin()),
				std::make_move_iterator(normalDataY.end())
			);

			normalData.insert(
				normalData.end(),
				std::make_move_iterator(normalDataY_.begin()),
				std::make_move_iterator(normalDataY_.end())
			);

			normalData.insert(
				normalData.end(),
				std::make_move_iterator(normalDataZ.begin()),
				std::make_move_iterator(normalDataZ.end())
			);

			normalData.insert(
				normalData.end(),
				std::make_move_iterator(normalDataZ_.begin()),
				std::make_move_iterator(normalDataZ_.end())
			);

			AddAttributes(positionData, colorData, uvData, normalData);
			UnBindVAO();
			//BuildBounds(positionData);
		}


	};


	class SkyBox : public Geometry
	{
	public:
		SkyBox(float width = 2000.0f, float height = 2000.0f, float depth = 2000.0f) : Geometry()
		{
			//position
			Vec3f p0 = { -width / 2.f, -height / 2.f, -depth / 2.f };
			Vec3f p1 = { width / 2.f, -height / 2.f, -depth / 2.f };
			Vec3f p2 = { -width / 2.f,  height / 2.f, -depth / 2.f };
			Vec3f p3 = { width / 2.f,  height / 2.f, -depth / 2.f };
			Vec3f p4 = { -width / 2.f, -height / 2.f,  depth / 2.f };
			Vec3f p5 = { width / 2.f, -height / 2.f,  depth / 2.f };
			Vec3f p6 = { -width / 2.f,  height / 2.f,  depth / 2.f };
			Vec3f p7 = { width / 2.f,  height / 2.f,  depth / 2.f };

			std::vector<Vec3f> positionData = { p5, p1, p3, p5, p3, p7, p0, p4, p6, p0, p6, p2,
											   p6, p7, p3, p6, p3, p2, p0, p1, p5, p0, p5, p4,
											   p4, p5, p7, p4, p7, p6, p1, p0, p2, p1, p2, p3 };

			AddAttributes(positionData);
			UnBindVAO();
		}

	};
	

}