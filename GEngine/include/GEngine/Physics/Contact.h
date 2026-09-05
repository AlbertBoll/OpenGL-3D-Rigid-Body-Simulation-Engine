#pragma once
#include <Math/Math.h>

namespace GEngine
{
	class RigidBody3D;

	using namespace Math;

	// Contact orientation is relative to the labelled bodies, independent of pair traversal order.
	// Swapping A/B swaps both world/local anchors and negates normal; separation and TOI stay unchanged.
	struct contact_t {
		Vec3f ptOnA_WorldSpace;
		Vec3f ptOnB_WorldSpace;
		Vec3f ptOnA_LocalSpace;
		Vec3f ptOnB_LocalSpace;

		Vec3f normal;	// Unit world-space B -> A normal (repulsive impulse direction on A).
		float separationDistance;	// positive when non-penetrating, negative when penetrating
		float timeOfImpact;

		RigidBody3D* m_BodyA{};
		RigidBody3D* m_BodyB{};
	};


}
