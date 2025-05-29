#pragma once
#include <Math/Math.h>


namespace GEngine
{
	class RigidBody3D;

	using namespace Math;

	bool GJK_DoesIntersect(const RigidBody3D* bodyA, const RigidBody3D* bodyB);
	bool GJK_DoesIntersect(const RigidBody3D* bodyA, const RigidBody3D* bodyB, const float bias, Vec3f& ptOnA, Vec3f& ptOnB);
	void GJK_ClosestPoints(const RigidBody3D* bodyA, const RigidBody3D* bodyB, Vec3f& ptOnA, Vec3f& ptOnB);
	void TestSignedVolumeProjection();
}
