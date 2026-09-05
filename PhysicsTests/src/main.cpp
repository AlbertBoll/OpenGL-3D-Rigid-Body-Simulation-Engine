#include <GEngine/Core/Log.h>
#include <GEngine/Math/Math.h>
#include <GEngine/Physics/Constraints/ConstraintPenetration.h>
#include <GEngine/Physics/Broadphase.h>
#include <GEngine/Physics/GJK.h>
#include <GEngine/Physics/Manifold.h>
#include <GEngine/Physics/PhysicsProfile.h>
#include <GEngine/Physics/PhysicsSystem.h>
#include <GEngine/Physics/PhysicsWorld.h>
#include <GEngine/Physics/ShapeBox.h>
#include <GEngine/Physics/ShapeConvex.h>
#include <GEngine/Physics/ShapeSphere.h>
#include <GEngine/Scene/_Entity.h>

#include <algorithm>
#include <cmath>
#include <array>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
	// Keep focused lifetime-state inspection in the test translation unit without adding a production API.
	template<typename Tag, typename Tag::Type Member>
	struct PrivateMemberAccess
	{
		friend typename Tag::Type GetPrivateMember(Tag) { return Member; }
	};

	struct PhysicsSystemManifoldsTag
	{
		using Type = GEngine::ManifoldCollector GEngine::PhysicsSystem::*;
		friend Type GetPrivateMember(PhysicsSystemManifoldsTag);
	};

	struct PhysicsSystemContactsTag
	{
		using Type = std::vector<GEngine::contact_t> GEngine::PhysicsSystem::*;
		friend Type GetPrivateMember(PhysicsSystemContactsTag);
	};

	struct PhysicsSystemBroadphaseTag
	{
		using Type = GEngine::SweepAndPruneBroadphase GEngine::PhysicsSystem::*;
		friend Type GetPrivateMember(PhysicsSystemBroadphaseTag);
	};

	struct PhysicsSystemCollisionPairsTag
	{
		using Type = std::vector<GEngine::collisionPair_t> GEngine::PhysicsSystem::*;
		friend Type GetPrivateMember(PhysicsSystemCollisionPairsTag);
	};

	template struct PrivateMemberAccess<PhysicsSystemManifoldsTag, &GEngine::PhysicsSystem::m_Manifolds>;
	template struct PrivateMemberAccess<PhysicsSystemContactsTag, &GEngine::PhysicsSystem::m_Contacts>;
	template struct PrivateMemberAccess<PhysicsSystemBroadphaseTag, &GEngine::PhysicsSystem::m_Broadphase>;
	template struct PrivateMemberAccess<PhysicsSystemCollisionPairsTag, &GEngine::PhysicsSystem::m_CollisionPairs>;

	GEngine::ManifoldCollector& GetManifolds(GEngine::PhysicsSystem& system)
	{
		return system.*GetPrivateMember(PhysicsSystemManifoldsTag{});
	}

	std::vector<GEngine::contact_t>& GetTransientContacts(GEngine::PhysicsSystem& system)
	{
		return system.*GetPrivateMember(PhysicsSystemContactsTag{});
	}

	GEngine::SweepAndPruneBroadphase& GetBroadphase(GEngine::PhysicsSystem& system)
	{
		return system.*GetPrivateMember(PhysicsSystemBroadphaseTag{});
	}

	std::vector<GEngine::collisionPair_t>& GetCollisionPairs(GEngine::PhysicsSystem& system)
	{
		return system.*GetPrivateMember(PhysicsSystemCollisionPairsTag{});
	}

	int failureCount = 0;
	int testCount = 0;
	int diagnosticCount = 0;
	int observedKnownIssueCount = 0;

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

	bool Near(const GEngine::Vec3f& actual, const GEngine::Vec3f& expected, float tolerance = 1.0e-5f)
	{
		return Near(actual.x, expected.x, tolerance) && Near(actual.y, expected.y, tolerance) &&
			Near(actual.z, expected.z, tolerance);
	}

	bool Near(const GEngine::Mat3& actual, const GEngine::Mat3& expected, float tolerance = 1.0e-5f)
	{
		for (int column = 0; column < 3; ++column)
		{
			if (!Near(actual[column], expected[column], tolerance))
			{
				return false;
			}
		}
		return true;
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

	void KnownIssueDiagnostic(bool correctedCondition, std::string_view message)
	{
		++diagnosticCount;
		if (correctedCondition)
		{
			std::cout << "XPASS: " << message << '\n';
			return;
		}

		++observedKnownIssueCount;
		std::cout << "XFAIL: " << message << '\n';
	}

	std::vector<GEngine::Vec3f> BoxPoints(const GEngine::Vec3f& halfExtents)
	{
		return {
			{ -halfExtents.x, -halfExtents.y, -halfExtents.z },
			{  halfExtents.x, -halfExtents.y, -halfExtents.z },
			{ -halfExtents.x,  halfExtents.y, -halfExtents.z },
			{  halfExtents.x,  halfExtents.y, -halfExtents.z },
			{ -halfExtents.x, -halfExtents.y,  halfExtents.z },
			{  halfExtents.x, -halfExtents.y,  halfExtents.z },
			{ -halfExtents.x,  halfExtents.y,  halfExtents.z },
			{  halfExtents.x,  halfExtents.y,  halfExtents.z }
		};
	}

	bool Finite(const GEngine::contact_t& contact)
	{
		return Finite(contact.ptOnA_WorldSpace) && Finite(contact.ptOnB_WorldSpace) &&
			Finite(contact.ptOnA_LocalSpace) && Finite(contact.ptOnB_LocalSpace) &&
			Finite(contact.normal) && std::isfinite(contact.separationDistance) &&
			std::isfinite(contact.timeOfImpact);
	}

	class CountingShape final : public GEngine::PhysicalShape
	{
	public:
		CountingShape()
		{
			SetGeometry(
				GEngine::Bounds(), GEngine::Vec3f(0.0f), GEngine::Mat3(1.0f));
		}

		void SetGeometry(const GEngine::Bounds& bounds, const GEngine::Vec3f& centerOfMass,
			const GEngine::Mat3& inertia)
		{
			m_LocalBounds = bounds;
			m_CenterOfMass = centerOfMass;
			m_Inertia = inertia;
			MarkGeometryChanged();
		}

		GEngine::Mat3 InertiaTensor() const override
		{
			++m_InertiaQueryCount;
			return m_Inertia;
		}

		GEngine::Bounds GetBounds(const GEngine::Vec3f& position, const GEngine::Quat& orientation) const override
		{
			++m_WorldBoundsQueryCount;
			const GEngine::Mat3 rotation = glm::toMat3(GEngine::Math::NormalizeOrIdentity(orientation));
			const GEngine::Vec3f corners[8] = {
				{ m_LocalBounds.mins.x, m_LocalBounds.mins.y, m_LocalBounds.mins.z },
				{ m_LocalBounds.mins.x, m_LocalBounds.mins.y, m_LocalBounds.maxs.z },
				{ m_LocalBounds.mins.x, m_LocalBounds.maxs.y, m_LocalBounds.mins.z },
				{ m_LocalBounds.maxs.x, m_LocalBounds.mins.y, m_LocalBounds.mins.z },
				{ m_LocalBounds.maxs.x, m_LocalBounds.maxs.y, m_LocalBounds.maxs.z },
				{ m_LocalBounds.maxs.x, m_LocalBounds.maxs.y, m_LocalBounds.mins.z },
				{ m_LocalBounds.maxs.x, m_LocalBounds.mins.y, m_LocalBounds.maxs.z },
				{ m_LocalBounds.mins.x, m_LocalBounds.maxs.y, m_LocalBounds.maxs.z }
			};
			GEngine::Bounds result;
			for (const GEngine::Vec3f& corner : corners)
			{
				result.Expand(rotation * corner + position);
			}
			return result;
		}

		GEngine::Bounds GetBounds() const override
		{
			return m_LocalBounds;
		}

		GEngine::Vec3f Support(const GEngine::Vec3f& direction, const GEngine::Vec3f& position,
			const GEngine::Quat& orientation, float bias) const override
		{
			const GEngine::Mat3 rotation = glm::toMat3(GEngine::Math::NormalizeOrIdentity(orientation));
			const GEngine::Vec3f local(
				direction.x >= 0.0f ? m_LocalBounds.maxs.x : m_LocalBounds.mins.x,
				direction.y >= 0.0f ? m_LocalBounds.maxs.y : m_LocalBounds.mins.y,
				direction.z >= 0.0f ? m_LocalBounds.maxs.z : m_LocalBounds.mins.z);
			return rotation * local + position + GEngine::Math::NormalizeOr(direction) * bias;
		}

		int GetInertiaQueryCount() const { return m_InertiaQueryCount; }
		int GetWorldBoundsQueryCount() const { return m_WorldBoundsQueryCount; }

	private:
		GEngine::Bounds m_LocalBounds;
		GEngine::Mat3 m_Inertia{ 1.0f };
		mutable int m_InertiaQueryCount{};
		mutable int m_WorldBoundsQueryCount{};
	};

	GEngine::Bounds MakeBounds(const GEngine::Vec3f& mins, const GEngine::Vec3f& maxs)
	{
		GEngine::Bounds bounds;
		bounds.mins = mins;
		bounds.maxs = maxs;
		return bounds;
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
		GEngine::ShapeSphere pointShape(0.0f);
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

	void ConfigureBoxBody(GEngine::RigidBody3D& body, GEngine::ShapeBox& shape,
		const GEngine::Vec3f& position, const GEngine::Quat& orientation)
	{
		body.m_Shape = &shape;
		body.m_Position = position;
		body.m_Orientation = orientation;
		body.Type = GEngine::Component::BodyType::Dynamic;
		body.m_InvMass = 1.0f;
	}

	void TestBodyRemovalLifetimeRegression()
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::PhysicsSystem system;
		auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f));
		system.SetPhysicsWorld(world);

		GEngine::RigidBody3D* bodyA = world->CreateRigidBody3D();
		GEngine::RigidBody3D* bodyB = world->CreateRigidBody3D();
		GEngine::RigidBody3D* unrelatedA = world->CreateRigidBody3D();
		GEngine::RigidBody3D* unrelatedB = world->CreateRigidBody3D();
		ConfigureSphereBody(*bodyA, sphere, GEngine::Vec3f(0.0f, 0.0f, 0.0f));
		ConfigureSphereBody(*bodyB, sphere, GEngine::Vec3f(2.0f, 0.0f, 0.0f));
		ConfigureSphereBody(*unrelatedA, sphere, GEngine::Vec3f(10.0f, 0.0f, 0.0f));
		ConfigureSphereBody(*unrelatedB, sphere, GEngine::Vec3f(12.0f, 0.0f, 0.0f));

		constexpr float dt = 1.0f / 120.0f;
		GEngine::ResetPhysicsProfile();
		system.Update(GEngine::Timestep(dt));
		const GEngine::PhysicsProfileSnapshot initialProfile = GEngine::GetPhysicsProfileSnapshot();
		Expect(!GEngine::IsPhysicsProfilingEnabled() ||
			(initialProfile.manifoldCount == 2 && initialProfile.manifoldContactCount == 2),
			"body-removal fixture creates the target and unrelated active manifolds");

		world->RemoveRigidBody3D(bodyA);
		Expect(world->GetPhysicsBodies().size() == 3,
			"removing the first body erases it from the world");

		GEngine::ResetPhysicsProfile();
		system.Update(GEngine::Timestep(dt));
		const GEngine::PhysicsProfileSnapshot afterFirstRemoval = GEngine::GetPhysicsProfileSnapshot();
		const bool firstBodyInvalidated = !GEngine::IsPhysicsProfilingEnabled() ||
			(afterFirstRemoval.manifoldCount == 1 && afterFirstRemoval.manifoldContactCount == 1);
		KnownIssueDiagnostic(firstBodyInvalidated,
			"body removal invalidates manifolds before deleting the body");
		Expect(firstBodyInvalidated,
			"removing the first body discards only its active manifold");
		Expect(bodyB->HasFiniteState() && unrelatedA->HasFiniteState() && unrelatedB->HasFiniteState(),
			"physics continues stepping safely after removing the first body");

		GEngine::RigidBody3D* bodyWithoutManifold = world->CreateRigidBody3D();
		ConfigureSphereBody(*bodyWithoutManifold, sphere, GEngine::Vec3f(100.0f, 0.0f, 0.0f));
		world->RemoveRigidBody3D(bodyWithoutManifold);
		GEngine::ResetPhysicsProfile();
		system.Update(GEngine::Timestep(dt));
		const GEngine::PhysicsProfileSnapshot afterNoManifoldRemoval = GEngine::GetPhysicsProfileSnapshot();
		Expect(world->GetPhysicsBodies().size() == 3 &&
			(!GEngine::IsPhysicsProfilingEnabled() || afterNoManifoldRemoval.manifoldCount == 1),
			"removing a body without a manifold preserves unrelated manifolds");

		GEngine::PhysicsSystem oppositeSystem;
		auto* oppositeWorld = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f));
		oppositeSystem.SetPhysicsWorld(oppositeWorld);
		GEngine::RigidBody3D* oppositeA = oppositeWorld->CreateRigidBody3D();
		GEngine::RigidBody3D* oppositeB = oppositeWorld->CreateRigidBody3D();
		ConfigureSphereBody(*oppositeA, sphere, GEngine::Vec3f(0.0f, 0.0f, 0.0f));
		ConfigureSphereBody(*oppositeB, sphere, GEngine::Vec3f(2.0f, 0.0f, 0.0f));
		oppositeSystem.Update(GEngine::Timestep(dt));
		oppositeWorld->RemoveRigidBody3D(oppositeB);
		GEngine::ResetPhysicsProfile();
		oppositeSystem.Update(GEngine::Timestep(dt));
		const GEngine::PhysicsProfileSnapshot afterOppositeRemoval = GEngine::GetPhysicsProfileSnapshot();
		Expect(oppositeWorld->GetPhysicsBodies().size() == 1 && oppositeA->HasFiniteState() &&
			(!GEngine::IsPhysicsProfilingEnabled() || afterOppositeRemoval.manifoldCount == 0),
			"removing the opposite body invalidates its manifold and permits another safe step");
	}

	void TestMultiManifoldBodyRemovalRegression()
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::PhysicsSystem system;
		auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f));
		system.SetPhysicsWorld(world);

		GEngine::RigidBody3D* sharedBody = world->CreateRigidBody3D();
		GEngine::RigidBody3D* leftBody = world->CreateRigidBody3D();
		GEngine::RigidBody3D* rightBody = world->CreateRigidBody3D();
		GEngine::RigidBody3D* unrelatedA = world->CreateRigidBody3D();
		GEngine::RigidBody3D* unrelatedB = world->CreateRigidBody3D();
		ConfigureSphereBody(*sharedBody, sphere, GEngine::Vec3f(0.0f, 0.0f, 0.0f));
		ConfigureSphereBody(*leftBody, sphere, GEngine::Vec3f(-2.0f, 0.0f, 0.0f));
		ConfigureSphereBody(*rightBody, sphere, GEngine::Vec3f(2.0f, 0.0f, 0.0f));
		ConfigureSphereBody(*unrelatedA, sphere, GEngine::Vec3f(10.0f, 0.0f, 0.0f));
		ConfigureSphereBody(*unrelatedB, sphere, GEngine::Vec3f(12.0f, 0.0f, 0.0f));

		constexpr float dt = 1.0f / 120.0f;
		system.Update(GEngine::Timestep(dt));
		GEngine::ManifoldCollector& manifolds = GetManifolds(system);
		Expect(manifolds.m_Manifolds.size() == 3 && manifolds.GetContactCount() == 3,
			"shared-body fixture creates two target manifolds and one unrelated manifold");

		world->RemoveRigidBody3D(sharedBody);
		const bool unrelatedManifoldPreserved = manifolds.m_Manifolds.size() == 1 &&
			manifolds.GetContactCount() == 1 && manifolds.m_Manifolds[0].GetNumContacts() == 1;
		GEngine::contact_t unrelatedContact{};
		if (unrelatedManifoldPreserved)
		{
			unrelatedContact = manifolds.m_Manifolds[0].GetContact(0);
		}
		const bool unrelatedPairPreserved = unrelatedManifoldPreserved &&
			((unrelatedContact.m_BodyA == unrelatedA && unrelatedContact.m_BodyB == unrelatedB) ||
				(unrelatedContact.m_BodyA == unrelatedB && unrelatedContact.m_BodyB == unrelatedA));
		Expect(unrelatedPairPreserved,
			"removing a shared body invalidates all of its manifolds and preserves the unrelated contact");

		system.Update(GEngine::Timestep(dt));
		Expect(GetManifolds(system).m_Manifolds.size() == 1 &&
			leftBody->HasFiniteState() && rightBody->HasFiniteState() &&
			unrelatedA->HasFiniteState() && unrelatedB->HasFiniteState(),
			"multi-manifold removal permits safe stepping with finite survivors");
	}

	void TestTransientContactBodyRemovalRegression()
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::PhysicsSystem system;
		auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f));
		system.SetPhysicsWorld(world);

		GEngine::RigidBody3D* removedBody = world->CreateRigidBody3D();
		GEngine::RigidBody3D* targetBody = world->CreateRigidBody3D();
		GEngine::RigidBody3D* unrelatedA = world->CreateRigidBody3D();
		GEngine::RigidBody3D* unrelatedB = world->CreateRigidBody3D();
		ConfigureSphereBody(*removedBody, sphere, GEngine::Vec3f(0.0f, 0.0f, 0.0f));
		ConfigureSphereBody(*targetBody, sphere, GEngine::Vec3f(5.0f, 0.0f, 0.0f));
		ConfigureSphereBody(*unrelatedA, sphere, GEngine::Vec3f(20.0f, 0.0f, 0.0f));
		ConfigureSphereBody(*unrelatedB, sphere, GEngine::Vec3f(25.0f, 0.0f, 0.0f));
		removedBody->m_LinearVelocity = GEngine::Vec3f(4.0f, 0.0f, 0.0f);
		unrelatedA->m_LinearVelocity = GEngine::Vec3f(4.0f, 0.0f, 0.0f);

		GEngine::contact_t removedBodyContact{};
		GEngine::contact_t unrelatedContact{};
		const bool createdRemovedBodyContact =
			GEngine::Collision::Intersect(removedBody, targetBody, 1.0f, removedBodyContact);
		const bool createdUnrelatedContact =
			GEngine::Collision::Intersect(unrelatedA, unrelatedB, 1.0f, unrelatedContact);
		Expect(createdRemovedBodyContact && createdUnrelatedContact &&
			removedBodyContact.timeOfImpact > 0.0f && unrelatedContact.timeOfImpact > 0.0f &&
			Finite(removedBodyContact) && Finite(unrelatedContact),
			"transient-removal fixture creates real finite positive-TOI contacts");

		std::vector<GEngine::contact_t>& transientContacts = GetTransientContacts(system);
		transientContacts.push_back(removedBodyContact);
		transientContacts.push_back(unrelatedContact);
		Expect(transientContacts.size() == 2,
			"positive-TOI contacts are queued before body removal");

		world->RemoveRigidBody3D(removedBody);
		const bool unrelatedContactPreserved = transientContacts.size() == 1 &&
			((transientContacts[0].m_BodyA == unrelatedA && transientContacts[0].m_BodyB == unrelatedB) ||
				(transientContacts[0].m_BodyA == unrelatedB && transientContacts[0].m_BodyB == unrelatedA));
		Expect(unrelatedContactPreserved,
			"body removal invalidates its transient contact before deletion and preserves unrelated transient state");

		system.Update(GEngine::Timestep(1.0f / 120.0f));
		Expect(world->GetPhysicsBodies().size() == 3 && GetTransientContacts(system).empty() &&
			targetBody->HasFiniteState() && unrelatedA->HasFiniteState() && unrelatedB->HasFiniteState(),
			"transient-contact removal permits safe stepping with finite survivors");
	}

	void TestConvexValidityContract()
	{
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float infinity = std::numeric_limits<float>::infinity();
		const GEngine::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
		const auto support = [&identity](const GEngine::ShapeConvex& convex,
			const GEngine::Vec3f& direction = GEngine::Vec3f(1.0f, 0.0f, 0.0f)) {
			return convex.Support(direction, GEngine::Vec3f(0.0f), identity, 0.0f);
		};

		GEngine::ShapeConvex defaultConvex;
		GEngine::ShapeConvex emptyConvex(std::vector<GEngine::Vec3f>{});
		const std::vector<GEngine::Vec3f> triangle{
			GEngine::Vec3f(0.0f, 0.0f, 0.0f),
			GEngine::Vec3f(1.0f, 0.0f, 0.0f),
			GEngine::Vec3f(0.0f, 1.0f, 0.0f)
		};
		GEngine::ShapeConvex fewerThanFourConvex(triangle);
		GEngine::ShapeConvex duplicateConvex({
			triangle[0], triangle[1], triangle[2], triangle[2]
		});
		const std::vector<GEngine::Vec3f> collinearPoints{
			GEngine::Vec3f(-2.0f, -4.0f, -6.0f), GEngine::Vec3f(-1.0f, -2.0f, -3.0f),
			GEngine::Vec3f(0.0f, 0.0f, 0.0f), GEngine::Vec3f(1.0f, 2.0f, 3.0f),
			GEngine::Vec3f(2.0f, 4.0f, 6.0f)
		};
		GEngine::ShapeConvex collinearConvex(collinearPoints);
		const std::vector<GEngine::Vec3f> coplanarPoints{
			GEngine::Vec3f(-1.0f, -1.0f, -2.0f), GEngine::Vec3f(1.0f, -1.0f, 0.0f),
			GEngine::Vec3f(1.0f, 1.0f, 2.0f), GEngine::Vec3f(-1.0f, 1.0f, 0.0f),
			GEngine::Vec3f(0.0f, 0.0f, 0.0f)
		};
		GEngine::ShapeConvex coplanarConvex(coplanarPoints);
		std::vector<GEngine::Vec3f> nonFinitePoints = UnitBoxPoints();
		nonFinitePoints[3].x = nan;
		GEngine::ShapeConvex nonFiniteConvex(nonFinitePoints);
		std::vector<GEngine::Vec3f> positiveInfinityPoints = UnitBoxPoints();
		positiveInfinityPoints[1].y = infinity;
		GEngine::ShapeConvex positiveInfinityConvex(positiveInfinityPoints);
		std::vector<GEngine::Vec3f> negativeInfinityPoints = UnitBoxPoints();
		negativeInfinityPoints[6].z = -infinity;
		GEngine::ShapeConvex negativeInfinityConvex(negativeInfinityPoints);

		Expect(!defaultConvex.IsValid() && !emptyConvex.IsValid() &&
			!fewerThanFourConvex.IsValid(),
			"default, empty, and fewer-than-four-point convex shapes report invalid");
		Expect(!duplicateConvex.IsValid() && !coplanarConvex.IsValid(),
			"fewer-than-four usable points and zero-volume coplanar hulls report invalid");
		Expect(!collinearConvex.IsValid() && collinearConvex.GetPoints().empty() &&
			!Finite(support(collinearConvex)),
			"four or more finite unique collinear points are rejected safely");
		Expect(!nonFiniteConvex.IsValid(), "non-finite convex input reports invalid");
		Expect(!positiveInfinityConvex.IsValid() && !negativeInfinityConvex.IsValid() &&
			positiveInfinityConvex.GetPoints().empty() && negativeInfinityConvex.GetPoints().empty(),
			"explicit positive and negative infinity convex inputs are rejected");
		Expect(defaultConvex.GetPoints().empty() && emptyConvex.GetPoints().empty() &&
			fewerThanFourConvex.GetPoints().empty() && duplicateConvex.GetPoints().empty() &&
			coplanarConvex.GetPoints().empty() && nonFiniteConvex.GetPoints().empty() &&
			positiveInfinityConvex.GetPoints().empty() && negativeInfinityConvex.GetPoints().empty(),
			"invalid convex construction never commits partial hull points");
		Expect(!Finite(support(defaultConvex)) && !Finite(support(emptyConvex)) &&
			!Finite(support(fewerThanFourConvex)) && !Finite(support(coplanarConvex)) &&
			!Finite(support(nonFiniteConvex)),
			"invalid convex support requests return a non-finite sentinel without indexing storage");

		GEngine::ShapeConvex validConvex(UnitBoxPoints());
		const std::uint64_t originalRevision = validConvex.GetRevision();
		const std::size_t originalPointCount = validConvex.GetPoints().size();
		const GEngine::Vec3f originalCenter = validConvex.GetCenterOfMass();
		const GEngine::Mat3 originalInertia = validConvex.InertiaTensor();
		const GEngine::Vec3f originalSupport = support(validConvex);
		Expect(validConvex.IsValid() && originalPointCount >= 4 && Finite(originalCenter) &&
			Finite(originalSupport) && Near(originalSupport.x, 1.0f, 1.0e-4f) &&
			Near(originalInertia, originalInertia),
			"finite three-dimensional convex geometry builds valid finite derived data");

		validConvex.Build(BoxPoints(GEngine::Vec3f(2.0f, 1.5f, 0.75f)));
		const std::uint64_t successfulRebuildRevision = validConvex.GetRevision();
		Expect(validConvex.IsValid() && successfulRebuildRevision == originalRevision + 1 &&
			Near(support(validConvex).x, 2.0f, 1.0e-4f),
			"one successful valid rebuild increments geometry revision exactly once");

		const std::size_t rebuiltPointCount = validConvex.GetPoints().size();
		const GEngine::Vec3f rebuiltCenter = validConvex.GetCenterOfMass();
		const GEngine::Mat3 rebuiltInertia = validConvex.InertiaTensor();
		const GEngine::Bounds rebuiltBounds = validConvex.GetBounds();
		const GEngine::Vec3f rebuiltSupport = support(validConvex);
		validConvex.Build(coplanarPoints);
		validConvex.Build(nonFinitePoints);
		Expect(validConvex.IsValid() && validConvex.GetRevision() == successfulRebuildRevision &&
			validConvex.GetPoints().size() == rebuiltPointCount &&
			Near(validConvex.GetCenterOfMass(), rebuiltCenter) &&
			Near(validConvex.InertiaTensor(), rebuiltInertia) &&
			Near(validConvex.GetBounds().mins, rebuiltBounds.mins) &&
			Near(validConvex.GetBounds().maxs, rebuiltBounds.maxs) &&
			Near(support(validConvex), rebuiltSupport),
			"subsequent rejected rebuilds preserve valid state and leave revision unchanged");

		Expect(!Finite(support(validConvex, GEngine::Vec3f(nan, 0.0f, 0.0f))),
			"non-finite convex support input fails safely");
	}

	void TestContactPairOrderDiagnostic()
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::RigidBody3D bodyA;
		GEngine::RigidBody3D bodyB;
		ConfigureSphereBody(bodyA, sphere, GEngine::Vec3f(0.0f));
		ConfigureSphereBody(bodyB, sphere, GEngine::Vec3f(2.0f, 0.0f, 0.0f));

		GEngine::contact_t direct{};
		direct.m_BodyA = &bodyA;
		direct.m_BodyB = &bodyB;
		direct.ptOnA_LocalSpace = GEngine::Vec3f(1.0f, 0.0f, 0.0f);
		direct.ptOnB_LocalSpace = GEngine::Vec3f(-1.0f, 0.0f, 0.0f);
		direct.ptOnA_WorldSpace = bodyA.BodySpaceToWorldSpace(direct.ptOnA_LocalSpace);
		direct.ptOnB_WorldSpace = bodyB.BodySpaceToWorldSpace(direct.ptOnB_LocalSpace);
		direct.normal = GEngine::Vec3f(-1.0f, 0.0f, 0.0f);

		GEngine::contact_t reversed{};
		reversed.m_BodyA = &bodyB;
		reversed.m_BodyB = &bodyA;
		reversed.ptOnA_LocalSpace = GEngine::Vec3f(-1.0f, 0.5f, 0.0f);
		reversed.ptOnB_LocalSpace = GEngine::Vec3f(1.0f, 0.5f, 0.0f);
		reversed.ptOnA_WorldSpace = bodyB.BodySpaceToWorldSpace(reversed.ptOnA_LocalSpace);
		reversed.ptOnB_WorldSpace = bodyA.BodySpaceToWorldSpace(reversed.ptOnB_LocalSpace);
		reversed.normal = GEngine::Vec3f(1.0f, 0.0f, 0.0f);

		GEngine::ManifoldCollector manifolds;
		manifolds.AddContact(direct);
		manifolds.AddContact(reversed);
		const GEngine::contact_t stored = manifolds.m_Manifolds[0].GetContact(1);
		const float canonicalNormalDot = glm::dot(direct.normal, stored.normal);
		std::cout << "BASELINE contact_pair_order_normal_dot=" << canonicalNormalDot << '\n';
		KnownIssueDiagnostic(canonicalNormalDot > 0.999f,
			"reordered contact preserves the canonical manifold normal direction");
	}

	int RunUnsafeBodyRemovalProbe()
	{
		TestBodyRemovalLifetimeRegression();
		TestMultiManifoldBodyRemovalRegression();
		TestTransientContactBodyRemovalRegression();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused body-removal checks failed\n";
			return 1;
		}

		std::cout << "Body-removal regression: " << testCount << " checks passed\n";
		return 0;
	}

	void TestStableBodyIdentityRegression()
	{
		GEngine::PhysicsWorld world;
		GEngine::RigidBody3D stackBody;
		Expect(!stackBody.GetIdentity().IsValid(),
			"bodies outside world ownership do not impersonate world-managed identities");

		GEngine::RigidBody3D* firstBody = world.CreateRigidBody3D();
		GEngine::RigidBody3D* secondBody = world.CreateRigidBody3D();
		const GEngine::RigidBodyIdentity firstIdentity = firstBody->GetIdentity();
		const GEngine::RigidBodyIdentity secondIdentity = secondBody->GetIdentity();
		Expect(firstIdentity.IsValid() && secondIdentity.IsValid() && firstIdentity != secondIdentity &&
			world.IsBodyIdentityValid(firstIdentity) && world.IsBodyIdentityValid(secondIdentity),
			"each live world body receives a valid unique identity");

		const GEngine::RigidBodyIdentity stableSecondIdentity = secondBody->GetIdentity();
		for (int index = 0; index < 32; ++index)
		{
			world.CreateRigidBody3D();
		}
		Expect(secondBody->GetIdentity() == stableSecondIdentity &&
			world.IsBodyIdentityValid(stableSecondIdentity),
			"body identity remains stable when owning storage grows");

		world.RemoveRigidBody3D(firstBody);
		Expect(!world.IsBodyIdentityValid(firstIdentity) &&
			world.IsBodyIdentityValid(stableSecondIdentity),
			"body removal invalidates only the removed identity");

		GEngine::RigidBody3D* replacementBody = world.CreateRigidBody3D();
		const GEngine::RigidBodyIdentity replacementIdentity = replacementBody->GetIdentity();
		Expect(replacementIdentity.IsValid() && world.IsBodyIdentityValid(replacementIdentity) &&
			replacementIdentity.GetSlot() == firstIdentity.GetSlot() &&
			replacementIdentity.GetGeneration() != firstIdentity.GetGeneration() &&
			replacementIdentity != firstIdentity && !world.IsBodyIdentityValid(firstIdentity),
			"a reused identity slot receives a new generation and cannot impersonate the removed body");

		GEngine::PhysicsWorld otherWorld;
		GEngine::RigidBody3D* otherBody = otherWorld.CreateRigidBody3D();
		Expect(otherBody->GetIdentity() != replacementIdentity &&
			!otherWorld.IsBodyIdentityValid(replacementIdentity) &&
			!world.IsBodyIdentityValid(otherBody->GetIdentity()),
			"body generations remain unique and world-scoped across simultaneous worlds");
	}

	int RunBodyIdentityRegression()
	{
		TestStableBodyIdentityRegression();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused body-identity checks failed\n";
			return 1;
		}

		std::cout << "Body-identity regression: " << testCount << " checks passed\n";
		return 0;
	}

	void TestReadOnlyPhysicsBodyStorageRegression()
	{
		GEngine::PhysicsWorld world;
		using PhysicsBodyView = decltype(world.GetPhysicsBodies());
		using PhysicsBodyReference = decltype(world.GetPhysicsBodies()[0]);
		static_assert(std::is_same_v<PhysicsBodyView, const std::vector<GEngine::RigidBody3D*>&>,
			"physics body storage must be exposed only through a const collection reference");
		static_assert(std::is_same_v<PhysicsBodyReference, GEngine::RigidBody3D* const&> &&
			!std::is_assignable_v<PhysicsBodyReference, GEngine::RigidBody3D*>,
			"clients must not be able to replace pointers in the owning body collection");

		GEngine::RigidBody3D* firstBody = world.CreateRigidBody3D();
		GEngine::RigidBody3D* secondBody = world.CreateRigidBody3D();
		const auto& bodies = world.GetPhysicsBodies();
		Expect(bodies.size() == 2 && bodies[0] == firstBody && bodies[1] == secondBody,
			"read-only body storage preserves deterministic creation order");

		world.RemoveRigidBody3D(firstBody);
		Expect(bodies.size() == 1 && bodies[0] == secondBody,
			"validated removal remains visible through the read-only body collection");

		GEngine::RigidBody3D* replacementBody = world.CreateRigidBody3D();
		Expect(bodies.size() == 2 && bodies[0] == secondBody && bodies[1] == replacementBody,
			"validated creation remains visible without exposing container mutation");
	}

	int RunPhysicsBodyStorageRegression()
	{
		TestReadOnlyPhysicsBodyStorageRegression();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused body-storage checks failed\n";
			return 1;
		}

		std::cout << "Body-storage regression: " << testCount << " checks passed\n";
		return 0;
	}

	bool HasResetTransientState(GEngine::PhysicsSystem& system)
	{
		const GEngine::BroadphaseStats& stats = GetBroadphase(system).GetLastStats();
		return GetManifolds(system).m_Manifolds.empty() && GetCollisionPairs(system).empty() &&
			GetTransientContacts(system).empty() && stats.axisOverlapCount == 0 &&
			stats.aabbRejectedCount == 0 && stats.staticPairRejectedCount == 0 &&
			stats.maskRejectedCount == 0 && stats.insertionSortSwapCount == 0 &&
			stats.fullSortCount == 0;
	}

	void PopulateCollidingWorld(GEngine::PhysicsSystem& system, GEngine::PhysicsWorld& world,
		GEngine::ShapeSphere& sphere, float positionOffset)
	{
		GEngine::RigidBody3D* bodyA = world.CreateRigidBody3D();
		GEngine::RigidBody3D* bodyB = world.CreateRigidBody3D();
		ConfigureSphereBody(*bodyA, sphere, GEngine::Vec3f(positionOffset, 0.0f, 0.0f));
		ConfigureSphereBody(*bodyB, sphere, GEngine::Vec3f(positionOffset + 2.0f, 0.0f, 0.0f));
		system.Update(GEngine::Timestep(1.0f / 120.0f));

		const bool hasManifold = GetManifolds(system).m_Manifolds.size() == 1 &&
			GetManifolds(system).GetContactCount() == 1;
		Expect(hasManifold && !GetCollisionPairs(system).empty() &&
			GetBroadphase(system).GetLastStats().fullSortCount == 1 &&
			bodyA->HasFiniteState() && bodyB->HasFiniteState(),
			"restart fixture creates a finite active manifold and broad-phase pair");

		if (hasManifold)
		{
			GEngine::contact_t transientContact = GetManifolds(system).m_Manifolds[0].GetContact(0);
			transientContact.timeOfImpact = 0.5f;
			GetTransientContacts(system).push_back(transientContact);
		}
		Expect(GetTransientContacts(system).size() == 1,
			"restart fixture contains transient contact state before reset");
	}

	void TestPhysicsWorldResetAndRestartRegression()
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::PhysicsSystem system;

		auto* firstWorld = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f));
		system.SetPhysicsWorld(firstWorld);
		PopulateCollidingWorld(system, *firstWorld, sphere, 0.0f);
		system.OnExit();
		Expect(system.GetPhysicsWorld() == nullptr && HasResetTransientState(system),
			"physics stop clears the world, manifolds, broad phase, pairs, and contacts");

		system.OnExit();
		system.Update(GEngine::Timestep(1.0f / 120.0f));
		Expect(system.GetPhysicsWorld() == nullptr && HasResetTransientState(system),
			"repeated physics stop and a stopped update are safe no-ops");

		auto* secondWorld = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f));
		system.SetPhysicsWorld(secondWorld);
		PopulateCollidingWorld(system, *secondWorld, sphere, 10.0f);
		system.SetPhysicsWorld(secondWorld);
		Expect(system.GetPhysicsWorld() == secondWorld && secondWorld->GetPhysicsBodies().size() == 2 &&
			GetManifolds(system).m_Manifolds.size() == 1,
			"setting the active world again preserves the live world and its state");

		auto* replacementWorld = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f));
		system.SetPhysicsWorld(replacementWorld);
		Expect(system.GetPhysicsWorld() == replacementWorld && HasResetTransientState(system),
			"world replacement releases prior ownership and clears all prior-world state");
		PopulateCollidingWorld(system, *replacementWorld, sphere, 20.0f);
		system.OnExit();
		Expect(system.GetPhysicsWorld() == nullptr && HasResetTransientState(system),
			"restarted physics can collide and stop cleanly again");
	}

	void TestSceneRuntimeLifecycleRegression()
	{
		{
			GEngine::_Scene scene;
			GEngine::_Entity bodyEntityA = scene.CreateEntity("runtime sphere A");
			GEngine::_Entity bodyEntityB = scene.CreateEntity("runtime sphere B");
			bodyEntityA.AddOrReplaceComponent<GEngine::Component::RigidBody3DComponent>().Type =
				GEngine::Component::BodyType::Dynamic;
			bodyEntityB.AddOrReplaceComponent<GEngine::Component::RigidBody3DComponent>().Type =
				GEngine::Component::BodyType::Dynamic;

			GEngine::Component::SphereFixture3DComponent fixtureA;
			GEngine::Component::SphereFixture3DComponent fixtureB;
			fixtureA.Property.m_Position = GEngine::Vec3f(0.0f, 0.0f, 0.0f);
			fixtureB.Property.m_Position = GEngine::Vec3f(2.0f, 0.0f, 0.0f);
			bodyEntityA.AddOrReplaceComponent<GEngine::Component::SphereFixture3DComponent>(fixtureA);
			bodyEntityB.AddOrReplaceComponent<GEngine::Component::SphereFixture3DComponent>(fixtureB);

			scene.OnRuntimeStart();
			GEngine::RigidBody3D* firstBodyA =
				bodyEntityA.GetComponent<GEngine::Component::RigidBody3DComponent>().RuntimeBody;
			GEngine::RigidBody3D* firstBodyB =
				bodyEntityB.GetComponent<GEngine::Component::RigidBody3DComponent>().RuntimeBody;
			Expect(scene.IsRunning() && scene.GetPhysicsSystem()->GetPhysicsWorld() != nullptr &&
				firstBodyA != nullptr && firstBodyB != nullptr && firstBodyA->m_Shape != nullptr &&
				firstBodyB->m_Shape != nullptr,
				"scene runtime start creates valid runtime body links");

			scene.Update(GEngine::Timestep(1.0f / 120.0f));
			Expect(firstBodyA && firstBodyB && firstBodyA->HasFiniteState() && firstBodyB->HasFiniteState(),
				"scene runtime bodies remain finite after the first update");

			scene.OnRuntimeStop();
			Expect(!scene.IsRunning() && scene.GetPhysicsSystem()->GetPhysicsWorld() == nullptr &&
				bodyEntityA.GetComponent<GEngine::Component::RigidBody3DComponent>().RuntimeBody == nullptr &&
				bodyEntityB.GetComponent<GEngine::Component::RigidBody3DComponent>().RuntimeBody == nullptr,
				"scene runtime stop clears all runtime body links and shuts down the physics world");

			scene.OnRuntimeStop();
			Expect(!scene.IsRunning() && scene.GetPhysicsSystem()->GetPhysicsWorld() == nullptr &&
				bodyEntityA.GetComponent<GEngine::Component::RigidBody3DComponent>().RuntimeBody == nullptr &&
				bodyEntityB.GetComponent<GEngine::Component::RigidBody3DComponent>().RuntimeBody == nullptr,
				"repeated scene runtime stop is safe");

			scene.Update(GEngine::Timestep(1.0f / 120.0f));
			Expect(scene.GetPhysicsSystem()->GetPhysicsWorld() == nullptr &&
				bodyEntityA.GetComponent<GEngine::Component::RigidBody3DComponent>().RuntimeBody == nullptr &&
				bodyEntityB.GetComponent<GEngine::Component::RigidBody3DComponent>().RuntimeBody == nullptr,
				"updating the stopped scene does not restore or dereference cleared runtime bodies");

			scene.OnRuntimeStart();
			GEngine::RigidBody3D* restartedBodyA =
				bodyEntityA.GetComponent<GEngine::Component::RigidBody3DComponent>().RuntimeBody;
			GEngine::RigidBody3D* restartedBodyB =
				bodyEntityB.GetComponent<GEngine::Component::RigidBody3DComponent>().RuntimeBody;
			Expect(scene.IsRunning() && scene.GetPhysicsSystem()->GetPhysicsWorld() != nullptr &&
				restartedBodyA != nullptr && restartedBodyB != nullptr && restartedBodyA->m_Shape != nullptr &&
				restartedBodyB->m_Shape != nullptr,
				"scene runtime restart recreates valid runtime bodies");

			scene.Update(GEngine::Timestep(1.0f / 120.0f));
			Expect(restartedBodyA && restartedBodyB && restartedBodyA->HasFiniteState() &&
				restartedBodyB->HasFiniteState(),
				"restarted scene runtime bodies remain finite after another update");

			scene.OnRuntimeStop();
			Expect(!scene.IsRunning() && scene.GetPhysicsSystem()->GetPhysicsWorld() == nullptr &&
				bodyEntityA.GetComponent<GEngine::Component::RigidBody3DComponent>().RuntimeBody == nullptr &&
				bodyEntityB.GetComponent<GEngine::Component::RigidBody3DComponent>().RuntimeBody == nullptr,
				"restarted scene stops cleanly before destruction");
		}

		Expect(true, "stopped scene destruction completes safely");
	}

	int RunUnsafeWorldRestartProbe()
	{
		TestPhysicsWorldResetAndRestartRegression();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused world-restart checks failed\n";
			return 1;
		}

		std::cout << "World-restart regression: " << testCount << " checks passed\n";
		return 0;
	}

	int RunSceneRuntimeLifecycleRegression()
	{
		TestSceneRuntimeLifecycleRegression();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused scene lifecycle checks failed\n";
			return 1;
		}

		std::cout << "Scene runtime lifecycle regression: " << testCount << " checks passed\n";
		return 0;
	}

	int RunConvexValidityRegression()
	{
		TestConvexValidityContract();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused convex-validity checks failed\n";
			return 1;
		}

		std::cout << "Convex-validity regression: " << testCount << " checks passed\n";
		return 0;
	}

	int RunUnsafeConvexSupportProbe(bool degenerate)
	{
		const std::vector<GEngine::Vec3f> points = degenerate
			? std::vector<GEngine::Vec3f>{
				GEngine::Vec3f(0.0f, 0.0f, 0.0f),
				GEngine::Vec3f(1.0f, 0.0f, 0.0f),
				GEngine::Vec3f(0.0f, 1.0f, 0.0f) }
			: std::vector<GEngine::Vec3f>{};
		GEngine::ShapeConvex convex(points);
		std::cout << "UNSAFE_PROBE " << (degenerate ? "degenerate" : "empty")
			<< "_convex_support indexing empty hull" << std::endl;
		const GEngine::Vec3f support = convex.Support(
			GEngine::Vec3f(1.0f, 0.0f, 0.0f), GEngine::Vec3f(0.0f),
			GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f), 0.0f);
		std::cout << "UNSAFE_PROBE convex_support=" << support.x << ',' << support.y << ',' << support.z << std::endl;
		return 0;
	}

	void TestBoxConstructionInvariant()
	{
		bool rejectedEmptyGeometry = false;
		try
		{
			GEngine::ShapeBox invalidBox(std::vector<GEngine::Vec3f>{});
		}
		catch (const std::invalid_argument&)
		{
			rejectedEmptyGeometry = true;
		}
		Expect(rejectedEmptyGeometry, "empty box geometry is rejected before support mapping");

		GEngine::ShapeBox box(UnitBoxPoints());
		const GEngine::Vec3f supportBefore = box.Support(GEngine::Vec3f(1.0f, 0.0f, 0.0f),
			GEngine::Vec3f(0.0f), GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f), 0.0f);
		box.Build({});
		const GEngine::Vec3f supportAfter = box.Support(GEngine::Vec3f(1.0f, 0.0f, 0.0f),
			GEngine::Vec3f(0.0f), GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f), 0.0f);
		Expect(box.IsValid() && Near(supportAfter, supportBefore),
			"failed box rebuild preserves the previous valid support geometry");
	}

	void ExpectBoxContact(GEngine::RigidBody3D& bodyA, GEngine::RigidBody3D& bodyB,
		std::string_view description)
	{
		GEngine::contact_t contact{};
		const bool intersects = GEngine::Collision::Intersect(&bodyA, &bodyB, contact);
		Expect(intersects, description);
		Expect(intersects && Finite(contact) && Near(glm::length(contact.normal), 1.0f, 1.0e-3f),
			"box contact data is finite and normalized");
	}

	void TestBoxContactRegression()
	{
		GEngine::ShapeBox box(UnitBoxPoints());
		GEngine::ShapeBox floor(BoxPoints(GEngine::Vec3f(5.0f, 0.5f, 5.0f)));
		GEngine::RigidBody3D bodyA;
		GEngine::RigidBody3D bodyB;

		ConfigureBoxBody(bodyA, box, GEngine::Vec3f(0.0f, 1.0f, 0.0f),
			GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f));
		ConfigureBoxBody(bodyB, floor, GEngine::Vec3f(0.0f, -0.5f, 0.0f),
			GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f));
		bodyB.Type = GEngine::Component::BodyType::Static;
		bodyB.m_InvMass = 0.0f;
		ExpectBoxContact(bodyA, bodyB, "dynamic box initially touching a static floor intersects");

		ConfigureBoxBody(bodyA, box, GEngine::Vec3f(0.0f),
			GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f));
		ConfigureBoxBody(bodyB, box, GEngine::Vec3f(2.0f, 0.0f, 0.0f),
			GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f));
		ExpectBoxContact(bodyA, bodyB, "boxes touching face-to-face intersect");

		bodyB.m_Position = GEngine::Vec3f(1.99f, 0.0f, 0.0f);
		ExpectBoxContact(bodyA, bodyB, "slightly penetrating boxes intersect");

		bodyB.m_Position = GEngine::Vec3f(2.01f, 0.0f, 0.0f);
		GEngine::contact_t separatedContact{};
		Expect(!GEngine::Collision::Intersect(&bodyA, &bodyB, separatedContact),
			"separated boxes remain separated");
		Expect(Finite(separatedContact), "separated box closest points remain finite");

		bodyB.m_Position = GEngine::Vec3f(2.2f, 0.0f, 0.0f);
		bodyB.m_Orientation = glm::angleAxis(0.78539816339f, GEngine::Vec3f(0.0f, 0.0f, 1.0f));
		ExpectBoxContact(bodyA, bodyB, "rotated boxes in contact intersect");
	}

	void TestSmallBoxStackRegression()
	{
		GEngine::ShapeBox box(UnitBoxPoints());
		GEngine::ShapeBox floor(BoxPoints(GEngine::Vec3f(5.0f, 0.5f, 5.0f)));
		GEngine::PhysicsSystem physics;
		auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f, -12.0f, 0.0f));
		physics.SetPhysicsWorld(world);

		GEngine::RigidBody3D* floorBody = world->CreateRigidBody3D();
		ConfigureBoxBody(*floorBody, floor, GEngine::Vec3f(0.0f, -0.5f, 0.0f),
			GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f));
		floorBody->Type = GEngine::Component::BodyType::Static;
		floorBody->m_InvMass = 0.0f;

		std::array<GEngine::RigidBody3D*, 4> boxes{};
		for (std::size_t index = 0; index < boxes.size(); ++index)
		{
			boxes[index] = world->CreateRigidBody3D();
			ConfigureBoxBody(*boxes[index], box,
				GEngine::Vec3f(0.0f, 1.0f + 2.0f * static_cast<float>(index), 0.0f),
				GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f));
		}

		for (int step = 0; step < 240; ++step)
		{
			physics.Update(GEngine::Timestep(1.0f / 120.0f));
		}

		bool finiteStack = floorBody->HasFiniteState();
		for (const GEngine::RigidBody3D* body : boxes)
		{
			finiteStack = finiteStack && body->HasFiniteState() && body->m_Position.y > -10.0f;
		}
		Expect(finiteStack, "small exact-contact box stack remains finite and above the fall-through bound");
	}

	void TestGoldenRotations()
	{
		constexpr float halfPi = 1.57079632679489661923f;
		GEngine::ShapeBox box(UnitBoxPoints());
		GEngine::RigidBody3D body;
		const GEngine::Vec3f position(3.0f, -2.0f, 5.0f);
		const GEngine::Vec3f localPoint(0.25f, -0.5f, 0.75f);
		ConfigureBoxBody(body, box, position,
			glm::angleAxis(0.63f, glm::normalize(GEngine::Vec3f(1.0f, 2.0f, -0.5f))));

		const GEngine::Vec3f worldPoint = body.BodySpaceToWorldSpace(localPoint);
		Expect(Near(body.WorldSpaceToBodySpace(worldPoint), localPoint),
			"local to world to local transform round trip is stable");
		const GEngine::Vec3f arbitraryWorld(-4.0f, 6.0f, 1.5f);
		Expect(Near(body.BodySpaceToWorldSpace(body.WorldSpaceToBodySpace(arbitraryWorld)), arbitraryWorld),
			"world to local to world transform round trip is stable");

		body.m_Position = GEngine::Vec3f(0.0f);
		body.m_Orientation = glm::angleAxis(halfPi, GEngine::Vec3f(1.0f, 0.0f, 0.0f));
		Expect(Near(body.BodySpaceToWorldSpace(GEngine::Vec3f(0.0f, 1.0f, 0.0f)),
			GEngine::Vec3f(0.0f, 0.0f, 1.0f)), "positive 90 degree X rotation follows the render convention");
		body.m_Orientation = glm::angleAxis(halfPi, GEngine::Vec3f(0.0f, 1.0f, 0.0f));
		Expect(Near(body.BodySpaceToWorldSpace(GEngine::Vec3f(0.0f, 0.0f, 1.0f)),
			GEngine::Vec3f(1.0f, 0.0f, 0.0f)), "positive 90 degree Y rotation follows the render convention");
		body.m_Orientation = glm::angleAxis(halfPi, GEngine::Vec3f(0.0f, 0.0f, 1.0f));
		Expect(Near(body.BodySpaceToWorldSpace(GEngine::Vec3f(1.0f, 0.0f, 0.0f)),
			GEngine::Vec3f(0.0f, 1.0f, 0.0f)), "positive 90 degree Z rotation follows the render convention");
	}

	void TestRotatedAsymmetricBox()
	{
		constexpr float halfPi = 1.57079632679489661923f;
		const std::vector<GEngine::Vec3f> points = {
			{ -2.0f, -1.0f, -0.5f }, { 2.0f, -1.0f, -0.5f },
			{ -2.0f,  1.0f, -0.5f }, { 2.0f,  1.0f, -0.5f },
			{ -2.0f, -1.0f,  0.5f }, { 2.0f, -1.0f,  0.5f },
			{ -2.0f,  1.0f,  0.5f }, { 2.0f,  1.0f,  0.5f }
		};
		GEngine::ShapeBox box(points);
		const GEngine::Quat rotation = glm::angleAxis(halfPi, GEngine::Vec3f(0.0f, 0.0f, 1.0f));
		GEngine::RigidBody3D body;
		ConfigureBoxBody(body, box, GEngine::Vec3f(0.0f), rotation);

		const GEngine::Vec3f support = box.Support(GEngine::Vec3f(0.0f, 1.0f, 0.0f),
			body.m_Position, body.m_Orientation, 0.0f);
		Expect(Near(glm::dot(support, GEngine::Vec3f(0.0f, 1.0f, 0.0f)), 2.0f),
			"rotated asymmetric box support uses the positive quaternion rotation");
		const GEngine::Bounds& bounds = body.GetWorldBounds();
		Expect(Near(bounds.WidthX(), 2.0f) && Near(bounds.WidthY(), 4.0f) && Near(bounds.WidthZ(), 1.0f),
			"rotated asymmetric box cached world AABB matches support space");

		body.m_Orientation = glm::angleAxis(0.61f,
			glm::normalize(GEngine::Vec3f(0.25f, 1.0f, -0.4f)));
		const GEngine::Mat3 bodyInverseInertia = body.GetInverseInertiaTensorBodySpace();
		const GEngine::Mat3 rotationMatrix = glm::toMat3(GEngine::Math::NormalizeOrIdentity(body.m_Orientation));
		const GEngine::Mat3 expectedWorldInverseInertia =
			rotationMatrix * bodyInverseInertia * glm::transpose(rotationMatrix);
		Expect(Near(body.GetInverseInertiaTensorWorldSpace(), expectedWorldInverseInertia),
			"rotated inverse inertia uses body to world to body transpose order");
	}

	void TestDerivedDataInvalidation()
	{
		const std::vector<GEngine::Vec3f> offsetPoints = {
			{ 0.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f }, { 0.0f, 4.0f, 0.0f }, { 2.0f, 4.0f, 0.0f },
			{ 0.0f, 0.0f, 6.0f }, { 2.0f, 0.0f, 6.0f }, { 0.0f, 4.0f, 6.0f }, { 2.0f, 4.0f, 6.0f }
		};
		GEngine::ShapeBox box(offsetPoints);
		GEngine::RigidBody3D body;
		ConfigureBoxBody(body, box, GEngine::Vec3f(10.0f, 20.0f, 30.0f),
			glm::angleAxis(1.57079632679489661923f, GEngine::Vec3f(0.0f, 0.0f, 1.0f)));

		Expect(Near(body.GetCenterOfMassWorldSpace(), GEngine::Vec3f(8.0f, 21.0f, 33.0f)),
			"cached center of mass includes the conventional rotated local center");
		const GEngine::Bounds initialBounds = body.GetWorldBounds();
		Expect(Near(body.GetWorldBounds().mins, initialBounds.mins) && Near(body.GetWorldBounds().maxs, initialBounds.maxs),
			"repeated cached world AABB queries remain identical");

		body.m_Position += GEngine::Vec3f(5.0f, -3.0f, 2.0f);
		Expect(Near(body.GetCenterOfMassWorldSpace(), GEngine::Vec3f(13.0f, 18.0f, 35.0f)),
			"direct position changes invalidate cached transforms");
		body.m_InvMass = 0.0f;
		Expect(Near(body.GetInverseInertiaTensorWorldSpace(), GEngine::Mat3(0.0f)),
			"inverse mass changes invalidate cached world inverse inertia");

		std::vector<GEngine::Vec3f> rebuiltPoints = offsetPoints;
		for (GEngine::Vec3f& point : rebuiltPoints)
		{
			point.x *= 2.0f;
		}
		box.Build(rebuiltPoints);
		Expect(Near(body.GetCenterOfMassWorldSpace(), GEngine::Vec3f(13.0f, 19.0f, 35.0f)),
			"shape revision invalidates cached center of mass after geometry rebuild");

		body.m_InvMass = 1.0f;
		body.m_LinearVelocity = GEngine::Vec3f(0.0f);
		body.m_AngularVelocity = GEngine::Vec3f(0.0f, 0.0f, 1.0f);
		const GEngine::Vec3f centerBeforeRotation = body.GetCenterOfMassWorldSpace();
		body.Update(0.1f);
		Expect(Near(body.GetCenterOfMassWorldSpace(), centerBeforeRotation),
			"body integration preserves an offset center of mass while rotating");
	}

	void TestWarmCacheOrientationInvalidationAndReuse()
	{
		constexpr float halfPi = 1.57079632679489661923f;
		CountingShape shape;
		GEngine::Mat3 inertia(0.0f);
		inertia[0][0] = 2.0f;
		inertia[1][1] = 4.0f;
		inertia[2][2] = 8.0f;
		shape.SetGeometry(MakeBounds(
			GEngine::Vec3f(-2.0f, -1.0f, -0.5f), GEngine::Vec3f(2.0f, 1.0f, 0.5f)),
			GEngine::Vec3f(1.0f, 2.0f, 3.0f), inertia);

		GEngine::RigidBody3D body;
		body.m_Shape = &shape;
		body.m_Position = GEngine::Vec3f(10.0f, 20.0f, 30.0f);
		body.m_Orientation = GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f);
		body.m_InvMass = 0.5f;
		body.Type = GEngine::Component::BodyType::Dynamic;

		body.GetBodyToWorldRotation();
		body.GetWorldToBodyRotation();
		body.GetWorldBounds();
		const GEngine::Mat3 bodyInverseInertia = body.GetInverseInertiaTensorBodySpace();
		body.GetInverseInertiaTensorWorldSpace();
		body.GetCenterOfMassWorldSpace();
		Expect(shape.GetWorldBoundsQueryCount() == 1 && shape.GetInertiaQueryCount() == 1,
			"initial derived-data queries populate expensive shape caches exactly once");

		body.GetBodyToWorldRotation();
		body.GetWorldToBodyRotation();
		body.GetWorldBounds();
		body.GetInverseInertiaTensorBodySpace();
		body.GetInverseInertiaTensorWorldSpace();
		body.GetCenterOfMassWorldSpace();
		Expect(shape.GetWorldBoundsQueryCount() == 1 && shape.GetInertiaQueryCount() == 1,
			"unchanged warm-cache queries reuse bounds and inertia without recomputation");

		body.m_Orientation = glm::angleAxis(halfPi, GEngine::Vec3f(0.0f, 0.0f, 1.0f));
		const GEngine::Mat3& bodyToWorld = body.GetBodyToWorldRotation();
		const GEngine::Mat3& worldToBody = body.GetWorldToBodyRotation();
		const GEngine::Bounds& rotatedBounds = body.GetWorldBounds();
		const GEngine::Mat3 rotatedWorldInverseInertia = body.GetInverseInertiaTensorWorldSpace();
		const GEngine::Vec3f rotatedCenter = body.GetCenterOfMassWorldSpace();
		const GEngine::Mat3 expectedWorldInverseInertia =
			bodyToWorld * bodyInverseInertia * worldToBody;

		Expect(Near(bodyToWorld * GEngine::Vec3f(1.0f, 0.0f, 0.0f), GEngine::Vec3f(0.0f, 1.0f, 0.0f)) &&
			Near(worldToBody * GEngine::Vec3f(0.0f, 1.0f, 0.0f), GEngine::Vec3f(1.0f, 0.0f, 0.0f)),
			"direct orientation mutation refreshes warm body/world rotation caches");
		Expect(Near(rotatedBounds.WidthX(), 2.0f) && Near(rotatedBounds.WidthY(), 4.0f) &&
			Near(rotatedBounds.WidthZ(), 1.0f),
			"direct orientation mutation refreshes the warm rotated world AABB");
		Expect(Near(rotatedWorldInverseInertia, expectedWorldInverseInertia),
			"direct orientation mutation refreshes warm world inverse inertia");
		Expect(Near(rotatedCenter, GEngine::Vec3f(8.0f, 21.0f, 33.0f)),
			"direct orientation mutation refreshes the warm offset world center of mass");
		Expect(shape.GetWorldBoundsQueryCount() == 2 && shape.GetInertiaQueryCount() == 1,
			"orientation mutation refreshes world bounds but reuses unchanged body inertia");

		body.GetWorldBounds();
		body.GetInverseInertiaTensorWorldSpace();
		body.GetCenterOfMassWorldSpace();
		Expect(shape.GetWorldBoundsQueryCount() == 2 && shape.GetInertiaQueryCount() == 1,
			"post-orientation warm queries perform no additional expensive shape work");

		GEngine::Mat3 rebuiltInertia(0.0f);
		rebuiltInertia[0][0] = 3.0f;
		rebuiltInertia[1][1] = 5.0f;
		rebuiltInertia[2][2] = 9.0f;
		shape.SetGeometry(MakeBounds(
			GEngine::Vec3f(-3.0f, -1.0f, -0.5f), GEngine::Vec3f(3.0f, 1.0f, 0.5f)),
			GEngine::Vec3f(2.0f, 3.0f, 4.0f), rebuiltInertia);
		body.GetWorldBounds();
		body.GetInverseInertiaTensorWorldSpace();
		const GEngine::Vec3f rebuiltCenter = body.GetCenterOfMassWorldSpace();
		Expect(shape.GetWorldBoundsQueryCount() == 3 && shape.GetInertiaQueryCount() == 2 &&
			Near(rebuiltCenter, GEngine::Vec3f(7.0f, 22.0f, 34.0f)),
			"geometry revision causes exactly one required bounds/inertia/center refresh");
	}

	void TestSphereRadiusInvalidationContract()
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::RigidBody3D body;
		ConfigureSphereBody(body, sphere, GEngine::Vec3f(4.0f, 0.0f, 0.0f));
		body.GetWorldBounds();
		body.GetInverseInertiaTensorWorldSpace();

		const std::uint64_t revisionBefore = sphere.GetRevision();
		sphere.SetRadius(2.0f);
		const GEngine::Bounds& bounds = body.GetWorldBounds();
		const GEngine::Mat3 inverseInertia = body.GetInverseInertiaTensorWorldSpace();
		const GEngine::Vec3f support = sphere.Support(
			GEngine::Vec3f(1.0f, 0.0f, 0.0f), body.m_Position, body.m_Orientation, 0.0f);
		Expect(sphere.GetRevision() == revisionBefore + 1 && Near(sphere.GetRadius(), 2.0f) &&
			Near(bounds.WidthX(), 4.0f) && Near(inverseInertia[0][0], 0.625f) && Near(support.x, 6.0f),
			"public sphere radius mutation revises cached bounds, inertia, and support geometry");
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

	bool ContainsPair(const std::vector<GEngine::collisionPair_t>& pairs, int a, int b)
	{
		return std::find(pairs.begin(), pairs.end(), GEngine::collisionPair_t{ a, b }) != pairs.end();
	}

	std::vector<GEngine::collisionPair_t> BruteForceBroadphasePairs(
		const std::vector<GEngine::RigidBody3D*>& bodies, float dtSeconds)
	{
		std::vector<GEngine::Bounds> sweptBounds;
		sweptBounds.reserve(bodies.size());
		for (const GEngine::RigidBody3D* body : bodies)
		{
			GEngine::Bounds bounds = body->GetWorldBounds();
			const GEngine::Vec3f initialMins = bounds.mins;
			const GEngine::Vec3f initialMaxs = bounds.maxs;
			const GEngine::Vec3f displacement = body->m_LinearVelocity * dtSeconds;
			bounds.Expand(initialMins + displacement);
			bounds.Expand(initialMaxs + displacement);
			bounds.Expand(bounds.mins - GEngine::Vec3f(0.01f));
			bounds.Expand(bounds.maxs + GEngine::Vec3f(0.01f));
			sweptBounds.push_back(bounds);
		}

		std::vector<GEngine::collisionPair_t> expected;
		for (std::size_t a = 0; a < bodies.size(); ++a)
		{
			for (std::size_t b = a + 1; b < bodies.size(); ++b)
			{
				const bool staticPair = bodies[a]->Type == GEngine::Component::BodyType::Static &&
					bodies[b]->Type == GEngine::Component::BodyType::Static;
				const bool masksOverlap =
					(bodies[a]->m_CollisionMask & bodies[b]->m_CollisionLayer) != 0u &&
					(bodies[b]->m_CollisionMask & bodies[a]->m_CollisionLayer) != 0u;
				if (!staticPair && masksOverlap && sweptBounds[a].DoesIntersect(sweptBounds[b]))
				{
					expected.push_back({ static_cast<int>(a), static_cast<int>(b) });
				}
			}
		}
		return expected;
	}

	void ConfigureBroadphaseBody(GEngine::RigidBody3D& body, GEngine::ShapeBox& shape,
		const GEngine::Vec3f& position, GEngine::Component::BodyType type = GEngine::Component::BodyType::Dynamic)
	{
		ConfigureBoxBody(body, shape, position, GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f));
		body.Type = type;
		body.m_InvMass = type == GEngine::Component::BodyType::Static ? 0.0f : 1.0f;
	}

	void TestBroadphaseCorrectnessAndFiltering()
	{
		GEngine::ShapeBox box(UnitBoxPoints());
		GEngine::SweepAndPruneBroadphase broadphase;
		std::vector<GEngine::collisionPair_t> pairs;

		std::array<GEngine::RigidBody3D, 2> touchingBodies;
		ConfigureBroadphaseBody(touchingBodies[0], box, GEngine::Vec3f(0.0f));
		ConfigureBroadphaseBody(touchingBodies[1], box, GEngine::Vec3f(2.02f, 0.0f, 0.0f));
		std::vector<GEngine::RigidBody3D*> bodies{ &touchingBodies[0], &touchingBodies[1] };
		broadphase.FindPairs(bodies, pairs, 0.0f);
		Expect(ContainsPair(pairs, 0, 1),
			"equal sweep endpoints retain a touching broadphase candidate");

		touchingBodies[0].Type = GEngine::Component::BodyType::Static;
		touchingBodies[1].Type = GEngine::Component::BodyType::Static;
		broadphase.FindPairs(bodies, pairs, 0.0f);
		Expect(pairs.empty() && broadphase.GetLastStats().staticPairRejectedCount == 1,
			"broadphase rejects static/static pairs before narrowphase");

		touchingBodies[0].Type = GEngine::Component::BodyType::Dynamic;
		touchingBodies[1].Type = GEngine::Component::BodyType::Dynamic;
		touchingBodies[0].m_CollisionLayer = 1u;
		touchingBodies[0].m_CollisionMask = 1u;
		touchingBodies[1].m_CollisionLayer = 2u;
		touchingBodies[1].m_CollisionMask = 1u;
		broadphase.FindPairs(bodies, pairs, 0.0f);
		Expect(pairs.empty() && broadphase.GetLastStats().maskRejectedCount == 1,
			"broadphase requires reciprocal collision layer/mask acceptance");
		touchingBodies[0].m_CollisionMask = 2u;
		broadphase.FindPairs(bodies, pairs, 0.0f);
		Expect(ContainsPair(pairs, 0, 1), "compatible reciprocal collision masks emit a candidate");

		std::array<GEngine::RigidBody3D, 2> diagonalBodies;
		ConfigureBroadphaseBody(diagonalBodies[0], box, GEngine::Vec3f(0.0f));
		ConfigureBroadphaseBody(diagonalBodies[1], box, GEngine::Vec3f(0.0f, 4.0f, 0.0f));
		bodies = { &diagonalBodies[0], &diagonalBodies[1] };
		broadphase.FindPairs(bodies, pairs, 0.0f);
		Expect(pairs.empty() && broadphase.GetLastStats().axisOverlapCount == 1 &&
			broadphase.GetLastStats().aabbRejectedCount == 1,
			"three-axis swept AABB rejection removes a sweep-axis false positive");

		diagonalBodies[1].m_Position = GEngine::Vec3f(4.0f, 0.0f, 0.0f);
		diagonalBodies[1].m_LinearVelocity = GEngine::Vec3f(-300.0f, 0.0f, 0.0f);
		broadphase.FindPairs(bodies, pairs, 0.01f);
		Expect(ContainsPair(pairs, 0, 1), "swept AABB retains a fast-moving collision candidate");
	}

	void TestBroadphasePersistenceAndTemporalCoherence()
	{
		GEngine::ShapeBox box(UnitBoxPoints());
		std::array<GEngine::RigidBody3D, 3> bodyStorage;
		for (std::size_t index = 0; index < bodyStorage.size(); ++index)
		{
			ConfigureBroadphaseBody(bodyStorage[index], box,
				GEngine::Vec3f(static_cast<float>(index) * 5.0f, 0.0f, 0.0f));
		}

		GEngine::SweepAndPruneBroadphase broadphase;
		std::vector<GEngine::collisionPair_t> pairs;
		std::vector<GEngine::RigidBody3D*> bodies{ &bodyStorage[0], &bodyStorage[1] };
		broadphase.FindPairs(bodies, pairs, 0.0f);
		const std::size_t endpointCapacity = broadphase.GetEndpointCapacity();
		const std::size_t activeCapacity = broadphase.GetPairScratchCapacity();
		Expect(broadphase.GetLastStats().fullSortCount == 1 && endpointCapacity >= 4 && activeCapacity >= 2,
			"first broadphase update allocates persistent endpoint and active-set storage");

		broadphase.FindPairs(bodies, pairs, 0.0f);
		Expect(broadphase.GetLastStats().fullSortCount == 0 &&
			broadphase.GetLastStats().insertionSortSwapCount == 0 &&
			broadphase.GetEndpointCapacity() == endpointCapacity &&
			broadphase.GetPairScratchCapacity() == activeCapacity,
			"unchanged bodies reuse capacity and already-sorted endpoint order");

		bodyStorage[1].m_Position = GEngine::Vec3f(-5.0f, 0.0f, 0.0f);
		broadphase.FindPairs(bodies, pairs, 0.0f);
		Expect(broadphase.GetLastStats().fullSortCount == 0 &&
			broadphase.GetLastStats().insertionSortSwapCount > 0,
			"body motion incrementally repairs persistent endpoint order");

		bodies.push_back(&bodyStorage[2]);
		broadphase.FindPairs(bodies, pairs, 0.0f);
		Expect(broadphase.GetLastStats().fullSortCount == 1 && broadphase.GetEndpointCapacity() >= 6,
			"body membership changes safely rebuild persistent endpoints");
	}

	void TestBroadphaseAgainstBruteForce()
	{
		GEngine::ShapeBox box(UnitBoxPoints());
		std::array<GEngine::RigidBody3D, 24> bodyStorage;
		std::vector<GEngine::RigidBody3D*> bodies;
		bodies.reserve(bodyStorage.size());
		for (std::size_t index = 0; index < bodyStorage.size(); ++index)
		{
			ConfigureBroadphaseBody(bodyStorage[index], box, GEngine::Vec3f(
				static_cast<float>(index % 6) * 1.7f,
				static_cast<float>((index / 6) % 2) * 3.5f,
				static_cast<float>(index % 3) * 0.4f),
				index % 5 == 0 ? GEngine::Component::BodyType::Static : GEngine::Component::BodyType::Dynamic);
			bodyStorage[index].m_LinearVelocity.x = static_cast<float>(static_cast<int>(index % 4) - 2) * 2.0f;
			bodyStorage[index].m_CollisionLayer = 1u << (index % 3);
			bodyStorage[index].m_CollisionMask = index % 4 == 0 ? 0x3u : ~std::uint32_t{ 0 };
			bodies.push_back(&bodyStorage[index]);
		}

		constexpr float dt = 1.0f / 60.0f;
		GEngine::SweepAndPruneBroadphase broadphase;
		std::vector<GEngine::collisionPair_t> actual;
		broadphase.FindPairs(bodies, actual, dt);
		const std::vector<GEngine::collisionPair_t> expected = BruteForceBroadphasePairs(bodies, dt);
		bool matches = actual.size() == expected.size();
		for (const GEngine::collisionPair_t& pair : expected)
		{
			matches = matches && ContainsPair(actual, pair.a, pair.b);
		}
		Expect(matches, "sweep-and-prune candidates exactly match filtered swept-AABB brute force");

		for (std::size_t index = 0; index < bodyStorage.size(); ++index)
		{
			bodyStorage[index].m_Position.x += (index % 2 == 0 ? 2.25f : -1.5f);
		}
		broadphase.FindPairs(bodies, actual, dt);
		const std::vector<GEngine::collisionPair_t> movedExpected = BruteForceBroadphasePairs(bodies, dt);
		matches = actual.size() == movedExpected.size();
		for (const GEngine::collisionPair_t& pair : movedExpected)
		{
			matches = matches && ContainsPair(actual, pair.a, pair.b);
		}
		Expect(matches && broadphase.GetLastStats().fullSortCount == 0,
			"incrementally sorted moving candidates match filtered swept-AABB brute force");
	}
}

int main(int argc, char** argv)
{
	GEngine::Log::Initialize();
	if (argc == 2)
	{
		const std::string_view argument(argv[1]);
		if (argument == "--unsafe-body-removal")
		{
			return RunUnsafeBodyRemovalProbe();
		}
		if (argument == "--unsafe-world-restart")
		{
			return RunUnsafeWorldRestartProbe();
		}
		if (argument == "--body-identity")
		{
			return RunBodyIdentityRegression();
		}
		if (argument == "--body-storage")
		{
			return RunPhysicsBodyStorageRegression();
		}
		if (argument == "--scene-runtime-lifecycle")
		{
			return RunSceneRuntimeLifecycleRegression();
		}
		if (argument == "--convex-validity")
		{
			return RunConvexValidityRegression();
		}
		if (argument == "--unsafe-empty-convex")
		{
			return RunUnsafeConvexSupportProbe(false);
		}
		if (argument == "--unsafe-degenerate-convex")
		{
			return RunUnsafeConvexSupportProbe(true);
		}
		std::cerr << "Unknown PhysicsTests argument: " << argument << '\n';
		return 2;
	}
	if (argc != 1)
	{
		std::cerr << "PhysicsTests accepts at most one diagnostic argument\n";
		return 2;
	}

	TestNormalization();
	TestBarycentricAndPointEquality();
	TestLcpPivots();
	TestSphereContacts();
	TestBodyRemovalLifetimeRegression();
	TestMultiManifoldBodyRemovalRegression();
	TestTransientContactBodyRemovalRegression();
	TestStableBodyIdentityRegression();
	TestReadOnlyPhysicsBodyStorageRegression();
	TestPhysicsWorldResetAndRestartRegression();
	TestSceneRuntimeLifecycleRegression();
	TestConvexValidityContract();
	TestContactPairOrderDiagnostic();
	TestDegenerateGjkDirection();
	TestZeroQuaternionBodyUpdate();
	TestBoxConstructionInvariant();
	TestBoxContactRegression();
	TestSmallBoxStackRegression();
	TestGoldenRotations();
	TestRotatedAsymmetricBox();
	TestDerivedDataInvalidation();
	TestWarmCacheOrientationInvalidationAndReuse();
	TestSphereRadiusInvalidationContract();
	TestConstraintDenominators();
	TestGravityAndInverseMass();
	TestBroadphaseCorrectnessAndFiltering();
	TestBroadphasePersistenceAndTemporalCoherence();
	TestBroadphaseAgainstBruteForce();

	if (failureCount != 0)
	{
		std::cerr << failureCount << " of " << testCount << " checks failed\n";
		return 1;
	}

	std::cout << "PhysicsTests diagnostics: " << observedKnownIssueCount << " known issues observed in "
		<< diagnosticCount << " non-gating diagnostics\n";
	std::cout << "PhysicsTests: " << testCount << " checks passed\n";
	return 0;
}
