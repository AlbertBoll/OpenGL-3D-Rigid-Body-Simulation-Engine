#include "gepch.h"
#include "Manifold.h"
#include "PhysicsProfile.h"
#include "PhysicsBody.h"
#include "Shape.h"
#include "Broadphase.h"
#include <Core/Timer.h>
#include <cmath>
#include <limits>

namespace GEngine
{

	namespace
	{
		bool HasActiveResponse(const RigidBody3D* a, const RigidBody3D* b)
		{
			return (a && a->GetInverseMass() > 0.0f && !a->IsSleeping()) ||
				(b && b->GetInverseMass() > 0.0f && !b->IsSleeping());
		}

		constexpr double AnchorDistance2 = 0.02 * 0.02;
		// Five degrees bounds both inter-body normal drift and new-query warm-start reuse.
		constexpr float NormalCoherence = 0.9961947f;

		bool ValidContact(const contact_t& contact)
		{
			return contact.m_BodyA && contact.m_BodyB && contact.m_BodyA != contact.m_BodyB &&
				contact.m_BodyA->m_Shape && contact.m_BodyB->m_Shape &&
				contact.m_BodyA->m_Shape->IsValid() && contact.m_BodyB->m_Shape->IsValid() &&
				Math::IsFinite(contact.ptOnA_LocalSpace) && Math::IsFinite(contact.ptOnB_LocalSpace) &&
				Math::IsFinite(contact.ptOnA_WorldSpace) && Math::IsFinite(contact.ptOnB_WorldSpace) &&
				Math::IsFinite(contact.normal) && Math::IsFinite(glm::length2(contact.normal)) &&
				glm::length2(contact.normal) > Math::NumericalEpsilon * Math::NumericalEpsilon &&
				Math::IsFinite(contact.separationDistance) && contact.timeOfImpact == 0.0f &&
				(contact.featureA == 0) == (contact.featureB == 0);
		}

		contact_t CanonicalContact(contact_t contact, const RigidBody3D* bodyA)
		{
			if (contact.m_BodyA != bodyA) {
				std::swap(contact.m_BodyA, contact.m_BodyB);
				std::swap(contact.ptOnA_LocalSpace, contact.ptOnB_LocalSpace);
				std::swap(contact.ptOnA_WorldSpace, contact.ptOnB_WorldSpace);
				std::swap(contact.featureA, contact.featureB);
				contact.normal = -contact.normal;
			}
			contact.normal = glm::normalize(contact.normal);
			return contact;
		}

		bool SameFeatures(const contact_t& a, const contact_t& b)
		{
			return a.featureA == b.featureA && a.featureB == b.featureB;
		}

		double AnchorDistance(const contact_t& a, const contact_t& b)
		{
			// Body-space distances are rigid-transform invariant. Both anchors must agree.
			return std::max(glm::length2(glm::dvec3(a.ptOnA_LocalSpace) - glm::dvec3(b.ptOnA_LocalSpace)),
				glm::length2(glm::dvec3(a.ptOnB_LocalSpace) - glm::dvec3(b.ptOnB_LocalSpace)));
		}
	}

	bool Manifold::CacheCompatible() const
	{
		const auto matches = [](const RigidBody3D* body, const BodyStamp& stamp) {
			return body && body->GetIdentity() == stamp.identity && body->m_Shape &&
				body->m_Shape == stamp.shape && body->m_Shape->IsValid() &&
				body->m_Shape->GetRevision() == stamp.shapeRevision;
		};
		return matches(m_BodyA, m_StampA) && matches(m_BodyB, m_StampB);
	}

	void Manifold::CaptureCompatibility()
	{
		m_StampA = { m_BodyA->GetIdentity(), m_BodyA->m_Shape, m_BodyA->m_Shape->GetRevision() };
		m_StampB = { m_BodyB->GetIdentity(), m_BodyB->m_Shape, m_BodyB->m_Shape->GetRevision() };
	}

	void Manifold::RemoveContact(int slot)
	{
        PhysicsDiagnostic::Observation diagnostic(PhysicsDiagnostic::Operation::ContactRemove,m_BodyA,m_BodyB);
        if (diagnostic) {
            diagnostic.value.slot=slot; diagnostic.value.previousSlot=slot;
            diagnostic.value.localA=m_Contacts[slot].ptOnA_LocalSpace; diagnostic.value.localB=m_Contacts[slot].ptOnB_LocalSpace;
            diagnostic.value.featureA=m_Contacts[slot].featureA; diagnostic.value.featureB=m_Contacts[slot].featureB;
            diagnostic.value.accumulatedBefore={m_Constraints[slot].m_CachedLambda[0],m_Constraints[slot].m_CachedLambda[1],m_Constraints[slot].m_CachedLambda[2]};
        }
		for (int j = slot; j + 1 < m_NumContacts; ++j) {
			m_Contacts[j] = m_Contacts[j + 1];
			m_Constraints[j] = m_Constraints[j + 1];
			m_NormalB[j] = m_NormalB[j + 1];
		}
		--m_NumContacts;
		m_Constraints[m_NumContacts].m_CachedLambda.Zero();
		m_Contacts[m_NumContacts] = {};
		m_NormalB[m_NumContacts] = Vec3f(0);
	}

	void Manifold::WriteContact(int slot, const contact_t& contact, const ConstraintPenetration* previous)
	{
        PhysicsDiagnostic::Observation diagnostic(previous ? PhysicsDiagnostic::Operation::ContactRefresh :
            PhysicsDiagnostic::Operation::ContactNew,m_BodyA,m_BodyB);
        if (diagnostic) {
            diagnostic.value.slot=slot;
            diagnostic.value.localA=contact.ptOnA_LocalSpace; diagnostic.value.localB=contact.ptOnB_LocalSpace;
            diagnostic.value.featureA=contact.featureA; diagnostic.value.featureB=contact.featureB;
            diagnostic.value.penetration=-contact.separationDistance;
            diagnostic.value.normal=-contact.normal;
            diagnostic.value.anchorA=contact.ptOnA_WorldSpace; diagnostic.value.anchorB=contact.ptOnB_WorldSpace;
            if (previous) diagnostic.value.accumulatedBefore={previous->m_CachedLambda[0],previous->m_CachedLambda[1],previous->m_CachedLambda[2]};
        }
		const Vec3f normal = Math::NormalizeOr(m_BodyA->GetWorldToBodyRotation() * -contact.normal);
		SolverMath::Vector<3> lambda;
		lambda.Zero();
		if (previous) {
			// Re-express the old impulse in the refreshed orthonormal contact basis.
			// Copy before writing: incremental refresh may read and write the same slot.
			Vec3f oldU, oldV, newU, newV;
			Math::GetOrtho(previous->m_Normal, oldU, oldV);
			Math::GetOrtho(normal, newU, newV);
			const Vec3f impulse = previous->m_Normal * previous->m_CachedLambda[0] +
				oldU * previous->m_CachedLambda[1] + oldV * previous->m_CachedLambda[2];
			if (Math::IsFinite(impulse)) {
				lambda[0] = std::max(0.0f, glm::dot(impulse, normal));
				lambda[1] = glm::dot(impulse, newU);
				lambda[2] = glm::dot(impulse, newV);
			}
		}
		m_Contacts[slot] = contact;
		auto& constraint = m_Constraints[slot];
		constraint.m_bodyA = m_BodyA;
		constraint.m_bodyB = m_BodyB;
		constraint.m_anchorA = contact.ptOnA_LocalSpace;
		constraint.m_anchorB = contact.ptOnB_LocalSpace;
		constraint.m_Normal = normal; // Contact B -> A becomes solver A -> B.
		constraint.m_CachedLambda = lambda; // PreSolve still applies the current Coulomb bound.
		m_NormalB[slot] = Math::NormalizeOr(m_BodyB->GetWorldToBodyRotation() * -contact.normal);
        if (diagnostic) diagnostic.value.accumulatedAfter={lambda[0],lambda[1],lambda[2]};
	}

	void Manifold::AddContact(const contact_t& incoming)
	{
		if (!ValidContact(incoming)) return;
		if (!m_BodyA && !m_BodyB) {
			m_BodyA = incoming.m_BodyA;
			m_BodyB = incoming.m_BodyB;
		}
		if (!((incoming.m_BodyA == m_BodyA && incoming.m_BodyB == m_BodyB) ||
			(incoming.m_BodyA == m_BodyB && incoming.m_BodyB == m_BodyA))) return;
		if (!CacheCompatible()) {
            PhysicsDiagnostic::Mark(PhysicsDiagnostic::Operation::PairReset,m_BodyA,m_BodyB,-1,-1,1);
			m_NumContacts = 0;
			CaptureCompatibility();
		}
		const contact_t contact = CanonicalContact(incoming, m_BodyA);
		// An obsolete normal cannot survive alongside the current pair's contact direction.
		for (int i = 0; i < m_NumContacts;) {
			const Vec3f oldNormal = m_BodyA->GetBodyToWorldRotation() * m_Constraints[i].m_Normal;
			const Vec3f normalB = m_BodyB->GetBodyToWorldRotation() * m_NormalB[i];
			if (!(glm::dot(oldNormal, -contact.normal) >= NormalCoherence) ||
				!(glm::dot(oldNormal, normalB) >= NormalCoherence)) RemoveContact(i);
			else ++i;
		}

		int match = -1;
		double bestDistance = AnchorDistance2;
		for (int i = 0; i < m_NumContacts; ++i) {
			const double distance = AnchorDistance(contact, m_Contacts[i]);
			// Distinct labelled features remain distinct even on boxes smaller than 0.02.
			if (contact.featureA && m_Contacts[i].featureA && !SameFeatures(contact, m_Contacts[i])) continue;
			if (distance < bestDistance) {
				match = i;
				bestDistance = distance;
			}
		}
		if (match >= 0) {
			WriteContact(match, contact, SameFeatures(contact, m_Contacts[match]) ? &m_Constraints[match] : nullptr);
			return;
		}

		// Retain the existing bounded spread reduction for incremental, single-witness input.
		int newSlot = m_NumContacts;
		if (newSlot >= MAX_CONTACTS) {
			Vec3f avg = contact.ptOnA_LocalSpace;
			for (int i = 0; i < MAX_CONTACTS; ++i) avg += m_Contacts[i].ptOnA_LocalSpace;
			avg *= 0.2f;
			float minDist = glm::length2(avg - contact.ptOnA_LocalSpace);
			newSlot = -1;
			for (int i = 0; i < MAX_CONTACTS; ++i) {
				const float distance = glm::length2(avg - m_Contacts[i].ptOnA_LocalSpace);
				if (distance < minDist) { minDist = distance; newSlot = i; }
			}
			if (newSlot < 0) return;
		}
		WriteContact(newSlot, contact, nullptr);
		if (newSlot == m_NumContacts) ++m_NumContacts;
	}


	void Manifold::RefreshContacts(const contact_t* contacts, int count, bool refreshCandidates)
	{
		Manifold refreshed;
		refreshed.m_BodyA = m_BodyA;
		refreshed.m_BodyB = m_BodyB;
		refreshed.CaptureCompatibility();
		const bool compatible = CacheCompatible();
		contact_t canonical[MAX_CONTACTS]{};
		int matches[MAX_CONTACTS]{ -1, -1, -1, -1 };
		bool used[MAX_CONTACTS]{};
		for (int point = 0; point < count; ++point) canonical[point] = CanonicalContact(contacts[point], m_BodyA);
		const auto candidate = [&](int slot) {
			return refreshCandidates && m_Contacts[slot].separationDistance > 0.0f;
		};

		// Exact topology has priority across the whole patch. At coincident box edges,
		// tiny sliding/rotation changes boundary bits without changing the supporting
		// face pair. Unmatched points may then use both nearby anchors on those same
		// labelled faces; a different face, normal, shape or body still starts cold.
		// Preserve both original survivor passes before considering an expired
		// point. Candidates can only warm an otherwise cold exact-feature point.
		for (int pass = 0; compatible && pass < (refreshCandidates ? 3 : 2); ++pass) {
			for (;;) {
				int bestPoint = -1, bestOld = -1;
				double bestDistance = AnchorDistance2;
				for (int point = 0; point < count; ++point) {
					if (matches[point] >= 0) continue;
					const auto& contact = canonical[point];
					for (int i = 0; i < m_NumContacts; ++i) {
						if (used[i]) continue;
						const auto& old = m_Contacts[i];
						if (pass < 2 && candidate(i)) continue;
						if (pass == 2 && (!candidate(i) || !old.featureA || !old.featureB)) continue;
						const bool featureMatch = pass != 1 ? SameFeatures(contact, old) :
							contact.featureA && contact.featureB && old.featureA && old.featureB &&
							(contact.featureA >> 4) == (old.featureA >> 4) &&
							(contact.featureB >> 4) == (old.featureB >> 4);
						if (!featureMatch) continue;
						const Vec3f oldNormal = m_BodyA->GetBodyToWorldRotation() * m_Constraints[i].m_Normal;
						const Vec3f normalB = m_BodyB->GetBodyToWorldRotation() * m_NormalB[i];
						if (!(glm::dot(oldNormal, -contact.normal) >= NormalCoherence) ||
							!(glm::dot(oldNormal, normalB) >= NormalCoherence)) continue;
						const double distance = AnchorDistance(contact, old);
						if (distance < bestDistance) {
							bestPoint = point; bestOld = i; bestDistance = distance;
						}
					}
				}
				if (bestPoint < 0) break;
				matches[bestPoint] = bestOld;
				used[bestOld] = true;
			}
		}

        PhysicsDiagnostic::Mark(PhysicsDiagnostic::Operation::PatchBegin,m_BodyA,m_BodyB,m_NumContacts,count,compatible?1:0);
		const auto append = [&](int point) {
			const int match = matches[point];
            PhysicsDiagnostic::Mark(PhysicsDiagnostic::Operation::PointMap,m_BodyA,m_BodyB,refreshed.m_NumContacts,match);
			refreshed.WriteContact(refreshed.m_NumContacts, canonical[point],
				match >= 0 ? &m_Constraints[match] : nullptr);
			++refreshed.m_NumContacts;
		};
		// Clipping can cyclically permute unchanged points when reference ownership
		// or boundary classification changes. Keep surviving constraints in their old
		// solve order, then append genuinely new points in deterministic input order.
		for (int i = 0; i < m_NumContacts; ++i) {
			if (candidate(i)) continue;
			for (int point = 0; point < count; ++point)
				if (matches[point] == i) append(point);
		}
		for (int point = 0; point < count; ++point)
			if (matches[point] < 0 || candidate(matches[point])) append(point);
		*this = refreshed;
        PhysicsDiagnostic::Mark(PhysicsDiagnostic::Operation::PatchEnd,m_BodyA,m_BodyB,m_NumContacts);
	}

	void Manifold::RemoveExpiredContacts()
	{
		RemoveExpiredContacts(false);
	}

	bool Manifold::RemoveExpiredContacts(bool keepContinuingCandidates)
	{
		if (!CacheCompatible()) { PhysicsDiagnostic::Mark(PhysicsDiagnostic::Operation::PairReset,m_BodyA,m_BodyB,-1,-1,2); m_NumContacts = 0; return false; }
		unsigned strict = 0, candidates = 0;
		const int oldCount = m_NumContacts;
		for (int i = 0; i < oldCount; ++i) {
			contact_t& contact = m_Contacts[i];
			const Vec3f a = m_BodyA->BodySpaceToWorldSpace(contact.ptOnA_LocalSpace);
			const Vec3f b = m_BodyB->BodySpaceToWorldSpace(contact.ptOnB_LocalSpace);
			const Vec3f normal = m_BodyA->GetBodyToWorldRotation() * m_Constraints[i].m_Normal;
			const Vec3f normalB = m_BodyB->GetBodyToWorldRotation() * m_NormalB[i];
			const Vec3f ab = b - a;
			const float separation = glm::dot(normal, ab);
			const Vec3f tangent = ab - normal * separation;
			if (Math::IsFinite(a) && Math::IsFinite(b) && Math::IsFinite(separation) &&
				glm::dot(normal, normalB) >= NormalCoherence &&
				glm::length2(tangent) < AnchorDistance2 &&
				(separation <= 0.0f || (keepContinuingCandidates && contact.featureA && contact.featureB))) {
				contact.ptOnA_WorldSpace = a;
				contact.ptOnB_WorldSpace = b;
				contact.normal = -normal;
				contact.separationDistance = separation;
				if (separation <= 0.0f) strict |= 1u << i;
				else candidates |= 1u << i;
			}
		}
		// A candidate never extends the pair lifetime or occupies new storage.
		const unsigned retained = strict | (strict ? candidates : 0u);
		for (int old = 0, slot = 0; old < oldCount; ++old) {
			if (retained & (1u << old)) ++slot;
			else RemoveContact(slot);
		}
		return strict && candidates;
	}


	void Manifold::PreSolve(const float dt_sec)
	{
		if (!CacheCompatible()) { PhysicsDiagnostic::Mark(PhysicsDiagnostic::Operation::PairReset,m_BodyA,m_BodyB,-1,-1,2); m_NumContacts = 0; return; }
		for (int i = 0; i < m_NumContacts;) {
			const Vec3f normalA = m_BodyA->GetBodyToWorldRotation() * m_Constraints[i].m_Normal;
			const Vec3f normalB = m_BodyB->GetBodyToWorldRotation() * m_NormalB[i];
			if (!(glm::dot(normalA, normalB) >= NormalCoherence)) RemoveContact(i);
			else {
                PhysicsDiagnostic::SetSlot(i);
				// Keep compatibility/normal validation for sleeping contact caches.
				if (HasActiveResponse(m_BodyA, m_BodyB)) m_Constraints[i].PreSolve(dt_sec);
				++i;
			}
		}
	}

	namespace
	{
		void PreferClosestNormalFamily(const double (&matrix)[4][5], const int (&pivotColumns)[4],
			int rank, int size, const int (&indices)[4], const double (&K)[4][4],
			const double (&rhs)[4], const double (&old)[4], int count, double tolerance, double (&x)[4])
		{
			// The original pinned-free-coordinate candidate remains the fallback.
			// For x = particular + N*z, minimize |x-old|^2 by solving
			// (N^T*N)*z = N^T*(old-particular). Each basis vector owns a distinct
			// unit free coordinate, so the Gram matrix is positive definite.
			if (rank == 0 || rank == size) return;
			int freeColumns[4]{}, freeCount = 0;
			for (int col = 0; col < size; ++col) {
				bool pivot = false;
				for (int row = 0; row < rank; ++row) pivot |= pivotColumns[row] == col;
				if (!pivot) freeColumns[freeCount++] = col;
			}
			double particular[4]{}, basis[4][4]{}, gram[4][5]{}, coefficients[4]{}, projected[4]{};
			for (int row = rank - 1; row >= 0; --row) {
				const int col = pivotColumns[row];
				double value = matrix[row][size];
				for (int j = col + 1; j < size; ++j) value -= matrix[row][j] * particular[j];
				particular[col] = value / matrix[row][col];
			}
			for (int axis = 0; axis < freeCount; ++axis) {
				basis[axis][freeColumns[axis]] = 1.0;
				for (int row = rank - 1; row >= 0; --row) {
					const int col = pivotColumns[row];
					double value = 0;
					for (int j = col + 1; j < size; ++j) value -= matrix[row][j] * basis[axis][j];
					basis[axis][col] = value / matrix[row][col];
				}
			}
			for (int row = 0; row < freeCount; ++row) {
				for (int col = 0; col < freeCount; ++col)
					for (int i = 0; i < size; ++i) gram[row][col] += basis[row][i] * basis[col][i];
				for (int i = 0; i < size; ++i)
					gram[row][freeCount] += basis[row][i] * (old[indices[i]] - particular[i]);
				for (int col = 0; col <= freeCount; ++col)
					if (!std::isfinite(gram[row][col])) return;
			}
			// No new rank tolerance: a nonfinite or unusable Gram solve leaves the
			// original candidate intact, and the original full-K guards still apply.
			for (int col = 0; col < freeCount; ++col) {
				int pivot = col;
				for (int row = col + 1; row < freeCount; ++row)
					if (std::abs(gram[row][col]) > std::abs(gram[pivot][col])) pivot = row;
				if (!(std::abs(gram[pivot][col]) > 0.0) || !std::isfinite(gram[pivot][col])) return;
				for (int j = col; j <= freeCount; ++j) std::swap(gram[pivot][j], gram[col][j]);
				for (int row = col + 1; row < freeCount; ++row) {
					const double factor = gram[row][col] / gram[col][col];
					for (int j = col; j <= freeCount; ++j) gram[row][j] -= factor * gram[col][j];
				}
			}
			for (int row = freeCount - 1; row >= 0; --row) {
				double value = gram[row][freeCount];
				for (int j = row + 1; j < freeCount; ++j) value -= gram[row][j] * coefficients[j];
				coefficients[row] = value / gram[row][row];
				if (!std::isfinite(coefficients[row])) return;
			}
			double projectedScore = 0, pinnedScore = 0, lambda[4]{};
			for (int i = 0; i < size; ++i) {
				projected[i] = particular[i];
				for (int axis = 0; axis < freeCount; ++axis) projected[i] += basis[axis][i] * coefficients[axis];
				if (!std::isfinite(projected[i]) || projected[i] < 0 ||
					projected[i] > std::numeric_limits<float>::max()) return;
				lambda[indices[i]] = projected[i];
				projectedScore += (projected[i] - old[indices[i]]) * (projected[i] - old[indices[i]]);
				pinnedScore += (x[i] - old[indices[i]]) * (x[i] - old[indices[i]]);
			}
			if (!(projectedScore < pinnedScore)) return;
			for (int i = 0; i < count; ++i) {
				double value = -rhs[i];
				for (int j = 0; j < count; ++j) value += K[i][j] * lambda[j];
				if (!std::isfinite(value) || value < -tolerance ||
					(lambda[i] > 0 && std::abs(value) > tolerance)) return;
			}
			for (int i = 0; i < size; ++i) x[i] = projected[i];
		}

		bool SolveNormalBlock(ConstraintPenetration *contacts, int count)
		{
			if (count < 2 || count > 4)
				return false;
			// Solve K * lambda - rhs >= 0, lambda >= 0, with complementary active rows.
			// At most four contacts means only sixteen active sets; no heap allocation.
			double K[4][4]{}, rhs[4]{}, old[4]{}, best[4]{}, bestScore = std::numeric_limits<double>::max();
			glm::dvec3 n[4], ra[4], rb[4], ja[4], jb[4];
			auto *A = contacts[0].m_bodyA;
			auto *B = contacts[0].m_bodyB;
			const glm::dmat3 IA(A->GetInverseInertiaTensorWorldSpace()), IB(B->GetInverseInertiaTensorWorldSpace());
			const double inverseMass = A->GetInverseMass() + B->GetInverseMass();
			double scale = 0, velocityScale = 1;
			for (int i = 0; i < count; ++i)
			{
				n[i] = A->GetBodyToWorldRotation() * contacts[i].m_Normal;
				ra[i] = glm::dvec3(A->BodySpaceToWorldSpace(contacts[i].m_anchorA)) -
						glm::dvec3(A->GetCenterOfMassWorldSpace());
				rb[i] = glm::dvec3(B->BodySpaceToWorldSpace(contacts[i].m_anchorB)) -
						glm::dvec3(B->GetCenterOfMassWorldSpace());
				ja[i] = glm::cross(ra[i], n[i]);
				jb[i] = glm::cross(rb[i], n[i]);
				old[i] = contacts[i].m_CachedLambda[0];
				const auto relative = glm::dvec3(B->GetLinearVelocity()) - glm::dvec3(A->GetLinearVelocity()) +
									  glm::cross(glm::dvec3(B->GetAngularVelocity()), rb[i]) -
									  glm::cross(glm::dvec3(A->GetAngularVelocity()), ra[i]);
				rhs[i] = -glm::dot(n[i], relative);
			}
			for (int i = 0; i < count; ++i)
				for (int j = 0; j < count; ++j)
				{
					K[i][j] =
						inverseMass * glm::dot(n[i], n[j]) + glm::dot(ja[i], IA * ja[j]) + glm::dot(jb[i], IB * jb[j]);
					if (!std::isfinite(K[i][j]))
						return false;
					rhs[i] += K[i][j] * old[j];
					scale = std::max(scale, std::abs(K[i][j]));
				}
			if (!(scale > 0))
				return false;
			for (int i = 0; i < count; ++i)
			{
				if (!std::isfinite(rhs[i]))
					return false;
				velocityScale = std::max(velocityScale, std::abs(rhs[i]));
			}
			// These tolerances validate a numerical solve; they add no velocity bias.
			const double tolerance = 1e-6 * velocityScale;
			for (int mask = 0; mask < (1 << count); ++mask)
			{
				int indices[4]{}, size = 0;
				for (int i = 0; i < count; ++i)
					if (mask & (1 << i))
						indices[size++] = i;
				double matrix[4][5]{}, x[4]{};
				int pivotColumns[4]{}, rank = 0;
				for (int i = 0; i < size; ++i)
				{
					x[i] = old[indices[i]];
					matrix[i][size] = rhs[indices[i]];
					for (int j = 0; j < size; ++j)
						matrix[i][j] = K[indices[i]][indices[j]];
				}
				// Four coplanar normals can have rank three. Keep warm values on free
				// columns and solve the independent rows, rather than invert a singular patch.
				for (int col = 0; col < size; ++col)
				{
					int pivot = rank;
					for (int row = rank + 1; row < size; ++row)
						if (std::abs(matrix[row][col]) > std::abs(matrix[pivot][col]))
							pivot = row;
					if (std::abs(matrix[pivot][col]) <= scale * 1e-9)
						continue;
					for (int j = 0; j <= size; ++j)
						std::swap(matrix[pivot][j], matrix[rank][j]);
					for (int row = rank + 1; row < size; ++row)
					{
						const double factor = matrix[row][col] / matrix[rank][col];
						for (int j = col; j <= size; ++j)
							matrix[row][j] -= factor * matrix[rank][j];
					}
					pivotColumns[rank++] = col;
				}
				bool valid = true;
				for (int row = rank; row < size; ++row)
					if (std::abs(matrix[row][size]) > tolerance)
						valid = false;
				if (!valid)
					continue;
				for (int row = rank - 1; row >= 0; --row)
				{
					int col = pivotColumns[row];
					double b = matrix[row][size];
					for (int j = col + 1; j < size; ++j)
						b -= matrix[row][j] * x[j];
					x[col] = b / matrix[row][col];
				}
				if (rank > 0 && rank < size)
					PreferClosestNormalFamily(matrix, pivotColumns, rank, size, indices, K, rhs, old, count, tolerance, x);
				double lambda[4]{}, score = 0;
				for (int i = 0; i < size; ++i)
				{
					if (!std::isfinite(x[i]) || x[i] < 0 || x[i] > std::numeric_limits<float>::max())
						valid = false;
					lambda[indices[i]] = x[i];
				}
				if (!valid)
					continue;
				for (int i = 0; i < count; ++i)
				{
					double v = -rhs[i];
					for (int j = 0; j < count; ++j)
						v += K[i][j] * lambda[j];
					if (v < -tolerance || (lambda[i] > 0 && std::abs(v) > tolerance))
						valid = false;
					score += (lambda[i] - old[i]) * (lambda[i] - old[i]);
				}
				// Prefer the feasible candidate nearest the previous normal support.
				if (valid && score < bestScore)
				{
					bestScore = score;
					for (int i = 0; i < count; ++i)
						best[i] = lambda[i];
				}
			}
			if (bestScore == std::numeric_limits<double>::max())
				return false;
			for (int i = 0; i < count; ++i)
			{
                PhysicsDiagnostic::Observation diagnostic(PhysicsDiagnostic::Operation::BlockNormal,A,B);
                if (diagnostic) {
                    diagnostic.value.slot=i;
                    diagnostic.value.localA=contacts[i].m_anchorA; diagnostic.value.localB=contacts[i].m_anchorB;
                    diagnostic.value.normal=Vec3f(n[i]);
                    diagnostic.value.anchorA=A->BodySpaceToWorldSpace(contacts[i].m_anchorA);
                    diagnostic.value.anchorB=B->BodySpaceToWorldSpace(contacts[i].m_anchorB);
                    diagnostic.value.accumulatedBefore[0]=contacts[i].m_CachedLambda[0];
                    diagnostic.value.accumulatedAfter[0]=static_cast<float>(best[i]);
                }
				float next = static_cast<float>(best[i]), delta = next - contacts[i].m_CachedLambda[0];
				contacts[i].m_CachedLambda[0] = next;
				const Vec3f impulse = Vec3f(n[i]) * delta;
                if (diagnostic) {
                    diagnostic.value.linearImpulseB=impulse; diagnostic.value.linearImpulseA=-impulse;
                    diagnostic.value.angularImpulseA=glm::cross(Vec3f(ra[i]),-impulse);
                    diagnostic.value.angularImpulseB=glm::cross(Vec3f(rb[i]),impulse);
                    diagnostic.value.appliedImpulsesKnown=true;
                }
				A->ApplyImpulse(A->BodySpaceToWorldSpace(contacts[i].m_anchorA), -impulse);
				B->ApplyImpulse(B->BodySpaceToWorldSpace(contacts[i].m_anchorB), impulse);
			}
			return true;
		}
	} // namespace

	void Manifold::Solve()
	{
		if (!HasActiveResponse(m_BodyA, m_BodyB)) return;
		// Balance force and torque over the whole support patch before friction.
		const bool blockSolved = SolveNormalBlock(m_Constraints, m_NumContacts);
		for (int i = 0; i < m_NumContacts; ++i) {
            PhysicsDiagnostic::SetSlot(i);
			if (blockSolved) m_Constraints[i].SolveFriction();
			else m_Constraints[i].Solve(); // Single witness or failed/unsupported block.
		}
	}
	void Manifold::PostSolve()
	{
		if (!HasActiveResponse(m_BodyA, m_BodyB)) return;
		for (int i = 0; i < m_NumContacts; i++)
		{
            PhysicsDiagnostic::SetSlot(i);
			m_Constraints[i].PostSolve();
		}
	}

	contact_t Manifold::GetContact(const int idx)
	{
		return m_Contacts[idx]; 
	}


	void ManifoldCollector::AddContact(const contact_t& contact)
	{
		AddContacts(&contact, 1);
	}

	void ManifoldCollector::AddContacts(const contact_t* contacts, int count)
	{
		AddContacts(contacts, count, false);
	}

	void ManifoldCollector::AddContacts(const contact_t* contacts, int count, bool refreshCandidates)
	{
		if (!contacts || count < 1 || count > 4) return;
		const auto& first = contacts[0];
		for (int i = 0; i < count; ++i) {
			const auto& contact = contacts[i];
			if (!ValidContact(contact) ||
				!((contact.m_BodyA == first.m_BodyA && contact.m_BodyB == first.m_BodyB) ||
					(contact.m_BodyA == first.m_BodyB && contact.m_BodyB == first.m_BodyA))) return;
		}
		// Keep linear lookup; stable body stamps validate any found cache before reuse.
		Manifold* found = nullptr;
		for (auto& manifold : m_Manifolds) {
			if ((manifold.m_BodyA == first.m_BodyA && manifold.m_BodyB == first.m_BodyB) ||
				(manifold.m_BodyA == first.m_BodyB && manifold.m_BodyB == first.m_BodyA)) {
				found = &manifold;
				break;
			}
		}
		if (!found) {
			m_Manifolds.emplace_back();
			found = &m_Manifolds.back();
			found->m_BodyA = first.m_BodyA;
			found->m_BodyB = first.m_BodyB;
            PhysicsDiagnostic::Mark(PhysicsDiagnostic::Operation::PairBegin,first.m_BodyA,first.m_BodyB);
		}
		if (count == 1) found->AddContact(first);
		else found->RefreshContacts(contacts, count, refreshCandidates);
	}

	bool ManifoldCollector::RefreshContactPatch(RigidBody3D* a, RigidBody3D* b,
		const contact_t* contacts, int count)
	{
		bool valid = a && b && a != b && contacts && count >= 2 && count <= 4;
		for (int i = 0; valid && i < count; ++i) {
			const auto& contact = contacts[i];
			valid = ValidContact(contact) &&
				((contact.m_BodyA == a && contact.m_BodyB == b) ||
				 (contact.m_BodyA == b && contact.m_BodyB == a));
		}
		if (!valid) {
			RemoveExpiredForPair(a, b);
			return false;
		}
		AddContacts(contacts, count, true);
		return true;
	}

	void ManifoldCollector::PreSolve(const float dt_sec)
	{
		for (int i = 0; i < m_Manifolds.size();) {
			m_Manifolds[i].PreSolve(dt_sec);
			// Invalidation can empty a manifold here, after the world's expiry stage.
			// Do not leave an empty pair holding pointers that body-removal skips.
			if (m_Manifolds[i].GetNumContacts() == 0) {
                PhysicsDiagnostic::Mark(PhysicsDiagnostic::Operation::PairEnd,m_Manifolds[i].m_BodyA,m_Manifolds[i].m_BodyB,-1,-1,1);
                m_Manifolds.erase(m_Manifolds.begin() + i);
            }
			else ++i;
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
		RemoveExpired(false);
	}

	bool ManifoldCollector::PrepareContactRefresh()
	{
		return RemoveExpired(true);
	}

	bool ManifoldCollector::RemoveExpired(bool keepContinuingCandidates)
	{
		// Remove expired manifolds
		bool candidates = false;
		for (auto it = m_Manifolds.end(); it != m_Manifolds.begin();) {
			--it;
			Manifold& manifold = *it;
			candidates |= manifold.RemoveExpiredContacts(keepContinuingCandidates);

			if (0 == manifold.m_NumContacts) {
                PhysicsDiagnostic::Mark(PhysicsDiagnostic::Operation::PairEnd,manifold.m_BodyA,manifold.m_BodyB,-1,-1,2);
				it = m_Manifolds.erase(it);
			}
		}
		return candidates;
	}

	void ManifoldCollector::RemoveExpiredForPair(const RigidBody3D* a, const RigidBody3D* b)
	{
		for (auto it = m_Manifolds.end(); it != m_Manifolds.begin();) {
			--it;
			if (!((it->m_BodyA == a && it->m_BodyB == b) ||
				(it->m_BodyA == b && it->m_BodyB == a))) continue;
			it->RemoveExpiredContacts();
			if (it->m_NumContacts == 0) {
				PhysicsDiagnostic::Mark(PhysicsDiagnostic::Operation::PairEnd,it->m_BodyA,it->m_BodyB,-1,-1,2);
				it = m_Manifolds.erase(it);
			}
		}
	}

	void ManifoldCollector::RemoveExpiredOutsidePairs(std::span<const collisionPair_t> pairs,
		std::span<RigidBody3D* const> bodies)
	{
		for (auto it = m_Manifolds.end(); it != m_Manifolds.begin();) {
			--it;
			bool candidate = false;
			for (int point = 0; point < it->m_NumContacts; ++point)
				candidate |= it->m_Contacts[point].separationDistance > 0.0f;
			if (!candidate) continue;
			bool found = false;
			for (const auto& pair : pairs) {
				if (pair.a < 0 || pair.b < 0 || pair.a == pair.b ||
					static_cast<std::size_t>(pair.a) >= bodies.size() ||
					static_cast<std::size_t>(pair.b) >= bodies.size()) continue;
				const auto* a = bodies[pair.a];
				const auto* b = bodies[pair.b];
				if (a && b && ((it->m_BodyA == a && it->m_BodyB == b) ||
					(it->m_BodyA == b && it->m_BodyB == a))) { found = true; break; }
			}
			if (found) continue;
			it->RemoveExpiredContacts();
			if (it->m_NumContacts == 0) {
				PhysicsDiagnostic::Mark(PhysicsDiagnostic::Operation::PairEnd,it->m_BodyA,it->m_BodyB,-1,-1,2);
				it = m_Manifolds.erase(it);
			}
		}
	}

	int ManifoldCollector::GetContactCount() const
	{
		int contactCount = 0;
		for (const Manifold& manifold : m_Manifolds)
		{
			contactCount += manifold.GetNumContacts();
		}
		return contactCount;
	}

}
