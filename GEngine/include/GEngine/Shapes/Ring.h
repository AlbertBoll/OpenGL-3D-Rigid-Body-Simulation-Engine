#pragma once
#include"Geometry/Geometry.h"

namespace GEngine::Shape
{
	class Ring : public Geometry
	{
	public:
		Ring(float inner_radius = 0.25f, float outer_radius = 1.f, int segments = 32) : Geometry()
		{
			std::vector<Vec3f> positionData;
			std::vector<Vec3f> colorData;
			std::vector<Vec2f> uvData;
			std::vector<Vec3f> vertexNormalData;
			//std::vector<Vec3f> faceNormalData;

			float angle = TwoPi / segments;
			Vec3f normal = { 0.f, 0.f, 1.f };

			Vec3f posA;
			Vec3f posB;
			Vec3f posC;
			Vec3f posD;

			Vec2f uvA;
			Vec2f uvB;
			Vec2f uvC;
			Vec2f uvD;


			for (int i = 0; i < segments; i++)
			{
				posA = { inner_radius * glm::cos(i * angle), inner_radius * glm::sin(i * angle), 0.f };
				posB = { outer_radius * glm::cos(i * angle), outer_radius * glm::sin(i * angle), 0.f };
				posC = { outer_radius * glm::cos((i + 1) * angle), outer_radius * glm::sin((i + 1) * angle), 0.f };
				posD = { inner_radius * glm::cos((i + 1) * angle), inner_radius * glm::sin((i + 1) * angle), 0.f };

				uvA = { inner_radius * glm::cos(i * angle) * 0.5 + 0.5, inner_radius * glm::sin(i * angle) * 0.5 + 0.5 };
				uvB = { outer_radius * glm::cos(i * angle) * 0.5 + 0.5, outer_radius * glm::sin(i * angle) * 0.5 + 0.5 };
				uvC = { outer_radius * glm::cos((i + 1) * angle) * 0.5 + 0.5, outer_radius * glm::sin((i + 1) * angle) * 0.5 + 0.5 };
				uvD = { inner_radius * glm::cos((i + 1) * angle) * 0.5 + 0.5, inner_radius * glm::sin((i + 1) * angle) * 0.5 + 0.5 };

				positionData.insert(positionData.end(), { posA, posB, posC, posA, posC, posD });
				uvData.insert(uvData.end(), { uvA, uvB, uvC, uvA, uvC, uvD });
				vertexNormalData.insert(vertexNormalData.end(), { normal, normal, normal, normal, normal, normal });
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
