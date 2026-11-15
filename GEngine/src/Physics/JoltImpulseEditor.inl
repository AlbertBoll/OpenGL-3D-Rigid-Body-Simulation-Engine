// Owner-thread operations outside PhysicsSystem::Update. No new Jolt owner.
PhysicsBodyResult<PhysicsImpulseMotion> ReadImpulseMotion(PhysicsBodyHandle h) const override
{
    if(h.index>=m_Bodies.size())return Error(PhysicsBodyErrorCode::InvalidHandle,h,"Jolt impulse identity");
    const auto& r=m_Bodies[h.index];
    if(r.m_Handle!=h || !r.m_Body)return Error(PhysicsBodyErrorCode::StaleHandle,h,"Jolt impulse stale body");
    if(!r.m_Body->IsDynamic())return Error(PhysicsBodyErrorCode::WrongBodyType,h,"Jolt impulses require Dynamic bodies");
    const auto& n=*r.m_Native;const auto& b=*r.m_Body;const auto* motion=b.GetMotionProperties();
    // Legacy API writes synchronize at Step. Never hide a pending change or flush
    // part of it while validating a batch that may later reject.
    if(n.m_Shape!=r.m_Shape || n.m_Shape->GetRevision()!=r.m_ShapeRevision ||
        (n.m_Position!=r.m_Position && n.m_Position!=V(b.GetPosition())) ||
        (n.m_Orientation!=r.m_Rotation && n.m_Orientation!=Q(b.GetRotation())) ||
        (n.m_LinearVelocity!=r.m_Linear && n.m_LinearVelocity!=V(b.GetLinearVelocity())) ||
        (n.m_AngularVelocity!=r.m_Angular && n.m_AngularVelocity!=V(b.GetAngularVelocity())) ||
        (n.GetInverseMass()!=r.m_InverseMass && n.GetInverseMass()!=motion->GetInverseMass()))
        return Error(PhysicsBodyErrorCode::Busy,h,"Jolt has pending legacy pose/mass/velocity edits; synchronize by resuming, then pause/retry impulse");
    // Safe property transactions can update both copies before the legacy cache is
    // remembered. Agreement with the authoritative body is already synchronized.
    const auto inv=b.GetInverseInertia();
    return PhysicsImpulseMotion{V(b.GetLinearVelocity()),V(b.GetAngularVelocity()),V(b.GetCenterOfMassPosition()),Q(b.GetRotation()),
        Mat3(V(inv.GetColumn3(0)),V(inv.GetColumn3(1)),V(inv.GetColumn3(2))),motion->GetInverseMass(),motion->GetMaxLinearVelocity(),motion->GetMaxAngularVelocity(),!b.IsActive()};
}
void CommitImpulse(const PhysicsImpulseResult& impulse) noexcept override
{
    auto& r=m_Bodies[impulse.body.index];auto& api=m_Physics.GetBodyInterface();const auto id=r.m_Body->GetID();
    if(impulse.impulse!=Vec3f(0)) {
        if(impulse.point)api.AddImpulse(id,V(impulse.impulse),V(*impulse.point));
        else api.AddImpulse(id,V(impulse.impulse));
    }
    if(impulse.angularImpulse!=Vec3f(0))api.AddAngularImpulse(id,V(impulse.angularImpulse));
    auto& native=*r.m_Native;native.m_LinearVelocity=V(api.GetLinearVelocity(id));native.m_AngularVelocity=V(api.GetAngularVelocity(id));
    PublishSleep(native,!r.m_Body->IsActive(),0);r.Remember();
}
void RestoreImpulseMotion(PhysicsBodyHandle h,const PhysicsImpulseMotion& before) noexcept override
{
    auto& r=m_Bodies[h.index];auto& api=m_Physics.GetBodyInterface();const auto id=r.m_Body->GetID();
    api.SetLinearAndAngularVelocity(id,V(before.linear),V(before.angular));
    if(before.sleeping)api.DeactivateBody(id);else api.ActivateBody(id);
    r.m_Native->m_LinearVelocity=V(api.GetLinearVelocity(id));r.m_Native->m_AngularVelocity=V(api.GetAngularVelocity(id));
    PublishSleep(*r.m_Native,before.sleeping,0);r.Remember();
}
