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

	void ConfigureBoxBody(GEngine::RigidBody3D& body, GEngine::ShapeBox& shape,
		const GEngine::Vec3f& position, const GEngine::Quat& orientation)
	{
		body.m_Shape = &shape;
		body.m_Position = position;
		body.m_Orientation = orientation;
		body.Type = GEngine::Component::BodyType::Dynamic;
		body.m_InvMass = 1.0f;
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
	TestGoldenRotations();
	TestRotatedAsymmetricBox();
	TestDerivedDataInvalidation();
	TestWarmCacheOrientationInvalidationAndReuse();
	TestSphereRadiusInvalidationContract();
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
