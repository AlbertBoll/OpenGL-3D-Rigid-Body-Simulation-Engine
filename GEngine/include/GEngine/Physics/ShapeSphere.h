#pragma once
#include "Shape.h"
#include "PhysicsShapeError.h"
#include <expected>

namespace GEngine
{
	class ShapeSphere : public PhysicalShape
	{
	public:
		ShapeSphere() : ShapeSphere(1.0f, ValidatedRadius{}) {}
        [[nodiscard]] static std::expected<ShapeSphere, PhysicsShapeError> Create(float radius);
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
        struct ValidatedRadius {};
        ShapeSphere(float radius, ValidatedRadius) : m_Radius(radius), m_BaseRadius(radius)
        { m_ShapeType = ShapeType::Sphere; }
		float m_Radius = 1.0f;
		float m_BaseRadius = 1.0f;
	};

}
