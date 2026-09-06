#pragma once

#include "Contact.h"
#include "PhysicsBody.h"
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
		struct BodyStamp
		{
			RigidBodyIdentity identity;
			const PhysicalShape* shape{};
			std::uint64_t shapeRevision{};
		};
		BodyStamp m_StampA, m_StampB;
		Vec3f m_NormalB[MAX_CONTACTS]{}; // Solver A -> B axis in B's local frame.
		bool CacheCompatible() const;
		void CaptureCompatibility();
		void RemoveContact(int slot);
		void WriteContact(int slot, const contact_t& contact, const ConstraintPenetration* previous);
		void RefreshContacts(const contact_t* contacts, int count);
		contact_t m_Contacts[MAX_CONTACTS]{};

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
		// A complete geometric patch (2-4 points): match old points once, retire absent points.
		void AddContacts(const contact_t* contacts, int count);

		void PreSolve(const float dt_sec);
		void Solve();
		void PostSolve();

		void RemoveExpired();
		void Clear() { m_Manifolds.clear(); }	// For resetting
		int GetContactCount() const;

	public:
		std::vector<Manifold> m_Manifolds;
	};

}

