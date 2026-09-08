#pragma once
#include "Shape.h"

namespace GEngine
{
	class ShapeSphere : public PhysicalShape
	{
	public:
		ShapeSphere() : ShapeSphere(1.0f) {}
		ShapeSphere(float radius);
		float GetRadius() const { return m_Radius; }
		// A changed radius installs new unscaled geometry; rejected/unchanged values are no-ops.
		void SetRadius(float radius);
		bool IsValid() const override;
		// Inherited via Shape
		Mat3 InertiaTensor() const override;

		Bounds GetBounds(const Vec3f& pos, const Quat& orient) const override;
		// Absolute scale of the base radius. Retains the existing X-axis sphere policy.
		void HandleScaleChanged(const Vec3f& new_scale) override;
		
		Bounds GetBounds() const override;

		Vec3f Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const override;

	private:
		float m_Radius = 1.0f;
		float m_BaseRadius = 1.0f;
	};

}
