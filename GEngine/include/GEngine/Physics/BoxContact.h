#pragma once

#include "PhysicsBody.h"
#include "ShapeBox.h"

#include <cmath>

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
}
