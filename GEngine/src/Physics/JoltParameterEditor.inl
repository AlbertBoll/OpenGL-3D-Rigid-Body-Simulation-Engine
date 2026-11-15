// Included inside JoltWorld. Public settings and the version-pinned authored
// contact-resistance settings share one listener; diagnostics are optional.
static PhysicsJoltSettings Parameters(const JPH::PhysicsSettings& s,int steps) noexcept {
    return {int(s.mNumVelocitySteps),int(s.mNumPositionSteps),steps,s.mMinVelocityForRestitution,s.mBaumgarte,
        s.mPenetrationSlop,s.mSpeculativeContactDistance,s.mAllowSleeping,s.mTimeBeforeSleep,s.mPointVelocitySleepThreshold,
        s.mConstraintWarmStart,s.mUseBodyPairContactCache,s.mUseManifoldReduction};
}
int m_CollisionSteps{1};bool m_UserAllowSleeping{true};
PhysicsJoltSettings ReadJoltSettings() const noexcept override {return Parameters(m_Physics.GetPhysicsSettings(),m_CollisionSteps);}
PhysicsJoltSettings ReadJoltNativeDefaults() const noexcept override {return Parameters(JPH::PhysicsSettings{},1);}
void UpdateJoltSettings(const PhysicsJoltSettings& p) noexcept override {
    auto s=m_Physics.GetPhysicsSettings(); // Preserve every unrelated SDK field.
    s.mNumVelocitySteps=unsigned(p.velocitySteps);s.mNumPositionSteps=unsigned(p.positionSteps);
    s.mMinVelocityForRestitution=p.minRestitutionVelocity;s.mBaumgarte=p.baumgarte;
    s.mPenetrationSlop=p.penetrationSlop;s.mSpeculativeContactDistance=p.speculativeDistance;
    s.mAllowSleeping=p.allowSleeping;s.mTimeBeforeSleep=p.timeBeforeSleep;s.mPointVelocitySleepThreshold=p.pointVelocitySleepThreshold;
    s.mConstraintWarmStart=p.warmStarting;s.mUseBodyPairContactCache=p.bodyPairCache;s.mUseManifoldReduction=p.manifoldReduction;
    m_Physics.SetPhysicsSettings(s);m_CollisionSteps=p.collisionSteps;m_UserAllowSleeping=p.allowSleeping;m_AllowSleeping=p.allowSleeping;
    auto& api=m_Physics.GetBodyInterface();
    for(auto& r:m_Bodies)if(r.m_Body){
        api.InvalidateContactCache(r.m_Body->GetID());
        if(!r.m_Body->IsStatic()){Wake(r.m_Handle);r.m_Body->ResetSleepTimer();PublishSleep(*r.m_Native,false,0);r.Remember();}
    }
    for(auto& joint:m_Joints)if(joint.m_Constraint)joint.m_Constraint->ResetWarmStart();
}
struct DiagnosticCapture final {
    static constexpr std::size_t Capacity=8192;
    JoltWorld& owner;std::atomic<std::size_t> count{0};
    std::array<PhysicsJoltContactSample,Capacity> samples{};
    std::uint64_t step{};double milliseconds{};
    explicit DiagnosticCapture(JoltWorld& o):owner(o){}
    void Capture(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& manifold,const JPH::ContactSettings& s,bool added) noexcept {
        if(manifold.mRelativeContactPointsOn1.empty() || manifold.mRelativeContactPointsOn2.empty())return;
        const auto slot=count.fetch_add(1,std::memory_order_relaxed);if(slot>=Capacity)return;
        auto& out=samples[slot];out={};out.bodyA=owner.m_Bodies[a.GetObjectLayer()>>1].m_Handle;out.bodyB=owner.m_Bodies[b.GetObjectLayer()>>1].m_Handle;
        out.normal=V(manifold.mWorldSpaceNormal);out.penetration=manifold.mPenetrationDepth;
        out.friction=s.mCombinedFriction;out.restitution=s.mCombinedRestitution;out.added=added;
        const auto pa=manifold.GetWorldSpaceContactPointOn1(0),pb=manifold.GetWorldSpaceContactPointOn2(0);
        out.localPointA=V(a.GetInverseCenterOfMassTransform()*pa);out.localPointB=V(b.GetInverseCenterOfMassTransform()*pb);
        out.normalVelocityBefore=(b.GetPointVelocity(pb)-a.GetPointVelocity(pa)).Dot(manifold.mWorldSpaceNormal);
    }
    void Finish(double ms) noexcept {
        milliseconds=ms;++step;
        for(std::size_t i=0;i<std::min(count.load(),Capacity);++i){auto& s=samples[i];
            const auto& a=*owner.m_Bodies[s.bodyA.index].m_Body;const auto& b=*owner.m_Bodies[s.bodyB.index].m_Body;
            const auto pa=a.GetCenterOfMassTransform()*V(s.localPointA),pb=b.GetCenterOfMassTransform()*V(s.localPointB);
            s.normalVelocityAfter=(b.GetPointVelocity(pb)-a.GetPointVelocity(pa)).Dot(V(s.normal));
        }
    }
};
std::unique_ptr<DiagnosticCapture> m_Diagnostic;
std::size_t m_ResistanceBodyCount{};
std::atomic<bool> m_ContactIdentityFailure{false};
static bool ValidResistance(const RigidBody3D& body) noexcept {
    return std::isfinite(body.GetSpinResistanceLength()) && body.GetSpinResistanceLength()>=0 &&
        std::isfinite(body.GetRollingResistanceLength()) && body.GetRollingResistanceLength()>=0;
}
bool ValidContactRecord(const BodyRecord& record) const noexcept {
    if(!record.m_Native || !record.m_Body || !record.m_Handle ||
        record.m_Handle.index>=m_Bodies.size() || &m_Bodies[record.m_Handle.index]!=&record ||
        record.m_Id!=record.m_Body->GetID())return false;
    const auto identity=record.m_Native->GetIdentity();
    return identity.IsValid() && identity.GetSlot()==std::uint64_t(record.m_Handle.index)+1 &&
        identity.GetGeneration()==record.m_Handle.generation;
}
struct MaterialContacts final : JPH::ContactListener {
    JoltWorld& owner;
    explicit MaterialContacts(JoltWorld& value):owner(value){}
    void Configure(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& manifold,JPH::ContactSettings& settings,bool added) noexcept {
        const std::size_t ia=a.GetObjectLayer()>>1,ib=b.GetObjectLayer()>>1;
        if(ia>=owner.m_Bodies.size() || ib>=owner.m_Bodies.size()) {
            owner.m_ContactIdentityFailure.store(true,std::memory_order_relaxed);return;
        }
        const auto& ra=owner.m_Bodies[ia];const auto& rb=owner.m_Bodies[ib];
        if(ra.m_Body!=&a || rb.m_Body!=&b || !owner.ValidContactRecord(ra) || !owner.ValidContactRecord(rb)) {
            owner.m_ContactIdentityFailure.store(true,std::memory_order_relaxed);return;
        }
        // No mutable Native material reads or body API/locks inside worker callbacks.
        settings.mCombinedSpinResistanceLength=std::min(ra.m_SpinLength,rb.m_SpinLength);
        settings.mCombinedRollingResistanceLength=std::min(ra.m_RollingLength,rb.m_RollingLength);
#ifdef GENGINE_G8_RESISTANCE_CHECKS
        settings.mContactResistanceObserver=G8ResistanceObservation::observer.load(std::memory_order_relaxed);
#endif
        if(owner.m_Diagnostic)owner.m_Diagnostic->Capture(a,b,manifold,settings,added);
    }
    void OnContactAdded(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& m,JPH::ContactSettings& s) override {Configure(a,b,m,s,true);}
    void OnContactPersisted(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& m,JPH::ContactSettings& s) override {Configure(a,b,m,s,false);}
} m_MaterialContacts{*this};
void RefreshContactListener() noexcept {
    m_Physics.SetContactListener(m_ResistanceBodyCount || m_Diagnostic ? &m_MaterialContacts : nullptr);
}
void PublishResistance(BodyRecord& record,float spin,float rolling,bool invalidate=true) noexcept {
    if(record.m_SpinLength==spin && record.m_RollingLength==rolling)return;
    const bool oldPositive=record.m_SpinLength>0 || record.m_RollingLength>0;
    const bool newPositive=spin>0 || rolling>0;
    if(oldPositive!=newPositive) {
        if(newPositive)++m_ResistanceBodyCount;else --m_ResistanceBodyCount;
    }
    record.m_SpinLength=spin;record.m_RollingLength=rolling;
    if(invalidate)m_Physics.GetBodyInterface().InvalidateContactCache(record.m_Id);
    if(oldPositive!=newPositive)RefreshContactListener();
}
PhysicsBodyResult<> EnableJoltDiagnostics(bool enabled) override {
    if(enabled && !m_Diagnostic){
        auto next=std::unique_ptr<DiagnosticCapture>(new(std::nothrow) DiagnosticCapture(*this));
        if(!next)return Error(PhysicsBodyErrorCode::Allocation,{},"Jolt contact diagnostic storage");
        m_Diagnostic=std::move(next);RefreshContactListener();
    } else if(!enabled && m_Diagnostic){m_Diagnostic.reset();RefreshContactListener();}
    return {};
}
PhysicsJoltDiagnosticView ReadJoltDiagnostics() const noexcept override {
    if(!m_Diagnostic)return {};
    const auto count=m_Diagnostic->count.load();const auto size=std::min(count,DiagnosticCapture::Capacity);
    return {true,m_Diagnostic->step,count,count-size,m_Diagnostic->milliseconds,{m_Diagnostic->samples.data(),size}};
}
