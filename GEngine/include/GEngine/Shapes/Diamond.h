#pragma once
#include "Geometry/Geometry.h"
#include <Physics/ShapeConvex.h>
#include <Physics/ShapeConvex.cpp>

namespace GEngine::Shape
{
	class Diamond : public Geometry
	{
	public:
		Diamond() : Geometry()
		{
			auto pts = GenerateDiamond();
			auto convex = ShapeConvex(pts);

			std::vector<Vec3f> positionData;
			
			std::vector<Vec2f> uvData;
			std::vector<Vec3f> vertexNormalData;

			std::vector<unsigned int> indices;
			
			

			std::vector< Vec3f > hullPts;
			std::vector< tri_t > hullTris;

			BuildConvexHull(convex.m_Points, hullPts, hullTris);

			vertexNormalData.reserve(hullTris.size());
			positionData.resize(hullPts.size());
			uvData.reserve(hullPts.size());

			for (int i = 0; i < hullPts.size(); i++) 
			{
				Vec3f norm(0.0f);

				for (int t = 0; t < hullTris.size(); t++)
				{
					const tri_t& tri = hullTris[t];
					if (i != tri.a && i != tri.b && i != tri.c) {
						continue;
					}

					const Vec3f& a = hullPts[tri.a];
					const Vec3f& b = hullPts[tri.b];
					const Vec3f& c = hullPts[tri.c];

					Vec3f ab = b - a;
					Vec3f ac = c - a;
					norm += glm::cross(ab, ac);
				}

				norm = glm::normalize(norm);
				vertexNormalData.push_back(norm);
			}

			for (int i = 0; i < hullPts.size(); i++)
			{
				Vec3f normal = vertexNormalData[i];
				float x = normal.x;
				float y = normal.y;
				float z = normal.z;
				float u = atan2(x, z) / TwoPi + 0.5f;
				float v = y * 0.5f + 0.5f;
				uvData.emplace_back(u, v);
				positionData[i] = hullPts[i];
			}

			std::vector<Vec3f> colorData(hullPts.size(), {0, 0, 0});
			


			// Add the indices
			indices.reserve(hullTris.size() * 3);
			for (int i = 0; i < hullTris.size(); i++)
			{
				indices.push_back(hullTris[i].a);
				indices.push_back(hullTris[i].b);
				indices.push_back(hullTris[i].c);
			}

			AddAttributes(positionData, colorData, uvData, vertexNormalData);
			AddIndices(indices);
			m_UniquePoints = pts;
			UnBindVAO();
		}
	};
}