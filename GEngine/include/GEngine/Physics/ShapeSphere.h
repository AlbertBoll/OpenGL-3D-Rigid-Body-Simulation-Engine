#pragma once
#include "Shape.h"

namespace GEngine
{
	class ShapeSphere : public PhysicalShape
	{
	public:
		ShapeSphere() = default;
		ShapeSphere(float radius);
		// Inherited via Shape
		Mat3 InertiaTensor() const override;

		Bounds GetBounds(const Vec3f& pos, const Quat& orient) const override;
		void HandleScaleChanged(const Vec3f& new_scale) override { m_Radius *= new_scale.x; }
		Bounds GetBounds() const override;

		Vec3f Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const override;

		float m_Radius = 1.0f;
	};

}