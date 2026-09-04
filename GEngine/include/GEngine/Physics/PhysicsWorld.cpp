#include "gepch.h"
#include "PhysicsWorld.h"

#include <atomic>

namespace GEngine
{
	namespace
	{
		std::atomic<std::uint64_t> s_NextBodyGeneration{ 1 };

		std::uint64_t AcquireBodyGeneration()
		{
			return s_NextBodyGeneration.fetch_add(1, std::memory_order_relaxed);
		}
	}

	PhysicsWorld::~PhysicsWorld()
	{
		for (auto& ele : m_RigidBodies)
		{
			if (ele)
			{
				ele->m_Identity = {};
			}
			delete ele;
		}

		m_RigidBodies.clear();
		m_BodyIdentitySlots.clear();
		m_FreeBodyIdentitySlots.clear();

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
		auto* body = new RigidBody3D();
		std::uint64_t slotIndex{};
		if (m_FreeBodyIdentitySlots.empty())
		{
			slotIndex = static_cast<std::uint64_t>(m_BodyIdentitySlots.size());
			m_BodyIdentitySlots.emplace_back();
		}
		else
		{
			slotIndex = m_FreeBodyIdentitySlots.back();
			m_FreeBodyIdentitySlots.pop_back();
		}

		BodyIdentitySlot& identitySlot = m_BodyIdentitySlots[slotIndex];
		identitySlot.body = body;
		identitySlot.generation = AcquireBodyGeneration();
		body->m_Identity = RigidBodyIdentity(slotIndex + 1, identitySlot.generation);

		m_RigidBodies.emplace_back(body);
		return body;
	}

	bool PhysicsWorld::IsBodyIdentityValid(RigidBodyIdentity identity) const
	{
		if (!identity.IsValid())
		{
			return false;
		}

		const std::uint64_t slotIndex = identity.GetSlot() - 1;
		return slotIndex < m_BodyIdentitySlots.size() &&
			m_BodyIdentitySlots[slotIndex].body != nullptr &&
			m_BodyIdentitySlots[slotIndex].generation == identity.GetGeneration();
	}

	void PhysicsWorld::ReleaseBodyIdentity(RigidBody3D* body)
	{
		if (!body || !IsBodyIdentityValid(body->m_Identity))
		{
			return;
		}

		const std::uint64_t slotIndex = body->m_Identity.GetSlot() - 1;
		m_BodyIdentitySlots[slotIndex].body = nullptr;
		m_FreeBodyIdentitySlots.push_back(slotIndex);
		body->m_Identity = {};
	}

	void PhysicsWorld::RemoveRigidBody3D(RigidBody3D* body)
	{
		if (m_BodyRemovalCallback)
		{
			m_BodyRemovalCallback(body);
		}

		m_RigidBodies.erase(std::remove(m_RigidBodies.begin(), m_RigidBodies.end(), body), m_RigidBodies.end());
		ReleaseBodyIdentity(body);
		delete body;
		body = nullptr;
	}

}
