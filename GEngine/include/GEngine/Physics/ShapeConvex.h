#pragma once
#include "Shape.h"

namespace GEngine
{

	struct tri_t {
		int a;
		int b;
		int c;
	};

	struct edge_t {
		int a;
		int b;

		bool operator == (const edge_t& rhs) const {
			return ((a == rhs.a && b == rhs.b) || (a == rhs.b && b == rhs.a));
		}
	};

	class ShapeConvex : public PhysicalShape
	{
	public:
		ShapeConvex() = default;
		ShapeConvex(const std::vector<Vec3f>& pts): PhysicalShape(pts) {
			Build(pts);
			m_ShapeType = ShapeType::Convex;
		}

		void Build(const std::vector<Vec3f>& pts);
		Mat3 InertiaTensor() const override { return m_InertiaTensor; }
		Vec3f Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const override;
		
		Bounds GetBounds(const Vec3f& pos, const Quat& orient) const override;
		Bounds GetBounds() const override { return m_Bounds; }

		float FastestLinearSpeed(const Vec3f& angularVelocity, const Vec3f& dir) const override;
	

		std::vector<Vec3f> m_Points;
		Bounds m_Bounds;
		Mat3 m_InertiaTensor;
	};

}