#pragma once
#include <Math/Math.h>


namespace GEngine
{
	class RigidBody3D;

	using namespace Math;
	class Constraint
	{
	public:
		virtual void PreSolve(const float dt_sec) {}
		virtual void Solve() {}
		virtual void PostSolve() {}

		static Mat4 Left(const Quat& q);
		static Mat4 Right(const Quat& q);

	protected:
		Mat<12, 12> GetInverseMassMatrix() const;
		//template<typename T>
		Vec<12> GetVelocities() const;
		template<typename T>
		void ApplyImpulses(const T& impulses);

	public:
		RigidBody3D* m_bodyA{};
		RigidBody3D* m_bodyB{};

		Vec3f m_anchorA;		// The anchor location in bodyA's space
		Vec3f m_axisA;		// The axis direction in bodyA's space

		Vec3f m_anchorB;		// The anchor location in bodyB's space
		Vec3f m_axisB;		// The axis direction in bodyB's space
	};

	

}