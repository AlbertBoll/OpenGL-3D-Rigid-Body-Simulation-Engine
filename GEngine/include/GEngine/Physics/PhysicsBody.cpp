#include "gepch.h"

#include "PhysicsBody.h"
#include "Shape.h"
#include <glm/gtx/quaternion.hpp>
#include <iostream>

namespace GEngine
{
	namespace
	{
		Mat3 ZeroMat3()
		{
			return Mat3(0.0f);
		}

		bool IsFinite(const Mat3& matrix)
		{
			for (int column = 0; column < 3; ++column)
			{
				if (!Math::IsFinite(matrix[column]))
				{
					return false;
				}
			}
			return true;
		}

		Mat3 InverseOrZero(const Mat3& matrix)
		{
			const float determinant = glm::determinant(matrix);
			if (!Math::IsFinite(determinant) || std::fabs(determinant) <= Math::NumericalEpsilon)
			{
				return ZeroMat3();
			}

			const Mat3 inverse = glm::inverse(matrix);
			return IsFinite(inverse) ? inverse : ZeroMat3();
		}

		Quat SafeOrientation(const Quat& orientation)
		{
			return Math::QuaternionOrIdentity(orientation);
		}
	}

	Vec3f RigidBody3D::GetCenterOfMassWorldSpace() const
	{
		const Vec3f centerOfMass = m_Shape->GetCenterOfMass();
		const Vec3f pos = m_Position + glm::transpose(glm::toMat3(SafeOrientation(m_Orientation))) * centerOfMass;
		return pos;
	}

	Vec3f RigidBody3D::GetCenterOfMassModelSpace() const
	{
		return  m_Shape->GetCenterOfMass();;
	}

	Vec3f RigidBody3D::WorldSpaceToBodySpace(const Vec3f& pt) const
	{
		Vec3f tmp = pt - GetCenterOfMassWorldSpace();
		Quat inverseOrient = glm::inverse(SafeOrientation(m_Orientation));//m_orientation.Inverse();
		Vec3f bodySpace = glm::transpose(glm::toMat3(inverseOrient)) * tmp;
		//Vec3f bodySpace = glm::toMat3(inverseOrient) * tmp;
		return bodySpace;
	}

	Vec3f RigidBody3D::BodySpaceToWorldSpace(const Vec3f& pt) const
	{
		Vec3f worldSpace = GetCenterOfMassWorldSpace() + glm::transpose(glm::toMat3(SafeOrientation(m_Orientation))) * pt;
		//Vec3f worldSpace = GetCenterOfMassWorldSpace() + glm::toMat3(m_Orientation) * pt;
		return worldSpace;
	}

	Mat3 RigidBody3D::GetInverseInertiaTensorBodySpace() const
	{
		if (!m_Shape || !Math::IsFinite(m_InvMass) || m_InvMass <= 0.0f)
		{
			return ZeroMat3();
		}

		Mat3 inertiaTensor = m_Shape->InertiaTensor();
		Mat3 invInertiaTensor = InverseOrZero(inertiaTensor) * m_InvMass;
		return invInertiaTensor;
	}

	Mat3 RigidBody3D::GetInverseInertiaTensorWorldSpace() const
	{
		if (!m_Shape || !Math::IsFinite(m_InvMass) || m_InvMass <= 0.0f)
		{
			return ZeroMat3();
		}

		Mat3 inertiaTensor = m_Shape->InertiaTensor();
		Mat3 invInertiaTensor = InverseOrZero(inertiaTensor) * m_InvMass;
		Mat3 orient = glm::toMat3(SafeOrientation(m_Orientation));
		//invInertiaTensor = orient * invInertiaTensor * glm::transpose(orient);
		invInertiaTensor = glm::transpose(orient) * invInertiaTensor * orient;
		return invInertiaTensor;
	}

	void RigidBody3D::ApplyImpulse(const Vec3f& impulsePoint, const Vec3f& impulse)
	{
		/*if (m_InvMass <= 0.0001f) {
			return;
		}*/

		if (Type == BodyType::Static) return;

		// impulsePoint is the world space location of the application of the impulse
		// impulse is the world space direction and magnitude of the impulse
		ApplyImpulseLinear(impulse);

		Vec3f position = GetCenterOfMassWorldSpace();	// applying impulses must produce torques through the center of mass
		Vec3f r = impulsePoint - position;
		Vec3f dL = glm::cross(r, impulse);// r.Cross(impulse);	// this is in world space
		ApplyImpulseAngular(dL);
	}

	void RigidBody3D::ApplyImpulseLinear(const Vec3f& impulse)
	{

		if (Type == BodyType::Static)
		{
			return;
		}

		// p = mv
		// dp = m dv = J
		// => dv = J / m
		if (!Math::IsFinite(impulse) || !Math::IsFinite(m_InvMass) || m_InvMass <= 0.0f)
		{
			return;
		}

		m_LinearVelocity += impulse * m_InvMass;
		AssertFiniteState();
	}

	void RigidBody3D::ApplyImpulseAngular(const Vec3f& impulse)
	{

		if (Type == BodyType::Static) return;

		// L = I w = r x p
		// dL = I dw = r x J 
		// => dw = I^-1 * ( r x J )
		if (!Math::IsFinite(impulse) || !Math::IsFinite(m_InvMass) || m_InvMass <= 0.0f)
		{
			return;
		}

		m_AngularVelocity += GetInverseInertiaTensorWorldSpace() * impulse;

		const float maxAngularSpeed = 30.0f; // 30 rad/s is fast enough for us. But feel free to adjust.
		if (glm::length2(m_AngularVelocity) > maxAngularSpeed * maxAngularSpeed) {
			m_AngularVelocity = glm::normalize(m_AngularVelocity);
			m_AngularVelocity *= maxAngularSpeed;
		}
		AssertFiniteState();
	}

	void RigidBody3D::Update(const float dt_sec)
	{
		GENGINE_CORE_ASSERT(Math::IsFinite(dt_sec), "Physics timestep must be finite");
		if (!Math::IsFinite(dt_sec) || !m_Shape)
		{
			return;
		}

		m_Orientation = SafeOrientation(m_Orientation);

		m_Position += m_LinearVelocity * dt_sec;
		
		// okay, we have an angular velocity around the center of mass, this needs to be
		// converted somehow to relative to model position.  This way we can properly update
		// the orientation of the model.
		Vec3f positionCM = GetCenterOfMassWorldSpace();
		Vec3f cmToPos = m_Position - positionCM;

		// Total Torque is equal to external applied torques + internal torque (precession)
		// T = T_external + omega x I * omega
		// T_external = 0 because it was applied in the collision response function
		// T = Ia = w x I * w
		// a = I^-1 ( w x I * w )
		Mat3 orientation = glm::toMat3(m_Orientation);
		Mat3 inertiaTensor = glm::transpose(orientation) * m_Shape->InertiaTensor() * orientation;
		Vec3f alpha = InverseOrZero(inertiaTensor) * glm::cross(m_AngularVelocity, inertiaTensor * m_AngularVelocity);
		m_AngularVelocity += alpha * dt_sec;

		// Update orientation
		Vec3f dAngle = m_AngularVelocity * dt_sec;
		
		//Quat dq = glm::angleAxis(glm::length(dAngle), glm::normalize(dAngle));
		Quat dq = glm::length(dAngle) >= 0.00001f ? glm::angleAxis(glm::length(dAngle), glm::normalize(dAngle)) : Quat{ 1, 0, 0, 0 };
		
		m_Orientation = dq * m_Orientation;
		
		//m_Orientation = glm::length(m_Orientation) >= 0.00001f ? glm::normalize(m_Orientation): m_Orientation;
		
		//std::cout << "m_Orientation length: " << glm::length(m_Orientation) << std::endl;
		m_Orientation = Math::NormalizeOrIdentity(m_Orientation);


		// Now get the new model position
		m_Position = positionCM + glm::transpose(glm::toMat3(dq)) * cmToPos;//dq.RotatePoint(cmToPos);
		AssertFiniteState();
	}

	bool RigidBody3D::HasFiniteState() const
	{
		return Math::IsFinite(m_Position) && Math::IsFinite(m_Orientation) &&
			Math::IsFinite(m_LinearVelocity) && Math::IsFinite(m_AngularVelocity) &&
			Math::IsFinite(m_InvMass) && Math::IsFinite(m_Elasticity) && Math::IsFinite(m_Friction);
	}

	#ifdef GENGINE_CONFIG_DEBUG
		void RigidBody3D::AssertFiniteState() const
		{
			GENGINE_CORE_ASSERT(HasFiniteState(), "Physics body state must remain finite");
		}
	#endif

}
