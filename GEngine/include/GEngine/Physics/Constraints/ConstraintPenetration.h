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
		// One bounded translation pass, after physical integration. Never changes velocities.
		void PostSolve() override;
		

		Vec3f m_Normal{};
		float m_Friction;
		Mat<3, 12> m_Jacobian{};
		Vec<3> m_CachedLambda{};

	private:
		bool m_PositionCorrectionEnabled{ false };

	};

}

