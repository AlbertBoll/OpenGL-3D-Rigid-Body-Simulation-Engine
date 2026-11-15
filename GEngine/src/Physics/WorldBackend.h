#pragma once
#include "Physics/PhysicsWorld.h"

namespace GEngine
{
    // Fully prepared, backend-neutral values. Publication never decomposes a
    // user tensor; rollback preserves the exact principal basis, not a refit.
    struct PreparedBackendMass
    {
        float inverseMass{};
        Vec3f inversePrincipal{};
        Quat principalRotation{1,0,0,0};
        friend bool operator==(const PreparedBackendMass&,const PreparedBackendMass&) = default;
    };
    static_assert(sizeof(PreparedBackendMass)==32);
    // Conservative simultaneous budget: sync records, forward/rollback property
    // records, collider payloads, plus padding allowance for live/staged bodies.
    // Each owning implementation also binds its actual record sizeof.
    static_assert((48+68+sizeof(PreparedBackendMass)+3*8)*PhysicsWorld::MaxImpulseBodies<=1024*1024);
#ifdef GENGINE_CHAIN_WAKE_CHECKS
    struct MassSynchronizationCheck
    {
        Vec3f position{},linearVelocity{},angularVelocity{},gravity{};
        Quat orientation{1,0,0,0};
        bool sleeping{};
        friend bool operator==(const MassSynchronizationCheck&,const MassSynchronizationCheck&) = default;
    };
#endif
    // Internal world strategy. Dispatch is at world/body/joint API boundaries;
    // no Jolt types or per-contact adapter objects cross this boundary.
    class WorldBackend
    {
    public:
        virtual ~WorldBackend() = default;
        virtual PhysicsBodyResult<> CreateBody(PhysicsBodyHandle, RigidBody3D&) = 0;
        virtual void DestroyBody(PhysicsBodyHandle) noexcept = 0;
        virtual PhysicsBodyResult<> Step(float, const Vec3f&, bool sleeping) = 0;
        virtual JointResult<> CreateJoint(PhysicsJointHandle, const DistanceJointDesc&) = 0;
        virtual void DestroyJoint(PhysicsJointHandle) noexcept = 0;
        virtual void ResetJoint(PhysicsJointHandle) noexcept = 0;
        virtual float JointImpulse(PhysicsJointHandle) const noexcept = 0;
        virtual bool JointActive(PhysicsJointHandle) const noexcept = 0;
        struct ColliderChange { virtual ~ColliderChange() = default; virtual void Commit() noexcept = 0; };
        virtual PhysicsBodyResult<std::unique_ptr<ColliderChange>> PrepareCollider(PhysicsBodyHandle, const PhysicalShape&) = 0;
        virtual PhysicsBodyResult<PreparedBackendMass> PrepareMass(PhysicsBodyHandle,const RigidBody3D&) const = 0;
        virtual PhysicsBodyResult<PreparedBackendMass> CaptureMass(PhysicsBodyHandle) const = 0;
#ifdef GENGINE_CHAIN_WAKE_CHECKS
        virtual PhysicsBodyResult<MassSynchronizationCheck> SnapshotSynchronizationForChecks(PhysicsBodyHandle) const = 0;
        virtual std::size_t SynchronizationStorageBytesForChecks() const noexcept = 0;
#endif
        virtual void UpdateProperties(PhysicsBodyHandle, const RigidBody3D&, const PhysicsBodyProperties&, const PreparedBackendMass&) noexcept = 0;
        virtual PhysicsBodyResult<PhysicsImpulseMotion> ReadImpulseMotion(PhysicsBodyHandle) const = 0;
        virtual void CommitImpulse(const PhysicsImpulseResult&) noexcept = 0;
        virtual void RestoreImpulseMotion(PhysicsBodyHandle,const PhysicsImpulseMotion&) noexcept = 0;
        // Identity/type only: legacy velocity/pose/mass writes may still be pending.
        virtual PhysicsBodyResult<> ValidateImpulseActivation(PhysicsBodyHandle,Component::BodyType) const = 0;
        virtual void CommitImpulseActivation(std::span<const PhysicsBodyHandle>) noexcept = 0;
        virtual void CommitPose(PhysicsBodyHandle, const RigidBody3D&) noexcept = 0;
        virtual void ReadProperties(PhysicsBodyHandle, PhysicsBodyProperties&) const noexcept = 0;
        virtual PhysicsJoltSettings ReadJoltSettings() const noexcept = 0;
        virtual PhysicsJoltSettings ReadJoltNativeDefaults() const noexcept = 0;
        virtual void UpdateJoltSettings(const PhysicsJoltSettings&) noexcept = 0;
        virtual PhysicsBodyResult<> EnableJoltDiagnostics(bool) = 0;
        virtual PhysicsJoltDiagnosticView ReadJoltDiagnostics() const noexcept = 0;
        virtual PhysicsWorldSleepSettings ReadWorldSleepSettings() const noexcept = 0;
        virtual void UpdateWorldSleepSettings(const PhysicsWorldSleepSettings&) noexcept = 0;
        virtual PhysicsBodyResult<PhysicsPlacementResult> QueryEditorPlacement(
            std::span<const PhysicsPlacementPose>,const PhysicsPlacementSettings&) const = 0;
    protected:
        static void PublishSleep(RigidBody3D& body, bool sleeping, float dt)
        {
            body.m_IsSleeping = sleeping;
            // This is elapsed time in the backend's sleeping state, not Jolt's
            // private three-point pre-sleep timer or Native sleep eligibility.
            body.m_InactiveSeconds = sleeping ? body.m_InactiveSeconds + dt : 0;
            body.m_WakeRequested = body.m_ExternalMutation = false;
        }
    };
    PhysicsBodyResult<std::unique_ptr<WorldBackend>> CreateJoltBackend();
}
