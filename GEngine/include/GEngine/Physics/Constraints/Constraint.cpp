#include "gepch.h"
#include "Constraint.h"

#include "../PhysicsBody.h"

namespace GEngine
{



	Mat4 Constraint::Left(const Quat& q)
	{
		Mat4 L;
		L[0] = Vec4f(q.w, -q.x, -q.y, -q.z);
		L[1] = Vec4f(q.x, q.w, -q.z, q.y);
		L[2] = Vec4f(q.y, q.z, q.w, -q.x);
		L[3] = Vec4f(q.z, -q.y, q.x, q.w);

		return L;

	}

	Mat4 Constraint::Right(const Quat& q)
	{
		Mat4 R;
		R[0] = Vec4f(q.w, -q.x, -q.y, -q.z);
		R[1] = Vec4f(q.x, q.w, q.z, -q.y);
		R[2] = Vec4f(q.y, -q.z, q.w, q.x);
		R[3] = Vec4f(q.z, q.y, -q.x, q.w);
		return R;
	}

	
	Mat<12, 12> Constraint::GetInverseMassMatrix() const
	{
		Mat<12, 12> invMassMatrix{};
		invMassMatrix.Zero();

		invMassMatrix[0][0] = m_bodyA->m_InvMass;
		invMassMatrix[1][1] = m_bodyA->m_InvMass;
		invMassMatrix[2][2] = m_bodyA->m_InvMass;

		Mat3 invInertiaA = m_bodyA->GetInverseInertiaTensorWorldSpace();
		for (int i = 0; i < 3; i++)
		{
			/*invMassMatrix[3 + i][3 + 0] = invInertiaA[i][0];
			invMassMatrix[3 + i][3 + 1] = invInertiaA[i][1];
			invMassMatrix[3 + i][3 + 2] = invInertiaA[i][2];*/
			invMassMatrix[3 + i][3 + 0] = invInertiaA[0][i];
			invMassMatrix[3 + i][3 + 1] = invInertiaA[1][i];
			invMassMatrix[3 + i][3 + 2] = invInertiaA[2][i];
		}

		invMassMatrix[6][6] = m_bodyB->m_InvMass;
		invMassMatrix[7][7] = m_bodyB->m_InvMass;
		invMassMatrix[8][8] = m_bodyB->m_InvMass;

		Mat3 invInertiaB = m_bodyB->GetInverseInertiaTensorWorldSpace();
		for (int i = 0; i < 3; i++) {
			/*invMassMatrix[9 + i][9 + 0] = invInertiaB[i][0];
			invMassMatrix[9 + i][9 + 1] = invInertiaB[i][1];
			invMassMatrix[9 + i][9 + 2] = invInertiaB[i][2];*/
			invMassMatrix[9 + i][9 + 0] = invInertiaB[0][i];
			invMassMatrix[9 + i][9 + 1] = invInertiaB[1][i];
			invMassMatrix[9 + i][9 + 2] = invInertiaB[2][i];
		}

		return invMassMatrix;


	}

	Vec<12> Constraint::GetVelocities() const
	{
		Vec<12> q_dt{};
		q_dt[0] = m_bodyA->m_LinearVelocity.x;
		q_dt[1] = m_bodyA->m_LinearVelocity.y;
		q_dt[2] = m_bodyA->m_LinearVelocity.z;

		q_dt[3] = m_bodyA->m_AngularVelocity.x;
		q_dt[4] = m_bodyA->m_AngularVelocity.y;
		q_dt[5] = m_bodyA->m_AngularVelocity.z;

		q_dt[6] = m_bodyB->m_LinearVelocity.x;
		q_dt[7] = m_bodyB->m_LinearVelocity.y;
		q_dt[8] = m_bodyB->m_LinearVelocity.z;

		q_dt[9] = m_bodyB->m_AngularVelocity.x;
		q_dt[10] = m_bodyB->m_AngularVelocity.y;
		q_dt[11] = m_bodyB->m_AngularVelocity.z;

		return q_dt;

	}

	template<typename T>
	void Constraint::ApplyImpulses(const T& impulses)
	{
		Vec3f forceInternalA(0.0f);
		Vec3f torqueInternalA(0.0f);
		Vec3f forceInternalB(0.0f);
		Vec3f torqueInternalB(0.0f);

		forceInternalA[0] = impulses[0];
		forceInternalA[1] = impulses[1];
		forceInternalA[2] = impulses[2];

		torqueInternalA[0] = impulses[3];
		torqueInternalA[1] = impulses[4];
		torqueInternalA[2] = impulses[5];

		forceInternalB[0] = impulses[6];
		forceInternalB[1] = impulses[7];
		forceInternalB[2] = impulses[8];

		torqueInternalB[0] = impulses[9];
		torqueInternalB[1] = impulses[10];
		torqueInternalB[2] = impulses[11];

		m_bodyA->ApplyImpulseLinear(forceInternalA);
		m_bodyA->ApplyImpulseAngular(torqueInternalA);

		m_bodyB->ApplyImpulseLinear(forceInternalB);
		m_bodyB->ApplyImpulseAngular(torqueInternalB);
	}

	template void Constraint::ApplyImpulses(const Vec<12>&);



}
