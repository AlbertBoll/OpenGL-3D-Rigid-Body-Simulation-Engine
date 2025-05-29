#pragma once
#include "Shape.h"

namespace GEngine
{

	class ShapeBox : public PhysicalShape
	{
	public:
		ShapeBox() = default;
		ShapeBox(const std::vector<Vec3f>& pts): PhysicalShape(pts) {
			Build(pts);
			m_ShapeType = ShapeType::Box;
		}
		void Build(const std::vector<Vec3f>& pts);

		Vec3f Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const override;
		//void HandleScaleChanged(const Vec3f& new_scale) override;
		Mat3 InertiaTensor() const override;

		Bounds GetBounds(const Vec3f& pos, const Quat& orient) const override;
		Bounds GetBounds() const override { return m_bounds; }

		float FastestLinearSpeed(const Vec3f& angularVelocity, const Vec3f& dir) const override;

		std::vector<Vec3f> m_points;
		Bounds m_bounds;
	};

}

