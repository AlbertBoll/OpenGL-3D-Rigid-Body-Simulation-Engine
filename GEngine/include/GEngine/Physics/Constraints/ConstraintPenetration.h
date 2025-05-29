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
			m_Baumgarte = 0.0f;
			m_Friction = 0.0f;
			m_Jacobian.Zero();
		}

		void PreSolve(const float dt_sec) override;
		void Solve() override;
		

		Vec3f m_Normal{};
		float m_Baumgarte;
		float m_Friction;
		Mat<3, 12> m_Jacobian{};
		Vec<3> m_CachedLambda{};

	};

}

