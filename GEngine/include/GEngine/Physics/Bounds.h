#pragma once
#include <glm/ext/vector_float3.hpp>
#include<vector>
#include <Math/Math.h>



namespace GEngine
{
	using namespace Math;

	class Bounds {
	public:
		Bounds() { Clear(); }
		Bounds(const Bounds& rhs) : mins(rhs.mins), maxs(rhs.maxs) {}
		const Bounds& operator = (const Bounds& rhs);
		~Bounds() {}

		void Clear() { mins = Vec3f(1e6); maxs = Vec3f(-1e6); }
		bool DoesIntersect(const Bounds& rhs) const;
		void Expand(const Vec3f* pts, const int num);
		void Expand(const std::vector<Vec3f>& pts);
		void Expand(const Vec3f& rhs);
		void Expand(const Bounds& rhs);

		float WidthX() const { return maxs.x - mins.x; }
		float WidthY() const { return maxs.y - mins.y; }
		float WidthZ() const { return maxs.z - mins.z; }

	public:
		Vec3f mins;
		Vec3f maxs;
	};

}