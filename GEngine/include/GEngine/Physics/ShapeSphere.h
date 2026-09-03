#pragma once
#include "Shape.h"

namespace GEngine
{
	class ShapeSphere : public PhysicalShape
	{
	public:
		ShapeSphere() = default;
		ShapeSphere(float radius);
		float GetRadius() const { return m_Radius; }
		void SetRadius(float radius)
		{
			m_Radius = radius;
			MarkGeometryChanged();
		}
		// Inherited via Shape
		Mat3 InertiaTensor() const override;

		Bounds GetBounds(const Vec3f& pos, const Quat& orient) const override;
		void HandleScaleChanged(const Vec3f& new_scale) override
		{
			SetRadius(m_Radius * new_scale.x);
		}
		
		Bounds GetBounds() const override;

		Vec3f Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const override;

	private:
		float m_Radius = 1.0f;
	};

}
