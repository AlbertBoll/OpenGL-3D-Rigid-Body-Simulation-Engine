#include "gepch.h"
#include "ConstraintPenetration.h"
#include "../PhysicsBody.h"

namespace GEngine
{
	void ConstraintPenetration::PreSolve(const float dt_sec)
	{
		// Get the world space position of the hinge from A's orientation
		const Vec3f worldAnchorA = m_bodyA->BodySpaceToWorldSpace(m_anchorA);

		// Get the world space position of the hinge from B's orientation
		const Vec3f worldAnchorB = m_bodyB->BodySpaceToWorldSpace(m_anchorB);

		const Vec3f ra = worldAnchorA - m_bodyA->GetCenterOfMassWorldSpace();
		const Vec3f rb = worldAnchorB - m_bodyB->GetCenterOfMassWorldSpace();
		const Vec3f a = worldAnchorA;
		const Vec3f b = worldAnchorB;

		const float frictionA = m_bodyA->m_Friction;
		const float frictionB = m_bodyB->m_Friction;
		m_Friction = frictionA * frictionB;

		Vec3f u;
		Vec3f v;
		Math::GetOrtho(m_Normal, u, v);

		// Convert tangent space from model space to world space
		Vec3f normal = glm::transpose(glm::toMat3(m_bodyA->m_Orientation)) * m_Normal;
		//Vec3f normal = glm::toMat3(m_bodyA->m_Orientation) * m_normal;

		u = glm::transpose(glm::toMat3(m_bodyA->m_Orientation)) * u;
		v = glm::transpose(glm::toMat3(m_bodyA->m_Orientation)) * v;

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

		//
		//	Calculate the baumgarte stabilization
		//
		float C = glm::dot(b - a, normal);
		//C = std::min(0.0f, C + 0.02f);	// Add slop
		C = std::min(0.0f, C + 0.02f);	//
		float Beta = 0.25f;
		m_Baumgarte = Beta * C / dt_sec;

	}

	void ConstraintPenetration::Solve()
	{
		const Mat<12, 3> JacobianTranspose = m_Jacobian.Transpose();

		// Build the system of equations
		const Vec<12> q_dt = GetVelocities();
		const Mat<12, 12> invMassMatrix = GetInverseMassMatrix();
		const Mat<3, 3> J_W_Jt = m_Jacobian * invMassMatrix * JacobianTranspose;
		Vec<3> rhs = m_Jacobian * q_dt * -1.0f;
		rhs[0] -= m_Baumgarte;

		// Solve for the Lagrange multipliers
		Vec<3> lambdaN = LCP_GaussSeidel(J_W_Jt, rhs);

		//// Accumulate the impulses and clamp to within the constraint limits
		Vec<3> oldLambda = m_CachedLambda;
		m_CachedLambda += lambdaN;
		const float lambdaLimit = 0.0f;
		if (m_CachedLambda[0] < lambdaLimit) 
		{
			m_CachedLambda[0] = lambdaLimit;
		}
		if (m_Friction > 0.0f) 
		{
			const float umg = m_Friction * 10.0f * 1.0f / (m_bodyA->m_InvMass + m_bodyB->m_InvMass);
			const float normalForce = fabsf(lambdaN[0] * m_Friction);
			const float maxForce = (umg > normalForce) ? umg : normalForce;
			/*m_CachedLambda[1] = glm::clamp(m_CachedLambda[1], -maxForce, maxForce);
			m_CachedLambda[2] = glm::clamp(m_CachedLambda[2], -maxForce, maxForce);*/
			if (m_CachedLambda[1] > maxForce) {
				m_CachedLambda[1] = maxForce;
			}
			if (m_CachedLambda[1] < -maxForce) {
				m_CachedLambda[1] = -maxForce;
			}

			if (m_CachedLambda[2] > maxForce) {
				m_CachedLambda[2] = maxForce;
			}
			if (m_CachedLambda[2] < -maxForce) {
				m_CachedLambda[2] = -maxForce;
			}
		}
		lambdaN = m_CachedLambda - oldLambda;

		// Apply the impulses
		const Vec<12> impulses = JacobianTranspose * lambdaN;
		ApplyImpulses(impulses);
	}
	
}