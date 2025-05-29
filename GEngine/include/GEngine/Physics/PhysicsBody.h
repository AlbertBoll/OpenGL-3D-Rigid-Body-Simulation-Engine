#pragma once
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/quaternion_float.hpp>
#include "../Core/Base.h"
#include "Math/Math.h"
#include"Component/Component.h"

namespace GEngine
{


	class PhysicalShape;

	
	using namespace Component;
	using namespace Math;

	class RigidBody3D
	{
	public:
		RigidBody3D() = default;
		Vec3f GetCenterOfMassWorldSpace() const;
		Vec3f GetCenterOfMassModelSpace() const;
		Vec3f WorldSpaceToBodySpace(const Vec3f& pt) const;
		Vec3f BodySpaceToWorldSpace(const Vec3f& pt) const;
		Mat3 GetInverseInertiaTensorBodySpace() const;
		Mat3 GetInverseInertiaTensorWorldSpace() const;

		void ApplyImpulse(const Vec3f& impulsePoint, const Vec3f& impulse);
		void ApplyImpulseLinear(const Vec3f& impulse);
		void ApplyImpulseAngular(const Vec3f& impulse);

		void Update(const float dt_sec);
	
		Vec3f m_Position{ 0.f };
		Quat m_Orientation{ 1.0f, 0.f, 0.f, 0.f };

		Vec3f m_LinearVelocity{ 0.f };
		Vec3f m_AngularVelocity{ 0.f };

		float		m_InvMass = 1.f;
		float		m_Elasticity = 1.f;
		float		m_Friction = 0.f;
		PhysicalShape* m_Shape{};
		BodyType Type = BodyType::Static;
	
		

	};
}