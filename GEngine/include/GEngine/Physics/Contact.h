#pragma once
#include <Math/Math.h>

namespace GEngine
{
	class RigidBody3D;

	using namespace Math;

	struct contact_t {
		Vec3f ptOnA_WorldSpace;
		Vec3f ptOnB_WorldSpace;
		Vec3f ptOnA_LocalSpace;
		Vec3f ptOnB_LocalSpace;

		Vec3f normal;	// In World Space coordinates
		float separationDistance;	// positive when non-penetrating, negative when penetrating
		float timeOfImpact;

		RigidBody3D* m_BodyA{};
		RigidBody3D* m_BodyB{};
	};


}
