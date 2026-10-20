#pragma once

#include "Contact.h"
#include "PhysicsBody.h"
#include "Constraints/ConstraintPenetration.h"
#include <span>


namespace GEngine
{
	class RigidBody3D;
	struct collisionPair_t;
#ifdef GE_ENABLE_COUPLED_SOLVER_REPLAY
    namespace PhysicsDiagnostic { struct CoupledSolverReplayAccess; }
#endif

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
		// Cached values permit graph construction without following contact body pointers.
		RigidBodyIdentity GetBodyAIdentity() const { return m_StampA.identity; }
		RigidBodyIdentity GetBodyBIdentity() const { return m_StampB.identity; }

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
		void RefreshContacts(const contact_t* contacts, int count, bool refreshCandidates = false);
		bool RemoveExpiredContacts(bool keepContinuingCandidates);
		contact_t m_Contacts[MAX_CONTACTS]{};

		int m_NumContacts;

		RigidBody3D* m_BodyA{};
		RigidBody3D* m_BodyB{};

		ConstraintPenetration m_Constraints[MAX_CONTACTS];

		friend class ManifoldCollector;
#ifdef GE_ENABLE_COUPLED_SOLVER_REPLAY
        friend struct PhysicsDiagnostic::CoupledSolverReplayAccess;
#endif
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
		// One positive step: keep labelled positive-gap candidates only inside a
		// pair with a strict survivor. Finalize every pair before PreSolve.
		// Returns whether any such candidate remains; no-donor steps need no
		// extra cleanup or patch-matching route.
		bool PrepareContactRefresh();
		void RemoveExpiredForPair(const RigidBody3D* a, const RigidBody3D* b);
		// Prepared at these same poses/configuration; before fresh admission.
		// Only candidate-bearing manifolds need a broadphase membership scan.
		void RemoveExpiredOutsidePairs(std::span<const collisionPair_t> pairs,
			std::span<RigidBody3D* const> bodies);
		// Requires PrepareContactRefresh at the current poses. Invalid input
		// strictly expires the specified pair; only a complete patch may donate.
		bool RefreshContactPatch(RigidBody3D* a, RigidBody3D* b,
			const contact_t* contacts, int count);
		void Clear() { m_Manifolds.clear(); }	// For resetting
		int GetContactCount() const;
	private:
		void AddContacts(const contact_t* contacts, int count, bool refreshCandidates);
		bool RemoveExpired(bool keepContinuingCandidates);

	public:
		std::vector<Manifold> m_Manifolds;
	};

}

