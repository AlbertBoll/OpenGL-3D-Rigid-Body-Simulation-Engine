#pragma once
#include "Shape.h"

namespace GEngine
{
    // Solid cylinder with hemispherical caps, centered at the origin on local Y.
    // Unit-mass inertia is about its center of mass, for uniform density.
    class ShapeCapsule final : public PhysicalShape
    {
    public:
        [[nodiscard]] static std::expected<ShapeCapsule, PhysicsShapeError>
        Create(float radius, float halfLength);
        [[nodiscard]] std::expected<void, PhysicsShapeError> SetScale(const Vec3f& scale);
        void HandleScaleChanged(const Vec3f& scale) override { (void)SetScale(scale); }
        float GetRadius() const { return m_Radius; }
        float GetHalfLength() const { return m_HalfLength; }
        bool IsValid() const override;
        Mat3 InertiaTensor() const override { return m_Inertia; }
        Bounds GetBounds() const override;
        Bounds GetBounds(const Vec3f& position, const Quat& orientation) const override;
        Vec3f Support(const Vec3f& direction, const Vec3f& position,
                      const Quat& orientation, float bias) const override;
        float FastestLinearSpeed(const Vec3f& angularVelocity, const Vec3f& direction) const override;
    private:
        ShapeCapsule(float radius, float halfLength, const Mat3& inertia)
            : m_Radius(radius), m_HalfLength(halfLength), m_BaseRadius(radius),
              m_BaseHalfLength(halfLength), m_Inertia(inertia)
        { m_ShapeType = ShapeType::Capsule; }
        float m_Radius, m_HalfLength, m_BaseRadius, m_BaseHalfLength;
        Mat3 m_Inertia;
    };
}
