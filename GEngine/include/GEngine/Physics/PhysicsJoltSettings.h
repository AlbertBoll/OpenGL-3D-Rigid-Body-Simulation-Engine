#pragma once
#include "PhysicsBody.h"
#include <cmath>
#include <span>

namespace GEngine {
// Interactive overrides only. These defaults mirror pinned Jolt 5.6.0, except
// collisionSteps, which is the preserved integration's Update argument.
struct PhysicsJoltSettings {
    int velocitySteps{10}, positionSteps{2}, collisionSteps{1};
    float minRestitutionVelocity{1}, baumgarte{.2f}, penetrationSlop{.02f}, speculativeDistance{.02f};
    bool allowSleeping{true};
    float timeBeforeSleep{.5f}, pointVelocitySleepThreshold{.03f};
    bool warmStarting{true}, bodyPairCache{true}, manifoldReduction{true};
    friend bool operator==(const PhysicsJoltSettings&,const PhysicsJoltSettings&)=default;
};
// Editor resource budgets, not changes to SDK defaults or qualification criteria.
inline const char* ValidateJoltSettings(const PhysicsJoltSettings& p) {
    if(p.velocitySteps<2 || p.velocitySteps>255)return "Velocity iterations require 2..255 (friction needs at least 2).";
    if(p.positionSteps<0 || p.positionSteps>255)return "Position iterations require 0..255; zero disables position correction.";
    if(p.collisionSteps<1 || p.collisionSteps>16)return "Collision substeps require 1..16 (interactive editor budget).";
    if(!std::isfinite(p.minRestitutionVelocity)||p.minRestitutionVelocity<=0)return "Minimum restitution velocity must be finite and positive.";
    if(!std::isfinite(p.baumgarte)||p.baumgarte<0||p.baumgarte>1)return "Baumgarte must be finite in [0,1].";
    for(float value:{p.penetrationSlop,p.speculativeDistance})
        if(!std::isfinite(value)||value<0||!std::isfinite(value*value))return "Contact distances and their squares must be finite and nonnegative.";
    const float movement=p.timeBeforeSleep*p.pointVelocitySleepThreshold;
    if(!std::isfinite(p.timeBeforeSleep)||p.timeBeforeSleep<=0||!std::isfinite(p.pointVelocitySleepThreshold)||p.pointVelocitySleepThreshold<=0||!std::isfinite(movement)||movement<=0)
        return "Sleep time, speed and their product must be finite and positive.";
    return nullptr;
}
struct PhysicsJoltContactSample {
    PhysicsBodyHandle bodyA{},bodyB{};
    Vec3f normal{},localPointA{},localPointB{};
    float penetration{},friction{},restitution{},normalVelocityBefore{},normalVelocityAfter{};
    bool added{};
};
struct PhysicsJoltDiagnosticView {
    bool enabled{};
    std::uint64_t step{};
    std::size_t callbacks{},overflow{};
    double updateMilliseconds{};
    std::span<const PhysicsJoltContactSample> contacts;
};
}
