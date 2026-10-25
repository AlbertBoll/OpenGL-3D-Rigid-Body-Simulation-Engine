#include "gepch.h"
#include "PhysicsWorld.h"
#include <atomic>
#include <limits>
#include <new>

namespace GEngine
{
    namespace
    {
        std::atomic<std::uint64_t> s_NextBodyGeneration{1};
        using Code = PhysicsBodyErrorCode;
        auto Failure(Code code, PhysicsBodyHandle handle, std::string_view operation)
        { return std::unexpected(PhysicsBodyError{code, handle, operation}); }
        bool ValidMaterial(const PhysicsMaterial& m)
        {
            return Math::IsFinite(m.friction) && m.friction >= 0 &&
                Math::IsFinite(m.restitution) && m.restitution >= 0 && m.restitution <= 1 &&
                Math::IsFinite(m.spinResistanceLength) && m.spinResistanceLength >= 0 &&
                Math::IsFinite(m.rollingResistanceLength) && m.rollingResistanceLength >= 0;
        }
        PhysicsMaterial MaterialOf(const RigidBody3D& b)
        { return {b.m_Friction, b.m_Elasticity, b.GetSpinResistanceLength(), b.GetRollingResistanceLength()}; }
        bool ValidPose(const Vec3f& p, const Quat& q)
        {
            const float squared = glm::dot(q, q);
            return Math::IsFinite(p) && Math::IsFinite(q) && Math::IsFinite(squared) &&
                squared > Math::NumericalEpsilonSquared;
        }
        template<class T> void ReserveForAppend(std::vector<T>& v, std::size_t count)
        {
            if (v.capacity() < count)
                v.reserve((std::max)(count, v.capacity() + v.capacity() / 2 + 1));
        }
        bool FiniteBounds(const Bounds& b)
        { return Math::IsFinite(b.mins) && Math::IsFinite(b.maxs); }
        bool FiniteMatrix(const Mat3& m)
        { return Math::IsFinite(m[0]) && Math::IsFinite(m[1]) && Math::IsFinite(m[2]); }
        bool FiniteGeometry(const RigidBody3D& b)
        {
            return b.m_Shape && FiniteMatrix(b.m_Shape->InertiaTensor()) &&
                FiniteBounds(b.m_Shape->GetBounds()) && FiniteBounds(b.GetWorldBounds()) &&
                Math::IsFinite(b.GetCenterOfMassWorldSpace()) &&
                FiniteMatrix(b.GetInverseInertiaTensorBodySpace()) &&
                FiniteMatrix(b.GetInverseInertiaTensorWorldSpace());
        }
        bool LinearResultFinite(const RigidBody3D& b, const Vec3f& impulse)
        { return Math::IsFinite(impulse * b.GetInverseMass()) &&
                 Math::IsFinite(b.m_LinearVelocity + impulse * b.GetInverseMass()); }
        bool AngularResultFinite(const RigidBody3D& b, const Vec3f& impulse)
        {
            const auto delta = b.GetInverseInertiaTensorWorldSpace() * impulse;
            const auto velocity = b.m_AngularVelocity + delta;
            return Math::IsFinite(delta) && Math::IsFinite(velocity) && Math::IsFinite(glm::dot(velocity, velocity));
        }
    }

    PhysicsWorld::PhysicsWorld()
    {
        if (auto domain = Asset::AssetDetail::TakeRegistryIdentity(Asset::AssetDetail::nextRegistryIdentity))
            m_Domain = *domain;
    }
    PhysicsWorld::PhysicsWorld(const Vec3f& gravity) : PhysicsWorld() { m_Gravity = gravity; }
    PhysicsWorld::~PhysicsWorld()
    {
        m_Closed = true;
        for (auto* body : m_RigidBodies)
        {
            const auto slot = body->m_Identity.GetSlot() - 1;
            if (m_OwnedResources[slot].retireSceneBinding) m_OwnedResources[slot].retireSceneBinding();
            body->m_Identity = {};
            delete body;
            m_OwnedResources[slot].shape.reset();
        }
    }
    PhysicsBodyResult<> PhysicsWorld::CheckAccess(std::string_view operation, bool mutation) const
    {
        if (std::this_thread::get_id() != m_OwnerThread) return Failure(Code::WrongThread, {}, operation);
        if (m_Closed) return Failure(Code::Closed, {}, operation);
        if (m_Mutating || (mutation && std::any_of(m_RigidBodies.begin(), m_RigidBodies.end(),
                [](const auto* b) { return b->m_InPhysicsStep; }))) return Failure(Code::Busy, {}, operation);
        return {};
    }
    PhysicsBodyResult<RigidBody3D*> PhysicsWorld::Resolve(PhysicsBodyHandle h,
        std::string_view operation, bool mutation) const
    {
        if (auto access = CheckAccess(operation, mutation); !access) return std::unexpected(access.error());
        if (!h) return Failure(Code::InvalidHandle, h, operation);
        if (h.registry != m_Domain) return Failure(Code::WrongWorld, h, operation);
        if (h.index >= m_BodyIdentitySlots.size()) return Failure(Code::StaleHandle, h, operation);
        const auto& slot = m_BodyIdentitySlots[h.index];
        if (!slot.body || slot.generation != h.generation) return Failure(Code::StaleHandle, h, operation);
        return slot.body;
    }
    PhysicsBodyHandle PhysicsWorld::HandleOf(RigidBodyIdentity id) const
    {
        if (!IsBodyIdentityValid(id) || id.GetSlot() > PhysicsBodyHandle::NullIndex) return {};
        return {static_cast<std::uint32_t>(id.GetSlot() - 1), id.GetGeneration(), m_Domain};
    }
    bool PhysicsWorld::IsBodyValid(PhysicsBodyHandle h) const { return bool(Resolve(h, "IsBodyValid")); }
    PhysicsBodyResult<PhysicsBodyHandle> PhysicsWorld::BodyAt(std::size_t index) const
    {
        if (auto access = CheckAccess("BodyAt", false); !access) return std::unexpected(access.error());
        if (index >= m_RigidBodies.size()) return Failure(Code::InvalidHandle, {}, "BodyAt");
        return HandleOf(m_RigidBodies[index]->GetIdentity());
    }
    bool PhysicsWorld::IsBodyIdentityValid(RigidBodyIdentity identity) const
    {
        if (!identity.IsValid()) return false;
        const auto slot = identity.GetSlot() - 1;
        return slot < m_BodyIdentitySlots.size() && m_BodyIdentitySlots[slot].body &&
            m_BodyIdentitySlots[slot].generation == identity.GetGeneration();
    }
    RigidBody3D* PhysicsWorld::CreateRigidBody3D()
    {
        if (!m_Domain || m_Closed || m_Mutating) return nullptr;
        const bool append = m_FreeBodyIdentitySlots.empty();
        if (append && m_BodyIdentitySlots.size() >= PhysicsBodyHandle::NullIndex) return nullptr;
        std::unique_ptr<RigidBody3D> body(new (std::nothrow) RigidBody3D);
        if (!body) return nullptr;
        const auto count = m_BodyIdentitySlots.size() + (append ? 1 : 0);
        // Potentially allocating storage is prepared before publishing any ownership/identity.
        ReserveForAppend(m_RigidBodies, m_RigidBodies.size() + 1);
        ReserveForAppend(m_BodyIdentitySlots, count);
        ReserveForAppend(m_OwnedResources, count);
        ReserveForAppend(m_FreeBodyIdentitySlots, count);
        const auto generation = Asset::AssetDetail::TakeRegistryIdentity(s_NextBodyGeneration);
        if (!generation) return nullptr;
        const auto slot = append ? m_BodyIdentitySlots.size() : std::size_t(m_FreeBodyIdentitySlots.back());
        if (append) { m_BodyIdentitySlots.emplace_back(); m_OwnedResources.emplace_back(); }
        else m_FreeBodyIdentitySlots.pop_back();
        body->m_Identity = RigidBodyIdentity(slot + 1, *generation);
        m_BodyIdentitySlots[slot] = {body.get(), *generation};
        m_RigidBodies.push_back(body.get());
        return body.release();
    }
    PhysicsBodyResult<PhysicsBodyHandle> PhysicsWorld::CreateBody(
        const PhysicsBodyDesc& d, std::unique_ptr<PhysicalShape>& shape)
    {
        constexpr auto op = "CreateBody";
        if (auto access = CheckAccess(op, true); !access) return std::unexpected(access.error());
        if (!m_Domain || s_NextBodyGeneration.load(std::memory_order_relaxed) == 0 ||
            (m_FreeBodyIdentitySlots.empty() && m_BodyIdentitySlots.size() >= PhysicsBodyHandle::NullIndex))
            return Failure(Code::IdentityExhausted, {}, op);
        if (!shape || !shape->IsValid()) return Failure(Code::InvalidShape, {}, op);
        if (!ValidPose(d.position, d.orientation)) return Failure(Code::InvalidPose, {}, op);
        if (d.type != BodyType::Static && d.type != BodyType::Kinematic && d.type != BodyType::Dynamic)
            return Failure(Code::WrongBodyType, {}, op);
        if (!Math::IsFinite(d.mass) || d.mass < 0 || (d.type == BodyType::Dynamic &&
            (d.mass == 0 || !Math::IsFinite(1.f / d.mass) || 1.f / d.mass == 0)))
            return Failure(Code::InvalidMass, {}, op);
        if (!Math::IsFinite(d.linearVelocity) || !Math::IsFinite(d.angularVelocity))
            return Failure(Code::InvalidVelocity, {}, op);
        if (!ValidMaterial(d.material)) return Failure(Code::InvalidMaterial, {}, op);
        RigidBody3D settingsCheck;
        if (!settingsCheck.SetSleepSettings(d.sleep)) return Failure(Code::NotSleepEligible, {}, op);
        settingsCheck.Type = d.type;
        settingsCheck.m_InvMass = d.type == BodyType::Dynamic ? 1.f / d.mass : 0.f;
        settingsCheck.m_Position = d.position;
        settingsCheck.m_Orientation = d.orientation / std::sqrt(glm::dot(d.orientation, d.orientation));
        settingsCheck.m_Shape = shape.get();
        if (!FiniteGeometry(settingsCheck)) return Failure(Code::NonFiniteResult, {}, op);
        auto* b = CreateRigidBody3D();
        if (!b) return Failure(Code::Allocation, {}, op);
        b->Type = d.type;
        b->m_InvMass = d.type == BodyType::Dynamic ? 1.f / d.mass : 0.f;
        b->m_Position = d.position;
        b->m_Orientation = d.orientation / std::sqrt(glm::dot(d.orientation, d.orientation));
        b->m_LinearVelocity = d.linearVelocity; b->m_AngularVelocity = d.angularVelocity;
        b->m_Friction = d.material.friction; b->m_Elasticity = d.material.restitution;
        b->m_CollisionLayer = d.collisionLayer; b->m_CollisionMask = d.collisionMask;
        b->m_Shape = shape.get();
        b->SetSpinResistanceLength(d.material.spinResistanceLength);
        b->SetRollingResistanceLength(d.material.rollingResistanceLength);
        b->SetSleepSettings(d.sleep);
        b->WakeUp();
        const auto handle = HandleOf(b->GetIdentity());
        m_OwnedResources[handle.index].shape = std::move(shape);
        return handle;
    }
    void PhysicsWorld::SetSceneRetirement(PhysicsBodyHandle h, std::function<void()> callback)
    {
        if (Resolve(h, "SetSceneRetirement")) m_OwnedResources[h.index].retireSceneBinding = std::move(callback);
    }
    void PhysicsWorld::ReleaseBodyIdentity(RigidBody3D* body)
    {
        const auto slot = body->m_Identity.GetSlot() - 1;
        m_BodyIdentitySlots[slot].body = nullptr;
        m_FreeBodyIdentitySlots.push_back(slot); // Capacity prepared before body publication.
        body->m_Identity = {};
    }
    void PhysicsWorld::RemoveRigidBody3D(RigidBody3D* body)
    {
        const auto found = std::find(m_RigidBodies.begin(), m_RigidBodies.end(), body);
        if (found == m_RigidBodies.end() || m_Mutating) return; // Never dereference a stale/foreign pointer.
        m_Mutating = true;
        const auto slot = body->m_Identity.GetSlot() - 1;
        if (m_BodyRemovalCallback) m_BodyRemovalCallback(body);
        auto retiring = std::move(m_OwnedResources[slot].retireSceneBinding);
        if (retiring) retiring(); // Disconnect shape borrowers before destroying either owner.
        m_RigidBodies.erase(found);
        ReleaseBodyIdentity(body);
        delete body;
        m_OwnedResources[slot].shape.reset();
        m_Mutating = false;
    }
    PhysicsBodyResult<> PhysicsWorld::DestroyBody(PhysicsBodyHandle h)
    {
        auto body = Resolve(h, "DestroyBody", true);
        if (!body) return std::unexpected(body.error());
        RemoveRigidBody3D(*body);
        return {};
    }
    PhysicsBodyResult<> PhysicsWorld::Close()
    {
        if (std::this_thread::get_id() != m_OwnerThread) return Failure(Code::WrongThread, {}, "Close");
        if (m_Closed) return {};
        if (auto access = CheckAccess("Close", true); !access) return access;
        while (!m_RigidBodies.empty()) RemoveRigidBody3D(m_RigidBodies.back());
        m_Closed = true;
        return {};
    }
    PhysicsBodyResult<PhysicsBodyState> PhysicsWorld::ReadBody(PhysicsBodyHandle h) const
    {
        auto resolved = Resolve(h, "ReadBody");
        if (!resolved) return std::unexpected(resolved.error());
        const auto& b = **resolved;
        if (!b.m_Shape || !b.m_Shape->IsValid()) return Failure(Code::InvalidShape, h, "ReadBody");
        if (!b.HasFiniteState() || !FiniteGeometry(b)) return Failure(Code::NonFiniteResult, h, "ReadBody");
        return PhysicsBodyState{h, b.Type, b.m_Position, b.GetLinearVelocity(), b.GetAngularVelocity(),
            b.m_Orientation, b.GetInverseMass(), MaterialOf(b), b.m_Shape->GetShapeType(),
            b.m_Shape->GetBounds(), b.GetWorldBounds(), b.GetCenterOfMassWorldSpace(),
            b.GetInverseInertiaTensorBodySpace(), b.GetInverseInertiaTensorWorldSpace(),
            b.GetSleepSettings(), b.GetInactiveSeconds(), b.IsSleeping()};
    }
    PhysicsBodyResult<Bounds> PhysicsWorld::GetBodyBoundsAt(PhysicsBodyHandle h, const Vec3f& p, const Quat& q) const
    {
        auto b = Resolve(h, "GetBodyBoundsAt");
        if (!b) return std::unexpected(b.error());
        if (!ValidPose(p, q)) return Failure(Code::InvalidPose, h, "GetBodyBoundsAt");
        if (!(*b)->m_Shape) return Failure(Code::InvalidShape, h, "GetBodyBoundsAt");
        const auto bounds = (*b)->m_Shape->GetBounds(p, q);
        if (!FiniteBounds(bounds)) return Failure(Code::NonFiniteResult, h, "GetBodyBoundsAt");
        return bounds;
    }
    PhysicsBodyResult<> PhysicsWorld::SetBodyVelocity(PhysicsBodyHandle h, const Vec3f& v, const Vec3f& w)
    {
        auto b = Resolve(h, "SetBodyVelocity", true); if (!b) return std::unexpected(b.error());
        if (!Math::IsFinite(v) || !Math::IsFinite(w)) return Failure(Code::InvalidVelocity, h, "SetBodyVelocity");
        if ((*b)->m_LinearVelocity != v || (*b)->m_AngularVelocity != w) (*b)->WakeUp();
        (*b)->m_LinearVelocity = v; (*b)->m_AngularVelocity = w;
        return {};
    }
    PhysicsBodyResult<> PhysicsWorld::SetBodyMass(PhysicsBodyHandle h, float mass)
    {
        auto b = Resolve(h, "SetBodyMass", true); if (!b) return std::unexpected(b.error());
        if ((*b)->Type != BodyType::Dynamic) return Failure(Code::WrongBodyType, h, "SetBodyMass");
        if (!Math::IsFinite(mass) || mass <= 0 || !Math::IsFinite(1.f / mass) || 1.f / mass == 0)
            return Failure(Code::InvalidMass, h, "SetBodyMass");
        auto proposed = **b; // Validate on a value copy; rejection cannot alter mass or wake state.
        proposed.SetBodyTypeAndInverseMass(BodyType::Dynamic, 1.f / mass);
        if (!FiniteGeometry(proposed)) return Failure(Code::NonFiniteResult, h, "SetBodyMass");
        (*b)->SetBodyTypeAndInverseMass(BodyType::Dynamic, 1.f / mass);
        return {};
    }
    PhysicsBodyResult<> PhysicsWorld::SetBodyMaterial(PhysicsBodyHandle h, const PhysicsMaterial& m)
    {
        auto b = Resolve(h, "SetBodyMaterial", true); if (!b) return std::unexpected(b.error());
        if (!ValidMaterial(m)) return Failure(Code::InvalidMaterial, h, "SetBodyMaterial");
        if (MaterialOf(**b) != m) (*b)->WakeUp();
        (*b)->m_Friction = m.friction; (*b)->m_Elasticity = m.restitution;
        (*b)->SetSpinResistanceLength(m.spinResistanceLength);
        (*b)->SetRollingResistanceLength(m.rollingResistanceLength);
        return {};
    }
    PhysicsBodyResult<> PhysicsWorld::ApplyLinearImpulse(PhysicsBodyHandle h, const Vec3f& impulse)
    {
        auto b = Resolve(h, "ApplyLinearImpulse", true); if (!b) return std::unexpected(b.error());
        if (!Math::IsFinite(impulse)) return Failure(Code::InvalidForce, h, "ApplyLinearImpulse");
        if ((*b)->Type != BodyType::Dynamic) return Failure(Code::WrongBodyType, h, "ApplyLinearImpulse");
        if (!LinearResultFinite(**b, impulse)) return Failure(Code::NonFiniteResult, h, "ApplyLinearImpulse");
        (*b)->ApplyImpulseLinear(impulse); return {};
    }
    PhysicsBodyResult<> PhysicsWorld::ApplyAngularImpulse(PhysicsBodyHandle h, const Vec3f& impulse)
    {
        auto b = Resolve(h, "ApplyAngularImpulse", true); if (!b) return std::unexpected(b.error());
        if (!Math::IsFinite(impulse)) return Failure(Code::InvalidForce, h, "ApplyAngularImpulse");
        if ((*b)->Type != BodyType::Dynamic) return Failure(Code::WrongBodyType, h, "ApplyAngularImpulse");
        if (!AngularResultFinite(**b, impulse)) return Failure(Code::NonFiniteResult, h, "ApplyAngularImpulse");
        (*b)->ApplyImpulseAngular(impulse); return {};
    }
    PhysicsBodyResult<> PhysicsWorld::ApplyImpulse(PhysicsBodyHandle h, const Vec3f& point, const Vec3f& impulse)
    {
        auto b = Resolve(h, "ApplyImpulse", true); if (!b) return std::unexpected(b.error());
        if (!Math::IsFinite(point) || !Math::IsFinite(impulse)) return Failure(Code::InvalidForce, h, "ApplyImpulse");
        if ((*b)->Type != BodyType::Dynamic) return Failure(Code::WrongBodyType, h, "ApplyImpulse");
        const auto lever = point - (*b)->GetCenterOfMassWorldSpace();
        const auto angular = glm::cross(lever, impulse);
        if (!Math::IsFinite(lever) || !Math::IsFinite(angular) || !LinearResultFinite(**b, impulse) ||
            !AngularResultFinite(**b, angular)) return Failure(Code::NonFiniteResult, h, "ApplyImpulse");
        (*b)->ApplyImpulse(point, impulse); return {};
    }
    PhysicsBodyResult<> PhysicsWorld::ApplyForceForDuration(PhysicsBodyHandle h, const Vec3f& force, float seconds)
    {
        auto b = Resolve(h, "ApplyForceForDuration", true); if (!b) return std::unexpected(b.error());
        if (!Math::IsFinite(force)) return Failure(Code::InvalidForce, h, "ApplyForceForDuration");
        if (!Math::IsFinite(seconds) || seconds <= 0) return Failure(Code::InvalidDuration, h, "ApplyForceForDuration");
        if (!Math::IsFinite(force * seconds)) return Failure(Code::NonFiniteResult, h, "ApplyForceForDuration");
        return ApplyLinearImpulse(h, force * seconds);
    }
    PhysicsBodyResult<> PhysicsWorld::SetBodySleepSettings(PhysicsBodyHandle h, const RigidBody3D::SleepSettings& settings)
    {
        auto b = Resolve(h, "SetBodySleepSettings", true); if (!b) return std::unexpected(b.error());
        if (!(*b)->SetSleepSettings(settings)) return Failure(Code::NotSleepEligible, h, "SetBodySleepSettings");
        return {};
    }
    PhysicsBodyResult<> PhysicsWorld::TrySleepBody(PhysicsBodyHandle h)
    {
        auto b = Resolve(h, "TrySleepBody", true); if (!b) return std::unexpected(b.error());
        if (!(*b)->CanSleep()) return Failure(Code::NotSleepEligible, h, "TrySleepBody");
        (*b)->TrySleep(); return {};
    }
    PhysicsBodyResult<> PhysicsWorld::WakeBody(PhysicsBodyHandle h)
    {
        auto b = Resolve(h, "WakeBody", true); if (!b) return std::unexpected(b.error());
        (*b)->WakeUp(); return {};
    }
}
