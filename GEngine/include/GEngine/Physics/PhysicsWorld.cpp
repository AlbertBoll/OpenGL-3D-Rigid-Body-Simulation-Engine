#include "gepch.h"
#include "../../../src/Physics/EditorPlacementBroadphase.h"
#include "PhysicsWorld.h"
#include "GJK.h"
#include "../../../src/Physics/EditorScale.h"
#include "../../../src/Physics/WorldBackend.h"
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
            return b.m_Shape && b.HasValidMassProperties() && FiniteMatrix(b.m_Shape->InertiaTensor()) &&
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
        bool PropertiesMatch(const PhysicsBodyProperties& actual,PhysicsBodyProperties requested)
        {
            // The established effective mass is read back from its Float32
            // reciprocal. Keep the authored value in OwnedResources unchanged.
            if(requested.type==BodyType::Dynamic)requested.mass=1.f/(1.f/requested.mass);
            return actual==requested;
        }
        bool PublishedMassMatches(const WorldBackend* backend,PhysicsBodyHandle h,const PreparedBackendMass& mass)
        {
            if(!backend)return true;
            const auto actual=backend->CaptureMass(h);
            return actual && *actual==mass;
        }
    }

    PhysicsWorld::PhysicsWorld(UnconfiguredTag)
    {
        if (auto domain = Asset::AssetDetail::TakeRegistryIdentity(Asset::AssetDetail::nextRegistryIdentity))
            m_Domain = *domain;
    }
    PhysicsWorld::PhysicsWorld() : PhysicsWorld(DefaultPhysicsBackend) {}
    PhysicsWorld::PhysicsWorld(PhysicsBackend backend) : PhysicsWorld(UnconfiguredTag{})
    {
        if (auto initialized = ConfigureBackend(backend); !initialized) throw initialized.error();
    }
    PhysicsBodyResult<std::unique_ptr<PhysicsWorld>> PhysicsWorld::Create(PhysicsBackend backend)
    {
        std::unique_ptr<PhysicsWorld> world(new (std::nothrow) PhysicsWorld(UnconfiguredTag{}));
        if (!world) return Failure(Code::Allocation, {}, "PhysicsWorld::Create");
        if (auto initialized = world->ConfigureBackend(backend); !initialized)
            return std::unexpected(initialized.error());
        return std::move(world);
    }
    PhysicsBodyResult<> PhysicsWorld::ConfigureBackend(PhysicsBackend backend)
    {
        if(auto access=CheckAccess("ConfigureBackend",true);!access)return access;
        if(!m_RigidBodies.empty() || m_JointCount)return Failure(Code::Busy,{},"Backend selection requires an empty world");
        if(!PhysicsBackendAvailable(backend))return Failure(Code::BackendUnavailable,{},"ConfigureBackend");
        if(backend==m_BackendKind)return {};
        if(backend==PhysicsBackend::Jolt)
        {
            auto created=CreateJoltBackend();if(!created)return std::unexpected(created.error());
            m_Backend=std::move(*created);
        }
        else m_Backend.reset();
        m_BackendKind=backend;return {};
    }
    PhysicsWorld::PhysicsWorld(const Vec3f& gravity) : PhysicsWorld(DefaultPhysicsBackend, gravity) {}
    PhysicsWorld::PhysicsWorld(PhysicsBackend backend, const Vec3f& gravity) : PhysicsWorld(backend)
    { m_Gravity = gravity; }
    PhysicsBodyResult<PhysicsJoltSettings> PhysicsWorld::ReadJoltSettings() const {
        if(auto a=CheckAccess("ReadJoltSettings",false);!a)return std::unexpected(a.error());
        if(!m_Backend)return Failure(Code::UnsupportedFeature,{},"Jolt settings are unavailable on Native");
        return m_Backend->ReadJoltSettings();
    }
    PhysicsBodyResult<PhysicsJoltSettings> PhysicsWorld::ReadJoltNativeDefaults() const {
        if(auto a=CheckAccess("ReadJoltNativeDefaults",false);!a)return std::unexpected(a.error());
        if(!m_Backend)return Failure(Code::UnsupportedFeature,{},"Jolt defaults are unavailable on Native");
        return m_Backend->ReadJoltNativeDefaults();
    }
    PhysicsBodyResult<> PhysicsWorld::UpdateJoltSettings(const PhysicsJoltSettings& value) {
        if(auto a=CheckAccess("UpdateJoltSettings",true);!a)return a;
        if(!m_Backend)return Failure(Code::UnsupportedFeature,{},"Jolt settings are unavailable on Native");
        if(const auto* why=ValidateJoltSettings(value))return Failure(Code::InvalidMaterial,{},why);
        if(value!=m_Backend->ReadJoltSettings())m_Backend->UpdateJoltSettings(value);
        return {};
    }
    PhysicsBodyResult<> PhysicsWorld::EnableJoltDiagnostics(bool enabled) {
        if(auto a=CheckAccess("EnableJoltDiagnostics",true);!a)return a;
        if(!m_Backend)return Failure(Code::UnsupportedFeature,{},"Jolt contact observation is unavailable on Native");
        return m_Backend->EnableJoltDiagnostics(enabled);
    }
    PhysicsBodyResult<PhysicsJoltDiagnosticView> PhysicsWorld::ReadJoltDiagnostics() const {
        if(auto a=CheckAccess("ReadJoltDiagnostics",false);!a)return std::unexpected(a.error());
        if(!m_Backend)return Failure(Code::UnsupportedFeature,{},"Jolt contact observation is unavailable on Native");
        return m_Backend->ReadJoltDiagnostics();
    }
    PhysicsBodyResult<PhysicsWorldSleepSettings> PhysicsWorld::ReadWorldSleepSettings() const
    {
        if(auto access=CheckAccess("ReadWorldSleepSettings",false);!access)return std::unexpected(access.error());
        if(!m_Backend)return Failure(Code::UnsupportedFeature,{},"Native uses per-body sleep thresholds");
        return m_Backend->ReadWorldSleepSettings();
    }
    PhysicsBodyResult<> PhysicsWorld::UpdateWorldSleepSettings(const PhysicsWorldSleepSettings& value)
    {
        if(auto access=CheckAccess("UpdateWorldSleepSettings",true);!access)return access;
        if(!m_Backend)return Failure(Code::UnsupportedFeature,{},"Native uses per-body sleep thresholds");
        const float movement=value.pointVelocityThreshold*value.timeBeforeSleep;
        if(!Math::IsFinite(value.pointVelocityThreshold) || value.pointVelocityThreshold<=0 ||
            !Math::IsFinite(value.timeBeforeSleep) || value.timeBeforeSleep<=0 ||
            !Math::IsFinite(movement) || movement<=0)
            return Failure(Code::InvalidDuration,{},"Sleep velocity/time and their product must be finite and positive");
        if(value!=m_Backend->ReadWorldSleepSettings())m_Backend->UpdateWorldSleepSettings(value);
        return {};
    }
    PhysicsWorld::~PhysicsWorld()
    {
        ReleaseTerminalStorage();
    }
    void PhysicsWorld::ReleaseTerminalStorage() noexcept
    {
        m_Closed = true;
        if (m_StepError && m_Backend) m_Backend->PrepareTerminalRetirement();
        m_Backend.reset(); // Retire constraints/workers while native mirrors are alive.
        m_Joints.reset(); m_JointCount = 0;
        for (auto* body : m_RigidBodies)
        {
            const auto slot = body->m_Identity.GetSlot() - 1;
            if (m_OwnedResources[slot].retireSceneBinding) m_OwnedResources[slot].retireSceneBinding();
            body->m_Identity = {};
            delete body;
            m_OwnedResources[slot].shape.reset();
        }
        m_RigidBodies.clear();
        m_BodyIdentitySlots.clear();
        m_OwnedResources.clear();
        m_EditorPlacementShapes.clear();
    }
    PhysicsBodyResult<> PhysicsWorld::CheckAccess(std::string_view operation, bool mutation, bool authoredRead) const
    {
        if (std::this_thread::get_id() != m_OwnerThread) return Failure(Code::WrongThread, {}, operation);
        if (m_Closed) return Failure(Code::Closed, {}, operation);
        if (m_StepError && !(authoredRead && !mutation && m_StepStage == PhysicsStepStage::Preparation))
            return std::unexpected(*m_StepError);
        if (m_Mutating || (mutation && std::any_of(m_RigidBodies.begin(), m_RigidBodies.end(),
                [](const auto* b) { return b->m_InPhysicsStep; }))) return Failure(Code::Busy, {}, operation);
        return {};
    }
    PhysicsBodyResult<RigidBody3D*> PhysicsWorld::Resolve(PhysicsBodyHandle h,
        std::string_view operation, bool mutation, bool authoredRead) const
    {
        if (auto access = CheckAccess(operation, mutation, authoredRead); !access) return std::unexpected(access.error());
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
        if (!m_Domain || m_Closed || m_Mutating || m_StepError) return nullptr;
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
        if (!settingsCheck.HasValidMassProperties()) return Failure(Code::InvalidMass, {}, "CreateBody: invalid or unrepresentable inertia pair");
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
        if(m_Backend)
        {
            auto created=m_Backend->CreateBody(handle,*b);
            if(!created){RemoveRigidBody3D(b);return std::unexpected(created.error());}
        }
        m_OwnedResources[handle.index].shape = std::move(shape);
        auto& properties = m_OwnedResources[handle.index].properties;
        properties = {};
        properties.type = d.type; properties.mass = d.mass > 0 ? d.mass : 1.f;
        properties.material = d.material; properties.sleep = d.sleep;
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
        RetireJoints(body);
        if(m_Backend)m_Backend->DestroyBody(HandleOf(body->GetIdentity()));
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
        if (m_Mutating || m_StepActive) return Failure(Code::Busy, {}, "Close");
        if (m_StepError) { ReleaseTerminalStorage(); return {}; }
        if (auto access = CheckAccess("Close", true); !access) return access;
        while (!m_RigidBodies.empty()) RemoveRigidBody3D(m_RigidBodies.back());
        m_EditorPlacementShapes.clear();
        m_Backend.reset();
        m_Closed = true;
        return {};
    }
    PhysicsBodyResult<PhysicsBodyState> PhysicsWorld::ReadBody(PhysicsBodyHandle h) const
    {
        auto resolved = Resolve(h, "ReadBody", false, true);
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
    PhysicsBodyResult<PhysicsBodyState> PhysicsWorld::ReadEffectiveBody(PhysicsBodyHandle h) const
    {
        if (auto idle = CheckAccess("ReadEffectiveBody", true); !idle)
            return std::unexpected(idle.error());
        auto state = ReadBody(h);
        if (!state || !m_Backend) return state;
        if (auto read = m_Backend->ReadEffectiveBody(*state); !read)
            return std::unexpected(read.error());
        if (!ValidPose(state->position, state->orientation) || !Math::IsFinite(state->centerOfMass) ||
            !Math::IsFinite(state->linearVelocity) || !Math::IsFinite(state->angularVelocity) ||
            !std::isfinite(state->inverseMass) || state->inverseMass < 0 || !ValidMaterial(state->material) ||
            !FiniteBounds(state->localBounds) || !FiniteBounds(state->worldBounds) ||
            !FiniteMatrix(state->inverseInertiaBody) || !FiniteMatrix(state->inverseInertiaWorld))
            return Failure(Code::NonFiniteResult, h, "ReadEffectiveBody");
        return state;
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
        if (!proposed.HasValidMassProperties()) return Failure(Code::InvalidMass, h, "SetBodyMass: invalid or unrepresentable inertia pair");
        if (!FiniteGeometry(proposed)) return Failure(Code::NonFiniteResult, h, "SetBodyMass");
        if(m_Backend) {
            auto prepared=m_Backend->PrepareMass(h,proposed);
            if(!prepared)return std::unexpected(prepared.error());
        }
        InvalidateJoints(*b);
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

    PhysicsBodyResult<PhysicsBodyProperties> PhysicsWorld::ReadBodyProperties(PhysicsBodyHandle h) const
    {
        auto body = Resolve(h, "ReadBodyProperties");
        if (!body) return std::unexpected(body.error());
        auto value = m_OwnedResources[h.index].properties;
        value.type = (*body)->Type;
        if ((*body)->GetInverseMass() > 0) value.mass = 1.f / (*body)->GetInverseMass();
        value.material = MaterialOf(**body); value.sleep = (*body)->GetSleepSettings();
        if (m_Backend) m_Backend->ReadProperties(h, value);
        return value;
    }

    PhysicsBodyResult<RigidBody3D> PhysicsWorld::PrepareBodyProperties(PhysicsBodyHandle h, const PhysicsBodyProperties& value) const
    {
        constexpr auto op = "UpdateBodyProperties";
        auto body = Resolve(h, op, true); if (!body) return std::unexpected(body.error());
        if (value.type != BodyType::Static && value.type != BodyType::Kinematic && value.type != BodyType::Dynamic)
            return Failure(Code::WrongBodyType, h, op);
        if (!Math::IsFinite(value.mass) || value.mass <= 0 || !Math::IsFinite(1.f/value.mass) || 1.f/value.mass == 0)
            return Failure(Code::InvalidMass, h, op);
        if (!ValidMaterial(value.material)) return Failure(Code::InvalidMaterial, h, op);
        if (!Math::IsFinite(value.linearDamping) || value.linearDamping < 0 || value.linearDamping > 1 ||
            !Math::IsFinite(value.angularDamping) || value.angularDamping < 0 || value.angularDamping > 1 ||
            !Math::IsFinite(value.gravityFactor) || value.gravityFactor < 0 || value.gravityFactor > 10)
            return Failure(Code::InvalidMaterial, h, "Damping requires [0,1]; gravity factor [0,10]");
        if(value.velocityStepsOverride<0 || value.velocityStepsOverride>255 || value.velocityStepsOverride==1 ||
           value.positionStepsOverride<0 || value.positionStepsOverride>255)
            return Failure(Code::InvalidMaterial,h,"Iteration overrides: velocity 0 or 2..255; position 0..255; zero inherits world.");
        if((value.velocityStepsOverride || value.positionStepsOverride) && (!m_Backend || value.type!=BodyType::Dynamic))
            return Failure(Code::UnsupportedFeature,h,"Solver overrides require a Dynamic Jolt body; clear overrides before changing motion type/backend.");
        if (!m_Backend && (value.linearDamping != 0 || value.angularDamping != 0 || value.gravityFactor != 1 ||
            !value.continuousCollision || !value.allowSleeping))
            return Failure(Code::UnsupportedFeature, h, "Native has fixed per-body damping/gravity/CCD/sleep-enable policy");
        if (m_Backend && value.sleep != RigidBody3D::SleepSettings{})
            return Failure(Code::UnsupportedFeature, h, "Jolt does not map Native sleep thresholds");
        // Changing an endpoint to fixed must not leave a constraint without mass response.
        if (value.type != BodyType::Dynamic)
            for (std::size_t i=0; i<MaxDistanceJoints && m_Joints; ++i) if (m_Joints[i].live)
            {
                const auto& d=m_Joints[i].row.desc;
                if (d.bodyA==h || d.bodyB==h)
                {
                    auto* other=ResolveJointBody(d.bodyA==h?d.bodyB:d.bodyA);
                    if (!other || other->Type != BodyType::Dynamic)
                        return Failure(Code::WrongBodyType, h, "Joint requires at least one Dynamic endpoint");
                }
            }
        auto proposed=**body;
        if (!proposed.SetSleepSettings(value.sleep)) return Failure(Code::NotSleepEligible,h,op);
        proposed.SetBodyTypeAndInverseMass(value.type,value.type==BodyType::Dynamic?1.f/value.mass:0.f);
        proposed.m_Friction=value.material.friction; proposed.m_Elasticity=value.material.restitution;
        proposed.SetSpinResistanceLength(value.material.spinResistanceLength);
        proposed.SetRollingResistanceLength(value.material.rollingResistanceLength);
        if (value.type==BodyType::Static) proposed.m_LinearVelocity=proposed.m_AngularVelocity=Vec3f(0);
        if (!proposed.HasValidMassProperties()) return Failure(Code::InvalidMass,h,"UpdateBodyProperties: invalid or unrepresentable inertia pair");
        if (!FiniteGeometry(proposed)) return Failure(Code::NonFiniteResult,h,op);
        return proposed;
    }
    void PhysicsWorld::PublishBodyProperties(PhysicsBodyHandle h,RigidBody3D* body,const RigidBody3D& proposed,const PhysicsBodyProperties& value,const PreparedBackendMass& mass) noexcept
    {
        if(m_Backend)m_Backend->UpdateProperties(h,proposed,value,mass);
        *body=proposed;body->WakeUp();m_OwnedResources[h.index].properties=value;InvalidateJoints(body);
    }
    PhysicsBodyResult<> PhysicsWorld::UpdateBodyProperties(PhysicsBodyHandle h,const PhysicsBodyProperties& value)
    {
        auto proposed=PrepareBodyProperties(h,value);if(!proposed)return std::unexpected(proposed.error());
        auto* body=*Resolve(h,"UpdateBodyProperties",true);
        const auto original=*body;
        const auto originalProperties=ReadBodyProperties(h);
        if(!originalProperties)return std::unexpected(originalProperties.error());
        const auto originalOwned=m_OwnedResources[h.index].properties;
        PreparedBackendMass mass,rollbackMass;
        if(m_Backend) {
            auto prepared=m_Backend->PrepareMass(h,*proposed);if(!prepared)return std::unexpected(prepared.error());
            auto rollback=m_Backend->CaptureMass(h);if(!rollback)return std::unexpected(rollback.error());
            mass=*prepared;rollbackMass=*rollback;
        }
        try {if(m_BodyPropertyMutationCallback)m_BodyPropertyMutationCallback({&body,1});}
        catch(const std::bad_alloc&){return Failure(Code::Allocation,h,"UpdateBodyProperties");}
        PublishBodyProperties(h,body,*proposed,value,mass);
        const auto actual=ReadBodyProperties(h);
        bool valid=actual && PropertiesMatch(*actual,value) && PublishedMassMatches(m_Backend.get(),h,mass);
#ifdef GENGINE_CHAIN_WAKE_CHECKS
        if(m_MassInertiaFailPropertyReadbackOnce){m_MassInertiaFailPropertyReadbackOnce=false;valid=false;}
#endif
        if(!valid) {
            PublishBodyProperties(h,body,original,*originalProperties,rollbackMass);
            m_OwnedResources[h.index].properties=originalOwned;
            const auto restored=ReadBodyProperties(h);
            if(!restored || *restored!=*originalProperties || !PublishedMassMatches(m_Backend.get(),h,rollbackMass))
                return Failure(Code::NonFiniteResult,h,"Property rollback verification failed; stop simulation");
            return Failure(Code::NonFiniteResult,h,"Property readback mismatch; property state restored, bodies awakened");
        }
        return {};
    }
    PhysicsBodyResult<> PhysicsWorld::UpdateBodyPropertiesBatch(std::span<const PhysicsBodyPropertyEdit> edits)
    {
        constexpr auto op="UpdateBodyPropertiesBatch";
        if(auto access=CheckAccess(op,true);!access)return access;
        if(edits.empty() || edits.size()>4096)return Failure(Code::Capacity,{},op);
        std::vector<RigidBody3D> prepared,originals;std::vector<RigidBody3D*> bodies;
        struct MassPublication {PreparedBackendMass forward,rollback;float retainedMass;};
        static_assert(sizeof(MassPublication)==68);
        std::vector<MassPublication> masses;
        try {
            prepared.reserve(edits.size());originals.reserve(edits.size());bodies.reserve(edits.size());
            masses.reserve(edits.size());
            for(std::size_t i=0;i<edits.size();++i) {
                const auto& e=edits[i];
                for(std::size_t j=0;j<i;++j)if(edits[j].body==e.body)return Failure(Code::InvalidHandle,e.body,"Duplicate body in property batch");
                const auto actual=ReadBodyProperties(e.body);if(!actual)return std::unexpected(actual.error());
                if(*actual!=e.before)return Failure(Code::StaleHandle,e.body,"Body properties changed; Revert/refresh before Apply");
                if(e.after.type!=e.before.type || e.after.mass!=e.before.mass || e.after.sleep!=e.before.sleep ||
                   e.after.material.spinResistanceLength!=e.before.material.spinResistanceLength ||
                   e.after.material.rollingResistanceLength!=e.before.material.rollingResistanceLength)
                    return Failure(Code::UnsupportedFeature,e.body,"Bulk properties preserve motion, mass, Native sleep thresholds and resistance lengths");
                if(e.after.type==BodyType::Static && (e.after.linearDamping!=e.before.linearDamping || e.after.angularDamping!=e.before.angularDamping ||
                   e.after.gravityFactor!=e.before.gravityFactor || e.after.continuousCollision!=e.before.continuousCollision || e.after.allowSleeping!=e.before.allowSleeping))
                    return Failure(Code::UnsupportedFeature,e.body,"Static bodies have no effective damping, gravity, CCD or sleeping edit");
                if(e.after.type!=BodyType::Dynamic && (e.after.linearDamping!=e.before.linearDamping || e.after.angularDamping!=e.before.angularDamping || e.after.gravityFactor!=e.before.gravityFactor))
                    return Failure(Code::UnsupportedFeature,e.body,"Damping and gravity edits require Dynamic bodies");
                auto p=PrepareBodyProperties(e.body,e.after);if(!p)return std::unexpected(p.error());
                MassPublication mass{{},{},m_OwnedResources[e.body.index].properties.mass};
                if(m_Backend) {
                    auto forward=m_Backend->PrepareMass(e.body,*p);if(!forward)return std::unexpected(forward.error());
                    auto rollback=m_Backend->CaptureMass(e.body);if(!rollback)return std::unexpected(rollback.error());
                    mass.forward=*forward;mass.rollback=*rollback;
                }
                masses.push_back(mass);
                prepared.push_back(*p);bodies.push_back(*Resolve(e.body,op,true));originals.push_back(*bodies.back());
            }
            // One graph allocation/validation stage for the complete batch. It may
            // fail before side effects. Discarding prepared values is full rollback.
            if(m_BodyPropertyMutationCallback)m_BodyPropertyMutationCallback(bodies);
        } catch(const std::bad_alloc&){return Failure(Code::Allocation,{},op);}
        // No allocation, fallible callback, stepping or observer is allowed between
        // these publications. Native and Jolt keep identity and joint ownership.
        for(std::size_t i=0;i<edits.size();++i)PublishBodyProperties(edits[i].body,bodies[i],prepared[i],edits[i].after,masses[i].forward);
        for(std::size_t member=0;member<edits.size();++member) {
            const auto& edit=edits[member];
            const auto actual=ReadBodyProperties(edit.body);
            bool valid=actual && PropertiesMatch(*actual,edit.after) && PublishedMassMatches(m_Backend.get(),edit.body,masses[member].forward);
#ifdef GENGINE_CHAIN_WAKE_CHECKS
            if(m_MassInertiaFailPropertyReadbackOnce){m_MassInertiaFailPropertyReadbackOnce=false;valid=false;}
#endif
            if(!valid) {
                // A backend invariant failure must not leave a partial property
                // batch. Restore EVERY member; caches remain invalidated and bodies
                // conservatively awake. Caller pauses on this integrity error.
                for(std::size_t i=0;i<edits.size();++i) {
                    PublishBodyProperties(edits[i].body,bodies[i],originals[i],edits[i].before,masses[i].rollback);
                    m_OwnedResources[edits[i].body.index].properties.mass=masses[i].retainedMass;
                }
                for(std::size_t i=0;i<edits.size();++i) {
                    const auto& original=edits[i];
                    const auto restored=ReadBodyProperties(original.body);
                    if(!restored || *restored!=original.before || !PublishedMassMatches(m_Backend.get(),original.body,masses[i].rollback))return Failure(Code::NonFiniteResult,original.body,"Property rollback verification failed; stop simulation");
                }
                return Failure(Code::NonFiniteResult,edit.body,"Property readback mismatch; complete property batch restored, bodies awakened");
            }
        }
        return {};
    }
#include "../../../src/Physics/ImpulseEditor.inl"

    PhysicsBodyResult<> PhysicsWorld::SeedImpulseActivation(ImpulseActivationPlan& plan,PhysicsBodyHandle h) const
    {
        // Caller already validated the command and owner/safe-step boundary.
        if(h.index>=MaxImpulseBodies)return Failure(Code::Capacity,h,"Connected impulse seed exceeds backend capacity");
        if(auto valid=m_Backend->ValidateImpulseActivation(h,BodyType::Dynamic);!valid)return valid;
        plan.reached.set(h.index);
        return {};
    }
    PhysicsBodyResult<> PhysicsWorld::PrepareImpulseActivation(ImpulseActivationPlan& plan) const
    {
        if(!m_JointCount || plan.reached.none())return {};
        std::bitset<MaxDistanceJoints> visited;
        bool changed=true;
        while(changed) {
            changed=false;
            for(std::size_t i=0;i<MaxDistanceJoints;++i) {
                if(!m_Joints[i].live || visited.test(i))continue;
                const auto& d=m_Joints[i].row.desc;
                const auto reached=[&](PhysicsBodyHandle h) {return h.index<MaxImpulseBodies && plan.reached.test(h.index);};
                if(!reached(d.bodyA) && !reached(d.bodyB))continue;
                // Validate BOTH endpoints, including fixed/driven terminal anchors.
                // An invalid live edge rejects the entire command, never disappears.
                for(const auto h:{d.bodyA,d.bodyB}) {
                    auto body=Resolve(h,"Connected impulse endpoint");
                    if(!body)return std::unexpected(body.error());
                    if(auto valid=m_Backend->ValidateImpulseActivation(h,(*body)->Type);!valid)return valid;
                    if((*body)->Type!=BodyType::Dynamic)continue;
                    if(!plan.included.test(h.index)) {
                        if(plan.count==plan.bodies.size())return Failure(Code::Capacity,h,"Connected impulse incident-endpoint bound");
                        plan.bodies[plan.count++]=h;plan.included.set(h.index);
                    }
                    plan.reached.set(h.index);
                }
                visited.set(i);changed=true;
            }
        }
        std::sort(plan.bodies.begin(),plan.bodies.begin()+plan.count,
            [](PhysicsBodyHandle a,PhysicsBodyHandle b){return a.index<b.index;});
        return {};
    }
    void PhysicsWorld::CommitImpulseActivation(const ImpulseActivationPlan& plan) noexcept
    {
        m_Backend->CommitImpulseActivation({plan.bodies.data(),plan.count});
    }
    void PhysicsWorld::CommitLegacyImpulseActivation(const ImpulseActivationPlan& plan,PhysicsBodyHandle h) noexcept
    {
        CommitImpulseActivation(plan);
        // A lone target need not occupy the joint-incident scratch array.
        if(!plan.included.test(h.index))m_Backend->CommitImpulseActivation({&h,1});
    }
    PhysicsBodyResult<> PhysicsWorld::ApplyLinearImpulse(PhysicsBodyHandle h, const Vec3f& impulse)
    {
        auto b = Resolve(h, "ApplyLinearImpulse", true); if (!b) return std::unexpected(b.error());
        if (!Math::IsFinite(impulse)) return Failure(Code::InvalidForce, h, "ApplyLinearImpulse");
        if ((*b)->Type != BodyType::Dynamic) return Failure(Code::WrongBodyType, h, "ApplyLinearImpulse");
        if (!LinearResultFinite(**b, impulse)) return Failure(Code::NonFiniteResult, h, "ApplyLinearImpulse");
        if(m_Backend && impulse!=Vec3f(0)) {
            ImpulseActivationPlan plan;
            if(auto valid=SeedImpulseActivation(plan,h);!valid)return valid;
            if(auto valid=PrepareImpulseActivation(plan);!valid)return valid;
            (*b)->ApplyImpulseLinear(impulse);CommitLegacyImpulseActivation(plan,h);return {};
        }
        (*b)->ApplyImpulseLinear(impulse); return {};
    }
    PhysicsBodyResult<> PhysicsWorld::ApplyAngularImpulse(PhysicsBodyHandle h, const Vec3f& impulse)
    {
        auto b = Resolve(h, "ApplyAngularImpulse", true); if (!b) return std::unexpected(b.error());
        if (!Math::IsFinite(impulse)) return Failure(Code::InvalidForce, h, "ApplyAngularImpulse");
        if ((*b)->Type != BodyType::Dynamic) return Failure(Code::WrongBodyType, h, "ApplyAngularImpulse");
        if (!AngularResultFinite(**b, impulse)) return Failure(Code::NonFiniteResult, h, "ApplyAngularImpulse");
        if(m_Backend && impulse!=Vec3f(0)) {
            ImpulseActivationPlan plan;
            if(auto valid=SeedImpulseActivation(plan,h);!valid)return valid;
            if(auto valid=PrepareImpulseActivation(plan);!valid)return valid;
            (*b)->ApplyImpulseAngular(impulse);CommitLegacyImpulseActivation(plan,h);return {};
        }
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
        if(m_Backend && (impulse!=Vec3f(0) || angular!=Vec3f(0))) {
            ImpulseActivationPlan plan;
            if(auto valid=SeedImpulseActivation(plan,h);!valid)return valid;
            if(auto valid=PrepareImpulseActivation(plan);!valid)return valid;
            (*b)->ApplyImpulse(point,impulse);CommitLegacyImpulseActivation(plan,h);return {};
        }
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
        if(m_Backend && settings!=RigidBody3D::SleepSettings{})
            return Failure(Code::UnsupportedFeature,h,"Jolt uses its native sleep test; custom Native thresholds unsupported");
        if (!(*b)->SetSleepSettings(settings)) return Failure(Code::NotSleepEligible, h, "SetBodySleepSettings");
        return {};
    }
    PhysicsBodyResult<> PhysicsWorld::TrySleepBody(PhysicsBodyHandle h)
    {
        auto b = Resolve(h, "TrySleepBody", true); if (!b) return std::unexpected(b.error());
        if(m_Backend)return Failure(Code::UnsupportedFeature,h,"Jolt sleep is determined by its solver");
        if (!(*b)->CanSleep()) return Failure(Code::NotSleepEligible, h, "TrySleepBody");
        (*b)->TrySleep(); return {};
    }
    PhysicsBodyResult<> PhysicsWorld::WakeBody(PhysicsBodyHandle h)
    {
        auto b = Resolve(h, "WakeBody", true); if (!b) return std::unexpected(b.error());
        (*b)->WakeUp(); return {};
    }
    RigidBody3D* PhysicsWorld::ResolveJointBody(PhysicsBodyHandle h) const
    {
        if (!h || h.registry != m_Domain || h.index >= m_BodyIdentitySlots.size()) return nullptr;
        const auto& slot = m_BodyIdentitySlots[h.index];
        return slot.generation == h.generation ? slot.body : nullptr;
    }
    JointResult<std::size_t> PhysicsWorld::ResolveJoint(PhysicsJointHandle h, std::string_view op, bool mutation) const
    {
        const auto fail = [&](JointErrorCode code) { return std::unexpected(JointError{code,h,op}); };
        if (!CheckAccess(op, mutation)) return fail(JointErrorCode::Access);
        if (!h) return fail(JointErrorCode::InvalidHandle);
        if (h.registry != m_Domain) return fail(JointErrorCode::WrongWorld);
        if (!m_Joints || h.index >= MaxDistanceJoints || !m_Joints[h.index].live ||
            m_Joints[h.index].generation != h.generation) return fail(JointErrorCode::StaleHandle);
        return h.index;
    }
    JointResult<PhysicsJointHandle> PhysicsWorld::CreateDistanceJoint(const DistanceJointDesc& d)
    {
        constexpr auto op = "CreateDistanceJoint";
        const auto fail = [](JointErrorCode c) { return std::unexpected(JointError{c,{},op}); };
        if (!CheckAccess(op,true)) return fail(JointErrorCode::Access);
        auto* a = ResolveJointBody(d.bodyA); auto* b = ResolveJointBody(d.bodyB);
        if (!a || !b || a == b) return fail(JointErrorCode::InvalidEndpoint);
        if (!Math::IsFinite(d.localAnchorA) || !Math::IsFinite(d.localAnchorB) ||
            !Math::IsFinite(d.length) || d.length <= Math::NumericalEpsilon)
            return fail(JointErrorCode::InvalidGeometry);
        if (a->GetInverseMass() + b->GetInverseMass() <= 0) return fail(JointErrorCode::NoResponse);
        // Admission does not alter live velocity/wake state, even on rejection.
        auto copyA = *a, copyB = *b;
        DistanceJointRow proposed; proposed.desc = d;
        if (!proposed.Prepare(copyA,copyB,1.f/60.f)) return fail(JointErrorCode::InvalidGeometry);
        proposed.Reset();
        if (m_JointCount == MaxDistanceJoints) return fail(JointErrorCode::Capacity);
        if (!m_Joints) {
            std::unique_ptr<JointSlot[]> storage(new (std::nothrow) JointSlot[MaxDistanceJoints]);
            if (!storage) return fail(JointErrorCode::Allocation);
            m_Joints = std::move(storage);
        }
        const auto generation = Asset::AssetDetail::TakeRegistryIdentity(s_NextBodyGeneration);
        if (!generation) return fail(JointErrorCode::Capacity);
        std::size_t i = 0; while (m_Joints[i].live) ++i;
        const PhysicsJointHandle handle{static_cast<std::uint32_t>(i),*generation,m_Domain};
        if(m_Backend){auto created=m_Backend->CreateJoint(handle,d);if(!created)return std::unexpected(created.error());}
        auto& slot = m_Joints[i]; slot.row = proposed; slot.point.Reset(); slot.kind = JointKind::Distance; slot.generation = *generation; slot.live = true;
        ++m_JointCount;
        if (m_JointMutationCallback) { m_JointMutationCallback(a); m_JointMutationCallback(b); }
        else { a->WakeUp(); b->WakeUp(); }
        return PhysicsJointHandle{static_cast<std::uint32_t>(i),*generation,m_Domain};
    }
    JointResult<DistanceJointState> PhysicsWorld::ReadDistanceJoint(PhysicsJointHandle h) const
    {
        auto index = ResolveJoint(h,"ReadDistanceJoint"); if (!index) return std::unexpected(index.error());
        if (m_Joints[*index].kind != JointKind::Distance)
            return std::unexpected(JointError{JointErrorCode::WrongKind,h,"ReadDistanceJoint"});
        const auto& row = m_Joints[*index].row;
        auto* a = ResolveJointBody(row.desc.bodyA); auto* b = ResolveJointBody(row.desc.bodyB);
        if (!a || !b) return std::unexpected(JointError{JointErrorCode::InvalidEndpoint,h,"ReadDistanceJoint"});
        const auto pa = a->BodySpaceToWorldSpace(row.desc.localAnchorA), pb = b->BodySpaceToWorldSpace(row.desc.localAnchorB);
        const float error = glm::length(pb-pa)-row.desc.length;
        const auto impulse=m_Backend?m_Backend->JointImpulse(h):row.impulse;
        if (!Math::IsFinite(pa) || !Math::IsFinite(pb) || !Math::IsFinite(error) || !Math::IsFinite(impulse))
            return std::unexpected(JointError{JointErrorCode::NonFinite,h,"ReadDistanceJoint"});
        return DistanceJointState{h,row.desc,pa,pb,error,impulse,m_Backend?m_Backend->JointActive(h):row.active};
    }
    JointResult<PhysicsJointHandle> PhysicsWorld::CreateBallSocketJoint(const BallSocketJointDesc& d)
    {
        constexpr auto op = "CreateBallSocketJoint";
        const auto fail = [](JointErrorCode c) { return std::unexpected(JointError{c,{},op}); };
        if (!CheckAccess(op,true)) return fail(JointErrorCode::Access);
        auto* a=ResolveJointBody(d.bodyA); auto* b=ResolveJointBody(d.bodyB);
        if (!a || !b || a==b) return fail(JointErrorCode::InvalidEndpoint);
        if (!Math::IsFinite(d.localAnchorA) || !Math::IsFinite(d.localAnchorB))
            return fail(JointErrorCode::InvalidGeometry);
        if (a->GetInverseMass()+b->GetInverseMass()<=0) return fail(JointErrorCode::NoResponse);
        auto copyA=*a, copyB=*b;
        BallSocketJointRow proposed; proposed.desc=d;
        if (!proposed.Prepare(copyA,copyB,1.f/60.f)) return fail(JointErrorCode::InvalidGeometry);
        proposed.Reset();
        if (m_JointCount==MaxDistanceJoints) return fail(JointErrorCode::Capacity);
        if (!m_Joints) {
            std::unique_ptr<JointSlot[]> storage(new (std::nothrow) JointSlot[MaxDistanceJoints]);
            if (!storage) return fail(JointErrorCode::Allocation);
            m_Joints=std::move(storage);
        }
        const auto generation=Asset::AssetDetail::TakeRegistryIdentity(s_NextBodyGeneration);
        if (!generation) return fail(JointErrorCode::Capacity);
        std::size_t i=0; while(m_Joints[i].live) ++i;
        const PhysicsJointHandle handle{static_cast<std::uint32_t>(i),*generation,m_Domain};
        if (m_Backend) { auto result=m_Backend->CreateJoint(handle,d); if(!result)return std::unexpected(result.error()); }
        auto& slot=m_Joints[i]; slot.Reset(); slot.point=proposed;
        slot.row.desc={d.bodyA,d.bodyB,d.localAnchorA,d.localAnchorB,0,d.collideConnected};
        slot.kind=JointKind::BallSocket; slot.generation=*generation; slot.live=true; ++m_JointCount;
        if (m_JointMutationCallback) { m_JointMutationCallback(a); m_JointMutationCallback(b); }
        else { a->WakeUp(); b->WakeUp(); }
        return handle;
    }
    JointResult<PhysicsJointHandle> PhysicsWorld::CreateAngularJoint(const AngularJointDesc& d)
    {
        constexpr auto op="CreateAngularJoint";
        const auto fail=[](JointErrorCode c){return std::unexpected(JointError{c,{},op});};
        if(!CheckAccess(op,true))return fail(JointErrorCode::Access);
        auto* a=ResolveJointBody(d.bodyA);auto* b=ResolveJointBody(d.bodyB);
        if(!a||!b||a==b)return fail(JointErrorCode::InvalidEndpoint);
        if(!AngularJoint::Valid(d))return fail(JointErrorCode::InvalidGeometry);
        if(d.kind!=JointKind::Hinge) {
            const auto q=AngularJoint::Observe(d,a->m_Orientation,b->m_Orientation).relativeRotation;
            if(q.w*q.w+q.y*q.y+q.z*q.z<=1.e-8f)return fail(JointErrorCode::InvalidGeometry);
        }
        if(a->GetInverseMass()+b->GetInverseMass()<=0)return fail(JointErrorCode::NoResponse);
        auto copyA=*a,copyB=*b;
        AngularJointRow proposed;proposed.desc=d;
        if(!proposed.Prepare(copyA,copyB,1.f/60.f))return fail(JointErrorCode::InvalidGeometry);
        proposed.Reset();
        if(m_JointCount==MaxDistanceJoints)return fail(JointErrorCode::Capacity);
        if(!m_Joints) {
            std::unique_ptr<JointSlot[]> storage(new(std::nothrow) JointSlot[MaxDistanceJoints]);
            if(!storage)return fail(JointErrorCode::Allocation);
            m_Joints=std::move(storage);
        }
        const auto generation=Asset::AssetDetail::TakeRegistryIdentity(s_NextBodyGeneration);
        if(!generation)return fail(JointErrorCode::Capacity);
        std::size_t i=0;while(m_Joints[i].live)++i;
        const PhysicsJointHandle handle{static_cast<std::uint32_t>(i),*generation,m_Domain};
        if(m_Backend){auto result=m_Backend->CreateJoint(handle,d);if(!result)return std::unexpected(result.error());}
        auto& slot=m_Joints[i];slot.Reset();slot.angular=proposed;
        slot.row.desc={d.bodyA,d.bodyB,d.localAnchorA,d.localAnchorB,0,d.collideConnected};
        slot.kind=d.kind;slot.generation=*generation;slot.live=true;++m_JointCount;
        if(m_JointMutationCallback){m_JointMutationCallback(a);m_JointMutationCallback(b);}
        else {a->WakeUp();b->WakeUp();}
        return handle;
    }
    JointResult<PhysicsJointHandle> PhysicsWorld::CreateJoint(const JointDescriptor& d)
    {
        switch(d.kind) {
        case JointKind::Distance: return CreateDistanceJoint({d.bodyA,d.bodyB,d.localAnchorA,d.localAnchorB,d.targetDistance,d.collideConnected});
        case JointKind::BallSocket: return CreateBallSocketJoint({d.bodyA,d.bodyB,d.localAnchorA,d.localAnchorB,d.collideConnected});
        case JointKind::Hinge:
        case JointKind::ConstantVelocity:
        case JointKind::LimitedConstantVelocity:
            return CreateAngularJoint({d.kind,d.bodyA,d.bodyB,d.localAnchorA,d.localAnchorB,d.collideConnected,d.angular});
        }
        return std::unexpected(JointError{JointErrorCode::WrongKind,{},"CreateJoint"});
    }
    JointResult<JointState> PhysicsWorld::ReadJoint(PhysicsJointHandle h) const
    {
        auto index=ResolveJoint(h,"ReadJoint"); if(!index)return std::unexpected(index.error());
        const auto& slot=m_Joints[*index]; const auto& d=slot.row.desc;
        auto* a=ResolveJointBody(d.bodyA); auto* b=ResolveJointBody(d.bodyB);
        if(!a || !b)return std::unexpected(JointError{JointErrorCode::InvalidEndpoint,h,"ReadJoint"});
        const auto pa=a->BodySpaceToWorldSpace(d.localAnchorA),pb=b->BodySpaceToWorldSpace(d.localAnchorB);
        const auto delta=pb-pa; const float length=glm::length(delta);
        Vec3f impulse{}; bool active{};
        if(slot.kind==JointKind::BallSocket) {
            impulse=m_Backend?m_Backend->JointVectorImpulse(h):slot.point.impulse;
            active=m_Backend?m_Backend->JointActive(h):slot.point.active;
        } else if(AngularJoint::IsKind(slot.kind)) {
            impulse=m_Backend?m_Backend->JointVectorImpulse(h):slot.angular.impulse;
            active=m_Backend?m_Backend->JointActive(h):slot.angular.active;
        } else {
            const float lambda=m_Backend?m_Backend->JointImpulse(h):slot.row.impulse;
            impulse=length>0?delta*(lambda/length):Vec3f(0);
            active=m_Backend?m_Backend->JointActive(h):slot.row.active;
        }
        const float error=slot.kind==JointKind::Distance?length-d.length:length;
        if(!Math::IsFinite(pa)||!Math::IsFinite(pb)||!Math::IsFinite(impulse)||!Math::IsFinite(error))
            return std::unexpected(JointError{JointErrorCode::NonFinite,h,"ReadJoint"});
        JointState state{h,{slot.kind,d.bodyA,d.bodyB,d.localAnchorA,d.localAnchorB,d.length,d.collideConnected},pa,pb,delta,impulse,error,active};
        if(AngularJoint::IsKind(slot.kind)) {
            auto effective=slot.angular.desc;
            if(m_Backend) {
                auto readback=m_Backend->ReadAngularJoint(h);
                if(!readback)return std::unexpected(readback.error());
                effective=*readback;
            }
            if(!AngularJoint::Valid(effective))return std::unexpected(JointError{JointErrorCode::NonFinite,h,"ReadJoint"});
            state.descriptor={effective.kind,effective.bodyA,effective.bodyB,effective.localAnchorA,
                effective.localAnchorB,0,effective.collideConnected,effective.angular};
            state.worldAnchorA=a->BodySpaceToWorldSpace(effective.localAnchorA);
            state.worldAnchorB=b->BodySpaceToWorldSpace(effective.localAnchorB);
            state.residual=state.worldAnchorB-state.worldAnchorA;state.error=glm::length(state.residual);
            state.angular=AngularJoint::Observe(effective,a->m_Orientation,b->m_Orientation);
            state.angular.angularImpulse=m_Backend?m_Backend->AngularImpulse(h):slot.angular.angularImpulse;
            state.angular.impulseAvailable=m_Backend?m_Backend->AngularImpulseAvailable(h):slot.angular.impulseAvailable;
            if(!Math::IsFinite(state.worldAnchorA)||!Math::IsFinite(state.worldAnchorB)||!std::isfinite(state.error)||
                !Math::IsFinite(state.angular.angularImpulse)||!std::isfinite(state.angular.axisError)||!std::isfinite(state.angular.cvError))
                return std::unexpected(JointError{JointErrorCode::NonFinite,h,"ReadJoint"});
        }
        return state;
    }
    JointResult<BallSocketJointState> PhysicsWorld::ReadBallSocketJoint(PhysicsJointHandle h) const
    {
        auto state=ReadJoint(h); if(!state)return std::unexpected(state.error());
        const auto& d=state->descriptor;
        if(d.kind!=JointKind::BallSocket)return std::unexpected(JointError{JointErrorCode::WrongKind,h,"ReadBallSocketJoint"});
        return BallSocketJointState{h,{d.bodyA,d.bodyB,d.localAnchorA,d.localAnchorB,d.collideConnected},state->worldAnchorA,state->worldAnchorB,state->residual,state->impulse,state->active};
    }
    JointResult<PhysicsJointHandle> PhysicsWorld::JointAt(std::size_t ordinal) const
    {
        if (!CheckAccess("JointAt",false)) return std::unexpected(JointError{JointErrorCode::Access,{},"JointAt"});
        if (m_Joints) for (std::size_t i=0;i<MaxDistanceJoints;++i) if (m_Joints[i].live) {
            if (ordinal-- == 0) return PhysicsJointHandle{static_cast<std::uint32_t>(i),m_Joints[i].generation,m_Domain};
        }
        return std::unexpected(JointError{JointErrorCode::InvalidHandle,{},"JointAt"});
    }
    JointResult<> PhysicsWorld::DestroyJoint(PhysicsJointHandle h)
    {
        auto index=ResolveJoint(h,"DestroyJoint",true); if(!index) return std::unexpected(index.error());
        auto& slot=m_Joints[*index];
        auto* a=ResolveJointBody(slot.row.desc.bodyA); auto* b=ResolveJointBody(slot.row.desc.bodyB);
        if(m_JointMutationCallback) { if(a)m_JointMutationCallback(a); if(b)m_JointMutationCallback(b); }
        else { if(a)a->WakeUp(); if(b)b->WakeUp(); }
        if(m_Backend)m_Backend->DestroyJoint(h);
        slot.Reset(); slot.live=false; --m_JointCount;
        return {};
    }
    void PhysicsWorld::InvalidateJoints(RigidBody3D* body)
    {
        if (!m_JointCount) return;
        const auto h=HandleOf(body->GetIdentity());
        for(std::size_t i=0;i<MaxDistanceJoints;++i) if(m_Joints[i].live) {
            auto& row=m_Joints[i].row;
            if(row.desc.bodyA==h || row.desc.bodyB==h) {
                m_Joints[i].Reset();
                if(m_Backend)m_Backend->ResetJoint({static_cast<std::uint32_t>(i),m_Joints[i].generation,m_Domain});
            }
        }
    }
    void PhysicsWorld::RetireJoints(RigidBody3D* body)
    {
        if (!m_JointCount) return;
        const auto h=HandleOf(body->GetIdentity());
        for(std::size_t i=0;i<MaxDistanceJoints;++i) if(m_Joints[i].live) {
            auto& slot=m_Joints[i];
            if(slot.row.desc.bodyA==h || slot.row.desc.bodyB==h) {
                if(m_Backend)m_Backend->DestroyJoint({static_cast<std::uint32_t>(i),slot.generation,m_Domain});
                slot.Reset(); slot.live=false; --m_JointCount;
            }
        }
    }
    bool PhysicsWorld::JointSuppressesPair(RigidBodyIdentity a, RigidBodyIdentity b) const
    {
        if(!m_JointCount) return false;
        const auto ha=HandleOf(a), hb=HandleOf(b);
        for(std::size_t i=0;i<MaxDistanceJoints;++i) if(m_Joints[i].live) {
            const auto& d=m_Joints[i].row.desc;
            if(!d.collideConnected && ((d.bodyA==ha && d.bodyB==hb)||(d.bodyA==hb && d.bodyB==ha))) return true;
        }
        return false;
    }

#include "../../../src/Physics/EditorPlacement.inl"

}
