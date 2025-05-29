#pragma once

#include "Contact.h"
#include "Constraints/ConstraintPenetration.h"


namespace GEngine
{
	class RigidBody3D;

	class Manifold
	{
	public:
		Manifold() : m_BodyA(nullptr), m_BodyB(nullptr), m_NumContacts(0) {}

		void AddContact(const contact_t& contact);
		void RemoveExpiredContacts();

		void PreSolve(const float dt_sec);
		void Solve();
		void PostSolve();

		contact_t GetContact(const int idx);
		int GetNumContacts() const { return m_NumContacts; }

	private:
		static constexpr int MAX_CONTACTS = 4;
		contact_t m_Contacts[MAX_CONTACTS];

		int m_NumContacts;

		RigidBody3D* m_BodyA{};
		RigidBody3D* m_BodyB{};

		ConstraintPenetration m_Constraints[MAX_CONTACTS];

		friend class ManifoldCollector;
	};

	/*
================================
ManifoldCollector
================================
*/
	class ManifoldCollector {
	public:
		ManifoldCollector() {}

		void AddContact(const contact_t& contact);

		void PreSolve(const float dt_sec);
		void Solve();
		void PostSolve();

		void RemoveExpired();
		void Clear() { m_Manifolds.clear(); }	// For resetting

	public:
		std::vector<Manifold> m_Manifolds;
	};

}

