#include "gepch.h"
#include "ShapeCapsule.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GEngine
{
    namespace
    {
        auto CapsuleError(PhysicsShapeErrorCode code, std::string_view message)
        { return std::unexpected(PhysicsShapeError{code, "ShapeCapsule", message}); }

        std::expected<Mat3, PhysicsShapeError> CapsuleInertia(float radius, float halfLength)
        {
            if (!std::isfinite(radius) || radius <= 0)
                return CapsuleError(PhysicsShapeErrorCode::InvalidRadius,
                                    "Capsule requires a finite positive radius");
            if (!std::isfinite(halfLength) || halfLength < 0 ||
                !std::isfinite(radius + halfLength))
                return CapsuleError(PhysicsShapeErrorCode::InvalidDimensions,
                                    "Capsule requires a finite nonnegative segment half-length");
            const double r = radius, h = halfLength;
            // Mass ratio after cancelling density*pi*r^2. Double intermediates
            // avoid overflowing volumes whose normalized inertia is representable.
            const double cylinder = (2*h) / (2*h + 4*r/3);
            const double caps = (4*r/3) / (2*h + 4*r/3);
            const double axial = cylinder*r*r/2 + caps*2*r*r/5;
            // Each hemisphere's COM is 3r/8 from its flat face.
            const double transverse = cylinder*(r*r/4 + h*h/3) +
                                      caps*(2*r*r/5 + h*h + 3*h*r/4);
            Mat3 inertia(0);
            inertia[0][0] = inertia[2][2] = static_cast<float>(transverse);
            inertia[1][1] = static_cast<float>(axial);
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(inertia[axis][axis]) || inertia[axis][axis] <= 0 ||
                    !std::isfinite(1.f / inertia[axis][axis]))
                    return CapsuleError(PhysicsShapeErrorCode::InvalidInertia,
                                        "Capsule inertia or its inverse is not representable");
            return inertia;
        }

        Vec3f UnitDirection(const Vec3f& direction)
        {
            const float magnitude = std::max({std::abs(direction.x), std::abs(direction.y),
                                              std::abs(direction.z)});
            if (magnitude == 0) return {1, 0, 0};
            const Vec3f scaled = direction / magnitude;
            return scaled / glm::length(scaled);
        }
    }

    std::expected<ShapeCapsule, PhysicsShapeError> ShapeCapsule::Create(float radius, float halfLength)
    {
        auto inertia = CapsuleInertia(radius, halfLength);
        if (!inertia) return std::unexpected(inertia.error());
        return ShapeCapsule(radius, halfLength, *inertia);
    }

    std::expected<void, PhysicsShapeError> ShapeCapsule::SetScale(const Vec3f& scale)
    {
        if (!Math::IsFinite(scale) || scale.x <= 0 || scale.x != scale.y || scale.x != scale.z)
            return CapsuleError(PhysicsShapeErrorCode::InvalidScale,
                                "Capsule supports finite positive uniform scale only");
        const float radius = m_BaseRadius * scale.x;
        const float halfLength = m_BaseHalfLength * scale.x;
        auto inertia = CapsuleInertia(radius, halfLength);
        if (!inertia) return std::unexpected(inertia.error());
        if (radius != m_Radius || halfLength != m_HalfLength)
        {
            m_Radius = radius; m_HalfLength = halfLength; m_Inertia = *inertia;
            MarkGeometryChanged();
        }
        return {};
    }

    bool ShapeCapsule::IsValid() const
    { return m_ShapeType == ShapeType::Capsule && bool(CapsuleInertia(m_Radius, m_HalfLength)); }

    Bounds ShapeCapsule::GetBounds() const
    {
        Bounds bounds;
        bounds.maxs = {m_Radius, m_HalfLength + m_Radius, m_Radius};
        bounds.mins = -bounds.maxs;
        return bounds;
    }

    Bounds ShapeCapsule::GetBounds(const Vec3f& position, const Quat& orientation) const
    {
        const Vec3f extent = glm::abs(orientation * Vec3f(0, m_HalfLength, 0)) + Vec3f(m_Radius);
        Bounds bounds;
        bounds.mins = position - extent; bounds.maxs = position + extent;
        return bounds;
    }

    Vec3f ShapeCapsule::Support(const Vec3f& direction, const Vec3f& position,
                              const Quat& orientation, float bias) const
    {
        if (!Math::IsFinite(direction) || !Math::IsFinite(position) ||
            !Math::IsFinite(orientation) || !std::isfinite(bias))
            return Vec3f(std::numeric_limits<float>::quiet_NaN());
        const Vec3f unit = UnitDirection(direction);
        const Vec3f segment = orientation * Vec3f(0, m_HalfLength, 0);
        return position + (glm::dot(unit, segment) >= 0 ? segment : -segment) +
               unit * (m_Radius + bias);
    }

    float ShapeCapsule::FastestLinearSpeed(const Vec3f& angularVelocity, const Vec3f& direction) const
    {
        // max dot(direction, omega x p) = support(dot(p, direction x omega)).
        // Segment plus ball support is exact, and also bounds its swept surface.
        const glm::dvec3 tangent = glm::cross(glm::dvec3(direction), glm::dvec3(angularVelocity));
        const double speed = double(m_HalfLength)*std::abs(tangent.y) +
                             double(m_Radius)*glm::length(tangent);
        return static_cast<float>(speed);
    }
}
