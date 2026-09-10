#include "gepch.h"
#include "ConstraintPenetration.h"
#include "../PhysicsBody.h"
#include "../PhysicsProfile.h"
#include <cmath>

namespace GEngine
{
	namespace
	{
		// Mirror Constraint's mass layout and impulse order using contact-local storage.
		SolverMath::Matrix<12, 12> SolverInverseMass(const RigidBody3D& a, const RigidBody3D& b)
		{
			SolverMath::Matrix<12, 12> result;
			result[0][0] = a.GetInverseMass();
			result[1][1] = a.GetInverseMass();
			result[2][2] = a.GetInverseMass();
			const Mat3 inertiaA = a.GetInverseInertiaTensorWorldSpace();
			for (int row = 0; row < 3; ++row)
				for (int column = 0; column < 3; ++column)
					result[3 + row][3 + column] = inertiaA[column][row];
			result[6][6] = b.GetInverseMass();
			result[7][7] = b.GetInverseMass();
			result[8][8] = b.GetInverseMass();
			const Mat3 inertiaB = b.GetInverseInertiaTensorWorldSpace();
			for (int row = 0; row < 3; ++row)
				for (int column = 0; column < 3; ++column)
					result[9 + row][9 + column] = inertiaB[column][row];
			return result;
		}

		SolverMath::Vector<12> SolverVelocities(const RigidBody3D& a, const RigidBody3D& b)
		{
			SolverMath::Vector<12> result;
			const Vec3f values[] = { a.GetLinearVelocity(), a.GetAngularVelocity(),
				b.GetLinearVelocity(), b.GetAngularVelocity() };
			for (int group = 0; group < 4; ++group)
				for (int axis = 0; axis < 3; ++axis) result[group * 3 + axis] = values[group][axis];
			return result;
		}

		void ApplySolverImpulses(RigidBody3D& a, RigidBody3D& b, const SolverMath::Vector<12>& impulse)
		{
			a.ApplyImpulseLinear(Vec3f(impulse[0], impulse[1], impulse[2]));
			a.ApplyImpulseAngular(Vec3f(impulse[3], impulse[4], impulse[5]));
			b.ApplyImpulseLinear(Vec3f(impulse[6], impulse[7], impulse[8]));
			b.ApplyImpulseAngular(Vec3f(impulse[9], impulse[10], impulse[11]));
		}

		bool IsFinite(const SolverMath::Vector<3>& value)
		{
			return Math::IsFinite(value[0]) && Math::IsFinite(value[1]) && Math::IsFinite(value[2]);
		}

		double CombinedFriction(const float frictionA, const float frictionB)
		{
			return frictionA > 0.0f && frictionB > 0.0f &&
				Math::IsFinite(frictionA) && Math::IsFinite(frictionB)
				? static_cast<double>(frictionA) * static_cast<double>(frictionB) : 0.0;
		}

		void ProjectCoulombImpulse(SolverMath::Vector<3>& impulse, const double friction)
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

		// Minimize 1/2 x^T Kt x - b^T x on the circular Coulomb disk.
		// Clamping Kt^-1 b radially is only correct for isotropic tangent mass.
		void SolveTangentDisk(SolverMath::Vector<3>& lambda, const SolverMath::Matrix<3, 3>& mass,
			const SolverMath::Vector<3>& residual, double friction)
		{
			const double a = mass[1][1], b = mass[1][2], c = mass[2][2];
			const double limit = friction * lambda[0];
			if (!(limit > 0.0)) { lambda[1] = lambda[2] = 0.0f; return; }
			if (!(a > Math::NumericalEpsilon && c > Math::NumericalEpsilon)) return;
			const double r1 = a * lambda[1] + b * lambda[2] + residual[1];
			const double r2 = b * lambda[1] + c * lambda[2] + residual[2];
			const auto solve = [&](double alpha, double& x, double& y) {
				double aa = a + alpha, cc = c + alpha, bb = b;
				double determinant = aa * cc - bb * bb;
				double scale = 1.0;
				if (!std::isfinite(determinant)) {
					// Extremely small finite friction can require a very large multiplier.
					scale = std::max(aa, cc);
					aa /= scale; cc /= scale; bb /= scale;
					determinant = aa * cc - bb * bb;
				}
				if (!(determinant > 0.0) || !std::isfinite(determinant)) return false;
				x = (cc * r1 - bb * r2) / determinant / scale;
				y = (aa * r2 - bb * r1) / determinant / scale;
				return std::isfinite(x) && std::isfinite(y);
			};
			double x = 0.0, y = 0.0;
			if (!solve(0.0, x, y)) return;
			if (std::hypot(x, y) > limit) {
				// Kt is positive definite. (Kt + alpha I)^-1 b decreases in norm,
				// and |b| / limit bounds alpha from above. 32 bisections resolve
				// the bracket beyond float precision without an unbounded solve.
				double low = 0.0, high = std::hypot(r1, r2) / limit;
				for (int iteration = 0; iteration < 32; ++iteration) {
					const double mid = (low + high) * 0.5;
					if (!solve(mid, x, y)) return;
					if (std::hypot(x, y) > limit) low = mid;
					else high = mid;
				}
				if (!solve(high, x, y)) return;
			}
			if (std::abs(x) <= std::numeric_limits<float>::max() &&
				std::abs(y) <= std::numeric_limits<float>::max()) {
				lambda[1] = static_cast<float>(x);
				lambda[2] = static_cast<float>(y);
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

		// Start cold each step: refreshed/replaced/expired contacts cannot reuse torsion,
		// and no old load is used for a torsional warm start before the normal solve.
		m_SpinImpulse = 0.0;
		m_SpinResistanceLength = dt_sec > 0.0f && Math::IsFinite(dt_sec)
			? std::min(m_bodyA->GetSpinResistanceLength(), m_bodyB->GetSpinResistanceLength()) : 0.0f;

		// Rolling also starts cold: no cached load, normal, material or dt can leak across steps.
		m_RollingImpulse = glm::dvec2(0.0);
		m_RollingResistanceLength = dt_sec > 0.0f && Math::IsFinite(dt_sec)
			? std::min(m_bodyA->GetRollingResistanceLength(), m_bodyB->GetRollingResistanceLength()) : 0.0f;

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
		const SolverMath::Vector<12> impulses = m_Jacobian.Transpose() * m_CachedLambda;
		ApplySolverImpulses(*m_bodyA, *m_bodyB, impulses);

		// A paused or reverse step must not depenetrate bodies. No 1/dt velocity bias.
		m_PositionCorrectionEnabled = Math::IsFinite(dt_sec) && dt_sec > Math::NumericalEpsilon;

	}

	void ConstraintPenetration::Solve() { Solve(true); }

	void ConstraintPenetration::SolveFriction() { Solve(false); }

	void ConstraintPenetration::Solve(bool solveNormal)
	{
		const SolverMath::Matrix<12, 3> JacobianTranspose = m_Jacobian.Transpose();

		// Build the system of equations
		const SolverMath::Vector<12> q_dt = SolverVelocities(*m_bodyA, *m_bodyB);
		const SolverMath::Matrix<12, 12> invMassMatrix = SolverInverseMass(*m_bodyA, *m_bodyB);
		const SolverMath::Matrix<3, 3> J_W_Jt = m_Jacobian * invMassMatrix * JacobianTranspose;
		SolverMath::Vector<3> rhs = m_Jacobian * q_dt * -1.0f;

		const SolverMath::Vector<3> oldLambda = m_CachedLambda;
		const double friction = m_Friction > 0.0f
			? CombinedFriction(m_bodyA->m_Friction, m_bodyB->m_Friction) : 0.0;
		if (!IsFinite(rhs)) return;
		for (int row = 0; row < 3; ++row)
			for (int column = 0; column < 3; ++column)
				if (!Math::IsFinite(J_W_Jt[row][column])) return;

		// Retain three local iterations, but solve the constrained accumulated
		// impulse. Every correction must see the preceding normal/friction clamp.
		for (int iteration = 0; iteration < (solveNormal ? 3 : 1); ++iteration) {
			SolverMath::Vector<3> residual = rhs - J_W_Jt * (m_CachedLambda - oldLambda);
			const float normalMass = J_W_Jt[0][0];
			if (solveNormal && normalMass > Math::NumericalEpsilon && Math::IsFinite(residual[0])) {
				const float next = m_CachedLambda[0] + residual[0] / normalMass;
				if (Math::IsFinite(next)) m_CachedLambda[0] = std::max(0.0f, next);
			}
			residual = rhs - J_W_Jt * (m_CachedLambda - oldLambda);
			if (IsFinite(residual)) SolveTangentDisk(m_CachedLambda, J_W_Jt, residual, friction);
			ProjectCoulombImpulse(m_CachedLambda, friction);
		}
		const SolverMath::Vector<3> lambdaN = m_CachedLambda - oldLambda;

		// Apply the impulses
		const SolverMath::Vector<12> impulses = JacobianTranspose * lambdaN;
		ApplySolverImpulses(*m_bodyA, *m_bodyB, impulses);
		SolveSpin();
		SolveRolling();
		m_bodyA->AssertFiniteState();
		m_bodyB->AssertFiniteState();
	}
	
	void ConstraintPenetration::SolveSpin()
	{
		if (!(m_SpinResistanceLength > 0.0f)) return;
		GE_PHYSICS_PROFILE_SCOPE(spinResistanceTimeNs);
		GE_PHYSICS_PROFILE_ADD(spinResistanceSolveCount, 1);
		// J = [0, -n, 0, n], k = n dot (IA^-1 + IB^-1) n.
		const Vec3f axis(m_Jacobian[0][6], m_Jacobian[0][7], m_Jacobian[0][8]);
		const double length2 = glm::dot(glm::dvec3(axis), glm::dvec3(axis));
		if (!(length2 > 0.0) || !std::isfinite(length2)) return;
		const Vec3f normal(glm::dvec3(axis) / std::sqrt(length2));
		const Vec3f responseA = m_bodyA->GetInverseInertiaTensorWorldSpace() * normal;
		const Vec3f responseB = m_bodyB->GetInverseInertiaTensorWorldSpace() * normal;
		if (!Math::IsFinite(responseA) || !Math::IsFinite(responseB)) return;
		const double k = glm::dot(glm::dvec3(normal), glm::dvec3(responseA) + glm::dvec3(responseB));
		const double spin = glm::dot(glm::dvec3(m_bodyB->GetAngularVelocity()) -
			glm::dvec3(m_bodyA->GetAngularVelocity()), glm::dvec3(normal));
		if (!(k > 0.0) || !std::isfinite(k) || !std::isfinite(spin) ||
			!Math::IsFinite(m_CachedLambda[0])) return;
		// The length already includes the material coefficient and contact-patch scale.
		// Each point uses its own solved load: a patch gets at most length * sum(lambdaN).
		const double limit = double(m_SpinResistanceLength) * std::max(0.0f, m_CachedLambda[0]);
		const double next = std::clamp(m_SpinImpulse - spin / k, -limit, limit);
		const double delta = next - m_SpinImpulse;
		if (!std::isfinite(delta) || std::abs(delta) > std::numeric_limits<float>::max()) return;
		const float applied = static_cast<float>(delta);
		if (applied == 0.0f) return;
		const Vec3f impulse = normal * applied;
		// Validate the actual float operations before either body is changed.
		const Vec3f nextA = m_bodyA->m_AngularVelocity -
			m_bodyA->GetInverseInertiaTensorWorldSpace() * impulse;
		const Vec3f nextB = m_bodyB->m_AngularVelocity +
			m_bodyB->GetInverseInertiaTensorWorldSpace() * impulse;
		if (!Math::IsFinite(impulse) || !Math::IsFinite(nextA) || !Math::IsFinite(nextB)) return;
		m_bodyA->ApplyImpulseAngular(-impulse);
		m_bodyB->ApplyImpulseAngular(impulse);
		m_SpinImpulse += applied;
		GE_PHYSICS_PROFILE_ADD(spinResistanceImpulseCount, 1);
	}

	void ConstraintPenetration::SolveRolling()
	{
		if (!(m_RollingResistanceLength > 0.0f)) return;
		GE_PHYSICS_PROFILE_SCOPE(rollingResistanceTimeNs);
		GE_PHYSICS_PROFILE_ADD(rollingResistanceSolveCount, 1);
		const glm::dvec3 axis(m_Jacobian[0][6], m_Jacobian[0][7], m_Jacobian[0][8]);
		const double length2 = glm::dot(axis, axis);
		if (!(length2 > 0.0) || !std::isfinite(length2) || !Math::IsFinite(m_CachedLambda[0])) return;
		const glm::dvec3 normal = axis / std::sqrt(length2);
		// The least-aligned coordinate axis gives a deterministic, well-conditioned basis.
		const auto absolute = glm::abs(normal);
		const glm::dvec3 seed = absolute.x <= absolute.y && absolute.x <= absolute.z
			? glm::dvec3(1, 0, 0) : (absolute.y <= absolute.z ? glm::dvec3(0, 1, 0) : glm::dvec3(0, 0, 1));
		const glm::dvec3 u = glm::normalize(glm::cross(normal, seed)), v = glm::cross(normal, u);
		const Mat3 inverseA = m_bodyA->GetInverseInertiaTensorWorldSpace();
		const Mat3 inverseB = m_bodyB->GetInverseInertiaTensorWorldSpace();
		const glm::dmat3 inverse = glm::dmat3(inverseA) + glm::dmat3(inverseB);
		const double a = glm::dot(u, inverse * u), b = glm::dot(u, inverse * v), c = glm::dot(v, inverse * v);
		if (!(a > 0.0 && c > 0.0) || !std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) return;
		const glm::dvec3 relative = glm::dvec3(m_bodyB->GetAngularVelocity()) - glm::dvec3(m_bodyA->GetAngularVelocity());
		const glm::dvec2 velocity(glm::dot(relative, u), glm::dot(relative, v));
		if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y)) return;
		// Minimize 1/2 lambda^T K lambda - rhs^T lambda on |lambda| <= ell * lambdaN.
		// A radial clamp of K^-1 rhs is incorrect for anisotropic angular inertia.
		const glm::dvec2 rhs(a * m_RollingImpulse.x + b * m_RollingImpulse.y - velocity.x,
			b * m_RollingImpulse.x + c * m_RollingImpulse.y - velocity.y);
		const double limit = double(m_RollingResistanceLength) * std::max(0.0f, m_CachedLambda[0]);
		glm::dvec2 next(0.0);
		const auto solve = [&](double alpha) {
			const double scale = std::max({a, c, alpha});
			const double aa = a / scale + alpha / scale, bb = b / scale, cc = c / scale + alpha / scale;
			const double determinant = aa * cc - bb * bb;
			if (!(determinant > 0.0) || !std::isfinite(determinant)) return false;
			const auto scaled = rhs / scale;
			next = glm::dvec2(cc * scaled.x - bb * scaled.y, aa * scaled.y - bb * scaled.x) / determinant;
			return std::isfinite(next.x) && std::isfinite(next.y);
		};
		if (limit > 0.0) {
			if (!solve(0.0)) return;
			if (std::hypot(next.x, next.y) > limit) {
				double low = 0.0, high = std::hypot(rhs.x, rhs.y) / limit;
				if (!std::isfinite(high)) return;
				// SPD K makes the norm monotone in alpha; high is a feasible upper bound.
				for (int iteration = 0; iteration < 48; ++iteration) {
					const double mid = (low + high) * 0.5;
					if (!solve(mid)) return;
					if (std::hypot(next.x, next.y) > limit) low = mid;
					else high = mid;
				}
				if (!solve(high)) return;
			}
		}
		const auto delta = next - m_RollingImpulse;
		const glm::dvec3 candidate = u * delta.x + v * delta.y;
		const double maximum = std::numeric_limits<float>::max();
		if (!(std::abs(candidate.x) <= maximum && std::abs(candidate.y) <= maximum && std::abs(candidate.z) <= maximum)) return;
		const Vec3f impulse(candidate);
		if (impulse == Vec3f(0.0f)) return;
		// Check the actual float response transactionally before changing either body.
		const Vec3f nextA = m_bodyA->m_AngularVelocity - inverseA * impulse;
		const Vec3f nextB = m_bodyB->m_AngularVelocity + inverseB * impulse;
		if (!Math::IsFinite(nextA) || !Math::IsFinite(nextB)) return;
		m_bodyA->ApplyImpulseAngular(-impulse);
		m_bodyB->ApplyImpulseAngular(impulse);
		m_RollingImpulse += glm::dvec2(glm::dot(u, glm::dvec3(impulse)), glm::dot(v, glm::dvec3(impulse)));
		GE_PHYSICS_PROFILE_ADD(rollingResistanceImpulseCount, 1);
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
