#pragma once

#include "Core/System.h"
#include <Math/Math.h>
#include "Manifold.h"
#include "Contact.h"
#include "Broadphase.h"
#include "ContactIsland.h"
#include "PhysicsWorld.h"
#include "ShapeSphere.h"
#include "ShapeConvex.h"
#include "ShapeCapsule.h"
#include "Renderer/DebugGeometry.h"



namespace GEngine
{
	class RigidBody3D;

	//using namespace Math;

	//struct contact_t {
	//	Vec3f ptOnA_WorldSpace;
	//	Vec3f ptOnB_WorldSpace;
	//	Vec3f ptOnA_LocalSpace;
	//	Vec3f ptOnB_LocalSpace;

	//	Vec3f normal;	// In World Space coordinates
	//	float separationDistance;	// positive when non-penetrating, negative when penetrating
	//	float timeOfImpact;

	//	RigidBody3D* bodyA{};
	//	RigidBody3D* bodyB{};
	//};

	class PhysicsWorld;

    struct PhysicsBodyPose
    {
        Component::BodyType type;
        Vec3f position;
        Quat orientation;
    };

	class PhysicsSystem : public System
	{
		// Inherited via System
	public:
		// Last Update only, in nanoseconds; zero when physics profiling is disabled.
		// Wake time includes the conservative activation graph. Other fields exclude it.
		struct SleepTimings
		{
			std::uint64_t sourceCheckNs{}, wakeNs{}, depthNs{}, eligibilityNs{};
			std::uint64_t qualificationNs{}, sourceStoreNs{}, stepFlagsNs{};
		};
		const SleepTimings& GetSleepTimings() const { return m_SleepTimings; }
		virtual ~PhysicsSystem();
		void Initialize() override;
		void Update(Timestep ts) override;
		void OnExit() override;
		void SetPhysicsWorld(PhysicsWorld* physics_world);
		PhysicsWorld* GetPhysicsWorld() { return m_PhysicsWorld; }

		// Between updates only: teleport an owned body, preserving both velocities.
		// Reject invalid poses transactionally; accepted quaternions are normalized.
		// Changed poses retire this body's contacts and refresh broad-phase state.
		bool SetBodyPose(RigidBody3D* body, const Vec3f& position, const Quat& orientation);

        // Normal Scene callers retain handles and values, never solver pointers.
        [[nodiscard]] PhysicsBodyResult<PhysicsBodyPose> ReadBodyPose(PhysicsBodyHandle) const;
        [[nodiscard]] PhysicsBodyResult<bool> SetBodyPose(PhysicsBodyHandle, const Vec3f&, const Quat&);
        [[nodiscard]] PhysicsBodyResult<bool> SetBodyScale(PhysicsBodyHandle, const Vec3f&);
        // Explicit runtime collider replacement; unit-scale description becomes the new base.
        [[nodiscard]] PhysicsBodyResult<> ReplaceBodyCollider(PhysicsBodyHandle, const CollisionShapeDesc&);
#ifdef GENGINE_PHASE24_COLLIDER_VALIDATION
#ifndef GENGINE_PHASE24_ISOLATED_BUILD
#error Collider diagnostics require an explicitly isolated qualification build.
#endif
        // Read-only diagnostic snapshot. In particular, do not call lazy cache
        // getters: observing intermediate replacements must not repair them.
        struct ColliderCacheInspection
        {
            const PhysicalShape *shape{}, *owner{}, *inertiaShape{}, *boundsShape{};
            std::uint64_t revision{}, inertiaRevision{}, boundsRevision{};
            bool inertiaValid{}, boundsValid{};
            Mat3 inverseInertiaBody{}, inverseInertiaWorld{};
            Bounds bounds;
        };
        [[nodiscard]] PhysicsBodyResult<ColliderCacheInspection> InspectColliderCache(PhysicsBodyHandle h) const
        {
            if(!m_PhysicsWorld)return std::unexpected(PhysicsBodyError{PhysicsBodyErrorCode::Closed,h,"collider diagnostic"});
            auto body=m_PhysicsWorld->Resolve(h,"collider diagnostic");if(!body)return std::unexpected(body.error());
            const auto& b=**body;const auto& c=b.m_DerivedData;
            return ColliderCacheInspection{b.m_Shape,m_PhysicsWorld->m_OwnedResources[h.index].shape.get(),
                c.inertiaSourceShape,c.boundsSourceShape,b.m_Shape->GetRevision(),c.inertiaSourceShapeRevision,
                c.boundsSourceShapeRevision,c.bodyInertiaValid,c.boundsValid,c.inverseInertiaBody,
                c.inverseInertiaWorld,c.worldBounds};
        }
        friend struct Phase24ColliderCheckAccess;
#endif
        // Retained Component compatibility: shape rejection does not reject Transform storage.
        void ApplyLegacyBodyScale(PhysicsBodyHandle, const Vec3f&);
        [[nodiscard]] DebugGeometryResult AppendPresentationGeometry(DebugGeometry&, PhysicsBodyHandle,
            const Vec3f& position, const Quat& orientation) const;

		static constexpr int MinSolverIterations = 1;
		static constexpr int MaxSolverIterations = 32;
		static constexpr int DefaultSolverIterations = 1;

		// Per-system policy, retained across world replacement. Invalid requests leave it unchanged.
		// Changes take effect on the next Update; configuration and stepping are single-threaded.
		bool SetSolverIterations(int iterations) noexcept
		{
			if (iterations < MinSolverIterations || iterations > MaxSolverIterations) return false;
			m_SolverIterations = iterations;
			return true;
		}
		int GetSolverIterations() const noexcept { return m_SolverIterations; }

		// Disabling sleeping restores the fully active reference traversal on the next tick.
		// This policy is retained across world replacement.
		void SetSleepingEnabled(bool enabled) { m_SleepingEnabled = enabled; }
		bool IsSleepingEnabled() const { return m_SleepingEnabled; }

		// Snapshot of the last resting solve after PreSolve validation, before integration.
		// Rebuilt each Update; cleared by body removal, changed SetBodyPose, and world reset.
		// Creation/direct body mutation is reflected at the next Update. This is not a
		// final-pose/CCD graph. Sleep decisions use its dynamic groups after integration.
		const std::vector<ContactIsland>& GetContactIslands() const { return m_ContactIslands; }

        // Owner thread, between updates. No stepping or cache/body mutation.
        // Cyan: authoritative sphere/box/convex/capsule collider; yellow: Physics world
        // AABB (possibly conservative); RGB: COM axes. Contact anchors follow current poses;
        // the labelled world B->A normal is from the last solve, not a new query.
        [[nodiscard]] DebugGeometryResult AppendDebugGeometry(DebugGeometry& out)
        {
            if (!m_PhysicsWorld) return {};
            for (const auto* body:m_PhysicsWorld->GetPhysicsBodies())
            {
                if (!body || !body->m_Shape || !body->m_Shape->IsValid()) continue;
                const auto& shape=*body->m_Shape;
                auto collider = AppendColliderGeometry(out, shape, body->m_Position, body->m_Orientation, {0,1,1,1});
                if (!collider) return collider;
                const auto& bounds=body->GetWorldBounds();
                if (auto r=out.Box(bounds.mins,bounds.maxs,{},glm::quat(1,0,0,0),{1,1,0,.6f});!r) return r;
                if (auto r=out.Axes(body->GetCenterOfMassWorldSpace(),body->m_Orientation,.2f);!r) return r;
            }
            for (auto& manifold:m_Manifolds.m_Manifolds)
            {
                if (!m_PhysicsWorld->IsBodyIdentityValid(manifold.GetBodyAIdentity()) ||
                    !m_PhysicsWorld->IsBodyIdentityValid(manifold.GetBodyBIdentity())) continue;
                for (int i=0;i<manifold.GetNumContacts();++i)
                {
                    const auto contact=manifold.GetContact(i);
                    const auto a=contact.m_BodyA->BodySpaceToWorldSpace(contact.ptOnA_LocalSpace);
                    const auto b=contact.m_BodyB->BodySpaceToWorldSpace(contact.ptOnB_LocalSpace);
                    // A contact is a point, not another collider. Three centered
                    // ticks preserve its extent/position without spending48 lines
                    // on a tiny sphere. Keep the full contact glyph atomic.
                    std::array<FrameDebugLine,5> lines{{
                        {a,b,{1,.25f,0,1},{},DebugDepth::Overlay},
                        {a,a+contact.normal*.4f,{1,.4f,0,1},{},DebugDepth::Overlay}}};
                    for (unsigned axis=0;axis<3;++axis) {
                        Vec3f extent(0);extent[axis]=.025f;
                        lines[axis+2]={a-extent,a+extent,{1,1,1,1},{},DebugDepth::Overlay};
                    }
                    if (auto r=out.Append(lines);!r) return r;
                }
            }
            return {};
        }

		

	private:

        static DebugGeometryResult AppendColliderGeometry(DebugGeometry&, const PhysicalShape&,
            const Vec3f&, const Quat&, const glm::vec4&);
        void InvalidateBodyContacts(RigidBody3D*);
		RigidBody3D* ResolveBody(RigidBodyIdentity identity) const;
		void WakeConnectedTo(RigidBody3D* body);
		SleepTimings m_SleepTimings;
		bool m_SleepingEnabled{ true };
		Vec3f m_LastSleepGravity{};
		bool m_HasSleepGravity{};
		std::vector<ContactIsland> m_ActivationIslands;
		int m_SolverIterations{ DefaultSolverIterations };
		PhysicsWorld* m_PhysicsWorld{};
		ManifoldCollector m_Manifolds;
		SweepAndPruneBroadphase m_Broadphase;
		std::vector<collisionPair_t> m_CollisionPairs;
		std::vector<contact_t> m_Contacts;
		std::vector<ContactIsland> m_ContactIslands;
		ContactIslandScratch m_ContactIslandScratch;

	};

	

	namespace Collision
	{
		bool SphereSphereIntersect(RigidBody3D* bodyA, RigidBody3D* bodyB, const float dt, contact_t& contact);
		bool Intersect(RigidBody3D* bodyA, RigidBody3D* bodyB, contact_t& contact);
		bool Intersect(RigidBody3D* bodyA, RigidBody3D* bodyB, const float dt, contact_t& contact);
		void ResolveContact(contact_t& contact);
		bool ConservativeAdvance(RigidBody3D* bodyA, RigidBody3D* bodyB, float dt, contact_t& contact);
	}

}
