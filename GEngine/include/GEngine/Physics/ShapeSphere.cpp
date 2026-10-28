#include "gepch.h"

#include "ShapeSphere.h"

#include <cmath>
#include <new>


namespace GEngine
{
    std::expected<ShapeSphere, PhysicsShapeError> ShapeSphere::Create(float radius)
    {
        if (!std::isfinite(radius) || radius <= 0.0f)
            return std::unexpected(PhysicsShapeError{PhysicsShapeErrorCode::InvalidRadius,
                "ShapeSphere::Create", "ShapeSphere requires a finite positive radius", radius});
        return ShapeSphere(radius, ValidatedRadius{});
    }

	bool ShapeSphere::IsValid() const
	{
		return std::isfinite(m_Radius) && m_Radius > 0.0f;
	}

	void ShapeSphere::SetRadius(float radius)
	{
		if (!std::isfinite(radius) || radius <= 0.0f || radius == m_Radius) {
			return;
		}
		m_Radius = radius;
		m_BaseRadius = radius;
		MarkGeometryChanged();
	}

	void ShapeSphere::HandleScaleChanged(const Vec3f& new_scale)
	{
		const float radius = m_BaseRadius * new_scale.x;
		if (!std::isfinite(radius) || radius <= 0.0f || radius == m_Radius) {
			return;
		}
		m_Radius = radius;
		MarkGeometryChanged();
	}

    std::expected<std::unique_ptr<PhysicalShape>, PhysicsShapeError>
    ShapeSphere::PrepareScale(const Vec3f& scale) const
    {
        const float radius = m_BaseRadius * scale.x;
        const double inertia = 2.0 * double(radius) * radius / 5.0;
        if (!Math::IsFinite(scale) || !std::isfinite(radius) || radius <= 0 ||
            !std::isfinite(float(inertia)) || float(inertia) <= 0 ||
            !std::isfinite(1.f / float(inertia)))
            return std::unexpected(PhysicsShapeError{PhysicsShapeErrorCode::InvalidScale,
                "ShapeSphere::PrepareScale", "Scale requires a representable positive X-axis radius/inertia"});
        std::unique_ptr<ShapeSphere> staged(new (std::nothrow) ShapeSphere(*this));
        if (!staged)
            return std::unexpected(PhysicsShapeError{PhysicsShapeErrorCode::Allocation,
                "ShapeSphere::PrepareScale", "Scale staging allocation failed"});
        staged->HandleScaleChanged(scale);
        return staged;
    }

	Mat3 ShapeSphere::InertiaTensor() const
	{
		Mat3 tensor(0.f);
		tensor[0][0] = tensor[1][1] = tensor[2][2] = 2.0f * m_Radius * m_Radius / 5.0f;
		return tensor;
	}

	Bounds ShapeSphere::GetBounds(const Vec3f& pos, const Quat& orient) const
	{
		Bounds tmp;
		tmp.mins = Vec3f(-m_Radius) + pos;
		tmp.maxs = Vec3f(m_Radius) + pos;
		return tmp;
	}

	Bounds ShapeSphere::GetBounds() const
	{
		Bounds tmp;
		tmp.mins = Vec3f(-m_Radius);
		tmp.maxs = Vec3f(m_Radius);
		return tmp;
	}

	Vec3f ShapeSphere::Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const
	{
		//Vec3f tmp = glm::normalize(dir);
		return (pos + dir * (m_Radius + bias));
	}

}
