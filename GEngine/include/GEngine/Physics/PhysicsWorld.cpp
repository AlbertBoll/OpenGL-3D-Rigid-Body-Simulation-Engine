#include "gepch.h"
#include "PhysicsWorld.h"

namespace GEngine
{
	PhysicsWorld::~PhysicsWorld()
	{
		for (auto& ele : m_RigidBodies)
		{
			delete ele;
		}

		m_RigidBodies.clear();

	}

	void PhysicsWorld::ConstructWorld()
	{

	}



	PhysicsWorld::PhysicsWorld(const Vec3f& gravity): m_Gravity(gravity)
	{
		for (auto& ele : m_RigidBodies) delete ele;
		m_RigidBodies.clear();
	}

	RigidBody3D* PhysicsWorld::CreateRigidBody3D()
	{

		//auto rigidBody3D = CreateScopedPtr<RigidBody3D>;
		m_RigidBodies.emplace_back(new RigidBody3D());
		return m_RigidBodies.back();
	}

	void PhysicsWorld::RemoveRigidBody3D(RigidBody3D* body)
	{
		m_RigidBodies.erase(std::remove(m_RigidBodies.begin(), m_RigidBodies.end(), body), m_RigidBodies.end());
		delete body;
		body = nullptr;
	}

}