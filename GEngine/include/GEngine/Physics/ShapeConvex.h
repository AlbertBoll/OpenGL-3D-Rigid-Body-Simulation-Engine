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
		ShapeConvex() {
			m_CenterOfMass = Vec3f(0.0f);
			m_ShapeType = ShapeType::Convex;
		}
		explicit ShapeConvex(const std::vector<Vec3f>& pts): ShapeConvex() {
			Build(pts);
		}

		void Build(const std::vector<Vec3f>& pts);
		bool IsValid() const override { return m_IsValid; }
		Mat3 InertiaTensor() const override { return m_InertiaTensor; }
		Vec3f Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const override;
		
		Bounds GetBounds(const Vec3f& pos, const Quat& orient) const override;
		Bounds GetBounds() const override { return m_Bounds; }
		const std::vector<Vec3f>& GetPoints() const { return m_Points; }

		float FastestLinearSpeed(const Vec3f& angularVelocity, const Vec3f& dir) const override;
	
	private:
		std::vector<Vec3f> m_Points;
		Bounds m_Bounds;
		Mat3 m_InertiaTensor{ 0.0f };
		bool m_IsValid = false;
	};

}
