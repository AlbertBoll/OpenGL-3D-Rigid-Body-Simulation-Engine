#pragma once
#include "Geometry/Geometry.h"

namespace GEngine::Shape
{
	/*inline bool are_same_point(const Vec3f& a, const Vec3f& b) {
		double epsilon = 1e-3;
		return std::fabs(a.x - b.x) < epsilon && std::fabs(a.y - b.y) < epsilon && std::fabs(a.z - b.z);
	}*/

	class Icosahedron : public Geometry
	{
	public:
		Icosahedron(float radius = 1.0f) : Geometry()
		{
			std::vector<Vec3f>positionData;
			std::vector<Vec3f>colorData;
			std::vector<Vec2f>uvData;
			std::vector<Vec3f>vertexNormalData;
			//std::vector<Vec3f>faceNormalData;

			const float t = (1.f + glm::sqrt(5.0f)) / 2.0f;

			std::vector<Vec3f> v
			{
				{-1,  t,  0}, {1, t,  0}, {-1, -t,  0}, {1, -t, 0}, { 0, -1,  t}, { 0, 1, t},
				{ 0, -1, -t}, {0, 1, -t}, { t,  0, -1}, {t,  0, 1}, {-t,  0, -1}, {-t, 0, 1}
			};

			const std::vector<Vec3f> triangleData
			{
				v[0],v[11],v[5], v[0],v[5],v[1],  v[0],v[1],v[7],   v[0],v[7],v[10],  v[0],v[10],v[11],
				v[1],v[5],v[9],  v[5],v[11],v[4], v[11],v[10],v[2], v[10],v[7],v[6],  v[7],v[1],v[8],
				v[3],v[9],v[4],  v[3],v[4],v[2],  v[3],v[2],v[6],   v[3],v[6],v[8],   v[3],v[8],v[9],
				v[4],v[9],v[5],  v[2],v[4],v[11], v[6],v[2],v[10],  v[8],v[6],v[7],   v[9],v[8],v[1]
			};

			for (auto& w : triangleData)
			{
				auto normal = glm::normalize(w);
				float x = normal.x;
				float y = normal.y;
				float z = normal.z;
				float u = atan2(x, z) / TwoPi + 0.5f;
				float v = y * 0.5f + 0.5f;
				auto position = radius * normal;
				positionData.emplace_back(position);
				uvData.emplace_back(u, v);
				vertexNormalData.emplace_back(normal);

			}

			m_UniquePoints = positionData;
			std::sort(m_UniquePoints.begin(), m_UniquePoints.end(), [](const Vec3f& a, const Vec3f& b) {
				if (a.x != b.x)
					return a.x < b.x;
				else if (a.y != b.y)
					return a.y < b.y;
				else
					return a.z < b.z;
				});

			auto last = std::unique(m_UniquePoints.begin(), m_UniquePoints.end(), are_same_point);
			m_UniquePoints.erase(last, m_UniquePoints.end());
			AddAttributes(positionData, colorData, uvData, vertexNormalData);
			UnBindVAO();

		}

	};



}
