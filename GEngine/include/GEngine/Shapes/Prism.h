#pragma once
#include"CylindricalShape.h"

namespace GEngine::Shape
{
	class Prism : public CylindricalShape
	{
	public:
		Prism(float radius = 1.f, float height = 1.f, int sides = 3, int heightSegments = 4, bool bClosed = true) :
			CylindricalShape(0, radius, height, sides, heightSegments, bClosed, bClosed)
		{
			m_UniquePoints = GetPoints();
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
		}
	};
}
