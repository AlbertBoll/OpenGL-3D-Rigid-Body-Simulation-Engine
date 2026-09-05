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

		bool IsSame(const Vec3f& lhs, const Vec3f& rhs)
		{
			return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
		}

		bool IsSame(const Quat& lhs, const Quat& rhs)
		{
			return lhs.w == rhs.w && lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
		}
	}

	bool RigidBody3D::SetBodyTypeAndInverseMass(BodyType type, float inverseMass)
	{
		if ((type != BodyType::Static && type != BodyType::Dynamic && type != BodyType::Kinematic) ||
			!Math::IsFinite(inverseMass) || inverseMass < 0.0f ||
			(type == BodyType::Dynamic && inverseMass == 0.0f))
		{
			return false;
		}
		Type = type;
		m_InvMass = type == BodyType::Dynamic ? inverseMass : 0.0f;
		return true;
	}
	void RigidBody3D::UpdateRotationData() const
	{
		if (m_DerivedData.rotationValid &&
			IsSame(m_DerivedData.rotationSourceOrientation, m_Orientation))
		{
			return;
		}

		const Quat orientation = SafeOrientation(m_Orientation);
		m_DerivedData.bodyToWorld = glm::toMat3(orientation);
		m_DerivedData.worldToBody = glm::transpose(m_DerivedData.bodyToWorld);
		m_DerivedData.rotationSourceOrientation = m_Orientation;
		++m_DerivedData.rotationRevision;
		m_DerivedData.rotationValid = true;
	}

	void RigidBody3D::UpdateCenterOfMassData() const
	{
		UpdateRotationData();
		const std::uint64_t shapeRevision = m_Shape ? m_Shape->GetRevision() : 0;
		if (m_DerivedData.centerValid && IsSame(m_DerivedData.centerSourcePosition, m_Position) &&
			m_DerivedData.centerSourceShape == m_Shape &&
			m_DerivedData.centerSourceShapeRevision == shapeRevision &&
			m_DerivedData.centerSourceRotationRevision == m_DerivedData.rotationRevision)
		{
			return;
		}

		m_DerivedData.centerOfMassWorld = m_Position;
		if (m_Shape)
		{
			m_DerivedData.centerOfMassWorld +=
				m_DerivedData.bodyToWorld * m_Shape->GetCenterOfMass();
		}
		m_DerivedData.centerSourcePosition = m_Position;
		m_DerivedData.centerSourceShape = m_Shape;
		m_DerivedData.centerSourceShapeRevision = shapeRevision;
		m_DerivedData.centerSourceRotationRevision = m_DerivedData.rotationRevision;
		m_DerivedData.centerValid = true;
	}

	void RigidBody3D::UpdateBodyInertiaData() const
	{
		const float inverseMass = GetInverseMass();
		const std::uint64_t shapeRevision = m_Shape ? m_Shape->GetRevision() : 0;
		if (m_DerivedData.bodyInertiaValid &&
			m_DerivedData.inertiaSourceInverseMass == inverseMass &&
			m_DerivedData.inertiaSourceShape == m_Shape &&
			m_DerivedData.inertiaSourceShapeRevision == shapeRevision)
		{
			return;
		}

		m_DerivedData.inertiaBody = ZeroMat3();
		m_DerivedData.inverseInertiaBody = ZeroMat3();

		if (m_Shape)
		{
			m_DerivedData.inertiaBody = m_Shape->InertiaTensor();
			if (inverseMass > 0.0f)
			{
				m_DerivedData.inverseInertiaBody =
					InverseOrZero(m_DerivedData.inertiaBody) * inverseMass;
			}
		}
		m_DerivedData.inertiaSourceInverseMass = inverseMass;
		m_DerivedData.inertiaSourceShape = m_Shape;
		m_DerivedData.inertiaSourceShapeRevision = shapeRevision;
		++m_DerivedData.bodyInertiaRevision;
		m_DerivedData.bodyInertiaValid = true;
	}

	void RigidBody3D::UpdateWorldInertiaData() const
	{
		UpdateRotationData();
		UpdateBodyInertiaData();
		if (m_DerivedData.worldInertiaValid &&
			m_DerivedData.worldInertiaSourceBodyRevision == m_DerivedData.bodyInertiaRevision &&
			m_DerivedData.worldInertiaSourceRotationRevision == m_DerivedData.rotationRevision)
		{
			return;
		}

		m_DerivedData.inertiaWorld = m_DerivedData.bodyToWorld *
			m_DerivedData.inertiaBody * m_DerivedData.worldToBody;
		m_DerivedData.inverseInertiaWorld = m_DerivedData.bodyToWorld *
			m_DerivedData.inverseInertiaBody * m_DerivedData.worldToBody;
		m_DerivedData.worldInertiaSourceBodyRevision = m_DerivedData.bodyInertiaRevision;
		m_DerivedData.worldInertiaSourceRotationRevision = m_DerivedData.rotationRevision;
		m_DerivedData.worldInertiaValid = true;
	}

	void RigidBody3D::UpdateBoundsData() const
	{
		UpdateRotationData();
		const std::uint64_t shapeRevision = m_Shape ? m_Shape->GetRevision() : 0;
		if (m_DerivedData.boundsValid && IsSame(m_DerivedData.boundsSourcePosition, m_Position) &&
			m_DerivedData.boundsSourceShape == m_Shape &&
			m_DerivedData.boundsSourceShapeRevision == shapeRevision &&
			m_DerivedData.boundsSourceRotationRevision == m_DerivedData.rotationRevision)
		{
			return;
		}

		m_DerivedData.worldBounds.Clear();
		if (m_Shape)
		{
			m_DerivedData.worldBounds = m_Shape->GetBounds(m_Position, SafeOrientation(m_Orientation));
		}
		m_DerivedData.boundsSourcePosition = m_Position;
		m_DerivedData.boundsSourceShape = m_Shape;
		m_DerivedData.boundsSourceShapeRevision = shapeRevision;
		m_DerivedData.boundsSourceRotationRevision = m_DerivedData.rotationRevision;
		m_DerivedData.boundsValid = true;
	}

	Vec3f RigidBody3D::GetCenterOfMassWorldSpace() const
	{
		UpdateCenterOfMassData();
		return m_DerivedData.centerOfMassWorld;
	}

	Vec3f RigidBody3D::GetCenterOfMassModelSpace() const
	{
		return  m_Shape->GetCenterOfMass();;
	}

	Vec3f RigidBody3D::WorldSpaceToBodySpace(const Vec3f& pt) const
	{
		UpdateCenterOfMassData();
		return m_DerivedData.worldToBody * (pt - m_DerivedData.centerOfMassWorld);
	}

	Vec3f RigidBody3D::BodySpaceToWorldSpace(const Vec3f& pt) const
	{
		UpdateCenterOfMassData();
		return m_DerivedData.centerOfMassWorld + m_DerivedData.bodyToWorld * pt;
	}

	Mat3 RigidBody3D::GetInverseInertiaTensorBodySpace() const
	{
		UpdateBodyInertiaData();
		return m_DerivedData.inverseInertiaBody;
	}

	Mat3 RigidBody3D::GetInverseInertiaTensorWorldSpace() const
	{
		UpdateWorldInertiaData();
		return m_DerivedData.inverseInertiaWorld;
	}

	const Mat3& RigidBody3D::GetBodyToWorldRotation() const
	{
		UpdateRotationData();
		return m_DerivedData.bodyToWorld;
	}

	const Mat3& RigidBody3D::GetWorldToBodyRotation() const
	{
		UpdateRotationData();
		return m_DerivedData.worldToBody;
	}

	const Bounds& RigidBody3D::GetWorldBounds() const
	{
		UpdateBoundsData();
		return m_DerivedData.worldBounds;
	}

	void RigidBody3D::ApplyImpulse(const Vec3f& impulsePoint, const Vec3f& impulse)
	{
		/*if (m_InvMass <= 0.0001f) {
			return;
		}*/

		if (Type != BodyType::Dynamic) return;

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

		if (Type != BodyType::Dynamic)
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

		if (Type != BodyType::Dynamic) return;

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
		if (!Math::IsFinite(dt_sec) || !m_Shape || !CanIntegrate())
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

		// Torque-free Euler equation in world space: I * omega_dot = -omega x (I * omega).
		UpdateWorldInertiaData();
		// Only dynamic bodies receive gyroscopic acceleration; mass validation is numerical protection.
		if (Type == BodyType::Dynamic && Math::IsFinite(m_InvMass) && m_InvMass > 0.0f)
		{
			// inertiaWorld is unit-mass inertia; inverseInertiaWorld includes inverse mass.
			// Cancel that mass factor so free precession is independent of body mass.
			const Vec3f gyroscopicTerm = glm::cross(m_AngularVelocity,
				m_DerivedData.inertiaWorld * m_AngularVelocity);
			const Vec3f alpha = -(m_DerivedData.inverseInertiaWorld * gyroscopicTerm) / m_InvMass;
			m_AngularVelocity += alpha * dt_sec;
		}

		// Update orientation
		Vec3f dAngle = m_AngularVelocity * dt_sec;
		
		//Quat dq = glm::angleAxis(glm::length(dAngle), glm::normalize(dAngle));
		Quat dq = glm::length(dAngle) >= 0.00001f ? glm::angleAxis(glm::length(dAngle), glm::normalize(dAngle)) : Quat{ 1, 0, 0, 0 };
		
		m_Orientation = dq * m_Orientation;
		
		//m_Orientation = glm::length(m_Orientation) >= 0.00001f ? glm::normalize(m_Orientation): m_Orientation;
		
		//std::cout << "m_Orientation length: " << glm::length(m_Orientation) << std::endl;
		m_Orientation = Math::NormalizeOrIdentity(m_Orientation);


		// Now get the new model position
		m_Position = positionCM + glm::toMat3(dq) * cmToPos;
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
