#pragma once

#include "Core/System.h"
#include <Math/Math.h>
#include "Manifold.h"
#include "Contact.h"
#include "Broadphase.h"



namespace GEngine
{
	class RigidBody3D;

	//using namespace Math;

	//struct contact_t {
	//	Vec3f ptOnA_WorldSpace;
	//	Vec3f ptOnB_WorldSpace;
	//	Vec3f ptOnA_LocalSpace;
	//	Vec3f ptOnB_LocalSpace;

	//	Vec3f normal;	// In World Space coordinates
	//	float separationDistance;	// positive when non-penetrating, negative when penetrating
	//	float timeOfImpact;

	//	RigidBody3D* bodyA{};
	//	RigidBody3D* bodyB{};
	//};

	class PhysicsWorld;

	class PhysicsSystem : public System
	{
		// Inherited via System
	public:
		virtual ~PhysicsSystem();
		void Initialize() override;
		void Update(Timestep ts) override;
		void OnExit() override;
		void SetPhysicsWorld(PhysicsWorld* physics_world);
		PhysicsWorld* GetPhysicsWorld() { return m_PhysicsWorld; }

		static constexpr int MinSolverIterations = 1;
		static constexpr int MaxSolverIterations = 32;
		static constexpr int DefaultSolverIterations = 1;

		// Per-system policy, retained across world replacement. Invalid requests leave it unchanged.
		// Changes take effect on the next Update; configuration and stepping are single-threaded.
		bool SetSolverIterations(int iterations) noexcept
		{
			if (iterations < MinSolverIterations || iterations > MaxSolverIterations) return false;
			m_SolverIterations = iterations;
			return true;
		}
		int GetSolverIterations() const noexcept { return m_SolverIterations; }

		

	private:
		int m_SolverIterations{ DefaultSolverIterations };
		PhysicsWorld* m_PhysicsWorld{};
		ManifoldCollector m_Manifolds;
		SweepAndPruneBroadphase m_Broadphase;
		std::vector<collisionPair_t> m_CollisionPairs;
		std::vector<contact_t> m_Contacts;

	};

	

	namespace Collision
	{
		bool SphereSphereIntersect(RigidBody3D* bodyA, RigidBody3D* bodyB, const float dt, contact_t& contact);
		bool Intersect(RigidBody3D* bodyA, RigidBody3D* bodyB, contact_t& contact);
		bool Intersect(RigidBody3D* bodyA, RigidBody3D* bodyB, const float dt, contact_t& contact);
		void ResolveContact(contact_t& contact);
		bool ConservativeAdvance(RigidBody3D* bodyA, RigidBody3D* bodyB, float dt, contact_t& contact);
	}

}
