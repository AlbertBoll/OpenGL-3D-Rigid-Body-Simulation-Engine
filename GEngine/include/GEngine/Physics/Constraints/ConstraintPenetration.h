#pragma once
#include "Constraint.h"

namespace GEngine
{
	class ConstraintPenetration :public Constraint
	{
	public:
		ConstraintPenetration() : Constraint()
		{
			m_CachedLambda.Zero();
			m_Friction = 0.0f;
			m_Jacobian.Zero();
		}

		void PreSolve(const float dt_sec) override;
		void Solve() override;
		// Called after the manifold has solved its coupled normal rows.
		void SolveFriction();
		// Accumulated only within the current step; no cross-step torsional warm start.
		double GetSpinImpulse() const { return m_SpinImpulse; }
		// Coordinates in the deterministic world-normal tangent basis, reset each step.
		glm::dvec2 GetRollingImpulse() const { return m_RollingImpulse; }
		// One bounded translation pass, after physical integration. Never changes velocities.
		void PostSolve() override;
		

		Vec3f m_Normal{};
		float m_Friction;
		Mat<3, 12> m_Jacobian{};
		Vec<3> m_CachedLambda{};

	private:
		void Solve(bool solveNormal);
		bool m_PositionCorrectionEnabled{ false };
		void SolveSpin();
		double m_SpinImpulse{};
		float m_SpinResistanceLength{};
		void SolveRolling();
		glm::dvec2 m_RollingImpulse{};
		float m_RollingResistanceLength{};

	};

}

