#pragma once
#include <glm/ext/vector_float3.hpp>
#include <Math/Math.h>
#include <vector>
#include "PhysicsBody.h"



namespace GEngine
{
	using namespace Math;
	class PhysicsWorld
	{
	public:
		~PhysicsWorld();
		void ConstructWorld();
		PhysicsWorld() = default;
		PhysicsWorld(const Vec3f& gravity);
		RigidBody3D* CreateRigidBody3D();
		void RemoveRigidBody3D(RigidBody3D* body);
		void SetGravity(const Vec3f& gravity) { m_Gravity = gravity; }

		auto& GetPhysicsBodies() 
		{
			return m_RigidBodies;
		}

		auto GetGravity()const { return m_Gravity; }

	private:
		Vec3f m_Gravity{ 0.f, -12.f, 0.f };
		std::vector<RigidBody3D*> m_RigidBodies;
	};

}