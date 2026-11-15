// Included inside GEngine by PhysicsWorld.cpp. No solver or scheduler changes.
PhysicsBodyResult<PhysicsImpulseMotion> PhysicsWorld::ReadImpulseMotion(PhysicsBodyHandle h) const
{
    auto body=Resolve(h,"ReadImpulseMotion");if(!body)return std::unexpected(body.error());
    if((*body)->Type!=BodyType::Dynamic)return Failure(Code::WrongBodyType,h,"Impulse requires a Dynamic body; Static/Kinematic targets are unsupported");
    if(m_Backend)return m_Backend->ReadImpulseMotion(h);
    const auto& b=**body;
    return PhysicsImpulseMotion{b.GetLinearVelocity(),b.GetAngularVelocity(),b.GetCenterOfMassWorldSpace(),
        b.m_Orientation,b.GetInverseInertiaTensorWorldSpace(),b.GetInverseMass(),std::numeric_limits<float>::max(),30.f,b.IsSleeping()};
}

PhysicsBodyResult<std::unique_ptr<PhysicsImpulseBatchResult>> PhysicsWorld::ApplyImpulseBatch(std::span<const PhysicsImpulseCommand> commands)
{
    if(auto access=CheckAccess("ApplyImpulseBatch",true);!access)return std::unexpected(access.error());
    if(commands.empty() || commands.size()>MaxImpulseBodies)return Failure(Code::Capacity,{},"Impulse batch must contain 1..4096 bodies; no subset applied");
    struct Prepared {RigidBody3D* body{};RigidBody3D before;Vec3f expectedLinear{},expectedAngular{};};
    try {
        ImpulseActivationPlan activation;
        std::vector<Prepared> staged(commands.size());
        auto output=std::make_unique<PhysicsImpulseBatchResult>();
        auto& results=output->bodies;results.resize(commands.size());
        std::vector<std::uint32_t> identities(std::size_t(0));identities.reserve(commands.size());
        for(const auto& c:commands)identities.push_back(c.body.index);
        std::sort(identities.begin(),identities.end());
        if(std::adjacent_find(identities.begin(),identities.end())!=identities.end())return Failure(Code::InvalidHandle,{},"Duplicate impulse target; entire batch rejected");
        const auto within=[](const Vec3f& v,float limit) {return Math::IsFinite(v) && Math::IsFinite(glm::dot(v,v)) && glm::length(glm::dvec3(v))<=double(limit);};
        for(std::size_t i=0;i<commands.size();++i) {
            const auto& c=commands[i];auto body=Resolve(c.body,"ApplyImpulseBatch",false);
            if(!body)return std::unexpected(body.error());
            if((*body)->Type!=BodyType::Dynamic)return Failure(Code::WrongBodyType,c.body,"Impulse requires every target to be Dynamic; no subset applied");
            if(!Math::IsFinite(c.linear)||!Math::IsFinite(c.angular)||(c.point&&!Math::IsFinite(*c.point)) ||
                (c.mode!=PhysicsImpulseMode::Impulse && c.mode!=PhysicsImpulseMode::VelocityChange))
                return Failure(Code::InvalidForce,c.body,"Nonfinite or invalid impulse command");
            auto motion=ReadImpulseMotion(c.body);if(!motion)return std::unexpected(motion.error());
            if(!Math::IsFinite(motion->inverseMass)||motion->inverseMass<=0 || !FiniteMatrix(motion->inverseInertia) ||
                !Math::IsFinite(motion->center)||!Math::IsFinite(motion->linear)||!Math::IsFinite(motion->angular))
                return Failure(Code::InvalidMass,c.body,"Authoritative mass/inertia or velocity is invalid");
            auto& r=results[i];r.body=c.body;r.before=*motion;r.point=c.point;r.impulse=c.linear;r.angularImpulse=c.angular;
            if(c.mode==PhysicsImpulseMode::VelocityChange) {
                r.impulse=c.linear/motion->inverseMass;
                if(c.angular!=Vec3f(0)) {
                    const auto inertia=glm::inverse(motion->inverseInertia);
                    if(!FiniteMatrix(inertia))return Failure(Code::InvalidMass,c.body,"Angular velocity change requires invertible world inertia");
                    r.angularImpulse=inertia*c.angular;
                }
            }
            const Vec3f torque=c.point?glm::cross(*c.point-motion->center,r.impulse):Vec3f(0);
            const auto intermediate=motion->angular+motion->inverseInertia*torque;
            auto& p=staged[i];p.body=*body;p.before=**body;
            p.expectedLinear=motion->linear+r.impulse*motion->inverseMass;
            p.expectedAngular=intermediate+motion->inverseInertia*r.angularImpulse;
            if(!Math::IsFinite(r.impulse)||!Math::IsFinite(r.angularImpulse)||!Math::IsFinite(torque) ||
                !within(p.expectedLinear,motion->maxLinear)||!within(intermediate,motion->maxAngular)||!within(p.expectedAngular,motion->maxAngular))
                return Failure(Code::InvalidVelocity,c.body,"Impulse exceeds finite/backend velocity bounds (angular limit 30 rad/s in current integration); reduce magnitude or point offset");
            if(m_Backend && (r.impulse!=Vec3f(0) || r.angularImpulse!=Vec3f(0)))
                if(auto valid=SeedImpulseActivation(activation,c.body);!valid)return std::unexpected(valid.error());
        }
        if(m_Backend)
            if(auto valid=PrepareImpulseActivation(activation);!valid)return std::unexpected(valid.error());
        // All allocation, identity and numerical validation precede any mutation.
        // The synchronous owner boundary forbids a step/observer between members.
        for(std::size_t i=0;i<results.size();++i) {
            const auto& r=results[i];auto& b=*staged[i].body;
            if(m_Backend)m_Backend->CommitImpulse(r);
            else {
                if(r.point)b.ApplyImpulse(*r.point,r.impulse);else b.ApplyImpulseLinear(r.impulse);
                if(r.angularImpulse!=Vec3f(0))b.ApplyImpulseAngular(r.angularImpulse);
            }
        }
        bool valid=true;
        const auto equal=[](const Vec3f& a,const Vec3f& b){return Math::IsFinite(a) && glm::length(glm::dvec3(a)-glm::dvec3(b))<=2.e-5*(1.+glm::length(glm::dvec3(b)));};
        for(std::size_t i=0;i<results.size();++i) {
            auto actual=ReadImpulseMotion(results[i].body);
            if(!actual){valid=false;break;}
            results[i].after=*actual;
            if(!equal(actual->linear,staged[i].expectedLinear)||!equal(actual->angular,staged[i].expectedAngular)){valid=false;break;}
        }
#ifdef GENGINE_CHAIN_WAKE_CHECKS
        // Isolated check build only: exercise real rollback after actual mutations.
        if(m_ChainWakeFailReadbackOnce){m_ChainWakeFailReadbackOnce=false;valid=false;}
#endif
        if(!valid) {
            for(std::size_t i=0;i<results.size();++i) {
                if(m_Backend)m_Backend->RestoreImpulseMotion(results[i].body,results[i].before);
                *staged[i].body=staged[i].before;
            }
            for(const auto& r:results) {
                const auto restored=ReadImpulseMotion(r.body);
                if(!restored || !equal(restored->linear,r.before.linear)||!equal(restored->angular,r.before.angular))
                    return Failure(Code::NonFiniteResult,r.body,"IMPULSE ROLLBACK FAILED; stop simulation and rebuild scene");
            }
            return Failure(Code::NonFiniteResult,{},"Impulse readback invariant failed; entire batch velocities restored; pause and inspect");
        }
        if(m_Backend) {
            CommitImpulseActivation(activation);
            // Activation publishes SDK-authoritative sleep to the mirror, including
            // selected zero-impulse members reached from another nonzero target.
            for(std::size_t i=0;i<results.size();++i)results[i].after.sleeping=staged[i].body->IsSleeping();
        }
        return output;
    } catch(const std::bad_alloc&) {return Failure(Code::Allocation,{},"Impulse staging allocation failed; no impulse applied");}
}
