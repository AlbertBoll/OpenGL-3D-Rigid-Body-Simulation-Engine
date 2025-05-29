#include "gepch.h"

#include "ShapeSphere.h"


namespace GEngine
{
	ShapeSphere::ShapeSphere(float radius) : m_Radius(radius)
	{
		m_CenterOfMass = { 0.f, 0.f, 0.f };
		m_ShapeType = ShapeType::Sphere;
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