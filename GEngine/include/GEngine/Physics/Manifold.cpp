#include "gepch.h"
#include "Manifold.h"
#include "PhysicsBody.h"
#include <Core/Timer.h>

namespace GEngine
{
	void Manifold::AddContact(const contact_t& contact_old)
	{
		// Make sure the contact's BodyA and BodyB are of the correct order
		contact_t contact = contact_old;
		if (contact_old.m_BodyA != m_BodyA || contact_old.m_BodyB != m_BodyB) {
			contact.ptOnA_LocalSpace = contact_old.ptOnB_LocalSpace;
			contact.ptOnB_LocalSpace = contact_old.ptOnA_LocalSpace;
			contact.ptOnA_WorldSpace = contact_old.ptOnB_WorldSpace;
			contact.ptOnB_WorldSpace = contact_old.ptOnA_WorldSpace;

			contact.m_BodyA = m_BodyA;
			contact.m_BodyB = m_BodyB;
		}

		// If this contact is close to another contact, then keep the old contact
		for (int i = 0; i < m_NumContacts; i++)
		{
			const RigidBody3D* bodyA = m_Contacts[i].m_BodyA;
			const RigidBody3D* bodyB = m_Contacts[i].m_BodyB;

			const Vec3f oldA = bodyA->BodySpaceToWorldSpace(m_Contacts[i].ptOnA_LocalSpace);
			const Vec3f oldB = bodyB->BodySpaceToWorldSpace(m_Contacts[i].ptOnB_LocalSpace);

			const Vec3f newA = contact.m_BodyA->BodySpaceToWorldSpace(contact.ptOnA_LocalSpace);
			const Vec3f newB = contact.m_BodyB->BodySpaceToWorldSpace(contact.ptOnB_LocalSpace);

			const Vec3f aa = newA - oldA;
			const Vec3f bb = newB - oldB;

			const float distanceThreshold = 0.02f;
			if (glm::length2(aa) < distanceThreshold * distanceThreshold)
			{
				return;
			}
			if (glm::length2(bb) < distanceThreshold * distanceThreshold)
			{
				return;
			}
		}

		// If we're all full on contacts, then keep the contacts that are furthest away from each other
		int newSlot = m_NumContacts;
		if (newSlot >= MAX_CONTACTS) {
			Vec3f avg = Vec3f(0, 0, 0);
			avg += m_Contacts[0].ptOnA_LocalSpace;
			avg += m_Contacts[1].ptOnA_LocalSpace;
			avg += m_Contacts[2].ptOnA_LocalSpace;
			avg += m_Contacts[3].ptOnA_LocalSpace;
			avg += contact.ptOnA_LocalSpace;
			avg *= 0.2f;

			float minDist = glm::length2(avg - contact.ptOnA_LocalSpace);
			int newIdx = -1;
			for (int i = 0; i < MAX_CONTACTS; i++) {
				float dist2 = glm::length2(avg - m_Contacts[i].ptOnA_LocalSpace);

				if (dist2 < minDist) {
					minDist = dist2;
					newIdx = i;
				}
			}

			if (-1 != newIdx) {
				newSlot = newIdx;
			}
			else {
				return;
			}
		}

		m_Contacts[newSlot] = contact;

		m_Constraints[newSlot].m_bodyA = contact.m_BodyA;
		m_Constraints[newSlot].m_bodyB = contact.m_BodyB;
		m_Constraints[newSlot].m_anchorA = contact.ptOnA_LocalSpace;
		m_Constraints[newSlot].m_anchorB = contact.ptOnB_LocalSpace;

		// Get the normal in BodyA's space
		Vec3f normal = glm::transpose(glm::toMat3(glm::inverse(m_BodyA->m_Orientation))) * (contact.normal * -1.0f);
	
		//if(glm::length(normal) >= 0.000001f)
		m_Constraints[newSlot].m_Normal = glm::normalize(normal);
		//else
			//m_Constraints[newSlot].m_Normal = normal;

		m_Constraints[newSlot].m_CachedLambda.Zero();

		if (newSlot == m_NumContacts) {
			m_NumContacts++;
		}
	}

	void Manifold::RemoveExpiredContacts()
	{
		// remove any contacts that have drifted too far
		for (int i = 0; i < m_NumContacts; i++)
		{
			contact_t& contact = m_Contacts[i];

			RigidBody3D* bodyA = contact.m_BodyA;
			RigidBody3D* bodyB = contact.m_BodyB;

			// Get the tangential distance of the point on A and the point on B
			const Vec3f a = bodyA->BodySpaceToWorldSpace(contact.ptOnA_LocalSpace);
			const Vec3f b = bodyB->BodySpaceToWorldSpace(contact.ptOnB_LocalSpace);

			Vec3f normal = glm::transpose(glm::toMat3(bodyA->m_Orientation)) * m_Constraints[i].m_Normal;

			// Calculate the tangential separation and penetration depth
			const Vec3f ab = b - a;
			float penetrationDepth = glm::dot(normal, ab);
			Vec3f abNormal = normal * penetrationDepth;
			Vec3f abTangent = ab - abNormal;

			// If the tangential displacement is less than a specific threshold, it's okay to keep it
			const float distanceThreshold = 0.02f;
			if (glm::length2(abTangent) < distanceThreshold * distanceThreshold && penetrationDepth <= 0.0f) {
				continue;
			}

			// This contact has moved beyond its threshold and should be removed
			for (int j = i; j < MAX_CONTACTS - 1; j++) {
				m_Constraints[j] = m_Constraints[j + 1];
				m_Contacts[j] = m_Contacts[j + 1];
				if (j >= m_NumContacts) {
					m_Constraints[j].m_CachedLambda.Zero();
				}
			}
			m_NumContacts--;
			i--;
		}
	}


	void Manifold::PreSolve(const float dt_sec)
	{
		for (int i = 0; i < m_NumContacts; i++)
		{
			m_Constraints[i].PreSolve(dt_sec);
		}
	}

	void Manifold::Solve()
	{
		for (int i = 0; i < m_NumContacts; i++)
		{	
			m_Constraints[i].Solve();
		}

	}
	void Manifold::PostSolve()
	{
		for (int i = 0; i < m_NumContacts; i++)
		{
			m_Constraints[i].PostSolve();
		}
	}

	contact_t Manifold::GetContact(const int idx)
	{
		return m_Contacts[idx]; 
	}

	void ManifoldCollector::AddContact(const contact_t& contact)
	{
		// Try to find the previously existing manifold for contacts between these two bodies
		int foundIdx = -1;
		int size = m_Manifolds.size();
		for (int i = 0; i < size; i++) {
			const Manifold& manifold = m_Manifolds[i];
			bool hasA = (manifold.m_BodyA == contact.m_BodyA || manifold.m_BodyB == contact.m_BodyA);
			bool hasB = (manifold.m_BodyA == contact.m_BodyB || manifold.m_BodyB == contact.m_BodyB);
			if (hasA && hasB) {
				foundIdx = i;
				break;
			}
		}

		// Add contact to manifolds
		if (foundIdx >= 0) {
			m_Manifolds[foundIdx].AddContact(contact);
		}
		else {
			Manifold manifold;
			manifold.m_BodyA = contact.m_BodyA;
			manifold.m_BodyB = contact.m_BodyB;

			manifold.AddContact(contact);
			m_Manifolds.push_back(manifold);
		}
	}

	void ManifoldCollector::PreSolve(const float dt_sec)
	{
		for (int i = 0; i < m_Manifolds.size(); i++)
		{
			m_Manifolds[i].PreSolve(dt_sec);
		}
	}

	void ManifoldCollector::Solve()
	{
		
		for (int i = 0; i < m_Manifolds.size(); i++) {
			m_Manifolds[i].Solve();
		}
	}

	void ManifoldCollector::PostSolve()
	{
		for (int i = 0; i < m_Manifolds.size(); i++) {
			m_Manifolds[i].PostSolve();
		}
	}

	void ManifoldCollector::RemoveExpired()
	{
		int size = m_Manifolds.size();
		// Remove expired manifolds
		for (int i = size - 1; i >= 0; i--) {
			Manifold& manifold = m_Manifolds[i];
			m_Manifolds[i].RemoveExpiredContacts();

			if (0 == manifold.m_NumContacts) {
				m_Manifolds.erase(m_Manifolds.begin() + i);
			}
		}
	}

}
