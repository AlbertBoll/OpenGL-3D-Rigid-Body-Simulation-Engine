#include "gepch.h"

#include "ShapeSphere.h"

#include <cmath>


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