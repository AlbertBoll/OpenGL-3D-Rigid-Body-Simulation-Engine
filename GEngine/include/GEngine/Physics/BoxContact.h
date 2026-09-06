#pragma once

#include "PhysicsBody.h"
#include "ShapeBox.h"
#include "Contact.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace GEngine
{
	struct BoxContactFeatures
	{
		RigidBodyIdentity referenceBody;
		RigidBodyIdentity incidentBody;
		BoxFaceFeature reference;
		BoxFaceFeature incident;
	};

	// Geometry extraction for an already supplied B -> A contact normal, not an overlap test.
	// Bodies must have distinct valid world identities and actual box shapes. Candidates are
	// A's face along -normal and B's along +normal. The more aligned candidate is reference;
	// ties within BoxFaceAlignmentTolerance use the lower (slot, generation) identity.
	// The incident face opposes the selected reference face normal, not the original query normal.
	// Reversing A/B and normal preserves physical feature ownership. No bodies/caches are mutated,
	// no contacts are emitted, and failure leaves output unchanged.
	inline bool ExtractBoxContactFeatures(const RigidBody3D& bodyA, const RigidBody3D& bodyB,
		const Vec3f& normalBToA, BoxContactFeatures& output)
	{
		const RigidBodyIdentity idA = bodyA.GetIdentity();
		const RigidBodyIdentity idB = bodyB.GetIdentity();
		const auto* boxA = dynamic_cast<const ShapeBox*>(bodyA.m_Shape);
		const auto* boxB = dynamic_cast<const ShapeBox*>(bodyB.m_Shape);
		if (!idA.IsValid() || !idB.IsValid() || idA == idB || !boxA || !boxB ||
			boxA->GetShapeType() != ShapeType::Box || boxB->GetShapeType() != ShapeType::Box) {
			return false;
		}

		BoxFaceFeature candidateA, candidateB;
		if (!boxA->GetContactFace(-normalBToA, bodyA.m_Position, bodyA.m_Orientation, candidateA) ||
			!boxB->GetContactFace(normalBToA, bodyB.m_Position, bodyB.m_Orientation, candidateB)) {
			return false;
		}
		const bool identityAFirst = idA.GetSlot() < idB.GetSlot() ||
			(idA.GetSlot() == idB.GetSlot() && idA.GetGeneration() < idB.GetGeneration());
		const bool referenceIsA = std::abs(candidateA.alignment - candidateB.alignment) <= BoxFaceAlignmentTolerance
			? identityAFirst : candidateA.alignment > candidateB.alignment;

		BoxContactFeatures features;
		features.referenceBody = referenceIsA ? idA : idB;
		features.incidentBody = referenceIsA ? idB : idA;
		features.reference = referenceIsA ? candidateA : candidateB;
		const ShapeBox& incidentBox = referenceIsA ? *boxB : *boxA;
		const RigidBody3D& incidentBody = referenceIsA ? bodyB : bodyA;
		if (!incidentBox.GetContactFace(-features.reference.normal, incidentBody.m_Position,
			incidentBody.m_Orientation, features.incident)) {
			return false;
		}
		output = features;
		return true;
	}

	namespace BoxContactDetail
	{
		// A quad clipped by four side planes and one depth plane has at most nine vertices.
		using Polygon = std::array<glm::dvec3, 12>;

		inline bool Clip(Polygon& polygon, int& count, const glm::dvec3& inward,
			double offset, double tolerance)
		{
			Polygon clipped{};
			int clippedCount = 0;
			const auto append = [&](const glm::dvec3& point) {
				if (clippedCount > 0 && glm::length2(point - clipped[clippedCount - 1]) <= tolerance * tolerance)
					return true;
				if (clippedCount == int(clipped.size())) return false;
				clipped[clippedCount++] = point;
				return true;
			};
			const auto distance = [&](const glm::dvec3& point) {
				const double d = glm::dot(inward, point) - offset;
				return std::abs(d) <= tolerance ? 0.0 : d;
			};
			for (int i = 0; i < count; ++i) {
				const glm::dvec3& a = polygon[i];
				const glm::dvec3& b = polygon[(i + 1) % count];
				const double da = distance(a), db = distance(b);
				if (da >= 0.0 && !append(a)) return false;
				if ((da < 0.0) != (db < 0.0)) {
					// Opposite classifications guarantee a nonzero denominator and t in [0, 1].
					const double t = da / (da - db);
					if (!append(a + t * (b - a))) return false;
				}
			}
			if (clippedCount > 1 && glm::length2(clipped[0] - clipped[clippedCount - 1]) <= tolerance * tolerance)
				--clippedCount;
			polygon = clipped;
			count = clippedCount;
			return true;
		}
	}

	// Expand a validated zero-TOI box seed into 2-4 surface contacts. This is not an overlap query.
	// Near face normals (cosine error <= 1e-4) use the geometric reference normal; edge/corner
	// directions retain the caller's seed. Clipping tolerance is max(1e-6, shortest face edge * 1e-5).
	// A tiny positive separation within that tolerance remains positive, never fake penetration.
	// Returns zero without modifying output when unsupported, degenerate, or numerically invalid.
	inline int BuildBoxFaceContacts(const contact_t& seed, std::array<contact_t, 4>& output)
	{
		if (!seed.m_BodyA || !seed.m_BodyB || seed.timeOfImpact != 0.0f ||
			!Math::IsFinite(seed.separationDistance) || !Math::IsFinite(seed.ptOnA_WorldSpace) ||
			!Math::IsFinite(seed.ptOnB_WorldSpace) || !Math::IsFinite(seed.ptOnA_LocalSpace) ||
			!Math::IsFinite(seed.ptOnB_LocalSpace)) return 0;
		BoxContactFeatures features;
		if (!ExtractBoxContactFeatures(*seed.m_BodyA, *seed.m_BodyB, seed.normal, features) ||
			features.reference.alignment < 1.0f - 1.0e-4f) return 0;

		const glm::dvec3 origin(features.reference.vertices[0]);
		const glm::dvec3 normal = glm::normalize(glm::dvec3(features.reference.normal));
		double shortestEdge2 = std::numeric_limits<double>::max();
		for (int i = 0; i < 4; ++i) {
			shortestEdge2 = std::min(shortestEdge2, glm::length2(glm::dvec3(features.reference.vertices[(i + 1) % 4]) -
				glm::dvec3(features.reference.vertices[i])));
			shortestEdge2 = std::min(shortestEdge2, glm::length2(glm::dvec3(features.incident.vertices[(i + 1) % 4]) -
				glm::dvec3(features.incident.vertices[i])));
		}
		const double tolerance = std::max(double(Math::NumericalEpsilon), std::sqrt(shortestEdge2) * 1.0e-5);
		BoxContactDetail::Polygon polygon{};
		int count = 4;
		for (int i = 0; i < count; ++i) polygon[i] = glm::dvec3(features.incident.vertices[i]) - origin;
		for (int side = 0; side < 4; ++side) {
			const glm::dvec3 a = glm::dvec3(features.reference.vertices[side]) - origin;
			const glm::dvec3 b = glm::dvec3(features.reference.vertices[(side + 1) % 4]) - origin;
			const glm::dvec3 inward = glm::normalize(glm::cross(normal, b - a));
			if (!BoxContactDetail::Clip(polygon, count, inward, glm::dot(inward, a), tolerance)) return 0;
		}
		// Keep the touching/penetrating portion of a tilted incident face as well as its edge intersections.
		if (!BoxContactDetail::Clip(polygon, count, -normal, 0.0, tolerance) || count < 2) return 0;

		std::array<int, 4> selected{ 0, 1, 2, 3 };
		if (count > 4) {
			int deepest = 0;
			for (int i = 1; i < count; ++i)
				if (glm::dot(normal, polygon[i]) < glm::dot(normal, polygon[deepest]) - tolerance) deepest = i;
			// Keep the deepest vertex and maximize the quadrilateral area in the reference plane.
			// Only bounded combinations are visited; ties preserve clipping/feature order.
			double bestArea = -1.0;
			const double areaTolerance = shortestEdge2 * 1.0e-5;
			for (int a = 0; a < count - 3; ++a)
				for (int b = a + 1; b < count - 2; ++b)
					for (int c = b + 1; c < count - 1; ++c)
						for (int d = c + 1; d < count; ++d) {
							if (a != deepest && b != deepest && c != deepest && d != deepest) continue;
							const double area = std::abs(glm::dot(normal,
								glm::cross(polygon[b] - polygon[a], polygon[c] - polygon[a]) +
								glm::cross(polygon[c] - polygon[a], polygon[d] - polygon[a])));
							if (bestArea < 0.0 || area > bestArea + areaTolerance) {
								bestArea = area;
								selected = { a, b, c, d };
							}
						}
			count = 4;
		}

		const bool referenceIsA = features.referenceBody == seed.m_BodyA->GetIdentity();
		std::array<contact_t, 4> contacts{};
		for (int i = 0; i < count; ++i) {
			const glm::dvec3 point = polygon[selected[i]];
			const Vec3f incidentPoint(origin + point);
			const Vec3f referencePoint(origin + point - normal * glm::dot(normal, point));
			contact_t& contact = contacts[i];
			contact = seed;
			contact.normal = Vec3f(referenceIsA ? -normal : normal);
			contact.ptOnA_WorldSpace = referenceIsA ? referencePoint : incidentPoint;
			contact.ptOnB_WorldSpace = referenceIsA ? incidentPoint : referencePoint;
			contact.ptOnA_LocalSpace = seed.m_BodyA->WorldSpaceToBodySpace(contact.ptOnA_WorldSpace);
			contact.ptOnB_LocalSpace = seed.m_BodyB->WorldSpaceToBodySpace(contact.ptOnB_WorldSpace);
			contact.separationDistance = float(glm::dot(glm::dvec3(contact.ptOnA_WorldSpace) -
				glm::dvec3(contact.ptOnB_WorldSpace), glm::dvec3(contact.normal)));
			if (!Math::IsFinite(contact.ptOnA_WorldSpace) || !Math::IsFinite(contact.ptOnB_WorldSpace) ||
				!Math::IsFinite(contact.ptOnA_LocalSpace) || !Math::IsFinite(contact.ptOnB_LocalSpace) ||
				!Math::IsFinite(contact.normal) || !Math::IsFinite(contact.separationDistance)) return 0;
		}
		output = contacts;
		return count;
	}
}
