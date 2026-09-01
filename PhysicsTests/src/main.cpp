#include <GEngine/Core/Log.h>
#include <GEngine/Math/Math.h>
#include <GEngine/Physics/Constraints/ConstraintPenetration.h>
#include <GEngine/Physics/GJK.h>
#include <GEngine/Physics/PhysicsSystem.h>
#include <GEngine/Physics/PhysicsWorld.h>
#include <GEngine/Physics/ShapeBox.h>
#include <GEngine/Physics/ShapeSphere.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
	int failureCount = 0;
	int testCount = 0;

	void Expect(bool condition, std::string_view message)
	{
		++testCount;
		if (!condition)
		{
			++failureCount;
			std::cerr << "FAIL: " << message << '\n';
		}
	}

	bool Near(float actual, float expected, float tolerance = 1.0e-5f)
	{
		return std::isfinite(actual) && std::fabs(actual - expected) <= tolerance;
	}

	bool Finite(const GEngine::Vec3f& value)
	{
		return GEngine::Math::IsFinite(value);
	}

	bool Finite(const GEngine::Quat& value)
	{
		return GEngine::Math::IsFinite(value);
	}

	std::vector<GEngine::Vec3f> UnitBoxPoints()
	{
		return {
			{ -1.0f, -1.0f, -1.0f }, { 1.0f, -1.0f, -1.0f },
			{ -1.0f,  1.0f, -1.0f }, { 1.0f,  1.0f, -1.0f },
			{ -1.0f, -1.0f,  1.0f }, { 1.0f, -1.0f,  1.0f },
			{ -1.0f,  1.0f,  1.0f }, { 1.0f,  1.0f,  1.0f }
		};
	}

	void TestNormalization()
	{
		GEngine::Math::Vector2 vector2(0.0f, 0.0f);
		vector2.Normalize();
		Expect(Near(vector2.x, 0.0f) && Near(vector2.y, 0.0f), "zero Vector2 normalization remains finite zero");

		GEngine::Math::Vector3 vector3(0.0f, 0.0f, 0.0f);
		vector3.Normalize();
		Expect(Near(vector3.x, 0.0f) && Near(vector3.y, 0.0f) && Near(vector3.z, 0.0f),
			"zero Vector3 normalization remains finite zero");

		GEngine::Math::Quaternion quaternion(0.0f, 0.0f, 0.0f, 0.0f);
		quaternion.Normalize();
		Expect(Near(quaternion.x, 0.0f) && Near(quaternion.y, 0.0f) && Near(quaternion.z, 0.0f) && Near(quaternion.w, 1.0f),
			"zero custom quaternion normalization falls back to identity");

		const GEngine::Vec3f normalized = GEngine::Math::NormalizeOr(GEngine::Vec3f(0.0f));
		Expect(Finite(normalized) && Near(glm::length(normalized), 1.0f), "zero GLM vector normalization uses a finite unit fallback");

		const GEngine::Quat normalizedQuaternion = GEngine::Math::NormalizeOrIdentity(GEngine::Quat(0.0f, 0.0f, 0.0f, 0.0f));
		Expect(Finite(normalizedQuaternion) && Near(normalizedQuaternion.w, 1.0f), "zero GLM quaternion normalization uses identity");

		GEngine::Vec3f u;
		GEngine::Vec3f v;
		GEngine::Math::GetOrtho(GEngine::Vec3f(0.0f), u, v);
		Expect(Finite(u) && Finite(v) && Near(glm::length(u), 1.0f) && Near(glm::length(v), 1.0f) &&
			Near(glm::dot(u, v), 0.0f), "zero normal produces a finite orthogonal basis");
	}

	void TestBarycentricAndPointEquality()
	{
		const float height = GEngine::Math::BarryCentric(
			GEngine::Vec3f(0.0f, 3.0f, 0.0f), GEngine::Vec3f(1.0f, 5.0f, 0.0f),
			GEngine::Vec3f(2.0f, 7.0f, 0.0f), GEngine::Vec2f(0.5f, 0.0f));
		Expect(Near(height, 3.0f), "degenerate barycentric interpolation returns a deterministic finite height");

		const GEngine::Vec2f lineWeights = GEngine::SignedVolume1D(GEngine::Vec3f(0.0f), GEngine::Vec3f(0.0f));
		const GEngine::Vec3f triangleWeights = GEngine::SignedVolume2D(
			GEngine::Vec3f(0.0f), GEngine::Vec3f(1.0f, 0.0f, 0.0f), GEngine::Vec3f(2.0f, 0.0f, 0.0f));
		const GEngine::Vec4f tetrahedronWeights = GEngine::SignedVolume3D(
			GEngine::Vec3f(0.0f), GEngine::Vec3f(1.0f, 0.0f, 0.0f),
			GEngine::Vec3f(2.0f, 0.0f, 0.0f), GEngine::Vec3f(3.0f, 0.0f, 0.0f));
		const GEngine::Vec3f epaWeights = GEngine::BarycentricCoordinates(
			GEngine::Vec3f(0.0f), GEngine::Vec3f(1.0f, 0.0f, 0.0f),
			GEngine::Vec3f(2.0f, 0.0f, 0.0f), GEngine::Vec3f(0.0f));
		Expect(GEngine::Math::IsFinite(lineWeights) && GEngine::Math::IsFinite(triangleWeights) &&
			GEngine::Math::IsFinite(tetrahedronWeights) && GEngine::Math::IsFinite(epaWeights),
			"degenerate GJK simplex and EPA barycentric denominators remain finite");

		Expect(GEngine::Math::are_same_point(GEngine::Vec3f(1.0f), GEngine::Vec3f(1.0f, 1.0f, 1.0f + 0.5e-6f)),
			"duplicate-point predicate accepts Z differences within epsilon");
		Expect(!GEngine::Math::are_same_point(GEngine::Vec3f(1.0f), GEngine::Vec3f(1.0f, 1.0f, 1.01f)),
			"duplicate-point predicate rejects Z differences outside epsilon");

		const float infinity = std::numeric_limits<float>::infinity();
		const float nan = std::numeric_limits<float>::quiet_NaN();
		Expect(!GEngine::Math::IsValid(GEngine::Vec3f(infinity, 0.0f, 0.0f)) &&
			!GEngine::Math::IsValid(GEngine::Vec3f(0.0f, nan, 0.0f)), "finite validation rejects Inf and NaN");
	}

	void TestLcpPivots()
	{
		GEngine::Math::Mat<3, 3> matrix;
		matrix.Zero();
		GEngine::Math::Vec<3> rhs;
		rhs.Zero();
		rhs[0] = 1.0f;
		matrix[0][0] = 1.0e-8f;
		const GEngine::Math::Vec<3> guarded = GEngine::Math::LCP_GaussSeidel(matrix, rhs);
		Expect(Near(guarded[0], 0.0f) && Near(guarded[1], 0.0f) && Near(guarded[2], 0.0f),
			"zero and near-zero LCP pivots are skipped without non-finite output");

		matrix[0][0] = 2.0f;
		rhs[0] = 4.0f;
		const GEngine::Math::Vec<3> solved = GEngine::Math::LCP_GaussSeidel(matrix, rhs);
		Expect(Near(solved[0], 2.0f), "ordinary finite LCP pivot retains its prior solution");
	}

	void ConfigureSphereBody(GEngine::RigidBody3D& body, GEngine::ShapeSphere& shape, const GEngine::Vec3f& position)
	{
		body.m_Shape = &shape;
		body.m_Position = position;
		body.m_Orientation = GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f);
		body.Type = GEngine::Component::BodyType::Dynamic;
		body.m_InvMass = 1.0f;
	}

	void TestSphereContacts()
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::RigidBody3D bodyA;
		GEngine::RigidBody3D bodyB;
		ConfigureSphereBody(bodyA, sphere, GEngine::Vec3f(0.0f));
		ConfigureSphereBody(bodyB, sphere, GEngine::Vec3f(0.0f));

		GEngine::contact_t contact{};
		Expect(GEngine::Collision::Intersect(&bodyA, &bodyB, contact), "coincident spheres intersect");
		Expect(Finite(contact.normal) && Near(glm::length(contact.normal), 1.0f) &&
			Finite(contact.ptOnA_WorldSpace) && Finite(contact.ptOnB_WorldSpace),
			"coincident static sphere contact is finite");

		GEngine::contact_t sweptContact{};
		Expect(GEngine::Collision::Intersect(&bodyA, &bodyB, 1.0f / 120.0f, sweptContact),
			"coincident stationary spheres intersect in swept query");
		Expect(Finite(sweptContact.normal) && Near(glm::length(sweptContact.normal), 1.0f),
			"coincident swept sphere contact is finite");

		bodyB.m_Position = GEngine::Vec3f(3.0f, 0.0f, 0.0f);
		GEngine::contact_t separatedContact{};
		Expect(!GEngine::Collision::Intersect(&bodyA, &bodyB, separatedContact), "ordinary separated spheres remain separated");
		bodyB.m_Position = GEngine::Vec3f(1.5f, 0.0f, 0.0f);
		Expect(GEngine::Collision::Intersect(&bodyA, &bodyB, separatedContact), "ordinary overlapping spheres remain intersecting");
	}

	void TestDegenerateGjkDirection()
	{
		GEngine::ShapeBox pointShape({ GEngine::Vec3f(0.0f) });
		GEngine::RigidBody3D bodyA;
		GEngine::RigidBody3D bodyB;
		bodyA.m_Shape = &pointShape;
		bodyB.m_Shape = &pointShape;
		bodyA.m_Orientation = GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f);
		bodyB.m_Orientation = GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f);

		GEngine::Vec3f pointA;
		GEngine::Vec3f pointB;
		GEngine::GJK_ClosestPoints(&bodyA, &bodyB, pointA, pointB);
		Expect(Finite(pointA) && Finite(pointB), "degenerate GJK direction returns finite closest points");
		Expect(!GEngine::GJK_DoesIntersect(&bodyA, &bodyB), "coincident zero-volume GJK shapes terminate deterministically");
	}

	void TestZeroQuaternionBodyUpdate()
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::RigidBody3D body;
		ConfigureSphereBody(body, sphere, GEngine::Vec3f(0.0f));
		body.m_Orientation = GEngine::Quat(0.0f, 0.0f, 0.0f, 0.0f);
		body.Update(0.0f);
		Expect(body.HasFiniteState() && Near(body.m_Orientation.w, 1.0f) && Near(glm::length(body.m_Orientation), 1.0f),
			"body integration repairs a zero quaternion to finite identity");
	}

	void TestConstraintDenominators()
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::RigidBody3D bodyA;
		GEngine::RigidBody3D bodyB;
		ConfigureSphereBody(bodyA, sphere, GEngine::Vec3f(0.0f));
		ConfigureSphereBody(bodyB, sphere, GEngine::Vec3f(0.0f));
		bodyA.Type = GEngine::Component::BodyType::Static;
		bodyB.Type = GEngine::Component::BodyType::Static;
		bodyA.m_InvMass = 0.0f;
		bodyB.m_InvMass = 0.0f;

		GEngine::ConstraintPenetration constraint;
		constraint.m_bodyA = &bodyA;
		constraint.m_bodyB = &bodyB;
		constraint.m_Normal = GEngine::Vec3f(0.0f);
		constraint.m_anchorA = GEngine::Vec3f(0.0f);
		constraint.m_anchorB = GEngine::Vec3f(0.0f);
		constraint.PreSolve(0.0f);
		Expect(Near(constraint.m_Baumgarte, 0.0f), "zero dt produces zero penetration correction");
		constraint.PreSolve(1.0e-8f);
		Expect(Near(constraint.m_Baumgarte, 0.0f), "near-zero dt produces zero penetration correction");
		constraint.Solve();
		Expect(bodyA.HasFiniteState() && bodyB.HasFiniteState() && Near(constraint.m_CachedLambda[0], 0.0f),
			"zero inverse-mass constraint solve remains finite");

		GEngine::contact_t contact{};
		contact.m_BodyA = &bodyA;
		contact.m_BodyB = &bodyB;
		contact.normal = GEngine::Vec3f(0.0f);
		contact.timeOfImpact = 0.0f;
		GEngine::Collision::ResolveContact(contact);
		Expect(bodyA.HasFiniteState() && bodyB.HasFiniteState(),
			"zero normal and zero effective-mass contact resolution remains finite");
	}

	void TestGravityAndInverseMass()
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::PhysicsSystem system;
		auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f, -12.0f, 0.0f));
		system.SetPhysicsWorld(world);

		GEngine::RigidBody3D* dynamicBody = world->CreateRigidBody3D();
		ConfigureSphereBody(*dynamicBody, sphere, GEngine::Vec3f(0.0f));
		dynamicBody->m_InvMass = 0.5f;

		GEngine::RigidBody3D* infiniteMassBody = world->CreateRigidBody3D();
		ConfigureSphereBody(*infiniteMassBody, sphere, GEngine::Vec3f(10.0f, 0.0f, 0.0f));
		infiniteMassBody->m_InvMass = 0.0f;

		constexpr float dt = 1.0f / 120.0f;
		system.Update(GEngine::Timestep(dt));
		Expect(dynamicBody->HasFiniteState() && Near(dynamicBody->m_LinearVelocity.y, -12.0f * dt) &&
			Near(dynamicBody->m_Position.y, -12.0f * dt * dt), "ordinary one-step gravity trajectory is preserved");
		Expect(infiniteMassBody->HasFiniteState() && Near(infiniteMassBody->m_LinearVelocity.y, 0.0f) &&
			Near(infiniteMassBody->m_Position.y, 0.0f), "zero inverse mass does not divide by zero under gravity");
	}
}

int main()
{
	GEngine::Log::Initialize();

	TestNormalization();
	TestBarycentricAndPointEquality();
	TestLcpPivots();
	TestSphereContacts();
	TestDegenerateGjkDirection();
	TestZeroQuaternionBodyUpdate();
	TestConstraintDenominators();
	TestGravityAndInverseMass();

	if (failureCount != 0)
	{
		std::cerr << failureCount << " of " << testCount << " checks failed\n";
		return 1;
	}

	std::cout << "PhysicsTests: " << testCount << " checks passed\n";
	return 0;
}
