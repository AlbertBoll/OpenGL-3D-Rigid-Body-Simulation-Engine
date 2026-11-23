#pragma once
#include "BallSocketJoint.h"
#include <array>

namespace GEngine
{
    // Pure convention/validation helpers shared by providers and inspection.
    struct AngularJoint
    {
        static bool IsKind(JointKind) noexcept;
        static bool Valid(const AngularJointDesc&) noexcept;
        static JointAngularState Observe(const AngularJointDesc&, const Quat&, const Quat&) noexcept;
    };

    // One fixed-size local block, evaluated in the existing Native solver pass.
    // No heap, extra contact iteration, global solver or retained body owner.
    class AngularJointRow
    {
        struct Row { Vec3f linear{}, angularA{}, angularB{}; double bias{}, lambda{}; bool unilateral{}; };
        AngularJointDesc desc;
        RigidBody3D *a{}, *b{};
        std::array<Row,6> rows{};
        std::array<std::array<double,6>,6> response{};
        Mat3 inverseA{0}, inverseB{0};
        Quat previousRelative{1,0,0,0};
        Vec3f impulse{}, angularImpulse{};
        float previousDt{};
        Vec3f motorAxis{};
        float motorInverseInertia{};
        HingeMotorSample motor{};
        void ApplyMotor(float total);
        std::size_t count{};
        int previousLimitU{}, previousLimitV{};
        bool active{}, impulseAvailable{};
        void Reset() noexcept;
        bool Prepare(RigidBody3D&, RigidBody3D&, float);
        bool RefreshLimitedRows();
        bool Solve();
        void Apply(std::size_t, double);
        friend class PhysicsWorld;
        friend class PhysicsSystem;
    };
}
