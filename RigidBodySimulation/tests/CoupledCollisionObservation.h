#pragma once

#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION

#include "../../GEngine/include/GEngine/Physics/PhysicsProfile.h"
#include <bit>

// Diagnostic-only, synchronous value observation. Body pointers are borrowed for
// the callback; callbacks must not step, query or mutate the observed world.
// This independent interface does not change the existing profiler ABI.
namespace GEngine::PhysicsDiagnostic::Coupled
{
    enum class EventKind { Begin, Finish, Admission };
    enum class Role { InitialPrediction, WakeRequery, PendingRefresh, CurrentToi };
    enum class AdmissionKind { RestingSingle, RestingPatch, ToiQueued };

    struct Context
    {
        std::uint64_t query{};
        const RigidBody3D* a{};
        const RigidBody3D* b{};
        std::uint64_t slotA{}, generationA{}, slotB{}, generationB{};
        Role role{};
        Stage stage{};
        float dt{}, accumulatedTime{};
    };

    struct Event
    {
        EventKind kind{};
        Context context{};
        BodyState beforeA{}, beforeB{}, afterA{}, afterB{};
        int outcome{-1}; // QueryOutcome: Contact=0, SeparatedValid=1, Failed=2.
        QueryExit reason{QueryExit::None};
        GjkDiagnostics diagnostics{};
        contact_t contact{}; // Query result, or the generic seed at admission.
        std::array<contact_t,4> patch{}; // Contacts actually passed to the next stage.
        unsigned patchCount{};
        AdmissionKind admission{};
    };

    struct Sink
    {
        void* context{};
        void (*event)(void*,const Event&) noexcept{};
        bool (*select)(void*,const RigidBody3D*,const RigidBody3D*) noexcept{};
        void (*violation)(void*) noexcept{};
        std::uint64_t nextQuery{}, observationNs{};
    };

    inline thread_local Sink* sink{};
    inline thread_local const Context* currentContext{};
    inline const Context* CurrentContext() noexcept { return currentContext; }

    inline bool SameBits(float a,float b) noexcept
    { return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b); }
    inline bool SameBits(const Vec3f& a,const Vec3f& b) noexcept
    { return SameBits(a.x,b.x) && SameBits(a.y,b.y) && SameBits(a.z,b.z); }
    inline bool SameBits(const BodyState& a,const BodyState& b) noexcept
    {
        return SameBits(a.position,b.position) && SameBits(a.linear,b.linear) &&
            SameBits(a.angular,b.angular) && SameBits(a.orientation.x,b.orientation.x) &&
            SameBits(a.orientation.y,b.orientation.y) && SameBits(a.orientation.z,b.orientation.z) &&
            SameBits(a.orientation.w,b.orientation.w) &&
            std::bit_cast<std::uint64_t>(a.inactive)==std::bit_cast<std::uint64_t>(b.inactive) &&
            a.sleeping==b.sleeping;
    }

    inline void Notify(Sink& destination,const Event& value) noexcept
    {
        const auto beforeA=Capture(*value.context.a), beforeB=Capture(*value.context.b);
        destination.event(destination.context,value);
        if ((!SameBits(beforeA,Capture(*value.context.a)) ||
             !SameBits(beforeB,Capture(*value.context.b))) && destination.violation)
            destination.violation(destination.context);
    }

    class QueryScope
    {
        Sink* m_Sink{};
        const Context* m_Previous{};
        bool m_Finished{};
        Event m_Value{};
    public:
        QueryScope(Role role,const RigidBody3D* a,const RigidBody3D* b,
                   float dt,float accumulatedTime) noexcept
        {
            if (!sink || !sink->event || !a || !b) return;
            auto* destination=sink;
            const auto start=Nanoseconds();
            const auto query=++destination->nextQuery;
            if (destination->select && !destination->select(destination->context,a,b)) {
                destination->observationNs+=Nanoseconds()-start;
                return;
            }
            m_Sink=destination;
            const auto identityA=a->GetIdentity(), identityB=b->GetIdentity();
            m_Value.context={query,a,b,identityA.GetSlot(),identityA.GetGeneration(),
                identityB.GetSlot(),identityB.GetGeneration(),role,
                role==Role::InitialPrediction || role==Role::WakeRequery ? Stage::Narrowphase : Stage::Solve,
                dt,accumulatedTime};
            m_Value.beforeA=m_Value.afterA=Capture(*a);
            m_Value.beforeB=m_Value.afterB=Capture(*b);
            m_Previous=currentContext;
            currentContext=&m_Value.context;
            Notify(*m_Sink,m_Value);
            m_Sink->observationNs+=Nanoseconds()-start;
        }
        QueryScope(const QueryScope&)=delete;
        QueryScope& operator=(const QueryScope&)=delete;
        ~QueryScope() noexcept
        { if (m_Sink && !m_Finished) currentContext=m_Previous; }

        const Event& Finish(int outcome,QueryExit reason,const GjkDiagnostics& diagnostics,
                            const contact_t& contact) noexcept
        {
            if (!m_Sink || m_Finished) return m_Value;
            const auto start=Nanoseconds();
            m_Value.kind=EventKind::Finish;
            m_Value.outcome=outcome;
            m_Value.reason=reason;
            m_Value.diagnostics=diagnostics;
            m_Value.contact=contact;
            m_Value.afterA=Capture(*m_Value.context.a);
            m_Value.afterB=Capture(*m_Value.context.b);
            Notify(*m_Sink,m_Value);
            currentContext=m_Previous;
            m_Finished=true;
            m_Sink->observationNs+=Nanoseconds()-start;
            return m_Value;
        }
    };

    inline void Admit(const Event& query,const contact_t& seed,
                      const contact_t* contacts,unsigned count,AdmissionKind kind) noexcept
    {
        if (!sink || !sink->event || !query.context.query) return;
        const auto start=Nanoseconds();
        Event value=query;
        value.kind=EventKind::Admission;
        value.contact=seed;
        value.admission=kind;
        value.patchCount=count;
        if (count>value.patch.size()) {
            if (sink->violation) sink->violation(sink->context);
            sink->observationNs+=Nanoseconds()-start;
            return;
        }
        for (unsigned i=0;i<count;++i) value.patch[i]=contacts[i];
        value.afterA=Capture(*value.context.a);
        value.afterB=Capture(*value.context.b);
        Notify(*sink,value);
        sink->observationNs+=Nanoseconds()-start;
    }
}

#endif
