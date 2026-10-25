#pragma once
#include "PhysicsBody.h"
#include "Shape.h"
#include <expected>
#include <functional>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

namespace GEngine
{
    class _Scene;
    struct PhysicsTestAccess; // Defined only by direct solver fixtures, not gameplay callers.
    enum class PhysicsBodyErrorCode
    {
        InvalidHandle, WrongWorld, StaleHandle, Closed, WrongThread, Busy,
        IdentityExhausted, Allocation, InvalidShape, InvalidPose, InvalidMass,
        InvalidMaterial, InvalidVelocity, InvalidForce, InvalidDuration,
        WrongBodyType, NonFiniteResult, NotSleepEligible
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

    // Owner-thread API. Handles do not own bodies; the world owns bodies and adopted shapes.
    // Mutations occur between fixed ticks. No operation advances the scheduler.
    class PhysicsWorld
    {
    public:
        PhysicsWorld();
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
        [[nodiscard]] PhysicsBodyResult<PhysicsBodyHandle> BodyAt(std::size_t index) const;
        [[nodiscard]] bool IsBodyValid(PhysicsBodyHandle handle) const;
        std::size_t GetBodyCount() const { return m_RigidBodies.size(); }
        [[nodiscard]] PhysicsBodyResult<Bounds> GetBodyBoundsAt(
            PhysicsBodyHandle, const Math::Vec3f&, const Math::Quat&) const;
        [[nodiscard]] PhysicsBodyResult<> SetBodyVelocity(PhysicsBodyHandle,
            const Math::Vec3f& linear, const Math::Vec3f& angular);
        [[nodiscard]] PhysicsBodyResult<> SetBodyMass(PhysicsBodyHandle, float mass);
        [[nodiscard]] PhysicsBodyResult<> SetBodyMaterial(PhysicsBodyHandle, const PhysicsMaterial&);
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
        void SetGravity(const Math::Vec3f& gravity) { m_Gravity = gravity; }
        auto GetGravity() const { return m_Gravity; }
        void ConstructWorld() {} // Retained no-op configuration hook.
    private:
        using BodyRemovalCallback = std::function<void(RigidBody3D*)>;
        // Preserve the hot identity-slot layout used by the approved solver.
        struct BodyIdentitySlot { RigidBody3D* body{}; std::uint64_t generation{}; };
        struct OwnedResources
        {
            std::unique_ptr<PhysicalShape> shape;
            std::function<void()> retireSceneBinding;
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
