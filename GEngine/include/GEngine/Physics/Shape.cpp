#include "gepch.h"
#include "Shape.h"
#include "Math/Math.h"

namespace GEngine
{
	using namespace Math;
	void PhysicalShape::HandleScaleChanged(const Vec3f& new_scale)
	{
		auto pts = m_MeshPoints;
		for (auto& pt : pts)
		{
			pt *= new_scale;
		}
		Build(pts);
	}

}