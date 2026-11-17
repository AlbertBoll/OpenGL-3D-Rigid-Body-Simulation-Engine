#pragma once
#include "../PhysicsBody.h"
#include <expected>
#include <string_view>

namespace GEngine
{
    enum class JointKind { Distance, BallSocket };
    struct PhysicsJointTag;
    using PhysicsJointHandle = Asset::AssetHandle<PhysicsJointTag>;
    struct DistanceJointDesc
    {
        PhysicsBodyHandle bodyA{}, bodyB{};
        Vec3f localAnchorA{}, localAnchorB{};
        float length{1};
        bool collideConnected{false};
    };
    enum class JointErrorCode
    {
        InvalidHandle, WrongWorld, StaleHandle, Access, InvalidEndpoint,
        InvalidGeometry, NoResponse, Capacity, Allocation, NonFinite, WrongKind
    };
    struct JointError { JointErrorCode code; PhysicsJointHandle joint{}; std::string_view operation; };
    template<class T = void> using JointResult = std::expected<T, JointError>;
    struct DistanceJointState
    {
        PhysicsJointHandle handle;
        DistanceJointDesc descriptor;
        Vec3f worldAnchorA{}, worldAnchorB{};
        float error{}, accumulatedImpulse{};
        bool active{};
    };

    // Implementation value: no virtual dispatch or dynamically sized math.
    // Only the world owns these rows; consumers retain handles and snapshots.
    class DistanceJointRow
    {
        DistanceJointDesc desc;
        RigidBody3D *a{}, *b{}; // Resolved for the current tick; never public.
        Vec3f axis{}, angularA{}, angularB{}, invAngularA{}, invAngularB{};
        float effectiveMass{}, bias{}, impulse{}, previousDt{};
        bool active{};
        void Reset() noexcept;
        bool Prepare(RigidBody3D&, RigidBody3D&, float);
        bool Solve();
        void Apply(float);
        friend class PhysicsWorld;
        friend class PhysicsSystem;
    };
}
