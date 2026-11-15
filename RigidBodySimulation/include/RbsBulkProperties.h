#pragma once
#include "Physics/PhysicsWorld.h"
namespace Rbs::Editor::BulkPhysics {
    enum Field : unsigned {Friction=1,Restitution=2,LinearDamping=4,AngularDamping=8,Gravity=16,CCD=32,Sleeping=64,VelocitySteps=128,PositionSteps=256};
    inline constexpr unsigned All=511;
    inline unsigned Supported(GEngine::PhysicsBackend backend,GEngine::Component::BodyType type) {
        unsigned mask=Friction|Restitution;
        if(backend==GEngine::PhysicsBackend::Jolt) {
            if(type!=GEngine::Component::BodyType::Static)mask|=CCD|Sleeping;
            if(type==GEngine::Component::BodyType::Dynamic)mask|=LinearDamping|AngularDamping|Gravity|VelocitySteps|PositionSteps;
        }
        return mask;
    }
    inline GEngine::PhysicsBodyProperties Merge(GEngine::PhysicsBodyProperties p,const GEngine::PhysicsBodyProperties& v,unsigned mask) {
        if(mask&Friction)p.material.friction=v.material.friction;if(mask&Restitution)p.material.restitution=v.material.restitution;
        if(mask&LinearDamping)p.linearDamping=v.linearDamping;if(mask&AngularDamping)p.angularDamping=v.angularDamping;
        if(mask&Gravity)p.gravityFactor=v.gravityFactor;if(mask&CCD)p.continuousCollision=v.continuousCollision;
        if(mask&Sleeping)p.allowSleeping=v.allowSleeping;if(mask&VelocitySteps)p.velocityStepsOverride=v.velocityStepsOverride;
        if(mask&PositionSteps)p.positionStepsOverride=v.positionStepsOverride;return p;
    }
    inline unsigned Difference(const GEngine::PhysicsBodyProperties& a,const GEngine::PhysicsBodyProperties& b) {
        return (a.material.friction!=b.material.friction?Friction:0)|(a.material.restitution!=b.material.restitution?Restitution:0)|
            (a.linearDamping!=b.linearDamping?LinearDamping:0)|(a.angularDamping!=b.angularDamping?AngularDamping:0)|
            (a.gravityFactor!=b.gravityFactor?Gravity:0)|(a.continuousCollision!=b.continuousCollision?CCD:0)|
            (a.allowSleeping!=b.allowSleeping?Sleeping:0)|(a.velocityStepsOverride!=b.velocityStepsOverride?VelocitySteps:0)|
            (a.positionStepsOverride!=b.positionStepsOverride?PositionSteps:0);
    }
}
