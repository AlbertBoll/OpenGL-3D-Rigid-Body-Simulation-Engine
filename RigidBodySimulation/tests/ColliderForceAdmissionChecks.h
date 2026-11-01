#pragma once
// Included inside the existing isolated Admission validation namespace.
inline void ForceAdmissionChecks()
{
    for(bool unverified : {false,true})
    {
        {
            PhysicsSystem system;system.SetPhysicsWorld(new PhysicsWorld());auto& world=*system.GetPhysicsWorld();
            const auto body=Add(world,SphereShapeDesc{1},Vec3f(0,2.99f,0));
            (void)Add(world,BoxShapeDesc{Vec3f(20,2,20)},Vec3f(0),Component::BodyType::Static);
            const CapsuleShapeDesc descriptor{1,1.2f};
            if(unverified)ColliderAdmissionCheckAccess::Fault(system,4,true);
            auto risk=system.AssessBodyColliderReplacement(body,descriptor);
            Require(unverified ? risk.error==ColliderAdmissionError::Unproved : risk.outcome==Outcome::RejectedRisk,
                "known risk and synthetic unverified geometry remain distinct");
            Require(PhysicsSystem::CanConfirmColliderRisk(risk),"valid geometric risk permits explicit confirmation");
            RejectCommand("force_fixture_safe_rejected",system,body,descriptor);
            bool complete=false;
            for(std::size_t fail=1;fail<=512&&!complete;++fail)
            {
                const State before(system,nullptr);
                {
                    AdmissionAllocation::Begin(fail);auto consent=system.PrepareColliderConfirmation(risk);
                    const auto alloc=AdmissionAllocation::End();complete=alloc.attempts<fail;
                    Require(!alloc.overflow,"confirmation allocation accounting bounded");
                    Require(complete ? bool(consent) : !consent&&consent.error()==ColliderAdmissionError::AllocationFailure,
                        "confirmation allocation failure is typed and non-overridable");
                }
                Require(before.Same(system,nullptr)&&AdmissionAllocation::stats.liveCount==0,
                    "confirmation preparation preserves state and retires allocation");
            }
            Require(complete,"all confirmation allocation ordinals swept");
            auto consent=system.PrepareColliderConfirmation(risk);Require(bool(consent),"explicit consent prepared");
            {
                const State before(system,nullptr);
                auto invalid=system.ReplaceBodyColliderConfirmed(body,CapsuleShapeDesc{-1,1},*consent);
                Require(!invalid.committed&&invalid.error==ColliderAdmissionError::InvalidShape&&before.Same(system,nullptr),
                    "Force cannot bypass malformed Shape");
                auto stale=system.ReplaceBodyColliderConfirmed(body,CapsuleShapeDesc{1,1.3f},*consent);
                Require(!stale.committed&&stale.error==ColliderAdmissionError::StaleConfirmation&&before.Same(system,nullptr),
                    "Force binds exact descriptor and fails without mutation");
            }
            {
                auto* native=PhysicsTestAccess::Resolve(world,body);const float mass=native->m_InvMass;native->m_InvMass=-1;
                const State before(system,nullptr);auto invalidConsent=system.PrepareColliderConfirmation(risk);
                Require(bool(invalidConsent),"malformed-world fixture captures current context");
                auto blocked=system.ReplaceBodyColliderConfirmed(body,descriptor,*invalidConsent);
                Require(!blocked.committed&&blocked.error==ColliderAdmissionError::InvalidShape&&before.Same(system,nullptr),
                    "Force cannot bypass invalid body mass or inertia safety");
                native->m_InvMass=mass;
            }
            const auto original=*world.ReadBody(body);const auto originalShape=system.ReadBodyCollider(body)->identity;
            complete=false;std::size_t failures{},allocationsAtSuccess{};
            for(std::size_t fail=1;fail<=512&&!complete;++fail)
            {
                const State before(system,nullptr);
                {
                    AdmissionAllocation::Begin(fail);const auto start=std::chrono::steady_clock::now();
                    auto result=system.ReplaceBodyColliderConfirmed(body,descriptor,*consent);
                    const auto alloc=AdmissionAllocation::End();
                    Report(unverified?"force_unverified":"force_known_overlap",int(fail),result,
                        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),alloc);
                    complete=alloc.attempts<fail;
                    if(!complete){++failures;Require(!result.committed&&result.error==ColliderAdmissionError::AllocationFailure,
                        "every Force transaction allocation failure returns typed noncommit");}
                    else {allocationsAtSuccess=alloc.attempts;Require(result.committed&&result.outcome!=Outcome::NoAddedOverlap,
                        "explicit Force commits without claiming certified safety");}
                }
                if(!complete)Require(before.Same(system,nullptr)&&AdmissionAllocation::stats.liveCount==0,
                    "Force allocation failure preserves complete Physics image and temporary ownership");
            }
            Require(complete&&failures==allocationsAtSuccess,"Force transaction sweep covers every actual allocation without skipped positions");
            checks<<'"'<<(unverified?"force_unverified":"force_known_overlap")<<"\",COMPLETE_"<<failures<<"_FAILED_POSITIONS_THEN_TERMINAL_"<<allocationsAtSuccess<<"_ALLOCATIONS\n";
            const auto actual=*world.ReadBody(body);const auto shape=*system.ReadBodyCollider(body);
            Require(shape.type==ShapeType::Capsule&&shape.identity!=originalShape&&actual.handle==original.handle&&
                actual.position==original.position&&actual.orientation==original.orientation&&actual.inverseMass==original.inverseMass&&
                actual.linearVelocity==original.linearVelocity&&actual.angularVelocity==original.angularVelocity,
                "Force authoritative Capsule readback preserves body pose mass and velocities");
            Require(Math::IsFinite(actual.inverseInertiaBody[0])&&Math::IsFinite(actual.inverseInertiaBody[1])&&Math::IsFinite(actual.inverseInertiaBody[2])&&Math::IsFinite(actual.inverseInertiaWorld[0])&&Math::IsFinite(actual.inverseInertiaWorld[1])&&Math::IsFinite(actual.inverseInertiaWorld[2]),"Force recomputes finite native inertia");
            if(unverified)ColliderAdmissionCheckAccess::Fault(system,4,false);
        }
        Require(AdmissionAllocation::stats.liveCount==0,"Force world teardown retires committed Shape and staged wake resources");
    }
}
