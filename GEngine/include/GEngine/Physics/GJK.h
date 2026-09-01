#pragma once
#include <Math/Math.h>


namespace GEngine
{
	class RigidBody3D;

	using namespace Math;

	Vec2f SignedVolume1D(const Vec3f& s1, const Vec3f& s2);
	Vec3f SignedVolume2D(const Vec3f& s1, const Vec3f& s2, const Vec3f& s3);
	Vec4f SignedVolume3D(const Vec3f& s1, const Vec3f& s2, const Vec3f& s3, const Vec3f& s4);
	Vec3f BarycentricCoordinates(Vec3f s1, Vec3f s2, Vec3f s3, const Vec3f& point);

	bool GJK_DoesIntersect(const RigidBody3D* bodyA, const RigidBody3D* bodyB);
	bool GJK_DoesIntersect(const RigidBody3D* bodyA, const RigidBody3D* bodyB, const float bias, Vec3f& ptOnA, Vec3f& ptOnB);
	void GJK_ClosestPoints(const RigidBody3D* bodyA, const RigidBody3D* bodyB, Vec3f& ptOnA, Vec3f& ptOnB);
	void TestSignedVolumeProjection();
}
