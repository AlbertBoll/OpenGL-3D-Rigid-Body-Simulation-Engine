#include "gepch.h"
#include "BallSocketJoint.h"

namespace GEngine
{
    void BallSocketJointRow::Reset() noexcept
    {
        impulse = Vec3f(0);
        previousDt = 0;
        active = false;
        a = b = nullptr;
    }
    void BallSocketJointRow::Apply(const Vec3f& lambda)
    {
        if (a->GetInverseMass() > 0) {
            a->m_LinearVelocity -= lambda * a->GetInverseMass();
            a->m_AngularVelocity -= inverseInertiaA * glm::cross(armA, lambda);
        }
        if (b->GetInverseMass() > 0) {
            b->m_LinearVelocity += lambda * b->GetInverseMass();
            b->m_AngularVelocity += inverseInertiaB * glm::cross(armB, lambda);
        }
    }
    bool BallSocketJointRow::Prepare(RigidBody3D& bodyA, RigidBody3D& bodyB, float dt, bool warmStart)
    {
        a = &bodyA; b = &bodyB; active = false;
        if (!(dt > 0) || !Math::IsFinite(dt)) { Reset(); return false; }
        if (previousDt != dt) impulse = Vec3f(0);
        previousDt = dt;
        const auto pa = a->BodySpaceToWorldSpace(desc.localAnchorA);
        const auto pb = b->BodySpaceToWorldSpace(desc.localAnchorB);
        armA = pa - a->GetCenterOfMassWorldSpace();
        armB = pb - b->GetCenterOfMassWorldSpace();
        inverseInertiaA = a->GetInverseInertiaTensorWorldSpace();
        inverseInertiaB = b->GetInverseInertiaTensorWorldSpace();
        Mat3 response{0};
        for (int axis = 0; axis < 3; ++axis) {
            Vec3f unit(0); unit[axis] = 1;
            response[axis] = unit * (a->GetInverseMass() + b->GetInverseMass()) +
                glm::cross(inverseInertiaA * glm::cross(armA, unit), armA) +
                glm::cross(inverseInertiaB * glm::cross(armB, unit), armB);
        }
        // A live dynamic endpoint makes this symmetric response positive definite.
        // Reject singular/nonfinite input before inversion; no regularization or
        // angular lock is introduced to conceal an invalid effective response.
        const float determinant = glm::determinant(response);
        if (!Math::IsFinite(determinant) || determinant <= 0) { Reset(); return false; }
        effectiveMass = glm::inverse(response);
        bias = .2f * (pb - pa) / dt;
        if (!Math::IsFinite(bias) || !Math::IsFinite(impulse)) { Reset(); return false; }
        for (int axis = 0; axis < 3; ++axis)
            if (!Math::IsFinite(effectiveMass[axis])) { Reset(); return false; }
        active = (a->GetInverseMass() > 0 && !a->IsSleeping()) ||
                 (b->GetInverseMass() > 0 && !b->IsSleeping());
        if (active && warmStart) Apply(impulse);
        return a->HasFiniteState() && b->HasFiniteState();
    }
    bool BallSocketJointRow::Solve()
    {
        if (!active) return true;
        const auto speed = b->GetLinearVelocity() + glm::cross(b->GetAngularVelocity(), armB) -
            a->GetLinearVelocity() - glm::cross(a->GetAngularVelocity(), armA);
        const auto delta = -effectiveMass * (speed + bias);
        const auto next = impulse + delta;
        if (!Math::IsFinite(next)) return false;
        impulse = next;
        Apply(delta);
        return a->HasFiniteState() && b->HasFiniteState();
    }
}
