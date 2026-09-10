#include "gepch.h"
#include "Manifold.h"
#include "PhysicsBody.h"
#include "Shape.h"
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
		const Vec3f normal = Math::NormalizeOr(m_BodyA->GetWorldToBodyRotation() * -contact.normal);
		Vec<3> lambda;
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


	void Manifold::RefreshContacts(const contact_t* contacts, int count)
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

		// Exact topology has priority across the whole patch. At coincident box edges,
		// tiny sliding/rotation changes boundary bits without changing the supporting
		// face pair. Unmatched points may then use both nearby anchors on those same
		// labelled faces; a different face, normal, shape or body still starts cold.
		for (int pass = 0; compatible && pass < 2; ++pass) {
			for (;;) {
				int bestPoint = -1, bestOld = -1;
				double bestDistance = AnchorDistance2;
				for (int point = 0; point < count; ++point) {
					if (matches[point] >= 0) continue;
					const auto& contact = canonical[point];
					for (int i = 0; i < m_NumContacts; ++i) {
						if (used[i]) continue;
						const auto& old = m_Contacts[i];
						const bool featureMatch = pass == 0 ? SameFeatures(contact, old) :
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

		const auto append = [&](int point) {
			const int match = matches[point];
			refreshed.WriteContact(refreshed.m_NumContacts, canonical[point],
				match >= 0 ? &m_Constraints[match] : nullptr);
			++refreshed.m_NumContacts;
		};
		// Clipping can cyclically permute unchanged points when reference ownership
		// or boundary classification changes. Keep surviving constraints in their old
		// solve order, then append genuinely new points in deterministic input order.
		for (int i = 0; i < m_NumContacts; ++i)
			for (int point = 0; point < count; ++point)
				if (matches[point] == i) append(point);
		for (int point = 0; point < count; ++point)
			if (matches[point] < 0) append(point);
		*this = refreshed;
	}

	void Manifold::RemoveExpiredContacts()
	{
		if (!CacheCompatible()) { m_NumContacts = 0; return; }
		for (int i = 0; i < m_NumContacts;) {
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
				glm::length2(tangent) < AnchorDistance2 && separation <= 0.0f) {
				contact.ptOnA_WorldSpace = a;
				contact.ptOnB_WorldSpace = b;
				contact.normal = -normal;
				contact.separationDistance = separation;
				++i;
			}
			else RemoveContact(i);
		}
	}


	void Manifold::PreSolve(const float dt_sec)
	{
		if (!CacheCompatible()) { m_NumContacts = 0; return; }
		for (int i = 0; i < m_NumContacts;) {
			const Vec3f normalA = m_BodyA->GetBodyToWorldRotation() * m_Constraints[i].m_Normal;
			const Vec3f normalB = m_BodyB->GetBodyToWorldRotation() * m_NormalB[i];
			if (!(glm::dot(normalA, normalB) >= NormalCoherence)) RemoveContact(i);
			else {
				// Keep compatibility/normal validation for sleeping contact caches.
				if (HasActiveResponse(m_BodyA, m_BodyB)) m_Constraints[i].PreSolve(dt_sec);
				++i;
			}
		}
	}

	namespace
	{
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
				float next = static_cast<float>(best[i]), delta = next - contacts[i].m_CachedLambda[0];
				contacts[i].m_CachedLambda[0] = next;
				const Vec3f impulse = Vec3f(n[i]) * delta;
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
			if (blockSolved) m_Constraints[i].SolveFriction();
			else m_Constraints[i].Solve(); // Single witness or failed/unsupported block.
		}
	}
	void Manifold::PostSolve()
	{
		if (!HasActiveResponse(m_BodyA, m_BodyB)) return;
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
		AddContacts(&contact, 1);
	}

	void ManifoldCollector::AddContacts(const contact_t* contacts, int count)
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
		}
		if (count == 1) found->AddContact(first);
		else found->RefreshContacts(contacts, count);
	}

	void ManifoldCollector::PreSolve(const float dt_sec)
	{
		for (int i = 0; i < m_Manifolds.size();) {
			m_Manifolds[i].PreSolve(dt_sec);
			// Invalidation can empty a manifold here, after the world's expiry stage.
			// Do not leave an empty pair holding pointers that body-removal skips.
			if (m_Manifolds[i].GetNumContacts() == 0) m_Manifolds.erase(m_Manifolds.begin() + i);
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
