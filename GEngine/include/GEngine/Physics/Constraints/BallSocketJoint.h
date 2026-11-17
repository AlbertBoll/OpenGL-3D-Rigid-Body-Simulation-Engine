#pragma once
#include "DistanceJoint.h"

namespace GEngine
{
#ifdef GENGINE_PHASE27_QUALIFICATION
    namespace Phase27Audit {
        struct ArenaEvent {
            std::uintptr_t address{}, caller{};
            std::size_t requested{}, aligned{}, used{};
            bool reserve{};
        };
        inline void (*arena)(const ArenaEvent&) noexcept{};
        struct HeapEvent { std::uintptr_t address{}, caller{}; std::size_t requested{}; bool reserve{}; };
        inline void (*heap)(const HeapEvent&) noexcept{};
    }
#endif
    struct BallSocketJointDesc
    {
        PhysicsBodyHandle bodyA{}, bodyB{};
        Vec3f localAnchorA{}, localAnchorB{};
        bool collideConnected{false};
    };

    // Kind-tagged semantic readback for shared inspection and reconstruction.
    // targetDistance is meaningful only for Distance; point anchors coincide.
    struct JointDescriptor
    {
        JointKind kind{JointKind::Distance};
        PhysicsBodyHandle bodyA{}, bodyB{};
        Vec3f localAnchorA{}, localAnchorB{};
        float targetDistance{};
        bool collideConnected{};
    };
    struct JointState
    {
        PhysicsJointHandle handle;
        JointDescriptor descriptor;
        Vec3f worldAnchorA{}, worldAnchorB{}, residual{}, impulse{};
        float error{};
        bool active{};
    };
    struct BallSocketJointState
    {
        PhysicsJointHandle handle;
        BallSocketJointDesc descriptor;
        Vec3f worldAnchorA{}, worldAnchorB{}, residual{}, accumulatedImpulse{};
        bool active{};
    };

    class BallSocketJointRow
    {
        BallSocketJointDesc desc;
        RigidBody3D *a{}, *b{};
        Vec3f armA{}, armB{}, bias{}, impulse{};
        Mat3 effectiveMass{0}, inverseInertiaA{0}, inverseInertiaB{0};
        float previousDt{};
        bool active{};
        void Reset() noexcept;
        bool Prepare(RigidBody3D&, RigidBody3D&, float, bool warmStart = true);
        bool Solve();
        void Apply(const Vec3f&);
#ifdef GENGINE_PHASE27_QUALIFICATION
        friend struct Phase27GraphAccess;
#endif
        friend class PhysicsWorld;
        friend class PhysicsSystem;
    };
}
