#pragma once
#include "Physics/ShapeConvex.h"

namespace GEngine::detail
{
	// Internal hull implementation shared by ShapeConvex and the Diamond mesh builder.
	// The definition lives only in ShapeConvex.cpp.
	bool BuildConvexHull(const std::vector<Math::Vec3f>& verts,
		std::vector<Math::Vec3f>& hullPts, std::vector<tri_t>& hullTris);
}
