#include "gepch.h"
#include "DistanceJoint.h"

namespace GEngine
{
    void DistanceJointRow::Reset() noexcept
    {
        impulse = previousDt = 0;
        active = false;
        a = b = nullptr;
    }
    void DistanceJointRow::Apply(float lambda)
    {
        // Solver impulses do not request a new external wake or reset the dwell.
        if (a->GetInverseMass() > 0) {
            a->m_LinearVelocity -= axis * (lambda * a->GetInverseMass());
            a->m_AngularVelocity -= invAngularA * lambda;
        }
        if (b->GetInverseMass() > 0) {
            b->m_LinearVelocity += axis * (lambda * b->GetInverseMass());
            b->m_AngularVelocity += invAngularB * lambda;
        }
    }
    bool DistanceJointRow::Prepare(RigidBody3D& bodyA, RigidBody3D& bodyB, float dt)
    {
        a = &bodyA; b = &bodyB; active = false;
        if (!(dt > 0) || !Math::IsFinite(dt)) { Reset(); return false; }
        if (previousDt != dt) impulse = 0;
        previousDt = dt;
        const auto pa = a->BodySpaceToWorldSpace(desc.localAnchorA);
        const auto pb = b->BodySpaceToWorldSpace(desc.localAnchorB);
        const auto delta = pb - pa;
        const float distance = glm::length(delta);
        if (!Math::IsFinite(distance) || distance <= Math::NumericalEpsilon) { Reset(); return false; }
        axis = delta / distance;
        angularA = glm::cross(pa - a->GetCenterOfMassWorldSpace(), axis);
        angularB = glm::cross(pb - b->GetCenterOfMassWorldSpace(), axis);
        invAngularA = a->GetInverseInertiaTensorWorldSpace() * angularA;
        invAngularB = b->GetInverseInertiaTensorWorldSpace() * angularB;
        const float response = a->GetInverseMass() + b->GetInverseMass() +
            glm::dot(angularA, invAngularA) + glm::dot(angularB, invAngularB);
        if (!Math::IsFinite(response) || response <= 0) { Reset(); return false; }
        effectiveMass = 1 / response;
        bias = .2f * (distance - desc.length) / dt;
        if (!Math::IsFinite(effectiveMass) || !Math::IsFinite(bias) || !Math::IsFinite(impulse)) { Reset(); return false; }
        active = (a->GetInverseMass() > 0 && !a->IsSleeping()) ||
                 (b->GetInverseMass() > 0 && !b->IsSleeping());
        if (active) Apply(impulse); // Exactly once, using only the previous tick cache.
        return a->HasFiniteState() && b->HasFiniteState();
    }
    bool DistanceJointRow::Solve()
    {
        if (!active) return true;
        const float speed = glm::dot(axis, b->GetLinearVelocity() - a->GetLinearVelocity()) +
            glm::dot(angularB, b->GetAngularVelocity()) - glm::dot(angularA, a->GetAngularVelocity());
        const float delta = -effectiveMass * (speed + bias);
        const float next = impulse + delta;
        if (!Math::IsFinite(next)) return false;
        impulse = next;
        Apply(delta);
        return a->HasFiniteState() && b->HasFiniteState();
    }
}
