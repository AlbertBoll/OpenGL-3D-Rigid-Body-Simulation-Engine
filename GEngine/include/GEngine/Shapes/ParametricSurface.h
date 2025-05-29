#pragma once
#include "Geometry/Geometry.h"
#include <functional>
#include <vector>

namespace GEngine::Shape
{
	
	using namespace Math;
	class ParametricSurface : public Geometry
	{
		//typedef Vec3f(*SurfaceFunc)(float u, float v);
		typedef std::function<Vec3f(float u, float v)> SurfaceFunc;

	public:
		ParametricSurface(float uStart, float uEnd, int uResolution, float vStart, float vEnd, int vResolution, const SurfaceFunc& surfaceFunc) : Geometry()
		{
			float deltaU = (uEnd - uStart) / uResolution;
			float deltaV = (vEnd - vStart) / vResolution;

			std::vector<std::vector<Vec3f>> positions;
			std::vector<std::vector<Vec2f>> uvs;
			std::vector<std::vector<Vec3f>> vertexNormals;

			std::vector<Vec3f> normalArray;
			float uTemp{};
			float vTemp{};

			float u_Temp{};
			float v_Temp{};

			std::vector<Vec3f> vArray;
			std::vector<Vec2f> uvArray;



			float delta = 0.0001f;
			//std::vector<Vec3f> vArray;

			Vec3f p0;
			Vec3f p1;
			Vec3f p2;
			Vec3f normalVec;

			for (int uIndex = 0; uIndex <= uResolution; uIndex++)
			{
				vArray.reserve(vResolution + 1);
				uvArray.reserve(vResolution + 1);
				for (int vIndex = 0; vIndex <= vResolution; vIndex++)
				{
					uTemp = uStart + uIndex * deltaU;
					vTemp = vStart + vIndex * deltaV;

					u_Temp = static_cast<float>(uIndex) / uResolution;
					v_Temp = static_cast<float>(vIndex) / vResolution;

					p0 = surfaceFunc(uTemp, vTemp);
					p1 = surfaceFunc(uTemp + delta, vTemp);
					p2 = surfaceFunc(uTemp, vTemp + delta);
					normalVec = GetNormal(p0, p1, p2);

					vArray.push_back(surfaceFunc(uTemp, vTemp));
					uvArray.emplace_back(u_Temp, v_Temp);
					normalArray.emplace_back(normalVec);
				}

				positions.push_back(vArray);
				uvs.push_back(uvArray);
				vertexNormals.push_back(normalArray);
				vArray.clear();
				uvArray.clear();
				normalArray.clear();
			}

			std::vector<Vec3f> positionData;
			std::vector<Vec3f> colorData;
			std::vector<Vec2f> uvData;
			std::vector<Vec3f> vertexNormalData;
			std::vector<Vec3f> faceNormalData;


			Vec3f C1{ 1.0f, 0.0f, 0.0f };
			Vec3f C2{ 0.0f, 1.0f, 0.0f };
			Vec3f C3{ 0.0f, 0.0f, 1.0f };
			Vec3f C4{ 0.0f, 1.0f, 1.0f };
			Vec3f C5{ 1.0f, 0.0f, 1.0f };
			Vec3f C6{ 1.0f, 1.0f, 0.0f };

			int x{ 0 };
			//int y{0};
			Vec3f first_point{};
			Vec3f second_point{};
			Vec3f third_point{};
			Vec3f fourth_point{};

			Vec2f first_point_{};
			Vec2f second_point_{};
			Vec2f third_point_{};
			Vec2f fourth_point_{};

			Vec3f nA{};
			Vec3f nB{};
			Vec3f nC{};
			Vec3f nD{};

			Vec3f fn0{};
			Vec3f fn1{};

			for (; x < uResolution; x++)
			{
				int y{ 0 };

				for (; y < vResolution; y++)
				{
					first_point = positions[x][y];
					second_point = positions[x + 1][y];
					third_point = positions[x + 1][y + 1];
					fourth_point = positions[x][y + 1];

					first_point_ = uvs[x][y];
					second_point_ = uvs[x + 1][y];
					third_point_ = uvs[x + 1][y + 1];
					fourth_point_ = uvs[x][y + 1];

					nA = vertexNormals[x][y];
					nB = vertexNormals[x + 1][y];
					nC = vertexNormals[x + 1][y + 1];
					nD = vertexNormals[x][y + 1];

					positionData.insert(positionData.end(),
						{ first_point, second_point, third_point, first_point, third_point, fourth_point });
					colorData.insert(colorData.end(), { C1, C2, C3, C4, C5, C6 });

					uvData.insert(uvData.end(), { first_point_, second_point_, third_point_, first_point_, third_point_, fourth_point_ });

					vertexNormalData.insert(vertexNormalData.end(), { nA, nB, nC, nA, nC, nD });

					fn0 = GetNormal(nA, nB, nC);
					fn1 = GetNormal(nA, nC, nD);

					faceNormalData.insert(faceNormalData.end(), { fn0, fn0, fn0, fn1, fn1, fn1 });
				}
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
			//AddAttributes(positionData, colorData, uvData, faceNormalData);
			//UnBindVAO();
		}

		[[nodiscard]] glm::vec3 GetNormal(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2) const
		{
			const auto v1 = p1 - p0;
			const auto v2 = p2 - p0;
			const auto normal = glm::cross(v1, v2);
			return glm::normalize(normal);

		}

		

	};

}
