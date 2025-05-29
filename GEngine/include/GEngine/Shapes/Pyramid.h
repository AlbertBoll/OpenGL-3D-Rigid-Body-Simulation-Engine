#pragma once
#include "CylindricalShape.h"

namespace GEngine::Shape
{
	class Pyramid : public CylindricalShape
	{
	public:
		//Pyramid(float side = 1.f) : Geometry()
		//{
		//	float s = 1.0f / sqrt(3.0f);
		//	//position
		//	Vec3f p0 = { s, s, s };
		//	Vec3f p1 = { -s, -s, s };
		//	Vec3f p2 = { -s, s, -s };
		//	Vec3f p3 = { s, -s, -s };
		//	
		//	std::vector<Vec3f> positionData = { p0, p1, p2, p3};

		//	std::vector<Vec3f> colorData(12, { 1.f, 1.f, 1.f });

		//	Vec2f uv0 = { 1.f, 1.f };
		//	Vec2f uv1 = { 0.f, 1.f };
		//	Vec2f uv2 = { 0.f, 0.f };
		//	Vec2f uv3 = { 1.f, 0.f };
		//	
		//	std::vector<Vec2f> uvData = { uv0, uv1, uv2, uv3};

		//	Vec3f n0 = { 0.577f, 0.577f, 0.577f };
		//	Vec3f n1 = { -0.577f,-0.577f, 0.577f };
		//	Vec3f n2 = { -0.577f, 0.577f,-0.577f };
		//	Vec3f n3 = { 0.577f,-0.577f,-0.577f };
		//	
		//	std::vector<Vec3f> vertexNormalData = { n0, n1, n2, n3};

		//	m_UniquePoints = positionData;

		//	AddAttributes(positionData, colorData, uvData, vertexNormalData);

		//	std::vector<unsigned int> indices{ 0, 1, 2,   // Face 1
		//									   0, 3, 1,   // Face 2
		//									   0, 2, 3,   // Face 3
		//									   1, 3, 2 }; // Face 4};
		//	AddIndices(indices);

		//	UnBindVAO();

		//	

		//}
		//Pyramid(float side = 1.f): Geometry()
		//{

		//	////position
		//	//Vec3f p0 = { -side / 2.f, -side / 2.f, -side / 2.f };
		//	//Vec3f p1 = { side / 2.f, -side / 2.f, -side / 2.f };
		//	//Vec3f p2 = { 0.f,  side / 2.f, 0.f };
		//	//Vec3f p3 = { -side / 2.f,  -side / 2.f, side / 2.f };
		//	//Vec3f p4 = { side / 2.f, -side / 2.f,  side / 2.f };
		//	//Vec3f p5 = { 0.f, side / 2.f,  0.f };
		//	//Vec3f p6 = { -side / 2.f,  -side / 2.f,  -side / 2.f };
		//	//Vec3f p7 = { -side / 2.f,  -side / 2.f,  side / 2.f };
		//	//Vec3f p8 = { 0.f, side / 2.f,  0.f };
		//	//Vec3f p9 = { side / 2.f, -side / 2.f,  -side / 2.f };
		//	//Vec3f p10 = { side / 2.f, -side / 2.f,  side / 2.f };
		//	//Vec3f p11 = { 0.f, side / 2.f,  0.f };
		//	//Vec3f p12 = { side / 2.f,  -side / 2.f,  -side / 2.f };
		//	//Vec3f p13 = { side / 2.f,  -side / 2.f,  side / 2.f };
		//	//Vec3f p14 = { 0.f, side / 2.f,  0.f };
		//	//Vec3f p15 = { -side / 2.f,  -side / 2.f,  side / 2.f };
		//	//Vec3f p16 = { -side / 2.f,  -side / 2.f,  -side / 2.f };
		//	//Vec3f p17 = { 0.f, side / 2.f,  0.f };

		//	//std::vector<Vec3f> positionData = { p0, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11,
		//	//								   p12, p13, p14, p15, p16, p17};

		//	//std::vector<Vec3f> colorData(18, { 1.f, 1.f, 1.f });
		//	//
		//	//Vec2f uv0 = { 0.f, 0.f };
		//	//Vec2f uv1 = { 1.f, 0.f };
		//	//Vec2f uv2 = { 0.5f, 1.f };
		//	//Vec2f uv3 = { 0.f, 0.f };
		//	//Vec2f uv4 = { 1.f, 0.f };
		//	//Vec2f uv5 = { 0.5f, 1.f };
		//	//Vec2f uv6 = { 0.f, 1.f };
		//	//Vec2f uv7 = { 0.f, 0.f };
		//	//Vec2f uv8 = { 0.5f, 1.f };
		//	//Vec2f uv9 = { 0.f, 1.f };
		//	//Vec2f uv10 = { 0.f, 0.f };
		//	//Vec2f uv11 = { 0.5f, 1.f };
		//	//Vec2f uv12 = { 1.f, 1.f };
		//	//Vec2f uv13 = { 1.f, 0.f };
		//	//Vec2f uv14 = { 0.5f, 1.f };
		//	//Vec2f uv15 = { 0.f, 0.f };
		//	//Vec2f uv16 = { 0.f, 1.f };
		//	//Vec2f uv17 = { 0.5f, 1.f };

		//	//std::vector<Vec2f> uvData = {uv0, uv1, uv2, uv3, uv4, uv5, uv6, uv7, uv8, uv9,
		//	//uv10, uv11, uv12, uv13, uv14, uv15, uv16, uv17};

		//	//Vec3f n1 = { 0.f, 0.f, -1.f };
		//	//Vec3f n2 = { 0.f, 0.f, -1.f };
		//	//Vec3f n3 = { 0.f, 0.f, -1.f };
		//	//Vec3f n4 = { 0.f, 0.f, 1.f };
		//	//Vec3f n5 = { 0.f, 0.f, 1.f };
		//	//Vec3f n6 = { 0.f, 0.f, 1.f };
		//	//Vec3f n7 = { -1.f, 0.f, 0.f };
		//	//Vec3f n8 = { -1.f, 0.f, 0.f };
		//	//Vec3f n9 = { -1.f, 0.f, 0.f };
		//	//Vec3f n10 = { 1.f, 0.f, 0.f };
		//	//Vec3f n11 = { 1.f, 0.f, 0.f };
		//	//Vec3f n12 = { 1.f, 0.f, 0.f };
		//	//Vec3f n13 = { 0.f, -1.f, 0.f };
		//	//Vec3f n14 = { 0.f, -1.f, 0.f };
		//	//Vec3f n15 = { 0.f, -1.f, 0.f };
		//	//Vec3f n16 = { 0.f, 1.f, 0.f };
		//	//Vec3f n17 = { 0.f, 1.f, 0.f };
		//	//Vec3f n18 = { 0.f, 1.f, 0.f };
		//	//std::vector<Vec3f> normalData = { n1, n2, n3, n4, n5, n6, n7, n8, n9, n10,
		//	//	n11, n12, n13, n14, n15, n16, n17, n18 };

		//	m_UniquePoints = positionData;
		//	std::sort(m_UniquePoints.begin(), m_UniquePoints.end(), [](const Vec3f& a, const Vec3f& b) {
		//		if (a.x != b.x)
		//			return a.x < b.x;
		//		else if (a.y != b.y)
		//			return a.y < b.y;
		//		else
		//			return a.z < b.z;
		//		});

		//	auto last = std::unique(m_UniquePoints.begin(), m_UniquePoints.end(), are_same_point);
		//	m_UniquePoints.erase(last, m_UniquePoints.end());

		//	AddAttributes(positionData, colorData, uvData, normalData);
		//	UnBindVAO();

		//}
		Pyramid(float radius = 1.f, float height = 1.f, int sides = 4, int heightSegments = 8, bool bClosed = true)
			: CylindricalShape(0, radius, height, sides, heightSegments, false, bClosed){}
	};
}
