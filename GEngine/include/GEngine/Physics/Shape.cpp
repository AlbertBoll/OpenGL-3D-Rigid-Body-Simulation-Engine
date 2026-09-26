#include "gepch.h"
#include "Shape.h"
#include "Math/Math.h"

namespace GEngine
{
	using namespace Math;
	void PhysicalShape::HandleScaleChanged(const Vec3f& new_scale)
	{
		if (!Math::IsFinite(new_scale)) {
			return;
		}
		auto pts = m_MeshPoints;
		for (auto& pt : pts)
		{
			pt *= new_scale;
			if (!Math::IsFinite(pt)) {
				return;
			}
		}
		// Build installs its input as the source for explicit geometry replacement.
		// A scale rebuild retains the previous unscaled source on every exit.
		std::vector<Vec3f> basePoints;
		basePoints.swap(m_MeshPoints);
		struct RestoreSource
		{
			std::vector<Vec3f>& current;
			std::vector<Vec3f>& original;
			~RestoreSource() { current.swap(original); }
		} restore{m_MeshPoints, basePoints};
		Build(pts);
	}

}