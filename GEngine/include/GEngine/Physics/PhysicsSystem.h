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
                DebugGeometryResult collider;
                if (shape.GetShapeType()==ShapeType::Sphere)
                    collider=out.Sphere(body->m_Position,static_cast<const ShapeSphere&>(shape).GetRadius(),{0,1,1,1});
                else if (shape.GetShapeType()==ShapeType::Capsule) {
                    const auto& capsule=static_cast<const ShapeCapsule&>(shape);
                    const float radius=capsule.GetRadius(), half=capsule.GetHalfLength();
                    // Two meridian outlines and both cap seams, using the same
                    // dimensions as Support(). Append once: capacity is atomic.
                    std::array<FrameDebugLine,100> lines; std::size_t count=0;
                    const auto append=[&](Vec3f a,Vec3f b) {
                        lines[count++]={body->m_Position+body->m_Orientation*a,
                            body->m_Position+body->m_Orientation*b,{0,1,1,1},{},DebugDepth::Tested};
                    };
                    for (int axis : {0,2}) {
                        for (float sign : {-1.f,1.f}) {
                            for (unsigned i=0;i<16;++i) {
                                const auto point=[&](unsigned j) {
                                    const float angle=float(j)*Math::Pi/16.f;
                                    Vec3f p(0); p[axis]=radius*std::cos(angle);
                                    p.y=sign*(half+radius*std::sin(angle)); return p;
                                };
                                append(point(i),point(i+1));
                            }
                            Vec3f a(0),b(0); a[axis]=b[axis]=sign*radius;
                            a.y=-half; b.y=half; append(a,b);
                        }
                    }
                    for (float sign : {-1.f,1.f}) for (unsigned i=0;i<16;++i) {
                        const auto point=[&](unsigned j) {
                            const float angle=float(j%16)*Math::TwoPi/16.f;
                            return Vec3f(radius*std::cos(angle),sign*half,radius*std::sin(angle));
                        };
                        append(point(i),point(i+1));
                    }
                    collider=out.Append(lines);
                }
                else if (shape.GetShapeType()==ShapeType::Convex) {
                    const auto& convex=static_cast<const ShapeConvex&>(shape);
                    collider=out.IndexedLines(convex.GetPoints(),convex.GetBoundaryEdges(),
                        body->m_Position,body->m_Orientation,{0,1,1,1});
                }
                else {
                    const auto bounds=shape.GetBounds();
                    collider=out.Box(bounds.mins,bounds.maxs,body->m_Position,body->m_Orientation,
                        shape.GetShapeType()==ShapeType::Box?glm::vec4(0,1,1,1):glm::vec4(1,0,1,1));
                }
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
