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
#include <stdexcept>
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

	// A mathematical point support map for base initialization and degenerate GJK tests.
	class PointShapeFixture final : public GEngine::PhysicalShape
	{
	public:
		PointShapeFixture() = default;
		using PhysicalShape::PhysicalShape;

		GEngine::Mat3 InertiaTensor() const override { return GEngine::Mat3(0.0f); }
		GEngine::Bounds GetBounds() const override { return GetBounds(GEngine::Vec3f(0.0f), GEngine::Quat()); }
		GEngine::Bounds GetBounds(const GEngine::Vec3f& position, const GEngine::Quat&) const override
		{
			GEngine::Bounds bounds;
			bounds.mins = bounds.maxs = position;
			return bounds;
		}
		GEngine::Vec3f Support(const GEngine::Vec3f&, const GEngine::Vec3f& position,
			const GEngine::Quat&, float) const override { return position; }
		std::size_t GetSourcePointCount() const { return m_MeshPoints.size(); }
	};

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

	void TestSphereAndBaseValidityContract()
	{
		const PointShapeFixture defaultBase;
		const PointShapeFixture meshBase(UnitBoxPoints());
		Expect(defaultBase.GetShapeType() == GEngine::ShapeType::Invalid && !defaultBase.IsValid() &&
			Near(defaultBase.GetCenterOfMass(), GEngine::Vec3f(0.0f), 0.0f) &&
			defaultBase.GetRevision() == 0 && defaultBase.GetSourcePointCount() == 0,
			"default base shape has deterministic unclassified type, zero center, and zero revision");
		Expect(meshBase.GetShapeType() == GEngine::ShapeType::Invalid && !meshBase.IsValid() &&
			Near(meshBase.GetCenterOfMass(), GEngine::Vec3f(0.0f), 0.0f) &&
			meshBase.GetRevision() == 0 && meshBase.GetSourcePointCount() == 8,
			"mesh base constructor initializes metadata while retaining its source points");

		const GEngine::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
		const GEngine::Vec3f direction(1.0f, 0.0f, 0.0f);
		GEngine::ShapeSphere defaultSphere;
		Expect(defaultSphere.IsValid() && defaultSphere.GetShapeType() == GEngine::ShapeType::Sphere &&
			defaultSphere.GetRadius() == 1.0f && defaultSphere.GetRevision() == 0 &&
			Near(defaultSphere.GetCenterOfMass(), GEngine::Vec3f(0.0f), 0.0f),
			"default sphere is a valid unit sphere with initialized type, center, and revision");
		Expect(Near(defaultSphere.InertiaTensor(), GEngine::Mat3(0.4f)) &&
			Near(defaultSphere.GetBounds().mins, GEngine::Vec3f(-1.0f)) &&
			Near(defaultSphere.GetBounds().maxs, GEngine::Vec3f(1.0f)) &&
			Near(defaultSphere.Support(direction, GEngine::Vec3f(0.0f), identity, 0.0f), direction),
			"default sphere has finite analytic inertia, bounds, and support");

		GEngine::ShapeSphere sphere(2.5f);
		Expect(sphere.IsValid() && sphere.GetShapeType() == GEngine::ShapeType::Sphere &&
			sphere.GetRadius() == 2.5f && sphere.GetRevision() == 0 &&
			Near(sphere.GetCenterOfMass(), GEngine::Vec3f(0.0f), 0.0f) &&
			Near(sphere.InertiaTensor(), GEngine::Mat3(2.5f)),
			"explicit valid sphere construction preserves its radius and analytic unit-mass inertia");

		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float infinity = std::numeric_limits<float>::infinity();
		const float invalidRadii[] = { 0.0f, -0.0f, -1.0f, nan, infinity, -infinity };
		for (float radius : invalidRadii)
		{
			bool rejected = false;
			try
			{
				GEngine::ShapeSphere invalidSphere(radius);
			}
			catch (const std::invalid_argument&)
			{
				rejected = true;
			}
			Expect(rejected, "sphere construction rejects each zero, negative, NaN, or infinite radius");
		}

		GEngine::RigidBody3D body;
		ConfigureSphereBody(body, sphere, GEngine::Vec3f(4.0f, 2.0f, -3.0f));
		const GEngine::Bounds localBounds = sphere.GetBounds();
		const GEngine::Bounds worldBounds = body.GetWorldBounds();
		const GEngine::Mat3 inertia = sphere.InertiaTensor();
		const GEngine::Mat3 inverseInertia = body.GetInverseInertiaTensorWorldSpace();
		const GEngine::Vec3f center = body.GetCenterOfMassWorldSpace();
		const GEngine::Vec3f support = sphere.Support(direction, body.m_Position, identity, 0.0f);
		const std::uint64_t revision = sphere.GetRevision();
		for (float radius : invalidRadii)
		{
			sphere.SetRadius(radius);
			Expect(sphere.IsValid() && sphere.GetRadius() == 2.5f && sphere.GetRevision() == revision &&
				Near(sphere.GetBounds().mins, localBounds.mins, 0.0f) &&
				Near(sphere.GetBounds().maxs, localBounds.maxs, 0.0f) &&
				Near(sphere.InertiaTensor(), inertia, 0.0f) &&
				Near(sphere.Support(direction, body.m_Position, identity, 0.0f), support, 0.0f) &&
				Near(body.GetWorldBounds().mins, worldBounds.mins, 0.0f) &&
				Near(body.GetWorldBounds().maxs, worldBounds.maxs, 0.0f) &&
				Near(body.GetInverseInertiaTensorWorldSpace(), inverseInertia, 0.0f) &&
				Near(body.GetCenterOfMassWorldSpace(), center, 0.0f),
				"each rejected radius preserves valid geometry, revision, and warmed body caches exactly");
		}
		sphere.SetRadius(sphere.GetRadius());
		Expect(sphere.GetRevision() == revision, "unchanged radius does not invalidate geometry caches");
		sphere.SetRadius(0.5f);
		Expect(sphere.IsValid() && sphere.GetRadius() == 0.5f && sphere.GetRevision() == revision + 1 &&
			Near(body.GetWorldBounds().mins, body.m_Position - GEngine::Vec3f(0.5f)) &&
			Near(body.GetWorldBounds().maxs, body.m_Position + GEngine::Vec3f(0.5f)) &&
			Near(body.GetInverseInertiaTensorWorldSpace(), GEngine::Mat3(10.0f)) &&
			Near(sphere.Support(direction, body.m_Position, identity, 0.0f),
				body.m_Position + direction * 0.5f),
			"valid radius after rejected updates commits once and refreshes analytic body caches");

		const std::uint64_t changedRevision = sphere.GetRevision();
		sphere.HandleScaleChanged(GEngine::Vec3f(-1.0f));
		sphere.HandleScaleChanged(GEngine::Vec3f(nan));
		sphere.HandleScaleChanged(GEngine::Vec3f(infinity));
		Expect(sphere.IsValid() && sphere.GetRadius() == 0.5f && sphere.GetRevision() == changedRevision,
			"existing scale callback cannot commit an invalid radius");

		GEngine::RigidBody3D defaultBodyA;
		GEngine::RigidBody3D defaultBodyB;
		ConfigureSphereBody(defaultBodyA, defaultSphere, GEngine::Vec3f(0.0f));
		ConfigureSphereBody(defaultBodyB, defaultSphere, GEngine::Vec3f(1.5f, 0.0f, 0.0f));
		GEngine::contact_t contact{};
		Expect(GEngine::Collision::Intersect(&defaultBodyA, &defaultBodyB, contact) && Finite(contact) &&
			Near(contact.separationDistance, -0.5f),
			"default spheres dispatch to finite analytic sphere contacts");
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
		PointShapeFixture pointShape;
		pointShape.SetShapeType(GEngine::ShapeType::Convex);
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

	void TestContactPairOrderRegression()
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
		Expect(canonicalNormalDot > 0.999f,
			"reordered contact preserves the canonical manifold normal direction");
	}


	GEngine::contact_t ReversedContact(const GEngine::contact_t& contact)
	{
		GEngine::contact_t reversed = contact;
		std::swap(reversed.m_BodyA, reversed.m_BodyB);
		std::swap(reversed.ptOnA_WorldSpace, reversed.ptOnB_WorldSpace);
		std::swap(reversed.ptOnA_LocalSpace, reversed.ptOnB_LocalSpace);
		reversed.normal = -contact.normal;
		return reversed;
	}

	GEngine::contact_t MakeContact(GEngine::RigidBody3D& bodyA, GEngine::RigidBody3D& bodyB,
		const GEngine::Vec3f& pointA, const GEngine::Vec3f& pointB, const GEngine::Vec3f& normal)
	{
		GEngine::contact_t contact{};
		contact.m_BodyA = &bodyA;
		contact.m_BodyB = &bodyB;
		contact.ptOnA_WorldSpace = pointA;
		contact.ptOnB_WorldSpace = pointB;
		contact.ptOnA_LocalSpace = bodyA.WorldSpaceToBodySpace(pointA);
		contact.ptOnB_LocalSpace = bodyB.WorldSpaceToBodySpace(pointB);
		contact.normal = normal;
		contact.separationDistance = glm::dot(pointA - pointB, normal);
		return contact;
	}

	void TestCollisionContactConvention()
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::ShapeBox box(UnitBoxPoints());
		for (int pairType = 0; pairType < 3; ++pairType)
		{
			for (const bool rotated : { false, true })
			{
				for (const bool swept : { false, true })
				{
					const GEngine::Quat rotation = glm::angleAxis(rotated ? 0.37f : 0.0f,
						glm::normalize(GEngine::Vec3f(1.0f, 2.0f, 3.0f)));
					const GEngine::Vec3f axis = rotation * GEngine::Vec3f(1.0f, 0.0f, 0.0f);
					GEngine::RigidBody3D bodyA, bodyB;
					ConfigureSphereBody(bodyA, sphere, GEngine::Vec3f(0.0f));
					ConfigureSphereBody(bodyB, sphere, axis * (swept ? 4.0f : 1.9f));
					if (pairType == 1) bodyA.m_Shape = &box;
					if (pairType != 0) bodyB.m_Shape = &box;
					bodyA.m_Orientation = bodyB.m_Orientation = rotation;
					if (swept)
					{
						bodyA.m_LinearVelocity = axis * 3.0f;
						bodyB.m_LinearVelocity = -axis;
					}

					GEngine::contact_t direct{}, reverse{};
					const bool hitA = swept
						? GEngine::Collision::Intersect(&bodyA, &bodyB, 0.6f, direct)
						: GEngine::Collision::Intersect(&bodyA, &bodyB, direct);
					const bool hitB = swept
						? GEngine::Collision::Intersect(&bodyB, &bodyA, 0.6f, reverse)
						: GEngine::Collision::Intersect(&bodyB, &bodyA, reverse);
					Expect(hitA && hitB && Finite(direct) && Finite(reverse),
						"sphere, box, and mixed queries return finite contacts in both body orders");
					if (!hitA || !hitB) continue;
					Expect(direct.m_BodyA == &bodyA && direct.m_BodyB == &bodyB &&
						reverse.m_BodyA == &bodyB && reverse.m_BodyB == &bodyA &&
						Near(glm::length(direct.normal), 1.0f, 1.0e-4f) &&
						Near(glm::length(reverse.normal), 1.0f, 1.0e-4f) &&
						glm::dot(direct.normal, -axis) > 0.99f &&
						glm::dot(reverse.normal, axis) > 0.99f &&
						glm::dot(direct.normal, reverse.normal) < -0.99f,
						"collision output normal is unit world-space B-to-A and reverses with A/B");
					Expect(Near(direct.separationDistance, reverse.separationDistance, 2.0e-3f) &&
						Near(direct.timeOfImpact, reverse.timeOfImpact, 2.0e-3f) &&
						(swept ? Near(direct.timeOfImpact, 0.5f, 2.0e-3f)
							: direct.separationDistance < 0.0f),
						"pair permutation preserves signed separation and analytic impact time");
					for (const auto& contact : { direct, reverse })
					{
						Expect(Near(contact.m_BodyA->BodySpaceToWorldSpace(contact.ptOnA_LocalSpace) +
							contact.m_BodyA->GetLinearVelocity() * contact.timeOfImpact,
							contact.ptOnA_WorldSpace, 2.0e-5f) &&
							Near(contact.m_BodyB->BodySpaceToWorldSpace(contact.ptOnB_LocalSpace) +
							contact.m_BodyB->GetLinearVelocity() * contact.timeOfImpact,
							contact.ptOnB_WorldSpace, 2.0e-5f),
							"world/local anchors belong to their labelled bodies at impact");
					}
					if (pairType == 0 && swept)
					{
						GEngine::contact_t sphereDirect{}, sphereReverse{};
						Expect(GEngine::Collision::SphereSphereIntersect(&bodyA, &bodyB, 0.6f, sphereDirect) &&
							GEngine::Collision::SphereSphereIntersect(&bodyB, &bodyA, 0.6f, sphereReverse) &&
							Finite(sphereDirect) && Finite(sphereReverse) &&
							Near(sphereDirect.normal, direct.normal) &&
							Near(sphereReverse.normal, -sphereDirect.normal) &&
							Near(sphereDirect.ptOnA_WorldSpace, sphereReverse.ptOnB_WorldSpace) &&
							Near(sphereDirect.ptOnB_WorldSpace, sphereReverse.ptOnA_WorldSpace) &&
							Near(sphereDirect.timeOfImpact, 0.5f),
							"dedicated swept sphere entry point follows the same contact convention");
					}
				}
			}
		}
	}

	void TestContactImpulsePermutation()
	{
		GEngine::ShapeSphere sphere(1.0f);
		const GEngine::Quat rotation = glm::angleAxis(0.6f,
			glm::normalize(GEngine::Vec3f(1.0f, 2.0f, 3.0f)));
		const GEngine::Vec3f axis = rotation * GEngine::Vec3f(1.0f, 0.0f, 0.0f);
		const GEngine::Vec3f tangent = rotation * GEngine::Vec3f(0.0f, 1.0f, 0.0f);
		for (const bool ballistic : { false, true })
		{
			for (const bool reversed : { false, true })
			{
				GEngine::RigidBody3D bodyA, bodyB;
				ConfigureSphereBody(bodyA, sphere, GEngine::Vec3f(0.0f));
				ConfigureSphereBody(bodyB, sphere, axis * 2.0f);
				bodyA.m_Orientation = rotation;
				bodyB.m_Orientation = glm::angleAxis(-0.4f, GEngine::Vec3f(0.0f, 1.0f, 0.0f));
				bodyB.m_InvMass = 0.5f;
				bodyA.m_Friction = bodyB.m_Friction = 0.0f;
				bodyA.m_Elasticity = bodyB.m_Elasticity = 0.5f;
				bodyA.m_LinearVelocity = axis * 2.0f;
				bodyB.m_LinearVelocity = -axis;
				const GEngine::Vec3f point = axis + tangent * 0.5f;
				const auto direct = MakeContact(bodyA, bodyB, point, point, -axis);
				auto contact = reversed ? ReversedContact(direct) : direct;
				const GEngine::Vec3f ra = point - bodyA.GetCenterOfMassWorldSpace();
				const GEngine::Vec3f rb = point - bodyB.GetCenterOfMassWorldSpace();
				const GEngine::Mat3 inertiaA = bodyA.GetInverseInertiaTensorWorldSpace();
				const GEngine::Mat3 inertiaB = bodyB.GetInverseInertiaTensorWorldSpace();
				const float inverseEffectiveMass = 1.5f +
					glm::dot(glm::cross(ra, axis), inertiaA * glm::cross(ra, axis)) +
					glm::dot(glm::cross(rb, axis), inertiaB * glm::cross(rb, axis));
				const GEngine::Vec3f impulseA = -axis * ((ballistic ? 1.25f : 1.0f) *
					3.0f / inverseEffectiveMass);
				if (ballistic)
				{
					contact.timeOfImpact = 0.1f;
					GEngine::Collision::ResolveContact(contact);
				}
				else
				{
					GEngine::ManifoldCollector manifolds;
					manifolds.AddContact(contact);
					manifolds.PreSolve(1.0f / 120.0f);
					manifolds.Solve();
				}
				Expect(bodyA.HasFiniteState() && bodyB.HasFiniteState() &&
					Near(bodyA.m_LinearVelocity, axis * 2.0f + impulseA) &&
					Near(bodyB.m_LinearVelocity, -axis - impulseA * 0.5f) &&
					Near(bodyA.m_AngularVelocity, inertiaA * glm::cross(ra, impulseA)) &&
					Near(bodyB.m_AngularVelocity, inertiaB * glm::cross(rb, -impulseA)),
					"resting and ballistic A/B permutations match analytic off-center repulsive impulses");
			}
		}
	}

	void TestPersistentContactPermutation()
	{
		GEngine::ShapeSphere sphere(1.0f);
		const GEngine::Quat rotation = glm::angleAxis(0.6f,
			glm::normalize(GEngine::Vec3f(1.0f, 2.0f, 3.0f)));
		const GEngine::Vec3f axis = rotation * GEngine::Vec3f(1.0f, 0.0f, 0.0f);
		const GEngine::Vec3f tangent = rotation * GEngine::Vec3f(0.0f, 1.0f, 0.0f);
		std::vector<GEngine::Vec3f> reference;
		// Same physical contacts: canonical input, alternating order, then reversed initial order.
		for (int ordering = 0; ordering < 3; ++ordering)
		{
			GEngine::RigidBody3D bodyA, bodyB;
			ConfigureSphereBody(bodyA, sphere, GEngine::Vec3f(0.0f));
			ConfigureSphereBody(bodyB, sphere, axis * 1.99f);
			bodyA.m_Orientation = rotation;
			bodyB.m_Orientation = glm::angleAxis(-0.4f, GEngine::Vec3f(0.0f, 1.0f, 0.0f));
			bodyB.m_InvMass = 0.5f;
			bodyA.m_Friction = bodyB.m_Friction = 0.0f;
			GEngine::ManifoldCollector manifolds;
			std::array<GEngine::contact_t, 2> contacts;
			for (int pointIndex = 0; pointIndex < 2; ++pointIndex)
			{
				const float offset = pointIndex == 0 ? -0.5f : 0.5f;
				contacts[pointIndex] = MakeContact(bodyA, bodyB, axis + tangent * offset,
					axis * 0.99f + tangent * offset, -axis);
				const bool reverseInput = ordering == 1 ? pointIndex == 1 :
					ordering == 2 ? pointIndex == 0 : false;
				manifolds.AddContact(reverseInput ? ReversedContact(contacts[pointIndex]) : contacts[pointIndex]);
			}
			Expect(manifolds.m_Manifolds.size() == 1 && manifolds.GetContactCount() == 2,
				"alternating body orders add distinct contacts to one existing manifold");
			if (manifolds.GetContactCount() != 2) continue;
			for (int pointIndex = 0; pointIndex < 2; ++pointIndex)
			{
				const auto expected = ordering == 2 ? ReversedContact(contacts[pointIndex]) : contacts[pointIndex];
				const auto stored = manifolds.m_Manifolds[0].GetContact(pointIndex);
				Expect(Finite(stored) && stored.m_BodyA == expected.m_BodyA &&
					stored.m_BodyB == expected.m_BodyB &&
					Near(stored.normal, expected.normal) &&
					Near(stored.ptOnA_WorldSpace, expected.ptOnA_WorldSpace) &&
					Near(stored.ptOnB_WorldSpace, expected.ptOnB_WorldSpace) &&
					Near(stored.ptOnA_LocalSpace, expected.ptOnA_LocalSpace) &&
					Near(stored.ptOnB_LocalSpace, expected.ptOnB_LocalSpace) &&
					Near(stored.separationDistance, expected.separationDistance, 0.0f) &&
					Near(stored.timeOfImpact, expected.timeOfImpact, 0.0f),
					"manifold reordering preserves both anchor spaces, scalar fields, and canonical normal");
			}
			manifolds.RemoveExpired();
			Expect(manifolds.GetContactCount() == 2,
				"reordered penetrating contacts survive expiry in rotated body frames");

			std::vector<GEngine::Vec3f> response;
			for (int step = 0; step < 3; ++step)
			{
				bodyA.m_LinearVelocity = axis * 2.0f;
				bodyB.m_LinearVelocity = -axis;
				bodyA.m_AngularVelocity = bodyB.m_AngularVelocity = GEngine::Vec3f(0.0f);
				manifolds.PreSolve(1.0f / 120.0f);
				// On later steps this captures the actual cached warm-start response.
				response.insert(response.end(), { bodyA.m_LinearVelocity, bodyB.m_LinearVelocity,
					bodyA.m_AngularVelocity, bodyB.m_AngularVelocity });
				for (int iteration = 0; iteration < 4; ++iteration) manifolds.Solve();
				manifolds.PostSolve();
				response.insert(response.end(), { bodyA.m_LinearVelocity, bodyB.m_LinearVelocity,
					bodyA.m_AngularVelocity, bodyB.m_AngularVelocity });
				Expect(bodyA.HasFiniteState() && bodyB.HasFiniteState(),
					"persistent reordered contact solving and warm starting remain finite");
			}
			Expect(glm::dot(response[4], axis) < 2.0f && glm::dot(response[5], axis) > -1.0f &&
				glm::dot(response[8], axis) < 2.0f,
				"persistent contacts produce repulsion and a nonzero cached warm-start impulse");
			if (ordering == 0) reference = response;
			else
			{
				bool equivalent = response.size() == reference.size();
				for (std::size_t i = 0; i < response.size() && equivalent; ++i)
					equivalent = Near(response[i], reference[i], 2.0e-5f);
				Expect(equivalent, "alternating and reversed manifold orders preserve cold and warm impulse response");
			}
			bodyB.m_Position += axis * 0.1f;
			manifolds.RemoveExpired();
			Expect(manifolds.GetContactCount() == 0,
				"separated contacts expire identically for either manifold body order");
		}
	}

	int RunContactConventionRegression()
	{
		TestContactPairOrderRegression();
		TestCollisionContactConvention();
		TestContactImpulsePermutation();
		TestPersistentContactPermutation();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused contact-convention checks failed\n";
			return 1;
		}
		std::cout << "Contact-convention regression: " << testCount << " checks passed\n";
		return 0;
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

	struct AngularDrift
	{
		float maxEnergyError{};
		float maxMomentumError{};
		bool finite{ true };
		bool unitOrientation{ true };
	};

	AngularDrift MeasureAngularDrift(GEngine::ShapeBox& shape, int frequency,
		float inverseMass, const GEngine::Quat& orientation)
	{
		GEngine::RigidBody3D body;
		ConfigureBoxBody(body, shape, GEngine::Vec3f(0.0f), orientation);
		body.m_InvMass = inverseMass;
		body.m_AngularVelocity = orientation * GEngine::Vec3f(0.7f, 1.1f, 1.6f);

		// Analytic centroidal inertia for half-extents (1, 2, 3), independent of body caches.
		const GEngine::Vec3f principalInertia = GEngine::Vec3f(13.0f, 10.0f, 5.0f) /
			(3.0f * inverseMass);
		const auto momentum = [&]()
		{
			const GEngine::Quat rotation = GEngine::Math::NormalizeOrIdentity(body.m_Orientation);
			return rotation * (principalInertia * (glm::conjugate(rotation) * body.m_AngularVelocity));
		};
		const GEngine::Vec3f initialMomentum = momentum();
		const float initialEnergy = 0.5f * glm::dot(body.m_AngularVelocity, initialMomentum);
		AngularDrift result;
		for (int step = 0; step < 10 * frequency; ++step)
		{
			body.Update(1.0f / static_cast<float>(frequency));
			const GEngine::Vec3f currentMomentum = momentum();
			const float energy = 0.5f * glm::dot(body.m_AngularVelocity, currentMomentum);
			const float energyError = std::fabs(energy / initialEnergy - 1.0f);
			const float momentumError = glm::length(currentMomentum - initialMomentum) /
				glm::length(initialMomentum);
			result.finite = result.finite && body.HasFiniteState() && Finite(currentMomentum) &&
				std::isfinite(energyError) && std::isfinite(momentumError);
			result.unitOrientation = result.unitOrientation &&
				Near(glm::length(body.m_Orientation), 1.0f, 2.0e-6f);
			result.maxEnergyError = std::max(result.maxEnergyError, energyError);
			result.maxMomentumError = std::max(result.maxMomentumError, momentumError);
		}
		std::cout << "ANGULAR_DRIFT hz=" << frequency << " inverse_mass=" << inverseMass
			<< " max_energy_error_percent=" << 100.0f * result.maxEnergyError
			<< " max_momentum_vector_error_percent=" << 100.0f * result.maxMomentumError << '\n';
		return result;
	}

	void TestTorqueFreeAngularDynamics()
	{
		std::vector<GEngine::Vec3f> points = UnitBoxPoints();
		for (GEngine::Vec3f& point : points)
		{
			point *= GEngine::Vec3f(1.0f, 2.0f, 3.0f);
		}
		GEngine::ShapeBox box(points);
		const GEngine::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
		const GEngine::Quat rotated = glm::angleAxis(0.73f,
			glm::normalize(GEngine::Vec3f(1.0f, -2.0f, 3.0f)));
		const GEngine::Vec3f initialOmega(0.7f, 1.1f, 1.6f);
		// Euler's principal-axis equations: ((Iy-Iz)/Ix wy wz, ...).
		const GEngine::Vec3f expectedAlpha(8.8f / 13.0f, -0.896f, 0.462f);
		for (const GEngine::Quat& orientation : { identity, rotated })
		{
			for (float inverseMass : { 0.25f, 1.0f, 4.0f })
			{
				GEngine::RigidBody3D body;
				ConfigureBoxBody(body, box, GEngine::Vec3f(0.0f), identity);
				body.GetInverseInertiaTensorWorldSpace();
				body.m_InvMass = inverseMass;
				body.m_Orientation = orientation;
				body.m_AngularVelocity = orientation * initialOmega;
				body.Update(1.0f / 120.0f);
				Expect(Near(body.m_AngularVelocity,
					orientation * (initialOmega + expectedAlpha / 120.0f), 2.0e-6f),
					"torque-free acceleration has the analytic sign, frame, and mass cancellation after cache edits");
			}
		}

		GEngine::RigidBody3D zeroInverseMassBody;
		ConfigureBoxBody(zeroInverseMassBody, box, GEngine::Vec3f(0.0f), rotated);
		zeroInverseMassBody.GetInverseInertiaTensorWorldSpace();
		zeroInverseMassBody.m_InvMass = 0.0f;
		zeroInverseMassBody.m_AngularVelocity = initialOmega;
		zeroInverseMassBody.Update(1.0f / 120.0f);
		Expect(zeroInverseMassBody.HasFiniteState() && Near(zeroInverseMassBody.m_AngularVelocity, initialOmega),
			"zero inverse inertia skips gyroscopic acceleration without dividing by zero");

		// Bound the existing first-order method, and require smaller drift when dt is halved.
		// These tolerances are proposed for Phase 13 human review, not exact conservation.
		for (float inverseMass : { 0.25f, 1.0f, 4.0f })
		{
			const GEngine::Quat orientation = inverseMass == 1.0f ? identity : rotated;
			const AngularDrift coarse = MeasureAngularDrift(box, 120, inverseMass, orientation);
			const AngularDrift fine = MeasureAngularDrift(box, 240, inverseMass, orientation);
			Expect(coarse.finite && fine.finite && coarse.unitOrientation && fine.unitOrientation,
				"ten-second free-body runs retain finite state and normalized orientations");
			Expect(coarse.maxEnergyError <= 0.05f && coarse.maxMomentumError <= 0.03f,
				"120 Hz free-body peak energy and world momentum-vector errors stay within 5% and 3%");
			Expect(fine.maxEnergyError <= 0.025f && fine.maxMomentumError <= 0.015f,
				"240 Hz free-body peak energy and world momentum-vector errors stay within 2.5% and 1.5%");
			Expect(fine.maxEnergyError <= 0.6f * coarse.maxEnergyError &&
				fine.maxMomentumError <= 0.6f * coarse.maxMomentumError,
				"halving the timestep reduces both ten-second invariant errors by at least 40%");
		}

		GEngine::ShapeSphere sphere(1.0f);
		GEngine::RigidBody3D sphericalBody;
		ConfigureSphereBody(sphericalBody, sphere, GEngine::Vec3f(0.0f));
		sphericalBody.m_InvMass = 0.25f;
		sphericalBody.m_AngularVelocity = initialOmega;
		GEngine::RigidBody3D principalBody;
		ConfigureBoxBody(principalBody, box, GEngine::Vec3f(0.0f), identity);
		principalBody.m_AngularVelocity = GEngine::Vec3f(0.0f, 0.0f, 1.6f);
		GEngine::RigidBody3D stationaryBody;
		ConfigureBoxBody(stationaryBody, box, GEngine::Vec3f(0.0f), rotated);
		bool finite = true;
		for (int step = 0; step < 1200; ++step)
		{
			sphericalBody.Update(1.0f / 120.0f);
			principalBody.Update(1.0f / 120.0f);
			stationaryBody.Update(1.0f / 120.0f);
			finite = finite && sphericalBody.HasFiniteState() && principalBody.HasFiniteState() &&
				stationaryBody.HasFiniteState();
		}
		Expect(finite && Near(sphericalBody.m_AngularVelocity, initialOmega, 1.0e-5f),
			"isotropic sphere retains constant angular velocity over ten seconds");
		const GEngine::Quat expectedRotation = glm::angleAxis(16.0f, GEngine::Vec3f(0.0f, 0.0f, 1.0f));
		Expect(Near(principalBody.m_AngularVelocity, GEngine::Vec3f(0.0f, 0.0f, 1.6f)) &&
			Near(std::fabs(glm::dot(principalBody.m_Orientation, expectedRotation)), 1.0f),
			"principal-axis spin retains its speed and analytic orientation");
		Expect(Near(stationaryBody.m_AngularVelocity, GEngine::Vec3f(0.0f)) &&
			Near(std::fabs(glm::dot(stationaryBody.m_Orientation, rotated)), 1.0f),
			"zero angular velocity preserves orientation without artificial motion");
	}

	float OrientationResidual(const GEngine::Quat& actual, const GEngine::Quat& expected)
	{
		const GEngine::Quat relative = GEngine::Math::NormalizeOrIdentity(
			actual * glm::conjugate(expected));
		return 2.0f * std::atan2(glm::length(GEngine::Vec3f(relative.x, relative.y, relative.z)),
			std::fabs(relative.w));
	}

	void TestAngularBodyTypeGuard()
	{
		std::vector<GEngine::Vec3f> points = UnitBoxPoints();
		for (GEngine::Vec3f& point : points)
		{
			point *= GEngine::Vec3f(1.0f, 2.0f, 3.0f);
		}
		GEngine::ShapeBox box(points);
		for (const auto type : { GEngine::Component::BodyType::Static, GEngine::Component::BodyType::Kinematic })
		{
			for (float inverseMass : { 0.0f, 0.37f, 4.0f })
			{
				GEngine::RigidBody3D body;
				ConfigureBoxBody(body, box, GEngine::Vec3f(0.0f),
					glm::angleAxis(0.73f, glm::normalize(GEngine::Vec3f(1.0f, -2.0f, 3.0f))));
				body.GetInverseInertiaTensorWorldSpace();
				body.Type = type;
				body.m_InvMass = inverseMass;
				const GEngine::Vec3f initialOmega(0.7f, 1.1f, 1.6f);
				body.m_AngularVelocity = initialOmega;
				bool unchanged = true;
				for (int step = 0; step < 1200; ++step)
				{
					body.Update(1.0f / 120.0f);
					unchanged = unchanged && body.HasFiniteState() &&
						Near(body.m_AngularVelocity, initialOmega, 0.0f);
				}
				Expect(unchanged, "Static and Kinematic bodies receive no gyroscopic acceleration regardless of positive mass");
			}
		}
	}

	void TestAngularCacheAgainstFreshInverse()
	{
		const GEngine::Quat rotation = glm::angleAxis(0.73f,
			glm::normalize(GEngine::Vec3f(1.0f, -2.0f, 3.0f)));
		for (float inverseMass : { 0.37f, 1.0f, 2.5f })
		{
			std::vector<GEngine::Vec3f> points = UnitBoxPoints();
			for (GEngine::Vec3f& point : points)
			{
				point *= GEngine::Vec3f(1.0f, 2.0f, 3.0f);
			}
			GEngine::ShapeBox box(points);
			std::vector<GEngine::Vec3f> replacementPoints = points;
			for (GEngine::Vec3f& point : replacementPoints)
			{
				point *= GEngine::Vec3f(1.5f, 0.75f, 1.25f);
			}
			GEngine::ShapeBox replacement(replacementPoints);
			GEngine::RigidBody3D body;
			ConfigureBoxBody(body, box, GEngine::Vec3f(0.0f), rotation);
			body.m_InvMass = inverseMass;
			body.m_AngularVelocity = rotation * GEngine::Vec3f(0.7f, 1.1f, 1.6f);
			float maxInverseError = 0.0f;
			float maxOmegaError = 0.0f;
			float maxOrientationError = 0.0f;
			bool finite = true;
			constexpr float dt = 1.0f / 120.0f;
			for (int step = 0; step < 1200; ++step)
			{
				body.GetInverseInertiaTensorWorldSpace(); // Warm before each direct mutation.
				if (step == 300)
				{
					std::vector<GEngine::Vec3f> rebuiltPoints = points;
					for (GEngine::Vec3f& point : rebuiltPoints)
					{
						point *= GEngine::Vec3f(1.25f, 1.1f, 0.9f);
					}
					box.Build(rebuiltPoints); // New revision; differs from the later replacement shape.
				}
				if (step == 600)
				{
					body.m_Shape = &replacement; // Different shape pointer with its own revision.
				}
				if (step == 900)
				{
					body.m_InvMass *= 1.3f;
					body.m_Orientation = rotation * body.m_Orientation;
				}

				// Independent fresh shape/pose calculation; no body cache supplies this reference.
				const GEngine::Quat orientation = GEngine::Math::NormalizeOrIdentity(body.m_Orientation);
				const GEngine::Mat3 bodyToWorld = glm::toMat3(orientation);
				const GEngine::Mat3 inertia = bodyToWorld * body.m_Shape->InertiaTensor() *
					glm::transpose(bodyToWorld);
				const GEngine::Mat3 freshInverse = glm::inverse(inertia);
				const GEngine::Mat3 cachedInverse = body.GetInverseInertiaTensorWorldSpace() / body.m_InvMass;
				for (int column = 0; column < 3; ++column)
				{
					const float error = glm::length(cachedInverse[column] - freshInverse[column]) /
						glm::length(freshInverse[column]);
					finite = finite && std::isfinite(error);
					maxInverseError = std::max(maxInverseError, error);
				}
				const GEngine::Vec3f expectedOmega = body.m_AngularVelocity -
					(freshInverse * glm::cross(body.m_AngularVelocity, inertia * body.m_AngularVelocity)) * dt;
				const GEngine::Vec3f dAngle = expectedOmega * dt;
				const GEngine::Quat expectedOrientation = GEngine::Math::NormalizeOrIdentity(
					glm::angleAxis(glm::length(dAngle), glm::normalize(dAngle)) * orientation);
				body.Update(dt);
				const float omegaError = glm::length(body.m_AngularVelocity - expectedOmega);
				const float orientationError = OrientationResidual(body.m_Orientation, expectedOrientation);
				finite = finite && body.HasFiniteState() && std::isfinite(omegaError) && std::isfinite(orientationError);
				maxOmegaError = std::max(maxOmegaError, omegaError);
				maxOrientationError = std::max(maxOrientationError, orientationError);
			}
			std::cout << "ANGULAR_CACHE inverse_mass=" << inverseMass
				<< " max_relative_inverse_error=" << maxInverseError
				<< " max_step_omega_error=" << maxOmegaError
				<< " max_step_orientation_radians=" << maxOrientationError << '\n';
			Expect(finite && maxInverseError <= 5.0e-6f,
				"cached inverse matches a fresh inverse after rotation, mass, shape identity, and revision changes");
			Expect(maxOmegaError <= 3.0e-6f && maxOrientationError <= 3.0e-6f,
				"cached angular steps match correct-sign fresh-inverse steps through geometry and mass mutations");
		}
	}

	void TestAngularRewindDiagnostic()
	{
		std::vector<GEngine::Vec3f> points = UnitBoxPoints();
		for (GEngine::Vec3f& point : points)
		{
			point *= GEngine::Vec3f(1.0f, 2.0f, 3.0f);
		}
		GEngine::ShapeBox box(points);
		GEngine::RigidBody3D initial;
		ConfigureBoxBody(initial, box, GEngine::Vec3f(1.0f, 2.0f, 3.0f),
			glm::angleAxis(0.73f, glm::normalize(GEngine::Vec3f(1.0f, -2.0f, 3.0f))));
		initial.m_LinearVelocity = GEngine::Vec3f(0.25f, -0.5f, 0.75f);
		initial.m_AngularVelocity = initial.m_Orientation * GEngine::Vec3f(0.7f, 1.1f, 1.6f);
		initial.GetInverseInertiaTensorWorldSpace();
		initial.GetWorldBounds();
		std::cout << "CCD_REWIND_INITIAL position=1,2,3 linear_velocity=0.25,-0.5,0.75 orientation="
			<< initial.m_Orientation.w << ',' << initial.m_Orientation.x << ','
			<< initial.m_Orientation.y << ',' << initial.m_Orientation.z
			<< " angular_velocity=" << initial.m_AngularVelocity.x << ','
			<< initial.m_AngularVelocity.y << ',' << initial.m_AngularVelocity.z << '\n';
		for (float toi : { 1.0f / 240.0f, 1.0f / 120.0f, 1.0f / 60.0f })
		{
			GEngine::RigidBody3D body = initial;
			bool finite = true;
			for (int query = 1; query <= 1000; ++query)
			{
				body.Update(toi);
				body.Update(-toi);
				finite = finite && body.HasFiniteState();
				if (query == 1 || query == 1000)
				{
					std::cout << "CCD_REWIND toi=" << toi << " queries=" << query
						<< " position_residual=" << glm::length(body.m_Position - initial.m_Position)
						<< " orientation_residual_radians=" << OrientationResidual(body.m_Orientation, initial.m_Orientation)
						<< " linear_velocity_residual=" << glm::length(body.m_LinearVelocity - initial.m_LinearVelocity)
						<< " angular_velocity_residual=" << glm::length(body.m_AngularVelocity - initial.m_AngularVelocity)
						<< '\n';
				}
			}
			Expect(finite, "CCD rewind diagnostic retains finite state; residuals are non-gating measurements");

			// Diagnosis only: compare a temporary snapshot restore, without changing collision queries.
			body = initial;
			body.Update(toi);
			body = initial;
			const float restoredOrientationError = OrientationResidual(body.m_Orientation, initial.m_Orientation);
			std::cout << "CCD_SNAPSHOT toi=" << toi
				<< " position_residual=" << glm::length(body.m_Position - initial.m_Position)
				<< " orientation_residual_radians=" << restoredOrientationError
				<< " linear_velocity_residual=" << glm::length(body.m_LinearVelocity - initial.m_LinearVelocity)
				<< " angular_velocity_residual=" << glm::length(body.m_AngularVelocity - initial.m_AngularVelocity)
				<< '\n';
			Expect(Near(body.m_Position, initial.m_Position, 0.0f) && restoredOrientationError <= 1.0e-7f &&
				Near(body.m_LinearVelocity, initial.m_LinearVelocity, 0.0f) &&
				Near(body.m_AngularVelocity, initial.m_AngularVelocity, 0.0f),
				"diagnostic snapshot restore retains the original physical state");
		}
	}

	int RunAngularDynamicsRegression()
	{
		TestTorqueFreeAngularDynamics();
		TestAngularBodyTypeGuard();
		TestAngularCacheAgainstFreshInverse();
		TestAngularRewindDiagnostic();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused angular-dynamics checks failed\n";
			return 1;
		}
		std::cout << "Angular-dynamics regression: " << testCount << " checks passed\n";
		return 0;
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

	int RunSphereValidityRegression()
	{
		TestSphereAndBaseValidityContract();
		TestSphereRadiusInvalidationContract();
		TestDegenerateGjkDirection();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused sphere-validity checks failed\n";
			return 1;
		}
		std::cout << "Sphere-validity regression: " << testCount << " checks passed\n";
		return 0;
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


	// Coincident centers and anchors isolate ballistic impulse equations from lever-arm coupling.
	struct BallisticFixture
	{
		GEngine::ShapeSphere sphere{ 1.0f };
		GEngine::RigidBody3D body, support;
		GEngine::contact_t contact{};

		BallisticFixture()
		{
			ConfigureSphereBody(body, sphere, GEngine::Vec3f(0.0f));
			ConfigureSphereBody(support, sphere, GEngine::Vec3f(0.0f));
			support.SetBodyTypeAndInverseMass(GEngine::Component::BodyType::Static, 0.0f);
			body.m_Friction = support.m_Friction = 0.5f;
			body.m_Elasticity = support.m_Elasticity = 0.5f;
			contact = MakeContact(body, support, GEngine::Vec3f(0.0f), GEngine::Vec3f(0.0f),
				GEngine::Vec3f(0.0f, 1.0f, 0.0f));
			contact.timeOfImpact = 0.1f;
		}

		void Resolve(bool reversed = false)
		{
			auto orderedContact = reversed ? ReversedContact(contact) : contact;
			GEngine::Collision::ResolveContact(orderedContact);
		}
	};

	double BallisticSphereKineticEnergy(const GEngine::RigidBody3D& body)
	{
		const double inverseMass = body.GetInverseMass();
		return inverseMass > 0.0 ? (glm::length2(body.m_LinearVelocity) +
			0.4 * glm::length2(body.m_AngularVelocity)) / (2.0 * inverseMass) : 0.0;
	}

	void TestBallisticSeparatingGuard()
	{
		for (const bool reversed : { false, true })
		{
			// Last case has closing COM motion but separating contact motion from spin.
			for (const auto normalAndSpin : { GEngine::Vec3f(0.05f, 0.0f, 0.0f),
				GEngine::Vec3f(0.0f), GEngine::Vec3f(-1.0f, 4.0f, 0.0f) })
			{
				BallisticFixture fixture;
				fixture.contact.ptOnA_LocalSpace = GEngine::Vec3f(0.5f, -1.0f, 0.0f);
				fixture.body.m_LinearVelocity = GEngine::Vec3f(3.0f, normalAndSpin.x, 4.0f);
				fixture.body.m_AngularVelocity = GEngine::Vec3f(0.0f, 0.0f, normalAndSpin.y);
				const auto velocity = fixture.body.m_LinearVelocity;
				const auto omega = fixture.body.m_AngularVelocity;
				fixture.Resolve(reversed);
				Expect(Near(fixture.body.m_LinearVelocity, velocity, 0.0f) &&
					Near(fixture.body.m_AngularVelocity, omega, 0.0f) &&
					Near(fixture.body.m_Position, GEngine::Vec3f(0.0f), 0.0f) &&
					fixture.body.HasFiniteState() && fixture.support.HasFiniteState(),
					"separating or exactly tangential ballistic contact applies no normal or friction impulse");
			}
			BallisticFixture stale;
			stale.body.m_Elasticity = stale.support.m_Elasticity = 1.0f;
			stale.body.m_LinearVelocity = GEngine::Vec3f(0.0f, -2.0f, 0.0f);
			stale.Resolve(reversed);
			Expect(Near(stale.body.m_LinearVelocity, GEngine::Vec3f(0.0f, 2.0f, 0.0f)),
				"closing ballistic contact retains the existing restitution product");
			stale.contact.timeOfImpact = 0.2f;
			stale.Resolve(reversed);
			Expect(Near(stale.body.m_LinearVelocity, GEngine::Vec3f(0.0f, 2.0f, 0.0f)),
				"earlier impact making a stored contact separate prevents a later attractive impulse");
		}
	}

	void TestBallisticCoulombImpulses()
	{
		const GEngine::Vec3f direction(0.6f, 0.0f, 0.8f), normal(0.0f, 1.0f, 0.0f);
		for (const bool dynamicSupport : { false, true })
		for (const bool reversed : { false, true })
		for (const float closingSpeed : { 0.001f, 0.5f, 4.0f })
		for (const float friction : { 0.0f, 0.25f, 2.0f })
		for (const float tangentSpeed : { 0.01f, 10.0f })
		{
			BallisticFixture fixture;
			fixture.body.m_InvMass = 0.5f;
			if (dynamicSupport) fixture.support.SetBodyTypeAndInverseMass(GEngine::Component::BodyType::Dynamic, 0.25f);
			fixture.body.m_Friction = friction;
			fixture.support.m_Friction = 1.0f;
			const auto initialVelocity = direction * tangentSpeed - normal * closingSpeed;
			fixture.body.m_LinearVelocity = initialVelocity;
			const float inverseMassSum = 0.5f + (dynamicSupport ? 0.25f : 0.0f);
			// Phase 18: low-speed impacts are inelastic; the material product remains 0.25 above 1 unit/s.
			const float normalImpulse = (closingSpeed > 1.0f ? 1.25f : 1.0f) * closingSpeed / inverseMassSum;
			const float tangentImpulse = std::min(tangentSpeed / inverseMassSum, friction * normalImpulse);
			const auto expectedImpulse = normal * normalImpulse - direction * tangentImpulse;
			const double initialEnergy = BallisticSphereKineticEnergy(fixture.body);
			fixture.Resolve(reversed);
			const auto measuredImpulse = (fixture.body.m_LinearVelocity - initialVelocity) / 0.5f;
			const auto measuredTangent = measuredImpulse - normal * glm::dot(measuredImpulse, normal);
			Expect(fixture.body.HasFiniteState() && fixture.support.HasFiniteState() &&
				Near(fixture.body.m_LinearVelocity, initialVelocity + expectedImpulse * 0.5f, 2.0e-5f) &&
				Near(fixture.support.m_LinearVelocity, -expectedImpulse * (dynamicSupport ? 0.25f : 0.0f), 2.0e-5f) &&
				Near(fixture.body.m_AngularVelocity, GEngine::Vec3f(0.0f), 0.0f),
				"grazing/sliding/sticking ballistic response matches analytic mass, restitution, and Coulomb impulses");
			Expect(glm::dot(measuredImpulse, normal) >= 0.0f &&
				glm::length(measuredTangent) <= friction * normalImpulse + 5.0e-6f &&
				BallisticSphereKineticEnergy(fixture.body) + BallisticSphereKineticEnergy(fixture.support) <= initialEnergy + 2.0e-5,
				"ballistic tangential impulse obeys the normal-supported disk and does not add kinetic energy");
		}
	}

	void TestBallisticMaterialRange()
	{
		for (const float coefficient : { 1.0e30f, std::numeric_limits<float>::max() })
		for (const float closingSpeed : { 2.0f, 1.0e-30f })
		{
			BallisticFixture fixture;
			fixture.body.m_Friction = fixture.support.m_Friction = coefficient;
			fixture.body.m_Elasticity = fixture.support.m_Elasticity = 0.0f;
			fixture.body.m_LinearVelocity = GEngine::Vec3f(6.0f, -closingSpeed, 8.0f);
			fixture.Resolve();
			Expect(fixture.body.HasFiniteState() && Near(fixture.body.m_LinearVelocity, GEngine::Vec3f(0.0f)),
				"large finite ballistic coefficient products keep supported friction without a cap or overflow");
		}
		for (const auto coefficients : { GEngine::Vec3f(0.0f, 1.0f, 0.0f), GEngine::Vec3f(1.0f, 0.0f, 0.0f),
			GEngine::Vec3f(-1.0f, 1.0f, 0.0f), GEngine::Vec3f(1.0f, -1.0f, 0.0f), GEngine::Vec3f(-1.0f, -1.0f, 0.0f) })
		{
			BallisticFixture fixture;
			fixture.body.m_Friction = coefficients.x;
			fixture.support.m_Friction = coefficients.y;
			fixture.body.m_LinearVelocity = GEngine::Vec3f(6.0f, -2.0f, 8.0f);
			fixture.Resolve();
			Expect(fixture.body.HasFiniteState() && Near(fixture.body.m_LinearVelocity, GEngine::Vec3f(6.0f, 0.5f, 8.0f)),
				"a zero or negative material coefficient disables ballistic friction on either body");
		}
		BallisticFixture immovable;
		immovable.body.SetBodyTypeAndInverseMass(GEngine::Component::BodyType::Kinematic, 1.0f);
		immovable.body.m_LinearVelocity = GEngine::Vec3f(6.0f, -2.0f, 8.0f);
		immovable.Resolve();
		Expect(immovable.body.HasFiniteState() && immovable.support.HasFiniteState() &&
			Near(immovable.body.m_LinearVelocity, GEngine::Vec3f(6.0f, -2.0f, 8.0f), 0.0f),
			"zero effective mass preserves prescribed motion and finite ballistic output");
	}

	void TestBallisticOffCenterFriction()
	{
		for (const bool reversed : { false, true })
		for (const float initialTangent : { -1.0f, 0.0f, 3.0f })
		for (const float friction : { 0.1f, 1.0f })
		{
			BallisticFixture fixture;
			fixture.body.m_Elasticity = fixture.support.m_Elasticity = 0.0f;
			fixture.body.m_Friction = friction;
			fixture.support.m_Friction = 1.0f;
			fixture.contact.ptOnA_LocalSpace = GEngine::Vec3f(0.5f, -1.0f, 0.0f);
			fixture.body.m_LinearVelocity = GEngine::Vec3f(initialTangent, -2.0f, 0.0f);
			// Unit sphere: inverse inertia 5/2. Normal impulse creates positive tangential slip.
			const float normalImpulse = 16.0f / 13.0f;
			const float postNormalSlip = initialTangent + 20.0f / 13.0f;
			const float tangentImpulse = std::min(postNormalSlip / 3.5f, friction * normalImpulse);
			const auto expectedVelocity = GEngine::Vec3f(initialTangent - tangentImpulse, -2.0f + normalImpulse, 0.0f);
			const auto expectedOmega = GEngine::Vec3f(0.0f, 0.0f, 2.5f * (0.5f * normalImpulse - tangentImpulse));
			const double normalOnlyEnergy = 0.5 * (initialTangent * initialTangent + 100.0 / 169.0) + 0.2 * 400.0 / 169.0;
			fixture.Resolve(reversed);
			Expect(fixture.body.HasFiniteState() && Near(fixture.body.m_LinearVelocity, expectedVelocity, 2.0e-5f) &&
				Near(fixture.body.m_AngularVelocity, expectedOmega, 2.0e-5f) &&
				BallisticSphereKineticEnergy(fixture.body) <= normalOnlyEnergy + 2.0e-5,
				"off-center ballistic friction opposes post-normal slip with the rotational effective mass");
		}
	}

	void TestBallisticSweptGrazingContact()
	{
		for (const bool reversed : { false, true })
		{
			BallisticFixture fixture;
			fixture.body.m_Position = GEngine::Vec3f(-3.0f, 0.0f, 0.0f);
			fixture.support.m_Position = GEngine::Vec3f(0.0f, 1.5f, 0.0f);
			fixture.body.m_Elasticity = fixture.support.m_Elasticity = 0.0f;
			fixture.body.m_LinearVelocity = GEngine::Vec3f(8.0f, 0.0f, 0.0f);
			const bool hit = GEngine::Collision::Intersect(&fixture.body, &fixture.support, 0.5f, fixture.contact);
			Expect(hit && fixture.contact.timeOfImpact > 0.0f && fixture.contact.timeOfImpact < 0.5f,
				"analytic swept sphere grazing fixture produces a positive TOI");
			if (!hit) continue;
			fixture.body.Update(fixture.contact.timeOfImpact);
			const auto before = fixture.body.m_LinearVelocity;
			const auto normal = fixture.contact.normal;
			const float normalImpulse = -glm::dot(before, normal);
			fixture.Resolve(reversed);
			const auto impulse = fixture.body.m_LinearVelocity - before;
			const auto tangent = impulse - normal * glm::dot(impulse, normal);
			Expect(fixture.body.HasFiniteState() && Near(glm::dot(impulse, normal), normalImpulse, 2.0e-5f) &&
				Near(glm::length(tangent), 0.25f * normalImpulse, 2.0e-5f) &&
				BallisticSphereKineticEnergy(fixture.body) < 32.0,
				"generated positive-TOI grazing contact saturates its Coulomb bound and dissipates energy");
		}
	}


	void TestRestitutionThresholdResponse()
	{
		using BodyType = GEngine::Component::BodyType;
		// Expected restitution is explicit at the boundary, independent of the production constant.
		struct SpeedCase { float closing, restitutionScale; };
		const SpeedCase speeds[] = {
			{ 0.0f, 0.0f }, { 0.5f, 0.0f }, { 0.9999f, 0.0f },
			{ 1.0f, 0.0f }, { 1.0001f, 1.0f }, { 4.0f, 1.0f }
		};
		for (const auto supportType : { BodyType::Static, BodyType::Dynamic, BodyType::Kinematic })
		for (const bool reversed : { false, true })
		for (const auto speed : speeds)
		for (const auto materials : { GEngine::Vec3f(0.5f, 0.8f, 0.0f),
			GEngine::Vec3f(0.8f, 0.5f, 0.0f), GEngine::Vec3f(0.0f, 1.0f, 0.0f),
			GEngine::Vec3f(1.0f, 1.0f, 0.0f) })
		{
			BallisticFixture fixture;
			fixture.body.m_InvMass = 0.5f;
			fixture.support.SetBodyTypeAndInverseMass(supportType, 0.25f);
			fixture.body.m_Friction = fixture.support.m_Friction = 0.0f;
			fixture.body.m_Elasticity = materials.x;
			fixture.support.m_Elasticity = materials.y;
			// A shared velocity must not turn slow relative motion into a high-speed impact.
			const GEngine::Vec3f supportVelocity = supportType == BodyType::Static
				? GEngine::Vec3f(0.0f) : GEngine::Vec3f(7.0f, -8.0f, 2.0f);
			fixture.support.m_LinearVelocity = supportVelocity;
			const auto initialVelocity = supportVelocity + GEngine::Vec3f(6.0f, -speed.closing, 8.0f);
			fixture.body.m_LinearVelocity = initialVelocity;
			const float inverseMassB = supportType == BodyType::Dynamic ? 0.25f : 0.0f;
			const float restitution = materials.x * materials.y * speed.restitutionScale;
			const GEngine::Vec3f impulse(0.0f, (1.0f + restitution) * speed.closing / (0.5f + inverseMassB), 0.0f);
			fixture.Resolve(reversed);
			Expect(fixture.body.HasFiniteState() && fixture.support.HasFiniteState() &&
				Near(fixture.body.m_LinearVelocity, initialVelocity + 0.5f * impulse, 2.0e-5f) &&
				Near(fixture.support.m_LinearVelocity, supportVelocity - inverseMassB * impulse, 2.0e-5f) &&
				Near(fixture.body.m_LinearVelocity.y - fixture.support.m_LinearVelocity.y,
					restitution * speed.closing, 2.0e-5f),
				"restitution boundary uses relative normal speed and the symmetric material product");
		}

		// Spin can either create a fast impact or reduce a fast COM approach to a slow contact.
		for (const bool reversed : { false, true })
		for (const bool spinningSupport : { false, true })
		for (const bool fastContact : { false, true })
		{
			BallisticFixture fixture;
			fixture.body.m_Friction = fixture.support.m_Friction = 0.0f;
			fixture.body.m_Elasticity = fixture.support.m_Elasticity = 1.0f;
			fixture.contact.ptOnA_LocalSpace = GEngine::Vec3f(0.5f, -1.0f, 0.0f);
			fixture.contact.ptOnB_LocalSpace = GEngine::Vec3f(0.5f, 1.0f, 0.0f);
			fixture.body.m_LinearVelocity.y = fastContact ? -0.25f : -2.0f;
			const float spin = fastContact ? -4.0f : 3.5f;
			if (spinningSupport)
			{
				fixture.support.SetBodyTypeAndInverseMass(BodyType::Kinematic, 0.0f);
				fixture.support.m_AngularVelocity.z = -spin;
			}
			else fixture.body.m_AngularVelocity.z = spin;
			fixture.Resolve(reversed);
			const auto velocityA = fixture.body.m_LinearVelocity +
				glm::cross(fixture.body.m_AngularVelocity, fixture.contact.ptOnA_LocalSpace);
			const auto velocityB = fixture.support.GetLinearVelocity() +
				glm::cross(fixture.support.GetAngularVelocity(), fixture.contact.ptOnB_LocalSpace);
			Expect(fixture.body.HasFiniteState() && fixture.support.HasFiniteState() &&
				Near(velocityA.y - velocityB.y, fastContact ? 2.25f : 0.0f, 2.0e-5f),
				"restitution threshold includes angular contact velocity on either participant");
		}
	}

	void TestRestitutionSphereDrops()
	{
		for (const int rate : { 120, 240 })
		for (const bool reversed : { false, true })
		for (const bool highDrop : { false, true })
		{
			GEngine::ShapeSphere sphere(1.0f);
			// Match the established audit single-sphere floor geometry.
			GEngine::ShapeBox floor(BoxPoints(GEngine::Vec3f(50.0f, 0.5f, 50.0f)));
			GEngine::PhysicsSystem system;
			auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f, -12.0f, 0.0f));
			system.SetPhysicsWorld(world);
			auto* first = world->CreateRigidBody3D();
			auto* second = world->CreateRigidBody3D();
			auto* body = reversed ? first : second;
			auto* support = reversed ? second : first;
			ConfigureSphereBody(*body, sphere, GEngine::Vec3f(0.0f, highDrop ? 1.5f : 1.02f, 0.0f));
			ConfigureBoxBody(*support, floor, GEngine::Vec3f(0.0f, -0.5f, 0.0f),
				GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f));
			support->SetBodyTypeAndInverseMass(GEngine::Component::BodyType::Static, 0.0f);
			body->m_Elasticity = highDrop ? 0.5f : 1.0f;
			support->m_Elasticity = highDrop ? 0.8f : 1.0f;
			body->m_Friction = support->m_Friction = 0.5f;
			const float dt = 1.0f / static_cast<float>(rate);
			const double initialEnergy = 12.0 * body->m_Position.y;
			double peakEnergy = initialEnergy;
			float peakSpeed = 0.0f, peakOmega = 0.0f, peakUpward = 0.0f, maxPenetration = 0.0f;
			float finalWindowSpeed = 0.0f, finalWindowOmega = 0.0f, finalWindowGap = 0.0f;
			float firstClosing = 0.0f, firstRebound = 0.0f;
			bool finite = true, impacted = false;
			// Four seconds; every sample in the final second must satisfy the settling criterion.
			for (int step = 0; step < 4 * rate; ++step)
			{
				const float incoming = body->m_LinearVelocity.y - 12.0f * dt;
				system.Update(GEngine::Timestep(dt));
				finite = finite && body->HasFiniteState() && support->HasFiniteState();
				const float speed = glm::length(body->m_LinearVelocity);
				const float omega = glm::length(body->m_AngularVelocity);
				const double energy = BallisticSphereKineticEnergy(*body) + 12.0 * body->m_Position.y;
				finite = finite && std::isfinite(energy);
				peakEnergy = std::max(peakEnergy, energy);
				peakSpeed = std::max(peakSpeed, speed);
				peakOmega = std::max(peakOmega, omega);
				peakUpward = std::max(peakUpward, body->m_LinearVelocity.y);
				maxPenetration = std::max(maxPenetration, 1.0f - body->m_Position.y);
				if (!impacted && incoming < -0.1f && body->m_LinearVelocity.y >= -0.01f)
				{
					impacted = true;
					firstClosing = -incoming;
					firstRebound = body->m_LinearVelocity.y;
				}
				if (step >= 3 * rate)
				{
					finalWindowSpeed = std::max(finalWindowSpeed, speed);
					finalWindowOmega = std::max(finalWindowOmega, omega);
					finalWindowGap = std::max(finalWindowGap, std::abs(body->m_Position.y - 1.0f));
				}
			}
			Expect(finite && impacted && maxPenetration <= 0.02f && peakEnergy <= initialEnergy + 0.005,
				"sphere drop remains finite with bounded penetration and no mechanical-energy growth");
			Expect(finalWindowSpeed <= 0.05f && finalWindowOmega <= 0.05f && finalWindowGap <= 0.005f &&
				GetManifolds(system).GetContactCount() > 0,
				"sphere drop settles in persistent support for the full final second");
			if (highDrop)
				Expect(firstClosing > 1.0f && firstRebound > 1.0f &&
					Near(firstRebound / firstClosing, 0.4f, 0.02f),
					"high-speed sphere drop retains the intended material-product rebound");
			else
				Expect(firstClosing < 1.0f && peakUpward <= 0.05f,
					"low-height fully elastic sphere does not receive a ballistic rebound");
			std::cout << "RESTITUTION_DROP rate=" << rate << " reversed=" << reversed << " high=" << highDrop
				<< " first_closing=" << firstClosing << " first_rebound=" << firstRebound
				<< " peak_energy=" << peakEnergy << " initial_energy=" << initialEnergy
				<< " peak_speed=" << peakSpeed << " peak_omega=" << peakOmega
				<< " max_penetration=" << maxPenetration << " final_window_speed=" << finalWindowSpeed
				<< " final_window_omega=" << finalWindowOmega << " final_window_gap=" << finalWindowGap
				<< " manifolds=" << GetManifolds(system).m_Manifolds.size()
				<< " points=" << GetManifolds(system).GetContactCount() << " finite=" << finite << '\n';
		}
	}

	int RunRestitutionThresholdRegression()
	{
		TestRestitutionThresholdResponse();
		TestRestitutionSphereDrops();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused restitution-threshold checks failed\n";
			return 1;
		}
		std::cout << "Restitution-threshold regression: " << testCount << " checks passed\n";
		return 0;
	}

	int RunBallisticContactRegression()
	{
		TestBallisticSeparatingGuard();
		TestBallisticCoulombImpulses();
		TestBallisticMaterialRange();
		TestBallisticOffCenterFriction();
		TestBallisticSweptGrazingContact();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused ballistic-contact checks failed\n";
			return 1;
		}
		std::cout << "Ballistic-contact regression: " << testCount << " checks passed\n";
		return 0;
	}

	// Zero-lever-arm fixture isolates the impulse disk from angular/normal coupling.
	struct FrictionFixture
	{
		GEngine::ShapeSphere sphere{ 1.0f };
		GEngine::RigidBody3D body, support;
		GEngine::ConstraintPenetration constraint;

		FrictionFixture(float friction = 0.25f, float inverseMass = 1.0f)
		{
			ConfigureSphereBody(body, sphere, GEngine::Vec3f(0.0f));
			ConfigureSphereBody(support, sphere, GEngine::Vec3f(0.0f));
			body.m_InvMass = inverseMass;
			support.SetBodyTypeAndInverseMass(GEngine::Component::BodyType::Static, 0.0f);
			body.m_Friction = friction;
			support.m_Friction = 1.0f;
			constraint.m_bodyA = &body;
			constraint.m_bodyB = &support;
			constraint.m_anchorA = constraint.m_anchorB = GEngine::Vec3f(0.0f);
			constraint.m_Normal = GEngine::Vec3f(0.0f, -1.0f, 0.0f);
		}
	};

	double TangentImpulseLength(const GEngine::ConstraintPenetration& constraint)
	{
		return std::hypot(static_cast<double>(constraint.m_CachedLambda[1]),
			static_cast<double>(constraint.m_CachedLambda[2]));
	}

	void TestRestingCoulombProjection()
	{
		for (const float inverseMass : { 0.5f, 2.0f })
		{
			for (const float friction : { 0.0f, 0.25f, 2.0f })
			{
				for (const float closingSpeed : { 0.0f, 0.02f, 40.0f })
				{
					for (const auto tangentVelocity : { GEngine::Vec3f(100.0f, 0.0f, 0.0f),
						GEngine::Vec3f(60.0f, 0.0f, 80.0f), GEngine::Vec3f(0.0006f, 0.0f, 0.0008f) })
					{
						FrictionFixture fixture(friction, inverseMass);
						fixture.body.m_Orientation = glm::angleAxis(0.63f, GEngine::Vec3f(0.0f, 1.0f, 0.0f));
						fixture.body.m_LinearVelocity = tangentVelocity + GEngine::Vec3f(0.0f, -closingSpeed, 0.0f);
						auto& constraint = fixture.constraint;
						constraint.PreSolve(1.0f / 120.0f);
						constraint.Solve();
						const float speed = glm::length(tangentVelocity);
						const auto expectedVelocity = tangentVelocity * std::max(0.0f, 1.0f - friction * closingSpeed / speed);
						const float expectedNormal = closingSpeed / inverseMass;
						const float expectedTangent = std::min(speed, friction * closingSpeed) / inverseMass;
						Expect(fixture.body.HasFiniteState() && fixture.support.HasFiniteState() &&
							Near(constraint.m_CachedLambda[0], expectedNormal, 2.0e-5f) &&
							Near(static_cast<float>(TangentImpulseLength(constraint)), expectedTangent, 2.0e-5f) &&
							TangentImpulseLength(constraint) <= friction * constraint.m_CachedLambda[0] + 2.0e-5,
							"resting friction uses the accumulated normal impulse and one 2D Coulomb disk");
						Expect(Near(fixture.body.m_LinearVelocity, expectedVelocity, 2.0e-5f) &&
							Near(fixture.body.m_AngularVelocity, GEngine::Vec3f(0.0f)) &&
							Near(fixture.support.m_LinearVelocity, GEngine::Vec3f(0.0f)),
							"unsupported, sliding, and sticking impulses match analytic translational response");
						const auto firstVelocity = fixture.body.m_LinearVelocity;
						for (int iteration = 0; iteration < 3; ++iteration) constraint.Solve();
						Expect(Near(fixture.body.m_LinearVelocity, firstVelocity, 3.0e-5f) &&
							Near(static_cast<float>(TangentImpulseLength(constraint)), expectedTangent, 3.0e-5f),
							"zero incremental normal impulse retains friction supported by accumulated lambda");
					}
				}
			}
		}
	}

	void TestFrictionWarmStartAndRetraction()
	{
		for (const float newFriction : { 0.125f, 0.0f, -1.0f, 1.0e30f, std::numeric_limits<float>::max() })
		{
			FrictionFixture fixture(0.5f);
			fixture.body.m_LinearVelocity = GEngine::Vec3f(6.0f, -2.0f, 8.0f);
			auto& constraint = fixture.constraint;
			constraint.PreSolve(1.0f / 120.0f);
			constraint.Solve();
			Expect(Near(constraint.m_CachedLambda[0], 2.0f) &&
				Near(static_cast<float>(TangentImpulseLength(constraint)), 1.0f),
				"warm-start fixture acquires a real saturated supporting friction impulse");
			fixture.body.m_Friction = newFriction;
			fixture.support.m_Friction = newFriction > 1.0f ? 1.0e30f : 1.0f;
			fixture.body.m_LinearVelocity = GEngine::Vec3f(0.0f);
			constraint.PreSolve(1.0f / 120.0f);
			const float tangentLimit = newFriction > 1.0f ? 1.0f : newFriction == 0.125f ? 0.25f : 0.0f;
			Expect((constraint.m_Friction > 0.0f) == (newFriction > 0.0f) &&
				Near(static_cast<float>(TangentImpulseLength(constraint)), tangentLimit) &&
				Near(fixture.body.m_LinearVelocity, GEngine::Vec3f(-0.6f * tangentLimit, 2.0f, -0.8f * tangentLimit)),
				"warm starting projects cached tangents onto the current finite material limit");
			// No new closing velocity: this solve must retract both normal support and friction.
			constraint.Solve();
			Expect(Near(constraint.m_CachedLambda[0], 0.0f) && TangentImpulseLength(constraint) < 1.0e-6 &&
				Near(fixture.body.m_LinearVelocity, GEngine::Vec3f(0.0f)) && fixture.body.HasFiniteState(),
				"lost normal support retracts previously applied friction using the impulse delta");
		}

		FrictionFixture reducedSupport(0.5f);
		reducedSupport.body.m_LinearVelocity = GEngine::Vec3f(60.0f, -40.0f, 80.0f);
		auto& constraint = reducedSupport.constraint;
		constraint.PreSolve(1.0f / 120.0f);
		constraint.Solve();
		const auto oldVelocity = reducedSupport.body.m_LinearVelocity;
		reducedSupport.body.m_LinearVelocity.y = 10.0f;
		constraint.Solve();
		Expect(Near(constraint.m_CachedLambda[0], 30.0f) &&
			Near(static_cast<float>(TangentImpulseLength(constraint)), 15.0f) &&
			Near(reducedSupport.body.m_LinearVelocity, oldVelocity + GEngine::Vec3f(3.0f, 0.0f, 4.0f)),
			"partial support reduction shrinks the accumulated disk and applies only the retraction");

		FrictionFixture largeCache;
		largeCache.constraint.m_CachedLambda[0] = 1.0f;
		largeCache.constraint.m_CachedLambda[1] = 1.0e30f;
		largeCache.constraint.m_CachedLambda[2] = 1.0e30f;
		largeCache.constraint.PreSolve(1.0f / 120.0f);
		Expect(largeCache.body.HasFiniteState() &&
			Near(static_cast<float>(TangentImpulseLength(largeCache.constraint)), 0.25f),
			"large finite cached tangents project without squared-length overflow");

		FrictionFixture negativeNormal;
		negativeNormal.constraint.m_CachedLambda[0] = -1.0f;
		negativeNormal.constraint.m_CachedLambda[1] = 0.5f;
		negativeNormal.constraint.PreSolve(1.0f / 120.0f);
		Expect(Near(negativeNormal.constraint.m_CachedLambda[0], 0.0f) &&
			TangentImpulseLength(negativeNormal.constraint) == 0.0 &&
			Near(negativeNormal.body.m_LinearVelocity, GEngine::Vec3f(0.0f)),
			"unsupported cached friction and negative cached normal do not warm start");
	}

	void TestLargeFiniteFrictionCoefficients()
	{
		for (const float coefficient : { 1.0e30f, std::numeric_limits<float>::max() })
		{
			FrictionFixture fixture(coefficient);
			fixture.support.m_Friction = coefficient;
			fixture.body.m_LinearVelocity = GEngine::Vec3f(6.0f, -2.0f, 8.0f);
			fixture.constraint.PreSolve(1.0f / 120.0f);
			fixture.constraint.Solve();
			Expect(fixture.body.HasFiniteState() && fixture.support.HasFiniteState() &&
				Near(fixture.body.m_LinearVelocity, GEngine::Vec3f(0.0f)) &&
				Near(fixture.constraint.m_CachedLambda[0], 2.0f) &&
				Near(static_cast<float>(TangentImpulseLength(fixture.constraint)), 10.0f),
				"large finite material products retain enough friction to stop supported sliding");

			FrictionFixture tinySupport(coefficient);
			tinySupport.support.m_Friction = coefficient;
			tinySupport.constraint.m_CachedLambda[0] = 1.0e-30f;
			tinySupport.constraint.m_CachedLambda[1] = 1.0e20f;
			tinySupport.constraint.m_CachedLambda[2] = -1.0e20f;
			tinySupport.constraint.PreSolve(1.0f / 120.0f);
			// A float-range cap on mu would incorrectly shrink these supported tangents.
			Expect(tinySupport.body.HasFiniteState() &&
				tinySupport.constraint.m_CachedLambda[0] == 1.0e-30f &&
				tinySupport.constraint.m_CachedLambda[1] == 1.0e20f &&
				tinySupport.constraint.m_CachedLambda[2] == -1.0e20f,
				"double coefficient product and Coulomb limit retain large tangents without a cap");
		}
	}

	void TestFrictionSlidingAndRolling()
	{
		const GEngine::Vec3f direction(0.6f, 0.0f, 0.8f);
		const GEngine::Vec3f lever(0.0f, -1.0f, 0.0f);
		for (const int rate : { 120, 240 })
		{
			for (const bool driven : { false, true })
			{
				FrictionFixture fixture(0.25f, 0.5f);
				fixture.body.m_Position = GEngine::Vec3f(0.0f, 1.0f, 0.0f);
				fixture.constraint.m_anchorA = lever;
				fixture.body.m_LinearVelocity = driven ? GEngine::Vec3f(0.0f) : direction * 10.0f;
				const float dt = 1.0f / rate;
				double peakKineticEnergy = driven ? 0.0 : 100.0;
				double maxConeExcess = 0.0;
				float maxPenetration = 0.0f, firstStepSpeed = 0.0f, maxSupportedSlip = 0.0f;
				bool allFinite = true;
				for (int step = 0; step < rate * 2; ++step)
				{
					fixture.body.m_LinearVelocity += GEngine::Vec3f(0.0f, -10.0f * dt, 0.0f);
					if (driven) fixture.body.m_LinearVelocity += direction * (2.0f * dt);
					// Analytic sphere/plane contact: isotropic inertia permits a fixed orientation.
					fixture.constraint.m_anchorB = fixture.body.m_Position + lever;
					fixture.constraint.PreSolve(dt);
					fixture.constraint.Solve();
					fixture.body.m_Position += fixture.body.m_LinearVelocity * dt;
					const auto slip = fixture.body.m_LinearVelocity + glm::cross(fixture.body.m_AngularVelocity, lever);
					maxSupportedSlip = std::max(maxSupportedSlip, glm::length(slip));
					if (step == 0) firstStepSpeed = glm::dot(fixture.body.m_LinearVelocity, direction);
					maxPenetration = std::max(maxPenetration, std::max(0.0f, 1.0f - fixture.body.m_Position.y));
					maxConeExcess = std::max(maxConeExcess, TangentImpulseLength(fixture.constraint) -
						0.25 * fixture.constraint.m_CachedLambda[0]);
					const double kinetic = glm::length2(fixture.body.m_LinearVelocity) +
						0.4 * glm::length2(fixture.body.m_AngularVelocity); // mass 2, unit radius
					peakKineticEnergy = std::max(peakKineticEnergy, kinetic);
					allFinite = allFinite && fixture.body.HasFiniteState() && std::isfinite(kinetic);
				}
				const float expectedSpeed = driven ? 20.0f / 7.0f : 50.0f / 7.0f;
				const auto finalSlip = fixture.body.m_LinearVelocity + glm::cross(fixture.body.m_AngularVelocity, lever);
				Expect(allFinite && maxConeExcess < 1.0e-6 && maxPenetration < 1.0e-5f &&
					Near(fixture.body.m_LinearVelocity, direction * expectedSpeed, 2.0e-4f) &&
					Near(fixture.body.m_AngularVelocity, glm::cross(GEngine::Vec3f(0.0f, 1.0f, 0.0f), direction) * expectedSpeed, 2.0e-4f) &&
					glm::length(finalSlip) < 2.0e-5f,
					"fixed-step supported sphere reaches analytic rolling with finite state and bounded friction");
				Expect(driven ? maxSupportedSlip < 2.0e-5f :
					Near(firstStepSpeed, 10.0f - 2.5f * dt, 2.0e-5f) && peakKineticEnergy <= 100.0001,
					"static friction preserves no-slip rolling and sliding friction dissipates at the Coulomb rate");
				std::cout << "FRICTION_PLANE rate=" << rate << " driven=" << driven
					<< " peak_kinetic=" << peakKineticEnergy << " final_speed=" << glm::length(fixture.body.m_LinearVelocity)
					<< " final_omega=" << glm::length(fixture.body.m_AngularVelocity)
					<< " final_slip=" << glm::length(finalSlip) << " max_penetration=" << maxPenetration
					<< " max_cone_excess=" << maxConeExcess << " finite=" << allFinite << '\n';
			}
		}
	}

	int RunRestingFrictionRegression()
	{
		TestRestingCoulombProjection();
		TestFrictionWarmStartAndRetraction();
		TestLargeFiniteFrictionCoefficients();
		TestFrictionSlidingAndRolling();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused resting-friction checks failed\n";
			return 1;
		}
		std::cout << "Resting-friction regression: " << testCount << " checks passed\n";
		return 0;
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

	struct BodyTypeConstraintProbe : GEngine::Constraint
	{
		using Constraint::GetInverseMassMatrix;
		using Constraint::GetVelocities;
	};

	void TestBodyTypeInvariants()
	{
		using GEngine::Component::BodyType;
		GEngine::ShapeSphere sphere(1.0f);
		std::vector<GEngine::Vec3f> points = UnitBoxPoints();
		for (auto& point : points) point *= GEngine::Vec3f(1.0f, 2.0f, 3.0f);
		GEngine::ShapeBox box(points);
		const GEngine::Vec3f velocity(0.25f, -0.5f, 0.75f), omega(0.7f, 1.1f, 1.6f);
		const GEngine::Quat orientation = glm::angleAxis(0.73f,
			glm::normalize(GEngine::Vec3f(1.0f, -2.0f, 3.0f)));
		for (const auto type : { BodyType::Static, BodyType::Kinematic })
		{
			for (const float inverseMass : { 0.0f, 0.5f, 3.0f })
			{
				GEngine::RigidBody3D body;
				ConfigureBoxBody(body, box, GEngine::Vec3f(1.0f, 2.0f, 3.0f), orientation);
				body.GetInverseInertiaTensorWorldSpace();
				body.Type = type;
				body.m_InvMass = inverseMass;
				body.m_LinearVelocity = velocity;
				body.m_AngularVelocity = omega;
				Expect(Near(body.GetInverseInertiaTensorBodySpace(), GEngine::Mat3(0.0f), 0.0f) &&
					Near(body.GetInverseInertiaTensorWorldSpace(), GEngine::Mat3(0.0f), 0.0f),
					"non-dynamic type changes zero warmed inverse inertia regardless of configured mass");
				body.ApplyImpulseLinear(GEngine::Vec3f(1.0f, 2.0f, 3.0f));
				body.ApplyImpulseAngular(GEngine::Vec3f(-1.0f, 2.0f, 0.5f));
				body.ApplyImpulse(body.GetCenterOfMassWorldSpace() + GEngine::Vec3f(1.0f, 0.0f, 0.0f),
					GEngine::Vec3f(0.0f, 2.0f, 1.0f));
				Expect(Near(body.m_LinearVelocity, velocity, 0.0f) && Near(body.m_AngularVelocity, omega, 0.0f),
					"all impulse entry points preserve Static and Kinematic stored velocities");
				for (int step = 0; step < 120; ++step) body.Update(1.0f / 120.0f);
				const bool kinematic = type == BodyType::Kinematic;
				const GEngine::Vec3f expectedPosition = GEngine::Vec3f(1.0f, 2.0f, 3.0f) +
					(kinematic ? velocity : GEngine::Vec3f(0.0f));
				const GEngine::Quat expectedOrientation = kinematic
					? glm::angleAxis(glm::length(omega), glm::normalize(omega)) * orientation : orientation;
				Expect(body.HasFiniteState() && Near(body.m_Position, expectedPosition, 2.0e-5f) &&
					OrientationResidual(body.m_Orientation, expectedOrientation) < 2.0e-5f &&
					Near(body.m_LinearVelocity, velocity, 0.0f) && Near(body.m_AngularVelocity, omega, 0.0f),
					"Static poses stay fixed and Kinematic poses follow prescribed translation and asymmetric rotation");
				body.Type = BodyType::Dynamic;
				body.m_InvMass = 0.5f;
				Expect(Near(body.GetInverseInertiaTensorBodySpace(), glm::inverse(box.InertiaTensor()) * 0.5f),
					"returning to Dynamic restores inverse inertia after non-dynamic cache use");
			}
		}

		GEngine::PhysicsSystem system;
		auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f, -12.0f, 0.0f));
		system.SetPhysicsWorld(world);
		int index = 0;
		for (const auto type : { BodyType::Static, BodyType::Kinematic, BodyType::Dynamic })
		{
			auto* body = world->CreateRigidBody3D();
			ConfigureSphereBody(*body, sphere, GEngine::Vec3f(10.0f * index++, 0.0f, 0.0f));
			body->Type = type;
			body->m_InvMass = 0.5f;
			body->m_LinearVelocity = GEngine::Vec3f(1.0f, 0.0f, 0.0f);
		}
		system.Update(GEngine::Timestep(0.25f));
		const auto& bodies = world->GetPhysicsBodies();
		Expect(Near(bodies[0]->m_Position, GEngine::Vec3f(0.0f), 0.0f),
			"world integration leaves a positive-mass moving Static body fixed");
		Expect(Near(bodies[1]->m_Position, GEngine::Vec3f(10.25f, 0.0f, 0.0f)) &&
			Near(bodies[1]->m_LinearVelocity, GEngine::Vec3f(1.0f, 0.0f, 0.0f), 0.0f),
			"world gravity preserves the Kinematic prescribed trajectory");
		Expect(Near(bodies[2]->m_Position, GEngine::Vec3f(20.25f, -0.75f, 0.0f)) &&
			Near(bodies[2]->m_LinearVelocity, GEngine::Vec3f(1.0f, -3.0f, 0.0f)),
			"Dynamic body retains the analytic semi-implicit gravity trajectory");
	}

	void TestBodyTypeContacts()
	{
		using GEngine::Component::BodyType;
		GEngine::ShapeSphere sphere(1.0f);
		for (const auto type : { BodyType::Static, BodyType::Kinematic })
		{
			for (const bool ballistic : { false, true })
			{
				GEngine::Vec3f referenceVelocity(0.0f), referenceOmega(0.0f);
				for (const float inverseMass : { 0.0f, 4.0f })
				{
					GEngine::RigidBody3D bodyA, bodyB;
					ConfigureSphereBody(bodyA, sphere, GEngine::Vec3f(0.0f));
					ConfigureSphereBody(bodyB, sphere, GEngine::Vec3f(2.0f, 0.0f, 0.0f));
					bodyB.GetInverseInertiaTensorWorldSpace();
					bodyB.Type = type;
					bodyB.m_InvMass = inverseMass;
					bodyA.m_Elasticity = bodyB.m_Elasticity = 0.0f;
					bodyA.m_Friction = bodyB.m_Friction = 0.5f;
					bodyA.m_LinearVelocity = GEngine::Vec3f(2.0f, 1.0f, 0.0f);
					const GEngine::Vec3f prescribed(type == BodyType::Kinematic ? 1.0f : -3.0f, 0.0f, 0.0f);
					bodyB.m_LinearVelocity = prescribed;
					BodyTypeConstraintProbe probe;
					probe.m_bodyA = &bodyA;
					probe.m_bodyB = &bodyB;
					const auto mass = probe.GetInverseMassMatrix();
					bool zeroBlock = true;
					for (int row = 6; row < 12; ++row)
						for (int column = 0; column < 12; ++column) zeroBlock = zeroBlock && mass[row][column] == 0.0f;
					Expect(zeroBlock && Near(mass[0][0], 1.0f), "solver excludes all non-dynamic mass and inertia");
					Expect(Near(probe.GetVelocities()[6], type == BodyType::Kinematic ? 1.0f : 0.0f, 0.0f),
						"solver uses prescribed Kinematic motion and ignores Static velocity");
					if (ballistic)
					{
						GEngine::contact_t contact{};
						contact.m_BodyA = &bodyA;
						contact.m_BodyB = &bodyB;
						contact.ptOnA_LocalSpace = GEngine::Vec3f(1.0f, 0.0f, 0.0f);
						contact.ptOnB_LocalSpace = GEngine::Vec3f(-1.0f, 0.0f, 0.0f);
						contact.normal = GEngine::Vec3f(-1.0f, 0.0f, 0.0f);
						contact.timeOfImpact = 0.1f;
						GEngine::Collision::ResolveContact(contact);
					}
					else
					{
						GEngine::ConstraintPenetration constraint;
						constraint.m_bodyA = &bodyA;
						constraint.m_bodyB = &bodyB;
						constraint.m_anchorA = GEngine::Vec3f(1.0f, 0.0f, 0.0f);
						constraint.m_anchorB = GEngine::Vec3f(-1.0f, 0.0f, 0.0f);
						constraint.m_Normal = GEngine::Vec3f(1.0f, 0.0f, 0.0f);
						constraint.PreSolve(1.0f / 120.0f);
						constraint.Solve();
					}
					Expect(bodyA.HasFiniteState() && bodyB.HasFiniteState() &&
						Near(bodyA.m_LinearVelocity.x, type == BodyType::Kinematic ? 1.0f : 0.0f) &&
						Near(bodyB.m_LinearVelocity, prescribed, 0.0f) &&
						Near(bodyB.m_AngularVelocity, GEngine::Vec3f(0.0f), 0.0f),
						"resting and ballistic contacts accelerate only the Dynamic participant");
					if (inverseMass == 0.0f)
					{
						referenceVelocity = bodyA.m_LinearVelocity;
						referenceOmega = bodyA.m_AngularVelocity;
					}
					else Expect(Near(bodyA.m_LinearVelocity, referenceVelocity) && Near(bodyA.m_AngularVelocity, referenceOmega),
						"non-dynamic configured mass cannot change either solver's normal or friction response");
				}
			}
		}
	}

	void TestBodyTypePrediction()
	{
		using GEngine::Component::BodyType;
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::ShapeBox box(UnitBoxPoints());
		for (const bool generic : { false, true })
		{
			for (const auto type : { BodyType::Static, BodyType::Kinematic })
			{
				GEngine::RigidBody3D bodyA, bodyB;
				ConfigureSphereBody(bodyA, sphere, GEngine::Vec3f(0.0f));
				ConfigureSphereBody(bodyB, sphere, GEngine::Vec3f(4.0f, 0.0f, 0.0f));
				if (generic) bodyB.m_Shape = &box;
				bodyB.Type = type;
				bodyB.m_LinearVelocity = GEngine::Vec3f(-10.0f, 0.0f, 0.0f);
				GEngine::contact_t contact{};
				const bool hit = GEngine::Collision::Intersect(&bodyA, &bodyB, 0.3f, contact);
				Expect(hit == (type == BodyType::Kinematic) && (!hit || Near(contact.timeOfImpact, 0.2f, 0.002f)),
					"sphere and generic CCD ignore Static velocity and preserve Kinematic time of impact");
				if (!generic) Expect(GEngine::Collision::SphereSphereIntersect(&bodyA, &bodyB, 0.3f, contact) ==
					(type == BodyType::Kinematic), "direct sphere CCD follows the body-type motion contract");
			}
		}
	}

	void TestBodyTypeConfigurationAndTransitions()
	{
		using GEngine::Component::BodyType;
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::RigidBody3D body;
		body.m_Shape = &sphere;
		Expect(body.Type == BodyType::Static && body.m_InvMass == 0.0f && body.GetInverseMass() == 0.0f,
			"default body has consistent Static zero inverse mass");
		Expect(body.SetBodyTypeAndInverseMass(BodyType::Dynamic, 0.5f), "validated configuration accepts positive Dynamic inverse mass");
		body.ApplyImpulseLinear(GEngine::Vec3f(2.0f, 0.0f, 0.0f));
		body.ApplyImpulseAngular(GEngine::Vec3f(0.0f, 0.0f, 0.8f));
		Expect(Near(body.m_LinearVelocity, GEngine::Vec3f(1.0f, 0.0f, 0.0f)) &&
			Near(body.m_AngularVelocity, GEngine::Vec3f(0.0f, 0.0f, 1.0f)),
			"Dynamic linear and angular impulses retain analytic mass scaling");
		const auto inverseBefore = body.GetInverseInertiaTensorWorldSpace();
		for (const float invalid : { 0.0f, -1.0f, std::numeric_limits<float>::infinity(),
			std::numeric_limits<float>::quiet_NaN() })
		{
			Expect(!body.SetBodyTypeAndInverseMass(BodyType::Dynamic, invalid) &&
				body.Type == BodyType::Dynamic && body.m_InvMass == 0.5f &&
				Near(body.GetInverseInertiaTensorWorldSpace(), inverseBefore, 0.0f),
				"invalid Dynamic mass configuration is rejected without changing type, mass, or warm inertia");
		}
		Expect(!body.SetBodyTypeAndInverseMass(static_cast<BodyType>(-1), 1.0f) && body.Type == BodyType::Dynamic,
			"invalid body type is rejected transactionally");
		for (const auto type : { BodyType::Static, BodyType::Kinematic })
		{
			Expect(body.SetBodyTypeAndInverseMass(type, 7.0f) && body.m_InvMass == 0.0f &&
				body.GetInverseMass() == 0.0f && Near(body.GetInverseInertiaTensorWorldSpace(), GEngine::Mat3(0.0f), 0.0f),
				"validated Static and Kinematic configuration stores zero inverse mass");
		}
		for (const float invalid : { 0.0f, -1.0f, std::numeric_limits<float>::infinity(),
			std::numeric_limits<float>::quiet_NaN() })
		{
			// Legacy public writes are numerically guarded even when callers bypass validation.
			body.Type = BodyType::Dynamic;
			body.m_InvMass = invalid;
			const auto velocity = body.m_LinearVelocity;
			const auto omega = body.m_AngularVelocity;
			const auto orientation = body.m_Orientation;
			const auto position = body.m_Position;
			body.ApplyImpulseLinear(GEngine::Vec3f(1.0f));
			body.ApplyImpulseAngular(GEngine::Vec3f(1.0f));
			body.Update(0.1f);
			Expect(body.GetInverseMass() == 0.0f &&
				Near(body.GetInverseInertiaTensorWorldSpace(), GEngine::Mat3(0.0f), 0.0f) &&
				Near(body.GetLinearVelocity(), GEngine::Vec3f(0.0f), 0.0f) &&
				Near(body.GetAngularVelocity(), GEngine::Vec3f(0.0f), 0.0f) &&
				Near(body.m_Position, position, 0.0f) && OrientationResidual(body.m_Orientation, orientation) == 0.0f &&
				Near(body.m_LinearVelocity, velocity, 0.0f) && Near(body.m_AngularVelocity, omega, 0.0f),
				"invalid legacy Dynamic mass cannot enter effective solver state, impulses, or integration");
		}

		GEngine::RigidBody3D other;
		ConfigureSphereBody(body, sphere, GEngine::Vec3f(0.0f));
		ConfigureSphereBody(other, sphere, GEngine::Vec3f(2.0f, 0.0f, 0.0f));
		GEngine::ConstraintPenetration constraint;
		constraint.m_bodyA = &body;
		constraint.m_bodyB = &other;
		constraint.m_anchorA = GEngine::Vec3f(1.0f, 0.0f, 0.0f);
		constraint.m_anchorB = GEngine::Vec3f(-1.0f, 0.0f, 0.0f);
		constraint.m_Normal = GEngine::Vec3f(1.0f, 0.0f, 0.0f);
		body.m_LinearVelocity = GEngine::Vec3f(2.0f, 0.0f, 0.0f);
		constraint.PreSolve(1.0f / 120.0f);
		constraint.Solve();
		Expect(constraint.m_CachedLambda[0] > 0.0f, "type-transition fixture creates a real cached normal impulse");
		body.Type = BodyType::Static;
		other.Type = BodyType::Kinematic;
		const auto velocityA = body.m_LinearVelocity;
		const auto velocityB = other.m_LinearVelocity;
		constraint.PreSolve(1.0f / 120.0f);
		constraint.Solve();
		Expect(body.HasFiniteState() && other.HasFiniteState() &&
			Near(body.m_LinearVelocity, velocityA, 0.0f) && Near(other.m_LinearVelocity, velocityB, 0.0f),
			"warm starting after both bodies become non-dynamic cannot apply cached impulses");
	}

	void TestBodyTypeWorldContacts()
	{
		using GEngine::Component::BodyType;
		GEngine::ShapeSphere sphere(1.0f);
		for (const auto type : { BodyType::Static, BodyType::Kinematic })
		{
			GEngine::PhysicsSystem system;
			auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f));
			system.SetPhysicsWorld(world);
			auto* dynamic = world->CreateRigidBody3D();
			auto* driver = world->CreateRigidBody3D();
			ConfigureSphereBody(*dynamic, sphere, GEngine::Vec3f(0.0f));
			ConfigureSphereBody(*driver, sphere, GEngine::Vec3f(2.5f, 0.0f, 0.0f));
			driver->Type = type;
			driver->m_InvMass = 4.0f;
			dynamic->m_Friction = driver->m_Friction = 0.0f;
			dynamic->m_Elasticity = driver->m_Elasticity = 0.0f;
			dynamic->m_LinearVelocity = GEngine::Vec3f(2.0f, 0.0f, 0.0f);
			const bool kinematic = type == BodyType::Kinematic;
			driver->m_LinearVelocity = GEngine::Vec3f(kinematic ? 1.0f : -3.0f, 0.0f, 0.0f);
			system.Update(GEngine::Timestep(1.0f));
			Expect(dynamic->HasFiniteState() && driver->HasFiniteState() &&
				Near(dynamic->m_LinearVelocity.x, kinematic ? 1.0f : 0.0f) &&
				Near(dynamic->m_Position.x, kinematic ? 1.5f : 0.5f) &&
				Near(driver->m_Position.x, kinematic ? 3.5f : 2.5f) &&
				Near(driver->m_LinearVelocity.x, kinematic ? 1.0f : -3.0f),
				"full world CCD and response preserve analytic Static/Kinematic driver and Dynamic trajectories");
		}
	}
	int RunBodyTypeRegression()
	{
		TestBodyTypeInvariants();
		TestBodyTypeContacts();
		TestBodyTypePrediction();
		TestBodyTypeConfigurationAndTransitions();
		TestBodyTypeWorldContacts();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " focused body-type checks failed\n";
			return 1;
		}
		std::cout << "Body-type regression: " << testCount << " checks passed\n";
		return 0;
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
		if (argument == "--restitution-threshold") return RunRestitutionThresholdRegression();
		if (argument == "--ballistic-contact") return RunBallisticContactRegression();
		if (argument == "--resting-friction") return RunRestingFrictionRegression();
		if (argument == "--contact-convention") return RunContactConventionRegression();
		if (argument == "--body-types") return RunBodyTypeRegression();
		if (argument == "--angular-dynamics")
		{
			return RunAngularDynamicsRegression();
		}
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
		if (argument == "--sphere-validity")
		{
			return RunSphereValidityRegression();
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
	TestSphereAndBaseValidityContract();
	TestSphereContacts();
	TestBodyRemovalLifetimeRegression();
	TestMultiManifoldBodyRemovalRegression();
	TestTransientContactBodyRemovalRegression();
	TestStableBodyIdentityRegression();
	TestReadOnlyPhysicsBodyStorageRegression();
	TestPhysicsWorldResetAndRestartRegression();
	TestSceneRuntimeLifecycleRegression();
	TestConvexValidityContract();
	TestContactPairOrderRegression();
	TestCollisionContactConvention();
	TestContactImpulsePermutation();
	TestPersistentContactPermutation();
	TestDegenerateGjkDirection();
	TestZeroQuaternionBodyUpdate();
	TestBoxConstructionInvariant();
	TestBoxContactRegression();
	TestSmallBoxStackRegression();
	TestGoldenRotations();
	TestRotatedAsymmetricBox();
	TestTorqueFreeAngularDynamics();
	TestAngularBodyTypeGuard();
	TestAngularCacheAgainstFreshInverse();
	TestAngularRewindDiagnostic();
	TestDerivedDataInvalidation();
	TestWarmCacheOrientationInvalidationAndReuse();
	TestSphereRadiusInvalidationContract();
	TestConstraintDenominators();
	TestRestitutionThresholdResponse();
	TestRestitutionSphereDrops();
	TestBallisticSeparatingGuard();
	TestBallisticCoulombImpulses();
	TestBallisticMaterialRange();
	TestBallisticOffCenterFriction();
	TestBallisticSweptGrazingContact();
	TestRestingCoulombProjection();
	TestFrictionWarmStartAndRetraction();
	TestLargeFiniteFrictionCoefficients();
	TestFrictionSlidingAndRolling();
	TestGravityAndInverseMass();
	TestBodyTypeInvariants();
	TestBodyTypeContacts();
	TestBodyTypePrediction();
	TestBodyTypeConfigurationAndTransitions();
	TestBodyTypeWorldContacts();
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
