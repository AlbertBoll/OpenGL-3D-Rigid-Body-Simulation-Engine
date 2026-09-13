#include "Phase32ExactBoxWorld.h"
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
#include <GEngine/Physics/BoxContact.h>
#include <GEngine/Physics/ShapeConvex.h>
#include <GEngine/Physics/ShapeSphere.h>
#include <GEngine/Scene/_Entity.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <vector>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

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

	struct ManifoldConstraintsTag
	{
		using Type = GEngine::ConstraintPenetration (GEngine::Manifold::*)[4];
		friend Type GetPrivateMember(ManifoldConstraintsTag);
	};
	template struct PrivateMemberAccess<ManifoldConstraintsTag, &GEngine::Manifold::m_Constraints>;

	GEngine::ConstraintPenetration& CachedConstraint(GEngine::Manifold& manifold, int slot)
	{
		return (manifold.*GetPrivateMember(ManifoldConstraintsTag{}))[slot];
	}

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


	// Phase 39 locks complete contact-row responses to the approved heap-math baseline.
	// The MSVC x64 /fp:precise fingerprint covers scalars, never padding or pointers.
#if defined(_MSC_VER) && defined(_DEBUG)
	std::size_t solverAllocations = 0;
	int CountSolverAllocation(int operation, void*, std::size_t, int, long, const unsigned char*, int)
	{
		if (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC) ++solverAllocations;
		return TRUE;
	}
#endif

	void TestSolverStorage()
	{
		using namespace GEngine;
		static_assert(std::is_trivially_copyable_v<SolverMath::Vector<3>>);
		static_assert(std::is_trivially_copyable_v<SolverMath::Matrix<3, 12>>);
		static_assert(sizeof(SolverMath::Matrix<3, 12>) == 36 * sizeof(float));
		auto points = UnitBoxPoints();
		for (auto& point : points) point *= Vec3f(1, 2, 3);
		ShapeBox shapeA(UnitBoxPoints()), shapeB(points);
		std::uint64_t fingerprint = 14695981039346656037ull;
		const auto hash = [&](auto value) {
			unsigned char bytes[sizeof(value)];
			std::memcpy(bytes, &value, sizeof(value));
			for (unsigned char byte : bytes) { fingerprint ^= byte; fingerprint *= 1099511628211ull; }
		};
		std::size_t allocations = 0;
#if defined(_MSC_VER) && defined(_DEBUG)
		const auto originalHook = _CrtSetAllocHook(CountSolverAllocation);
		solverAllocations = 0;
		{ Vec<12> sentinel; sentinel[0] = 1.0f; }
		_CrtSetAllocHook(originalHook);
		Expect(solverAllocations > 0, "solver allocation hook observes the original heap-backed math");
#endif
		hash(1.0f);
		for (int sample = 0; sample < 96; ++sample) {
			RigidBody3D a, b;
			a.m_Shape = &shapeA; b.m_Shape = &shapeB;
			a.SetBodyTypeAndInverseMass(sample % 4 == 3 ? Component::BodyType::Static : Component::BodyType::Dynamic, 0.75f);
			b.SetBodyTypeAndInverseMass(sample % 4 == 0 ? Component::BodyType::Dynamic :
				(sample % 4 == 2 ? Component::BodyType::Kinematic : Component::BodyType::Static), 1.25f);
			a.m_Orientation = glm::angleAxis(0.013f * sample, glm::normalize(Vec3f(1, 2, 3)));
			b.m_Orientation = glm::angleAxis(-0.021f * sample, glm::normalize(Vec3f(3, -1, 2)));
			a.m_Position = Vec3f(-0.1f, 0.2f, 0.3f); b.m_Position = Vec3f(0.2f, -0.1f, 0.4f);
			a.m_LinearVelocity = Vec3f(0.125f * (sample % 7 - 3), -0.75f, 0.25f);
			b.m_LinearVelocity = Vec3f(-0.125f, 0.25f, -0.125f * (sample % 5));
			a.m_AngularVelocity = Vec3f(0.25f, -0.5f, 0.125f * (sample % 3));
			b.m_AngularVelocity = Vec3f(-0.5f, 0.125f, 0.25f);
			a.m_Friction = sample % 3 == 0 ? 0.0f : 0.5f; b.m_Friction = 0.75f;
			a.SetSpinResistanceLength(sample % 2 ? 0.05f : 0.0f); b.SetSpinResistanceLength(0.05f);
			a.SetRollingResistanceLength(sample % 3 ? 0.05f : 0.0f); b.SetRollingResistanceLength(0.05f);
#if defined(_MSC_VER) && defined(_DEBUG)
			solverAllocations = 0;
			_CrtSetAllocHook(CountSolverAllocation);
#endif
			{
				ConstraintPenetration original;
				original.m_bodyA = &a; original.m_bodyB = &b;
				original.m_anchorA = Vec3f(0.125f, -0.25f, 0.5f);
				original.m_anchorB = Vec3f(-0.25f, 0.125f, -0.125f);
				original.m_Normal = glm::normalize(Vec3f(0.01f * (sample % 5), -1.0f, 0.02f * (sample % 7)));
				original.m_CachedLambda[0] = sample % 2 ? 0.125f : 0.0f;
				original.m_CachedLambda[1] = 0.03125f;
				original.m_CachedLambda[2] = -0.015625f;
				ConstraintPenetration contact = original;
				original = contact;
				const float dt[] = { 1.0f/60, 1.0f/120, 0.0f, -1.0f/60 };
				for (int step = 0; step < 3; ++step) {
					contact.PreSolve(dt[(sample / 4) % 4]);
					for (int pass = 0; pass < 6; ++pass) {
						if (pass % 2) contact.SolveFriction(); else contact.Solve();
						for (int row = 0; row < 3; ++row) {
							hash(contact.m_CachedLambda[row]);
							for (int column = 0; column < 12; ++column) hash(contact.m_Jacobian[row][column]);
						}
						hash(contact.GetSpinImpulse());
						hash(contact.GetRollingImpulse().x); hash(contact.GetRollingImpulse().y);
						for (RigidBody3D* body : { &a, &b })
							for (int axis = 0; axis < 3; ++axis) {
								hash(body->m_LinearVelocity[axis]); hash(body->m_AngularVelocity[axis]);
							}
					}
					contact.PostSolve();
					for (int axis = 0; axis < 3; ++axis) { hash(a.m_Position[axis]); hash(b.m_Position[axis]); }
				}
			}
#if defined(_MSC_VER) && defined(_DEBUG)
			_CrtSetAllocHook(originalHook);
			allocations += solverAllocations;
#endif
			Expect(a.HasFiniteState() && b.HasFiniteState(), "fixed-storage contact response remains finite across body/material/dt cases");
		}
		std::cout << "Solver storage: fingerprint=" << fingerprint << " allocations=" << allocations
			<< " constraint_bytes=" << sizeof(ConstraintPenetration) << '\n';
#if defined(_MSC_VER) && defined(_M_X64)
		Expect(fingerprint == 14496816551934000004ull, "contact state exactly matches the Phase 38 heap-math reference");
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
		Expect(allocations == 0, "contact construction/copy/warm start/solve/post-solve perform no CRT allocations");
#endif
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


	void TestAbsoluteSphereScaling()
	{
		using namespace GEngine;
		ShapeSphere sphere(2.0f);
		PhysicalShape& shape = sphere; // Match virtual dispatch through the scale callback.
		RigidBody3D body;
		ConfigureSphereBody(body, sphere, Vec3f(4, 2, -3));
		body.GetWorldBounds();
		body.GetInverseInertiaTensorWorldSpace();
		for (float scale : { 1.0f, 2.0f, 3.0f, 3.0f, 0.5f, 1.0f }) {
			const float oldRadius = sphere.GetRadius();
			const auto revision = sphere.GetRevision();
			shape.HandleScaleChanged(Vec3f(scale));
			const float radius = 2.0f * scale;
			Expect(sphere.GetRadius() == radius, "sphere absolute scale uses the base radius, including repeats and reset");
			Expect(sphere.GetRevision() == revision + (oldRadius != radius ? 1 : 0),
				"sphere scale changes geometry revision exactly once only when radius changes");
			Expect(Near(shape.GetBounds().mins, Vec3f(-radius), 0) &&
				Near(shape.GetBounds().maxs, Vec3f(radius), 0) &&
				Near(shape.Support(Vec3f(1, 0, 0), body.m_Position, body.m_Orientation, 0),
					body.m_Position + Vec3f(radius, 0, 0), 0), "scaled sphere has analytic finite bounds and support");
			Expect(Near(body.GetWorldBounds().mins, body.m_Position - Vec3f(radius)) &&
				Near(body.GetWorldBounds().maxs, body.m_Position + Vec3f(radius)) &&
				Near(body.GetInverseInertiaTensorWorldSpace(), Mat3(2.5f / (radius * radius))),
				"sphere absolute scaling refreshes warmed bounds and inverse inertia");
		}
		shape.HandleScaleChanged(Vec3f(3, 4, 5));
		Expect(sphere.GetRadius() == 6, "sphere retains its existing X-axis policy for nonuniform scale");
		sphere.SetRadius(sphere.GetRadius());
		shape.HandleScaleChanged(Vec3f(2));
		Expect(sphere.GetRadius() == 4, "unchanged SetRadius is a no-op and preserves the unscaled source");
		sphere.SetRadius(1.5f);
		shape.HandleScaleChanged(Vec3f(3));
		Expect(sphere.GetRadius() == 4.5f, "changed SetRadius establishes the new base radius for absolute scaling");
		const auto revision = sphere.GetRevision();
		const auto bounds = body.GetWorldBounds();
		const auto inertia = body.GetInverseInertiaTensorWorldSpace();
		for (float invalid : { 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max() }) {
			shape.HandleScaleChanged(Vec3f(invalid));
			Expect(sphere.GetRadius() == 4.5f && sphere.GetRevision() == revision &&
				Near(body.GetWorldBounds().mins, bounds.mins, 0) &&
				Near(body.GetWorldBounds().maxs, bounds.maxs, 0) &&
				Near(body.GetInverseInertiaTensorWorldSpace(), inertia, 0),
				"invalid or overflowed sphere scale preserves geometry, revision and caches");
		}
		sphere.SetRadius(-1);
		shape.HandleScaleChanged(Vec3f(2));
		Expect(sphere.GetRadius() == 3, "rejected radius and scale updates preserve the sphere base");
	}

	template<class Shape>
	void TestAbsolutePointScaling()
	{
		using namespace GEngine;
		const Vec3f halfExtents(1, 2, 3);
		auto input = BoxPoints(halfExtents);
		Shape shape(input);
		input[0] = Vec3f(999); // The source is owned by the shape, not by the caller.
		PhysicalShape& physical = shape;
		RigidBody3D body;
		body.m_Shape = &shape;
		body.m_Position = Vec3f(4, 2, -3);
		body.m_Orientation = glm::angleAxis(0.37f, Math::NormalizeOr(Vec3f(1, 2, 3)));
		body.SetBodyTypeAndInverseMass(BodyType::Dynamic, 0.5f);
		const Mat3 rotation = glm::toMat3(body.m_Orientation);
		body.GetWorldBounds();
		body.GetCenterOfMassWorldSpace();
		body.GetInverseInertiaTensorWorldSpace();
		const Vec3f scales[] = { Vec3f(1), Vec3f(2), Vec3f(3), Vec3f(3),
			Vec3f(0.5f), Vec3f(2, 0.5f, 1.5f), Vec3f(-2, 0.5f, 1.5f), Vec3f(1) };
		for (const auto& scale : scales) {
			const auto revision = shape.GetRevision();
			physical.HandleScaleChanged(scale);
			const Vec3f extent = halfExtents * glm::abs(scale);
			Expect(shape.IsValid() && shape.GetRevision() == revision + 1,
				"successful point-shape scale rebuild publishes one geometry revision");
			Expect(Near(shape.GetBounds().mins, -extent, 0) && Near(shape.GetBounds().maxs, extent, 0),
				"box and convex absolute scales do not compound, including repeat, signed and nonuniform scales");
			Shape reference(BoxPoints(extent));
			Expect(Near(shape.GetCenterOfMass(), reference.GetCenterOfMass(), 2e-4f),
				"scaled point-shape centroid agrees with fresh geometry under existing mass-property sampling");
			const Vec3f worldExtent = glm::abs(rotation[0]) * extent.x +
				glm::abs(rotation[1]) * extent.y + glm::abs(rotation[2]) * extent.z;
			Expect(Near(body.GetWorldBounds().mins, body.m_Position - worldExtent, 2e-4f) &&
				Near(body.GetWorldBounds().maxs, body.m_Position + worldExtent, 2e-4f) &&
				Near(body.GetCenterOfMassWorldSpace(), body.m_Position + rotation * reference.GetCenterOfMass(), 2e-4f),
				"point-shape scale refreshes warmed rotated world bounds and COM");
			Expect(Near(shape.InertiaTensor(), reference.InertiaTensor(), 2e-4f) &&
				Near(body.GetInverseInertiaTensorWorldSpace(),
					rotation * (glm::inverse(reference.InertiaTensor()) * 0.5f) * glm::transpose(rotation), 2e-4f),
				"scaled point-shape inertia matches fresh geometry and refreshes the body cache");
			const Vec3f direction = Math::NormalizeOr(Vec3f(1, 2, 3));
			Expect(Near(shape.Support(direction, body.m_Position, body.m_Orientation, 0),
				reference.Support(direction, body.m_Position, body.m_Orientation, 0), 2e-4f),
				"scaled point-shape support agrees with independently constructed geometry");
		}
		const auto revision = shape.GetRevision();
		const auto bounds = body.GetWorldBounds();
		const auto inertia = body.GetInverseInertiaTensorWorldSpace();
		const Vec3f invalidScales[] = { Vec3f(0), Vec3f(1, 0, 1),
			Vec3f(std::numeric_limits<float>::quiet_NaN(), 1, 1),
			Vec3f(1, std::numeric_limits<float>::infinity(), 1), Vec3f(std::numeric_limits<float>::max()) };
		for (const auto& invalid : invalidScales) {
			physical.HandleScaleChanged(invalid);
			Expect(shape.IsValid() && shape.GetRevision() == revision &&
				Near(body.GetWorldBounds().mins, bounds.mins, 0) &&
				Near(body.GetWorldBounds().maxs, bounds.maxs, 0) &&
				Near(body.GetInverseInertiaTensorWorldSpace(), inertia, 0),
				"rejected point-shape scale is transactional and retains warmed caches");
		}
		physical.HandleScaleChanged(Vec3f(2));
		Expect(Near(shape.GetBounds().maxs, halfExtents * 2.0f, 0),
			"point-shape source survives rejected rebuilds");
		auto replacement = BoxPoints(Vec3f(2, 1, 0.5f));
		for (auto& point : replacement) { point += Vec3f(1, -2, 0.5f); }
		physical.Build(replacement);
		physical.Build({}); // Failed explicit replacement must not overwrite the valid base.
		physical.HandleScaleChanged(Vec3f(3));
		Expect(Near(shape.GetBounds().mins, Vec3f(-3, -9, 0), 0) &&
			Near(shape.GetBounds().maxs, Vec3f(9, -3, 3), 0),
			"explicit Build replaces the base source; subsequent absolute scale includes the model offset");
		physical.HandleScaleChanged(Vec3f(1));
		Expect(Near(shape.GetBounds().mins, Vec3f(-1, -3, 0), 0) &&
			Near(shape.GetBounds().maxs, Vec3f(3, -1, 1), 0), "scale one restores the replacement base geometry");
	}

	void TestScaleSourceExceptionSafety()
	{
		using namespace GEngine;
		class ThrowingRebuildShape : public ShapeBox
		{
		public:
			using ShapeBox::ShapeBox;
			bool failBuild = true;
			void Build(const std::vector<Vec3f>& points) override
			{
				if (failBuild) {
					m_MeshPoints = points;
					throw std::runtime_error("injected rebuild failure");
				}
				ShapeBox::Build(points);
			}
		};
		ThrowingRebuildShape shape(UnitBoxPoints());
		const auto revision = shape.GetRevision();
		bool threw = false;
		try { shape.HandleScaleChanged(Vec3f(2)); }
		catch (const std::runtime_error&) { threw = true; }
		Expect(threw && shape.GetRevision() == revision && Near(shape.GetBounds().maxs, Vec3f(1), 0),
			"scale callback propagates a rebuild exception without publishing geometry");
		shape.failBuild = false;
		shape.HandleScaleChanged(Vec3f(3));
		Expect(Near(shape.GetBounds().maxs, Vec3f(3), 0), "scale callback restores the base source after a rebuild exception");
	}

	int RunAbsoluteScalingRegression()
	{
		TestScaleSourceExceptionSafety();
		TestAbsoluteSphereScaling();
		TestAbsolutePointScaling<GEngine::ShapeBox>();
		TestAbsolutePointScaling<GEngine::ShapeConvex>();
		if (failureCount) { std::cerr << failureCount << " of " << testCount << " absolute-scaling checks failed\n"; return 1; }
		std::cout << "Absolute-scaling regression: " << testCount << " checks passed\n";
		return 0;
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
		std::swap(reversed.featureA, reversed.featureB);
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

	void TestRuntimePoseApi()
	{
		using namespace GEngine;
		ShapeSphere sphere(1.0f);
		ShapeBox box(BoxPoints(Vec3f(1.0f, 2.0f, 3.0f)));
		PhysicsSystem system;
		auto* world = new PhysicsWorld(Vec3f(0.0f));
		system.SetPhysicsWorld(world);
		for (float x : { 0.0f, 2.0f, 20.0f, 22.0f })
			ConfigureSphereBody(*world->CreateRigidBody3D(), sphere, Vec3f(x, 0.0f, 0.0f));
		system.Update(Timestep(1.0f / 120.0f));
		auto* body = world->GetPhysicsBodies()[0];
		auto& manifolds = GetManifolds(system).m_Manifolds;
		Expect(manifolds.size() == 2, "pose API fixture has two independent warm-start pairs");
		if (manifolds.size() != 2) return;
		for (auto& manifold : manifolds)
		{
			CachedConstraint(manifold, 0).m_CachedLambda[0] = 3.0f;
			auto contact = manifold.GetContact(0);
			contact.timeOfImpact = 0.5f;
			GetTransientContacts(system).push_back(contact);
		}
		const Vec3f original = body->m_Position;
		const Quat rotation = body->m_Orientation;
		const auto pairsBefore = GetCollisionPairs(system);
		auto unchanged = [&]()
		{
			return body->m_Position == original && body->m_Orientation == rotation &&
				manifolds.size() == 2 && GetTransientContacts(system).size() == 2 &&
				GetCollisionPairs(system) == pairsBefore && CachedConstraint(manifolds[0], 0).m_CachedLambda[0] == 3.0f;
		};
		Expect(system.SetBodyPose(body, original, rotation) && unchanged(),
			"identical pose preserves manifolds, impulses, TOIs and broad-phase pairs");
		Expect(system.SetBodyPose(body, original, -rotation) && unchanged(),
			"quaternion sign equivalence does not discard warm-start state");
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();
		for (const Vec3f position : { Vec3f(nan, 0.0f, 0.0f), Vec3f(0.0f, inf, 0.0f) })
			Expect(!system.SetBodyPose(body, position, rotation) && unchanged(),
				"non-finite API translations reject without mutating pose or caches");
		for (const Quat invalid : { Quat(0.0f, 0.0f, 0.0f, 0.0f), Quat(nan, 0.0f, 0.0f, 0.0f),
			Quat(inf, 0.0f, 0.0f, 0.0f), Quat(1e-10f, 0.0f, 0.0f, 0.0f), Quat(1e30f, 0.0f, 0.0f, 0.0f) })
			Expect(!system.SetBodyPose(body, original + Vec3f(5.0f), invalid) && unchanged(),
				"degenerate or unnormalizable API quaternion rejects the complete transaction");
		RigidBody3D foreign;
		Expect(!system.SetBodyPose(&foreign, Vec3f(1.0f), rotation) && unchanged() && foreign.m_Position == Vec3f(0.0f),
			"pose API rejects foreign bodies without changing either world");
		Expect(!system.SetBodyPose(nullptr, original, rotation) && unchanged(), "pose API rejects null bodies");
		PhysicsSystem stopped;
		Expect(!stopped.SetBodyPose(body, original, rotation) && unchanged(), "pose API rejects calls without an active world");

		body->m_LinearVelocity = Vec3f(0.1f, 0.2f, 0.3f);
		body->m_AngularVelocity = Vec3f(0.3f, 0.2f, 0.1f);
		Expect(system.SetBodyPose(body, Vec3f(24.0f, 0.0f, 0.0f), rotation * 2.0f) &&
			body->m_Position == Vec3f(24.0f, 0.0f, 0.0f) && Near(glm::length(body->m_Orientation), 1.0f) &&
			body->m_LinearVelocity == Vec3f(0.1f, 0.2f, 0.3f) && body->m_AngularVelocity == Vec3f(0.3f, 0.2f, 0.1f),
			"accepted API teleport normalizes orientation and preserves both velocities");
		Expect(manifolds.size() == 1 && GetTransientContacts(system).size() == 1 &&
			manifolds[0].GetContact(0).m_BodyA != body && manifolds[0].GetContact(0).m_BodyB != body &&
			CachedConstraint(manifolds[0], 0).m_CachedLambda[0] == 3.0f,
			"teleport retires affected warm-start and TOI state while preserving unrelated impulses");
		Expect(GetCollisionPairs(system).empty() && GetBroadphase(system).GetLastStats().fullSortCount == 0,
			"teleport clears cached candidate and swept-bound state");
		std::vector<collisionPair_t> pairs;
		GetBroadphase(system).FindPairs(world->GetPhysicsBodies(), pairs, 0.0f);
		Expect(std::find(pairs.begin(), pairs.end(), collisionPair_t{0, 3}) != pairs.end() &&
			std::find(pairs.begin(), pairs.end(), collisionPair_t{0, 1}) == pairs.end(),
			"next broad-phase query finds the new neighbor and excludes the old neighbor");
		contact_t contact{};
		Expect(Collision::Intersect(body, world->GetPhysicsBodies()[3], contact) && Finite(contact),
			"narrow phase sees a finite contact at the teleported position");

		body->m_Shape = &box;
		body->GetWorldBounds();
		body->GetCenterOfMassWorldSpace();
		const Mat3 inverseBody = body->GetInverseInertiaTensorBodySpace();
		body->GetInverseInertiaTensorWorldSpace();
		const Quat quarterTurn = glm::angleAxis(Math::Pi * 0.5f, Vec3f(0.0f, 0.0f, 1.0f));
		const Vec3f moved(7.0f, 8.0f, 9.0f);
		Expect(system.SetBodyPose(body, moved, quarterTurn), "box accepts a translation and quarter-turn pose edit");
		const Mat3 matrix = glm::toMat3(quarterTurn);
		Expect(Near(body->GetWorldBounds().mins, moved - Vec3f(2.0f, 1.0f, 3.0f)) &&
			Near(body->GetWorldBounds().maxs, moved + Vec3f(2.0f, 1.0f, 3.0f)) &&
			Near(body->GetCenterOfMassWorldSpace(), moved) &&
			Near(body->GetInverseInertiaTensorWorldSpace(), matrix * inverseBody * glm::transpose(matrix)),
			"box teleport refreshes warmed rotated bounds, COM and world inverse inertia");
		for (float angle : { 0.3f, 1.7f, 2.4f })
		{
			system.SetBodyPose(body, moved, glm::angleAxis(angle, glm::normalize(Vec3f(1.0f, 2.0f, 3.0f))));
			const auto retainedCount = GetTransientContacts(system).size();
			GetTransientContacts(system).push_back(contact);
			const Quat accepted = body->m_Orientation;
			Expect(system.SetBodyPose(body, moved, accepted) && body->m_Orientation == accepted &&
				GetTransientContacts(system).size() == retainedCount + 1,
				"resubmitting a nontrivial accepted quaternion preserves exact pose and contact state");
			Expect(system.SetBodyPose(body, moved, -accepted) && body->m_Orientation == accepted &&
				GetTransientContacts(system).size() == retainedCount + 1,
				"resubmitting the negative accepted quaternion preserves exact pose and contact state");
			GetTransientContacts(system).pop_back();
		}
	}

	void TestRuntimeTransformSynchronization()
	{
		using namespace GEngine;
		using namespace GEngine::Component;
		// Scene shape ownership is unchanged; release this fixture's shapes after the scene.
		std::vector<std::unique_ptr<PhysicalShape>> shapes;
		_Scene scene;
		const BodyType types[] = { BodyType::Static, BodyType::Kinematic, BodyType::Dynamic };
		std::vector<_Entity> entities;
		for (int i = 0; i < 3; ++i)
		{
			auto entity = scene.CreateEntity("pose authority");
			entity.AddOrReplaceComponent<RigidBody3DComponent>().Type = types[i];
			auto& fixture = entity.AddOrReplaceComponent<SphereFixture3DComponent>();
			fixture.Radius = 0.5f;
			fixture.Property.m_Position = Vec3f(-100.0f - i * 10.0f, 0.0f, 0.0f);
			fixture.Property.m_LinearVelocity = Vec3f(float(i + 1), 0.0f, 0.0f);
			fixture.Property.m_AngularVelocity = Vec3f(0.0f, 0.0f, 0.2f);
			auto& transform = entity.GetComponent<Transform3DComponent>();
			transform.SetTranslation(Vec3f(i * 20.0f, 10.0f, 0.0f));
			transform.SetRotation(Vec3f(0.0f, 0.0f, 0.3f));
			entities.push_back(entity);
		}
		auto captureShapes = [&]()
		{
			for (auto entity : entities)
				shapes.emplace_back(entity.GetComponent<RigidBody3DComponent>().RuntimeBody->m_Shape);
		};
		scene.OnRuntimeStart();
		captureShapes();
		scene.GetPhysicsSystem()->GetPhysicsWorld()->SetGravity(Vec3f(0.0f));
		for (int i = 0; i < 3; ++i)
		{
			auto& transform = entities[i].GetComponent<Transform3DComponent>();
			auto* body = entities[i].GetComponent<RigidBody3DComponent>().RuntimeBody;
			Expect(body->m_Position == transform.Translation &&
				glm::length(body->m_Orientation - transform.QuatRotation) < 1e-6f,
				"startup pose comes from the entity transform for every body type");
			body->GetWorldBounds();
			body->GetCenterOfMassWorldSpace();
			body->GetInverseInertiaTensorWorldSpace();
			transform.SetTranslation(Vec3f(i * 20.0f, 30.0f, 4.0f));
			transform.SetRotation(Vec3f(0.0f, 0.0f, 1.0f));
		}
		scene.Update(Timestep(0.0f));
		for (int i = 0; i < 3; ++i)
		{
			auto& transform = entities[i].GetComponent<Transform3DComponent>();
			auto* body = entities[i].GetComponent<RigidBody3DComponent>().RuntimeBody;
			Expect(body->m_Position == Vec3f(i * 20.0f, 30.0f, 4.0f) &&
				glm::length(body->m_Orientation - Quat(Vec3f(0.0f, 0.0f, 1.0f))) < 1e-6f,
				"runtime translation and Euler rotation edits reach static, kinematic and dynamic bodies");
			Expect(body->m_LinearVelocity == Vec3f(float(i + 1), 0.0f, 0.0f) &&
				body->m_AngularVelocity == Vec3f(0.0f, 0.0f, 0.2f),
				"transform teleports preserve configured linear and angular velocity");
			Expect(glm::length(body->GetWorldBounds().mins - (body->m_Position - Vec3f(0.5f))) < 1e-5f &&
				glm::length(body->GetCenterOfMassWorldSpace() - body->m_Position) < 1e-5f,
				"pose edits refresh previously warmed bounds and center of mass");
			const Vec3f before = body->m_Position;
			transform.Translation.x = std::numeric_limits<float>::quiet_NaN();
			scene.Update(Timestep(0.0f));
			Expect(body->m_Position == before && transform.Translation == before && body->HasFiniteState(),
				"invalid transform position is rejected and the accepted pose is republished");
			transform.SetTranslation(before + Vec3f(0.0f, 2.0f, 0.0f));
			transform.SetRotation(Quat(0.0f, 0.0f, 0.0f, 0.0f));
			scene.Update(Timestep(0.0f));
			Expect(body->m_Position == before && transform.Translation == before &&
				glm::length(transform.QuatRotation) > 0.99f,
				"invalid orientation rejects the entire pose edit transactionally");
			transform.Translation = before + Vec3f(0.0f, 3.0f, 0.0f);
			transform.QuatRotation = Quat(Vec3f(0.0f, 0.0f, 0.5f)) * 2.0f;
			scene.Update(Timestep(0.0f));
			Expect(body->m_Position == before + Vec3f(0.0f, 3.0f, 0.0f) &&
				std::abs(glm::length(body->m_Orientation) - 1.0f) < 1e-6f,
				"direct translation/quaternion edits are consumed and valid quaternions normalized");
		}
		for (int i = 0; i < 3; ++i)
		{
			auto* body = entities[i].GetComponent<RigidBody3DComponent>().RuntimeBody;
			const Vec3f before = body->m_Position;
			scene.Update(Timestep(_Scene::PhysicsStepSeconds));
			const Vec3f expected = before + (types[i] == BodyType::Static ? Vec3f(0.0f) :
				Vec3f(float(i + 1) * static_cast<float>(_Scene::PhysicsStepSeconds), 0.0f, 0.0f));
			Expect(glm::length(body->m_Position - expected) < 1e-5f &&
				entities[i].GetComponent<Transform3DComponent>().Translation == body->m_Position,
				"static pose stays fixed; kinematic and dynamic integration publishes without feedback");
			body->m_Position += Vec3f(0.0f, 0.0f, 1.0f);
			scene.Update(Timestep(0.0f));
			Expect(glm::length(body->m_Position - (expected +
				(types[i] == BodyType::Static ? Vec3f(0.0f) : Vec3f(0.0f, 0.0f, 1.0f)))) < 1e-5f,
				"static entity pose remains authoritative; moving bodies accept physics-side pose changes");
		}
		scene.OnRuntimeStop();
		entities[2].GetComponent<Transform3DComponent>().SetTranslation(Vec3f(90.0f, 40.0f, 0.0f));
		scene.Update(Timestep(0.0f));
		scene.OnRuntimeStart();
		captureShapes();
		Expect(entities[2].GetComponent<RigidBody3DComponent>().RuntimeBody->m_Position == Vec3f(90.0f, 40.0f, 0.0f),
			"restart imports the current transform without replaying stale fixture poses");
		scene.OnRuntimeStop();
		auto invalid = scene.CreateEntity("invalid startup pose");
		invalid.AddOrReplaceComponent<RigidBody3DComponent>();
		invalid.AddOrReplaceComponent<SphereFixture3DComponent>();
		invalid.GetComponent<Transform3DComponent>().SetRotation(Quat(0.0f, 0.0f, 0.0f, 0.0f));
		scene.OnRuntimeStart();
		captureShapes();
		Expect(invalid.GetComponent<RigidBody3DComponent>().RuntimeBody == nullptr &&
			scene.GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies().size() == 3,
			"invalid startup pose creates no runtime body or pose bridge");
		scene.Update(Timestep(0.0f));
		auto* world = scene.GetPhysicsSystem()->GetPhysicsWorld();
		auto* removed = entities[2].GetComponent<RigidBody3DComponent>().RuntimeBody;
		world->RemoveRigidBody3D(removed);
		auto* replacement = world->CreateRigidBody3D();
		ConfigureSphereBody(*replacement, *static_cast<ShapeSphere*>(shapes.back().get()), Vec3f(200.0f));
		entities[2].GetComponent<Transform3DComponent>().SetTranslation(Vec3f(500.0f));
		scene.Update(Timestep(0.0f));
		Expect(replacement->m_Position == Vec3f(200.0f),
			"stale bridge generation cannot edit or publish a replacement body");
		scene.GetPhysicsSystem()->SetPhysicsWorld(new PhysicsWorld(Vec3f(0.0f)));
		scene.Update(Timestep(0.0f));
		Expect(scene.GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies().empty(),
			"world replacement leaves old scene pose bridges inert until restart");
		scene.OnRuntimeStop();
	}

	void LoadSchedulingBoxWorld(GEngine::PhysicsSystem& system,
		std::vector<std::unique_ptr<GEngine::PhysicalShape>>& shapes)
	{
		using namespace GEngine;
		auto* world = new PhysicsWorld(Vec3f(0, -12, 0));
		system.SetPhysicsWorld(world);
		std::istringstream input(Phase32ExactBoxWorld);
		std::size_t count = 0; input >> count;
		bool valid = count == 21;
		for (std::size_t i = 0; i < count; ++i) {
			auto* body = world->CreateRigidBody3D();
			int shapeType = 0, type = 0; float radius = 0; std::size_t pointsCount = 0;
			input >> shapeType >> type >> body->m_InvMass >> body->m_Elasticity >> body->m_Friction >>
				body->m_CollisionLayer >> body->m_CollisionMask >>
				body->m_Position.x >> body->m_Position.y >> body->m_Position.z >>
				body->m_Orientation.w >> body->m_Orientation.x >> body->m_Orientation.y >> body->m_Orientation.z >>
				body->m_LinearVelocity.x >> body->m_LinearVelocity.y >> body->m_LinearVelocity.z >>
				body->m_AngularVelocity.x >> body->m_AngularVelocity.y >> body->m_AngularVelocity.z >> radius >> pointsCount;
			std::vector<Vec3f> points(pointsCount);
			for (auto& point : points) input >> point.x >> point.y >> point.z;
			valid = valid && input.good() && shapeType == int(ShapeType::Box) && pointsCount == 36;
			body->Type = BodyType(type);
			shapes.push_back(std::make_unique<ShapeBox>(points)); body->m_Shape = shapes.back().get();
		}
		Expect(valid, "scheduling comparison loads the exact approved application export in creation order");
	}

	void TestSceneScheduleEquivalence()
	{
		using namespace GEngine;
		const double dt = _Scene::PhysicsStepSeconds;
		std::vector<std::vector<double>> patterns{ std::vector<double>(60, dt),
			std::vector<double>(240, dt / 4), std::vector<double>(30, 2 * dt), {}, {}, { 12 * dt } };
		for (int i = 0; i < 30; ++i) { patterns[3].push_back(dt / 4); patterns[3].push_back(7 * dt / 4); }
		for (int i = 0; i < 15; ++i) {
			patterns[4].push_back(3 * dt); patterns[4].push_back(dt / 2); patterns[4].push_back(dt / 2);
		}
		patterns[5].insert(patterns[5].end(), 192, dt / 4);
		for (std::size_t pattern = 0; pattern < patterns.size(); ++pattern) {
			std::vector<std::unique_ptr<PhysicalShape>> shapes;
			_Scene scene; PhysicsSystem reference;
			LoadSchedulingBoxWorld(*scene.GetPhysicsSystem(), shapes);
			LoadSchedulingBoxWorld(reference, shapes);
			bool exact = true, finite = true, bounded = true, conserved = true;
			double supplied = 0, discarded = 0; std::uint64_t ticks = 0; int zeroUpdates = 0;
			for (int second = 0; second < 20; ++second)
			for (double elapsed : patterns[pattern]) {
				scene.Update(Timestep(elapsed)); supplied += elapsed;
				const auto& timing = scene.GetPhysicsTiming(); discarded += timing.discardedSeconds;
				bounded = bounded && timing.stepsLastUpdate <= _Scene::MaxPhysicsStepsPerUpdate;
				zeroUpdates += timing.stepsLastUpdate == 0;
				for (std::uint32_t step = 0; step < timing.stepsLastUpdate; ++step) {
					reference.Update(Timestep(dt)); ++ticks;
				}
				conserved = conserved && std::abs(supplied - (ticks * dt + timing.pendingSeconds + discarded)) < 1e-10;
				const auto& actual = scene.GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies();
				const auto& expected = reference.GetPhysicsWorld()->GetPhysicsBodies();
				for (std::size_t i = 0; i < actual.size(); ++i) {
					finite = finite && actual[i]->HasFiniteState();
					exact = exact && actual[i]->m_Position == expected[i]->m_Position &&
						actual[i]->m_Orientation == expected[i]->m_Orientation &&
						actual[i]->m_LinearVelocity == expected[i]->m_LinearVelocity &&
						actual[i]->m_AngularVelocity == expected[i]->m_AngularVelocity;
				}
				exact = exact && GetManifolds(*scene.GetPhysicsSystem()).GetContactCount() == GetManifolds(reference).GetContactCount();
			}
			const auto timing = scene.GetPhysicsTiming();
			Expect(exact && finite, "every rendered-schedule sample exactly matches direct fixed stepping of the approved box world");
			Expect(ticks == 1200 && timing.totalSteps == ticks && discarded == 0 && timing.pendingSeconds < 1e-10,
				"all six one-second frame patterns execute exactly 1200 ticks in 20 seconds without losing time");
			Expect(bounded && conserved, "steady, fast, slow, jitter and stall schedules obey the work cap and time balance");
			if (pattern == 1) Expect(zeroUpdates == 3600, "240 Hz rendering performs no physics on three out of four updates");
			std::cout << "SCENE_SCHEDULE pattern=" << pattern << " ticks=" << ticks << " supplied=" << supplied
				<< " pending=" << timing.pendingSeconds << " discarded=" << discarded << " exact=" << exact << '\n';
		}
	}

	void TestSceneClockPolicy()
	{
		using namespace GEngine;
		const double dt = _Scene::PhysicsStepSeconds;
		const double precise = 1.0 + 1e-10;
		const Timestep time(precise);
		Expect(time.GetSecondsPrecise() == precise && time.GetSeconds() == static_cast<float>(precise) &&
			static_cast<float>(time) == time.GetSeconds(), "Timestep preserves double elapsed time and legacy float consumers");
		Expect(time.GetMilliseconds() == static_cast<float>(1000 * precise), "Timestep millisecond conversion retains its public units");
		_Scene scene, independent;
		scene.Update(Timestep(10));
		Expect(scene.GetPhysicsTiming().totalSteps == 0 && scene.GetPhysicsTiming().pendingSeconds == 0,
			"a scene without a runtime world does not accumulate elapsed time");
		scene.OnRuntimeStart(); independent.OnRuntimeStart();
		scene.Update(Timestep(dt / 2));
		Expect(scene.GetPhysicsTiming().stepsLastUpdate == 0 && scene.GetPhysicsTiming().pendingSeconds == dt / 2,
			"a fractional tick remains pending without advancing physics");
		scene.Update(Timestep(dt / 2));
		Expect(scene.GetPhysicsTiming().totalSteps == 1 && scene.GetPhysicsTiming().stepsLastUpdate == 1 &&
			scene.GetPhysicsTiming().pendingSeconds < 1e-15, "two half-tick frames produce exactly one fixed tick");
		Expect(independent.GetPhysicsTiming().totalSteps == 0 && independent.GetPhysicsTiming().pendingSeconds == 0,
			"scenes own independent clocks");
		scene.Update(Timestep(dt - 1e-10));
		Expect(scene.GetPhysicsTiming().stepsLastUpdate == 0, "clock roundoff tolerance cannot consume a materially incomplete tick");
		scene.Update(Timestep(1e-10));
		Expect(scene.GetPhysicsTiming().stepsLastUpdate == 1, "completing a near-boundary tick advances exactly once");
		scene.Update(Timestep(1));
		const auto overflow = scene.GetPhysicsTiming();
		Expect(overflow.stepsLastUpdate == 2 && std::abs(overflow.pendingSeconds - (.25 - 2 * dt)) < 1e-14 &&
			std::abs(overflow.discardedSeconds - .75) < 1e-14, "one-second stall performs two ticks, retains backlog and reports overflow");
		Expect(std::abs(overflow.totalSteps * dt + overflow.pendingSeconds + overflow.totalDiscardedSeconds - (1 + 2 * dt)) < 1e-13,
			"executed, pending and discarded seconds account for all valid unpaused input");
		for (const double invalid : { 0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() }) {
			scene.Update(Timestep(invalid)); const auto& timing = scene.GetPhysicsTiming();
			Expect(timing.stepsLastUpdate == 0 && timing.discardedSeconds == 0 && timing.totalSteps == overflow.totalSteps &&
				timing.pendingSeconds == overflow.pendingSeconds && timing.totalDiscardedSeconds == overflow.totalDiscardedSeconds,
				"zero or invalid elapsed time neither accumulates nor drains backlog");
		}
		scene.SetPaused(true); scene.Update(Timestep(10));
		Expect(scene.GetPhysicsTiming().totalSteps == overflow.totalSteps && scene.GetPhysicsTiming().pendingSeconds == overflow.pendingSeconds,
			"paused elapsed time is excluded and prior backlog is retained");
		scene.SetPaused(false); scene.Update(Timestep(dt / 2));
		Expect(scene.GetPhysicsTiming().stepsLastUpdate == 2 && scene.GetPhysicsTiming().discardedSeconds == 0,
			"resume drains existing backlog with the same two-tick budget");
		scene.OnRuntimeStop();
		Expect(scene.GetPhysicsTiming().totalSteps == 0 && scene.GetPhysicsTiming().pendingSeconds == 0 &&
			scene.GetPhysicsTiming().totalDiscardedSeconds == 0, "runtime stop resets all clock state");
		scene.OnRuntimeStart(); scene.Update(Timestep(dt / 2));
		Expect(scene.GetPhysicsTiming().totalSteps == 0 && scene.GetPhysicsTiming().pendingSeconds == dt / 2,
			"restart has no stale catch-up debt");
		scene.GetPhysicsSystem()->SetPhysicsWorld(new PhysicsWorld()); scene.Update(Timestep(dt / 2));
		Expect(scene.GetPhysicsTiming().totalSteps == 0 && scene.GetPhysicsTiming().pendingSeconds == dt / 2,
			"a different runtime world starts a fresh clock");
		scene.GetPhysicsSystem()->OnExit(); scene.Update(Timestep(dt));
		Expect(scene.GetPhysicsTiming().pendingSeconds == 0 && scene.GetPhysicsTiming().totalSteps == 0,
			"an absent world clears pending time even after external shutdown");
		scene.OnRuntimeStart();
		for (int i = 0; i < 2; ++i) scene.Update(Timestep(std::numeric_limits<double>::max()));
		const auto huge = scene.GetPhysicsTiming();
		Expect(huge.stepsLastUpdate == 2 && std::isfinite(huge.pendingSeconds) && std::isfinite(huge.discardedSeconds) &&
			huge.totalDiscardedSeconds == std::numeric_limits<double>::max(),
			"huge finite stalls retain bounded work and saturate the lifetime discard diagnostic without infinity");

		// Deterministic model of expensive ticks feeding their cost into the next frame.
		scene.OnRuntimeStart(); double elapsed = .016, supplied = 0;
		bool bounded = true, conserved = true;
		for (int frame = 0; frame < 600; ++frame) {
			scene.Update(Timestep(elapsed)); supplied += elapsed;
			const auto& timing = scene.GetPhysicsTiming();
			bounded = bounded && timing.stepsLastUpdate <= 2 && timing.pendingSeconds <= .25;
			conserved = conserved && std::abs(supplied - (timing.totalSteps * dt + timing.pendingSeconds + timing.totalDiscardedSeconds)) < 1e-10;
			elapsed = std::max(.016, timing.stepsLastUpdate * .020 + .002);
		}
		Expect(bounded && conserved && scene.GetPhysicsTiming().totalDiscardedSeconds > 0,
			"expensive-tick feedback stays at two ticks and reports overload instead of hiding lost time");
	}

	void TestPausedScenePoseSynchronization()
	{
		using namespace GEngine; using namespace GEngine::Component;
		std::vector<std::unique_ptr<PhysicalShape>> shapes;
		_Scene scene; auto entity = scene.CreateEntity("paused pose edit");
		entity.AddOrReplaceComponent<RigidBody3DComponent>().Type = BodyType::Dynamic;
		entity.AddOrReplaceComponent<SphereFixture3DComponent>(); scene.OnRuntimeStart();
		auto* body = entity.GetComponent<RigidBody3DComponent>().RuntimeBody;
		shapes.emplace_back(body->m_Shape); body->m_LinearVelocity = Vec3f(2, 0, 0);
		scene.Update(Timestep(_Scene::PhysicsStepSeconds / 2)); scene.SetPaused(true);
		auto& transform = entity.GetComponent<Transform3DComponent>(); transform.SetTranslation(Vec3f(4, 5, 6));
		scene.Update(Timestep(1));
		Expect(body->m_Position == Vec3f(4, 5, 6) && transform.Translation == body->m_Position &&
			body->m_LinearVelocity == Vec3f(2, 0, 0) && scene.GetPhysicsTiming().totalSteps == 0,
			"paused frames synchronize authored poses without simulating or changing velocity");
		scene.SetPaused(false); scene.Update(Timestep(_Scene::PhysicsStepSeconds / 2));
		Expect(scene.GetPhysicsTiming().totalSteps == 1 && transform.Translation == body->m_Position &&
			body->m_Position.x > 4, "resume consumes the retained fraction and publishes one final physics pose");
	}

	int RunFixedSchedulingRegression()
	{
		TestSceneClockPolicy(); TestPausedScenePoseSynchronization(); TestSceneScheduleEquivalence();
		std::cout << "Fixed scheduling: " << testCount << " checks, " << failureCount << " failures\n";
		return failureCount ? 1 : 0;
	}

	int RunRuntimeTransformRegression()
	{
		TestRuntimePoseApi();
		TestRuntimeTransformSynchronization();
		if (failureCount) { std::cerr << failureCount << " of " << testCount << " runtime-transform checks failed\n"; return 1; }
		std::cout << "Runtime-transform regression: " << testCount << " checks passed\n";
		return 0;
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
			bodyEntityA.GetComponent<GEngine::Component::Transform3DComponent>().SetTranslation(fixtureA.Property.m_Position);
			bodyEntityB.GetComponent<GEngine::Component::Transform3DComponent>().SetTranslation(fixtureB.Property.m_Position);

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

	bool SameBoxFace(const GEngine::BoxFaceFeature& a, const GEngine::BoxFaceFeature& b)
	{
		if (a.id != b.id || a.shapeRevision != b.shapeRevision || !Near(a.normal, b.normal, 0.0f) ||
			a.alignment != b.alignment) return false;
		for (int corner = 0; corner < 4; ++corner)
			if (!Near(a.vertices[corner], b.vertices[corner], 0.0f)) return false;
		return true;
	}

	bool SameBoxFeatures(const GEngine::BoxContactFeatures& a, const GEngine::BoxContactFeatures& b)
	{
		return a.referenceBody == b.referenceBody && a.incidentBody == b.incidentBody &&
			SameBoxFace(a.reference, b.reference) && SameBoxFace(a.incident, b.incident);
	}

	void TestBoxFaceGeometry()
	{
		using namespace GEngine;
		const std::array<Quat, 3> rotations = { Quat(1, 0, 0, 0),
			glm::angleAxis(glm::half_pi<float>(), Vec3f(0, 1, 0)),
			glm::angleAxis(0.73f, glm::normalize(Vec3f(1, -2, 3))) };
		for (float scale : { 0.001f, 1.0f, 1000.0f }) {
			const Vec3f halfExtents = Vec3f(1, 2, 3) * scale;
			const Vec3f localOffset = Vec3f(4, -5, 6) * scale;
			auto points = BoxPoints(halfExtents);
			for (Vec3f& point : points) point += localOffset;
			ShapeBox box(points);
			const Vec3f position = Vec3f(7, 8, -9) * scale;
			for (const Quat& orientation : rotations) {
				const Mat3 rotation = glm::toMat3(orientation);
				for (int axis = 0; axis < 3; ++axis) {
					for (int sign : { -1, 1 }) {
						Vec3f localNormal(0.0f);
						localNormal[axis] = float(sign);
						const Vec3f worldNormal = rotation * localNormal;
						BoxFaceFeature face;
						const auto revision = box.GetRevision();
						Expect(box.GetContactFace(worldNormal, position, orientation, face),
							"all six faces extract at small/unit/large scale with translated asymmetric geometry");
						Expect(face.id == static_cast<BoxFaceId>(axis * 2 + (sign > 0 ? 1 : 0)) &&
							face.shapeRevision == revision && box.GetRevision() == revision,
							"face IDs encode local axis/sign and extraction preserves geometry revision");
						Expect(Near(face.normal, worldNormal) && Near(glm::length(face.normal), 1.0f) &&
							Near(face.alignment, 1.0f), "face normals are outward, unit and rotation covariant");

						// Independent oracle: filter the supplied source corners by the requested local plane.
						std::ptrdiff_t matched = 0;
						for (const Vec3f& point : points) {
							if (!Near(point[axis], localOffset[axis] + sign * halfExtents[axis], scale * 1.0e-5f)) continue;
							const Vec3f expected = rotation * point + position;
							matched += std::count_if(face.vertices.begin(), face.vertices.end(),
								[&](const Vec3f& actual) { return Near(actual, expected, scale * 1.0e-5f); });
						}
						Expect(matched == 4, "face contains exactly the four source corners on its model-space plane");
						bool outward = true;
						for (int corner = 0; corner < 4; ++corner) {
							const Vec3f edge0 = face.vertices[(corner + 1) % 4] - face.vertices[corner];
							const Vec3f edge1 = face.vertices[(corner + 2) % 4] - face.vertices[(corner + 1) % 4];
							outward &= Finite(face.vertices[corner]) && glm::dot(glm::cross(edge0, edge1), worldNormal) > 0;
						}
						Expect(outward, "every cyclic face corner has finite vertices and outward CCW winding");
					}
				}
			}
		}
	}

	void TestBoxFaceSelectionAndRebuild()
	{
		using namespace GEngine;
		ShapeBox box(UnitBoxPoints());
		const Quat identity(1, 0, 0, 0);
		struct Selection { Vec3f direction; BoxFaceId expected; };
		const Selection cases[] = {
			{ Vec3f(1, 1, 0), BoxFaceId::PositiveX }, { Vec3f(1, 0, 1), BoxFaceId::PositiveX },
			{ Vec3f(0, 1, 1), BoxFaceId::PositiveY }, { Vec3f(-1, -1, -1), BoxFaceId::NegativeX },
			{ Vec3f(1, 1.000001f, 0), BoxFaceId::PositiveX },
			{ Vec3f(1, 1.001f, 0), BoxFaceId::PositiveY },
			{ Vec3f(0, -1, -1.000001f), BoxFaceId::NegativeY },
			{ Vec3f(0, -1, -1.001f), BoxFaceId::NegativeZ }
		};
		for (const Selection& selection : cases) {
			for (float magnitude : { 1.0e-30f, 1.0f, 1.0e30f }) {
				BoxFaceFeature face;
				Expect(box.GetContactFace(selection.direction * magnitude, Vec3f(0), identity, face) &&
					face.id == selection.expected, "edge/corner ties use local axis priority independent of direction magnitude");
			}
		}
		for (float magnitude : { std::numeric_limits<float>::denorm_min(),
			std::numeric_limits<float>::min(), std::numeric_limits<float>::max() }) {
			BoxFaceFeature extreme;
			Expect(box.GetContactFace(Vec3f(magnitude, 0, 0), Vec3f(0), identity, extreme) &&
				extreme.id == BoxFaceId::PositiveX && Finite(extreme.normal) && Near(extreme.alignment, 1.0f),
				"smallest subnormal through largest finite axis directions normalize without NaN or overflow");
		}
		const Quat rotation = glm::angleAxis(0.81f, glm::normalize(Vec3f(2, 3, -1)));
		const Vec3f direction = glm::toMat3(rotation) * Vec3f(1, 1, 1);
		BoxFaceFeature face, negatedQuaternion;
		Expect(box.GetContactFace(direction, Vec3f(2, -3, 4), rotation, face) &&
			face.id == BoxFaceId::PositiveX, "a rotated exact corner tie retains the local X face");
		Expect(box.GetContactFace(direction, Vec3f(2, -3, 4), -rotation, negatedQuaternion) &&
			SameBoxFace(face, negatedQuaternion), "quaternion sign does not change selected face or vertex order");
		BoxFaceFeature slightQuaternionDrift;
		Expect(box.GetContactFace(direction, Vec3f(2, -3, 4), rotation * 1.000001f, slightQuaternionDrift) &&
			slightQuaternionDrift.id == face.id && Near(slightQuaternionDrift.normal, face.normal),
			"small accepted quaternion drift is normalized for geometric extraction");

		auto shuffled = UnitBoxPoints();
		std::reverse(shuffled.begin(), shuffled.end());
		shuffled.push_back(shuffled.front());
		ShapeBox reordered(shuffled);
		BoxFaceFeature reorderedFace;
		Expect(reordered.GetContactFace(direction, Vec3f(2, -3, 4), rotation, reorderedFace) &&
			SameBoxFace(face, reorderedFace), "input corner ordering and duplicate points do not alter face identity or winding");
		const auto initialRevision = box.GetRevision();
		auto rebuilt = BoxPoints(Vec3f(2, 3, 4));
		for (Vec3f& point : rebuilt) point += Vec3f(5, 6, 7);
		box.Build(rebuilt);
		BoxFaceFeature rebuiltFace;
		Expect(box.GetContactFace(Vec3f(1, 0, 0), Vec3f(0), identity, rebuiltFace) &&
			rebuiltFace.id == BoxFaceId::PositiveX && rebuiltFace.shapeRevision > initialRevision,
			"valid offset/asymmetric rebuild preserves local face ID and changes its geometry revision");
		bool newPlane = true;
		for (const Vec3f& vertex : rebuiltFace.vertices) newPlane &= Near(vertex.x, 7.0f);
		Expect(newPlane, "rebuild returns the new model-origin face plane rather than stale COM-relative geometry");
		box.Build({});
		BoxFaceFeature afterRejectedBuild;
		Expect(box.GetContactFace(Vec3f(1, 0, 0), Vec3f(0), identity, afterRejectedBuild) &&
			SameBoxFace(rebuiltFace, afterRejectedBuild), "rejected rebuild preserves face geometry and revision");
	}

	void TestBoxContactFeaturePairs()
	{
		using namespace GEngine;
		ShapeBox box(UnitBoxPoints());
		PhysicsWorld world;
		RigidBody3D* a = world.CreateRigidBody3D();
		RigidBody3D* b = world.CreateRigidBody3D();
		a->m_Shape = b->m_Shape = &box;
		a->m_Position = Vec3f(0, 2, 0);
		b->m_Position = Vec3f(0);
		const Quat identity(1, 0, 0, 0);
		BoxContactFeatures features;
		Expect(ExtractBoxContactFeatures(*a, *b, Vec3f(0, 1, 0), features) &&
			features.referenceBody == a->GetIdentity() && features.incidentBody == b->GetIdentity() &&
			features.reference.id == BoxFaceId::NegativeY && features.incident.id == BoxFaceId::PositiveY,
			"aligned B-to-A contact selects the facing faces with the earlier identity as reference");
		const auto reference = features;
		for (float perturbation : { -1.0e-6f, 0.0f, 1.0e-6f }) {
			BoxContactFeatures forward, reverse;
			const Vec3f normal(perturbation, 1, -perturbation);
			Expect(ExtractBoxContactFeatures(*a, *b, normal, forward) &&
				ExtractBoxContactFeatures(*b, *a, -normal, reverse) && SameBoxFeatures(forward, reverse) &&
				forward.referenceBody == reference.referenceBody && forward.reference.id == reference.reference.id,
				"small normal changes and reversed pair traversal retain the same physical reference and incident faces");
		}
		for (float angle : { 0.001f, 0.2f }) {
			a->m_Orientation = glm::angleAxis(angle, Vec3f(0, 0, 1));
			BoxContactFeatures reverse;
			Expect(ExtractBoxContactFeatures(*a, *b, Vec3f(0, 1, 0), features) &&
				features.referenceBody == (angle < 0.01f ? a->GetIdentity() : b->GetIdentity()),
				"near alignment ties prefer stable identity; a clearly better face becomes reference");
			Expect(ExtractBoxContactFeatures(*b, *a, Vec3f(0, -1, 0), reverse) &&
				SameBoxFeatures(features, reverse), "reversed pairs also preserve an incident A / reference B result");
		}

		a->m_Orientation = identity;
		b->m_Orientation = glm::angleAxis(glm::radians(20.0f), Vec3f(0, 0, 1));
		const Vec3f obliqueNormal(0.5f, std::sqrt(0.75f), 0.0f);
		BoxFaceFeature originalCandidate;
		Expect(box.GetContactFace(obliqueNormal, b->m_Position, b->m_Orientation, originalCandidate) &&
			originalCandidate.id == BoxFaceId::PositiveX, "oblique fixture starts with B's local X candidate");
		Expect(ExtractBoxContactFeatures(*a, *b, obliqueNormal, features) &&
			features.referenceBody == a->GetIdentity() && features.reference.id == BoxFaceId::NegativeY &&
			features.incident.id == BoxFaceId::PositiveY &&
			glm::dot(features.reference.normal, features.incident.normal) < -0.9f,
			"incident extraction uses the actual reference normal and changes B's face from X to Y");
		BoxContactFeatures reverseOblique;
		Expect(ExtractBoxContactFeatures(*b, *a, -obliqueNormal, reverseOblique) &&
			SameBoxFeatures(features, reverseOblique), "oblique incident reselection is invariant under pair reversal");

		const auto beforeRotation = features;
		const Quat commonRotation = glm::angleAxis(0.62f, glm::normalize(Vec3f(3, -2, 1)));
		const Mat3 rotation = glm::toMat3(commonRotation);
		const Vec3f translation(7, -4, 2);
		a->m_Orientation = commonRotation * a->m_Orientation;
		b->m_Orientation = commonRotation * b->m_Orientation;
		a->m_Position = rotation * a->m_Position + translation;
		b->m_Position = rotation * b->m_Position + translation;
		const Vec3f positionA = a->m_Position, positionB = b->m_Position;
		const Quat orientationA = a->m_Orientation, orientationB = b->m_Orientation;
		a->m_LinearVelocity = Vec3f(1, 2, 3);
		b->m_AngularVelocity = Vec3f(-1, 2, -3);
		const auto revision = box.GetRevision();
		Expect(ExtractBoxContactFeatures(*a, *b, rotation * obliqueNormal, features) &&
			features.referenceBody == beforeRotation.referenceBody && features.reference.id == beforeRotation.reference.id &&
			features.incident.id == beforeRotation.incident.id &&
			Near(features.reference.normal, rotation * beforeRotation.reference.normal),
			"common rigid transformation preserves reference/incident ownership and local features");
		bool transformedVertices = true;
		for (int i = 0; i < 4; ++i) {
			transformedVertices &= Near(features.reference.vertices[i], rotation * beforeRotation.reference.vertices[i] + translation) &&
				Near(features.incident.vertices[i], rotation * beforeRotation.incident.vertices[i] + translation);
		}
		Expect(transformedVertices, "reference and incident vertex ordering transforms covariantly");
		Expect(Near(a->m_Position, positionA, 0) && Near(b->m_Position, positionB, 0) &&
			a->m_Orientation == orientationA && b->m_Orientation == orientationB &&
			Near(a->m_LinearVelocity, Vec3f(1, 2, 3), 0) && Near(b->m_AngularVelocity, Vec3f(-1, 2, -3), 0) &&
			box.GetRevision() == revision, "feature queries preserve live pose, velocities and shape revision");
	}

	void TestBoxFeatureFailureSafety()
	{
		using namespace GEngine;
		ShapeBox box(UnitBoxPoints());
		const Quat identity(1, 0, 0, 0);
		BoxFaceFeature sentinel;
		Expect(box.GetContactFace(Vec3f(0, 0, -1), Vec3f(0), identity, sentinel), "failure tests begin with valid output");
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();
		for (const Vec3f& direction : { Vec3f(0), Vec3f(nan, 0, 0), Vec3f(1, inf, 0) }) {
			BoxFaceFeature output = sentinel;
			Expect(!box.GetContactFace(direction, Vec3f(0), identity, output) && SameBoxFace(output, sentinel),
				"zero or nonfinite directions fail without overwriting an existing face");
		}
		for (const Quat& orientation : { Quat(0, 0, 0, 0), Quat(2, 0, 0, 0),
			Quat(nan, 0, 0, 0), Quat(1, inf, 0, 0) }) {
			BoxFaceFeature output = sentinel;
			Expect(!box.GetContactFace(Vec3f(1, 0, 0), Vec3f(0), orientation, output) && SameBoxFace(output, sentinel),
				"nonunit and nonfinite orientations fail without writing output");
		}
		for (const Vec3f& position : { Vec3f(nan, 0, 0), Vec3f(0, inf, 0), Vec3f(1.0e30f) }) {
			BoxFaceFeature output = sentinel;
			Expect(!box.GetContactFace(Vec3f(1, 0, 0), position, identity, output) && SameBoxFace(output, sentinel),
				"nonfinite positions and world-coordinate face collapse fail transactionally");
		}
		const float largest = std::numeric_limits<float>::max();
		ShapeBox largeBox(BoxPoints(Vec3f(largest * 0.25f)));
		BoxFaceFeature largeFace;
		Expect(largeBox.GetContactFace(Vec3f(1, 0, 0), Vec3f(0), identity, largeFace) &&
			largeFace.id == BoxFaceId::PositiveX, "large finite face areas do not overflow extraction validation");
		BoxFaceFeature overflowOutput = sentinel;
		Expect(!largeBox.GetContactFace(Vec3f(1, 0, 0), Vec3f(largest), identity, overflowOutput) &&
			SameBoxFace(overflowOutput, sentinel), "overflowing world vertices fail without writing output");
		PhysicsWorld world;
		RigidBody3D* a = world.CreateRigidBody3D();
		RigidBody3D* b = world.CreateRigidBody3D();
		a->m_Shape = b->m_Shape = &box;
		BoxContactFeatures pair;
		Expect(ExtractBoxContactFeatures(*a, *b, Vec3f(0, 1, 0), pair), "pair failure tests begin with valid features");
		const auto pairSentinel = pair;
		RigidBody3D standalone;
		standalone.m_Shape = &box;
		Expect(!ExtractBoxContactFeatures(standalone, *b, Vec3f(0, 1, 0), pair) && SameBoxFeatures(pair, pairSentinel),
			"a body without stable world identity cannot enter reference selection");
		Expect(!ExtractBoxContactFeatures(*a, *a, Vec3f(0, 1, 0), pair) && SameBoxFeatures(pair, pairSentinel),
			"self pairs fail without changing output");
		for (const Vec3f& normal : { Vec3f(0), Vec3f(nan), Vec3f(inf) }) {
			Expect(!ExtractBoxContactFeatures(*a, *b, normal, pair) && SameBoxFeatures(pair, pairSentinel),
				"invalid pair contact normals fail transactionally");
		}
		b->m_Position = Vec3f(nan);
		Expect(!ExtractBoxContactFeatures(*a, *b, Vec3f(0, 1, 0), pair) && SameBoxFeatures(pair, pairSentinel),
			"failure on the second box does not expose a partially written pair");
		b->m_Position = Vec3f(0);
		b->m_Shape = nullptr;
		Expect(!ExtractBoxContactFeatures(*a, *b, Vec3f(0, 1, 0), pair) && SameBoxFeatures(pair, pairSentinel),
			"null shape fails safely");
		ShapeSphere sphere(1.0f);
		b->m_Shape = &sphere;
		Expect(!ExtractBoxContactFeatures(*a, *b, Vec3f(0, 1, 0), pair) && SameBoxFeatures(pair, pairSentinel),
			"non-box shape fails safely");
		sphere.SetShapeType(ShapeType::Box);
		Expect(!ExtractBoxContactFeatures(*a, *b, Vec3f(0, 1, 0), pair) && SameBoxFeatures(pair, pairSentinel),
			"a mislabeled non-box cannot trigger an unsafe downcast");
	}

	int RunBoxFeatureRegression()
	{
		TestBoxFaceGeometry();
		TestBoxFaceSelectionAndRebuild();
		TestBoxContactFeaturePairs();
		TestBoxFeatureFailureSafety();
		std::cout << "Box contact features: " << testCount - failureCount << '/' << testCount << " checks passed\n";
		return failureCount == 0 ? 0 : 1;
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

	void TestBoxFaceContactGeometry()
	{
		using namespace GEngine;
		// Independently construct the corners of an axis-aligned rectangle intersection,
		// then apply a common rigid transform. Include off-origin geometry and all six faces.
		for (float scale : { 0.001f, 1.0f, 1000.0f }) {
			for (bool rotated : { false, true }) {
				for (int axis = 0; axis < 3; ++axis) {
					for (float sign : { -1.0f, 1.0f }) {
						const Quat rotation = rotated ? glm::angleAxis(0.63f, glm::normalize(Vec3f(1, 2, 3))) : Quat(1, 0, 0, 0);
						const Vec3f modelOffset = scale * Vec3f(0.3f, -0.2f, 0.4f);
						auto corners = BoxPoints(Vec3f(scale));
						for (Vec3f& corner : corners) corner += modelOffset;
						ShapeBox box(corners);
						PhysicsWorld world(Vec3f(0));
						auto* a = world.CreateRigidBody3D();
						auto* b = world.CreateRigidBody3D();
						const int u = (axis + 1) % 3, v = (axis + 2) % 3;
						Vec3f outward(0), shift(0);
						outward[axis] = sign;
						shift[axis] = sign * 1.9f;
						shift[u] = 0.75f;
						shift[v] = -0.5f;
						const Vec3f center = scale * Vec3f(3, -4, 2);
						const Vec3f normal = rotation * outward;
						ConfigureBoxBody(*a, box, center - rotation * modelOffset, rotation);
						ConfigureBoxBody(*b, box, a->m_Position + rotation * (scale * shift), rotation);
						const contact_t seed = MakeContact(*a, *b, center + scale * normal,
							center + rotation * (scale * shift) - scale * normal, -normal);
						std::array<contact_t, 4> contacts{}, reversed{}, repeated{};
						const int count = BuildBoxFaceContacts(seed, contacts);
						Expect(count == 4, "partial face overlap clips to four rectangle corners at every axis, sign and scale");
						Expect(BuildBoxFaceContacts(ReversedContact(seed), reversed) == count &&
							BuildBoxFaceContacts(seed, repeated) == count, "reversal and repeated clipping preserve contact count");
						const float tolerance = scale * 2.0e-5f;
						for (int i = 0; i < count; ++i) {
							const contact_t& contact = contacts[i];
							Expect(Finite(contact) && contact.m_BodyA == a && contact.m_BodyB == b &&
								contact.timeOfImpact == 0 && Near(contact.normal, -normal),
								"clipped contacts preserve finite state, body ownership and B-to-A unit normal");
							Expect(Near(contact.ptOnB_WorldSpace, contact.ptOnA_WorldSpace - 0.1f * scale * normal, tolerance) &&
								Near(contact.separationDistance, -0.1f * scale, tolerance) &&
								Near(contact.separationDistance, glm::dot(contact.ptOnA_WorldSpace - contact.ptOnB_WorldSpace, contact.normal), tolerance),
								"paired surface anchors have zero tangential offset and signed geometric penetration");
							Expect(Near(a->BodySpaceToWorldSpace(contact.ptOnA_LocalSpace), contact.ptOnA_WorldSpace, tolerance) &&
								Near(b->BodySpaceToWorldSpace(contact.ptOnB_LocalSpace), contact.ptOnB_WorldSpace, tolerance),
								"off-origin box anchors round-trip through COM-relative body space");
							Expect(Near(reversed[i].ptOnB_WorldSpace, contact.ptOnA_WorldSpace, 0) &&
								Near(reversed[i].ptOnA_WorldSpace, contact.ptOnB_WorldSpace, 0) &&
								Near(reversed[i].normal, -contact.normal, 0) &&
								Near(reversed[i].separationDistance, contact.separationDistance, 0) &&
								Near(repeated[i].ptOnA_WorldSpace, contact.ptOnA_WorldSpace, 0),
								"pair reversal and repeated queries preserve ordered physical contact geometry exactly");
						}
						for (float cu : { -0.25f, 1.0f }) {
							for (float cv : { -1.0f, 0.5f }) {
								Vec3f local(0);
								local[axis] = sign;
								local[u] = cu;
								local[v] = cv;
								const Vec3f expected = center + rotation * (scale * local);
								int matches = 0;
								for (int i = 0; i < count; ++i) matches += Near(contacts[i].ptOnA_WorldSpace, expected, tolerance);
								Expect(matches == 1, "each independently computed rectangle corner occurs exactly once");
							}
						}
					}
				}
			}
		}
	}

	void TestBoxFaceClippingBoundaries()
	{
		using namespace GEngine;
		ShapeBox box(UnitBoxPoints());
		PhysicsWorld world(Vec3f(0));
		auto* a = world.CreateRigidBody3D();
		auto* b = world.CreateRigidBody3D();
		ConfigureBoxBody(*a, box, Vec3f(0), Quat(1, 0, 0, 0));
		ConfigureBoxBody(*b, box, Vec3f(0, 1.9f, 0), glm::angleAxis(glm::quarter_pi<float>(), Vec3f(0, 1, 0)));
		auto seed = [&]() { return MakeContact(*a, *b, Vec3f(0, 1, 0), Vec3f(0, 0.9f, 0), Vec3f(0, -1, 0)); };
		std::array<contact_t, 4> contacts{};
		int count = BuildBoxFaceContacts(seed(), contacts);
		Expect(count == 4, "eight-vertex square/diamond intersection reduces to four contacts");
		const float k = std::sqrt(2.0f) - 1.0f;
		float twiceArea = 0;
		for (int i = 0; i < count; ++i) {
			const Vec3f p = contacts[i].ptOnA_WorldSpace, q = contacts[(i + 1) % count].ptOnA_WorldSpace;
			twiceArea += p.x * q.z - p.z * q.x;
			Expect(Near(p.y, 1) && ((Near(std::abs(p.x), 1) && Near(std::abs(p.z), k)) ||
				(Near(std::abs(p.x), k) && Near(std::abs(p.z), 1))),
				"reduced octagon contacts belong to the analytic square/diamond boundary");
		}
		Expect(Near(std::abs(twiceArea) * 0.5f, 2.0f * (1.0f + k * k)),
			"four-point reduction retains the maximum analytic inscribed quadrilateral area");

		// Tilt the diamond while keeping its entire clipped octagon below the plane.
		// The incident plane is y = centerY - sec(tilt) + tan(tilt) * x;
		// x = -1 is its deepest clipped boundary, independently of vertex ordering.
		const float tilt = 0.1f;
		b->m_Orientation = glm::angleAxis(tilt, Vec3f(0, 0, 1)) * b->m_Orientation;
		b->m_Position.y = 1.7f;
		count = BuildBoxFaceContacts(seed(), contacts);
		float deepestSeparation = 0;
		for (int i = 0; i < count; ++i) deepestSeparation = std::min(deepestSeparation, contacts[i].separationDistance);
		Expect(count == 4 && Near(deepestSeparation, 1.7f - 1.0f / std::cos(tilt) - std::tan(tilt) - 1.0f),
			"octagon reduction retains an analytically deepest penetrating point of a tilted face");
		std::array<contact_t, 4> reversedReduction{};
		Expect(BuildBoxFaceContacts(ReversedContact(seed()), reversedReduction) == count,
			"reversing a reduced tilted polygon preserves its contact count");
		for (int i = 0; i < count; ++i)
			Expect(Near(contacts[i].ptOnA_WorldSpace, reversedReduction[i].ptOnB_WorldSpace, 0) &&
				Near(contacts[i].separationDistance, reversedReduction[i].separationDistance, 0),
				"reduced tilted contact order and depths are exactly invariant to pair reversal");

		// Tilt about Z: the lower incident face crosses the reference plane.
		// Its penetrating half is a rectangle with two true corners and two plane intersections.
		const float angle = 0.2f;
		b->m_Orientation = glm::angleAxis(angle, Vec3f(0, 0, 1));
		b->m_Position = Vec3f(0, 1 + std::cos(angle), 0);
		ShapeBox wide(BoxPoints(Vec3f(3, 1, 3)));
		a->m_Shape = &wide;
		count = BuildBoxFaceContacts(seed(), contacts);
		Expect(count == 4, "tilted face clips at the reference depth plane to four contacts");
		int deepCount = 0, touchingCount = 0;
		for (int i = 0; i < count; ++i) {
			const auto& c = contacts[i];
			const Vec3f model = glm::inverse(b->m_Orientation) * (c.ptOnB_WorldSpace - b->m_Position);
			Expect(Near(c.ptOnA_WorldSpace.y, 1) && Near(model.y, -1) && Near(std::abs(model.z), 1) &&
				model.x >= -1.00001f && model.x <= 0.00001f && c.separationDistance <= 1.0e-5f,
				"tilted anchors lie on both actual surfaces and retain only the penetrating/touching face portion");
			deepCount += Near(c.separationDistance, -std::sin(angle));
			touchingCount += Near(c.separationDistance, 0);
		}
		Expect(deepCount == 2 && touchingCount == 2, "tilted clipping preserves two deepest corners and two zero-depth intersections");

		a->m_Shape = &box;
		b->m_Orientation = Quat(1, 0, 0, 0);
		b->m_Position = Vec3f(2, 1.9f, 0);
		count = BuildBoxFaceContacts(seed(), contacts);
		Expect(count == 2 && !Near(contacts[0].ptOnA_WorldSpace, contacts[1].ptOnA_WorldSpace),
			"line overlap retains two distinct surface contacts without duplicate endpoints");
		b->m_Position = Vec3f(0, 2 + 5.0e-6f, 0);
		count = BuildBoxFaceContacts(seed(), contacts);
		Expect(count == 4 && contacts[0].separationDistance > 0 && Near(contacts[0].separationDistance, 5.0e-6f, 1.0e-6f),
			"a gap accepted within the clipping tolerance retains its positive geometric separation");

		const auto sentinel = contacts;
		auto expectFallback = [&](const contact_t& input) {
			Expect(BuildBoxFaceContacts(input, contacts) == 0, "unsupported or invalid box face clipping requests seed fallback");
			bool unchanged = true;
			for (int i = 0; i < 4; ++i) unchanged = unchanged &&
				contacts[i].m_BodyA == sentinel[i].m_BodyA && contacts[i].m_BodyB == sentinel[i].m_BodyB &&
				Near(contacts[i].ptOnA_WorldSpace, sentinel[i].ptOnA_WorldSpace, 0) &&
				Near(contacts[i].ptOnB_WorldSpace, sentinel[i].ptOnB_WorldSpace, 0) &&
				Near(contacts[i].ptOnA_LocalSpace, sentinel[i].ptOnA_LocalSpace, 0) &&
				Near(contacts[i].ptOnB_LocalSpace, sentinel[i].ptOnB_LocalSpace, 0) &&
				Near(contacts[i].normal, sentinel[i].normal, 0) &&
				contacts[i].separationDistance == sentinel[i].separationDistance && contacts[i].timeOfImpact == sentinel[i].timeOfImpact;
			Expect(unchanged, "failed clipping leaves every output contact field unchanged");
		};
		for (const Vec3f position : { Vec3f(2, 1.9f, 2), Vec3f(3, 1.9f, 0), Vec3f(0, 2.01f, 0) }) {
			b->m_Position = position;
			expectFallback(seed());
		}
		b->m_Position = Vec3f(0, 1.9f, 0);
		contact_t invalid = seed();
		invalid.timeOfImpact = 0.1f;
		expectFallback(invalid);
		invalid = seed(); invalid.m_BodyA = nullptr;
		expectFallback(invalid);
		for (const Vec3f normal : { Vec3f(0), Vec3f(1, -1, 0), Vec3f(std::numeric_limits<float>::infinity()) }) {
			invalid = seed(); invalid.normal = normal;
			expectFallback(invalid);
		}
		invalid = seed(); invalid.ptOnA_LocalSpace.x = std::numeric_limits<float>::quiet_NaN();
		expectFallback(invalid);
		invalid = seed(); invalid.separationDistance = std::numeric_limits<float>::infinity();
		expectFallback(invalid);
		invalid = seed();
		b->m_Orientation = Quat(0, 0, 0, 0);
		expectFallback(invalid);
		b->m_Orientation = Quat(1, 0, 0, 0);
		ShapeSphere sphere(1);
		b->m_Shape = &sphere;
		expectFallback(invalid);
	}

	void TestBoxFaceManifoldWorld()
	{
		using namespace GEngine;
		for (bool reversed : { false, true }) {
			for (float angle : { 0.0f, glm::quarter_pi<float>() }) {
				for (float floorWidth : { 1.0f, 5.0f }) {
					ShapeBox box(UnitBoxPoints());
					ShapeBox floor(BoxPoints(Vec3f(floorWidth, 0.5f, floorWidth)));
					PhysicsSystem system;
					auto* world = new PhysicsWorld(Vec3f(0.0f));
					system.SetPhysicsWorld(world);
					RigidBody3D* first = world->CreateRigidBody3D();
					RigidBody3D* second = world->CreateRigidBody3D();
					RigidBody3D* floorBody = reversed ? second : first;
					RigidBody3D* boxBody = reversed ? first : second;
					ConfigureBoxBody(*floorBody, floor, Vec3f(0), Quat(1, 0, 0, 0));
					floorBody->SetBodyTypeAndInverseMass(BodyType::Static, 0.0f);
					ConfigureBoxBody(*boxBody, box, Vec3f(0, 1.49f, 0), glm::angleAxis(angle, Vec3f(0, 1, 0)));
					for (int step = 0; step < 10; ++step) {
						ResetPhysicsProfile();
						system.Update(Timestep(1.0f / 120.0f));
						Expect(GetManifolds(system).m_Manifolds.size() == 1 && GetManifolds(system).GetContactCount() == 4,
							"aligned/rotated box faces start and remain a four-point world manifold in either creation order");
						const auto expectedContactCount = IsPhysicsProfilingEnabled() ? 4u : 0u;
						Expect(GetPhysicsProfileSnapshot().generatedContactCount == expectedContactCount &&
							GetPhysicsProfileSnapshot().solverConstraintCount == expectedContactCount,
							"contact telemetry counts four contacts when profiling is enabled and remains zero otherwise");
					}
					Expect(Near(boxBody->m_Position, Vec3f(0, 1.49f, 0)) &&
						Near(boxBody->m_LinearVelocity, Vec3f(0)) && Near(boxBody->m_AngularVelocity, Vec3f(0)),
						"face generation preserves an unforced resting pose and motion within the approved position slop");
				}
			}
		}
	}

	void TestBoxFaceRestingStability()
	{
		using namespace GEngine;
		for (int iterations : { 1, 8 }) {
			for (bool reversed : { false, true }) {
				for (float angle : { 0.0f, glm::quarter_pi<float>() }) {
					ShapeBox box(UnitBoxPoints());
					ShapeBox floor(BoxPoints(Vec3f(5, 0.5f, 5)));
					PhysicsSystem system;
					system.SetSolverIterations(iterations);
					auto* world = new PhysicsWorld(Vec3f(0, -12, 0));
					system.SetPhysicsWorld(world);
					auto* first = world->CreateRigidBody3D();
					auto* second = world->CreateRigidBody3D();
					auto* support = reversed ? second : first;
					auto* body = reversed ? first : second;
					ConfigureBoxBody(*support, floor, Vec3f(0), Quat(1, 0, 0, 0));
					support->SetBodyTypeAndInverseMass(BodyType::Static, 0);
					ConfigureBoxBody(*body, box, Vec3f(0, 1.5f, 0), glm::angleAxis(angle, Vec3f(0, 1, 0)));
					support->m_Elasticity = body->m_Elasticity = 0;
					support->m_Friction = body->m_Friction = 0.5f;
					double peakEnergy = 18;
					float peakSpeed = 0, peakOmega = 0, peakDepth = 0, finalWindowSpeed = 0, finalWindowOmega = 0;
					bool finite = true, stableManifold = true;
					for (int step = 0; step < 1200; ++step) {
						system.Update(Timestep(1.0f / 120.0f));
						finite = finite && body->HasFiniteState();
						const float speed = glm::length(body->m_LinearVelocity), omega = glm::length(body->m_AngularVelocity);
						peakSpeed = std::max(peakSpeed, speed);
						peakOmega = std::max(peakOmega, omega);
						peakEnergy = std::max(peakEnergy, 12.0 * body->m_Position.y + 0.5 * speed * speed + omega * omega / 3.0);
						peakDepth = std::max(peakDepth, 0.5f - body->GetWorldBounds().mins.y);
						if (step >= 1080) {
							finalWindowSpeed = std::max(finalWindowSpeed, speed);
							finalWindowOmega = std::max(finalWindowOmega, omega);
							const int points = GetManifolds(system).GetContactCount();
							stableManifold = stableManifold && GetManifolds(system).m_Manifolds.size() == 1 && points >= 2 && points <= 4;
						}
					}
					Expect(finite && peakEnergy <= 18.0 * 1.001, "ten-second resting face stays finite without energy growth above 0.1 percent tolerance");
					Expect(peakDepth <= 0.0201f && body->m_Position.y >= 1.4799f,
						"resting box remains supported within the approved position slop plus numerical tolerance");
					Expect(stableManifold && finalWindowSpeed <= 0.05f && finalWindowOmega <= 0.05f,
						"aligned and rotated resting faces retain two to four contacts and stay below moving thresholds in the final second");
					std::cout << "BOX_FACE_REST iterations=" << iterations << " reversed=" << reversed << " angle=" << angle
						<< " peak_energy=" << peakEnergy << " peak_speed=" << peakSpeed << " peak_omega=" << peakOmega
						<< " peak_depth=" << peakDepth << " final_y=" << body->m_Position.y
						<< " final_window_speed=" << finalWindowSpeed << " final_window_omega=" << finalWindowOmega
						<< " final_contacts=" << GetManifolds(system).GetContactCount() << " finite=" << finite << '\n';
				}
			}
		}
	}

	int RunBoxManifoldRegression()
	{
		TestBoxFaceContactGeometry();
		TestBoxFaceClippingBoundaries();
		TestBoxFaceManifoldWorld();
		TestBoxFaceRestingStability();
		std::cout << "Box face manifolds: " << testCount - failureCount << '/' << testCount << " checks passed\n";
		return failureCount == 0 ? 0 : 1;
	}


	void TestManifoldPersistence()
	{
		using namespace GEngine;
		// Observe warm-start impulses without exposing the manifold's private solver state.
		for (bool reverse : { false, true }) {
			ShapeSphere sphere(1);
			RigidBody3D a, b;
			ConfigureSphereBody(a, sphere, Vec3f(0));
			ConfigureSphereBody(b, sphere, Vec3f(0, 2, 0));
			a.SetBodyTypeAndInverseMass(BodyType::Static, 0);
			a.m_Friction = b.m_Friction = 0;
			const auto seed = MakeContact(a, b, Vec3f(0, 1, 0), Vec3f(0, 1, 0), Vec3f(0, -1, 0));
			ManifoldCollector pair;
			pair.AddContact(seed);
			b.m_LinearVelocity = Vec3f(0, -1, 0);
			pair.PreSolve(1.0f / 120.0f); pair.Solve();
			b.m_LinearVelocity = b.m_AngularVelocity = Vec3f(0);
			auto refreshed = MakeContact(a, b, Vec3f(0.005f, 1, 0), Vec3f(0.005f, 1, 0), seed.normal);
			pair.AddContact(reverse ? ReversedContact(refreshed) : refreshed);
			Expect(pair.GetContactCount() == 1 && Near(pair.m_Manifolds[0].GetContact(0).ptOnA_LocalSpace,
				refreshed.ptOnA_LocalSpace, 0), "matching anchor pairs refresh stored geometry in either order");
			pair.PreSolve(1.0f / 120.0f);
			Expect(Near(b.m_LinearVelocity, Vec3f(0, 1, 0)) &&
				Near(b.m_AngularVelocity, Vec3f(0, 0, 0.0125f)),
				"coherent refresh retains support impulse and uses the refreshed lever arm");

			ManifoldCollector distinct;
			distinct.AddContact(seed);
			auto other = MakeContact(a, b, seed.ptOnA_WorldSpace, Vec3f(0.03f, 1, 0), seed.normal);
			distinct.AddContact(other);
			Expect(distinct.GetContactCount() == 2, "sharing only A's anchor does not merge a distinct anchor pair");
			distinct.Clear(); distinct.AddContact(seed);
			other = MakeContact(a, b, Vec3f(0.03f, 1, 0), seed.ptOnB_WorldSpace, seed.normal);
			distinct.AddContact(other);
			Expect(distinct.GetContactCount() == 2, "sharing only B's anchor does not merge a distinct anchor pair");

			b.m_LinearVelocity = b.m_AngularVelocity = Vec3f(0);
			refreshed.normal = Vec3f(-1, 0, 0);
			pair.AddContact(reverse ? ReversedContact(refreshed) : refreshed);
			pair.PreSolve(1.0f / 120.0f);
			Expect(pair.GetContactCount() == 1 && Near(b.m_LinearVelocity, Vec3f(0), 0) &&
				Near(b.m_AngularVelocity, Vec3f(0), 0), "incoherent new normal discards old constraints and warm-start impulse");
		}

		for (int mutation = 0; mutation < 4; ++mutation) {
			ShapeSphere sphere(1), replacement(1);
			PhysicsWorld world(Vec3f(0));
			auto* a = world.CreateRigidBody3D();
			auto* b = world.CreateRigidBody3D();
			ConfigureSphereBody(*a, sphere, Vec3f(0));
			ConfigureSphereBody(*b, sphere, Vec3f(0, 2, 0));
			a->SetBodyTypeAndInverseMass(BodyType::Static, 0);
			ManifoldCollector pair;
			pair.AddContact(MakeContact(*a, *b, Vec3f(0, 1, 0), Vec3f(0, 1, 0), Vec3f(0, -1, 0)));
			b->m_LinearVelocity = Vec3f(0, -1, 0);
			pair.PreSolve(1.0f / 120.0f); pair.Solve();
			b->m_LinearVelocity = b->m_AngularVelocity = Vec3f(0);
			if (mutation == 0) sphere.SetRadius(1.1f);
			if (mutation == 1) b->m_Shape = &replacement;
			if (mutation == 2) {
				*b = RigidBody3D(*a); // Same address, different identity; no freed pointer.
				ConfigureSphereBody(*b, sphere, Vec3f(0, 2, 0));
			}
			if (mutation == 3) b->m_Orientation = glm::angleAxis(glm::radians(10.0f), Vec3f(0, 0, 1));
			pair.RemoveExpired();
			Expect(pair.GetContactCount() == 0, "shape revision/replacement or body identity expires cached contacts");
			pair.PreSolve(1.0f / 120.0f);
			Expect(Near(b->m_LinearVelocity, Vec3f(0), 0) && Near(b->m_AngularVelocity, Vec3f(0), 0),
				"expired contact cannot apply a stale warm-start impulse");
		}
	}


	void TestPersistenceBasisAndGuards()
	{
		using namespace GEngine;
		ShapeSphere sphere(1);
		RigidBody3D a, b;
		ConfigureSphereBody(a, sphere, Vec3f(0));
		ConfigureSphereBody(b, sphere, Vec3f(0));
		a.SetBodyTypeAndInverseMass(BodyType::Static, 0);
		a.m_Friction = b.m_Friction = 1;
		const Vec3f axis(std::sqrt(1 - 0.899f * 0.899f), 0, 0.899f);
		const Vec3f newAxis(std::sqrt(1 - 0.901f * 0.901f), 0, 0.901f);
		ManifoldCollector pair;
		auto contact = MakeContact(a, b, Vec3f(0), Vec3f(0), -axis);
		pair.AddContact(contact);
		auto& constraint = CachedConstraint(pair.m_Manifolds[0], 0);
		constraint.m_CachedLambda[0] = 2;
		constraint.m_CachedLambda[1] = 0.3f;
		constraint.m_CachedLambda[2] = -0.4f;
		Vec3f u, v;
		Math::GetOrtho(axis, u, v);
		const Vec3f expectedImpulse = axis * 2.0f + u * 0.3f - v * 0.4f;
		contact.normal = -newAxis;
		pair.AddContact(contact);
		pair.PreSolve(1.0f / 120.0f);
		Expect(Near(b.m_LinearVelocity, expectedImpulse, 2.0e-6f) && Near(b.m_AngularVelocity, Vec3f(0), 0),
			"small normal refresh preserves the world impulse across GetOrtho's tangent-basis branch");
		Expect(Near(pair.m_Manifolds[0].GetContact(0).normal, -newAxis, 1.0e-6f),
			"normal refresh stores the new contact direction");

		for (float degrees : { 4.0f, 6.0f }) {
			b.m_Orientation = Quat(1, 0, 0, 0);
			pair.Clear();
			contact.normal = Vec3f(0, -1, 0);
			pair.AddContact(contact);
			CachedConstraint(pair.m_Manifolds[0], 0).m_CachedLambda[0] = 1;
			contact.normal = glm::angleAxis(glm::radians(degrees), Vec3f(0, 0, 1)) * contact.normal;
			pair.AddContact(contact);
			const float lambda = CachedConstraint(pair.m_Manifolds[0], 0).m_CachedLambda[0];
			Expect(degrees < 5 ? Near(lambda, std::cos(glm::radians(degrees)), 1.0e-6f) : lambda == 0,
				"normal reuse is retained at four degrees and cold at six degrees");
		}

		for (bool commonRotation : { false, true }) {
			a.m_Orientation = b.m_Orientation = Quat(1, 0, 0, 0);
			pair.Clear(); contact.normal = Vec3f(0, -1, 0); pair.AddContact(contact);
			CachedConstraint(pair.m_Manifolds[0], 0).m_CachedLambda[0] = 1;
			b.m_Orientation = glm::angleAxis(glm::radians(10.0f), Vec3f(0, 0, 1));
			if (commonRotation) a.m_Orientation = b.m_Orientation;
			pair.RemoveExpired(); // COM anchors cannot hide this normal-only check with tangential drift.
			Expect(pair.GetContactCount() == (commonRotation ? 1 : 0),
				"common rigid rotation preserves contact while relative normal rotation expires it");
		}

		a.m_Orientation = b.m_Orientation = Quat(1, 0, 0, 0);
		for (int boundary = 0; boundary < 3; ++boundary) {
			b.m_Shape = &sphere; pair.Clear();
			contact = MakeContact(a, b, Vec3f(0), Vec3f(0), Vec3f(0, -1, 0));
			pair.AddContact(contact);
			CachedConstraint(pair.m_Manifolds[0], 0).m_CachedLambda[0] = 1;
			b.m_LinearVelocity = b.m_AngularVelocity = Vec3f(0);
			sphere.SetRadius(sphere.GetRadius() + 0.1f);
			if (boundary == 0) pair.RemoveExpired();
			if (boundary == 1) pair.PreSolve(1.0f / 120.0f);
			if (boundary == 2) { pair.AddContact(contact); pair.PreSolve(1.0f / 120.0f); }
			Expect(Near(b.m_LinearVelocity, Vec3f(0), 0),
				"geometry revision invalidates before expiry, direct PreSolve, and new-contact refresh");
		}
		pair.Clear(); pair.AddContact(contact);
		b.m_Shape = nullptr;
		pair.RemoveExpired();
		Expect(pair.GetContactCount() == 0, "null replacement shape expires without a shape dereference");
		b.m_Shape = &sphere;
		pair.AddContact(contact);
		auto invalid = contact;
		invalid.normal.x = std::numeric_limits<float>::quiet_NaN();
		pair.AddContact(invalid);
		Expect(pair.GetContactCount() == 1 && Finite(pair.m_Manifolds[0].GetContact(0)),
			"nonfinite incoming contact is rejected without damaging valid cached geometry");
	}


	void TestPersistenceLifetimeAndQueryMetadata()
	{
		using namespace GEngine;
		ShapeSphere sphere(1);
		PhysicsSystem system;
		auto* world = new PhysicsWorld(Vec3f(0));
		system.SetPhysicsWorld(world);
		auto* a = world->CreateRigidBody3D();
		auto* b = world->CreateRigidBody3D();
		ConfigureSphereBody(*a, sphere, Vec3f(0));
		ConfigureSphereBody(*b, sphere, Vec3f(0, 1.99f, 0));
		auto seed = MakeContact(*a, *b, Vec3f(0, 1, 0), Vec3f(0, 0.99f, 0), Vec3f(0, -1, 0));
		for (int query = 0; query < 4; ++query) {
			auto result = seed;
			result.featureA = result.featureB = 123;
			bool hit = false;
			if (query == 0) hit = Collision::Intersect(a, b, result);
			if (query == 1) hit = Collision::Intersect(a, b, 1.0f / 120.0f, result);
			if (query == 2) hit = Collision::SphereSphereIntersect(a, b, 1.0f / 120.0f, result);
			if (query == 3) hit = Collision::ConservativeAdvance(a, b, 1.0f / 120.0f, result);
			Expect(hit && result.featureA == 0 && result.featureB == 0,
				"reused single-witness output clears previously stored box feature metadata");
		}
		auto& pair = GetManifolds(system);
		pair.AddContact(seed);
		sphere.SetRadius(1.1f);
		pair.PreSolve(1.0f / 120.0f);
		Expect(pair.m_Manifolds.empty(), "PreSolve invalidation erases the empty pair before a body can be deleted");
		world->RemoveRigidBody3D(b);
		system.Update(Timestep(1.0f / 120.0f));
		Expect(pair.m_Manifolds.empty() && world->GetPhysicsBodies().size() == 1,
			"body removal after cache invalidation leaves no stale pair on the next world step");
	}

	void TestFeaturePatchPersistence()
	{
		using namespace GEngine;
		for (float scale : { 0.001f, 1.0f, 1000.0f }) {
			for (float yaw : { 0.0f, glm::quarter_pi<float>() }) {
				ShapeBox box(BoxPoints(Vec3f(scale)));
				PhysicsWorld world(Vec3f(0));
				auto* a = world.CreateRigidBody3D();
				auto* b = world.CreateRigidBody3D();
				ConfigureBoxBody(*a, box, Vec3f(0), Quat(1, 0, 0, 0));
				ConfigureBoxBody(*b, box, Vec3f(0, 1.99f * scale, 0), glm::angleAxis(yaw, Vec3f(0, 1, 0)));
				const auto seed = MakeContact(*a, *b, Vec3f(0, scale, 0), Vec3f(0, 0.99f * scale, 0), Vec3f(0, -1, 0));
				std::array<contact_t, 4> patch{}, reversed{};
				const int count = BuildBoxFaceContacts(seed, patch);
				Expect(count == 4 && BuildBoxFaceContacts(ReversedContact(seed), reversed) == 4,
					"feature persistence fixture emits four contacts at small, unit, and large scales");
				if (count != 4) continue;
				for (int i = 0; i < 4; ++i) {
					Expect(patch[i].featureA != 0 && patch[i].featureB != 0 &&
						patch[i].featureA == reversed[i].featureB && patch[i].featureB == reversed[i].featureA,
						"box feature keys are nonzero and retain physical ownership on A/B reversal");
					for (int j = 0; j < i; ++j)
						Expect(patch[i].featureA != patch[j].featureA || patch[i].featureB != patch[j].featureB,
							"clipped face contacts have distinct feature pairs including reduced octagons");
				}

				const Quat common = glm::angleAxis(0.6f, glm::normalize(Vec3f(1, 2, 3)));
				const Vec3f translation = Vec3f(3, -2, 5) * scale;
				a->m_Position = translation;
				b->m_Position = common * Vec3f(0, 1.99f * scale, 0) + translation;
				a->m_Orientation = common;
				b->m_Orientation = common * glm::angleAxis(yaw, Vec3f(0, 1, 0));
				auto movedSeed = MakeContact(*a, *b, common * seed.ptOnA_WorldSpace + translation,
					common * seed.ptOnB_WorldSpace + translation, common * seed.normal);
				std::array<contact_t, 4> moved{};
				const int movedCount = BuildBoxFaceContacts(movedSeed, moved);
				bool stableKeys = movedCount == 4;
				for (int i = 0; i < movedCount; ++i)
					stableKeys = stableKeys && moved[i].featureA == patch[i].featureA && moved[i].featureB == patch[i].featureB;
				Expect(stableKeys, "box feature keys survive a common rigid transform at each supported test scale");
				a->m_Position = Vec3f(0);
				b->m_Position = Vec3f(0, 1.99f * scale, 0);
				a->m_Orientation = Quat(1, 0, 0, 0);
				b->m_Orientation = glm::angleAxis(yaw, Vec3f(0, 1, 0));
				ManifoldCollector pair;
				pair.AddContacts(patch.data(), 4);
				Expect(pair.GetContactCount() == 4, "distinct small-box features survive the absolute anchor-match tolerance");
				for (int i = 0; i < 4; ++i)
					CachedConstraint(pair.m_Manifolds[0], i).m_CachedLambda[0] = float(i + 1);
				for (int i = 0; i < 4; ++i) reversed[i] = ReversedContact(patch[3 - i]);
				pair.AddContacts(reversed.data(), 4);
				for (int i = 0; i < 4; ++i)
					Expect(CachedConstraint(pair.m_Manifolds[0], i).m_CachedLambda[0] == float(i + 1) &&
						pair.m_Manifolds[0].GetContact(i).featureA == patch[i].featureA &&
						pair.m_Manifolds[0].GetContact(i).featureB == patch[i].featureB,
						"reordered patch retains each labelled feature impulse in its previous solve slot");

				// Complete refresh retires missing points and starts changed faces/anchors cold.
				auto changed = patch;
				changed[0].featureA += 16u;
				changed[1].ptOnA_LocalSpace.x += 0.03f;
				changed[1].ptOnA_WorldSpace = a->BodySpaceToWorldSpace(changed[1].ptOnA_LocalSpace);
				pair.AddContacts(changed.data(), 3);
				Expect(pair.GetContactCount() == 3 && CachedConstraint(pair.m_Manifolds[0], 0).m_CachedLambda[0] == 3 &&
					CachedConstraint(pair.m_Manifolds[0], 1).m_CachedLambda[0] == 0 &&
					CachedConstraint(pair.m_Manifolds[0], 2).m_CachedLambda[0] == 0,
					"changed face or excessive anchor drift resets only that impulse, and absent points retire");

				changed[1].normal.x = std::numeric_limits<float>::quiet_NaN();
				pair.AddContacts(changed.data(), 3);
				Expect(pair.GetContactCount() == 3 && CachedConstraint(pair.m_Manifolds[0], 0).m_CachedLambda[0] == 3,
					"invalid batch is rejected transactionally");
				pair.AddContacts(patch.data(), 4);
				for (int i = 0; i < 4; ++i) CachedConstraint(pair.m_Manifolds[0], i).m_CachedLambda[0] = 1;
				box.Build(BoxPoints(Vec3f(scale * 1.01f)));
				pair.AddContacts(patch.data(), 4);
				bool cold = true;
				for (int i = 0; i < 4; ++i) cold = cold && CachedConstraint(pair.m_Manifolds[0], i).m_CachedLambda[0] == 0;
				Expect(cold, "shape rebuild resets every patch impulse even when feature IDs and input anchors repeat");
			}
		}
	}


	void TestBoxPatchContinuity()
	{
		using namespace GEngine;
		for (bool reversed : { false, true }) {
			ShapeBox box(UnitBoxPoints());
			PhysicsWorld world(Vec3f(0));
			auto* a = world.CreateRigidBody3D();
			auto* b = world.CreateRigidBody3D();
			ConfigureBoxBody(*a, box, Vec3f(0), Quat(1, 0, 0, 0));
			ConfigureBoxBody(*b, box, Vec3f(0, 1.99f, 0), Quat(1, 0, 0, 0));
			const auto seed = MakeContact(*a, *b, Vec3f(0, 1, 0), Vec3f(0, 0.99f, 0), Vec3f(0, -1, 0));
			std::array<contact_t, 4> patch{};
			Expect(BuildBoxFaceContacts(seed, patch) == 4, "continuity fixture starts with a complete aligned face");
			ManifoldCollector pair;
			pair.AddContacts(patch.data(), 4);
			for (int i = 0; i < 4; ++i) CachedConstraint(pair.m_Manifolds[0], i).m_CachedLambda[0] = float(i+1);
			bool changedBoundary = false;
			for (float offset : { 0.0001f, 0.001f, 0.005f }) {
				std::array<contact_t, 4> old{};
				for (int i = 0; i < 4; ++i) old[i] = pair.m_Manifolds[0].GetContact(i);
				b->m_Position = Vec3f(offset, 1.99f, offset * 0.5f);
				b->m_Orientation = glm::angleAxis(offset * 0.1f, Vec3f(0, 1, 0));
				std::array<contact_t, 4> next{};
				const int count = BuildBoxFaceContacts(seed, next);
				Expect(count == 4, "tiny sliding/rotation retains a four-point face patch");
				if (count != 4) continue;
				if (reversed) {
					std::reverse(next.begin(), next.end());
					for (auto& c : next) c = ReversedContact(c);
				}
				pair.AddContacts(next.data(), count);
				Expect(pair.GetContactCount() == 4, "boundary transitions do not duplicate or discard patch points");
				for (int i = 0; i < 4; ++i) {
					const auto current = pair.m_Manifolds[0].GetContact(i);
					changedBoundary = changedBoundary || current.featureA != old[i].featureA || current.featureB != old[i].featureB;
					Expect(CachedConstraint(pair.m_Manifolds[0], i).m_CachedLambda[0] == float(i+1) &&
						glm::length(current.ptOnA_LocalSpace-old[i].ptOnA_LocalSpace) < 0.02f &&
						glm::length(current.ptOnB_LocalSpace-old[i].ptOnB_LocalSpace) < 0.02f,
						"nearby same-face boundary changes retain each support impulse in its previous solve slot");
					bool refreshed = false;
					for (auto c : next) {
						if (reversed) c = ReversedContact(c);
						refreshed = refreshed || (Near(current.ptOnA_LocalSpace,c.ptOnA_LocalSpace,0) &&
							Near(current.ptOnB_LocalSpace,c.ptOnB_LocalSpace,0) &&
							current.featureA == c.featureA && current.featureB == c.featureB);
					}
					Expect(refreshed, "continuity matching refreshes actual generated anchors and feature metadata");
				}
			}
			Expect(changedBoundary, "continuity regression crosses actual clipping boundary-key classifications");
		}
	}

	void TestPatchOrderSolverEquivalence()
	{
		using namespace GEngine;
		for (int passes : { 1, 8 }) {
			std::vector<Vec3f> reference;
			for (bool permuted : { false, true }) {
				ShapeBox box(UnitBoxPoints());
				PhysicsWorld world(Vec3f(0));
				auto* a = world.CreateRigidBody3D();
				auto* b = world.CreateRigidBody3D();
				ConfigureBoxBody(*a, box, Vec3f(0), Quat(1,0,0,0));
				ConfigureBoxBody(*b, box, Vec3f(0,1.99f,0), Quat(1,0,0,0));
				a->SetBodyTypeAndInverseMass(BodyType::Static,0);
				a->m_Friction = b->m_Friction = 0.5f;
				std::array<contact_t,4> patch{};
				BuildBoxFaceContacts(MakeContact(*a,*b,Vec3f(0,1,0),Vec3f(0,0.99f,0),Vec3f(0,-1,0)),patch);
				ManifoldCollector pair;
				pair.AddContacts(patch.data(),4);
				bool equivalent = true;
				for (int step = 0; step < 32; ++step) {
					auto input = patch;
					if (permuted) std::rotate(input.begin(),input.begin()+(step%3+1),input.end());
					pair.AddContacts(input.data(),4);
					b->m_LinearVelocity = Vec3f(0,-0.1f,0);
					b->m_AngularVelocity = Vec3f(0);
					pair.PreSolve(1.0f/120.0f);
					for (int i = 0; i < passes; ++i) pair.Solve();
					if (!permuted) {
						reference.push_back(b->m_LinearVelocity);
						reference.push_back(b->m_AngularVelocity);
					}
					else equivalent = equivalent && Near(b->m_LinearVelocity,reference[2*step],0) &&
						Near(b->m_AngularVelocity,reference[2*step+1],0);
					equivalent = equivalent && b->HasFiniteState();
				}
				Expect(equivalent, "cyclic input order preserves exact warm/cold sequential-solver response at one/eight passes");
			}
		}
	}

	int RunPersistenceContinuityRegression()
	{
		TestBoxPatchContinuity();
		TestPatchOrderSolverEquivalence();
		std::cout << "Persistence continuity: " << testCount-failureCount << '/' << testCount << " checks passed\n";
		return failureCount == 0 ? 0 : 1;
	}

	int RunManifoldPersistenceRegression()
	{
		TestManifoldPersistence();
		TestBoxPatchContinuity();
		TestPatchOrderSolverEquivalence();
		TestPersistenceBasisAndGuards();
		TestFeaturePatchPersistence();
		TestPersistenceLifetimeAndQueryMetadata();
		std::cout << "Manifold persistence: " << testCount - failureCount << '/' << testCount << " checks passed\n";
		return failureCount == 0 ? 0 : 1;
	}

	struct SolverChainFixture
	{
		GEngine::ShapeSphere sphere{ 1.0f };
		GEngine::PhysicsSystem system;
		std::array<GEngine::RigidBody3D*, 3> bodies{};

		SolverChainFixture()
		{
			auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f));
			system.SetPhysicsWorld(world);
			for (int i = 0; i < 3; ++i)
			{
				bodies[i] = world->CreateRigidBody3D();
				bodies[i]->m_Shape = &sphere;
				bodies[i]->m_Position = GEngine::Vec3f(0.0f, 2.0f * i, 0.0f);
				bodies[i]->m_Orientation = GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f);
				bodies[i]->SetBodyTypeAndInverseMass(i == 0
					? GEngine::Component::BodyType::Static : GEngine::Component::BodyType::Dynamic,
					i == 0 ? 0.0f : 1.0f);
				bodies[i]->m_Friction = 0.5f;
				// Isolate traversal of two exact, persisted witnesses from narrow-phase/TOI variation.
				bodies[i]->m_CollisionMask = 0;
			}
			for (int i = 0; i < 2; ++i)
			{
				const GEngine::Vec3f point(0.0f, 1.0f + 2.0f * i, 0.0f);
				GetManifolds(system).AddContact(MakeContact(*bodies[i], *bodies[i + 1],
					point, point, GEngine::Vec3f(0.0f, -1.0f, 0.0f)));
			}
		}

		void ResetMotion()
		{
			// Keep cached impulses but restore exact contact geometry and the same incoming velocity.
			for (int i = 1; i < 3; ++i)
			{
				bodies[i]->m_Position = GEngine::Vec3f(0.0f, 2.0f * i, 0.0f);
				bodies[i]->m_LinearVelocity = GEngine::Vec3f(0.0f, -1.0f, 0.0f);
				bodies[i]->m_AngularVelocity = GEngine::Vec3f(0.0f);
			}
		}

		void CheckStep(float expectedSpeed, int iterations)
		{
			constexpr float dt = 1.0f / 120.0f;
			GEngine::ResetPhysicsProfile();
			system.Update(GEngine::Timestep(dt));
			for (int i = 1; i < 3; ++i)
			{
				Expect(bodies[i]->HasFiniteState() &&
					Near(bodies[i]->m_LinearVelocity, GEngine::Vec3f(0.0f, expectedSpeed, 0.0f), 1.0e-6f),
					"configured passes propagate support with the analytical chain residual");
				Expect(Near(bodies[i]->m_Position.y, 2.0f * i + expectedSpeed * dt, 1.0e-6f) &&
					Near(bodies[i]->m_AngularVelocity, GEngine::Vec3f(0.0f), 0.0f),
					"solver passes integrate once without creating angular motion");
			}
			Expect(Near(bodies[0]->m_Position, GEngine::Vec3f(0.0f), 0.0f) &&
				Near(bodies[0]->m_LinearVelocity, GEngine::Vec3f(0.0f), 0.0f) &&
				GetManifolds(system).GetContactCount() == 2 && GetTransientContacts(system).empty(),
				"repeated traversal preserves the static support and both persistent contacts");
			if (GEngine::IsPhysicsProfilingEnabled())
			{
				const auto profile = GEngine::GetPhysicsProfileSnapshot();
				Expect(profile.stepCount == 1 && profile.solverIterationCount == iterations &&
					profile.solverConstraintCount == 2 && profile.integratedBodyCount == 3,
					"profiling reports configured passes and one integration per update");
			}
		}
	};

	void TestSolverIterationConfiguration()
	{
		GEngine::PhysicsSystem system, independent;
		Expect(system.GetSolverIterations() == 1, "default solver policy preserves one-pass behavior");
		Expect(system.SetSolverIterations(8), "valid solver count is accepted");
		for (const int invalid : { std::numeric_limits<int>::min(), -1, 0, 33, std::numeric_limits<int>::max() })
		{
			Expect(!system.SetSolverIterations(invalid) && system.GetSolverIterations() == 8,
				"invalid iteration requests are rejected without changing the prior setting");
		}
		Expect(independent.GetSolverIterations() == 1, "solver settings are independent per system");
		Expect(system.SetSolverIterations(32) && system.GetSolverIterations() == 32,
			"upper solver bound is accepted");
		Expect(system.SetSolverIterations(1) && system.GetSolverIterations() == 1,
			"lower solver bound is accepted");
		system.SetSolverIterations(4);
		system.SetPhysicsWorld(new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f)));
		system.SetPhysicsWorld(new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f)));
		GEngine::ResetPhysicsProfile();
		system.Update(GEngine::Timestep(1.0f / 120.0f));
		Expect(system.GetSolverIterations() == 4 &&
			GEngine::GetPhysicsProfileSnapshot().solverIterationCount == 0,
			"world replacement retains policy and an empty world reports no contact solve passes");
		system.OnExit();
		Expect(system.GetSolverIterations() == 4, "world teardown retains the system's solver policy");
	}

	void TestSolverIterationTraversal()
	{
		for (const int iterations : { 1, 2, 4, 8, 16, 32 })
		{
			SolverChainFixture fixture, repeat;
			fixture.system.SetSolverIterations(iterations);
			repeat.system.SetSolverIterations(iterations);
			// Equal masses start at -1. The floor removes the lower body's velocity; the upper
			// contact shares the remaining momentum, halving both velocities on every pass.
			const float residual = -std::ldexp(1.0f, -iterations);
			fixture.ResetMotion();
			fixture.CheckStep(residual, iterations);
			repeat.ResetMotion();
			repeat.CheckStep(residual, iterations);
			Expect(Near(fixture.bodies[1]->m_Position, repeat.bodies[1]->m_Position, 0.0f) &&
				Near(fixture.bodies[2]->m_LinearVelocity, repeat.bodies[2]->m_LinearVelocity, 0.0f),
				"repeated identical solver workloads produce identical state");
			fixture.ResetMotion();
			// Warm starting supplies the previously accumulated support, so N more passes
			// reduce the residual by another factor of 2^N. Reapplying it per pass is incorrect.
			fixture.CheckStep(-std::ldexp(1.0f, -2 * iterations), iterations);
		}
		SolverChainFixture changing;
		changing.ResetMotion();
		changing.CheckStep(-0.5f, 1);
		changing.system.SetSolverIterations(8);
		changing.ResetMotion();
		changing.CheckStep(-std::ldexp(1.0f, -9), 8);
	}

	int RunSolverIterationsRegression()
	{
		TestSolverIterationConfiguration();
		TestSolverIterationTraversal();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " solver-iteration checks failed\n";
			return 1;
		}
		std::cout << "Solver-iteration regression: " << testCount << " checks passed\n";
		return 0;
	}


	void TestPositionStabilization()
	{
		using namespace GEngine;
		using Component::BodyType;
		ShapeBox box(BoxPoints(Vec3f(1.0f, 2.0f, 3.0f)));
		const Quat rotation = glm::angleAxis(0.63f, glm::normalize(Vec3f(1.0f, 2.0f, 3.0f)));
		const Vec3f axis = rotation * Vec3f(0.0f, 1.0f, 0.0f);
		for (const bool reversed : { false, true })
		for (const float inverseA : { 0.25f, 1.0f, 4.0f })
		for (const auto typeB : { BodyType::Static, BodyType::Kinematic, BodyType::Dynamic })
		{
			RigidBody3D bodyA, bodyB;
			ConfigureBoxBody(bodyA, box, Vec3f(0.0f), rotation);
			ConfigureBoxBody(bodyB, box, -0.12f * axis, rotation);
			bodyA.m_InvMass = inverseA;
			bodyB.Type = typeB;
			bodyB.m_InvMass = 2.0f; // Deliberately contradictory for non-dynamic types.
			bodyA.m_Friction = bodyB.m_Friction = 0.0f;
			ConstraintPenetration constraint;
			constraint.m_bodyA = reversed ? &bodyB : &bodyA;
			constraint.m_bodyB = reversed ? &bodyA : &bodyB;
			constraint.m_anchorA = constraint.m_anchorB = Vec3f(0.0f);
			constraint.m_Normal = Vec3f(0.0f, reversed ? -1.0f : 1.0f, 0.0f);
			constraint.PreSolve(1.0f / 120.0f);
			constraint.Solve();
			Expect(Near(bodyA.m_LinearVelocity, Vec3f(0.0f), 0.0f) &&
				Near(bodyB.m_LinearVelocity, Vec3f(0.0f), 0.0f) &&
				Near(constraint.m_CachedLambda[0], 0.0f, 0.0f),
				"penetration alone creates no physical impulse or warm-start support");
			// Non-principal spin checks that correction preserves anisotropic kinetic energy.
			bodyA.m_LinearVelocity = Vec3f(1.0f, 2.0f, 3.0f);
			bodyA.m_AngularVelocity = Vec3f(0.7f, 1.1f, 1.6f);
			const auto inertia = bodyA.GetInverseInertiaTensorWorldSpace();
			const auto bounds = bodyA.GetWorldBounds();
			constraint.PostSolve();
			const float inverseB = typeB == BodyType::Dynamic ? 2.0f : 0.0f;
			const auto deltaA = -axis * (0.025f * inverseA / (inverseA + inverseB));
			const auto deltaB = axis * (0.025f * inverseB / (inverseA + inverseB));
			Expect(Near(bodyA.m_Position, deltaA, 2.0e-6f) &&
				Near(bodyB.m_Position, -0.12f * axis + deltaB, 2.0e-6f),
				"position correction repels in either order with effective inverse-mass weights");
			Expect(Near(bodyA.m_LinearVelocity, Vec3f(1.0f, 2.0f, 3.0f), 0.0f) &&
				Near(bodyA.m_AngularVelocity, Vec3f(0.7f, 1.1f, 1.6f), 0.0f) &&
				bodyA.m_Orientation == rotation &&
				Near(bodyA.GetInverseInertiaTensorWorldSpace(), inertia, 0.0f),
				"position pass preserves velocities and anisotropic inertia exactly");
			Expect(bodyA.HasFiniteState() && bodyB.HasFiniteState() &&
				Near(bodyA.GetCenterOfMassWorldSpace(), bodyA.m_Position) &&
				Near(bodyA.GetWorldBounds().mins, bounds.mins + deltaA, 2.0e-6f),
				"position edits refresh warmed COM and bounds caches with finite output");
		}

		for (const float depth : { -0.1f, 0.0f, 0.01f, 0.02f, 0.12f, 10.0f })
		for (const float dt : { -0.01f, 0.0f, 1.0e-8f, 1.0f / 240.0f, 1.0f / 60.0f })
		{
			RigidBody3D body, support;
			ConfigureBoxBody(body, box, Vec3f(0.0f, -depth, 0.0f), Quat(1, 0, 0, 0));
			ConfigureBoxBody(support, box, Vec3f(0.0f), Quat(1, 0, 0, 0));
			support.Type = BodyType::Static;
			ConstraintPenetration constraint;
			constraint.m_bodyA = &support;
			constraint.m_bodyB = &body;
			constraint.m_anchorA = constraint.m_anchorB = Vec3f(0.0f);
			constraint.m_Normal = Vec3f(0.0f, 1.0f, 0.0f);
			constraint.PreSolve(dt);
			constraint.PostSolve();
			const float correction = dt > Math::NumericalEpsilon ?
				std::min(0.2f, 0.25f * std::max(0.0f, depth - 0.02f)) : 0.0f;
			Expect(Near(body.m_Position.y, -depth + correction, 2.0e-6f) &&
				Near(body.m_LinearVelocity, Vec3f(0.0f), 0.0f),
				"position pass honors separation, slop, cap, and non-positive/tiny timestep guards");
		}
	}

	void TestPositionStabilizationWorld()
	{
		using namespace GEngine;
		ShapeSphere sphere(1.0f);
		for (const int iterations : { 1, 8, 32 })
		{
			PhysicsSystem system;
			system.SetSolverIterations(iterations);
			auto* world = new PhysicsWorld(Vec3f(0.0f));
			system.SetPhysicsWorld(world);
			auto* support = world->CreateRigidBody3D();
			auto* body = world->CreateRigidBody3D();
			ConfigureSphereBody(*support, sphere, Vec3f(0.0f));
			ConfigureSphereBody(*body, sphere, Vec3f(0.0f, 1.88f, 0.0f));
			support->Type = Component::BodyType::Static;
			support->m_Friction = body->m_Friction = 0.0f;
			support->m_CollisionMask = body->m_CollisionMask = 0;
			GetManifolds(system).AddContact(MakeContact(*support, *body,
				Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 0.88f, 0.0f), Vec3f(0.0f, -1.0f, 0.0f)));
			body->m_LinearVelocity = Vec3f(0.0f, 0.6f, 0.0f);
			system.Update(Timestep(1.0f / 60.0f));
			Expect(Near(body->m_Position.y, 1.9125f, 2.0e-6f) &&
				Near(body->m_LinearVelocity.y, 0.6f, 0.0f),
				"world corrects current depth once after integration independent of velocity pass count");
			body->m_Position.y = 1.88f;
			body->m_LinearVelocity = Vec3f(0.0f);
			for (int step = 0; step < 120; ++step) system.Update(Timestep(1.0f / 120.0f));
			Expect(Near(body->m_Position.y, 1.98f, 2.0e-6f) &&
				Near(body->m_LinearVelocity, Vec3f(0.0f), 0.0f) &&
				Near(body->m_AngularVelocity, Vec3f(0.0f), 0.0f),
				"repeated world correction converges to slop without bounce or cached correction impulses");
		}
	}


	void TestPenetratedStackPositionStabilization()
	{
		using namespace GEngine;
		ShapeBox box(UnitBoxPoints());
		ShapeBox floor(BoxPoints(Vec3f(50.0f, 0.5f, 50.0f)));
		std::array<Vec3f, 16> reference{};
		for (int repeat = 0; repeat < 2; ++repeat)
		{
			PhysicsSystem system;
			auto* world = new PhysicsWorld(Vec3f(0.0f));
			system.SetPhysicsWorld(world);
			auto* support = world->CreateRigidBody3D();
			ConfigureBoxBody(*support, floor, Vec3f(0.0f), Quat(1, 0, 0, 0));
			support->Type = Component::BodyType::Static;
			support->m_CollisionMask = 0;
			std::array<RigidBody3D*, 16> bodies{};
			for (int column = 0; column < 4; ++column)
			for (int level = 0; level < 4; ++level)
			{
				const int index = 4 * column + level;
				auto* body = bodies[index] = world->CreateRigidBody3D();
				ConfigureBoxBody(*body, box, Vec3f(column * 2.01f, 1.38f + level * 1.88f, 0.0f), Quat(1, 0, 0, 0));
				body->m_CollisionMask = 0;
				auto* below = level == 0 ? support : bodies[index - 1];
				const Vec3f anchorA(body->m_Position.x, level == 0 ? 0.5f : below->m_Position.y + 1.0f, 0.0f);
				const Vec3f anchorB = body->m_Position - Vec3f(0.0f, 1.0f, 0.0f);
				GetManifolds(system).AddContact(MakeContact(*below, *body, anchorA, anchorB, Vec3f(0.0f, -1.0f, 0.0f)));
			}
			double peakKinetic = 0.0;
			bool finite = true;
			for (int step = 0; step < 1200; ++step)
			{
				system.Update(Timestep(1.0f / 120.0f));
				double kinetic = 0.0;
				for (const auto* body : bodies)
				{
					finite = finite && body->HasFiniteState();
					kinetic += 0.5 * glm::length2(body->m_LinearVelocity) + glm::length2(body->m_AngularVelocity) / 3.0;
				}
				peakKinetic = std::max(peakKinetic, kinetic);
			}
			float maxDepth = 0.0f;
			float averageY = 0.0f;
			for (int index = 0; index < 16; ++index)
			{
				const int level = index % 4;
				const float supportTop = level == 0 ? 0.5f : bodies[index - 1]->m_Position.y + 1.0f;
				maxDepth = std::max(maxDepth, supportTop - (bodies[index]->m_Position.y - 1.0f));
				averageY += bodies[index]->m_Position.y / 16.0f;
				if (repeat == 0) reference[index] = bodies[index]->m_Position;
				else Expect(Near(bodies[index]->m_Position, reference[index], 0.0f),
					"penetrated stack repeats exactly in fixed creation and contact order");
			}
			Expect(finite && peakKinetic == 0.0, "penetrated 4x4 stack has no kinetic-energy injection in zero gravity");
			Expect(maxDepth <= 0.02001f && averageY >= 4.44998f,
				"penetrated stack reduces every 0.12 overlap to 0.02 slop without collapse");
			std::cout << "POSITION_STACK repeat=" << repeat << " peak_kinetic=" << peakKinetic
				<< " final_max_depth=" << maxDepth << " final_average_y=" << averageY << '\n';
		}
	}


	void TestPositionCorrectionGuardsAndFriction()
	{
		using namespace GEngine;
		ShapeSphere sphere(1.0f);
		for (int scenario = 0; scenario < 7; ++scenario)
		{
			RigidBody3D a, b;
			ConfigureSphereBody(a, sphere, Vec3f(0.0f));
			ConfigureSphereBody(b, sphere, Vec3f(0.0f, -0.12f, 0.0f));
			a.Type = Component::BodyType::Static;
			ConstraintPenetration constraint;
			constraint.m_bodyA = &a;
			constraint.m_bodyB = &b;
			constraint.m_anchorA = constraint.m_anchorB = Vec3f(0.5f, 0.0f, 0.0f);
			constraint.m_Normal = Vec3f(0.0f, 1.0f, 0.0f);
			if (scenario != 0) constraint.PreSolve(1.0f / 120.0f);
			if (scenario == 1) constraint.m_Normal = Vec3f(0.0f);
			if (scenario == 2) constraint.m_Normal.x = std::numeric_limits<float>::quiet_NaN();
			if (scenario == 3) constraint.m_anchorA.x = std::numeric_limits<float>::infinity();
			if (scenario == 4) b.m_Position.x = 0.021f; // Witness drifts outside the manifold tolerance.
			if (scenario == 5) b.Type = Component::BodyType::Kinematic; // No responsive participant.
			if (scenario == 6) constraint.m_bodyB = &a;
			const auto position = b.m_Position;
			constraint.PostSolve();
			Expect(a.HasFiniteState() && b.HasFiniteState() && Near(b.m_Position, position, 0.0f),
				"unprepared, invalid, drifted, immovable, and self-pair corrections fail safely");
		}
		for (const float closingSpeed : { 0.0f, 1.0f })
		{
			RigidBody3D a, b;
			ConfigureSphereBody(a, sphere, Vec3f(0.0f));
			ConfigureSphereBody(b, sphere, Vec3f(0.0f, -0.12f, 0.0f));
			a.Type = Component::BodyType::Static;
			a.m_Friction = b.m_Friction = 0.5f;
			b.m_LinearVelocity = Vec3f(3.0f, -closingSpeed, 4.0f);
			ConstraintPenetration constraint;
			constraint.m_bodyA = &a;
			constraint.m_bodyB = &b;
			constraint.m_anchorA = constraint.m_anchorB = Vec3f(0.0f);
			constraint.m_Normal = Vec3f(0.0f, 1.0f, 0.0f);
			constraint.PreSolve(1.0f / 120.0f);
			constraint.Solve();
			const Vec3f expectedVelocity = Vec3f(3.0f, 0.0f, 4.0f) * (1.0f - 0.05f * closingSpeed);
			Expect(Near(b.m_LinearVelocity, expectedVelocity, 2.0e-6f) &&
				Near(constraint.m_CachedLambda[0], closingSpeed, 2.0e-6f),
				"penetration supplies no friction support while real normal impulses retain Coulomb friction");
			const auto velocity = b.m_LinearVelocity;
			const auto lambda = constraint.m_CachedLambda;
			constraint.PostSolve();
			constraint.PostSolve();
			Expect(Near(b.m_Position.y, -0.095f, 2.0e-6f) && Near(b.m_LinearVelocity, velocity, 0.0f) &&
				constraint.m_CachedLambda[0] == lambda[0] && constraint.m_CachedLambda[1] == lambda[1] &&
				constraint.m_CachedLambda[2] == lambda[2],
				"position pass is consumed once and leaves physical warm-start impulses untouched");
		}
	}

	int RunPositionStabilizationRegression()
	{
		TestPositionStabilization();
		TestPositionStabilizationWorld();
		TestPositionCorrectionGuardsAndFriction();
		TestPenetratedStackPositionStabilization();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " position-stabilization checks failed\n";
			return 1;
		}
		std::cout << "Position-stabilization regression: " << testCount << " checks passed\n";
		return 0;
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
		constraint.PostSolve();
		Expect(Near(bodyA.m_Position, GEngine::Vec3f(0.0f), 0.0f), "zero dt produces zero penetration correction");
		constraint.PreSolve(1.0e-8f);
		constraint.PostSolve();
		Expect(Near(bodyA.m_Position, GEngine::Vec3f(0.0f), 0.0f), "near-zero dt produces zero penetration correction");
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


	void TestRollingResistanceRows()
	{
		using namespace GEngine;
		for (float radius : {0.25f, 1.0f, 3.0f})
		for (float inverseMass : {0.5f, 2.0f})
		for (float length : {0.0f, 0.01f, 0.2f, 100.0f})
		for (float closing : {0.0f, 0.2f, 2.0f}) {
			FrictionFixture f(0.0f, inverseMass);
			f.sphere.SetRadius(radius);
			f.body.SetRollingResistanceLength(length); f.support.SetRollingResistanceLength(0.2f);
			f.body.m_LinearVelocity = Vec3f(0, -closing, 0);
			f.body.m_AngularVelocity = Vec3f(3, 2, 4);
			const float inertia = 0.4f * radius * radius / inverseMass;
			const float bound = std::min(length, 0.2f) * closing / inverseMass;
			const float impulse = std::min(bound, inertia * 5.0f);
			const float scale = 1.0f - impulse / (inertia * 5.0f);
			ResetPhysicsProfile(); f.constraint.PreSolve(1.0f/60); f.constraint.Solve();
			Expect(Near(f.body.m_AngularVelocity, Vec3f(3*scale, 2, 4*scale), 3e-5f),
				"rolling row follows the load/length/inertia law and leaves isotropic normal spin unchanged");
			Expect(glm::length(f.constraint.GetRollingImpulse()) <= bound + 2e-6 &&
				Near(float(glm::length(f.constraint.GetRollingImpulse())), impulse, 3e-5f),
				"rolling impulse uses one circular load budget across radius, mass and material values");
			const auto first=f.body.m_AngularVelocity;
			for(int pass=0;pass<7;++pass) f.constraint.Solve();
			Expect(Near(first,f.body.m_AngularVelocity,3e-5f),"extra passes do not multiply rolling resistance");
			if(IsPhysicsProfilingEnabled()) Expect((GetPhysicsProfileSnapshot().rollingResistanceImpulseCount>0)==(impulse>0),
				"rolling counters distinguish applied resistance from disabled and unloaded rows");
		}
		FrictionFixture f(0);
		f.body.SetRollingResistanceLength(0.1f);f.support.SetRollingResistanceLength(0.1f);
		f.body.m_LinearVelocity=Vec3f(0,-1,0);f.body.m_AngularVelocity=Vec3f(3,2,4);
		f.constraint.PreSolve(1.0f/60);f.constraint.Solve();
		const auto spun=f.body.m_AngularVelocity;
		f.body.m_LinearVelocity=Vec3f(0,1,0);f.constraint.PreSolve(1.0f/120);
		Expect(f.constraint.GetRollingImpulse()==glm::dvec2(0) && f.body.m_AngularVelocity==spun,
			"rolling starts cold after a changed timestep without obsolete-load warm starting");
		f.constraint.Solve();
		Expect(f.constraint.m_CachedLambda[0]==0 && f.constraint.GetRollingImpulse()==glm::dvec2(0) && f.body.m_AngularVelocity==spun,
			"separating contact receives no new rolling torque");
		for(float dt : {0.0f,-0.01f}) {
			f.body.m_LinearVelocity=Vec3f(0,-1,0);f.constraint.PreSolve(dt);f.constraint.Solve();
			Expect(f.constraint.GetRollingImpulse()==glm::dvec2(0),"paused and reverse steps disable rolling resistance");
		}
		// Shrinking the supporting load must retract an already applied within-step budget.
		f.body.m_LinearVelocity=Vec3f(0,-1,0);f.constraint.PreSolve(1.0f/60);f.constraint.Solve();
		f.body.m_LinearVelocity=Vec3f(0,2,0);f.constraint.Solve();
		Expect(f.constraint.m_CachedLambda[0]==0 && glm::length(f.constraint.GetRollingImpulse())<1e-7,
			"rolling accumulation retracts when another solver row removes the normal load");
	}

	void TestRollingTwoBodiesAndAnisotropy()
	{
		using namespace GEngine;
		for(bool reversed : {false,true})
		for(BodyType type : {BodyType::Dynamic,BodyType::Static,BodyType::Kinematic})
		for(Vec3f normal : {Vec3f(0,1,0),glm::normalize(Vec3f(1,2,3))}) {
			FrictionFixture f(0);
			const Vec3f tangent=glm::normalize(glm::cross(normal,Vec3f(1,0,0)));
			f.support.SetBodyTypeAndInverseMass(type,0.5f);
			f.body.m_Position=normal;f.support.m_Position=-normal;
			f.constraint.m_anchorA=-normal;f.constraint.m_anchorB=normal;
			f.constraint.m_Normal=-normal;
			f.body.SetRollingResistanceLength(0.05f);f.support.SetRollingResistanceLength(0.1f);
			f.body.m_LinearVelocity=-normal*2.0f;
			f.body.m_AngularVelocity=tangent*4.0f+normal*2.0f;f.support.m_AngularVelocity=tangent;
			if(reversed) {
				std::swap(f.constraint.m_bodyA,f.constraint.m_bodyB);
				std::swap(f.constraint.m_anchorA,f.constraint.m_anchorB);f.constraint.m_Normal=normal;
			}
			const float inverseB=f.support.GetInverseMass();
			const float impulse=0.05f*2.0f/(1.0f+inverseB);
			const auto momentum=f.body.m_AngularVelocity*0.4f+f.support.m_AngularVelocity*0.8f;
			f.constraint.PreSolve(1.0f/60);f.constraint.Solve();
			Expect(Near(f.body.m_AngularVelocity,tangent*(4.0f-2.5f*impulse)+normal*2.0f,3e-5f) &&
				Near(f.support.m_AngularVelocity,tangent*(1.0f+2.5f*inverseB*impulse),3e-5f),
				"sphere-sphere rolling respects pair reversal, normal directions and static/kinematic boundaries");
			if(type==BodyType::Dynamic) Expect(Near(momentum,f.body.m_AngularVelocity*0.4f+f.support.m_AngularVelocity*0.8f,3e-5f),
				"two dynamic spheres conserve angular momentum while relative rolling decays");
		}
		for(float length : {0.05f,100.0f}) {
			FrictionFixture f(0);ShapeBox box(BoxPoints(Vec3f(1,2,3)));f.body.m_Shape=&box;
			f.body.m_Orientation=glm::angleAxis(0.7f,glm::normalize(Vec3f(1,1,1)));
			f.body.SetRollingResistanceLength(length);f.support.SetRollingResistanceLength(length);
			f.body.m_LinearVelocity=Vec3f(0,-1,0);f.body.m_AngularVelocity=Vec3f(3,2,4);
			f.constraint.m_Normal=f.body.GetWorldToBodyRotation()*Vec3f(0,-1,0);
			const auto inertia=glm::inverse(f.body.GetInverseInertiaTensorWorldSpace());
			const auto before=f.body.m_AngularVelocity;
			const double energy=0.5*glm::dot(before,inertia*before);
			f.constraint.PreSolve(1.0f/60);f.constraint.Solve();
			const auto after=f.body.m_AngularVelocity, applied=inertia*(after-before);
			Expect(0.5*glm::dot(after,inertia*after)<=energy+2e-5 && std::abs(applied.y)<2e-5,
				"anisotropic rolling applies tangent torque without injecting kinetic energy");
			if(length<1) {
				Expect(Near(glm::length(applied),length,3e-5f) && std::abs(applied.x*after.z-applied.z*after.x)<3e-5 &&
					glm::dot(applied,after)<=2e-5,"bounded anisotropic rolling satisfies the circular-disk KKT condition");
			} else Expect(std::hypot(after.x,after.z)<3e-5,"unbounded anisotropic rolling cancels both tangent angular components");
		}
		FrictionFixture control(0),rolling(0);
		for(auto*f : {&control,&rolling}) {
			f->body.SetSpinResistanceLength(0.05f);f->support.SetSpinResistanceLength(0.05f);
			f->body.m_LinearVelocity=Vec3f(0,-1,0);f->body.m_AngularVelocity=Vec3f(0,4,0);
		}
		rolling.body.SetRollingResistanceLength(0.05f);rolling.support.SetRollingResistanceLength(0.05f);
		for(auto*f : {&control,&rolling}) {f->constraint.PreSolve(1.0f/60);f->constraint.Solve();}
		Expect(control.body.m_AngularVelocity==rolling.body.m_AngularVelocity &&
			control.constraint.GetSpinImpulse()==rolling.constraint.GetSpinImpulse() && rolling.constraint.GetRollingImpulse()==glm::dvec2(0),
			"pure normal spin is governed only by Phase 37 without double-counted rolling resistance");
	}

	void TestCombinedAngularResistanceAnisotropy()
	{
		using namespace GEngine;
		// Small material exercises both active bounds; large material exercises coupled cancellation.
		for (float length : {0.05f, 100.0f}) {
			const auto run = [&](bool reversed) {
				FrictionFixture f(0);
				ShapeBox shapeA(BoxPoints(Vec3f(1,2,3))), shapeB(BoxPoints(Vec3f(1.4f,0.75f,2.2f)));
				f.body.m_Shape=&shapeA;f.support.m_Shape=&shapeB;
				f.support.SetBodyTypeAndInverseMass(BodyType::Dynamic,0.5f);
				f.body.m_Orientation=glm::angleAxis(0.7f,glm::normalize(Vec3f(1,1,1)));
				f.support.m_Orientation=glm::angleAxis(-0.4f,glm::normalize(Vec3f(2,-1,1)));
				const Vec3f normal=glm::normalize(Vec3f(2,1,3));
				f.body.m_LinearVelocity=normal;
				f.body.m_AngularVelocity=Vec3f(1.5f,2.0f,-0.75f);
				f.support.m_AngularVelocity=Vec3f(-0.35f,0.4f,0.7f);
				for(auto* body : {&f.body,&f.support}) {
					body->SetSpinResistanceLength(length);body->SetRollingResistanceLength(length);
				}
				if(reversed) std::swap(f.constraint.m_bodyA,f.constraint.m_bodyB);
				f.constraint.m_Normal=f.constraint.m_bodyA->GetWorldToBodyRotation()*(reversed?-normal:normal);
				const glm::dmat3 inertiaA=glm::inverse(glm::dmat3(f.body.GetInverseInertiaTensorWorldSpace()));
				const glm::dmat3 inertiaB=glm::inverse(glm::dmat3(f.support.GetInverseInertiaTensorWorldSpace()));
				const auto energy = [&]() {
					const glm::dvec3 a(f.body.m_AngularVelocity),b(f.support.m_AngularVelocity);
					return 0.5*(glm::dot(a,inertiaA*a)+glm::dot(b,inertiaB*b));
				};
				const auto momentum=inertiaA*glm::dvec3(f.body.m_AngularVelocity)+inertiaB*glm::dvec3(f.support.m_AngularVelocity);
				const double initial=energy();double previous=initial,peak=initial;
				const float initialRelative=glm::length(f.body.m_AngularVelocity-f.support.m_AngularVelocity);
				bool finite=true,dissipative=true,bounded=true,bothActive=false;
				Vec3f previousA,previousB;
				f.constraint.PreSolve(1.0f/60);
				for(int pass=0;pass<32;++pass) {
					previousA=f.body.m_AngularVelocity;previousB=f.support.m_AngularVelocity;
					f.constraint.Solve();const double current=energy();peak=std::max(peak,current);
					finite &= f.body.HasFiniteState() && f.support.HasFiniteState() && std::isfinite(current);
					dissipative &= current<=previous+1e-6*std::max(1.0,initial);previous=current;
					const double limit=double(length)*std::max(0.0f,f.constraint.m_CachedLambda[0]);
					bounded &= std::abs(f.constraint.GetSpinImpulse())<=limit+3e-5 && glm::length(f.constraint.GetRollingImpulse())<=limit+3e-5;
					bothActive |= std::abs(f.constraint.GetSpinImpulse())>1e-6 && glm::length(f.constraint.GetRollingImpulse())>1e-6;
				}
				const float relative=glm::length(f.body.m_AngularVelocity-f.support.m_AngularVelocity);
				Expect(finite && dissipative && peak<=initial+1e-6*std::max(1.0,initial),
					"combined spin/rolling on rotated anisotropic bodies stays finite and dissipative on every pass");
				Expect(bounded && bothActive,"combined anisotropic contact exercises both independent physical load bounds");
				Expect(Near(previousA,f.body.m_AngularVelocity,3e-5f) && Near(previousB,f.support.m_AngularVelocity,3e-5f) &&
					(length>1 ? relative<3e-5f : relative<initialRelative),
					"spin/rolling coupling converges without alternating growth or residual unconstrained rotation");
				Expect(glm::length(momentum-inertiaA*glm::dvec3(f.body.m_AngularVelocity)-inertiaB*glm::dvec3(f.support.m_AngularVelocity))<3e-5,
					"combined anisotropic resistance conserves equal/opposite angular momentum");
				std::cout<<"ANGULAR_COUPLING length="<<length<<" reversed="<<reversed<<" initial_energy="<<initial
					<<" peak_energy="<<peak<<" final_energy="<<previous<<" relative_speed="<<relative<<'\n';
				return std::array<Vec3f,4>{f.body.m_AngularVelocity,f.support.m_AngularVelocity,f.body.m_LinearVelocity,f.support.m_LinearVelocity};
			};
			const auto forward=run(false),repeat=run(false),reversed=run(true);
			Expect(forward==repeat,"combined anisotropic resistance repeats with identical body velocities");
			bool equivalent=true;for(int i=0;i<4;++i)equivalent &= Near(forward[i],reversed[i],3e-5f);
			Expect(equivalent,"reversing the anisotropic contact pair preserves the combined spin/rolling response");
		}
	}

	void TestRollingPlaneAndAirborne()
	{
		using namespace GEngine;
		double stopping[2]{};int index=0;
		for(int rate : {60,120}) {
			for(bool enabled : {false,true}) {
				ShapeSphere sphere(1);ShapeBox floor(BoxPoints(Vec3f(30,0.5f,30)));
				PhysicsSystem system;auto*world=new PhysicsWorld();system.SetPhysicsWorld(world);system.SetSleepingEnabled(false);
				auto*support=world->CreateRigidBody3D();auto*body=world->CreateRigidBody3D();
				ConfigureBoxBody(*support,floor,Vec3f(0),Quat(1,0,0,0));support->SetBodyTypeAndInverseMass(BodyType::Static,0);
				ConfigureSphereBody(*body,sphere,Vec3f(0,1.5f,0));
				support->m_Friction=body->m_Friction=0.5f;support->m_Elasticity=body->m_Elasticity=0;
				support->SetRollingResistanceLength(enabled?0.05f:0);body->SetRollingResistanceLength(0.05f);
				const Vec3f direction=glm::normalize(Vec3f(1,0,1));
				body->m_LinearVelocity=2.0f*direction;body->m_AngularVelocity=glm::cross(Vec3f(0,1,0),body->m_LinearVelocity);
				double peakEnergy=20.8,stop=0;bool finite=true;std::uint64_t impulses=0;float oneSecond=0;
				for(int tick=1;tick<=8*rate;++tick) {
					ResetPhysicsProfile();system.Update(Timestep(1.0f/rate));
					const auto v=body->m_LinearVelocity,w=body->m_AngularVelocity;
					const double energy=0.5*glm::dot(v,v)+0.2*glm::dot(w,w)+12*body->m_Position.y;
					finite &= body->HasFiniteState() && std::isfinite(energy);peakEnergy=std::max(peakEnergy,energy);
					impulses+=GetPhysicsProfileSnapshot().rollingResistanceImpulseCount;
					if(tick==rate) oneSecond=glm::dot(v,direction);
					if(!stop && glm::length(v)<=0.02f && glm::length(w)<=0.02f) stop=double(tick)/rate;
				}
				// No-slip deceleration is ell*m*g*R/(I+m*R^2) = 3/7 for this fixture.
				Expect(finite && peakEnergy<=20.81,"rolling on a plane stays finite without mechanical-energy growth");
				Expect(enabled ? (std::abs(oneSecond-11.0f/7)<0.03f && stop>4.4 && stop<5.0 &&
					glm::length(body->m_LinearVelocity)<0.02f && glm::length(body->m_AngularVelocity)<0.02f) :
					(std::abs(oneSecond-2.0f)<0.02f && stop==0),"rolling translation and rotation decay at the predicted physical rate");
				if(IsPhysicsProfilingEnabled()) Expect((impulses>0)==enabled,"plane rolling telemetry respects a zero material");
				std::cout<<"ROLLING_PLANE rate="<<rate<<" enabled="<<enabled<<" one_second_speed="<<oneSecond
					<<" stop_seconds="<<stop<<" peak_energy="<<peakEnergy<<" final_v="<<glm::length(body->m_LinearVelocity)
					<<" final_w="<<glm::length(body->m_AngularVelocity)<<" impulses="<<impulses<<'\n';
				if(enabled) stopping[index]=stop;
			}
			++index;
			ShapeSphere sphere(1);PhysicsSystem system;auto*world=new PhysicsWorld(Vec3f(0));system.SetPhysicsWorld(world);
			auto*body=world->CreateRigidBody3D();auto*reference=world->CreateRigidBody3D();
			ConfigureSphereBody(*body,sphere,Vec3f(0,20,0));ConfigureSphereBody(*reference,sphere,Vec3f(100,20,0));
			body->SetRollingResistanceLength(0.05f);
			body->m_AngularVelocity=reference->m_AngularVelocity=Vec3f(1,4,2);
			ResetPhysicsProfile();for(int tick=0;tick<8*rate;++tick)system.Update(Timestep(1.0f/rate));
			Expect(body->m_AngularVelocity==reference->m_AngularVelocity && body->m_Orientation==reference->m_Orientation &&
				GetPhysicsProfileSnapshot().rollingResistanceImpulseCount==0,"airborne rolling material preserves the exact zero-material trajectory");
		}
		Expect(std::abs(stopping[0]-stopping[1])<=1.0/30+1e-6,"60/120 Hz rolling stopping times differ by at most two 60 Hz ticks");
	}

	void TestRollingMaterialAndWake()
	{
		using namespace GEngine;
		ShapeSphere sphere(1);
		PhysicsSystem system; auto* world=new PhysicsWorld(); system.SetPhysicsWorld(world);
		auto* support=world->CreateRigidBody3D(); auto* body=world->CreateRigidBody3D();
		ConfigureSphereBody(*support,sphere,Vec3f(0)); support->SetBodyTypeAndInverseMass(BodyType::Static,0);
		ConfigureSphereBody(*body,sphere,Vec3f(0,2,0));
		Expect(body->GetRollingResistanceLength()==0, "rolling resistance defaults to the approved reference behavior");
		for(float value : {-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
			Expect(!body->SetRollingResistanceLength(value) && body->GetRollingResistanceLength()==0,
				"invalid resistance lengths are rejected transactionally");
		Expect(body->SetRollingResistanceLength(std::numeric_limits<float>::max()) &&
			body->SetRollingResistanceLength(0), "finite nonnegative resistance range is accepted");
		for(int i=0;i<120;++i) system.Update(Timestep(1.0f/60));
		Expect(body->IsSleeping(), "material-wake fixture reaches settled sleep");
		support->SetRollingResistanceLength(0); system.Update(Timestep(1.0f/60));
		Expect(body->IsSleeping(), "unchanged resistance setting preserves sleep");
		support->SetRollingResistanceLength(0.05f); system.Update(Timestep(1.0f/60));
		Expect(!body->IsSleeping(), "static material edit wakes its supported dependent via existing wake connectivity");
		for(int i=0;i<120;++i) system.Update(Timestep(1.0f/60));
		body->SetRollingResistanceLength(0.05f);
		Expect(!body->IsSleeping() && body->GetInactiveSeconds()==0,
			"dynamic material edit immediately wakes and clears dwell");
		// The largest valid length must still yield a finite stop impulse.
		FrictionFixture f(0);
		f.body.SetRollingResistanceLength(std::numeric_limits<float>::max());
		f.support.SetRollingResistanceLength(std::numeric_limits<float>::max());
		f.body.m_LinearVelocity=Vec3f(0,-1,0);f.body.m_AngularVelocity=Vec3f(4,0,0);
		f.constraint.PreSolve(1.0f/60);f.constraint.Solve();
		Expect(f.body.HasFiniteState() && std::abs(f.body.m_AngularVelocity.x)<1e-5,
			"extreme finite material length remains bounded by the finite cancellation impulse");
	}

	int RunRollingResistanceRegression()
	{
		TestRollingResistanceRows();TestRollingTwoBodiesAndAnisotropy();TestRollingMaterialAndWake();TestRollingPlaneAndAirborne();TestCombinedAngularResistanceAnisotropy();
		std::cout<<"Rolling resistance: "<<testCount<<" checks, "<<failureCount<<" failures\n";
		return failureCount ? 1 : 0;
	}

	void TestSpinResistanceRows()
	{
		using namespace GEngine;
		for (float radius : {0.25f, 1.0f, 3.0f})
		for (float inverseMass : {0.5f, 2.0f})
		for (float length : {0.0f, 0.01f, 0.2f, 100.0f})
		for (float closing : {0.0f, 0.2f, 2.0f})
		{
			FrictionFixture f(0.0f, inverseMass);
			f.sphere.SetRadius(radius);
			f.body.SetSpinResistanceLength(length);
			f.support.SetSpinResistanceLength(0.2f);
			f.body.m_LinearVelocity = Vec3f(0, -closing, 0);
			f.body.m_AngularVelocity = Vec3f(1, 4, 2);
			const float inertia = 0.4f * radius * radius / inverseMass;
			const float bound = std::min(length, 0.2f) * closing / inverseMass;
			const float impulse = std::min(bound, inertia * 4.0f);
			ResetPhysicsProfile();
			f.constraint.PreSolve(1.0f/60);
			f.constraint.Solve();
			Expect(Near(f.body.m_AngularVelocity, Vec3f(1, 4 - impulse/inertia, 2), 3e-5f),
				"torsion matches the load/length/inertia law and leaves tangent-plane spin unchanged");
			Expect(std::abs(f.constraint.GetSpinImpulse()) <= bound + 2e-6 &&
				Near(float(std::abs(f.constraint.GetSpinImpulse())), impulse, 3e-5f),
				"torsional impulse has the documented bound across sizes, masses and zero loads");
			const auto first = f.body.m_AngularVelocity;
			for (int i=0;i<7;++i) f.constraint.Solve();
			Expect(Near(f.body.m_AngularVelocity, first, 3e-5f),
				"extra global passes do not multiply the accumulated resistance budget");
			if (IsPhysicsProfilingEnabled())
				Expect((GetPhysicsProfileSnapshot().spinResistanceImpulseCount > 0) == (impulse > 0),
					"spin telemetry distinguishes actual resistance from unloaded/disabled work");
		}
		// Existing normal cache may warm start, but torsion must not act on yesterday's load.
		FrictionFixture f(0.0f);
		f.body.SetSpinResistanceLength(0.1f); f.support.SetSpinResistanceLength(0.1f);
		f.body.m_LinearVelocity = Vec3f(0,-1,0); f.body.m_AngularVelocity=Vec3f(0,4,0);
		f.constraint.PreSolve(1.0f/60); f.constraint.Solve();
		const auto spun = f.body.m_AngularVelocity;
		f.body.m_LinearVelocity = Vec3f(0,1,0);
		f.constraint.PreSolve(1.0f/120);
		Expect(f.constraint.GetSpinImpulse()==0 && f.body.m_AngularVelocity==spun,
			"new timestep starts torsion cold without an obsolete-load warm start");
		f.constraint.Solve();
		Expect(f.constraint.m_CachedLambda[0]==0 && f.constraint.GetSpinImpulse()==0 &&
			f.body.m_AngularVelocity==spun, "separating contact with cached normal load applies no torsion");
		for (float dt : {0.0f,-0.01f}) {
			f.body.m_LinearVelocity=Vec3f(0,-1,0);
			f.constraint.PreSolve(dt); f.constraint.Solve();
			Expect(f.constraint.GetSpinImpulse()==0, "paused/reverse constraint preparation disables torsion");
		}
	}

	void TestSpinResistanceTwoBodies()
	{
		using namespace GEngine;
		for (bool reversed : {false,true})
		for (BodyType type : {BodyType::Dynamic,BodyType::Static,BodyType::Kinematic})
		for (Vec3f normal : {Vec3f(0,1,0), glm::normalize(Vec3f(1,2,3))})
		{
			FrictionFixture f(0.0f);
			f.support.SetBodyTypeAndInverseMass(type,0.5f);
			f.body.SetSpinResistanceLength(0.05f); f.support.SetSpinResistanceLength(0.1f);
			f.body.m_LinearVelocity=-normal*2.0f;
			f.body.m_AngularVelocity=normal*4.0f;
			f.support.m_AngularVelocity=normal; // Static stored motion must be ignored.
			f.constraint.m_Normal=-normal;
			if(reversed) {
				std::swap(f.constraint.m_bodyA,f.constraint.m_bodyB);
				f.constraint.m_Normal=normal;
			}
			const float inverseB=f.support.GetInverseMass();
			const float impulse=0.05f*2.0f/(1.0f+inverseB);
			const auto momentumBefore=f.body.m_AngularVelocity*0.4f+f.support.m_AngularVelocity*0.8f;
			f.constraint.PreSolve(1.0f/60); f.constraint.Solve();
			Expect(Near(f.body.m_AngularVelocity,normal*(4.0f-2.5f*impulse),2e-5f) &&
				Near(f.support.m_AngularVelocity,normal*(1.0f+2.5f*inverseB*impulse),2e-5f),
				"pair reversal and arbitrary normals preserve equal/opposite angular response and boundary semantics");
			if(type==BodyType::Dynamic)
				Expect(Near(f.body.m_AngularVelocity*0.4f+f.support.m_AngularVelocity*0.8f,momentumBefore,2e-5f),
					"two dynamic spheres conserve total angular momentum while relative spin decays");
		}
		// A rotated anisotropic inertia changes omega in multiple axes for a normal-axis torque.
		FrictionFixture f(0.0f);
		ShapeBox box(BoxPoints(Vec3f(1,2,3)));
		f.body.m_Shape=&box;
		f.body.m_Orientation=glm::angleAxis(0.7f,glm::normalize(Vec3f(1,1,1)));
		f.body.SetSpinResistanceLength(100); f.support.SetSpinResistanceLength(100);
		f.body.m_LinearVelocity=Vec3f(0,-1,0); f.body.m_AngularVelocity=Vec3f(1,4,2);
		f.constraint.m_Normal=f.body.GetWorldToBodyRotation()*Vec3f(0,-1,0);
		const auto inverse=f.body.GetInverseInertiaTensorWorldSpace();
		const auto before=f.body.m_AngularVelocity;
		const double initial=0.5*glm::dot(before,glm::inverse(inverse)*before);
		f.constraint.PreSolve(1.0f/60); f.constraint.Solve();
		const auto after=f.body.m_AngularVelocity;
		Expect(std::abs(after.y)<2e-5 && 0.5*glm::dot(after,glm::inverse(inverse)*after)<=initial+1e-5,
			"world inverse inertia cancels relative normal spin without anisotropic kinetic-energy injection");
	}

	void TestSpinMaterialAndWake()
	{
		using namespace GEngine;
		ShapeSphere sphere(1);
		PhysicsSystem system; auto* world=new PhysicsWorld(); system.SetPhysicsWorld(world);
		auto* support=world->CreateRigidBody3D(); auto* body=world->CreateRigidBody3D();
		ConfigureSphereBody(*support,sphere,Vec3f(0)); support->SetBodyTypeAndInverseMass(BodyType::Static,0);
		ConfigureSphereBody(*body,sphere,Vec3f(0,2,0));
		Expect(body->GetSpinResistanceLength()==0, "spin resistance defaults to the approved reference behavior");
		for(float value : {-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
			Expect(!body->SetSpinResistanceLength(value) && body->GetSpinResistanceLength()==0,
				"invalid resistance lengths are rejected transactionally");
		Expect(body->SetSpinResistanceLength(std::numeric_limits<float>::max()) &&
			body->SetSpinResistanceLength(0), "finite nonnegative resistance range is accepted");
		for(int i=0;i<120;++i) system.Update(Timestep(1.0f/60));
		Expect(body->IsSleeping(), "material-wake fixture reaches settled sleep");
		support->SetSpinResistanceLength(0); system.Update(Timestep(1.0f/60));
		Expect(body->IsSleeping(), "unchanged resistance setting preserves sleep");
		support->SetSpinResistanceLength(0.05f); system.Update(Timestep(1.0f/60));
		Expect(!body->IsSleeping(), "static material edit wakes its supported dependent via existing wake connectivity");
		for(int i=0;i<120;++i) system.Update(Timestep(1.0f/60));
		body->SetSpinResistanceLength(0.05f);
		Expect(!body->IsSleeping() && body->GetInactiveSeconds()==0,
			"dynamic material edit immediately wakes and clears dwell");
		// The largest valid length must still yield a finite stop impulse.
		FrictionFixture f(0);
		f.body.SetSpinResistanceLength(std::numeric_limits<float>::max());
		f.support.SetSpinResistanceLength(std::numeric_limits<float>::max());
		f.body.m_LinearVelocity=Vec3f(0,-1,0);f.body.m_AngularVelocity=Vec3f(0,4,0);
		f.constraint.PreSolve(1.0f/60);f.constraint.Solve();
		Expect(f.body.HasFiniteState() && std::abs(f.body.m_AngularVelocity.y)<1e-5,
			"extreme finite material length remains bounded by the finite cancellation impulse");
	}

	void TestSupportedSpinAndAirborne()
	{
		using namespace GEngine;
		double stopped[2]{};
		int rateIndex=0;
		for(int rate : {60,120}) {
			for(bool enabled : {false,true}) {
				ShapeSphere sphere(1); ShapeBox floor(BoxPoints(Vec3f(20,0.5f,20)));
				PhysicsSystem system; auto* world=new PhysicsWorld(); system.SetPhysicsWorld(world);
				system.SetSleepingEnabled(false); // Measure physical decay before sleep truncation.
				auto* support=world->CreateRigidBody3D(); auto* body=world->CreateRigidBody3D();
				ConfigureBoxBody(*support,floor,Vec3f(0),Quat(1,0,0,0));
				support->SetBodyTypeAndInverseMass(BodyType::Static,0);
				ConfigureSphereBody(*body,sphere,Vec3f(0,1.5f,0));
				support->m_Friction=body->m_Friction=0;
				support->m_Elasticity=body->m_Elasticity=0;
				support->SetSpinResistanceLength(enabled?0.05f:0);body->SetSpinResistanceLength(0.05f);
				body->m_AngularVelocity=Vec3f(0,4,0);
				double maxEnergy=21.2, stop=0;bool finite=true;std::uint64_t applied=0;
				float atOne=0;
				for(int tick=1;tick<=4*rate;++tick) {
					ResetPhysicsProfile(); system.Update(Timestep(1.0f/rate));
					const auto v=body->m_LinearVelocity,w=body->m_AngularVelocity;
					const double energy=0.5*glm::dot(v,v)+0.2*glm::dot(w,w)+12*body->m_Position.y;
					finite &= body->HasFiniteState() && std::isfinite(energy);
					maxEnergy=std::max(maxEnergy,energy);
					applied+=GetPhysicsProfileSnapshot().spinResistanceImpulseCount;
					if(tick==rate) atOne=w.y;
					if(!stop && std::abs(w.y)<=0.02f) stop=double(tick)/rate;
				}
				Expect(finite && maxEnergy<=21.2+0.01, "supported pure spin stays finite without mechanical-energy growth");
				Expect(enabled ? (std::abs(atOne-2.5f)<0.02f && stop>2.5 && stop<2.8) :
					(std::abs(atOne-4.0f)<1e-5f && stop==0),
					"plane spin decay matches torque=length*mg; a zero material preserves free normal-axis spin");
				if(IsPhysicsProfilingEnabled()) Expect((applied>0)==enabled,"plane probe reports applied torsional resistance");
				std::cout<<"SPIN_PLANE rate="<<rate<<" enabled="<<enabled<<" one_second_spin="<<atOne
					<<" stop_seconds="<<stop<<" peak_energy="<<maxEnergy<<" final_spin="<<body->m_AngularVelocity.y
					<<" impulses="<<applied<<'\n';
				if(enabled) stopped[rateIndex]=stop;
			}
			++rateIndex;
			ShapeSphere sphere(1);PhysicsSystem system;auto* world=new PhysicsWorld(Vec3f(0));system.SetPhysicsWorld(world);
			auto* body=world->CreateRigidBody3D();ConfigureSphereBody(*body,sphere,Vec3f(0,20,0));
			body->SetSpinResistanceLength(1);body->m_AngularVelocity=Vec3f(1,4,2);
			auto* reference=world->CreateRigidBody3D();ConfigureSphereBody(*reference,sphere,Vec3f(100,20,0));
			reference->m_AngularVelocity=body->m_AngularVelocity;
			ResetPhysicsProfile();
			for(int tick=0;tick<4*rate;++tick) system.Update(Timestep(1.0f/rate));
			Expect(body->m_AngularVelocity==reference->m_AngularVelocity && body->m_Orientation==reference->m_Orientation &&
				Near(body->m_AngularVelocity,Vec3f(1,4,2),1e-5f) && GetPhysicsProfileSnapshot().spinResistanceImpulseCount==0,
				"airborne body receives no contact resistance at either tick rate");
		}
		Expect(std::abs(stopped[0]-stopped[1])<=1.0/60+1e-6,
			"60/120 Hz pure-spin settling differs by at most one 60 Hz tick");
	}

	int RunSpinResistanceRegression()
	{
		TestSpinResistanceRows();
		TestSpinResistanceTwoBodies();
		TestSpinMaterialAndWake();
		TestSupportedSpinAndAirborne();
		std::cout<<"Spin resistance: "<<testCount<<" checks, "<<failureCount<<" failures\n";
		return failureCount ? 1 : 0;
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



	void TestManifoldNormalConvergence()
	{
		using namespace GEngine;
		ShapeBox box(UnitBoxPoints());
		for (const bool dynamicSupport : { false, true })
		for (const bool permuted : { false, true })
		for (const int rate : { 60, 120 })
		{
			PhysicsWorld world(Vec3f(0));
			auto* a = world.CreateRigidBody3D(); auto* b = world.CreateRigidBody3D();
			const Quat rotation = glm::angleAxis(0.37f, glm::normalize(Vec3f(1, 2, 3)));
			const Vec3f up = rotation * Vec3f(0, 1, 0);
			ConfigureBoxBody(*a, box, Vec3f(0), rotation);
			ConfigureBoxBody(*b, box, 1.99f * up, rotation);
			a->SetBodyTypeAndInverseMass(dynamicSupport ? BodyType::Dynamic : BodyType::Static, dynamicSupport ? 0.5f : 0);
			b->SetBodyTypeAndInverseMass(BodyType::Dynamic, 2);
			a->m_Friction = b->m_Friction = 0;
			std::array<contact_t, 4> patch{};
			Expect(BuildBoxFaceContacts(MakeContact(*a, *b, up, 0.99f * up, -up), patch) == 4,
				"normal-block fixture contains four coplanar, redundant contact rows");
			if (permuted) {
				std::reverse(patch.begin(), patch.end());
				for (auto& contact : patch) contact = ReversedContact(contact);
			}
			ManifoldCollector pair; pair.AddContacts(patch.data(), 4);
			for (const float closing : { 12.0f / rate, 12.0f / rate, -12.0f / rate }) {
				// Second step reuses real cached support; third must retract it.
				a->m_LinearVelocity = dynamicSupport ? Vec3f(0) : Vec3f(10, -7, 3);
				a->m_AngularVelocity = dynamicSupport ? Vec3f(0) : Vec3f(2, 1, -1);
				b->m_LinearVelocity = -closing * up; b->m_AngularVelocity = Vec3f(0);
				pair.PreSolve(1.0f / rate); pair.Solve();
				const float expectedImpulse = std::max(0.0f, closing) / (a->GetInverseMass() + b->GetInverseMass());
				double totalImpulse = 0; bool complementary = true;
				for (int i = 0; i < 4; ++i) {
					auto& c = CachedConstraint(pair.m_Manifolds[0], i);
					const Vec3f n = c.m_bodyA->GetBodyToWorldRotation() * c.m_Normal;
					const auto velocity = [&](RigidBody3D* body, Vec3f anchor) {
						return body->GetLinearVelocity() + glm::cross(body->GetAngularVelocity(),
							body->BodySpaceToWorldSpace(anchor) - body->GetCenterOfMassWorldSpace());
					};
					const float vn = glm::dot(n, velocity(c.m_bodyB, c.m_anchorB) - velocity(c.m_bodyA, c.m_anchorA));
					totalImpulse += c.m_CachedLambda[0];
					complementary = complementary && c.m_CachedLambda[0] >= 0 && vn >= -3.0e-6f &&
						std::abs(vn * c.m_CachedLambda[0]) < 2.0e-6f;
				}
				Expect(complementary && std::abs(totalImpulse - expectedImpulse) < 3.0e-6,
					"one manifold block pass balances all normal rows, including warm retraction and separation");
				Expect(glm::length(b->m_AngularVelocity) < 3.0e-6f && glm::length(a->GetAngularVelocity()) < 3.0e-6f &&
					Near(b->m_LinearVelocity, up * (-closing + 2.0f * expectedImpulse), 3.0e-6f),
					"coplanar support preserves torque balance and analytic shared linear momentum");
				Expect(a->HasFiniteState() && b->HasFiniteState() && (dynamicSupport ||
					Near(a->m_LinearVelocity, Vec3f(10, -7, 3), 0)),
					"block solve preserves finite state and ignores stored static velocities");
			}
		}
	}

	void TestContactFrictionConvergence()
	{
		using namespace GEngine;
		ShapeBox box(UnitBoxPoints());
		for (const int rate : { 60, 120 })
		for (const float inverseMass : { 0.5f, 1.0f, 2.0f })
		for (const bool swapped : { false, true })
		for (const bool oblique : { false, true })
		{
			RigidBody3D body, support;
			const Quat rotation = oblique ? glm::angleAxis(0.63f, glm::normalize(Vec3f(1, 2, 3))) : Quat(1, 0, 0, 0);
			ConfigureBoxBody(body, box, Vec3f(0), rotation);
			ConfigureBoxBody(support, box, Vec3f(0), rotation);
			body.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic, inverseMass);
			support.SetBodyTypeAndInverseMass(Component::BodyType::Static, 0);
			body.m_Friction = 0.25f; support.m_Friction = 1;
			const Vec3f lever = oblique ? Vec3f(1, -1, 1) : Vec3f(1, -1, 0);
			body.m_LinearVelocity = rotation * Vec3f(5, -12.0f / rate, oblique ? 2.0f : 0.0f);
			body.m_AngularVelocity = support.m_AngularVelocity = support.m_LinearVelocity = Vec3f(0);
			ConstraintPenetration constraint;
			constraint.m_bodyA = swapped ? &support : &body;
			constraint.m_bodyB = swapped ? &body : &support;
			constraint.m_anchorA = constraint.m_anchorB = lever;
			constraint.m_Normal = Vec3f(0, swapped ? 1.0f : -1.0f, 0);
			const auto energy = [&]() {
				return 0.5 / inverseMass * glm::dot(body.m_LinearVelocity, body.m_LinearVelocity) +
					0.5 * glm::dot(body.m_AngularVelocity,
						glm::inverse(body.GetInverseInertiaTensorWorldSpace()) * body.m_AngularVelocity);
			};
			const double initialEnergy = energy();
			constraint.PreSolve(1.0f / rate);
			for (int pass = 0; pass < 64; ++pass) constraint.Solve();
			const Vec3f contactVelocity = body.GetWorldToBodyRotation() *
				(body.m_LinearVelocity + glm::cross(body.m_AngularVelocity, rotation * lever));
			const float normalImpulse = constraint.m_CachedLambda[0];
			Expect(body.HasFiniteState() && std::abs(contactVelocity.y) < 2.0e-5f && normalImpulse > 0,
				"off-center sliding support converges to normal complementarity at 60/120 Hz");
			Expect(energy() <= initialEnergy + 2.0e-5,
				"converged off-center resting contact does not add kinetic energy");
			const double tangentLength = TangentImpulseLength(constraint);
			Expect(std::abs(tangentLength - 0.25 * normalImpulse) < 2.0e-6,
				"off-center sliding contact remains on its accumulated Coulomb disk");
			Vec3f u, v; Math::GetOrtho(constraint.m_Normal, u, v);
			const Vec3f tangentImpulseOnBody = (swapped ? 1.0f : -1.0f) *
				(u * constraint.m_CachedLambda[1] + v * constraint.m_CachedLambda[2]);
			const Vec3f slip(contactVelocity.x, 0, contactVelocity.z);
			Expect(glm::length(glm::cross(tangentImpulseOnBody, slip)) < 2.0e-5f &&
				glm::dot(tangentImpulseOnBody, slip) <= 0,
				"anisotropic tangent effective mass converges to maximum-dissipation friction");
			if (!oblique) {
				const float expected = (12.0f / rate) / (inverseMass * (2.5f - 1.5f * 0.25f));
				Expect(Near(normalImpulse, expected, 2.0e-5f),
					"off-center sliding impulse matches the independent analytic coupled solution");
			}
		}
	}


	void TestExactBoxWorldTimestepStability(float spinLength = 0.0f, float rollingLength = 0.0f)
	{
		using namespace GEngine;
		for (const int rate : { 60, 120 }) {
			std::vector<std::unique_ptr<PhysicalShape>> shapes;
			PhysicsSystem system;
			auto* world = new PhysicsWorld(Vec3f(0, -12, 0));
			system.SetPhysicsWorld(world);
			std::istringstream input(Phase32ExactBoxWorld);
			std::size_t count = 0; input >> count;
			Expect(count == 21, "exact exported stack has sixteen boxes and five static boundaries");
			for (std::size_t i = 0; i < count; ++i) {
				auto* body = world->CreateRigidBody3D();
				int shapeType = 0, type = 0; float radius = 0; std::size_t pointsCount = 0;
				input >> shapeType >> type >> body->m_InvMass >> body->m_Elasticity >> body->m_Friction >>
					body->m_CollisionLayer >> body->m_CollisionMask >>
					body->m_Position.x >> body->m_Position.y >> body->m_Position.z >>
					body->m_Orientation.w >> body->m_Orientation.x >> body->m_Orientation.y >> body->m_Orientation.z >>
					body->m_LinearVelocity.x >> body->m_LinearVelocity.y >> body->m_LinearVelocity.z >>
					body->m_AngularVelocity.x >> body->m_AngularVelocity.y >> body->m_AngularVelocity.z >>
					radius >> pointsCount;
				std::vector<Vec3f> points(pointsCount);
				for (auto& point : points) input >> point.x >> point.y >> point.z;
				Expect(input.good() && shapeType == int(ShapeType::Box) && pointsCount == 36,
					"exact export retains mesh points, creation order, poses, velocities and materials");
				body->SetSpinResistanceLength(spinLength);
				body->SetRollingResistanceLength(rollingLength);
				body->Type = BodyType(type);
				shapes.push_back(std::make_unique<ShapeBox>(points));
				body->m_Shape = shapes.back().get();
			}
			struct Window {
				int start, end, samples = 0;
				float peakV = 0, peakW = 0, depth = 0, excursion = 0, movement = 0;
				double minAverageY = std::numeric_limits<double>::max(), finalAverageY = 0;
				int minManifolds = 1000, maxManifolds = 0, minContacts = 1000, maxContacts = 0;
				std::array<Vec3f, 21> minimum{}, maximum{}, previous{};
			};
			std::array<Window, 2> windows{ Window{5 * rate, 18 * rate}, Window{15 * rate, 20 * rate} };
			bool finite = true; double peakEnergy = 864.0;
			const auto& bodies = world->GetPhysicsBodies();
			for (int tick = 1; tick <= 20 * rate; ++tick) {
				system.Update(Timestep(1.0f / rate));
				double energy = 0, averageY = 0; int dynamicCount = 0;
				for (auto* body : bodies) {
					finite = finite && body->HasFiniteState();
					if (body->GetInverseMass() <= 0) continue;
					const double mass = 1 / body->GetInverseMass();
					energy += 0.5 * mass * glm::dot(body->m_LinearVelocity, body->m_LinearVelocity) +
						0.5 * glm::dot(body->m_AngularVelocity,
							glm::inverse(body->GetInverseInertiaTensorWorldSpace()) * body->m_AngularVelocity) +
						12 * mass * body->GetCenterOfMassWorldSpace().y;
					averageY += body->m_Position.y; ++dynamicCount;
				}
				finite = finite && dynamicCount == 16 && std::isfinite(energy);
				averageY /= 16; peakEnergy = std::max(peakEnergy, energy);
				auto& manifolds = GetManifolds(system);
				float depth = 0;
				for (auto& manifold : manifolds.m_Manifolds)
					for (int i = 0; i < manifold.GetNumContacts(); ++i) {
						const auto c = manifold.GetContact(i);
						depth = std::max(depth, -glm::dot(
							c.m_BodyA->BodySpaceToWorldSpace(c.ptOnA_LocalSpace) -
							c.m_BodyB->BodySpaceToWorldSpace(c.ptOnB_LocalSpace), c.normal));
					}
				for (auto& window : windows) {
					if (tick < window.start || tick > window.end) continue;
					for (std::size_t i = 0; i < bodies.size(); ++i) {
						const auto* body = bodies[i]; if (body->GetInverseMass() <= 0) continue;
						const auto position = body->m_Position;
						if (!window.samples) window.minimum[i] = window.maximum[i] = position;
						else window.movement = std::max(window.movement, glm::length(position - window.previous[i]));
						window.minimum[i] = glm::min(window.minimum[i], position);
						window.maximum[i] = glm::max(window.maximum[i], position);
						window.previous[i] = position;
						window.excursion = std::max(window.excursion, glm::length(window.maximum[i] - window.minimum[i]));
						window.peakV = std::max(window.peakV, glm::length(body->m_LinearVelocity));
						window.peakW = std::max(window.peakW, glm::length(body->m_AngularVelocity));
					}
					window.depth = std::max(window.depth, depth);
					window.minAverageY = std::min(window.minAverageY, averageY);
					window.finalAverageY = averageY;
					const int manifoldCount = int(manifolds.m_Manifolds.size()), contacts = manifolds.GetContactCount();
					window.minManifolds = std::min(window.minManifolds, manifoldCount);
					window.maxManifolds = std::max(window.maxManifolds, manifoldCount);
					window.minContacts = std::min(window.minContacts, contacts);
					window.maxContacts = std::max(window.maxContacts, contacts);
					++window.samples;
				}
			}
			Expect(finite && peakEnergy <= 864.0 * 1.005,
				"exact 60/120 Hz stack stays finite without excess mechanical energy");
			for (const auto& window : windows) {
				Expect(window.samples == window.end - window.start + 1 && window.peakV <= 0.05f &&
					window.peakW <= 0.02f && window.depth <= 0.035f && window.excursion <= 0.05f &&
					window.minAverageY >= 4.45,
					"exact exported stack meets unchanged resting-motion, penetration, excursion and height gates");
				std::cout << "EXACT_STACK rate=" << rate << " passes=" << system.GetSolverIterations()
					<< " window=" << window.start / rate << "-" << window.end / rate
					<< " peak_v=" << window.peakV << " peak_w=" << window.peakW << " depth=" << window.depth
					<< " excursion=" << window.excursion << " movement=" << window.movement
					<< " min_average_y=" << window.minAverageY << " final_average_y=" << window.finalAverageY
					<< " manifolds=" << window.minManifolds << "-" << window.maxManifolds
					<< " contacts=" << window.minContacts << "-" << window.maxContacts
					<< " peak_energy=" << peakEnergy << " finite=" << finite << '\n';
			}
		}
	}

	int RunTimestepStabilityRegression()
	{
		TestExactBoxWorldTimestepStability();
		std::cout << "Timestep stability: " << testCount << " checks, " << failureCount << " failures\n";
		return failureCount ? 1 : 0;
	}

	int RunContactConvergenceRegression()
	{
		TestManifoldNormalConvergence();
		TestContactFrictionConvergence();
		std::cout << "Contact convergence: " << testCount << " checks, " << failureCount << " failures\n";
		return failureCount ? 1 : 0;
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

	// Compare the same live object's bytes, including private cache flags and revisions.
	// No copied object's padding is compared, and no bytes are written back to the body.
	struct PredictionBodySnapshot
	{
		explicit PredictionBodySnapshot(const GEngine::RigidBody3D& body) : source(body)
		{
			std::memcpy(bytes.data(), &source, bytes.size());
		}
		bool Unchanged() const { return std::memcmp(bytes.data(), &source, bytes.size()) == 0; }
		const GEngine::RigidBody3D& source;
		std::array<unsigned char, sizeof(GEngine::RigidBody3D)> bytes;
	};

	class ObservedPredictionBox final : public GEngine::ShapeBox
	{
	public:
		using ShapeBox::ShapeBox;
		const PredictionBodySnapshot* liveA{};
		const PredictionBodySnapshot* liveB{};
		mutable bool liveUnchanged = true;
		mutable bool sawAdvancedPose = false;
		GEngine::Vec3f Support(const GEngine::Vec3f& direction, const GEngine::Vec3f& position,
			const GEngine::Quat& orientation, float bias) const override
		{
			if (liveA && liveB)
			{
				liveUnchanged = liveUnchanged && liveA->Unchanged() && liveB->Unchanged();
				sawAdvancedPose = sawAdvancedPose || !Near(position, liveA->source.m_Position, 0.0f);
			}
			return ShapeBox::Support(direction, position, orientation, bias);
		}
	};

	bool SamePredictionContact(const GEngine::contact_t& a, const GEngine::contact_t& b)
	{
		return a.m_BodyA == b.m_BodyA && a.m_BodyB == b.m_BodyB &&
			a.featureA == b.featureA && a.featureB == b.featureB &&
			Near(a.ptOnA_WorldSpace, b.ptOnA_WorldSpace, 0.0f) &&
			Near(a.ptOnB_WorldSpace, b.ptOnB_WorldSpace, 0.0f) &&
			Near(a.ptOnA_LocalSpace, b.ptOnA_LocalSpace, 0.0f) &&
			Near(a.ptOnB_LocalSpace, b.ptOnB_LocalSpace, 0.0f) &&
			Near(a.normal, b.normal, 0.0f) && Near(a.separationDistance, b.separationDistance, 0.0f) &&
			Near(a.timeOfImpact, b.timeOfImpact, 0.0f);
	}

	void TestCollisionPredictionPurity()
	{
		using namespace GEngine;
		auto points = BoxPoints(Vec3f(0.5f, 0.8f, 1.2f));
		for (auto& point : points) point += Vec3f(0.2f, 0.1f, -0.15f);
		ObservedPredictionBox box(points);
		ShapeConvex convex(points);
		ShapeBox wall(BoxPoints(Vec3f(1.0f, 10.0f, 10.0f)));
		ShapeSphere sphere(1.0f);
		for (int shape = 0; shape < 3; ++shape)
		for (int cache = 0; cache < 3; ++cache)
		for (int scenario = 0; scenario < 4; ++scenario)
		for (int entry = 0; entry < 3; ++entry)
		{
			RigidBody3D a, b;
			a.m_Shape = shape == 0 ? static_cast<PhysicalShape*>(&sphere) :
				shape == 1 ? static_cast<PhysicalShape*>(&box) : static_cast<PhysicalShape*>(&convex);
			b.m_Shape = shape == 0 ? static_cast<PhysicalShape*>(&sphere) : static_cast<PhysicalShape*>(&wall);
			a.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic, 0.5f);
			b.m_Position = Vec3f(scenario == 0 ? 1.0f : 4.0f, 0, 0);
			a.m_LinearVelocity = Vec3f(scenario == 2 ? -10.0f : 10.0f, 0, 0);
			a.m_AngularVelocity = scenario == 2 ? Vec3f(0) : Vec3f(0.7f, 1.1f, 1.6f);
			a.m_Orientation = glm::angleAxis(0.2f, Math::NormalizeOr(Vec3f(1, 2, 3)));
			if (cache != 0)
			{
				for (RigidBody3D* body : { &a, &b })
				{
					body->GetCenterOfMassWorldSpace();
					body->GetInverseInertiaTensorWorldSpace();
					body->GetWorldBounds();
				}
				if (cache == 2)
				{
					a.m_Position.y += 0.01f;
					a.m_Orientation = glm::angleAxis(0.3f, Math::NormalizeOr(Vec3f(1, 2, 3)));
					a.m_InvMass = 0.75f;
				}
			}
			const PredictionBodySnapshot beforeA(a), beforeB(b);
			box.liveA = &beforeA;
			box.liveB = &beforeB;
			box.liveUnchanged = true;
			box.sawAdvancedPose = false;
			const float dt = scenario == 3 ? 0.19f : 0.5f;
			auto query = [&](contact_t& contact)
			{
				if (entry == 0) return Collision::Intersect(&a, &b, dt, contact);
				if (entry == 1) return shape == 0 ? Collision::SphereSphereIntersect(&a, &b, dt, contact) :
					Collision::ConservativeAdvance(&a, &b, dt, contact);
				return Collision::Intersect(&a, &b, contact);
			};
			contact_t first{};
			first.featureA = first.featureB = 123;
			const bool hit = query(first);
			// Rotating generic approaches can exhaust the existing CA/GJK convergence policy.
			// Exercise their output and purity without imposing a new collision acceptance rule.
			const bool rotatingApproach = shape != 0 && scenario == 1 && entry != 2;
			if (!rotatingApproach) Expect(hit == (scenario == 0 || (scenario == 1 && entry != 2)),
				"prediction fixtures distinguish overlap, analytic future hit, separating miss, and short-horizon miss");
			if (rotatingApproach && cache == 0 && entry == 0)
				std::cout << "ROTATING_PREDICTION shape=" << shape << " hit=" << hit
					<< " separation=" << first.separationDistance << '\n';
			bool repeatable = true;
			bool preserved = beforeA.Unchanged() && beforeB.Unchanged();
			for (int repeat = 0; repeat < 32; ++repeat)
			{
				contact_t again{};
				again.featureA = again.featureB = 123;
				const bool againHit = query(again);
				repeatable = repeatable && againHit == hit && SamePredictionContact(first, again);
				preserved = preserved && beforeA.Unchanged() && beforeB.Unchanged();
			}
			Expect(preserved && box.liveUnchanged,
				"queries preserve live pose, velocities, identity and cold/warm/stale caches bitwise, even during support");
			Expect(repeatable && Finite(first) && first.m_BodyA == &a && first.m_BodyB == &b &&
				first.featureA == 0 && first.featureB == 0,
				"repeated hit/miss output is exact, finite, unfeatured and references original bodies");
			if (shape == 1 && entry != 2 && scenario == 3)
				Expect(box.sawAdvancedPose, "short-horizon generic miss exercises an advanced local pose before returning");
			box.liveA = box.liveB = nullptr;
		}
	}

	void TestPredictionAnchorsAndOrder()
	{
		using namespace GEngine;
		ShapeSphere sphere(1.0f);
		for (const auto type : { Component::BodyType::Static, Component::BodyType::Kinematic,
			Component::BodyType::Dynamic })
		{
			RigidBody3D a, b;
			a.m_Shape = b.m_Shape = &sphere;
			a.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic, 1);
			b.SetBodyTypeAndInverseMass(type, 1);
			b.m_Position = Vec3f(5, 0, 0);
			a.m_LinearVelocity = Vec3f(8, 0, 0);
			b.m_LinearVelocity = Vec3f(-2, 0, 0);
			a.m_AngularVelocity = Vec3f(0.7f, 1.1f, 1.6f);
			b.m_AngularVelocity = Vec3f(-1, 2, -0.5f);
			contact_t ab{}, ba{};
			const bool hit = Collision::Intersect(&a, &b, 0.6f, ab);
			const bool reversedHit = Collision::Intersect(&b, &a, 0.6f, ba);
			const float toi = type == Component::BodyType::Static ? 3.0f / 8.0f : 0.3f;
			Expect(hit && reversedHit && Near(ab.timeOfImpact, toi, 2.0e-6f) &&
				Near(ba.timeOfImpact, toi, 2.0e-6f) && Near(ab.separationDistance, 3.0f, 2.0e-6f) &&
				Near(ab.normal, Vec3f(-1, 0, 0)) && Near(ba.normal, -ab.normal) &&
				Near(ab.ptOnA_WorldSpace, ba.ptOnB_WorldSpace) && Near(ab.ptOnB_WorldSpace, ba.ptOnA_WorldSpace),
				"spinning swept spheres preserve analytic TOI, initial separation, body-type motion and A/B convention");
			// Independent transform formula for spherical inertia and constant angular velocity.
			for (int bodyIndex = 0; bodyIndex < 2; ++bodyIndex)
			{
				const auto& body = bodyIndex == 0 ? a : b;
				const Vec3f omega = body.GetAngularVelocity();
				const float angle = glm::length(omega) * toi;
				const Quat orientation = angle == 0 ? body.m_Orientation :
					glm::angleAxis(angle, glm::normalize(omega)) * body.m_Orientation;
				const Vec3f local = bodyIndex == 0 ? ab.ptOnA_LocalSpace : ab.ptOnB_LocalSpace;
				const Vec3f world = bodyIndex == 0 ? ab.ptOnA_WorldSpace : ab.ptOnB_WorldSpace;
				Expect(Near(body.m_Position + body.GetLinearVelocity() * toi + glm::toMat3(orientation) * local,
					world, 2.0e-5f), "predicted sphere local anchors reconstruct impact witnesses independently");
			}
		}
		// Successful generic sweeps with an independent constant-pose/linear-motion reference.
		ShapeBox unitBox(UnitBoxPoints());
		ShapeConvex unitConvex(UnitBoxPoints());
		for (PhysicalShape* shape : { static_cast<PhysicalShape*>(&unitBox), static_cast<PhysicalShape*>(&unitConvex) })
		for (bool reversed : { false, true })
		{
			RigidBody3D a, b;
			a.m_Shape = shape;
			b.m_Shape = &unitBox;
			a.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic, 1);
			b.SetBodyTypeAndInverseMass(Component::BodyType::Kinematic, 0);
			const Quat orientation = glm::angleAxis(0.37f, Math::NormalizeOr(Vec3f(1, 2, 3)));
			const Vec3f axis = orientation * Vec3f(1, 0, 0);
			a.m_Orientation = b.m_Orientation = orientation;
			b.m_Position = axis * 4.0f;
			a.m_LinearVelocity = axis * 3.0f;
			b.m_LinearVelocity = -axis;
			const PredictionBodySnapshot beforeA(a), beforeB(b);
			contact_t contact{};
			Expect(Collision::ConservativeAdvance(reversed ? &b : &a, reversed ? &a : &b, 0.6f, contact) &&
				Finite(contact) && Near(contact.timeOfImpact, 0.5f, 2.0e-3f) &&
				Near(contact.normal, reversed ? axis : -axis, 2.0e-3f) && beforeA.Unchanged() && beforeB.Unchanged(),
				"generic box/convex sweeps preserve analytic impact time, normal and live state in both input orders");
			for (int index = 0; index < 2; ++index)
			{
				const RigidBody3D* body = index == 0 ? contact.m_BodyA : contact.m_BodyB;
				const Vec3f local = index == 0 ? contact.ptOnA_LocalSpace : contact.ptOnB_LocalSpace;
				const Vec3f witness = index == 0 ? contact.ptOnA_WorldSpace : contact.ptOnB_WorldSpace;
				Expect((body == &a || body == &b) && Near(body->m_Position +
					body->GetLinearVelocity() * contact.timeOfImpact + glm::toMat3(orientation) *
					(local + body->m_Shape->GetCenterOfMass()), witness, 2.0e-5f),
					"generic predicted anchors reconstruct impact witnesses using the original labelled body");
			}
		}

		ShapeBox box(BoxPoints(Vec3f(1, 2, 3)));
		PhysicsWorld world;
		auto* a = world.CreateRigidBody3D();
		auto* b = world.CreateRigidBody3D();
		auto* c = world.CreateRigidBody3D();
		for (auto* body : { a, b, c })
		{
			body->m_Shape = &box;
			body->SetBodyTypeAndInverseMass(Component::BodyType::Dynamic, 1);
			body->m_AngularVelocity = Vec3f(0.7f, 1.1f, 1.6f);
		}
		b->m_Position = Vec3f(4, 0, 0);
		c->m_Position = Vec3f(-4, 0, 0);
		a->m_LinearVelocity = Vec3f(10, 0, 0);
		const PredictionBodySnapshot beforeA(*a), beforeB(*b), beforeC(*c);
		contact_t ab{}, ac{}, afterAB{}, afterAC{};
		const bool hitAB = Collision::Intersect(a, b, 0.4f, ab);
		const bool hitAC = Collision::Intersect(a, c, 0.4f, ac);
		bool identical = true;
		for (int repeat = 0; repeat < 1000; ++repeat)
		{
			const bool repeatAC = Collision::Intersect(a, c, 0.4f, afterAC);
			const bool repeatAB = Collision::Intersect(a, b, 0.4f, afterAB);
			identical = identical && repeatAB == hitAB && repeatAC == hitAC &&
				SamePredictionContact(ab, afterAB) && SamePredictionContact(ac, afterAC);
		}
		Expect(!hitAC && identical && a->GetIdentity().IsValid() &&
			beforeA.Unchanged() && beforeB.Unchanged() && beforeC.Unchanged(),
			"1000 reordered queries sharing a spinning asymmetric world body preserve exact results and live state");
		contact_t zero{};
		Expect(!Collision::ConservativeAdvance(a, b, 0.0f, zero) && beforeA.Unchanged() && beforeB.Unchanged() &&
			zero.m_BodyA == a && zero.m_BodyB == b,
			"zero-horizon conservative query preserves state and returns original body references");
	}


	struct ToiWorldFixture
	{
		GEngine::ShapeSphere sphere{ 1.0f };
		GEngine::PhysicsSystem system;
		std::array<GEngine::RigidBody3D*, 3> bodies{};
		explicit ToiWorldFixture(bool reversed)
		{
			auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f));
			system.SetPhysicsWorld(world);
			for (int i = 0; i < 3; ++i)
			{
				const int index = reversed ? 2 - i : i;
				auto* body = world->CreateRigidBody3D();
				bodies[index] = body;
				ConfigureSphereBody(*body, sphere, GEngine::Vec3f(4.0f * index, 0.0f, 0.0f));
				body->m_Friction = 0.0f;
				body->m_Elasticity = 1.0f;
				body->m_CollisionLayer = 1u << index;
			}
		}
		void CheckFiniteAndEmpty()
		{
			Expect(bodies[0]->HasFiniteState() && bodies[1]->HasFiniteState() && bodies[2]->HasFiniteState() &&
				GetTransientContacts(system).empty(), "TOI update leaves finite bodies and no retained transient events");
		}
	};

	void TestToiRescheduling()
	{
		using namespace GEngine;
		using BodyType = Component::BodyType;
		for (const bool reversed : { false, true })
		{
			// Equal masses exchange velocities: AB at .25 moves BC from 1.0 to .4.
			ToiWorldFixture fixture(reversed);
			auto& a = *fixture.bodies[0]; auto& b = *fixture.bodies[1]; auto& c = *fixture.bodies[2];
			a.m_LinearVelocity.x = 10.0f; b.m_LinearVelocity.x = 2.0f;
			a.m_CollisionMask = 2u; b.m_CollisionMask = 5u; c.m_CollisionMask = 2u;
			fixture.system.Update(1.1f);
			Expect(Near(a.m_Position, Vec3f(4.2f, 0, 0), 2.0e-5f) && Near(b.m_Position, Vec3f(6, 0, 0), 2.0e-5f) &&
				Near(c.m_Position, Vec3f(15, 0, 0), 2.0e-5f), "earlier shared-body impulse advances a pending TOI before its obsolete time");
			Expect(Near(a.m_LinearVelocity, Vec3f(2, 0, 0), 2.0e-5f) && Near(b.m_LinearVelocity, Vec3f(0), 2.0e-5f) &&
				Near(c.m_LinearVelocity, Vec3f(10, 0, 0), 2.0e-5f), "rescheduled equal-mass impacts preserve analytic momentum and energy");
			fixture.CheckFiniteAndEmpty();
		}
		for (const bool reversed : { false, true })
		for (const float duration : { 1.0f, 2.2f })
		{
			// AB slows A from 10 to 2; A reaches static C at 2.0 instead of .6.
			ToiWorldFixture fixture(reversed);
			auto& a = *fixture.bodies[0]; auto& b = *fixture.bodies[1]; auto& c = *fixture.bodies[2];
			a.m_LinearVelocity.x = 10.0f; b.m_LinearVelocity.x = 2.0f;
			c.SetBodyTypeAndInverseMass(BodyType::Static, 0.0f);
			a.m_CollisionMask = 6u; b.m_CollisionMask = 1u; c.m_CollisionMask = 1u;
			fixture.system.Update(duration);
			const float expectedX = duration < 2.0f ? 4.0f : 5.6f;
			const float expectedVelocity = duration < 2.0f ? 2.0f : -2.0f;
			Expect(Near(a.m_Position, Vec3f(expectedX, 0, 0), 3.0e-5f) &&
				Near(a.m_LinearVelocity, Vec3f(expectedVelocity, 0, 0), 2.0e-5f),
				"slowed but still closing event is deferred or discarded when beyond the remaining horizon");
			Expect(Near(b.m_Position.x, 4.5f + 10.0f * (duration - 0.25f), 3.0e-5f) &&
				Near(c.m_Position, Vec3f(8, 0, 0), 0.0f), "rescheduling integrates the whole world for exactly the requested time");
			fixture.CheckFiniteAndEmpty();
		}
	}

	void TestToiCurrentGeometry()
	{
		using namespace GEngine;
		using BodyType = Component::BodyType;
		for (const bool reversed : { false, true })
		for (const bool secondHit : { false, true })
		{
			ToiWorldFixture fixture(reversed);
			auto& a = *fixture.bodies[0]; auto& b = *fixture.bodies[1]; auto& c = *fixture.bodies[2];
			a.m_LinearVelocity = Vec3f(8, 0, 0);
			b.m_Position = Vec3f(3, 1.2f, 0);
			c.m_Position = secondHit ? Vec3f(5, -2, 0) : Vec3f(7, 0, 0);
			b.SetBodyTypeAndInverseMass(BodyType::Static, 0.0f);
			c.SetBodyTypeAndInverseMass(BodyType::Static, 0.0f);
			a.m_Elasticity = 0.0f;
			// Analytic first inelastic impact at .175: v' = v - dot(v,n)n, n=(-.8,-.6,0).
			Vec3f expectedPosition(1.4f, 0, 0), expectedVelocity(2.88f, -3.84f, 0);
			float remaining = 0.825f;
			if (secondHit)
			{
				// Independent ray/sphere root and frictionless impulse on the changed trajectory.
				const Vec3f delta = c.m_Position - expectedPosition;
				const float speedSquared = glm::dot(expectedVelocity, expectedVelocity);
				const float projection = glm::dot(delta, expectedVelocity);
				const float discriminant = projection * projection - speedSquared * (glm::dot(delta, delta) - 4.0f);
				Expect(discriminant > 0.0f, "deflected second-impact fixture has a real analytic root");
				const float time = (projection - std::sqrt(discriminant)) / speedSquared;
				expectedPosition += expectedVelocity * time;
				const Vec3f normal = (expectedPosition - c.m_Position) * 0.5f;
				expectedVelocity -= glm::dot(expectedVelocity, normal) * normal;
				remaining -= time;
			}
			expectedPosition += expectedVelocity * remaining;
			fixture.system.Update(1.0f);
			Expect(Near(a.m_Position, expectedPosition, 1.0e-4f) && Near(a.m_LinearVelocity, expectedVelocity, 1.0e-4f),
				secondHit ? "deflected trajectory refreshes the second impact time, normal, and surface anchors" :
				"deflection discards a geometrically stale event even while closing along its old normal");
			Expect(Near(a.m_AngularVelocity, Vec3f(0), 2.0e-5f), "fresh frictionless sphere anchors produce no artificial torque");
			fixture.CheckFiniteAndEmpty();
		}
	}

	void TestToiAfterRestingSolveAndSimultaneous()
	{
		using namespace GEngine;
		using BodyType = Component::BodyType;
		for (const bool reversed : { false, true })
		{
			ToiWorldFixture fixture(reversed);
			auto& a = *fixture.bodies[0]; auto& b = *fixture.bodies[1]; auto& c = *fixture.bodies[2];
			b.m_Position.x = 2.0f;
			a.m_LinearVelocity.x = 8.0f;
			a.m_Elasticity = b.m_Elasticity = 0.0f;
			c.SetBodyTypeAndInverseMass(BodyType::Static, 0.0f);
			a.m_CollisionMask = 6u; b.m_CollisionMask = 1u; c.m_CollisionMask = 1u;
			fixture.system.Update(1.0f);
			Expect(Near(a.m_LinearVelocity, Vec3f(4, 0, 0), 2.0e-5f) && Near(a.m_Position, Vec3f(4, 0, 0), 2.0e-5f) &&
				Near(b.m_Position, Vec3f(6, 0, 0), 2.0e-5f), "resting solver velocity changes invalidate initial positive-TOI predictions");
			fixture.CheckFiniteAndEmpty();
		}
		std::array<Vec3f, 3> reference{};
		for (int repetition = 0; repetition < 8; ++repetition)
		{
			ToiWorldFixture fixture(false);
			auto& a = *fixture.bodies[0]; auto& b = *fixture.bodies[1]; auto& c = *fixture.bodies[2];
			a.m_LinearVelocity.x = 8.0f; b.m_LinearVelocity.x = 4.0f;
			a.m_CollisionMask = 2u; b.m_CollisionMask = 5u; c.m_CollisionMask = 2u;
			fixture.system.Update(0.5f); // Both initial events are at the end of the step.
			Expect(Near(a.m_Position.x, 4.0f) && Near(b.m_Position.x, 6.0f) && Near(c.m_Position.x, 8.0f) &&
				Near(a.m_LinearVelocity.x + b.m_LinearVelocity.x + c.m_LinearVelocity.x, 12.0f) &&
				Near(glm::length2(a.m_LinearVelocity) + glm::length2(b.m_LinearVelocity) + glm::length2(c.m_LinearVelocity), 80.0f),
				"simultaneous end-of-step events revalidate with zero time remaining and conserve elastic invariants");
			for (int i = 0; i < 3; ++i)
			{
				if (repetition == 0) reference[i] = fixture.bodies[i]->m_LinearVelocity;
				Expect(Near(reference[i], fixture.bodies[i]->m_LinearVelocity, 0.0f), "fixed simultaneous event order is repeatable");
			}
			fixture.CheckFiniteAndEmpty();
		}
	}


	void TestToiMissCanBeRevived()
	{
		using namespace GEngine;
		using BodyType = Component::BodyType;
		for (const bool reversed : { false, true })
		{
			ToiWorldFixture fixture(reversed);
			auto& a = *fixture.bodies[0]; auto& b = *fixture.bodies[1]; auto& c = *fixture.bodies[2];
			auto* driver = fixture.system.GetPhysicsWorld()->CreateRigidBody3D();
			ConfigureSphereBody(*driver, fixture.sphere, Vec3f(14, 0, 0));
			driver->SetBodyTypeAndInverseMass(BodyType::Kinematic, 0.0f);
			driver->m_LinearVelocity.x = -10.0f;
			driver->m_Elasticity = 1.0f; driver->m_Friction = 0.0f;
			driver->m_CollisionLayer = 8u; driver->m_CollisionMask = 4u;
			a.m_CollisionMask = 6u; b.m_CollisionMask = 1u; c.m_CollisionMask = 9u;
			a.m_LinearVelocity.x = 10.0f; b.m_LinearVelocity.x = 2.0f;
			// AB at .25 temporarily removes AC from the horizon; CD at .4 makes C
			// approach at -20, reviving AC at 6/11. Equal masses then exchange velocities.
			fixture.system.Update(0.7f);
			Expect(Near(a.m_Position, Vec3f(0), 3.0e-5f) && Near(a.m_LinearVelocity, Vec3f(-20, 0, 0), 3.0e-5f) &&
				Near(c.m_Position, Vec3f(5.4f, 0, 0), 3.0e-5f) && Near(c.m_LinearVelocity, Vec3f(2, 0, 0), 3.0e-5f),
				"another shared-body impulse can revive a pending pair that temporarily missed the horizon");
			Expect(driver->HasFiniteState() && Near(driver->m_Position, Vec3f(7, 0, 0), 2.0e-5f) &&
				Near(driver->m_LinearVelocity, Vec3f(-10, 0, 0), 0.0f), "TOI rescheduling preserves prescribed Kinematic motion");
			fixture.CheckFiniteAndEmpty();
		}
	}

	class ToiObservedBox final : public GEngine::ShapeBox
	{
	public:
		using ShapeBox::ShapeBox;
		const GEngine::RigidBody3D* movingBody{};
		bool failAtImpact = false;
		mutable bool queriedAtImpact = false;
		GEngine::Vec3f Support(const GEngine::Vec3f& direction, const GEngine::Vec3f& position,
			const GEngine::Quat& orientation, float bias) const override
		{
			if (movingBody && movingBody->m_Position.x > 1.9f && movingBody->m_Position.x < 2.1f)
			{
				queriedAtImpact = true;
				if (failAtImpact) return GEngine::Vec3f(std::numeric_limits<float>::quiet_NaN());
			}
			return ShapeBox::Support(direction, position, orientation, bias);
		}
	};

	void TestToiGenericRevalidationFailureSafety()
	{
		using namespace GEngine;
		using BodyType = Component::BodyType;
		for (const bool reversed : { false, true })
		for (const bool failAtImpact : { false, true })
		{
			ToiObservedBox box(UnitBoxPoints());
			ToiWorldFixture fixture(reversed);
			auto& a = *fixture.bodies[0]; auto& b = *fixture.bodies[1]; auto& c = *fixture.bodies[2];
			box.movingBody = &a; box.failAtImpact = failAtImpact;
			a.m_LinearVelocity.x = 8.0f; a.m_Elasticity = 0.0f;
			b.m_Shape = &box;
			b.SetBodyTypeAndInverseMass(BodyType::Static, 0.0f);
			c.m_CollisionMask = 0u;
			fixture.system.Update(0.5f);
			Expect(box.queriedAtImpact, "generic TOI contact geometry is queried again at the actual integrated pose");
			Expect(Near(a.m_Position, Vec3f(failAtImpact ? 4.0f : 2.0f, 0, 0), 3.0e-3f) &&
				Near(a.m_LinearVelocity, Vec3f(failAtImpact ? 8.0f : 0.0f, 0, 0), 3.0e-3f),
				failAtImpact ? "failed current GJK query discards a previously valid prediction without applying its stale impulse" :
				"successful current generic query preserves the analytic inelastic sphere-box impact");
			fixture.CheckFiniteAndEmpty();
		}
	}

	int RunToiRevalidationRegression()
	{
		TestToiMissCanBeRevived();
		TestToiGenericRevalidationFailureSafety();
		TestToiRescheduling();
		TestToiCurrentGeometry();
		TestToiAfterRestingSolveAndSimultaneous();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " TOI-revalidation checks failed\n";
			return 1;
		}
		std::cout << "TOI-revalidation regression: " << testCount << " checks passed\n";
		return 0;
	}

	int RunPurePredictionRegression()
	{
		TestCollisionPredictionPurity();
		TestPredictionAnchorsAndOrder();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " pure-prediction checks failed\n";
			return 1;
		}
		std::cout << "Pure-prediction regression: " << testCount << " checks passed\n";
		return 0;
	}

	void TestSleepSettingsAndTimer()
	{
		using Body = GEngine::RigidBody3D;
		using GEngine::Component::BodyType;
		Body body;
		Expect(!body.IsSleeping() && body.GetInactiveSeconds() == 0.0 && !body.CanSleep(),
			"new body starts awake without accumulated inactivity");
		Expect(body.GetSleepSettings() == Body::SleepSettings{}, "new body has the documented default sleep settings");
		body.SetBodyTypeAndInverseMass(BodyType::Dynamic, 1.0f);
		for (int step = 0; step < 3; ++step) body.UpdateSleepTimer(0.125);
		Expect(body.GetInactiveSeconds() == 0.375 && !body.TrySleep(), "sleep requires the complete dwell, not one quiet sample");
		body.UpdateSleepTimer(0.125);
		Expect(body.CanSleep() && !body.IsSleeping(), "reaching the dwell only qualifies the body; sleeping requires an explicit decision");
		Expect(body.TrySleep() && body.TrySleep() && body.IsSleeping(), "explicit sleep transition is idempotent");
		for (const double dt : { 0.0, -0.125, std::numeric_limits<double>::infinity(),
			-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
		{
			body.UpdateSleepTimer(dt);
			Expect(body.IsSleeping() && body.GetInactiveSeconds() == 0.5, "invalid or paused elapsed time does not change sleep bookkeeping");
		}
		const auto settings = body.GetSleepSettings();
		for (int field = 0; field < 3; ++field)
		{
			for (const double value : { -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
			{
				auto invalid = settings;
				if (field == 0) invalid.linearSpeedThreshold = float(value);
				if (field == 1) invalid.angularSpeedThreshold = float(value);
				if (field == 2) invalid.inactivitySeconds = value;
				Expect(!body.SetSleepSettings(invalid) && body.GetSleepSettings() == settings &&
					body.IsSleeping() && body.GetInactiveSeconds() == 0.5, "invalid sleep settings leave policy, flag and timer unchanged");
			}
		}
		auto changed = settings;
		changed.inactivitySeconds = 0.0;
		Expect(!body.SetSleepSettings(changed) && body.IsSleeping(), "zero dwell is rejected without waking a body");
		Expect(body.SetSleepSettings(settings) && body.IsSleeping() && body.GetInactiveSeconds() == 0.5,
			"reapplying the same sleep policy preserves accumulated inactivity");
		for (int field = 0; field < 3; ++field)
		{
			changed = body.GetSleepSettings();
			if (field == 0) changed.linearSpeedThreshold *= 2.0f;
			if (field == 1) changed.angularSpeedThreshold *= 2.0f;
			if (field == 2) changed.inactivitySeconds *= 2.0;
			Expect(body.SetSleepSettings(changed) && !body.IsSleeping() && body.GetInactiveSeconds() == 0.0,
				"changing any valid threshold or dwell invalidates previous qualification");
			body.UpdateSleepTimer(changed.inactivitySeconds);
			Expect(body.TrySleep(), "new policy can qualify after a fresh complete dwell");
		}
		body.WakeUp();
		body.WakeUp();
		Expect(!body.IsSleeping() && !body.CanSleep() && body.GetInactiveSeconds() == 0.0, "explicit wake is idempotent and resets the full dwell");
		changed.inactivitySeconds = std::numeric_limits<double>::max();
		Expect(body.SetSleepSettings(changed), "maximum finite dwell is accepted");
		body.UpdateSleepTimer(changed.inactivitySeconds * 0.75);
		body.UpdateSleepTimer(changed.inactivitySeconds * 0.75);
		body.UpdateSleepTimer(changed.inactivitySeconds);
		Expect(body.GetInactiveSeconds() == changed.inactivitySeconds && body.TrySleep(), "timer saturates without overflow even at the maximum finite dwell");

		for (const int hz : { 60, 120 })
		{
			body.SetSleepSettings(settings);
			body.WakeUp();
			const double dt = 1.0 / hz;
			int ticks = 0;
			while (!body.CanSleep() && ticks <= hz)
			{
				body.UpdateSleepTimer(dt);
				++ticks;
			}
			Expect(ticks >= hz / 2 && ticks <= hz / 2 + 1 && body.GetInactiveSeconds() == 0.5 && !body.IsSleeping(),
				"60/120 Hz inactivity uses supplied simulation seconds, with at most one conservative boundary sample");
			body.UpdateSleepTimer(std::numeric_limits<double>::max());
			Expect(body.GetInactiveSeconds() == 0.5, "large finite elapsed time cannot exceed the configured dwell");
		}
	}

	void TestSleepMotionAndEligibility()
	{
		using Body = GEngine::RigidBody3D;
		using GEngine::Component::BodyType;
		Body body;
		body.SetBodyTypeAndInverseMass(BodyType::Dynamic, 1.0f);
		body.SetSleepSettings({ 0.25f, 0.125f, 0.5 });
		for (int angular = 0; angular < 2; ++angular)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				for (const float sign : { -1.0f, 1.0f })
				{
					body.m_LinearVelocity = body.m_AngularVelocity = GEngine::Vec3f(0.0f);
					auto& velocity = angular ? body.m_AngularVelocity : body.m_LinearVelocity;
					const float limit = angular ? 0.125f : 0.25f;
					velocity[axis] = sign * limit;
					body.UpdateSleepTimer(0.5);
					Expect(body.TrySleep(), "signed linear/angular threshold equality qualifies on every axis");
					velocity[axis] = sign * std::nextafter(limit, std::numeric_limits<float>::infinity());
					Expect(!body.CanSleep(), "readiness rechecks current motion even after legacy public writes");
					body.UpdateSleepTimer(0.125);
					Expect(!body.IsSleeping() && body.GetInactiveSeconds() == 0.0, "exceeding either speed threshold wakes and resets inactivity");
				}
			}
			body.m_LinearVelocity = body.m_AngularVelocity = GEngine::Vec3f(0.0f);
			(angular ? body.m_AngularVelocity : body.m_LinearVelocity) = GEngine::Vec3f(angular ? 0.1f : 0.2f);
			body.UpdateSleepTimer(0.5);
			Expect(!body.CanSleep() && body.GetInactiveSeconds() == 0.0, "sleep uses vector speed rather than independent component thresholds");
		}
		body.m_LinearVelocity = body.m_AngularVelocity = GEngine::Vec3f(0.0f);
		body.UpdateSleepTimer(0.5);
		body.m_LinearVelocity.x = 1.0f;
		Expect(!body.TrySleep() && body.GetInactiveSeconds() == 0.0, "explicit sleep rejects stale qualification without needing another timer sample");
		body.m_LinearVelocity.x = 0.0f;
		body.UpdateSleepTimer(0.125);
		Expect(!body.CanSleep(), "motion interruption requires a fresh full dwell");

		const float large = std::numeric_limits<float>::max();
		body.SetSleepSettings({ large, large, 0.5 });
		body.m_LinearVelocity = GEngine::Vec3f(large, 0.0f, 0.0f);
		body.m_AngularVelocity = body.m_LinearVelocity;
		body.UpdateSleepTimer(0.5);
		Expect(body.TrySleep(), "large finite equal vector magnitudes do not overflow the threshold test");
		body.m_LinearVelocity.y = large;
		Expect(!body.TrySleep(), "large finite diagonal motion cannot pass through infinity comparison");
		body.m_LinearVelocity = GEngine::Vec3f(0.0f);
		body.m_AngularVelocity.y = large;
		body.UpdateSleepTimer(0.5);
		Expect(!body.CanSleep(), "angular magnitude has the same overflow guard");
		body.m_AngularVelocity = GEngine::Vec3f(0.0f);
		Expect(body.SetSleepSettings({ 0.0f, 0.0f, 0.5 }), "zero speed thresholds allow an exact-rest policy");
		body.UpdateSleepTimer(0.5);
		Expect(body.TrySleep(), "exact rest qualifies under zero speed thresholds");
		body.m_LinearVelocity.x = std::numeric_limits<float>::min();
		Expect(!body.TrySleep(), "tiny nonzero motion is not rounded to rest by float-square underflow");
		body.m_LinearVelocity.x = 0.0f;
		for (const auto type : { BodyType::Static, BodyType::Kinematic, static_cast<BodyType>(-1) })
		{
			body.Type = type;
			body.UpdateSleepTimer(1.0);
			Expect(!body.TrySleep() && body.GetInactiveSeconds() == 0.0, "static, kinematic and invalid body types cannot qualify for dynamic sleeping");
		}
		body.Type = BodyType::Dynamic;
		for (const float invalid : { 0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
		{
			body.m_InvMass = invalid;
			body.UpdateSleepTimer(1.0);
			Expect(!body.TrySleep() && body.GetInactiveSeconds() == 0.0, "invalid or zero effective inverse mass cannot accumulate inactivity");
		}
		body.m_InvMass = 1.0f;
		for (float* field : { &body.m_Position.x, &body.m_Orientation.w, &body.m_LinearVelocity.y,
			&body.m_AngularVelocity.z, &body.m_Friction, &body.m_Elasticity })
		{
			for (const float invalid : { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
			{
				const float original = *field;
				body.UpdateSleepTimer(0.5);
				Expect(body.TrySleep(), "finite body qualifies before invalid-state guard test");
				*field = invalid;
				body.UpdateSleepTimer(0.125);
				Expect(!body.IsSleeping() && !body.CanSleep() && body.GetInactiveSeconds() == 0.0,
					"non-finite physical state cannot remain sleep-qualified");
				*field = original;
			}
		}
	}

	void TestSleepPhysicalStateAndLifetime()
	{
		using GEngine::Component::BodyType;
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::RigidBody3D body;
		ConfigureSphereBody(body, sphere, GEngine::Vec3f(1.0f, 2.0f, 3.0f));
		body.m_LinearVelocity = GEngine::Vec3f(0.02f, 0.0f, 0.0f);
		body.m_AngularVelocity = GEngine::Vec3f(0.0f, 0.01f, 0.0f);
		const auto physicalState = [](const GEngine::RigidBody3D& b)
		{
			return std::array<float, 16>{ b.m_Position.x, b.m_Position.y, b.m_Position.z,
				b.m_Orientation.w, b.m_Orientation.x, b.m_Orientation.y, b.m_Orientation.z,
				b.m_LinearVelocity.x, b.m_LinearVelocity.y, b.m_LinearVelocity.z,
				b.m_AngularVelocity.x, b.m_AngularVelocity.y, b.m_AngularVelocity.z,
				b.m_InvMass, b.m_Friction, b.m_Elasticity };
		};
		const auto before = physicalState(body);
		const auto inertia = body.GetInverseInertiaTensorWorldSpace();
		const auto bounds = body.GetWorldBounds();
		body.UpdateSleepTimer(0.5);
		Expect(body.TrySleep() && physicalState(body) == before, "explicit sleep does not zero small velocities or alter physical state");
		Expect(body.GetLinearVelocity() == GEngine::Vec3f(0) && body.GetAngularVelocity() == GEngine::Vec3f(0),
			"sleeping motion accessors agree with frozen integration while retaining raw primitive state");
		body.Update(0.25f);
		Expect(physicalState(body) == before, "explicit sleeping freezes direct body integration");
		body.WakeUp();
		Expect(physicalState(body) == before && body.m_Shape == &sphere && body.Type == BodyType::Dynamic &&
			Near(body.GetInverseInertiaTensorWorldSpace(), inertia, 0.0f) &&
			Near(body.GetWorldBounds().mins, bounds.mins, 0.0f) && Near(body.GetWorldBounds().maxs, bounds.maxs, 0.0f),
			"wake preserves pose, motion, mass, materials, shape and derived data");

		GEngine::PhysicsSystem control, candidate;
		control.SetSleepingEnabled(false);
		candidate.SetSleepingEnabled(false);
		auto* controlWorld = new GEngine::PhysicsWorld();
		auto* candidateWorld = new GEngine::PhysicsWorld();
		control.SetPhysicsWorld(controlWorld);
		candidate.SetPhysicsWorld(candidateWorld);
		for (auto* world : { controlWorld, candidateWorld })
		{
			auto* floor = world->CreateRigidBody3D();
			ConfigureSphereBody(*floor, sphere, GEngine::Vec3f(0.0f));
			floor->SetBodyTypeAndInverseMass(BodyType::Static, 0.0f);
			auto* dynamic = world->CreateRigidBody3D();
			ConfigureSphereBody(*dynamic, sphere, GEngine::Vec3f(0.0f, 2.0f, 0.0f));
		}
		auto* passive = candidateWorld->GetPhysicsBodies()[1];
		passive->UpdateSleepTimer(0.5);
		Expect(passive->TrySleep(), "world fixture explicitly sets the passive flag");
		bool same = true, contactsObserved = false;
		for (int step = 0; step < 120; ++step)
		{
			control.Update(GEngine::Timestep(1.0f / 60.0f));
			candidate.Update(GEngine::Timestep(1.0f / 60.0f));
			same &= physicalState(*controlWorld->GetPhysicsBodies()[1]) == physicalState(*passive) &&
				GetManifolds(control).GetContactCount() == GetManifolds(candidate).GetContactCount();
			contactsObserved |= GetManifolds(candidate).GetContactCount() > 0;
		}
		Expect(same && contactsObserved, "disabled sleeping preserves the fully active gravity/contact reference trajectories exactly");
		Expect(!controlWorld->GetPhysicsBodies()[1]->IsSleeping() && controlWorld->GetPhysicsBodies()[1]->GetInactiveSeconds() == 0.0,
			"disabled sleeping does not accumulate inactivity or automatically sleep a quiet body");
		const auto oldIdentity = passive->GetIdentity();
		candidateWorld->RemoveRigidBody3D(passive);
		auto* replacement = candidateWorld->CreateRigidBody3D();
		Expect(!candidateWorld->IsBodyIdentityValid(oldIdentity) && replacement->GetIdentity() != oldIdentity &&
			!replacement->IsSleeping() && replacement->GetInactiveSeconds() == 0.0 &&
			replacement->GetSleepSettings() == GEngine::RigidBody3D::SleepSettings{},
			"replacement bodies have fresh sleep state despite identity-slot reuse");
		std::cout << "Phase 34 RigidBody3D size: " << sizeof(GEngine::RigidBody3D) << " bytes\n";
	}

	int RunSleepPrimitivesRegression()
	{
		TestSleepSettingsAndTimer();
		TestSleepMotionAndEligibility();
		TestSleepPhysicalStateAndLifetime();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " sleep-primitives checks failed\n";
			return 1;
		}
		std::cout << "Sleep-primitives regression: " << testCount << " checks passed\n";
		return 0;
	}

	void AddIslandEdge(GEngine::ManifoldCollector& manifolds, GEngine::RigidBody3D* a, GEngine::RigidBody3D* b)
	{
		// Synthetic retained contacts isolate graph connectivity from collision geometry.
		GEngine::contact_t contact{};
		contact.m_BodyA = a;
		contact.m_BodyB = b;
		contact.normal = GEngine::Vec3f(1, 0, 0);
		manifolds.AddContact(contact);
	}

	void TestContactIslandConnectivity()
	{
		using namespace GEngine;
		ShapeSphere sphere(1);
		PhysicsWorld world(Vec3f(0));
		std::vector<RigidBody3D*> bodies;
		for (int i = 0; i < 8; ++i)
		{
			auto* body = world.CreateRigidBody3D();
			ConfigureSphereBody(*body, sphere, Vec3f(0));
			if (i >= 6) body->SetBodyTypeAndInverseMass(i == 6 ? BodyType::Static : BodyType::Kinematic, 0);
			bodies.push_back(body);
		}
		bodies[1]->UpdateSleepTimer(1);
		Expect(bodies[1]->TrySleep(), "island fixture includes a passively sleeping dynamic body");
		ManifoldCollector manifolds;
		const std::array<std::array<int, 2>, 9> edges{{{0,1},{1,2},{2,0},{3,4},{0,6},{3,6},{2,7},{4,7},{6,7}}};
		for (auto edge : edges) AddIslandEdge(manifolds, bodies[edge[0]], bodies[edge[1]]);
		const auto id = [&](int i) { return bodies[i]->GetIdentity(); };
		const std::vector<ContactIsland> expected{
			{{id(0),id(1),id(2)}, {id(6),id(7)}, {{id(0),id(1)},{id(0),id(2)},{id(0),id(6)},{id(1),id(2)},{id(2),id(7)}}},
			{{id(3),id(4)}, {id(6),id(7)}, {{id(3),id(4)},{id(3),id(6)},{id(4),id(7)}}},
			{{id(5)}, {}, {}}
		};
		std::vector<PredictionBodySnapshot> snapshots;
		for (const auto* body : bodies) snapshots.emplace_back(*body);
		Expect(BuildContactIslands(bodies, manifolds.m_Manifolds) == expected,
			"islands match chains/cycles and isolated dynamics; shared static/kinematic boundaries do not bridge groups");
		for (int variant = 0; variant < 8; ++variant)
		{
			ManifoldCollector reordered;
			for (std::size_t i = 0; i < edges.size(); ++i)
			{
				const auto edge = edges[(i + variant) % edges.size()];
				AddIslandEdge(reordered, bodies[edge[1]], bodies[edge[0]]);
			}
			reordered.m_Manifolds.push_back(reordered.m_Manifolds.front());
			reordered.m_Manifolds.emplace_back();
			auto input = bodies;
			std::rotate(input.begin(), input.begin() + variant, input.end());
			std::reverse(input.begin(), input.end());
			Expect(BuildContactIslands(input, reordered.m_Manifolds) == expected,
				"body/manifold permutations, A/B reversal, duplicate edges and empty manifolds preserve exact ordered islands");
		}
		Expect(std::all_of(snapshots.begin(), snapshots.end(), [](const auto& snapshot) { return snapshot.Unchanged(); }),
			"graph construction preserves every live body byte, including sleep state and derived caches");
		Expect(BuildContactIslands({}, manifolds.m_Manifolds).empty(), "empty body input yields no islands");
		Expect(BuildContactIslands({bodies[6], bodies[7]}, manifolds.m_Manifolds).empty(),
			"boundary-only contacts yield no dynamic islands");
		bodies[1]->m_InvMass = 0;
		bodies[2]->m_InvMass = std::numeric_limits<float>::quiet_NaN();
		const auto zeroMass = BuildContactIslands(bodies, manifolds.m_Manifolds);
		Expect(zeroMass.size() == 3 && zeroMass[0].dynamicBodies == std::vector<RigidBodyIdentity>{id(0)} &&
			zeroMass[0].boundaryBodies == std::vector<RigidBodyIdentity>{id(1),id(2),id(6)},
			"zero/invalid effective mass uses the existing solver boundary semantics without propagating connectivity");
	}

	void TestContactIslandGraphOracle()
	{
		using namespace GEngine;
		ShapeSphere sphere(1);
		PhysicsWorld world(Vec3f(0));
		constexpr int count = 32, dynamicCount = 24;
		std::vector<RigidBody3D*> bodies;
		for (int i = 0; i < count; ++i)
		{
			auto* body = world.CreateRigidBody3D();
			ConfigureSphereBody(*body, sphere, Vec3f(0));
			if (i >= dynamicCount) body->SetBodyTypeAndInverseMass(i % 2 ? BodyType::Static : BodyType::Kinematic, 0);
			bodies.push_back(body);
		}
		std::vector<ContactIsland> reusable;
		ContactIslandScratch scratch;
		std::vector<ContactIsland> scratchOutput;
		std::uint32_t seed = 0x35C0FFEEu;
		const auto random = [&]() { seed = seed * 1664525u + 1013904223u; return seed; };
		for (int sample = 0; sample < 32; ++sample)
		{
			bool adjacent[count][count]{};
			ManifoldCollector manifolds;
			for (int a = 0; a < count; ++a) for (int b = a + 1; b < count; ++b)
			{
				if ((random() >> 24) >= static_cast<unsigned>(sample + 1)) continue;
				adjacent[a][b] = adjacent[b][a] = true;
				AddIslandEdge(manifolds, bodies[b], bodies[a]);
			}
			// Independent breadth-first graph oracle; boundaries cannot enter the queue.
			bool visited[count]{};
			std::vector<ContactIsland> expected;
			for (int start = 0; start < dynamicCount; ++start)
			{
				if (visited[start]) continue;
				std::vector<int> queue{start};
				visited[start] = true;
				for (std::size_t head = 0; head < queue.size(); ++head)
					for (int b = 0; b < dynamicCount; ++b)
						if (adjacent[queue[head]][b] && !visited[b]) { visited[b] = true; queue.push_back(b); }
				bool member[count]{};
				for (int a : queue) member[a] = true;
				ContactIsland island;
				for (int a = 0; a < dynamicCount; ++a)
					if (member[a]) island.dynamicBodies.push_back(bodies[a]->GetIdentity());
				for (int b = dynamicCount; b < count; ++b)
					if (std::any_of(queue.begin(), queue.end(), [&](int a) { return adjacent[a][b]; }))
						island.boundaryBodies.push_back(bodies[b]->GetIdentity());
				for (int a = 0; a < count; ++a) for (int b = a + 1; b < count; ++b)
					if (adjacent[a][b] && (member[a] || member[b]))
						island.contactPairs.push_back({bodies[a]->GetIdentity(), bodies[b]->GetIdentity()});
				expected.push_back(island);
			}
			Expect(BuildContactIslands(bodies, manifolds.m_Manifolds) == expected,
				"seeded sparse/dense contact graphs match independent breadth-first connectivity and complete boundary/edge coverage");
			reusable = BuildContactIslands(bodies, manifolds.m_Manifolds, std::move(reusable));
			Expect(reusable == expected, "reused island storage fully replaces prior membership, boundaries and edges");
			BuildContactIslands(bodies, manifolds.m_Manifolds, scratchOutput, scratch);
			Expect(scratchOutput == expected, "reused graph scratch matches independent connectivity oracle after topology changes");
			auto permuted = bodies;
			for (std::size_t i = permuted.size(); i > 1; --i) std::swap(permuted[i-1], permuted[random() % i]);
			std::reverse(manifolds.m_Manifolds.begin(), manifolds.m_Manifolds.end());
			Expect(BuildContactIslands(permuted, manifolds.m_Manifolds) == expected,
				"seeded graph membership and ordering are exact after input permutations");
		}
		Expect(BuildContactIslands({}, {}, std::move(reusable)).empty(), "reused output retires every island for an empty world");
	}

	void TestContactIslandStorageRebuild()
	{
		using namespace GEngine;
		ShapeSphere sphere(1);
		PhysicsWorld world(Vec3f(0));
		for (int i = 0; i < 2048; ++i)
		{
			auto* body = world.CreateRigidBody3D();
			ConfigureSphereBody(*body, sphere, Vec3f(0));
			if (i % 2) body->SetBodyTypeAndInverseMass(BodyType::Static, 0);
		}
		auto bodies = world.GetPhysicsBodies();
		ContactIslandScratch scratch;
		std::vector<ContactIsland> islands;
		BuildContactIslands(bodies, {}, islands, scratch);
		const auto singleton = islands;
		const auto* outputStorage = islands.data();
		const auto* nodeStorage = scratch.nodes.data();
		std::vector<const RigidBodyIdentity*> memberStorage;
		for (const auto& island : islands) memberStorage.push_back(island.dynamicBodies.data());
		std::reverse(bodies.begin(), bodies.end());
		BuildContactIslands(bodies, {}, islands, scratch);
		bool sameStorage = islands.data() == outputStorage && scratch.nodes.data() == nodeStorage;
		for (std::size_t i = 0; i < islands.size(); ++i)
			sameStorage = sameStorage && islands[i].dynamicBodies.data() == memberStorage[i];
		Expect(islands == singleton && islands.size() == 1024 && sameStorage,
			"large separated graphs rebuild exact ordered singleton output without replacing retained storage");
		ManifoldCollector contacts;
		const auto& live = world.GetPhysicsBodies();
		AddIslandEdge(contacts, live[0], live[2]);
		AddIslandEdge(contacts, live[0], live[1]);
		AddIslandEdge(contacts, live[2], live[1]); // Repeated boundary, distinct pair.
		contacts.m_Manifolds.push_back(contacts.m_Manifolds.front());
		BuildContactIslands(bodies, contacts.m_Manifolds, islands, scratch);
		Expect(islands.size() == 1023 && islands[0].dynamicBodies.size() == 2 &&
			islands[0].boundaryBodies == std::vector<RigidBodyIdentity>{live[1]->GetIdentity()} &&
			islands[0].contactPairs.size() == 3, "singleton storage transitions to a sparse graph with unique boundaries and pairs");
		BuildContactIslands(bodies, {}, islands, scratch);
		Expect(islands == singleton, "the no-edge path retires every former membership, boundary and contact pair");
		BuildContactIslands({}, contacts.m_Manifolds, islands, scratch);
		Expect(islands.empty(), "retained scratch cannot supply live endpoints to an empty input");
		BuildContactIslands(bodies, {}, islands, scratch);
		Expect(islands == singleton, "scratch rebuilds complete singleton membership after an empty world");

		// Non-contiguous live slots force binary lookup. Same-slot foreign bodies
		// also prevent dense indexing and must remain distinct by generation.
		PhysicsWorld foreign(Vec3f(0));
		auto* foreignA = foreign.CreateRigidBody3D();
		auto* foreignB = foreign.CreateRigidBody3D();
		ConfigureSphereBody(*foreignA, sphere, Vec3f(0));
		ConfigureSphereBody(*foreignB, sphere, Vec3f(0));
		ManifoldCollector sparse;
		AddIslandEdge(sparse, live[0], live[4]);
		AddIslandEdge(sparse, foreignA, foreignB);
		AddIslandEdge(sparse, live[0], live[2]); // Missing from the supplied live set.
		BuildContactIslands({live[4], foreignB, live[0], foreignA}, sparse.m_Manifolds, islands, scratch);
		const std::vector<ContactIsland> expected{
			{{live[0]->GetIdentity(), live[4]->GetIdentity()}, {}, {{live[0]->GetIdentity(), live[4]->GetIdentity()}}},
			{{foreignA->GetIdentity(), foreignB->GetIdentity()}, {}, {{foreignA->GetIdentity(), foreignB->GetIdentity()}}}
		};
		Expect(islands == expected, "sparse slots, absent endpoints and equal slots with different generations retain exact identity semantics");
	}

	void TestContactIslandIdentityLifetime()
	{
		using namespace GEngine;
		ShapeSphere sphere(1);
		PhysicsWorld world(Vec3f(0));
		auto* a = world.CreateRigidBody3D();
		auto* b = world.CreateRigidBody3D();
		ConfigureSphereBody(*a, sphere, Vec3f(0));
		ConfigureSphereBody(*b, sphere, Vec3f(0));
		ManifoldCollector stale;
		AddIslandEdge(stale, a, b);
		const auto oldIdentity = a->GetIdentity();
		world.RemoveRigidBody3D(a);
		auto* replacement = world.CreateRigidBody3D();
		ConfigureSphereBody(*replacement, sphere, Vec3f(0));
		Expect(replacement->GetIdentity().GetSlot() == oldIdentity.GetSlot() && replacement->GetIdentity() != oldIdentity,
			"island lifetime fixture reuses an identity slot with a new generation");
		const auto graph = BuildContactIslands(world.GetPhysicsBodies(), stale.m_Manifolds);
		Expect(graph.size() == 2 && graph[0].dynamicBodies == std::vector<RigidBodyIdentity>{replacement->GetIdentity()} &&
			graph[1].dynamicBodies == std::vector<RigidBodyIdentity>{b->GetIdentity()} && graph[0].contactPairs.empty() && graph[1].contactPairs.empty(),
			"stale manifold stamps cannot bind a reused slot; graph reads no deleted body pointer");
		PhysicsWorld foreign(Vec3f(0));
		for (int i = 0; i < 2; ++i) ConfigureSphereBody(*foreign.CreateRigidBody3D(), sphere, Vec3f(0));
		const auto foreignGraph = BuildContactIslands(foreign.GetPhysicsBodies(), stale.m_Manifolds);
		Expect(foreignGraph.size() == 2 && foreignGraph[0].contactPairs.empty() && foreignGraph[1].contactPairs.empty(),
			"foreign-world generations cannot impersonate stamped contact endpoints");
		ManifoldCollector fresh;
		AddIslandEdge(fresh, replacement, b);
		Expect(BuildContactIslands(world.GetPhysicsBodies(), fresh.m_Manifolds).size() == 1,
			"fresh contacts reconnect replacement bodies using their current full identities");
	}

	void TestContactIslandSystemLifecycle()
	{
		using namespace GEngine;
		ShapeSphere sphere(1);
		PhysicsSystem system;
		Expect(system.GetContactIslands().empty(), "new system has no island snapshot");
		auto* world = new PhysicsWorld(Vec3f(0));
		system.SetPhysicsWorld(world);
		for (float x : {0.0f, 2.0f, 4.0f, 20.0f})
			ConfigureSphereBody(*world->CreateRigidBody3D(), sphere, Vec3f(x,0,0));
		system.Update(Timestep(1.0f/120));
		const auto initial = system.GetContactIslands();
		Expect(initial.size() == 2 && initial[0].dynamicBodies.size() == 3 && initial[0].contactPairs.size() == 2 &&
			initial[1].dynamicBodies.size() == 1, "normal stepping constructs the current resting graph plus isolated dynamics");
		Expect(initial == BuildContactIslands(world->GetPhysicsBodies(), GetManifolds(system).m_Manifolds),
			"stored islands describe the manifolds retained for the solve");
		auto* middle = world->GetPhysicsBodies()[1];
		Expect(system.SetBodyPose(middle, middle->m_Position, middle->m_Orientation) && system.GetContactIslands() == initial,
			"no-op pose edits preserve the snapshot");
		Expect(!system.SetBodyPose(nullptr, Vec3f(0), Quat(1,0,0,0)) && system.GetContactIslands() == initial,
			"rejected pose edits preserve the snapshot");
		Expect(system.SetBodyPose(middle, Vec3f(10,0,0), middle->m_Orientation) && system.GetContactIslands().empty(),
			"accepted pose changes invalidate the old graph immediately");
		system.Update(Timestep(1.0f/120));
		Expect(system.GetContactIslands().size() == 4 && GetManifolds(system).m_Manifolds.empty(),
			"removing a contact bridge splits connectivity on the next update");
		middle->m_Position = Vec3f(2,0,0);
		system.Update(Timestep(1.0f/120));
		Expect(system.GetContactIslands().size() == 2, "direct pose mutation is reflected by the next complete graph rebuild");
		middle->m_Position = Vec3f(2,10,0); // Tangential drift expires both retained anchors.
		system.Update(Timestep(1.0f/120));
		Expect(system.GetContactIslands().size() == 4, "expired contacts do not remain as island edges");
		world->RemoveRigidBody3D(middle);
		Expect(system.GetContactIslands().empty(), "body removal invalidates all cached island membership before deletion");
		system.Update(Timestep(1.0f/120));
		Expect(system.GetContactIslands().size() == 3, "post-removal rebuild contains only remaining live identities");
		auto* added = world->CreateRigidBody3D();
		ConfigureSphereBody(*added, sphere, Vec3f(40,0,0));
		system.Update(Timestep(1.0f/120));
		Expect(system.GetContactIslands().size() == 4, "newly created dynamics are included on the next update");
		added->SetBodyTypeAndInverseMass(BodyType::Kinematic, 0);
		system.Update(Timestep(1.0f/120));
		Expect(system.GetContactIslands().size() == 3, "body-type changes are reflected on the next update");
		system.SetPhysicsWorld(world);
		Expect(system.GetContactIslands().size() == 3, "same-world assignment preserves the current snapshot");
		system.SetPhysicsWorld(new PhysicsWorld(Vec3f(0)));
		Expect(system.GetContactIslands().empty(), "world replacement clears prior island identities");
		system.Update(Timestep(1.0f/120));
		Expect(system.GetContactIslands().empty(), "empty worlds produce no islands");
		system.OnExit(); system.OnExit();
		system.Update(Timestep(1.0f/120));
		Expect(system.GetContactIslands().empty(), "repeated shutdown and worldless updates preserve an empty snapshot");

		// Predicted future collisions do not supply resting graph edges.
		auto* movingWorld = new PhysicsWorld(Vec3f(0));
		system.SetPhysicsWorld(movingWorld);
		auto* a = movingWorld->CreateRigidBody3D();
		auto* b = movingWorld->CreateRigidBody3D();
		ConfigureSphereBody(*a, sphere, Vec3f(0));
		ConfigureSphereBody(*b, sphere, Vec3f(3,0,0));
		a->m_LinearVelocity = Vec3f(10,0,0);
		system.Update(Timestep(0.2f));
		Expect(system.GetContactIslands().size() == 2 && system.GetContactIslands()[0].contactPairs.empty() &&
			system.GetContactIslands()[1].contactPairs.empty() && a->HasFiniteState() && b->HasFiniteState(),
			"positive-TOI response stays outside the resting island graph and remains finite");
	}


	void TestIslandSleepAndWake()
	{
		using namespace GEngine;
		Manifold empty;
		empty.Solve(); empty.PostSolve(); empty.PreSolve(1.0f/60);
		Expect(empty.GetNumContacts()==0, "sleep activity guards preserve safe no-op operations on an empty manifold");
		ShapeSphere sphere(1.0f);
		PhysicsSystem system;
		auto* world = new PhysicsWorld(Vec3f(0));
		system.SetPhysicsWorld(world);
		auto* a = world->CreateRigidBody3D();
		auto* b = world->CreateRigidBody3D();
		auto* unrelated = world->CreateRigidBody3D();
		ConfigureSphereBody(*a, sphere, Vec3f(0));
		ConfigureSphereBody(*b, sphere, Vec3f(2,0,0));
		ConfigureSphereBody(*unrelated, sphere, Vec3f(20,0,0));
		for (int tick = 0; tick < 40; ++tick) system.Update(Timestep(1.0f/60));
		Expect(a->IsSleeping() && b->IsSleeping() && unrelated->IsSleeping(),
			"quiet connected dynamics and singleton islands automatically sleep after the dwell");
		const auto position = a->m_Position;
		for (int tick = 0; tick < 20; ++tick) system.Update(Timestep(1.0f/60));
		Expect(a->IsSleeping() && a->m_Position == position && a->m_LinearVelocity == Vec3f(0),
			"sleeping islands remain exactly frozen across repeated steps");
		a->ApplyImpulseLinear(Vec3f(1,0,0));
		Expect(!a->IsSleeping(), "external nonzero impulse immediately wakes its body");
		system.Update(Timestep(1.0f/60));
		Expect(!a->IsSleeping() && !b->IsSleeping() && unrelated->IsSleeping(),
			"impulse wake reaches the connected island without waking a separated island");
		Expect(b->m_LinearVelocity.x > 0.0f, "newly woken contact participates in the same step's response");
	}


	void TestSleepWakeSourcesAndBoundaries()
	{
		using namespace GEngine;
		for (int mutation = 0; mutation < 13; ++mutation) {
			ShapeSphere sphere(1);
			PhysicsSystem system;
			auto* world = new PhysicsWorld(Vec3f(0)); system.SetPhysicsWorld(world);
			auto* a = world->CreateRigidBody3D(); auto* b = world->CreateRigidBody3D();
			auto* distant = world->CreateRigidBody3D();
			ConfigureSphereBody(*a, sphere, Vec3f(0)); ConfigureSphereBody(*b, sphere, Vec3f(2,0,0));
			ConfigureSphereBody(*distant, sphere, Vec3f(20,0,0));
			for (int tick = 0; tick < 40; ++tick) system.Update(Timestep(1.0f/60));
			Expect(a->IsSleeping() && b->IsSleeping() && distant->IsSleeping(), "mutation fixture starts with two sleeping islands");
			switch (mutation) {
			case 0: a->m_Position.y += 0.1f; break;
			case 1: a->m_Orientation = glm::angleAxis(0.1f, Vec3f(0,0,1)); break;
			case 2: a->m_LinearVelocity.x = 0.001f; break;
			case 3: a->m_AngularVelocity.z = 0.001f; break;
			case 4: a->m_InvMass = 2; break;
			case 5: a->m_Friction = 0.75f; break;
			case 6: a->m_Elasticity = 0.75f; break;
			case 7: a->m_CollisionMask = 0; break;
			case 8: a->m_CollisionLayer = 2; break;
			case 9: a->WakeUp(); break;
			case 10: a->ApplyImpulseAngular(Vec3f(0,0,0.01f)); break;
			case 11: a->ApplyImpulse(a->m_Position + Vec3f(0,1,0), Vec3f(0.01f,0,0)); break;
			case 12: system.SetBodyPose(a, Vec3f(0,0.1f,0), a->m_Orientation); break;
			}
			system.Update(Timestep(1.0f/60));
			Expect(!a->IsSleeping() && !b->IsSleeping() && distant->IsSleeping(),
				"public edits, explicit wake and impulses wake the connected island while preserving unrelated sleep");
		}
		for (int mutation = 0; mutation < 6; ++mutation) {
			ShapeSphere boundaryShape(1), sphere(1);
			PhysicsSystem system; auto* world = new PhysicsWorld(Vec3f(0)); system.SetPhysicsWorld(world);
			auto* boundary = world->CreateRigidBody3D();
			auto* a = world->CreateRigidBody3D(); auto* b = world->CreateRigidBody3D();
			ConfigureSphereBody(*boundary, boundaryShape, Vec3f(0));
			boundary->SetBodyTypeAndInverseMass(mutation == 1 ? BodyType::Kinematic : BodyType::Static, 0);
			ConfigureSphereBody(*a, sphere, Vec3f(-2,0,0)); ConfigureSphereBody(*b, sphere, Vec3f(2,0,0));
			for (int tick = 0; tick < 40; ++tick) system.Update(Timestep(1.0f/60));
			Expect(a->IsSleeping() && b->IsSleeping() && system.GetContactIslands().size() == 2,
				"stationary shared static/kinematic boundaries permit independent sleeping islands");
			if (mutation == 0) a->ApplyImpulseLinear(Vec3f(-1,0,0));
			if (mutation == 1) boundary->m_LinearVelocity = Vec3f(0,0.1f,0);
			if (mutation == 2) boundary->m_Position.y += 0.1f;
			if (mutation == 3) boundaryShape.SetRadius(1.1f);
			if (mutation == 4) system.SetBodyPose(boundary, Vec3f(0,0.1f,0), boundary->m_Orientation);
			if (mutation == 5) world->RemoveRigidBody3D(boundary);
			system.Update(Timestep(1.0f/60));
			Expect(!a->IsSleeping() && (mutation == 0 ? b->IsSleeping() : !b->IsSleeping()),
				"a boundary never transmits a dynamic wake, but its own motion, geometry edit or removal wakes all dependants");
		}
	}

	void TestSleepEligibilityAndInteraction()
	{
		using namespace GEngine;
		ShapeSphere sphere(1);
		PhysicsSystem system; auto* world = new PhysicsWorld(Vec3f(0)); system.SetPhysicsWorld(world);
		auto* a = world->CreateRigidBody3D(); auto* b = world->CreateRigidBody3D();
		ConfigureSphereBody(*a, sphere, Vec3f(0)); ConfigureSphereBody(*b, sphere, Vec3f(2,0,0));
		a->SetSleepSettings({0.05f,0.02f,0.1}); b->SetSleepSettings({0.05f,0.02f,0.75});
		for (int tick = 0; tick < 20; ++tick) system.Update(Timestep(1.0f/60));
		Expect(!a->IsSleeping() && !b->IsSleeping() && a->CanSleep(), "an island waits for every member's full dwell");
		for (int tick = 0; tick < 40; ++tick) system.Update(Timestep(1.0f/60));
		Expect(a->IsSleeping() && b->IsSleeping(), "members with different dwells transition together");
		a->ApplyImpulseLinear(Vec3f(0)); a->ApplyImpulseAngular(Vec3f(0));
		Expect(a->IsSleeping(), "zero impulses preserve sleep");
		const auto pose = a->m_Position; const auto timer = a->GetInactiveSeconds();
		system.Update(Timestep(0));
		Expect(a->IsSleeping() && a->m_Position == pose && a->GetInactiveSeconds() == timer, "paused steps preserve sleeping state and pose");
		auto* projectile = world->CreateRigidBody3D(); ConfigureSphereBody(*projectile, sphere, Vec3f(-5,0,0));
		projectile->m_LinearVelocity = Vec3f(60,0,0);
		system.Update(Timestep(0.1f));
		Expect(!a->IsSleeping() && !b->IsSleeping() && a->m_LinearVelocity.x > 0,
			"a newly created positive-TOI impact discovers and wakes the sleeping group before response");
		const auto stale = b->GetIdentity(); world->RemoveRigidBody3D(b);
		auto* fresh = world->CreateRigidBody3D(); ConfigureSphereBody(*fresh, sphere, Vec3f(400,0,0));
		Expect(!fresh->IsSleeping() && fresh->GetIdentity() != stale && !world->IsBodyIdentityValid(stale),
			"reused identity slots cannot inherit an old island's sleeping state");
		for (int tick = 0; tick < 40; ++tick) system.Update(Timestep(1.0f/60));
		Expect(fresh->IsSleeping(), "a new isolated body can independently qualify for sleep");
		world->SetGravity(Vec3f(0,-12,0)); system.Update(Timestep(1.0f/60));
		Expect(!fresh->IsSleeping() && fresh->m_LinearVelocity.y < 0, "gravity changes wake sleepers and apply gravity in the same tick");
		system.SetSleepingEnabled(false);
		for (int tick = 0; tick < 40; ++tick) system.Update(Timestep(1.0f/60));
		Expect(!fresh->IsSleeping() && fresh->GetInactiveSeconds() == 0, "disabling sleep restores active reference bookkeeping");
		system.SetPhysicsWorld(new PhysicsWorld(Vec3f(0)));
		Expect(system.GetContactIslands().empty() && !system.IsSleepingEnabled(), "world replacement clears sleep graph state and retains system policy");
		system.OnExit(); system.OnExit(); system.Update(Timestep(1.0f/60));

		PhysicsSystem penetrated; auto* overlap = new PhysicsWorld(Vec3f(0)); penetrated.SetPhysicsWorld(overlap);
		auto* fixed = overlap->CreateRigidBody3D(); auto* moving = overlap->CreateRigidBody3D();
		ConfigureSphereBody(*fixed, sphere, Vec3f(0)); fixed->SetBodyTypeAndInverseMass(BodyType::Static, 0);
		ConfigureSphereBody(*moving, sphere, Vec3f(1,0,0)); moving->SetSleepSettings({0.05f,0.02f,0.001});
		for (int tick = 0; tick < 8; ++tick) {
			const auto before = moving->m_Position; penetrated.Update(Timestep(1.0f/60));
			if (glm::length(moving->m_Position-before) > 0.05f/60)
				Expect(!moving->IsSleeping(), "ongoing positional correction cannot be hidden by zero-velocity sleeping");
		}
	}


	void TestSleepSupportAndDeterminism()
	{
		using namespace GEngine;
		{
			struct ObservedValiditySphere : ShapeSphere {
				ObservedValiditySphere() : ShapeSphere(1) {}
				mutable int queries{};
				bool IsValid() const override { ++queries; return ShapeSphere::IsValid(); }
			} shape;
			PhysicsSystem system; auto* world = new PhysicsWorld(Vec3f(0)); system.SetPhysicsWorld(world);
			auto* body = world->CreateRigidBody3D(); ConfigureSphereBody(*body, shape, Vec3f(0));
			for (int tick=0;tick<3;++tick) system.Update(Timestep(1.0f/60));
			shape.queries=0;
			for (int tick=0;tick<10;++tick) system.Update(Timestep(1.0f/60));
			Expect(shape.queries==0, "unchanged separated geometry is not repeatedly revalidated for sleep eligibility");
			shape.SetRadius(1.1f); system.Update(Timestep(1.0f/60));
			Expect(shape.queries>0 && !body->IsSleeping() && body->GetInactiveSeconds()==0,
				"geometry revision changes revalidate sleep eligibility and restart its dwell");
		}
		// A quiet body's reused eligibility must accumulate exactly like the primitive,
		// and even a below-threshold public mutation must restart its dwell.
		{
			ShapeSphere shape(1);
			PhysicsSystem system; auto* world = new PhysicsWorld(Vec3f(0)); system.SetPhysicsWorld(world);
			auto* body = world->CreateRigidBody3D(); ConfigureSphereBody(*body, shape, Vec3f(0));
			system.Update(Timestep(1.0f/60));
			RigidBody3D reference = *body;
			bool exactDwell = true;
			for (float dt : {0.001f, 0.01f, 1.0f/120, 1.0f/60, 0.02f, 0.1f}) {
				reference.UpdateSleepTimer(dt); system.Update(Timestep(dt));
				exactDwell &= body->GetInactiveSeconds() == reference.GetInactiveSeconds();
			}
			Expect(exactDwell && !body->IsSleeping(), "unchanged awake eligibility retains exact primitive dwell accumulation");
			for (int change=0;change<6;++change) {
				for (int tick=0;tick<4;++tick) system.Update(Timestep(1.0f/60));
				if (change==0) body->m_Friction += 0.001f;
				if (change==1) body->m_Position.x += 0.0001f;
				if (change==2) body->m_LinearVelocity.x = 0.001f;
				if (change==3) body->m_AngularVelocity.y = 0.001f;
				if (change==4) body->SetSleepSettings({0.04f,0.02f,0.5});
				if (change==5) body->ApplyImpulseLinear(Vec3f(0.0001f,0,0));
				system.Update(Timestep(1.0f/60));
				Expect(!body->IsSleeping() && body->GetInactiveSeconds()==0,
					"small explicit mutations invalidate a partially accumulated sleep dwell");
			}
		}
		ShapeSphere sphere(1);
		PhysicsSystem first, second;
		for (auto* system : {&first,&second}) {
			auto* world = new PhysicsWorld(Vec3f(0)); system->SetPhysicsWorld(world);
			for (int i=0;i<4;++i) ConfigureSphereBody(*world->CreateRigidBody3D(),sphere,Vec3f(i*2.0f,0,0));
		}
		bool exact = true, slept = false;
		for (int tick=0;tick<240;++tick) {
			for (auto* system : {&first,&second}) {
				auto* body = system->GetPhysicsWorld()->GetPhysicsBodies()[0];
				if(tick==60) body->ApplyImpulseLinear(Vec3f(1,0,0));
				if(tick==120) system->SetBodyPose(body,body->m_Position+Vec3f(0,1,0),body->m_Orientation);
				system->Update(Timestep(1.0f/60));
			}
			for (int i=0;i<4;++i) {
				const auto* a=first.GetPhysicsWorld()->GetPhysicsBodies()[i];
				const auto* b=second.GetPhysicsWorld()->GetPhysicsBodies()[i];
				exact &= a->m_Position==b->m_Position && a->m_Orientation==b->m_Orientation &&
					a->m_LinearVelocity==b->m_LinearVelocity && a->m_AngularVelocity==b->m_AngularVelocity &&
					a->IsSleeping()==b->IsSleeping() && a->GetInactiveSeconds()==b->GetInactiveSeconds();
				slept |= a->IsSleeping();
			}
		}
		Expect(exact && slept,"repeated creation order and wake inputs preserve exact physical, timer and sleep state");

		PhysicsSystem supported; auto* world=new PhysicsWorld(); supported.SetPhysicsWorld(world);
		auto* support=world->CreateRigidBody3D(); auto* body=world->CreateRigidBody3D();
		ConfigureSphereBody(*support,sphere,Vec3f(0)); support->SetBodyTypeAndInverseMass(BodyType::Static,0);
		ConfigureSphereBody(*body,sphere,Vec3f(0,2,0));
		for(int tick=0;tick<120;++tick) supported.Update(Timestep(1.0f/60));
		Expect(body->IsSleeping(),"gravity-supported single sphere reaches sleeping rest");
		Expect(supported.SetBodyPose(body,body->m_Position,body->m_Orientation) && body->IsSleeping(),
			"identical pose resubmission preserves sleep and support caches");
		const auto position=body->m_Position;
		world->RemoveRigidBody3D(support);
		Expect(!body->IsSleeping(),"support removal wakes its dependent body before the support is freed");
		supported.Update(Timestep(1.0f/60));
		Expect(body->m_Position.y<position.y && body->m_LinearVelocity.y<0,
			"a sleeper falls immediately after support removal");

		PhysicsSystem driven; auto* movingWorld=new PhysicsWorld(Vec3f(0)); driven.SetPhysicsWorld(movingWorld);
		auto* kinematic=movingWorld->CreateRigidBody3D(); auto* dynamic=movingWorld->CreateRigidBody3D();
		ConfigureSphereBody(*kinematic,sphere,Vec3f(0)); kinematic->SetBodyTypeAndInverseMass(BodyType::Kinematic,0);
		ConfigureSphereBody(*dynamic,sphere,Vec3f(2,0,0));
		kinematic->m_AngularVelocity=Vec3f(0.001f,0,0); // Motion about the contact normal: no tangential push.
		for(int tick=0;tick<60;++tick) driven.Update(Timestep(1.0f/60));
		Expect(!dynamic->IsSleeping() && dynamic->GetInactiveSeconds()==0,
			"even slow kinematic interaction prevents sleep and clears dwell");
		kinematic->m_AngularVelocity=Vec3f(0);
		for(int tick=0;tick<20;++tick) driven.Update(Timestep(1.0f/60));
		Expect(!dynamic->IsSleeping(),"stopping a kinematic support requires a new complete dwell");
		for(int tick=0;tick<20;++tick) driven.Update(Timestep(1.0f/60));
		Expect(dynamic->IsSleeping(),"a stopped kinematic support permits sleep after a fresh dwell");
	}

	int RunSleepIntegrationRegression()
	{
		TestIslandSleepAndWake();
		TestSleepWakeSourcesAndBoundaries();
		TestSleepEligibilityAndInteraction();
		TestSleepSupportAndDeterminism();
		if (failureCount != 0) {
			std::cerr << failureCount << " of " << testCount << " sleep-integration checks failed\n";
			return 1;
		}
		std::cout << "Sleep-integration regression: " << testCount << " checks passed\n";
		return 0;
	}

	int RunContactIslandsRegression()
	{
		TestContactIslandConnectivity();
		TestContactIslandGraphOracle();
		TestContactIslandStorageRebuild();
		TestContactIslandIdentityLifetime();
		TestContactIslandSystemLifecycle();
		if (failureCount != 0)
		{
			std::cerr << failureCount << " of " << testCount << " contact-islands checks failed\n";
			return 1;
		}
		std::cout << "Contact-islands regression: " << testCount << " checks passed\n";
		return 0;
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

	struct AngularSweptBoundsTag
	{
		using Type = std::vector<GEngine::Bounds> GEngine::SweepAndPruneBroadphase::*;
		friend Type GetPrivateMember(AngularSweptBoundsTag);
	};
	template struct PrivateMemberAccess<AngularSweptBoundsTag, &GEngine::SweepAndPruneBroadphase::m_SweptBounds>;

	bool EnclosesBounds(const GEngine::Bounds& outer, const GEngine::Bounds& inner)
	{
		for (int axis = 0; axis < 3; ++axis)
			if (outer.mins[axis] > inner.mins[axis] || outer.maxs[axis] < inner.maxs[axis]) return false;
		return true;
	}

	void TestAngularSweepContainment()
	{
		using namespace GEngine;
		using BodyType = Component::BodyType;
		for (const float scale : { 0.25f, 1.0f, 8.0f })
		{
			auto points = BoxPoints(Vec3f(4.0f, 0.5f, 0.25f) * scale);
			for (Vec3f& point : points) point += Vec3f(2, -3, 0.4f) * scale;
			ShapeBox box(points); ShapeConvex convex(points);
			for (PhysicalShape* shape : { static_cast<PhysicalShape*>(&box), static_cast<PhysicalShape*>(&convex) })
			for (const BodyType type : { BodyType::Dynamic, BodyType::Kinematic })
			for (const float angle : { 0.0f, 0.8f })
			for (const Vec3f localOmega : { Vec3f(0, 0, 30), Vec3f(6, 10, -7) })
			{
				RigidBody3D body;
				body.m_Shape = shape;
				body.SetBodyTypeAndInverseMass(type, 1.0f);
				body.m_Position = Vec3f(10, -5, 2) * scale;
				body.m_Orientation = glm::angleAxis(angle, glm::normalize(Vec3f(1, 2, 3)));
				body.m_AngularVelocity = body.m_Orientation * localOmega;
				body.m_LinearVelocity = Vec3f(1, -2, 0.5f) * scale;
				const Vec3f position = body.m_Position, velocity = body.m_LinearVelocity, omega = body.m_AngularVelocity;
				const Quat orientation = body.m_Orientation;
				SweepAndPruneBroadphase broadphase;
				std::vector<RigidBody3D*> bodies{ &body };
				std::vector<collisionPair_t> pairs;
				const float duration = glm::two_pi<float>() / 30.0f;
				broadphase.FindPairs(bodies, pairs, duration);
				const Bounds swept = (broadphase.*GetPrivateMember(AngularSweptBoundsTag{}))[0];
				bool contains = Math::IsFinite(swept.mins) && Math::IsFinite(swept.maxs);
				RigidBody3D incremental = body;
				for (int sample = 0; sample <= 64; ++sample)
				{
					RigidBody3D predicted = body;
					predicted.Update(duration * sample / 64.0f);
					if (sample > 0) incremental.Update(duration / 64.0f);
					contains = contains && predicted.HasFiniteState() && incremental.HasFiniteState() &&
						EnclosesBounds(swept, predicted.GetWorldBounds()) && EnclosesBounds(swept, incremental.GetWorldBounds());
				}
				Expect(contains, "angular swept bounds contain intermediate direct and partitioned rotating/translating offset-shape poses");
				Expect(Near(body.m_Position, position, 0) && Near(body.m_LinearVelocity, velocity, 0) &&
					Near(body.m_AngularVelocity, omega, 0) && OrientationResidual(body.m_Orientation, orientation) == 0,
					"broad-phase rotation enclosure preserves live physical state");
				broadphase.FindPairs(bodies, pairs, duration);
				const Bounds repeated = (broadphase.*GetPrivateMember(AngularSweptBoundsTag{}))[0];
				Expect(Near(swept.mins, repeated.mins, 0) && Near(swept.maxs, repeated.maxs, 0) &&
					broadphase.GetLastStats().fullSortCount == 0, "rotation bounds repeat exactly without rebuilding SAP membership");
			}
		}
	}

	void TestAngularSweepCandidatesAndGuards()
	{
		using namespace GEngine;
		using BodyType = Component::BodyType;
		ShapeBox rod(BoxPoints(Vec3f(4, 0.15f, 0.15f)));
		ShapeSphere sphere(0.25f);
		RigidBody3D a, b;
		ConfigureBoxBody(a, rod, Vec3f(0), Quat(1, 0, 0, 0));
		ConfigureSphereBody(b, sphere, Vec3f(0, 3.5f, 0));
		b.SetBodyTypeAndInverseMass(BodyType::Static, 0.0f);
		a.m_AngularVelocity = Vec3f(0, 0, 30);
		SweepAndPruneBroadphase broadphase;
		std::vector<RigidBody3D*> bodies{ &a, &b };
		std::vector<collisionPair_t> pairs;
		const float duration = glm::two_pi<float>() / 30.0f;
		RigidBody3D end = a; end.Update(duration);
		Expect(!a.GetWorldBounds().DoesIntersect(b.GetWorldBounds()) && !end.GetWorldBounds().DoesIntersect(b.GetWorldBounds()),
			"full-turn rod fixture misses the obstacle at both endpoints");
		broadphase.FindPairs(bodies, pairs, duration);
		Expect(pairs.size() == 1 && ContainsPair(pairs, 0, 1), "full-turn angular sweep retains the intermediate long-box collision");
		broadphase.FindPairs(bodies, pairs, 0.0f);
		Expect(pairs.empty(), "zero duration does not expand a stored angular velocity");
		a.SetBodyTypeAndInverseMass(BodyType::Static, 0.0f);
		b.SetBodyTypeAndInverseMass(BodyType::Dynamic, 1.0f);
		broadphase.FindPairs(bodies, pairs, duration);
		Expect(pairs.empty(), "Static stored spin does not enlarge a fixed collider");
		a.Type = BodyType::Dynamic; a.m_InvMass = 0.0f;
		broadphase.FindPairs(bodies, pairs, duration);
		Expect(pairs.empty(), "non-integrating zero-mass Dynamic stored spin does not enlarge bounds");
		a.SetBodyTypeAndInverseMass(BodyType::Kinematic, 0.0f);
		broadphase.FindPairs(bodies, pairs, duration);
		Expect(pairs.size() == 1, "Kinematic angular motion expands swept bounds");
		a.m_CollisionMask = 0u;
		broadphase.FindPairs(bodies, pairs, duration);
		Expect(pairs.empty() && broadphase.GetLastStats().maskRejectedCount == 1,
			"expanded angular candidates still obey reciprocal collision masks");
		a.m_CollisionMask = ~0u;
		a.m_Shape = &sphere;
		broadphase.FindPairs(bodies, pairs, duration);
		Expect(pairs.empty(), "sphere rotation does not enlarge rotation-invariant geometry");
		a.m_Shape = &rod; a.m_AngularVelocity = Vec3f(0);
		broadphase.FindPairs(bodies, pairs, duration);
		Expect(pairs.empty(), "nonrotating box retains the existing linear bounds");
	}

	template<typename Base>
	class AngularSpeedProbe : public Base
	{
	public:
		using Base::Base;
		mutable bool called = false;
		mutable GEngine::Vec3f firstOmega{}, firstDirection{};
		mutable float firstSpeed = 0;
		float FastestLinearSpeed(const GEngine::Vec3f& omega, const GEngine::Vec3f& direction) const override
		{
			const float speed = Base::FastestLinearSpeed(omega, direction);
			if (!called) { called = true; firstOmega = omega; firstDirection = direction; firstSpeed = speed; }
			return speed;
		}
	};

	template<typename Base>
	void CheckAngularSpeedFrames()
	{
		using namespace GEngine;
		auto points = BoxPoints(Vec3f(4, 0.5f, 0.25f));
		for (Vec3f& point : points) point += Vec3f(2, -1, 0.4f);
		for (const float angle : { 0.3f, 0.9f, 1.5f })
		{
			AngularSpeedProbe<Base> shapeA(points), shapeB(points);
			RigidBody3D a, b;
			a.m_Shape = &shapeA; b.m_Shape = &shapeB;
			a.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic, 1.0f);
			b.SetBodyTypeAndInverseMass(Component::BodyType::Kinematic, 0.0f);
			a.m_Orientation = glm::angleAxis(angle, glm::normalize(Vec3f(1, 2, 3)));
			b.m_Orientation = glm::angleAxis(-angle, glm::normalize(Vec3f(3, -2, 1)));
			b.m_Position = Vec3f(0, 30, 0);
			a.m_AngularVelocity = Vec3f(2, -3, 4); b.m_AngularVelocity = Vec3f(-4, 1, 3);
			contact_t contact{};
			Expect(!Collision::ConservativeAdvance(&a, &b, 1.0e-4f, contact), "short angular frame fixture stays separated");
			const Vec3f worldDirection = glm::normalize(contact.ptOnB_WorldSpace - contact.ptOnA_WorldSpace);
			for (int side = 0; side < 2; ++side)
			{
				auto& shape = side == 0 ? shapeA : shapeB;
				const auto& body = side == 0 ? a : b;
				const Vec3f direction = side == 0 ? worldDirection : -worldDirection;
				float expected = 0;
				for (const Vec3f& point : points)
				{
					const Vec3f lever = body.m_Orientation * (point - shape.GetCenterOfMass());
					expected = std::max(expected, glm::dot(glm::cross(body.GetAngularVelocity(), lever), direction));
				}
				Expect(shape.called && Near(shape.firstSpeed, expected, 3.0e-5f),
					"CA angular speed agrees with independently transformed world-space vertex velocities for either participant");
				Expect(Near(shape.firstOmega, glm::conjugate(body.m_Orientation) * body.GetAngularVelocity(), 3.0e-6f) &&
					Near(shape.firstDirection, glm::conjugate(body.m_Orientation) * direction, 3.0e-6f),
					"CA supplies angular velocity and search direction in the shape's local frame");
			}
		}
	}

	void TestRotatingLongBoxCollision()
	{
		using namespace GEngine;
		using BodyType = Component::BodyType;
		for (const bool reversed : { false, true })
		for (const float angle : { 0.0f, 0.35f })
		{
			ShapeBox rod(BoxPoints(Vec3f(4, 0.15f, 0.15f)));
			ShapeSphere sphere(0.25f);
			PhysicsSystem system;
			auto* world = new PhysicsWorld(Vec3f(0)); system.SetPhysicsWorld(world);
			auto* first = world->CreateRigidBody3D(); auto* second = world->CreateRigidBody3D();
			auto* a = reversed ? second : first; auto* b = reversed ? first : second;
			const Quat orientation = glm::angleAxis(angle, Vec3f(0, 0, 1));
			ConfigureBoxBody(*a, rod, Vec3f(0), orientation);
			ConfigureSphereBody(*b, sphere, orientation * Vec3f(0, 3.5f, 0));
			b->SetBodyTypeAndInverseMass(BodyType::Static, 0.0f);
			a->m_AngularVelocity = Vec3f(0, 0, 30);
			a->m_Elasticity = b->m_Elasticity = 0; a->m_Friction = b->m_Friction = 0;
			contact_t contact{};
			const bool hit = Collision::Intersect(a, b, 0.06f, contact);
			std::cout << "ANGULAR_CCD angle=" << angle << " reversed=" << reversed << " hit=" << hit << " toi=" << contact.timeOfImpact << '\n';
			Expect(hit && contact.timeOfImpact > 0 && contact.timeOfImpact < 0.06f && Finite(contact),
				"rotating long-box fixture has a finite positive narrow-phase impact");
			system.Update(0.06f);
			Expect(GetCollisionPairs(system).size() == 1 && a->HasFiniteState() && b->HasFiniteState() &&
				a->m_AngularVelocity.z < 29.99f && glm::length2(a->m_LinearVelocity) > 0.0f,
				"long rotating box reaches collision response instead of tunneling due to broad-phase rejection");
		}
	}


	void TestEpaContactGeometry()
	{
		using namespace GEngine;
		for (float scale : { 0.1f, 1.0f, 10.0f, 100.0f }) {
			ShapeBox box(BoxPoints(Vec3f(scale)));
			for (float offset : { 0.4f, 1.2f, 1.999f, 2.0f, 2.1f }) {
				RigidBody3D a, b;
				ConfigureBoxBody(a, box, Vec3f(0), Quat(1, 0, 0, 0));
				ConfigureBoxBody(b, box, scale * Vec3f(offset, 0.3f, 0.2f), Quat(1, 0, 0, 0));
				for (bool reverse : { false, true }) {
					Vec3f pa(99), pb(99);
					const auto status = GJK_GetContact(reverse ? &b : &a, reverse ? &a : &b, 0, pa, pb);
					Expect(Finite(pa) && Finite(pb), "EPA scaled box outputs are finite");
					if (offset > 2) {
						Expect(status == GjkContactStatus::Separated && pa == Vec3f(0) && pb == Vec3f(0),
							"EPA is bypassed for an independent positive box face gap");
					} else {
						Expect(status == GjkContactStatus::Contact &&
							Near(glm::length(pb - pa), (2 - offset) * scale, 5e-4f * scale),
							"EPA box penetration matches independent face depth across scales and pair orders");
					}
				}
			}
		}
	}

	void TestGjkScaleRegression()
	{
		using namespace GEngine;
		for (float scale : { 0.1f, 0.5f, 1.0f, 10.0f, 100.0f }) {
			ShapeSphere sphere(scale);
			for (const Vec3f offset : { Vec3f(3, 0, 0), Vec3f(4, 3, 2), Vec3f(1, 7, -3) }) {
				RigidBody3D a, b;
				ConfigureSphereBody(a, sphere, scale * Vec3f(3, -2, 1));
				ConfigureSphereBody(b, sphere, a.m_Position + scale * offset);
				Vec3f pa, pb;
				Expect(!GJK_DoesIntersect(&a, &b), "scaled analytic spheres are separated in GJK");
				Expect(GJK_GetContact(&a, &b, 0, pa, pb) == GjkContactStatus::Separated,
					"scaled analytic spheres report separation without EPA");
				GJK_ClosestPoints(&a, &b, pa, pb);
				const float expected = scale * (glm::length(offset) - 2.0f);
				Expect(Finite(pa) && Finite(pb) && Near(glm::length(pb - pa), expected, 5e-4f * scale),
					"scaled GJK distance agrees with analytic sphere distance");
			}
		}
		// Fixed seed and known local separating/penetrating axis, followed by a common rigid transform.
		// The oracle does not reuse production simplex or EPA calculations.
		std::uint32_t seed = 0x27a91u;
		const auto random = [&seed]() {
			seed = 1664525u * seed + 1013904223u;
			return static_cast<float>(seed >> 8) / 16777216.0f;
		};
		for (float scale : { 0.1f, 1.0f, 10.0f, 100.0f }) {
			ShapeBox box(BoxPoints(Vec3f(scale)));
			for (int sample = 0; sample < 64; ++sample) {
				const bool overlap = (sample % 2) == 0;
				const Vec3f axis(random(), random(), random());
				const Quat rotation = glm::angleAxis(6.0f * random(), Math::NormalizeOr(axis));
				const Vec3f translation = scale * Vec3f(8 * random(), -4 * random(), 6 * random());
				const Vec3f offset(overlap ? 1.2f : 3.2f, 0.4f * random(), 0.4f * random());
				RigidBody3D a, b;
				ConfigureBoxBody(a, box, translation, rotation);
				ConfigureBoxBody(b, box, translation + rotation * (scale * offset), rotation);
				Expect(GJK_DoesIntersect(&a, &b) == overlap && GJK_DoesIntersect(&b, &a) == overlap,
					"seeded scaled boxes agree with independent local-axis overlap oracle in both orders");
				Vec3f pa, pb;
				if (!overlap) {
					GJK_ClosestPoints(&a, &b, pa, pb);
					Expect(Finite(pa) && Finite(pb) && Near(glm::length(pb - pa), 1.2f * scale, 5e-4f * scale),
						"seeded scaled box distance agrees with analytic face gap");
				}
			}
		}
	}


	class GjkSupportFixture final : public GEngine::ShapeSphere
	{
	public:
		mutable unsigned calls{};
		unsigned failAt{};
		using ShapeSphere::ShapeSphere;

		GEngine::Vec3f Support(const GEngine::Vec3f& direction, const GEngine::Vec3f& position,
			const GEngine::Quat& orientation, float bias) const override
		{
			if (++calls == failAt) { return GEngine::Vec3f(std::numeric_limits<float>::quiet_NaN()); }
			return ShapeSphere::Support(direction, position, orientation, bias);
		}
	};

	void TestGjkTerminationContract()
	{
		using namespace GEngine;
		ShapeBox box(UnitBoxPoints());
		RigidBody3D a, b;
		const Quat identity(1, 0, 0, 0);
		ConfigureBoxBody(a, box, Vec3f(0), identity);
		ConfigureBoxBody(b, box, Vec3f(1.2f, 0.3f, 0.2f), identity);
		Vec3f pa, pb;
		GjkDiagnostics diagnostics;
		for (unsigned budget : { 0u, 1u }) {
			Expect(!GJK_DoesIntersect(&a, &b, &diagnostics, budget) &&
				diagnostics.termination == GjkTermination::IterationLimit && diagnostics.iterations == budget,
				"boolean GJK obeys an exhausted budget without reporting an intersection");
			pa = pb = Vec3f(99);
			Expect(GJK_GetContact(&a, &b, 0.001f, pa, pb, &diagnostics, budget) == GjkContactStatus::Failed &&
				diagnostics.termination == GjkTermination::IterationLimit && diagnostics.iterations == budget &&
				pa == Vec3f(0) && pb == Vec3f(0),
				"contact GJK exposes cap exhaustion and clears witness outputs");
			pa = pb = Vec3f(99);
			GJK_ClosestPoints(&a, &b, pa, pb, &diagnostics, budget);
			Expect(diagnostics.termination == GjkTermination::IterationLimit && diagnostics.iterations == budget &&
				pa == Vec3f(0) && pb == Vec3f(0),
				"distance GJK exposes cap exhaustion and does not publish partial witnesses");
			Expect(!GJK_DoesIntersect(&a, &b, 0.001f, pa, pb, &diagnostics, budget) &&
				diagnostics.termination == GjkTermination::IterationLimit && diagnostics.iterations == budget,
				"legacy contact wrapper forwards budget and diagnostics");
		}
		GjkDiagnostics full, oversized;
		Vec3f fullA, fullB, largeA, largeB;
		Expect(GJK_GetContact(&a, &b, 0.001f, fullA, fullB, &full) == GjkContactStatus::Contact &&
			full.termination == GjkTermination::OriginReached && full.iterations > 1 && full.iterations <= GjkMaxIterations,
			"valid contact succeeds with the default bounded budget");
		Expect(GJK_GetContact(&a, &b, 0.001f, largeA, largeB, &oversized, std::numeric_limits<unsigned>::max()) ==
			GjkContactStatus::Contact && oversized.iterations == full.iterations && fullA == largeA && fullB == largeB,
			"oversized budget preserves bounded default contact output");
		Expect(!GJK_DoesIntersect(nullptr, &b, &diagnostics) &&
			diagnostics.termination == GjkTermination::InvalidInput && diagnostics.iterations == 0,
			"invalid body input resets prior query diagnostics");
		for (float bias : { -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
			pa = pb = Vec3f(99);
			Expect(GJK_GetContact(&a, &b, bias, pa, pb, &diagnostics) == GjkContactStatus::Failed &&
				diagnostics.termination == GjkTermination::InvalidInput && diagnostics.iterations == 0 &&
				pa == Vec3f(0) && pb == Vec3f(0),
				"invalid GJK bias is observable and resets witnesses");
		}
		GjkSupportFixture sphere(1);
		a.m_Shape = &sphere;
		for (unsigned failureCall : { 1u, 2u }) {
			sphere.failAt = failureCall;
			for (int api = 0; api < 3; ++api) {
				sphere.calls = 0;
				pa = pb = Vec3f(99);
				bool failed = true;
				if (api == 0) { failed = !GJK_DoesIntersect(&a, &b, &diagnostics); }
				if (api == 1) { failed = GJK_GetContact(&a, &b, 0.001f, pa, pb, &diagnostics) == GjkContactStatus::Failed; }
				if (api == 2) { GJK_ClosestPoints(&a, &b, pa, pb, &diagnostics); }
				Expect(failed && diagnostics.termination == GjkTermination::InvalidSupport &&
					diagnostics.iterations == failureCall - 1 && (api == 0 || (pa == Vec3f(0) && pb == Vec3f(0))),
					"non-finite initial or iterative support fails safely with an exact termination count");
			}
		}
		sphere.failAt = 0;
		PointShapeFixture point;
		point.SetShapeType(ShapeType::Convex);
		a.m_Shape = b.m_Shape = &point;
		b.m_Position = a.m_Position;
		Expect(!GJK_DoesIntersect(&a, &b, &diagnostics) &&
			diagnostics.termination == GjkTermination::DuplicateSupport && diagnostics.iterations == 1,
			"degenerate coincident point support terminates as an observable duplicate");
		GJK_ClosestPoints(&a, &b, pa, pb, &diagnostics);
		Expect(pa == Vec3f(0) && pb == Vec3f(0) && diagnostics.termination == GjkTermination::DuplicateSupport,
			"valid degenerate distance witnesses remain finite");

		ConfigureBoxBody(a, box, Vec3f(0), identity);
		ConfigureBoxBody(b, box, Vec3f(3, 0, 0), identity);
		Expect(!GJK_DoesIntersect(&a, &b, &diagnostics) &&
			diagnostics.termination == GjkTermination::SeparatingAxis,
			"true separating-axis termination is distinguishable from a failed search");
	}


	void TestEpaTerminationContract()
	{
		using namespace GEngine;
		ShapeBox box(UnitBoxPoints());
		RigidBody3D a, b;
		ConfigureBoxBody(a, box, Vec3f(0), Quat(1, 0, 0, 0));
		ConfigureBoxBody(b, box, Vec3f(1.2f, 0.3f, 0.2f), Quat(1, 0, 0, 0));
		Vec3f pa, pb, referenceA, referenceB;
		GjkDiagnostics diagnostics, reference;
		Expect(GJK_GetContact(&a, &b, 0.001f, referenceA, referenceB, &reference) == GjkContactStatus::Contact &&
			reference.epa.iterations > 1 && reference.epa.iterations <= EpaMaxIterations &&
			(reference.epa.termination == EpaTermination::Converged || reference.epa.termination == EpaTermination::DuplicateSupport),
			"successful EPA exposes a bounded count and validated success reason");
		ResetPhysicsProfile();
		for (unsigned budget : { 0u, 1u }) {
			pa = pb = Vec3f(99);
			Expect(GJK_GetContact(&a, &b, 0.001f, pa, pb, &diagnostics, GjkMaxIterations, budget) == GjkContactStatus::Failed &&
				diagnostics.termination == GjkTermination::ContactExpansionFailed &&
				diagnostics.epa.termination == EpaTermination::IterationLimit && diagnostics.epa.iterations == budget &&
				pa == Vec3f(0) && pb == Vec3f(0),
				"EPA exhaustion is a failure, not separation or partial contact, and clears witnesses");
			pa = pb = Vec3f(99);
			Expect(!GJK_DoesIntersect(&a, &b, 0.001f, pa, pb, &diagnostics, GjkMaxIterations, budget) &&
				diagnostics.epa.termination == EpaTermination::IterationLimit && diagnostics.epa.iterations == budget &&
				pa == Vec3f(0) && pb == Vec3f(0), "contact wrapper forwards EPA diagnostics and budget");
		}
		auto profile = GetPhysicsProfileSnapshot();
		Expect(!IsPhysicsProfilingEnabled() || (profile.epaCallCount == 4 && profile.epaFailureCount == 4 &&
			profile.epaIterationCount == 2 && profile.epaMaxIterations == 1),
			"EPA profiler counts failed calls, total iterations and maximum independently of GJK work");
		Expect(GJK_GetContact(&a, &b, 0.001f, pa, pb, &diagnostics, GjkMaxIterations,
			std::numeric_limits<unsigned>::max()) == GjkContactStatus::Contact && pa == referenceA && pb == referenceB &&
			diagnostics.epa.iterations == reference.epa.iterations && diagnostics.epa.termination == reference.epa.termination,
			"oversized EPA budget is clamped and preserves the default contact");
		const auto beforeUnobserved = GetPhysicsProfileSnapshot();
		Expect(GJK_GetContact(&a, &b, 0.001f, pa, pb, nullptr, GjkMaxIterations, 0) == GjkContactStatus::Failed &&
			pa == Vec3f(0) && pb == Vec3f(0), "EPA fails safely when per-query diagnostics are omitted");
		profile = GetPhysicsProfileSnapshot();
		Expect(!IsPhysicsProfilingEnabled() || (profile.epaCallCount == beforeUnobserved.epaCallCount + 1 &&
			profile.epaFailureCount == beforeUnobserved.epaFailureCount + 1),
			"EPA failures remain visible in profiling when callers omit diagnostics");
		b.m_Position = Vec3f(3, 0, 0);
		Expect(GJK_GetContact(&a, &b, 0.001f, pa, pb, &diagnostics) == GjkContactStatus::Separated &&
			diagnostics.epa.termination == EpaTermination::NotRun && diagnostics.epa.iterations == 0,
			"true separation resets stale EPA diagnostics to NotRun");
		Expect(GJK_GetContact(nullptr, &b, 0.001f, pa, pb, &diagnostics) == GjkContactStatus::Failed &&
			diagnostics.epa.termination == EpaTermination::NotRun && diagnostics.epa.iterations == 0,
			"invalid GJK input does not masquerade as an attempted EPA query");
		b.m_Position = Vec3f(1.2f, 0.3f, 0.2f);
		Expect(GJK_GetContact(&a, &b, 1e30f, pa, pb, &diagnostics) == GjkContactStatus::Failed &&
			diagnostics.epa.termination == EpaTermination::InvalidSimplex && diagnostics.epa.iterations == 0 &&
			pa == Vec3f(0) && pb == Vec3f(0), "overflowed EPA seed geometry is diagnosed and rejected before expansion");

		GjkSupportFixture sphere(1);
		a.m_Shape = &sphere;
		// Measure the support calls needed to reach EPA, then fail its first support.
		Expect(GJK_GetContact(&a, &b, 0.001f, pa, pb, &diagnostics, GjkMaxIterations, 0) == GjkContactStatus::Failed &&
			diagnostics.epa.termination == EpaTermination::IterationLimit, "support fixture reaches the EPA stage");
		sphere.failAt = sphere.calls + 1;
		sphere.calls = 0;
		Expect(GJK_GetContact(&a, &b, 0.001f, pa, pb, &diagnostics) == GjkContactStatus::Failed &&
			diagnostics.epa.termination == EpaTermination::InvalidSupport && diagnostics.epa.iterations == 1 &&
			pa == Vec3f(0) && pb == Vec3f(0), "non-finite EPA support never escapes as a contact");
		sphere.failAt = 0;
		a.m_Shape = b.m_Shape = &sphere;
		b.m_Position = a.m_Position;
		for (unsigned budget : { EpaMaxIterations, std::numeric_limits<unsigned>::max() }) {
			Expect(GJK_GetContact(&a, &b, 0, pa, pb, &diagnostics, GjkMaxIterations, budget) == GjkContactStatus::Failed &&
				diagnostics.epa.termination == EpaTermination::IterationLimit && diagnostics.epa.iterations == EpaMaxIterations &&
				pa == Vec3f(0) && pb == Vec3f(0), "coincident smooth spheres exhaust exactly the hard cap even with an oversized request");
		}
		ResetPhysicsProfile();
		profile = GetPhysicsProfileSnapshot();
		Expect(profile.epaCallCount == 0 && profile.epaFailureCount == 0 && profile.epaIterationCount == 0 &&
			profile.epaMaxIterations == 0, "profile reset clears every EPA counter");
	}

	void TestGjkSeededQueries()
	{
		using namespace GEngine;
		std::uint32_t seed = 0x270027u;
		const auto random = [&seed]() {
			seed = 1664525u * seed + 1013904223u;
			return static_cast<float>(seed >> 8) / 16777216.0f;
		};
		std::array<unsigned, 9> reasons{};
		std::array<unsigned, 12> epaReasons{};
		std::uint64_t epaCalls = 0, epaIterations = 0, epaFailures = 0;
		unsigned epaMaximum = 0;
		ResetPhysicsProfile();
		unsigned maximumIterations = 0;
		unsigned safeContactFailures = 0;
		std::uint64_t contactFingerprint = 14695981039346656037ull;
		const auto hashContactWord = [&contactFingerprint](std::uint32_t word) {
			for (unsigned byte = 0; byte < 4; ++byte) {
				contactFingerprint ^= (word >> (8 * byte)) & 255u;
				contactFingerprint *= 1099511628211ull;
			}
		};
		for (float scale : { 0.1f, 1.0f, 10.0f, 100.0f }) {
			ShapeBox box(BoxPoints(Vec3f(scale)));
			ShapeConvex convex(BoxPoints(Vec3f(scale, 0.7f * scale, 0.5f * scale)));
			ShapeSphere sphere(scale);
			PhysicalShape* shapes[] = { &box, &convex, &sphere };
			for (int sample = 0; sample < 300; ++sample) {
				RigidBody3D a, b;
				a.m_Shape = shapes[sample % 3];
				b.m_Shape = shapes[(sample / 3) % 3];
				a.m_Position = scale * Vec3f(random(), random(), random());
				b.m_Position = a.m_Position + scale * Vec3f(4 * random() - 2, 4 * random() - 2, 4 * random() - 2);
				const Vec3f axisA(random(), random(), random()), axisB(random(), random(), random());
				a.m_Orientation = glm::angleAxis(3 * random(), Math::NormalizeOr(axisA));
				b.m_Orientation = glm::angleAxis(3 * random(), Math::NormalizeOr(axisB));
				const PredictionBodySnapshot beforeA(a), beforeB(b);
				Vec3f pa, pb, repeatA, repeatB;
				GjkDiagnostics contact, repeat, boolean, distance;
				const auto status = GJK_GetContact(&a, &b, 0.001f, pa, pb, &contact);
				hashContactWord(static_cast<std::uint32_t>(status));
				for (float value : { pa.x, pa.y, pa.z, pb.x, pb.y, pb.z }) {
					std::uint32_t bits;
					std::memcpy(&bits, &value, sizeof(bits));
					hashContactWord(bits);
				}
				const auto again = GJK_GetContact(&a, &b, 0.001f, repeatA, repeatB, &repeat);
				Expect(status == again && pa == repeatA && pb == repeatB &&
					contact.termination == repeat.termination && contact.iterations == repeat.iterations,
					"seeded GJK contact witnesses and termination repeat exactly");
				Expect(contact.epa.termination == repeat.epa.termination && contact.epa.iterations == repeat.epa.iterations &&
					contact.epa.iterations <= EpaMaxIterations, "seeded EPA diagnostics repeat exactly within the cap");
				const bool epaRan = contact.epa.termination != EpaTermination::NotRun;
				const bool epaSuccess = contact.epa.termination == EpaTermination::Converged ||
					contact.epa.termination == EpaTermination::DuplicateSupport;
				Expect((status == GjkContactStatus::Contact) == epaSuccess &&
					(status != GjkContactStatus::Separated || !epaRan) &&
					(!epaRan || epaSuccess || status == GjkContactStatus::Failed),
					"seeded EPA success, failure and bypass agree with the contact result");
				const auto epaReason = static_cast<std::size_t>(contact.epa.termination);
				Expect(epaReason < epaReasons.size(), "seeded EPA termination has a declared reason");
				if (epaReason < epaReasons.size()) { ++epaReasons[epaReason]; }
				epaCalls += epaRan ? 2 : 0;
				epaIterations += 2 * contact.epa.iterations;
				epaFailures += epaRan && !epaSuccess ? 2 : 0;
				epaMaximum = std::max(epaMaximum, contact.epa.iterations);
				Expect(Finite(pa) && Finite(pb) &&
					(status != GjkContactStatus::Failed || (pa == Vec3f(0) && pb == Vec3f(0) &&
						contact.termination == GjkTermination::ContactExpansionFailed)),
					"seeded valid-shape contacts publish finite witnesses or observable safe expansion failure");
				if (status == GjkContactStatus::Failed) { ++safeContactFailures; }
				const bool overlap = GJK_DoesIntersect(&a, &b, &boolean);
				Expect((status == GjkContactStatus::Separated) == !overlap,
					"seeded boolean and contact queries agree on the GJK overlap classification");
				GJK_ClosestPoints(&a, &b, pa, pb, &distance);
				GJK_ClosestPoints(&a, &b, repeatA, repeatB, &repeat);
				Expect(Finite(pa) && Finite(pb) && pa == repeatA && pb == repeatB &&
					distance.termination == repeat.termination && distance.iterations == repeat.iterations,
					"seeded GJK distance witnesses and termination repeat exactly");
				for (const auto& result : { contact, boolean, distance }) {
					const auto reason = static_cast<std::size_t>(result.termination);
					Expect(result.iterations > 0 && result.iterations <= GjkMaxIterations &&
						result.termination != GjkTermination::IterationLimit && reason >= 3 && reason < reasons.size(),
						"seeded supported geometry terminates within the hard cap with an explicit valid reason");
					if (reason < reasons.size()) { ++reasons[reason]; }
					maximumIterations = std::max(maximumIterations, result.iterations);
				}
				Expect(beforeA.Unchanged() && beforeB.Unchanged(), "seeded GJK queries leave live body state byte-for-byte unchanged");
			}
		}
		const auto profile = GetPhysicsProfileSnapshot();
		Expect(!IsPhysicsProfilingEnabled() || (profile.epaCallCount == epaCalls &&
			profile.epaFailureCount == epaFailures && profile.epaIterationCount == epaIterations &&
			profile.epaMaxIterations == epaMaximum), "aggregate EPA telemetry equals the sum of all individual attempted queries");
		std::cout << "EPA_FUZZ queries=" << epaCalls << " iterations=" << epaIterations
			<< " max_iterations=" << epaMaximum << " failures=" << epaFailures << " reasons=";
		for (unsigned count : epaReasons) { std::cout << count << ','; }
		std::cout << '\n';
		std::cout << "GJK_FUZZ pairs=1200 max_iterations=" << maximumIterations
			<< " safe_contact_failures=" << safeContactFailures << " contact_fingerprint=" << contactFingerprint << " reasons=";
		for (unsigned count : reasons) { std::cout << count << ','; }
		std::cout << '\n';
	}

	void TestEpaThinGeometry()
	{
		using namespace GEngine;
		for (float scale : { 0.1f, 1.0f, 10.0f, 100.0f }) {
			for (float thickness : { 1e-4f, 1e-3f, 1e-2f }) {
				ShapeBox box(BoxPoints(scale * Vec3f(1, 1, thickness)));
				Expect(box.IsValid(), "thin EPA regression starts from valid shape geometry");
				RigidBody3D a, b;
				const Quat rotation = glm::angleAxis(0.37f, Math::NormalizeOr(Vec3f(1, 2, 3)));
				ConfigureBoxBody(a, box, scale * Vec3f(3, -2, 1), rotation);
				ConfigureBoxBody(b, box, a.m_Position + rotation * (scale * Vec3f(0.2f, 0.3f, thickness)), rotation);
				const PredictionBodySnapshot beforeA(a), beforeB(b);
				for (bool reverse : { false, true }) {
					Vec3f pa(99), pb(99), repeatA, repeatB;
					GjkDiagnostics result, repeat;
					const auto status = GJK_GetContact(reverse ? &b : &a, reverse ? &a : &b, 0, pa, pb, &result);
					const auto again = GJK_GetContact(reverse ? &b : &a, reverse ? &a : &b, 0, repeatA, repeatB, &repeat);
					Expect(Finite(pa) && Finite(pb) && status == again && pa == repeatA && pb == repeatB &&
						result.termination == repeat.termination && result.epa.termination == repeat.epa.termination &&
						result.epa.iterations == repeat.epa.iterations && result.epa.iterations <= EpaMaxIterations,
						"thin transformed EPA queries remain finite, repeatable and bounded in both body orders");
					const bool success = result.epa.termination == EpaTermination::Converged ||
						result.epa.termination == EpaTermination::DuplicateSupport;
					Expect((status == GjkContactStatus::Contact) == success &&
						(status == GjkContactStatus::Contact || (pa == Vec3f(0) && pb == Vec3f(0))),
						"thin geometry can publish a contact only after EPA output validation");
				}
				Expect(beforeA.Unchanged() && beforeB.Unchanged(), "thin EPA queries preserve live body state");
			}
		}
	}

	int RunEpaRobustnessRegression()
	{
		TestEpaContactGeometry();
		TestEpaTerminationContract();
		TestEpaThinGeometry();
		TestGjkSeededQueries();
		if (failureCount) { std::cerr << failureCount << " of " << testCount << " EPA robustness checks failed\n"; return 1; }
		std::cout << "EPA robustness regression: " << testCount << " checks passed\n";
		return 0;
	}

	int RunGjkRobustnessRegression()
	{
		TestEpaContactGeometry();
		TestGjkScaleRegression();
		TestGjkTerminationContract();
		TestGjkSeededQueries();
		if (failureCount) { std::cerr << failureCount << " of " << testCount << " GJK robustness checks failed\n"; return 1; }
		std::cout << "GJK robustness regression: " << testCount << " checks passed\n";
		return 0;
	}

	int RunAngularSweepRegression()
	{
		TestAngularSweepContainment();
		TestAngularSweepCandidatesAndGuards();
		CheckAngularSpeedFrames<GEngine::ShapeBox>();
		CheckAngularSpeedFrames<GEngine::ShapeConvex>();
		TestRotatingLongBoxCollision();
		if (failureCount) { std::cerr << failureCount << " of " << testCount << " angular-sweep checks failed\n"; return 1; }
		std::cout << "Angular-sweep regression: " << testCount << " checks passed\n";
		return 0;
	}

}

int main(int argc, char** argv)
{
	GEngine::Log::Initialize();
	if (argc == 2)
	{
		const std::string_view argument(argv[1]);
		if (argument == "--solver-storage") { TestSolverStorage(); return failureCount ? 1 : 0; }
		if (argument == "--angular-resistance-coupling") { TestCombinedAngularResistanceAnisotropy(); return failureCount ? 1 : 0; }
		if (argument == "--rolling-resistance") return RunRollingResistanceRegression();
		if (argument == "--rolling-stability") { TestExactBoxWorldTimestepStability(0.05f,0.05f); return failureCount ? 1 : 0; }
		if (argument == "--spin-resistance") return RunSpinResistanceRegression();
		if (argument == "--spin-stability") { TestExactBoxWorldTimestepStability(0.05f); return failureCount ? 1 : 0; }
		if (argument == "--runtime-transform") return RunRuntimeTransformRegression();
		if (argument == "--absolute-scaling") return RunAbsoluteScalingRegression();
		if (argument == "--epa-robustness") return RunEpaRobustnessRegression();
		if (argument == "--gjk-robustness") return RunGjkRobustnessRegression();
		if (argument == "--angular-sweep") return RunAngularSweepRegression();
		if (argument == "--toi-revalidation") return RunToiRevalidationRegression();
		if (argument == "--pure-prediction") return RunPurePredictionRegression();
		if (argument == "--persistence-continuity") return RunPersistenceContinuityRegression();
		if (argument == "--manifold-persistence") return RunManifoldPersistenceRegression();
		if (argument == "--box-manifolds") return RunBoxManifoldRegression();
		if (argument == "--box-features") return RunBoxFeatureRegression();
		if (argument == "--position-stabilization") return RunPositionStabilizationRegression();
		if (argument == "--solver-iterations") return RunSolverIterationsRegression();
		if (argument == "--restitution-threshold") return RunRestitutionThresholdRegression();
		if (argument == "--ballistic-contact") return RunBallisticContactRegression();
		if (argument == "--fixed-scheduling") return RunFixedSchedulingRegression();
		if (argument == "--timestep-stability") return RunTimestepStabilityRegression();
		if (argument == "--contact-convergence") return RunContactConvergenceRegression();
		if (argument == "--resting-friction") return RunRestingFrictionRegression();
		if (argument == "--contact-convention") return RunContactConventionRegression();
		if (argument == "--sleep-integration") return RunSleepIntegrationRegression();
		if (argument == "--contact-islands") return RunContactIslandsRegression();
		if (argument == "--sleep-primitives") return RunSleepPrimitivesRegression();
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

	TestSolverStorage();
	TestScaleSourceExceptionSafety();
	TestAbsoluteSphereScaling();
	TestAbsolutePointScaling<GEngine::ShapeBox>();
	TestAbsolutePointScaling<GEngine::ShapeConvex>();
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
	TestRuntimePoseApi();
	TestRuntimeTransformSynchronization();
	TestSceneRuntimeLifecycleRegression();
	TestConvexValidityContract();
	TestContactPairOrderRegression();
	TestCollisionContactConvention();
	TestContactImpulsePermutation();
	TestPersistentContactPermutation();
	TestEpaTerminationContract();
	TestEpaThinGeometry();
	TestGjkSeededQueries();
	TestGjkTerminationContract();
	TestEpaContactGeometry();
	TestGjkScaleRegression();
	TestDegenerateGjkDirection();
	TestZeroQuaternionBodyUpdate();
	TestBoxConstructionInvariant();
	TestBoxFaceGeometry();
	TestBoxFaceSelectionAndRebuild();
	TestBoxContactFeaturePairs();
	TestBoxFeatureFailureSafety();
	TestBoxContactRegression();
	TestBoxFaceContactGeometry();
	TestBoxFaceClippingBoundaries();
	TestBoxFaceManifoldWorld();
	TestBoxFaceRestingStability();
	TestManifoldPersistence();
	TestBoxPatchContinuity();
	TestPatchOrderSolverEquivalence();
	TestPersistenceBasisAndGuards();
	TestFeaturePatchPersistence();
	TestPersistenceLifetimeAndQueryMetadata();
	TestSolverIterationConfiguration();
	TestSolverIterationTraversal();
	TestSmallBoxStackRegression();
	TestPositionStabilization();
	TestPositionStabilizationWorld();
	TestPositionCorrectionGuardsAndFriction();
	TestPenetratedStackPositionStabilization();
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
	TestSceneClockPolicy();
	TestPausedScenePoseSynchronization();
	TestSceneScheduleEquivalence();
	TestExactBoxWorldTimestepStability();
	TestManifoldNormalConvergence();
	TestContactFrictionConvergence();
	TestRestingCoulombProjection();
	TestFrictionWarmStartAndRetraction();
	TestLargeFiniteFrictionCoefficients();
	TestFrictionSlidingAndRolling();
	TestGravityAndInverseMass();
	TestContactIslandConnectivity();
	TestContactIslandGraphOracle();
	TestContactIslandStorageRebuild();
	TestContactIslandIdentityLifetime();
	TestContactIslandSystemLifecycle();
	TestRollingResistanceRows();
	TestCombinedAngularResistanceAnisotropy();
	TestRollingTwoBodiesAndAnisotropy();
	TestRollingMaterialAndWake();
	TestRollingPlaneAndAirborne();
	TestSpinResistanceRows();
	TestSpinResistanceTwoBodies();
	TestSpinMaterialAndWake();
	TestSupportedSpinAndAirborne();
	TestIslandSleepAndWake();
	TestSleepWakeSourcesAndBoundaries();
	TestSleepEligibilityAndInteraction();
	TestSleepSupportAndDeterminism();
	TestSleepSettingsAndTimer();
	TestSleepMotionAndEligibility();
	TestSleepPhysicalStateAndLifetime();
	TestBodyTypeInvariants();
	TestBodyTypeContacts();
	TestBodyTypePrediction();
	TestToiMissCanBeRevived();
	TestToiGenericRevalidationFailureSafety();
	TestToiRescheduling();
	TestToiCurrentGeometry();
	TestToiAfterRestingSolveAndSimultaneous();
	TestCollisionPredictionPurity();
	TestPredictionAnchorsAndOrder();
	TestBodyTypeConfigurationAndTransitions();
	TestBodyTypeWorldContacts();
	TestAngularSweepContainment();
	TestAngularSweepCandidatesAndGuards();
	CheckAngularSpeedFrames<GEngine::ShapeBox>();
	CheckAngularSpeedFrames<GEngine::ShapeConvex>();
	TestRotatingLongBoxCollision();
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
