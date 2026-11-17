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
#include <expected>
#include <optional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <chrono>



namespace GEngine
{
#ifdef GENGINE_PHASE26_QUALIFICATION
    // Isolated observer only: complete Update lifetime, with common parity capture
    // outside the timing boundary. No ordinary build API/callback is added.
    namespace Phase26Observation {
        struct Observer { void* context{}; void (*begin)(void*) noexcept{};
            void (*end)(void*,std::uint64_t) noexcept{}; };
        inline thread_local Observer* observer{};
        inline thread_local bool jointHot{};
        struct JointScope {
            bool previous=jointHot;
            JointScope(){jointHot=true;}
            ~JointScope(){jointHot=previous;}
        };
        class CompleteStep {
            Observer* sink=observer;
            std::chrono::steady_clock::time_point start;
        public:
            CompleteStep(){if(sink)sink->begin(sink->context);start=std::chrono::steady_clock::now();}
            ~CompleteStep(){const auto end=std::chrono::steady_clock::now();
                if(sink)sink->end(sink->context,std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count());}
        };
    }
#endif
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

    enum class ColliderAdmissionOutcome { NoAddedOverlap, RejectedRisk, Indeterminate };
    enum class ColliderAdmissionError { None, Unassessed, InvalidIdentity, Busy, UnsupportedShape, InvalidShape, AllocationFailure, BodyLimit, QueryLimit, StorageLimit, Deadline, Unproved, StaleConfirmation };
    enum class ColliderPairOutcome { NotRequired, SeparatedByBounds, Contact, SeparatedQuery, Failed, Unsupported };
    struct ColliderAdmissionPair
    {
        PhysicsBodyHandle other{};
        ColliderPairOutcome oldOutcome{ColliderPairOutcome::NotRequired}, newOutcome{ColliderPairOutcome::NotRequired};
        const char* reason{"unassessed"};
        const char* method{"none"};
        int oldTermination{}, newTermination{};
        bool comparableMeasure{};
        double oldMeasure{}, newMeasure{};
    };
    // Empty/error construction and ownership moves allocate nothing, including in
    // checked-iterator builds. Detailed storage is bounded, explicit and fallible.
    class ColliderAdmissionPairs
    {
    public:
        ColliderAdmissionPairs() noexcept = default;
        ColliderAdmissionPairs(const ColliderAdmissionPairs&) = delete;
        ColliderAdmissionPairs& operator=(const ColliderAdmissionPairs&) = delete;
        ColliderAdmissionPairs(ColliderAdmissionPairs&& other) noexcept
            : m_Data(std::move(other.m_Data)), m_Size(std::exchange(other.m_Size,0)),
              m_Capacity(std::exchange(other.m_Capacity,0)) {}
        ColliderAdmissionPairs& operator=(ColliderAdmissionPairs&& other) noexcept
        {
            if(this!=&other){m_Data=std::move(other.m_Data);m_Size=std::exchange(other.m_Size,0);m_Capacity=std::exchange(other.m_Capacity,0);}return *this;
        }
        [[nodiscard]] std::expected<void,ColliderAdmissionError> TryReserve(std::size_t count) noexcept
        {
            if(count<=m_Capacity)return {};
            if(count>4096||m_Size)return std::unexpected(ColliderAdmissionError::StorageLimit);
            static_assert(std::is_nothrow_default_constructible_v<ColliderAdmissionPair>);
            std::unique_ptr<ColliderAdmissionPair[]> owned(new(std::nothrow) ColliderAdmissionPair[count]);
            if(!owned)return std::unexpected(ColliderAdmissionError::AllocationFailure);
            m_Data=std::move(owned);m_Capacity=count;return {};
        }
        [[nodiscard]] bool TryAppend(const ColliderAdmissionPair& value) noexcept
        {if(m_Size==m_Capacity)return false;m_Data[m_Size++]=value;return true;}
        const ColliderAdmissionPair* begin() const noexcept{return m_Data.get();}
        const ColliderAdmissionPair* end() const noexcept{return m_Size?m_Data.get()+m_Size:m_Data.get();}
        std::size_t size() const noexcept{return m_Size;}
        bool empty() const noexcept{return m_Size==0;}
    private:
        std::unique_ptr<ColliderAdmissionPair[]> m_Data;
        std::size_t m_Size{},m_Capacity{};
    };
    using ColliderAdmissionDescriptor = std::variant<BoxShapeDesc,SphereShapeDesc,CapsuleShapeDesc>;
    struct ColliderAdmissionReport
    {
        ColliderAdmissionReport() noexcept = default;
        ColliderAdmissionReport(const ColliderAdmissionReport&) = delete;
        ColliderAdmissionReport& operator=(const ColliderAdmissionReport&) = delete;
        ColliderAdmissionReport(ColliderAdmissionReport&&) noexcept = default;
        ColliderAdmissionReport& operator=(ColliderAdmissionReport&&) noexcept = default;
        ColliderAdmissionOutcome outcome{ColliderAdmissionOutcome::Indeterminate};
        ColliderAdmissionError error{ColliderAdmissionError::Unassessed};
        // Only admitted fixed-size descriptors can enter a report. A caller
        // cannot introduce an owning Convex vector into its no-heap move path.
        ColliderAdmissionDescriptor descriptor{};
        [[nodiscard]] bool SetDescriptor(const CollisionShapeDesc& source) noexcept
        {
            if(auto* value=std::get_if<BoxShapeDesc>(&source)){descriptor=*value;return true;}
            if(auto* value=std::get_if<SphereShapeDesc>(&source)){descriptor=*value;return true;}
            if(auto* value=std::get_if<CapsuleShapeDesc>(&source)){descriptor=*value;return true;}
            return false;
        }
        PhysicsBodyHandle body{};
        bool complete{}, committed{};
        const char* reason{"unassessed"};
        ColliderAdmissionPairs pairs;
        std::size_t assessedBodies{}, candidatePairs{}, queryCount{}, trackedPeakBytes{}, temporaryAllocations{};
        double elapsedMs{}, certificateMargin{};
    };

    struct ColliderShapeState
    {
        ShapeType type{ShapeType::Invalid};
        ColliderAdmissionDescriptor dimensions{};
        bool editable{};
        std::uintptr_t identity{};
        std::uint64_t revision{};
    };

    // Opaque, move-only consent context. Exact world values, not a hash or a
    // cached safe verdict. No live pointers are exposed or dereferenced later.
    class ColliderReplacementConfirmation
    {
    public:
        ColliderReplacementConfirmation() noexcept = default;
        ColliderReplacementConfirmation(ColliderReplacementConfirmation&&) noexcept = default;
        ColliderReplacementConfirmation& operator=(ColliderReplacementConfirmation&&) noexcept = default;
    private:
        struct BodyContext
        {
            PhysicsBodyHandle handle{};
            std::uintptr_t shape{};
            std::uint64_t revision{};
            ShapeType shapeType{ShapeType::Invalid};
            Vec3f position{}, linear{}, angular{}, minimum{}, maximum{};
            Quat orientation{};
            float inverseMass{};
            Component::BodyType type{};
            std::uint32_t layer{}, mask{};
            friend bool operator==(const BodyContext&, const BodyContext&) = default;
        };
        PhysicsBodyHandle m_Body{};
        ColliderAdmissionDescriptor m_Descriptor{};
        std::uint64_t m_Revision{};
        std::unique_ptr<BodyContext[]> m_Bodies;
        std::size_t m_Count{};
        friend class PhysicsSystem;
    };

	class PhysicsSystem : public System
	{
#ifdef GENGINE_PHASE27_QUALIFICATION
        friend struct Phase27GraphAccess;
#endif
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
        [[nodiscard]] PhysicsBodyResult<bool> SetBodyScale(PhysicsBodyHandle, const Vec3f&, const Vec3f& referenceScale=Vec3f(1));
        struct BodyTransformEdit
        {
            PhysicsBodyHandle body;
            Vec3f position{}; Quat rotation{1,0,0,0};
            std::optional<Vec3f> scale;
            Vec3f referenceScale{1};
        };
        // Bounded owner-thread transaction: prepare every geometry/wake allocation
        // before touching any body. Commit preserves handles, velocities and joints.
        [[nodiscard]] PhysicsBodyResult<> SetBodyTransforms(std::span<const BodyTransformEdit>);
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
        // Owner thread, between updates. Preview grants no later commit authority.
        [[nodiscard]] ColliderAdmissionReport AssessBodyColliderReplacement(PhysicsBodyHandle,
            const CollisionShapeDesc&, std::unique_ptr<PhysicalShape>* preview = nullptr) const;
        [[nodiscard]] ColliderAdmissionReport ReplaceBodyColliderIfNoAddedOverlap(PhysicsBodyHandle, const CollisionShapeDesc&);
        [[nodiscard]] PhysicsBodyResult<ColliderShapeState> ReadBodyCollider(PhysicsBodyHandle) const;
        // Cold authoring snapshot: owns convex vertices; no borrowed runtime shape escapes.
        [[nodiscard]] PhysicsBodyResult<CollisionShapeDesc> CopyBodyShapeDescriptor(PhysicsBodyHandle) const;
        static bool CanConfirmColliderRisk(const ColliderAdmissionReport&) noexcept;
        [[nodiscard]] std::expected<ColliderReplacementConfirmation, ColliderAdmissionError>
        PrepareColliderConfirmation(const ColliderAdmissionReport&) const;
        [[nodiscard]] bool IsColliderConfirmationCurrent(const ColliderReplacementConfirmation&,
            PhysicsBodyHandle, const CollisionShapeDesc&) const;
        [[nodiscard]] ColliderAdmissionReport ReplaceBodyColliderConfirmed(PhysicsBodyHandle,
            const CollisionShapeDesc&, const ColliderReplacementConfirmation&);

        // Retained Component compatibility: shape rejection does not reject Transform storage.
        void ApplyLegacyBodyScale(PhysicsBodyHandle, const Vec3f&);
        [[nodiscard]] DebugGeometryResult AppendPresentationGeometry(DebugGeometry&, PhysicsBodyHandle,
            const Vec3f& position, const Quat& orientation) const;
        [[nodiscard]] static DebugGeometryResult AppendColliderPreview(DebugGeometry& out,
            const PhysicalShape& shape, const Vec3f& position, const Quat& orientation, const glm::vec4& color)
        { return AppendColliderGeometry(out, shape, position, orientation, color); }
        std::uint64_t ColliderContextRevision() const { return m_ColliderContextRevision; }

        // Application commands at the existing fixed-tick boundary; never advances a clock.
        // Installed between updates, cleared before world teardown.
        void SetBeforeStep(std::function<void(PhysicsWorld&,std::uint64_t)> callback) { m_BeforeStep=std::move(callback); }
        std::uint64_t GetCompletedSteps() const { return m_CompletedSteps; }
        std::uint64_t GetJointSolveNanoseconds() const { return m_JointSolveNs; }
        std::uint64_t GetLastUpdateNanoseconds() const { return m_InclusiveNs; }
        std::uint64_t GetContactSolveNanoseconds() const { return m_ContactSolveNs; }
        bool HasJointFailure() const { return m_JointFailure; }
        const std::optional<PhysicsBodyError>& GetBackendFailure() const noexcept { return m_BackendFailure; }
#ifdef GENGINE_PHASE26_QUALIFICATION
        std::size_t QualificationJointStorageBytes() const {
            return sizeof(m_JointBlock)+sizeof(m_JointEdges)+
                (m_PhysicsWorld && m_PhysicsWorld->m_Joints?PhysicsWorld::MaxDistanceJoints*sizeof(PhysicsWorld::JointSlot):0);
        }
#endif

		static constexpr int MinSolverIterations = 1;
		static constexpr int MaxSolverIterations = 32;
		static constexpr int DefaultSolverIterations = 1;

		// Per-system policy, retained across world replacement. Invalid requests leave it unchanged.
		// Changes take effect on the next Update; configuration and stepping are single-threaded.
		bool SetSolverIterations(int iterations) noexcept
		{
			if (m_PhysicsWorld && m_PhysicsWorld->GetBackend()==PhysicsBackend::Jolt) return false;
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

        bool SupportsContactDiagnostics() const noexcept { return !m_PhysicsWorld || m_PhysicsWorld->GetBackend()==PhysicsBackend::Native; }

        struct DebugLayers
        {
            bool colliders{true}, worldAabbs{}, contactPoints{}, contactNormals{}, bodyAxes{};
            bool Any() const { return colliders || worldAabbs || contactPoints || contactNormals || bodyAxes; }
        };

        // Scope filters emission only. Retained contacts are visited exactly once;
        // their stored world B-to-A normal never changes with selection order.
        [[nodiscard]] DebugGeometryResult AppendDebugGeometry(DebugGeometry& out,
            const DebugLayers& layers, std::span<const PhysicsBodyHandle> selected, bool all)
        {
            if (!m_PhysicsWorld || !layers.Any() || (!all && selected.empty())) return {};
            const auto includes = [&](RigidBodyIdentity identity)
            {
                if (all) return true;
                const auto handle = m_PhysicsWorld->HandleOf(identity);
                for (const auto member : selected) if (member == handle) return true;
                return false;
            };
            if (layers.colliders || layers.worldAabbs || layers.bodyAxes)
                for (const auto* body : m_PhysicsWorld->GetPhysicsBodies())
                {
                    if (!body || !includes(body->GetIdentity()) || !body->m_Shape || !body->m_Shape->IsValid()) continue;
                    if (layers.colliders)
                        if (auto r = AppendColliderGeometry(out, *body->m_Shape, body->m_Position,
                                body->m_Orientation, {0, 1, 1, 1}); !r) return r;
                    if (layers.worldAabbs)
                    {
                        const auto& bounds = body->GetWorldBounds();
                        if (auto r = out.Box(bounds.mins, bounds.maxs, {}, glm::quat(1, 0, 0, 0),
                                {1, 1, 0, .6f}); !r) return r;
                    }
                    if (layers.bodyAxes)
                        if (auto r = out.Axes(body->GetCenterOfMassWorldSpace(), body->m_Orientation, .2f); !r) return r;
                }
            if (layers.contactPoints || layers.contactNormals)
                for (auto& manifold : m_Manifolds.m_Manifolds)
                {
                    if (!m_PhysicsWorld->IsBodyIdentityValid(manifold.GetBodyAIdentity()) ||
                        !m_PhysicsWorld->IsBodyIdentityValid(manifold.GetBodyBIdentity()) ||
                        (!includes(manifold.GetBodyAIdentity()) && !includes(manifold.GetBodyBIdentity()))) continue;
                    for (int i = 0; i < manifold.GetNumContacts(); ++i)
                    {
                        const auto contact = manifold.GetContact(i);
                        const auto a = contact.m_BodyA->BodySpaceToWorldSpace(contact.ptOnA_LocalSpace);
                        const auto b = contact.m_BodyB->BodySpaceToWorldSpace(contact.ptOnB_LocalSpace);
                        std::array<FrameDebugLine, 5> lines;
                        std::size_t count = 0;
                        if (layers.contactPoints)
                            lines[count++] = {a, b, {1, .25f, 0, 1}, {}, DebugDepth::Overlay};
                        if (layers.contactNormals)
                            lines[count++] = {a, a + contact.normal * .4f, {1, .4f, 0, 1}, {}, DebugDepth::Overlay};
                        if (layers.contactPoints)
                            for (unsigned axis = 0; axis < 3; ++axis)
                            {
                                Vec3f extent(0); extent[axis] = .025f;
                                lines[count++] = {a - extent, a + extent, {1, 1, 1, 1}, {}, DebugDepth::Overlay};
                            }
                        if (auto r = out.Append({lines.data(), count}); !r) return r;
                    }
                }
            if(layers.colliders || layers.bodyAxes) return AppendJointGeometry(out,selected,all);
            return {};
        }

        [[nodiscard]] DebugGeometryResult AppendJointGeometry(DebugGeometry& out,
            std::span<const PhysicsBodyHandle> selected={},bool all=true) const
        {
            if(!m_PhysicsWorld) return {};
            for(std::size_t i=0;i<m_PhysicsWorld->GetJointCount();++i) {
                auto h=m_PhysicsWorld->JointAt(i); if(!h) return std::unexpected(DebugGeometryCode::InvalidInput);
                auto j=m_PhysicsWorld->ReadJoint(*h); if(!j) return std::unexpected(DebugGeometryCode::InvalidInput);
                if(!all && std::find(selected.begin(),selected.end(),j->descriptor.bodyA)==selected.end() &&
                    std::find(selected.begin(),selected.end(),j->descriptor.bodyB)==selected.end()) continue;
                auto a=m_PhysicsWorld->ReadBody(j->descriptor.bodyA),b=m_PhysicsWorld->ReadBody(j->descriptor.bodyB);
                if(!a || !b)return std::unexpected(DebugGeometryCode::InvalidInput);
                if(j->descriptor.kind==JointKind::BallSocket) {
                    const glm::vec4 colorA{.1f,.85f,1.f,1.f},colorB{1.f,.55f,.1f,1.f};
                    // Different marker shapes remain legible when the anchors coincide.
                    if(auto r=out.Sphere(j->worldAnchorA,.065f,colorA,DebugDepth::Overlay);!r)return r;
                    for(unsigned axis=0;axis<3;++axis) {
                        Vec3f extent(0);extent[axis]=.12f;
                        if(auto r=out.Line(j->worldAnchorB-extent,j->worldAnchorB+extent,colorB,DebugDepth::Overlay);!r)return r;
                    }
                    if(auto r=out.Line(j->worldAnchorA,j->worldAnchorB,{1,.2f,.8f,1},DebugDepth::Overlay);!r)return r;
                    if(auto r=out.Line(a->centerOfMass,j->worldAnchorA,colorA,DebugDepth::Overlay);!r)return r;
                    if(auto r=out.Line(b->centerOfMass,j->worldAnchorB,colorB,DebugDepth::Overlay);!r)return r;
                    for(unsigned side=0;side<2;++side) {
                        const auto shape=ReadBodyCollider(side?j->descriptor.bodyB:j->descriptor.bodyA);
                        if(shape)if(const auto* sphere=std::get_if<SphereShapeDesc>(&shape->dimensions))
                            if(auto r=out.Sphere(side?b->position:a->position,sphere->radius,side?colorB:colorA,DebugDepth::Overlay);!r)return r;
                    }
                    continue;
                }
                const bool asleep=(a->inverseMass==0 || a->sleeping) && (b->inverseMass==0 || b->sleeping);
                const glm::vec4 color=std::abs(j->error)>.005f*(j->descriptor.kind==JointKind::Distance?j->descriptor.targetDistance:
                    glm::length(j->descriptor.localAnchorA)+glm::length(j->descriptor.localAnchorB)) ? glm::vec4(1,.2f,0,1) :
                    asleep?glm::vec4(.25f,.5f,1,1):glm::vec4(.2f,1,.2f,1);
                if(auto r=out.Line(j->worldAnchorA,j->worldAnchorB,color,DebugDepth::Overlay);!r)return r;
                if(auto r=out.Axes(j->worldAnchorA,a->orientation,.15f);!r)return r;
                if(auto r=out.Axes(j->worldAnchorB,b->orientation,.15f);!r)return r;
            }
            return {};
        }

        // Legacy global debug entry point preserves its layers and colors.
        [[nodiscard]] DebugGeometryResult AppendDebugGeometry(DebugGeometry& out)
        {
            return AppendDebugGeometry(out, {true, true, true, true, true}, {}, true);
        }


	private:

        static DebugGeometryResult AppendColliderGeometry(DebugGeometry&, const PhysicalShape&,
            const Vec3f&, const Quat&, const glm::vec4&);
        void InvalidateBodyContacts(RigidBody3D*);
        void CommitStagedCollider(PhysicsBodyHandle, RigidBody3D*, std::unique_ptr<PhysicalShape>&, bool preparedWake = false);
        ColliderAdmissionReport AssessStagedCollider(PhysicsBodyHandle, const CollisionShapeDesc&, RigidBody3D*, PhysicalShape&) const;
        ColliderAdmissionReport ReplaceBodyColliderWithAdmission(PhysicsBodyHandle,
            const CollisionShapeDesc&, const ColliderReplacementConfirmation*);
        ColliderReplacementConfirmation::BodyContext ColliderContext(const RigidBody3D&) const;
        std::uint64_t m_ColliderContextRevision{};
#ifdef GENGINE_COLLIDER_ADMISSION_VALIDATION
        friend struct ColliderAdmissionCheckAccess;
        bool m_AdmissionTestExpired{};
        int m_AdmissionTestQueryFailureReason{};
        std::size_t m_AdmissionTestBodyLimit{4096}, m_AdmissionTestQueryLimit{8190}, m_AdmissionTestStorageLimit{4194304};
#endif


        std::optional<PhysicsBodyError> m_BackendFailure;
        std::function<void(PhysicsWorld&,std::uint64_t)> m_BeforeStep;
        std::uint64_t m_CompletedSteps{}, m_JointSolveNs{}, m_InclusiveNs{}, m_ContactSolveNs{};
        bool m_JointFailure{};
        std::array<std::array<RigidBodyIdentity,2>,PhysicsWorld::MaxDistanceJoints> m_JointEdges{};
        std::size_t m_JointEdgeCount{};
        std::span<const std::array<RigidBodyIdentity,2>> JointEdges();
        // Reused scalar storage for a banded chain block in the existing solve pass.
        // No second solver, extra iterations, pose projection or hot allocation.
        struct JointBlockEntry {
            DistanceJointRow* row{};
            double diagonal{}, offDiagonal{}, rhs{}, delta{};
        };
        std::array<JointBlockEntry,PhysicsWorld::MaxDistanceJoints> m_JointBlock{};
        bool SolveDistanceJoints();
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
