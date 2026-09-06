#include "gepch.h"
#include "ConstraintPenetration.h"
#include "../PhysicsBody.h"
#include <cmath>

namespace GEngine
{
	namespace
	{
		bool IsFinite(const Vec<3>& value)
		{
			return Math::IsFinite(value[0]) && Math::IsFinite(value[1]) && Math::IsFinite(value[2]);
		}

		double CombinedFriction(const float frictionA, const float frictionB)
		{
			return frictionA > 0.0f && frictionB > 0.0f &&
				Math::IsFinite(frictionA) && Math::IsFinite(frictionB)
				? static_cast<double>(frictionA) * static_cast<double>(frictionB) : 0.0;
		}

		void ProjectCoulombImpulse(Vec<3>& impulse, const double friction)
		{
			if (!IsFinite(impulse))
			{
				impulse.Zero();
				return;
			}

			impulse[0] = std::max(0.0f, impulse[0]);
			// Fix the accumulated normal impulse, then project both tangents onto its Coulomb disk.
			// Double intermediates cover the full range of finite float coefficients and impulses.
			const double limit = friction * static_cast<double>(impulse[0]);
			const double tangentLength = std::hypot(static_cast<double>(impulse[1]),
				static_cast<double>(impulse[2]));
			if (!(limit > 0.0))
			{
				impulse[1] = impulse[2] = 0.0f;
			}
			else if (tangentLength > limit)
			{
				const double scale = limit / tangentLength;
				impulse[1] = static_cast<float>(impulse[1] * scale);
				impulse[2] = static_cast<float>(impulse[2] * scale);
			}
		}

	}

	void ConstraintPenetration::PreSolve(const float dt_sec)
	{
		GENGINE_CORE_ASSERT(Math::IsFinite(dt_sec), "Constraint timestep must be finite");
		if (!IsFinite(m_CachedLambda))
		{
			m_CachedLambda.Zero();
		}

		// Get the world space position of the hinge from A's orientation
		const Vec3f worldAnchorA = m_bodyA->BodySpaceToWorldSpace(m_anchorA);

		// Get the world space position of the hinge from B's orientation
		const Vec3f worldAnchorB = m_bodyB->BodySpaceToWorldSpace(m_anchorB);

		const Vec3f ra = worldAnchorA - m_bodyA->GetCenterOfMassWorldSpace();
		const Vec3f rb = worldAnchorB - m_bodyB->GetCenterOfMassWorldSpace();

		const double friction = CombinedFriction(m_bodyA->m_Friction, m_bodyB->m_Friction);
		// The existing float slot tracks enabled tangent rows; the coefficient stays in double.
		m_Friction = friction > 0.0 ? 1.0f : 0.0f;
		// Cached tangents must obey the current material limit before they are warm started.
		ProjectCoulombImpulse(m_CachedLambda, friction);

		Vec3f u;
		Vec3f v;
		Math::GetOrtho(m_Normal, u, v);

		// Convert tangent space from model space to world space
		const Mat3& bodyToWorld = m_bodyA->GetBodyToWorldRotation();
		Vec3f normal = bodyToWorld * Math::VectorOr(m_Normal);

		u = bodyToWorld * u;
		v = bodyToWorld * v;

		m_Jacobian.Zero();

		Vec3f J1 = normal * -1.0f;
		m_Jacobian[0][0] = J1.x;
		m_Jacobian[0][1] = J1.y;
		m_Jacobian[0][2] = J1.z;

		Vec3f J2 = glm::cross(ra, normal * -1.0f);
		m_Jacobian[0][3] = J2.x;
		m_Jacobian[0][4] = J2.y;
		m_Jacobian[0][5] = J2.z;

		Vec3f J3 = normal * 1.0f;
		m_Jacobian[0][6] = J3.x;
		m_Jacobian[0][7] = J3.y;
		m_Jacobian[0][8] = J3.z;

		Vec3f J4 = glm::cross(rb, normal * 1.0f);
		m_Jacobian[0][9] = J4.x;
		m_Jacobian[0][10] = J4.y;
		m_Jacobian[0][11] = J4.z;

		//
		//	Friction Jacobians
		//
		if (m_Friction > 0.0f) 
		{
			Vec3f J1 = u * -1.0f;
			m_Jacobian[1][0] = J1.x;
			m_Jacobian[1][1] = J1.y;
			m_Jacobian[1][2] = J1.z;

			Vec3f J2 = glm::cross(ra, u * -1.0f);
			m_Jacobian[1][3] = J2.x;
			m_Jacobian[1][4] = J2.y;
			m_Jacobian[1][5] = J2.z;

			Vec3f J3 = u * 1.0f;
			m_Jacobian[1][6] = J3.x;
			m_Jacobian[1][7] = J3.y;
			m_Jacobian[1][8] = J3.z;

			Vec3f J4 = glm::cross(rb, u * 1.0f);
			m_Jacobian[1][9] = J4.x;
			m_Jacobian[1][10] = J4.y;
			m_Jacobian[1][11] = J4.z;
		}
		if (m_Friction > 0.0f) {
			Vec3f J1 = v * -1.0f;
			m_Jacobian[2][0] = J1.x;
			m_Jacobian[2][1] = J1.y;
			m_Jacobian[2][2] = J1.z;

			Vec3f J2 = glm::cross(ra, v * -1.0f);
			m_Jacobian[2][3] = J2.x;
			m_Jacobian[2][4] = J2.y;
			m_Jacobian[2][5] = J2.z;

			Vec3f J3 = v * 1.0f;
			m_Jacobian[2][6] = J3.x;
			m_Jacobian[2][7] = J3.y;
			m_Jacobian[2][8] = J3.z;

			Vec3f J4 = glm::cross(rb, v * 1.0f);
			m_Jacobian[2][9] = J4.x;
			m_Jacobian[2][10] = J4.y;
			m_Jacobian[2][11] = J4.z;
		}

		//
	// Apply warm starting from last frame
	//
		const Vec<12> impulses = m_Jacobian.Transpose() * m_CachedLambda;
		ApplyImpulses(impulses);

		// A paused or reverse step must not depenetrate bodies. No 1/dt velocity bias.
		m_PositionCorrectionEnabled = Math::IsFinite(dt_sec) && dt_sec > Math::NumericalEpsilon;

	}

	void ConstraintPenetration::Solve()
	{
		const Mat<12, 3> JacobianTranspose = m_Jacobian.Transpose();

		// Build the system of equations
		const Vec<12> q_dt = GetVelocities();
		const Mat<12, 12> invMassMatrix = GetInverseMassMatrix();
		const Mat<3, 3> J_W_Jt = m_Jacobian * invMassMatrix * JacobianTranspose;
		Vec<3> rhs = m_Jacobian * q_dt * -1.0f;

		// Solve for the Lagrange multipliers
		Vec<3> lambdaN = LCP_GaussSeidel(J_W_Jt, rhs);
		if (!IsFinite(lambdaN))
		{
			lambdaN.Zero();
		}

		//// Accumulate the impulses and clamp to within the constraint limits
		Vec<3> oldLambda = m_CachedLambda;
		m_CachedLambda += lambdaN;
		const double friction = m_Friction > 0.0f
			? CombinedFriction(m_bodyA->m_Friction, m_bodyB->m_Friction) : 0.0;
		ProjectCoulombImpulse(m_CachedLambda, friction);
		lambdaN = m_CachedLambda - oldLambda;

		// Apply the impulses
		const Vec<12> impulses = JacobianTranspose * lambdaN;
		ApplyImpulses(impulses);
		m_bodyA->AssertFiniteState();
		m_bodyB->AssertFiniteState();
	}
	
	void ConstraintPenetration::PostSolve()
	{
		if (!m_PositionCorrectionEnabled || !m_bodyA || !m_bodyB || m_bodyA == m_bodyB)
		{
			return;
		}
		m_PositionCorrectionEnabled = false;

		// Retain the existing slop/fraction; cap separation change per contact per step.
		constexpr float slop = 0.02f;
		constexpr float fraction = 0.25f;
		constexpr float maxCorrection = 0.2f;
		const double inverseA = m_bodyA->GetInverseMass();
		const double inverseB = m_bodyB->GetInverseMass();
		const double inverseSum = inverseA + inverseB;
		if (!(inverseSum > 0.0)) return;

		// Recompute anchors after integration and earlier position corrections.
		const Vec3f a = m_bodyA->BodySpaceToWorldSpace(m_anchorA);
		const Vec3f b = m_bodyB->BodySpaceToWorldSpace(m_anchorB);
		Vec3f normal = m_bodyA->GetBodyToWorldRotation() * m_Normal;
		const float normalLength2 = glm::length2(normal);
		if (!Math::IsFinite(a) || !Math::IsFinite(b) || !Math::IsFinite(normalLength2) ||
			normalLength2 <= Math::NumericalEpsilon * Math::NumericalEpsilon) return;
		normal /= std::sqrt(normalLength2);
		const Vec3f separation = b - a;
		const float depth = -glm::dot(separation, normal);
		const Vec3f tangent = separation + normal * depth;
		// Match the manifold's existing drift tolerance; do not project stale witnesses.
		if (!Math::IsFinite(depth) || !Math::IsFinite(tangent) ||
			glm::length2(tangent) >= slop * slop || depth <= slop) return;

		const float correction = std::min(maxCorrection, fraction * (depth - slop));
		const Vec3f positionA = m_bodyA->m_Position - normal *
			(correction * static_cast<float>(inverseA / inverseSum));
		const Vec3f positionB = m_bodyB->m_Position + normal *
			(correction * static_cast<float>(inverseB / inverseSum));
		if (!Math::IsFinite(positionA) || !Math::IsFinite(positionB)) return;

		// Translation alone preserves orientation-dependent kinetic energy as well as velocities.
		// For two dynamic bodies the inverse-mass weights preserve their common center of mass.
		if (inverseA > 0.0) m_bodyA->m_Position = positionA;
		if (inverseB > 0.0) m_bodyB->m_Position = positionB;
	}

}
