#pragma once
#include "PhysicsBody.h"
#include "PhysicsBackend.h"
#include "PhysicsJoltSettings.h"
#include "Shape.h"
#include "Constraints/DistanceJoint.h"
#include <expected>
#include <functional>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>
#include <array>
#include <bitset>
#include <optional>
#include <span>
#include <limits>

namespace GEngine
{
    class _Scene;
    class WorldBackend;
    struct PreparedBackendMass;
    struct PhysicsTestAccess; // Defined only by direct solver fixtures, not gameplay callers.
    enum class PhysicsBodyErrorCode
    {
        InvalidHandle, WrongWorld, StaleHandle, Closed, WrongThread, Busy,
        IdentityExhausted, Allocation, InvalidShape, InvalidPose, InvalidMass,
        InvalidMaterial, InvalidVelocity, InvalidForce, InvalidDuration,
        WrongBodyType, NonFiniteResult, NotSleepEligible,
        BackendUnavailable, BackendInitialization, UnsupportedFeature, Capacity,
        Penetration, NoSurface, QueryLimit
    };
    struct PhysicsBodyError
    {
        PhysicsBodyErrorCode code;
        PhysicsBodyHandle body{};
        std::string_view operation;
    };
    template<class T = void> using PhysicsBodyResult = std::expected<T, PhysicsBodyError>;
    struct PhysicsMaterial
    {
        float friction{.5f}, restitution{.5f};
        float spinResistanceLength{}, rollingResistanceLength{};
        friend bool operator==(const PhysicsMaterial&, const PhysicsMaterial&) = default;
    };
    struct PhysicsBodyDesc
    {
        Component::BodyType type{Component::BodyType::Dynamic};
        Math::Vec3f position{}, linearVelocity{}, angularVelocity{};
        Math::Quat orientation{1, 0, 0, 0};
        float mass{1}; // Positive for Dynamic; Static/Kinematic have zero effective inverse mass.
        PhysicsMaterial material;
        std::uint32_t collisionLayer{1}, collisionMask{~std::uint32_t{0}};
        RigidBody3D::SleepSettings sleep;
    };
    struct PhysicsBodyState
    {
        PhysicsBodyHandle handle;
        Component::BodyType type;
        Math::Vec3f position, linearVelocity, angularVelocity;
        Math::Quat orientation;
        float inverseMass;
        PhysicsMaterial material;
        ShapeType shapeType;
        Bounds localBounds, worldBounds;
        Math::Vec3f centerOfMass;
        Math::Mat3 inverseInertiaBody, inverseInertiaWorld;
        RigidBody3D::SleepSettings sleep;
        double inactiveSeconds;
        bool sleeping;
    };

    // Interactive authoring command. Defaults preserve existing integration behavior.
    // Native has fixed damping/gravity/CCD policy; Jolt exposes its per-body settings.
    struct PhysicsBodyProperties
    {
        Component::BodyType type{Component::BodyType::Dynamic};
        float mass{1}; // Retained positive authored mass, including while fixed.
        PhysicsMaterial material;
        RigidBody3D::SleepSettings sleep;
        float linearDamping{}, angularDamping{}, gravityFactor{1};
        bool continuousCollision{true}, allowSleeping{true};
        int velocityStepsOverride{}, positionStepsOverride{}; // 0 inherits world; island uses maximum.
        friend bool operator==(const PhysicsBodyProperties&, const PhysicsBodyProperties&) = default;
    };

    struct PhysicsBodyPropertyEdit
    {
        PhysicsBodyHandle body;
        PhysicsBodyProperties before,after; // Optimistic readback guard; no stale overwrites.
    };
    // Jolt world-level three-point sleep test; not Native per-body thresholds.
    // Defaults match the pinned SDK. Interactive overrides never rewrite fixtures.
    struct PhysicsWorldSleepSettings
    {
        float pointVelocityThreshold{.03f}, timeBeforeSleep{.5f};
        friend bool operator==(const PhysicsWorldSleepSettings&, const PhysicsWorldSleepSettings&) = default;
    };

    // One-shot world-space actions. VelocityChange interprets linear/angular as
    // delta m/s and delta rad/s; otherwise N*s and N*m*s. Point is absolute world
    // position, absent means COM. No scheduler advance or persistent force.
    enum class PhysicsImpulseMode { Impulse, VelocityChange };
    struct PhysicsImpulseCommand {
        PhysicsBodyHandle body;
        Vec3f linear{}, angular{};
        std::optional<Vec3f> point;
        PhysicsImpulseMode mode=PhysicsImpulseMode::Impulse;
    };
    struct PhysicsImpulseMotion {
        Vec3f linear{},angular{},center{};
        Quat orientation{1,0,0,0};
        Mat3 inverseInertia{0};
        float inverseMass{},maxLinear=std::numeric_limits<float>::max(),maxAngular=30;
        bool sleeping{};
    };
    struct PhysicsImpulseResult {
        PhysicsBodyHandle body;
        Vec3f impulse{},angularImpulse{};
        std::optional<Vec3f> point;
        PhysicsImpulseMotion before,after;
    };

    struct PhysicsImpulseBatchResult {
        std::vector<PhysicsImpulseResult> bodies=std::vector<PhysicsImpulseResult>(std::size_t(0));
    };

    // Editor-only, read-only candidate query. No borrowed shapes or backend types escape.
    struct PhysicsPlacementPose
    {
        PhysicsBodyHandle body;
        Vec3f position{};
        Quat rotation{1,0,0,0};
        std::optional<Vec3f> scale; // Absolute authored scale, prepared without changing live geometry.
        Vec3f referenceScale{1};
    };
    struct PhysicsPlacementSettings
    {
        bool surface{}, alignNormal{}, resolve{}, preserveExistingContacts{};
        Vec3f rayOrigin{}, rayDelta{}, pivot{};
        float offset{}, maxCorrection{5.f};
        static constexpr float ContactTolerance=1.e-4f; // SDK default collision tolerance, not solver slop.
    };
    struct PhysicsPlacementResult
    {
        std::vector<PhysicsPlacementPose> poses = std::vector<PhysicsPlacementPose>(std::size_t(0));
        std::size_t count{}, queries{};
        Vec3f normal{}, point{}, translation{};
        Quat rotation{1,0,0,0};
        bool surface{}, corrected{};
    };

    // Owner-thread API. Handles do not own bodies; the world owns bodies and adopted shapes.
    // Mutations occur between fixed ticks. No operation advances the scheduler.
    class PhysicsWorld
    {
    public:
        PhysicsWorld();
        [[nodiscard]] PhysicsBodyResult<> ConfigureBackend(PhysicsBackend);
        PhysicsBackend GetBackend() const noexcept { return m_BackendKind; }
        explicit PhysicsWorld(const Math::Vec3f& gravity);
        ~PhysicsWorld();
        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;
        PhysicsWorld(PhysicsWorld&&) = delete;
        PhysicsWorld& operator=(PhysicsWorld&&) = delete;
        // Transfers shape only on success; rejection preserves the caller's owner.
        [[nodiscard]] PhysicsBodyResult<PhysicsBodyHandle> CreateBody(
            const PhysicsBodyDesc&, std::unique_ptr<PhysicalShape>& shape);
        [[nodiscard]] PhysicsBodyResult<> DestroyBody(PhysicsBodyHandle);
        [[nodiscard]] PhysicsBodyResult<> Close();
        [[nodiscard]] PhysicsBodyResult<PhysicsBodyState> ReadBody(PhysicsBodyHandle) const;
        [[nodiscard]] PhysicsBodyResult<PhysicsPlacementResult> QueryEditorPlacement(
            std::span<const PhysicsPlacementPose>, const PhysicsPlacementSettings&) const;
        [[nodiscard]] PhysicsBodyResult<PhysicsBodyHandle> BodyAt(std::size_t index) const;
        [[nodiscard]] bool IsBodyValid(PhysicsBodyHandle handle) const;
        std::size_t GetBodyCount() const { return m_RigidBodies.size(); }
        [[nodiscard]] PhysicsBodyResult<Bounds> GetBodyBoundsAt(
            PhysicsBodyHandle, const Math::Vec3f&, const Math::Quat&) const;
        [[nodiscard]] PhysicsBodyResult<> SetBodyVelocity(PhysicsBodyHandle,
            const Math::Vec3f& linear, const Math::Vec3f& angular);
        [[nodiscard]] PhysicsBodyResult<> SetBodyMass(PhysicsBodyHandle, float mass);
        [[nodiscard]] PhysicsBodyResult<> SetBodyMaterial(PhysicsBodyHandle, const PhysicsMaterial&);
        [[nodiscard]] PhysicsBodyResult<PhysicsBodyProperties> ReadBodyProperties(PhysicsBodyHandle) const;
        [[nodiscard]] PhysicsBodyResult<> UpdateBodyProperties(PhysicsBodyHandle, const PhysicsBodyProperties&);
        // Shared-field authoring only: no bulk mass/motion/Native threshold changes.
        // All validation and allocating cache preparation precede no-fail publication.
        [[nodiscard]] PhysicsBodyResult<> UpdateBodyPropertiesBatch(std::span<const PhysicsBodyPropertyEdit>);
        [[nodiscard]] PhysicsBodyResult<PhysicsJoltSettings> ReadJoltSettings() const;
        [[nodiscard]] PhysicsBodyResult<PhysicsJoltSettings> ReadJoltNativeDefaults() const;
        [[nodiscard]] PhysicsBodyResult<> UpdateJoltSettings(const PhysicsJoltSettings&);
        [[nodiscard]] PhysicsBodyResult<> EnableJoltDiagnostics(bool);
        [[nodiscard]] PhysicsBodyResult<PhysicsJoltDiagnosticView> ReadJoltDiagnostics() const;
        [[nodiscard]] PhysicsBodyResult<PhysicsWorldSleepSettings> ReadWorldSleepSettings() const;
        [[nodiscard]] PhysicsBodyResult<> UpdateWorldSleepSettings(const PhysicsWorldSleepSettings&);
        static constexpr std::size_t MaxImpulseBodies=4096; // Current backend body capacity; independent from Transform staging.
        [[nodiscard]] PhysicsBodyResult<PhysicsImpulseMotion> ReadImpulseMotion(PhysicsBodyHandle) const;
        [[nodiscard]] PhysicsBodyResult<std::unique_ptr<PhysicsImpulseBatchResult>> ApplyImpulseBatch(std::span<const PhysicsImpulseCommand>);
        [[nodiscard]] PhysicsBodyResult<> ApplyLinearImpulse(PhysicsBodyHandle, const Math::Vec3f&);
        [[nodiscard]] PhysicsBodyResult<> ApplyAngularImpulse(PhysicsBodyHandle, const Math::Vec3f&);
        [[nodiscard]] PhysicsBodyResult<> ApplyImpulse(PhysicsBodyHandle,
            const Math::Vec3f& worldPoint, const Math::Vec3f& impulse);
        // Explicit force integral F * seconds, applied once; no queued force or hidden step.
        [[nodiscard]] PhysicsBodyResult<> ApplyForceForDuration(PhysicsBodyHandle,
            const Math::Vec3f& force, float seconds);
        [[nodiscard]] PhysicsBodyResult<> SetBodySleepSettings(
            PhysicsBodyHandle, const RigidBody3D::SleepSettings&);
        [[nodiscard]] PhysicsBodyResult<> TrySleepBody(PhysicsBodyHandle);
        [[nodiscard]] PhysicsBodyResult<> WakeBody(PhysicsBodyHandle);
        bool IsBodyIdentityValid(RigidBodyIdentity identity) const;
        static constexpr std::size_t MaxDistanceJoints = 256;
        [[nodiscard]] JointResult<PhysicsJointHandle> CreateDistanceJoint(const DistanceJointDesc&);
        [[nodiscard]] JointResult<DistanceJointState> ReadDistanceJoint(PhysicsJointHandle) const;
        [[nodiscard]] JointResult<> DestroyJoint(PhysicsJointHandle);
        [[nodiscard]] JointResult<PhysicsJointHandle> JointAt(std::size_t ordinal) const;
        std::size_t GetJointCount() const noexcept { return m_JointCount; }
        void SetGravity(const Math::Vec3f& gravity) { m_Gravity = gravity; }
        auto GetGravity() const { return m_Gravity; }
        void ConstructWorld() {} // Retained no-op configuration hook.
    private:
        // Command-local scratch: at most two incident endpoints per managed joint.
        // Independent impulse targets retain their separate 4096-body batch bound.
        struct ImpulseActivationPlan {
            std::array<PhysicsBodyHandle,2*MaxDistanceJoints> bodies{};
            std::bitset<MaxImpulseBodies> reached, included;
            std::size_t count{};
        };
        static_assert(sizeof(ImpulseActivationPlan)==13320); // x64: no heap/idle-step work.
        PhysicsBodyResult<> SeedImpulseActivation(ImpulseActivationPlan&,PhysicsBodyHandle) const;
        PhysicsBodyResult<> PrepareImpulseActivation(ImpulseActivationPlan&) const;
        void CommitImpulseActivation(const ImpulseActivationPlan&) noexcept;
        void CommitLegacyImpulseActivation(const ImpulseActivationPlan&,PhysicsBodyHandle) noexcept;
#ifdef GENGINE_CHAIN_WAKE_CHECKS
        friend struct ChainWakeTestAccess;
        bool m_ChainWakeFailReadbackOnce{};
        bool m_MassInertiaFailPropertyReadbackOnce{};
#endif
        PhysicsBackend m_BackendKind{PhysicsBackend::Native};
        std::unique_ptr<WorldBackend> m_Backend;
        struct EditorPlacementShape {
            PhysicsBodyHandle handle{};const PhysicalShape* source{};std::uint64_t revision{};
            Vec3f reference{1};std::unique_ptr<PhysicalShape> shape;
        };
        mutable std::vector<EditorPlacementShape> m_EditorPlacementShapes;
        struct JointSlot { DistanceJointRow row; std::uint64_t generation{}; bool live{}; };
        std::unique_ptr<JointSlot[]> m_Joints;
        std::size_t m_JointCount{};
        std::function<void(RigidBody3D*)> m_JointMutationCallback;
        std::function<void(std::span<RigidBody3D* const>)> m_BodyPropertyMutationCallback;
        PhysicsBodyResult<RigidBody3D> PrepareBodyProperties(PhysicsBodyHandle,const PhysicsBodyProperties&) const;
        void PublishBodyProperties(PhysicsBodyHandle,RigidBody3D*,const RigidBody3D&,const PhysicsBodyProperties&,const PreparedBackendMass&) noexcept;
        JointResult<std::size_t> ResolveJoint(PhysicsJointHandle, std::string_view, bool mutation = false) const;
        RigidBody3D* ResolveJointBody(PhysicsBodyHandle) const;
        void InvalidateJoints(RigidBody3D*);
        void RetireJoints(RigidBody3D*);
        bool JointSuppressesPair(RigidBodyIdentity, RigidBodyIdentity) const;
        using BodyRemovalCallback = std::function<void(RigidBody3D*)>;
        // Preserve the hot identity-slot layout used by the approved solver.
        struct BodyIdentitySlot { RigidBody3D* body{}; std::uint64_t generation{}; };
        struct OwnedResources
        {
            std::unique_ptr<PhysicalShape> shape;
            std::function<void()> retireSceneBinding;
            PhysicsBodyProperties properties;
        };
        RigidBody3D* CreateRigidBody3D(); // Internal solver fixtures can borrow shapes.
        void RemoveRigidBody3D(RigidBody3D*);
        const std::vector<RigidBody3D*>& GetPhysicsBodies() const { return m_RigidBodies; }
        void SetBodyRemovalCallback(const BodyRemovalCallback& callback) { m_BodyRemovalCallback = callback; }
        void ReleaseBodyIdentity(RigidBody3D*);
        PhysicsBodyHandle HandleOf(RigidBodyIdentity) const;
        PhysicsBodyResult<> CheckAccess(std::string_view, bool mutation) const;
        PhysicsBodyResult<RigidBody3D*> Resolve(PhysicsBodyHandle, std::string_view, bool mutation = false) const;
        void SetSceneRetirement(PhysicsBodyHandle, std::function<void()>);
        Math::Vec3f m_Gravity{0, -12, 0};
        std::vector<RigidBody3D*> m_RigidBodies;
        std::vector<BodyIdentitySlot> m_BodyIdentitySlots;
        std::vector<std::uint64_t> m_FreeBodyIdentitySlots;
        BodyRemovalCallback m_BodyRemovalCallback;
        // Cold ownership resources use the same slot; no second identity allocator/table.
        std::vector<OwnedResources> m_OwnedResources;
        std::uint64_t m_Domain{};
        const std::thread::id m_OwnerThread{std::this_thread::get_id()};
        bool m_Closed{}, m_Mutating{};
        friend class PhysicsSystem;
        friend class _Scene;
        friend struct PhysicsTestAccess;
    };
}
