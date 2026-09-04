#pragma once
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/quaternion_float.hpp>
#include "../Core/Base.h"
#include "Math/Math.h"
#include"Component/Component.h"
#include "Bounds.h"
#include <cstdint>

namespace GEngine
{


	class PhysicalShape;
	class PhysicsWorld;

	class RigidBodyIdentity
	{
	public:
		constexpr RigidBodyIdentity() = default;
		constexpr bool IsValid() const { return m_Slot != 0 && m_Generation != 0; }
		constexpr std::uint64_t GetSlot() const { return m_Slot; }
		constexpr std::uint64_t GetGeneration() const { return m_Generation; }

		friend constexpr bool operator==(const RigidBodyIdentity&, const RigidBodyIdentity&) = default;

	private:
		constexpr RigidBodyIdentity(std::uint64_t slot, std::uint64_t generation)
			: m_Slot(slot), m_Generation(generation) {}

		std::uint64_t m_Slot{};
		std::uint64_t m_Generation{};

		friend class PhysicsWorld;
	};

	
	using namespace Component;
	using namespace Math;

	class RigidBody3D
	{
	public:
		RigidBody3D() = default;
		Vec3f GetCenterOfMassWorldSpace() const;
		Vec3f GetCenterOfMassModelSpace() const;
		Vec3f WorldSpaceToBodySpace(const Vec3f& pt) const;
		Vec3f BodySpaceToWorldSpace(const Vec3f& pt) const;
		Mat3 GetInverseInertiaTensorBodySpace() const;
		Mat3 GetInverseInertiaTensorWorldSpace() const;
		const Mat3& GetBodyToWorldRotation() const;
		const Mat3& GetWorldToBodyRotation() const;
		const Bounds& GetWorldBounds() const;
		RigidBodyIdentity GetIdentity() const { return m_Identity; }

		void ApplyImpulse(const Vec3f& impulsePoint, const Vec3f& impulse);
		void ApplyImpulseLinear(const Vec3f& impulse);
		void ApplyImpulseAngular(const Vec3f& impulse);

		void Update(const float dt_sec);
		bool HasFiniteState() const;
	#ifdef GENGINE_CONFIG_DEBUG
		void AssertFiniteState() const;
	#else
		void AssertFiniteState() const {}
	#endif
	
		Vec3f m_Position{ 0.f };
		Quat m_Orientation{ 1.0f, 0.f, 0.f, 0.f };

		Vec3f m_LinearVelocity{ 0.f };
		Vec3f m_AngularVelocity{ 0.f };

		float		m_InvMass = 1.f;
		float		m_Elasticity = 1.f;
		float		m_Friction = 0.f;
		std::uint32_t m_CollisionLayer = 1u;
		std::uint32_t m_CollisionMask = ~std::uint32_t{ 0 };
		PhysicalShape* m_Shape{};
		BodyType Type = BodyType::Static;

	private:
		RigidBodyIdentity m_Identity;

		void UpdateRotationData() const;
		void UpdateCenterOfMassData() const;
		void UpdateBodyInertiaData() const;
		void UpdateWorldInertiaData() const;
		void UpdateBoundsData() const;

		struct DerivedData
		{
			Quat rotationSourceOrientation{ 1.0f, 0.0f, 0.0f, 0.0f };
			Mat3 bodyToWorld{ 1.0f };
			Mat3 worldToBody{ 1.0f };
			std::uint64_t rotationRevision{};
			bool rotationValid{};

			Vec3f centerSourcePosition{};
			const PhysicalShape* centerSourceShape{};
			std::uint64_t centerSourceShapeRevision{};
			std::uint64_t centerSourceRotationRevision{};
			Vec3f centerOfMassWorld{};
			bool centerValid{};

			float inertiaSourceInverseMass{};
			const PhysicalShape* inertiaSourceShape{};
			std::uint64_t inertiaSourceShapeRevision{};
			Mat3 inertiaBody{ 0.0f };
			Mat3 inverseInertiaBody{ 0.0f };
			std::uint64_t bodyInertiaRevision{};
			bool bodyInertiaValid{};

			std::uint64_t worldInertiaSourceBodyRevision{};
			std::uint64_t worldInertiaSourceRotationRevision{};
			Mat3 inverseInertiaWorld{ 0.0f };
			Mat3 inertiaWorld{ 0.0f };
			bool worldInertiaValid{};

			Vec3f boundsSourcePosition{};
			const PhysicalShape* boundsSourceShape{};
			std::uint64_t boundsSourceShapeRevision{};
			std::uint64_t boundsSourceRotationRevision{};
			Bounds worldBounds{};
			bool boundsValid{};
		};

		mutable DerivedData m_DerivedData;

		friend class PhysicsWorld;

	};
}
