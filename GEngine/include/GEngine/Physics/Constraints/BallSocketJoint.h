#pragma once
#include "DistanceJoint.h"
#include <optional>

namespace GEngine
{
#if defined(GENGINE_PHASE27_QUALIFICATION) || defined(GENGINE_PHASE28_QUALIFICATION)
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

    struct HingeMotorSettings
    {
        bool enabled{};
        float targetVelocity{}; // Signed rad/s about frame A's X axis.
        float maxTorque{};      // N*m, symmetric in either direction.
        bool Valid() const noexcept;
        friend bool operator==(const HingeMotorSettings&, const HingeMotorSettings&) = default;
    };

    struct HingeMotorSample
    {
        float impulse{}, peakAccumulated{}, impulseCap{}; // N*m*s in the last fixed step.
        double work{}; // Signed joules, including the warm start.
        bool valid{};
        int limitSide{}; // Signed stop carrying a velocity impulse this step.
    };

    // Body-local, right-handed frames: X is the hinge/CV axis; Y/Z are U/V.
    // The frames retain the authored zero relation. No per-read recalibration.
    struct AngularJointParameters
    {
        Quat localFrameA{1,0,0,0}, localFrameB{1,0,0,0};
        float lower{-3.14159265358979323846f}, upper{3.14159265358979323846f};
        float halfAngleU{.78539816339744830962f}, halfAngleV{.78539816339744830962f};
        bool limitsEnabled{};
        std::optional<HingeMotorSettings> motor;
        friend bool operator==(const AngularJointParameters&, const AngularJointParameters&) = default;
    };
    struct AngularJointDesc
    {
        JointKind kind{JointKind::Hinge};
        PhysicsBodyHandle bodyA{}, bodyB{};
        Vec3f localAnchorA{}, localAnchorB{};
        bool collideConnected{};
        AngularJointParameters angular{};
    };
    struct JointAngularState
    {
        Quat relativeRotation{1,0,0,0};
        // Provider row impulses (N*m*s), not world XYZ torque:
        // hinge {limit,alignment0,alignment1}; CV {relation,U,V}.
        Vec3f angularImpulse{};
        float signedAngle{}, axisError{}, cvError{}, angleU{}, angleV{};
        float violationU{}, violationV{};
        int limitU{}, limitV{}; // -1 lower, 0 free, +1 upper.
        bool impulseAvailable{};
        HingeMotorSample motor{};
        float motorSpeed{}, motorError{};
        bool motorSaturated{};
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
        AngularJointParameters angular{};
    };
    struct JointState
    {
        PhysicsJointHandle handle;
        JointDescriptor descriptor;
        Vec3f worldAnchorA{}, worldAnchorB{}, residual{}, impulse{};
        float error{};
        bool active{};
        JointAngularState angular{};
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
