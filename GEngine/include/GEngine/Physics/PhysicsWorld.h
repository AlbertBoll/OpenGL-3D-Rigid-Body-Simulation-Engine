#pragma once
#include <glm/ext/vector_float3.hpp>
#include <Math/Math.h>
#include <functional>
#include <vector>
#include "PhysicsBody.h"



namespace GEngine
{
	using namespace Math;
	class PhysicsWorld
	{
	public:
		~PhysicsWorld();
		void ConstructWorld();
		PhysicsWorld() = default;
		PhysicsWorld(const Vec3f& gravity);
		RigidBody3D* CreateRigidBody3D();
		void RemoveRigidBody3D(RigidBody3D* body);
		bool IsBodyIdentityValid(RigidBodyIdentity identity) const;
		void SetGravity(const Vec3f& gravity) { m_Gravity = gravity; }

		const std::vector<RigidBody3D*>& GetPhysicsBodies() const
		{
			return m_RigidBodies;
		}

		auto GetGravity()const { return m_Gravity; }

	private:
		using BodyRemovalCallback = std::function<void(RigidBody3D*)>;
		struct BodyIdentitySlot
		{
			RigidBody3D* body{};
			std::uint64_t generation{};
		};

		void SetBodyRemovalCallback(const BodyRemovalCallback& callback) { m_BodyRemovalCallback = callback; }
		void ReleaseBodyIdentity(RigidBody3D* body);

		Vec3f m_Gravity{ 0.f, -12.f, 0.f };
		std::vector<RigidBody3D*> m_RigidBodies;
		std::vector<BodyIdentitySlot> m_BodyIdentitySlots;
		std::vector<std::uint64_t> m_FreeBodyIdentitySlots;
		BodyRemovalCallback m_BodyRemovalCallback;

		friend class PhysicsSystem;
	};

}
