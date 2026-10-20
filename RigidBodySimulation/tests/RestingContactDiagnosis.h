#pragma once

// Phase 19's opt-in RBS fixture and recorder. The ordinary app creates no session.
#include "RbsLaunchConfig.h"
#include "RbsState.h"
#include "Physics/PhysicsProfile.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/ShapeSphere.h"
#include "Physics/ShapeBox.h"
#include "Physics/ShapeConvex.h"
#include "Physics/Broadphase.h"
#include <cstdlib>
#include "Scene/_Scene.h"
#include "Core/Log.h"
#include <bit>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <new>
#include <utility>
#include <vector>
#include "Renderer/RenderFrame.h"

namespace Rbs::RestDiagnosis
{
    using namespace ::GEngine;
    namespace PD=::GEngine::PhysicsDiagnostic;
    inline constexpr std::array<unsigned,9> Durations{1800,600,600,240,240,240,240,600,2400};
    inline constexpr std::array<const char*,9> Names{"lattice","ground","friction","impact-0.5",
        "impact-1.0","impact-1.5","off-center","spin","stack"};
    inline constexpr std::array<unsigned,11> ConditionalDurations{1800,300,300,120,120,120,120,300,1200,300,300};
    inline constexpr std::array<const char*,11> ConditionalNames{"lattice","ground","friction","impact-0.5",
        "impact-1.0","impact-1.5","off-center","spin","stack","free-rolling","convex-sliding"};
    struct BodyInput
    {
        int shape{}, type{};
        float inverseMass{}, elasticity{.5f}, friction{.5f}, radius{1};
        std::uint32_t layer{1}, mask{~std::uint32_t{0}};
        Vec3f position{}, linear{}, angular{};
        Quat orientation{1,0,0,0};
        std::vector<Vec3f> points;
    };
    inline void Vector(std::istream& in,Vec3f& v) { in>>v.x>>v.y>>v.z; }
    inline bool LoadLattice(const std::string& path,std::vector<BodyInput>& bodies)
    {
        std::ifstream in(path);
        unsigned count{}; in>>count;
        if (!in || count!=185) return false;
        bodies.resize(count);
        for (unsigned i=0;i<count;++i) {
            auto& b=bodies[i]; unsigned points{};
            in>>b.shape>>b.type>>b.inverseMass>>b.elasticity>>b.friction>>b.layer>>b.mask;
            Vector(in,b.position);
            in>>b.orientation.w>>b.orientation.x>>b.orientation.y>>b.orientation.z;
            Vector(in,b.linear); Vector(in,b.angular); in>>b.radius>>points;
            if (!in || points>36 || b.shape!=(i<5?1:0) || b.type!=(i<5?0:1) ||
                b.inverseMass!=(i<5?0.f:1.f) || b.elasticity!=.5f || b.friction!=.5f ||
                b.layer!=1 || b.mask!=~std::uint32_t{0} || b.linear!=Vec3f(0) || b.angular!=Vec3f(0) ||
                (i<5 ? points!=36 : (points!=0 || b.radius!=1.f))) return false;
            b.points.resize(points);
            for (auto& p:b.points) { Vector(in,p); if (!Math::IsFinite(p)) return false; }
            if (!in || !Math::IsFinite(b.position) || !Math::IsFinite(b.orientation)) return false;
        }
        in>>std::ws;
        return in.eof();
    }
    inline BodyInput Box(Vec3f position,Vec3f extent,bool dynamic)
    {
        BodyInput b; b.shape=1; b.type=dynamic?1:0; b.inverseMass=dynamic?1.f:0.f;
        b.position=position;
        for (int x:{-1,1}) for (int y:{-1,1}) for (int z:{-1,1})
            b.points.emplace_back(extent*Vec3f(float(x),float(y),float(z))*.5f);
        return b;
    }
    inline std::vector<BodyInput> Focused(unsigned episode)
    {
        std::vector<BodyInput> result{Box({}, {100,1,100},false)};
        BodyInput b; b.type=1; b.inverseMass=1; b.position={0,1.5f,0};
        if (episode==2) { b=Box({0,1.5f,0},{2,2,2},true); b.linear={.5f,0,0}; }
        if (episode>=3 && episode<=5) { b.position.y=1.501f; b.linear.y=-.5f*float(episode-2); }
        if (episode==6) {
            const float angle=glm::radians(15.f);
            b=Box({0,.501f+std::cos(angle)+std::sin(angle),0},{2,2,2},true);
            b.orientation=glm::angleAxis(angle,Vec3f(0,0,1)); b.linear={0,-1.5f,0};
        }
        if (episode==7) b.angular={0,1,0};
        if (episode==8) {
            for (unsigned i=0;i<4;++i) result.push_back(Box({0,1.5f+2.01f*float(i),0},{2,2,2},true));
        } else result.push_back(b);
        return result;
    }
    template<class T> inline void Binary(std::ofstream& out,const T& v)
    {
        static_assert(std::is_arithmetic_v<T>);
        out.write(reinterpret_cast<const char*>(&v),sizeof(v));
    }
    inline void BinaryState(std::ofstream& out,const PD::BodyState& s)
    {
        for (float v:{s.position.x,s.position.y,s.position.z,s.orientation.w,s.orientation.x,
             s.orientation.y,s.orientation.z,s.linear.x,s.linear.y,s.linear.z,s.angular.x,s.angular.y,s.angular.z}) Binary(out,v);
        Binary(out,static_cast<std::uint8_t>(s.sleeping)); Binary(out,s.inactive);
    }
    inline void CsvVector(std::ostream& out,const Vec3f& v) { out<<','<<v.x<<','<<v.y<<','<<v.z; }

    struct Session;
    inline bool RunConditionalQueries(Session&);

    // Outward-rounded arithmetic for an independent disk-optimality certificate.
    // Inputs are exact binary32/binary64 observer operands. No solver routine is
    // called and no tolerance is added to the accepted energy allowance.
    struct AuditInterval
    {
        double lo{},hi{};
        AuditInterval()=default;
        AuditInterval(double x):lo(x),hi(x) {}
        AuditInterval(double a,double b):lo(a),hi(b) {}
        static double Down(double x) { return std::nextafter(x,-std::numeric_limits<double>::infinity()); }
        static double Up(double x) { return std::nextafter(x,std::numeric_limits<double>::infinity()); }
        friend AuditInterval operator+(AuditInterval a,AuditInterval b) { return {Down(a.lo+b.lo),Up(a.hi+b.hi)}; }
        friend AuditInterval operator-(AuditInterval a,AuditInterval b) { return {Down(a.lo-b.hi),Up(a.hi-b.lo)}; }
        friend AuditInterval operator*(AuditInterval a,AuditInterval b) {
            const double p[]{a.lo*b.lo,a.lo*b.hi,a.hi*b.lo,a.hi*b.hi};
            return {Down(*std::min_element(p,p+4)),Up(*std::max_element(p,p+4))};
        }
        static AuditInterval Square(AuditInterval a) {
            const double low=a.lo<=0 && a.hi>=0?0:std::min(a.lo*a.lo,a.hi*a.hi);
            return {std::max(0.,Down(low)),Up(std::max(a.lo*a.lo,a.hi*a.hi))};
        }
        static AuditInterval Norm(AuditInterval a,AuditInterval b) {
            const auto s=Square(a)+Square(b);
            return {std::max(0.,Down(std::sqrt(std::max(0.,s.lo)))),Up(std::sqrt(s.hi))};
        }
        static double AbsUpper(AuditInterval a) { return std::max(std::abs(a.lo),std::abs(a.hi)); }
        bool Finite() const { return std::isfinite(lo) && std::isfinite(hi) && lo<=hi; }
    };

    struct PostCandidateAudit
    {
        struct EventRecord { PD::Event event{}; unsigned episode{},tick{}; std::uint64_t sequence{};
            std::array<std::uint64_t,4> identity{}; };
        struct StageRecord { PD::BodyState state{}; unsigned episode{},tick{},stage{}; std::uint64_t slot{},generation{}; };
        struct Cache { const RigidBody3D* a{}; const RigidBody3D* b{}; Vec3f localA{},localB{};
            std::array<double,3> lambda{}; std::uintptr_t token{}; bool present{},projected{},warm{},reduced{}; };
        struct Tick { unsigned episode{},tick{},sliding{},legacyPositive{},infeasible{},explained{};
            double maxF{},maxGap{},maxIncrement{},maxIntermediateCone{},beforeK{},afterK{},work{},balance{}; };
        std::array<EventRecord,1024> events{};
        std::array<StageRecord,288> stages{};
        std::array<Tick,128> pending{};
        std::array<Cache,4> cache{};
        std::array<PD::BodyState,2> responsePose{};
        Tick current{};
        unsigned eventCount{},stageCount{},eventWritten{},stageWritten{},pendingCount{},ticks{},sliding{},legacyPositive{},explained{};
        std::uint64_t sequence{};
        double responseWorkStart{};
        bool responseOpen{},responseComplete{};
    };
    static_assert(sizeof(PostCandidateAudit)<=1024*1024,"accepted one MiB additional diagnostic storage");

    struct Session
    {
        RestingContactDiagnostic config;
        PD::Observer sink{};
        _Scene* scene{};
        SceneAssets assets;
        std::vector<BodyInput> lattice, inputs;
        std::vector<_Entity> entities;
        std::vector<PD::BodyState> previous;
        std::ofstream bodies, stages, contacts, timing, metrics, events, frames, initial, islandRows;
        unsigned episode{}, tick{}, completedEpisodes{};
        std::uint64_t sequence{}, eventCount{}, wakeCount{}, sleepCount{};
        std::uint64_t stageClock{}, stepClock{}, recorderNs{}, callbackNs{}, detailedNs{};
        std::size_t lastStorage{}, storageGrowth{};
        std::size_t debugLineCapacity{32768};
        bool failed{}, episodeDone{}, complete{};
        // Explicit Phase20 request only. These flags select fixture assertions,
        // never a production algorithm or numerical policy.
        bool conditional{}, candidate{}, queriesComplete{},postPolicy{},postFailureLogged{};
        std::unique_ptr<PostCandidateAudit> postAudit;
        std::ofstream postEvents,postStages,postTicks;
        unsigned collisionCalls{}, directCalls{}, caCalls{}, projectionCalls{}, physicalFailures{};
        std::uint64_t completedTicks{};
        std::uint64_t queryOperandExportBytes{};
        std::uint64_t profiledTicks{},physicsWorldNs{};
        bool overflow{};
        _Scene* applicationScene{};
        std::unique_ptr<_Scene> sixtyHzScene;
        std::vector<std::unique_ptr<PhysicalShape>> ownedShapes;
        std::ofstream queryRows, queryInputs, helperRows, cpuRows, qualificationRows;
        inline static constexpr std::array<const char*,21> GateNames{"normal_nonnegative","coulomb","friction_energy",
            "zero_resistance","impulse_equation","sphere_energy_residual","rest_penetration","rest_height_range",
            "rest_vertical_speed","upward_correction","quiet_window","unexplained_wake","rest_energy",
            "missed_penetrating_contact","impact_response","accumulated_friction_energy","friction_optimality",
            "friction_cache_continuity","persistent_response_energy","persistent_response_balance","friction_certificate"};
        std::array<std::uint64_t,GateNames.size()> gateChecks{},gateFailures{};
        struct FailedOperand { unsigned episode{},tick{},gate{}; double value{},limit{}; PD::Event event{};
            std::array<std::uint64_t,4> identity{}; };
        std::array<FailedOperand,GateNames.size()*2> failureExamples{};
        std::size_t failureExampleCount{},failureExampleWritten{};
        struct ConditionalTick {
            unsigned episode{},tick{},bodies{},contacts{},active{},sleeping{},quiet{},wakes{},sleeps{},eligible{},missed{};
            double energy{},kinetic{},potential{},penetration{},maxLinear{},maxAngular{},y{},vy{},upwardCorrection{};
            double gravityWork{},responseWork{},responseMeasured{},integrationPotential{},integrationKinetic{},positionPotential{},sleepWork{},residual{};
            double minNormal{},maxCoulombExcess{},maxFrictionWork{},maxImpulseError{};
            std::uint64_t events{},observerNs{},capacityBytes{},capacityGrowth{},boundaryNs{},eventSinkNs{},eventObservationNs{};
        } currentTick{},pendingTick{};
        PhysicsProfileSnapshot profileBefore{};
        std::uint64_t eventObservationBefore{};
        bool tickPending{};
        double previousEnergy{},previousKinetic{},restWindowEnergy{},restMinY{},restMaxY{};
        double toiNormalImpulse{};
        const RigidBody3D* toiBodyA{};
        const RigidBody3D* toiBodyB{};
        unsigned impactResponses{};
        std::string failure;

        inline static constexpr std::array<std::pair<const char*,std::uint64_t PhysicsProfileSnapshot::*>,25> ProfileFields{{
            {"physicsWorldTimeNs",&PhysicsProfileSnapshot::physicsWorldTimeNs},{"narrowphaseTimeNs",&PhysicsProfileSnapshot::narrowphaseTimeNs},
            {"gjkTimeNs",&PhysicsProfileSnapshot::gjkTimeNs},{"epaTimeNs",&PhysicsProfileSnapshot::epaTimeNs},
            {"manifoldTimeNs",&PhysicsProfileSnapshot::manifoldTimeNs},{"solverTimeNs",&PhysicsProfileSnapshot::solverTimeNs},
            {"contactResolutionTimeNs",&PhysicsProfileSnapshot::contactResolutionTimeNs},{"broadphaseTimeNs",&PhysicsProfileSnapshot::broadphaseTimeNs},
            {"candidatePairCount",&PhysicsProfileSnapshot::candidatePairCount},{"narrowphaseCallCount",&PhysicsProfileSnapshot::narrowphaseCallCount},
            {"gjkCallCount",&PhysicsProfileSnapshot::gjkCallCount},{"gjkIterationCount",&PhysicsProfileSnapshot::gjkIterationCount},
            {"epaCallCount",&PhysicsProfileSnapshot::epaCallCount},{"epaIterationCount",&PhysicsProfileSnapshot::epaIterationCount},
            {"epaFailureCount",&PhysicsProfileSnapshot::epaFailureCount},{"supportCallCount",&PhysicsProfileSnapshot::supportCallCount},
            {"generatedContactCount",&PhysicsProfileSnapshot::generatedContactCount},{"solverIterationCount",&PhysicsProfileSnapshot::solverIterationCount},
            {"integratedBodyCount",&PhysicsProfileSnapshot::integratedBodyCount},{"contactIslandCapacityGrowthCount",&PhysicsProfileSnapshot::contactIslandCapacityGrowthCount},
            {"gravityTimeNs",&PhysicsProfileSnapshot::gravityTimeNs},{"integrationTimeNs",&PhysicsProfileSnapshot::integrationTimeNs},
            {"spinResistanceImpulseCount",&PhysicsProfileSnapshot::spinResistanceImpulseCount},{"rollingResistanceImpulseCount",&PhysicsProfileSnapshot::rollingResistanceImpulseCount},
            {"stepCount",&PhysicsProfileSnapshot::stepCount}}};
        double Kinetic(const RigidBody3D& body,const PD::BodyState& state) const noexcept
        {
            if (body.GetInverseMass()<=0) return 0;
            const auto rotation=glm::dmat3(glm::mat3_cast(state.orientation));
            const auto w=glm::transpose(rotation)*glm::dvec3(state.angular);
            return .5/body.GetInverseMass()*(glm::dot(glm::dvec3(state.linear),glm::dvec3(state.linear))+
                glm::dot(w,glm::dmat3(body.m_Shape->InertiaTensor())*w));
        }
        double Potential(const RigidBody3D& body,const PD::BodyState& state) const noexcept
        { return body.GetInverseMass()>0?-glm::dot(glm::dvec3(scene->GetPhysicsSystem()->GetPhysicsWorld()->GetGravity()),glm::dvec3(state.position))/body.GetInverseMass():0; }
        void Gate(unsigned gate,bool pass,double value,double limit,const PD::Event* event=nullptr) noexcept
        {
            ++gateChecks[gate]; if (pass) return;
            ++gateFailures[gate]; ++physicalFailures;
            if (gateFailures[gate]<=2) {
                auto& example=failureExamples[failureExampleCount++]; example.episode=episode; example.tick=tick;
                example.gate=gate; example.value=value; example.limit=limit; if (event) example.event=*event;
                if (postPolicy && event && event->a && event->b)
                    example.identity={event->a->GetIdentity().GetSlot(),event->a->GetIdentity().GetGeneration(),event->b->GetIdentity().GetSlot(),event->b->GetIdentity().GetGeneration()};
            }
            if (candidate) Fail(GateNames[gate]);
        }
        void PostEvent(const PD::Event& e,double pairKinetic,double frictionWork) noexcept
        {
            auto& audit=*postAudit; const auto op=e.operation;
            const auto* world=scene->GetPhysicsSystem()->GetPhysicsWorld();
            const auto& all=world->GetPhysicsBodies();
            bool identity=true;
            for (const auto* body:{e.a,e.b}) identity=identity && body && Index(body)<all.size() &&
                all[Index(body)]==body && world->IsBodyIdentityValid(body->GetIdentity());
            Gate(17,identity,identity?0:1,0,&e);
            if (!identity) return;
            ++audit.sequence;
            if ((episode==2 || episode==9) && tick<=6) {
                if (audit.eventCount==audit.events.size()) { overflow=true; Fail("post-candidate event capacity"); return; }
                auto& row=audit.events[audit.eventCount++]; row.event=e; row.episode=episode; row.tick=tick; row.sequence=audit.sequence;
                row.identity={e.a->GetIdentity().GetSlot(),e.a->GetIdentity().GetGeneration(),e.b->GetIdentity().GetSlot(),e.b->GetIdentity().GetGeneration()};
                row.event.a=row.event.b=nullptr; // No retained body borrower.
            }
            if (audit.responseOpen && (op==PD::Operation::GravityImpulse || op==PD::Operation::Integrate ||
                op==PD::Operation::Position || op==PD::Operation::ToiPosition || op==PD::Operation::ToiNormal ||
                op==PD::Operation::ToiFriction || op==PD::Operation::SleepDecision)) Gate(17,false,int(op),-1,&e);
            if (audit.responseOpen && op==PD::Operation::ContactRemove) {
                const bool known=e.slot>=0 && e.slot<int(audit.cache.size()) && audit.cache[e.slot].present &&
                    audit.cache[e.slot].a==e.a && audit.cache[e.slot].b==e.b &&
                    audit.cache[e.slot].localA==e.localA && audit.cache[e.slot].localB==e.localB;
                Gate(17,known,known?0:1,0,&e);
                if (known) {
                    for (int i=e.slot;i<3;++i) audit.cache[i]=audit.cache[i+1];
                    audit.cache.back()={};
                }
            }
            if (audit.responseOpen && (op==PD::Operation::PairReset || op==PD::Operation::PairEnd)) audit.cache={};
            const bool cacheOp=op==PD::Operation::ContactSnapshot || op==PD::Operation::WarmStart ||
                op==PD::Operation::BlockNormal || op==PD::Operation::NormalSliding;
            if (!cacheOp) return;
            bool linked=audit.responseOpen && e.slot>=0 && e.slot<int(audit.cache.size());
            if (!linked) { Gate(17,false,e.slot,0,&e); return; }
            auto& cache=audit.cache[e.slot];
            linked=cache.present && cache.a==e.a && cache.b==e.b;
            if (op==PD::Operation::ContactSnapshot) {
                linked=linked && !cache.projected && PD::SameState(e.beforeA,e.afterA) && PD::SameState(e.beforeB,e.afterB);
                cache.lambda=e.accumulatedAfter; cache.projected=true;
                const double limit=double(e.a->m_Friction)*e.b->m_Friction*cache.lambda[0];
                Gate(1,std::hypot(cache.lambda[1],cache.lambda[2])<=limit+1e-5*std::max(1.,std::abs(cache.lambda[0])),
                    std::hypot(cache.lambda[1],cache.lambda[2])-limit,1e-5*std::max(1.,std::abs(cache.lambda[0])),&e);
            } else {
                linked=linked && cache.projected && cache.localA==e.localA && cache.localB==e.localB &&
                    cache.lambda[0]==e.accumulatedBefore[0];
                if (op!=PD::Operation::BlockNormal) linked=linked && cache.lambda==e.accumulatedBefore;
                if (op==PD::Operation::WarmStart) {
                    linked=linked && !cache.warm && e.constraintToken!=0 && e.accumulatedBefore==e.accumulatedAfter;
                    cache.token=e.constraintToken; cache.warm=true;
                } else linked=linked && cache.warm && (op==PD::Operation::BlockNormal || cache.token==e.constraintToken);
                if (op==PD::Operation::BlockNormal) {
                    cache.reduced=cache.reduced || e.accumulatedAfter[0]<e.accumulatedBefore[0];
                    cache.lambda[0]=e.accumulatedAfter[0];
                    // Tangents are absent in this event: retain the actual cache.
                    const double excess=std::hypot(cache.lambda[1],cache.lambda[2])-double(e.a->m_Friction)*e.b->m_Friction*cache.lambda[0];
                    audit.current.maxIntermediateCone=std::max(audit.current.maxIntermediateCone,excess);
                } else {
                    for (int i=0;i<12;++i) {
                        double expected=0;
                        for (int row=0;row<3;++row) {
                            const auto& j=row==0?e.normalRow:(row==1?e.tangentRowU:e.tangentRowV);
                            expected+=j[i]*(op==PD::Operation::WarmStart?e.accumulatedAfter[row]:e.accumulatedAfter[row]-e.accumulatedBefore[row]);
                        }
                        const Vec3f& actual=i<3?e.linearImpulseA:(i<6?e.angularImpulseA:(i<9?e.linearImpulseB:e.angularImpulseB));
                        const double error=std::abs(double(actual[i%3])-expected),limit=1e-5*std::max(1.,std::abs(expected));
                        Gate(4,e.appliedImpulsesKnown && error<=limit,error,limit,&e);
                    }
                }
            }
            Gate(17,linked,linked?0:1,0,&e);
            if (op!=PD::Operation::NormalSliding) return;
            ++audit.current.sliding; ++audit.sliding;
            const double tau=1e-5*std::max(1.,pairKinetic),mu=double(e.a->m_Friction)*e.b->m_Friction;
            const double radius=mu*e.accumulatedAfter[0];
            const double t0=e.accumulatedBefore[1],t1=e.accumulatedBefore[2];
            const double u=e.accumulatedAfter[1],v=e.accumulatedAfter[2];
            const bool feasible=std::hypot(t0,t1)<=radius+1e-5*std::max(1.,std::abs(e.accumulatedAfter[0]));
            audit.current.maxIncrement=std::max(audit.current.maxIncrement,frictionWork);
            if (frictionWork>tau) { ++audit.legacyPositive; ++audit.current.legacyPositive; }
            if (feasible) Gate(2,frictionWork<=tau,frictionWork,tau,&e);
            else {
                ++audit.current.infeasible;
                const bool explained=linked && cache.warm && (cache.reduced || e.accumulatedAfter[0]<e.accumulatedBefore[0]);
                Gate(17,explained,explained?0:1,0,&e);
                if (explained) { ++audit.explained; ++audit.current.explained; }
            }
            using I=AuditInterval;
            I h00{},h01{},h11{},g0{},g1{};
            const I dn=I(e.accumulatedAfter[0])-I(e.accumulatedBefore[0]);
            for (int role=0;role<2;++role) {
                const auto* body=role?e.b:e.a; if (body->GetInverseMass()<=0) continue;
                const auto& state=role?e.beforeB:e.beforeA;
                const RigidBody3D copy=*body;
                const auto inverse=copy.GetInverseInertiaTensorWorldSpace();
                I q0[6]{};
                for (int i=0;i<6;++i) {
                    q0[i]=I(i<3?state.linear[i]:state.angular[i-3]);
                    for (int j=0;j<6;++j) {
                        I w{};
                        if (i<3 && j==i) w=I(body->GetInverseMass());
                        else if (i>=3 && j>=3) w=I(inverse[j-3][i-3]);
                        const int column=role*6+j;
                        q0[i]=q0[i]+w*(I(e.normalRow[column])*dn-I(e.tangentRowU[column])*I(t0)-I(e.tangentRowV[column])*I(t1));
                        const I ui(e.tangentRowU[role*6+i]),vi(e.tangentRowV[role*6+i]);
                        h00=h00+ui*w*I(e.tangentRowU[column]);
                        // Only the symmetric part contributes to x^T K x.
                        h01=h01+(ui*w*I(e.tangentRowV[column])+vi*w*I(e.tangentRowU[column]))*I(.5);
                        h11=h11+vi*w*I(e.tangentRowV[column]);
                    }
                    g0=g0+I(e.tangentRowU[role*6+i])*q0[i]; g1=g1+I(e.tangentRowV[role*6+i])*q0[i];
                }
            }
            auto f=[&](double x,double y) { return g0*I(x)+g1*I(y)+(h00*I::Square(I(x))+I(2)*h01*I(x)*I(y)+h11*I::Square(I(y)))*I(.5); };
            const auto actual=f(u,v),det=h00*h11-I::Square(h01);
            // Project only the diagnostic candidate inward. Feasibility is then
            // certified with outward arithmetic, not presumed from a float clamp.
            double x=u,y=v;
            if (radius==0) x=y=0;
            else if (radius>0 && std::isfinite(radius)) {
                const double lengthUpper=I::Norm(I(x),I(y)).hi;
                if (std::isfinite(lengthUpper) && lengthUpper>radius) {
                    // Construct against the same outward bound used below.
                    // Frozen binary32 tangents and mu=.25 need at most eight
                    // inward ULP refinements; invalid inputs still fail closed.
                    const double scale=std::nextafter(radius/lengthUpper,0.);
                    x=std::nextafter(x*scale,0.); y=std::nextafter(y*scale,0.);
                    for (unsigned refinement=0;refinement<8 && I::Norm(I(x),I(y)).hi>radius;++refinement) {
                        x=std::nextafter(x,0.); y=std::nextafter(y,0.);
                    }
                }
            }
            const auto feasibleLength=I::Norm(I(x),I(y));
            // Zero is exact and avoids a subnormal outward square-root artifact.
            const bool disk=(x==0 && y==0) || feasibleLength.hi<=radius;
            const auto projected=f(x,y),gx=g0+h00*I(x)+h01*I(y),gy=g1+h01*I(x)+h11*I(y);
            const auto gap=gx*I(x)+gy*I(y)+I(radius)*I::Norm(gx,gy);
            const double upper=I::Up(std::max(0.,gap.hi)+I::AbsUpper(actual-projected));
            const bool valid=h00.Finite() && h01.Finite() && h11.Finite() && g0.Finite() && g1.Finite() &&
                actual.Finite() && gap.Finite() && det.lo>=0 && h00.lo>=0 && h11.lo>=0 &&
                radius>=0 && mu>=0 && disk && std::isfinite(upper);
            Gate(20,valid,valid?0:1,0,&e);
            Gate(15,valid && actual.hi<=tau,actual.hi,tau,&e); Gate(16,valid && upper<=tau,upper,tau,&e);
            audit.current.maxF=std::max(audit.current.maxF,actual.hi); audit.current.maxGap=std::max(audit.current.maxGap,upper);
            cache.lambda=e.accumulatedAfter;
        }
        void ConditionalEvent(const PD::Event& e) noexcept
        {
            ++currentTick.events; ++eventCount;
            const auto op=e.operation;
            if (op==PD::Operation::ToiNormal) {
                toiNormalImpulse=glm::dot(glm::dvec3(e.linearImpulseB),glm::dvec3(e.normal));
                toiBodyA=e.a; toiBodyB=e.b;
                currentTick.minNormal=std::min(currentTick.minNormal,toiNormalImpulse);
                Gate(0,toiNormalImpulse>=-1e-6,toiNormalImpulse,-1e-6,&e);
            }
            if (op==PD::Operation::ToiFriction) {
                if (toiBodyA!=e.a || toiBodyB!=e.b) { Fail("conditional TOI response pairing missing"); return; }
                const double excess=glm::length(glm::dvec3(e.linearImpulseB))-.25*toiNormalImpulse;
                const double allowance=1e-5*std::max(1.,std::abs(toiNormalImpulse));
                currentTick.maxCoulombExcess=std::max(currentTick.maxCoulombExcess,excess);
                Gate(1,excess<=allowance,excess,allowance,&e);
            }
            const bool accumulated=op==PD::Operation::WarmStart || op==PD::Operation::NormalSliding || op==PD::Operation::BlockNormal;
            if (accumulated) {
                const double n=e.accumulatedAfter[0],t=std::hypot(e.accumulatedAfter[1],e.accumulatedAfter[2]);
                currentTick.minNormal=std::min(currentTick.minNormal,n);
                const double excess=t-.25*n,allowance=1e-5*std::max(1.,std::abs(n));
                currentTick.maxCoulombExcess=std::max(currentTick.maxCoulombExcess,excess);
                Gate(0,n>=-1e-6,n,-1e-6,&e);
                if (!postPolicy || op!=PD::Operation::BlockNormal) Gate(1,excess<=allowance,excess,allowance,&e);
            }
            double frictionWork=0,pairKinetic=0;
            for (unsigned role=0;role<2;++role) {
                const auto* body=role?e.b:e.a;
                if (!body || (role && body==e.a) || body->GetInverseMass()<=0) continue;
                const auto& before=role?e.beforeB:e.beforeA; const auto& after=role?e.afterB:e.afterA;
                const double kb=Kinetic(*body,before),ka=Kinetic(*body,after),du=Potential(*body,after)-Potential(*body,before);
                pairKinetic+=kb;
                if (op==PD::Operation::GravityImpulse) {
                    const auto dv=glm::dvec3(scene->GetPhysicsSystem()->GetPhysicsWorld()->GetGravity())*e.duration;
                    currentTick.gravityWork+=(glm::dot(glm::dvec3(before.linear),dv)+.5*glm::dot(dv,dv))/body->GetInverseMass();
                } else if (op==PD::Operation::Integrate) { currentTick.integrationPotential+=du; currentTick.integrationKinetic+=ka-kb; }
                else if (op==PD::Operation::Position || op==PD::Operation::ToiPosition) {
                    currentTick.positionPotential+=du; currentTick.upwardCorrection+=std::max(0.,double(after.position.y-before.position.y));
                } else if (op==PD::Operation::SleepDecision) currentTick.sleepWork+=ka-kb;
                if (e.appliedImpulsesKnown) {
                    const glm::dvec3 j(role?e.linearImpulseB:e.linearImpulseA),l(role?e.angularImpulseB:e.angularImpulseA);
                    const RigidBody3D observed=*body; // Cache-populating getters operate on the observer copy.
                    const auto inverse=glm::dmat3(observed.GetInverseInertiaTensorWorldSpace());
                    const auto dv=double(body->GetInverseMass())*j,dw=inverse*l;
                    const double expected=glm::dot(glm::dvec3(before.linear),j)+glm::dot(glm::dvec3(before.angular),l)+.5*(glm::dot(j,dv)+glm::dot(l,dw));
                    currentTick.responseWork+=expected; currentTick.responseMeasured+=ka-kb;
                    const auto actualDv=glm::dvec3(after.linear)-glm::dvec3(before.linear);
                    const auto actualDw=glm::dvec3(after.angular)-glm::dvec3(before.angular);
                    for (int component=0;component<3;++component) for (unsigned angular=0;angular<2;++angular) {
                        const double expectedDelta=angular?dw[component]:dv[component];
                        const double error=std::abs((angular?actualDw[component]:actualDv[component])-expectedDelta);
                        const double allowance=1e-5*std::max(1.,std::abs(expectedDelta));
                        currentTick.maxImpulseError=std::max(currentTick.maxImpulseError,error);
                        Gate(4,error<=allowance,error,allowance,&e);
                    }
                    if (op==PD::Operation::Spin || op==PD::Operation::Rolling) {
                        const float coefficient=op==PD::Operation::Spin?body->GetSpinResistanceLength():body->GetRollingResistanceLength();
                        if (coefficient==0) Gate(3,glm::length2(l)==0,glm::length(l),0,&e);
                        frictionWork+=expected;
                    }
                    if (op==PD::Operation::ToiFriction) frictionWork+=expected;
                    if (op==PD::Operation::NormalSliding) {
                        const int base=role?6:0; const double dn=e.accumulatedAfter[0]-e.accumulatedBefore[0];
                        const double dt0=e.accumulatedAfter[1]-e.accumulatedBefore[1],dt1=e.accumulatedAfter[2]-e.accumulatedBefore[2];
                        glm::dvec3 jn{},ln{},jt{},lt{};
                        for (int k=0;k<3;++k) {
                            jn[k]=e.normalRow[base+k]*dn; ln[k]=e.normalRow[base+3+k]*dn;
                            jt[k]=e.tangentRowU[base+k]*dt0+e.tangentRowV[base+k]*dt1;
                            lt[k]=e.tangentRowU[base+3+k]*dt0+e.tangentRowV[base+3+k]*dt1;
                        }
                        frictionWork+=glm::dot(glm::dvec3(before.linear)+double(body->GetInverseMass())*jn,jt)+
                            glm::dot(glm::dvec3(before.angular)+inverse*ln,lt)+.5*(double(body->GetInverseMass())*glm::dot(jt,jt)+glm::dot(lt,inverse*lt));
                    }
                    if (episode>=3 && episode<=5 && before.linear.y<-.1f && after.linear.y>before.linear.y+.1f) {
                        ++impactResponses; const double outgoing=episode==5?.375:0.;
                        Gate(14,std::abs(after.linear.y-outgoing)<=.01,std::abs(after.linear.y-outgoing),.01,&e);
                        if (episode==5) Gate(14,std::abs(after.linear.y/1.5-.25)<=.01,std::abs(after.linear.y/1.5-.25),.01,&e);
                    }
                    if (episode==6 && glm::length2(dw)>0) ++impactResponses;
                }
            }
            if (op==PD::Operation::NormalSliding || op==PD::Operation::ToiFriction || op==PD::Operation::Spin || op==PD::Operation::Rolling) {
                const double allowance=1e-5*std::max(1.,pairKinetic);
                currentTick.maxFrictionWork=std::max(currentTick.maxFrictionWork,frictionWork);
                if (!postPolicy || op!=PD::Operation::NormalSliding) Gate(2,frictionWork<=allowance,frictionWork,allowance,&e);
            }
            if (postPolicy) PostEvent(e,pairKinetic,frictionWork);
        }
        void FlushConditionalTick()
        {
            if (!conditional || !tickPending) return;
            const auto after=GetPhysicsProfileSnapshot(); const auto& r=pendingTick;
            if (after.stepCount-profileBefore.stepCount!=1 || after.physicsWorldTimeNs<=profileBefore.physicsWorldTimeNs) Fail("conditional completed Physics profile interval missing");
            else { ++profiledTicks; physicsWorldNs+=after.physicsWorldTimeNs-profileBefore.physicsWorldTimeNs; }
            if (r.episode==0 || r.episode==8 || r.episode==10) {
                cpuRows<<r.episode<<','<<r.tick;
                for (const auto& [name,field]:ProfileFields) cpuRows<<','<<(field==&PhysicsProfileSnapshot::candidatePairCount?after.*field:after.*field-profileBefore.*field);
                cpuRows<<','<<after.manifoldCount<<','<<after.manifoldContactCount<<','<<after.solverConstraintCount<<','<<r.observerNs<<'\n';
            }
            qualificationRows<<r.episode<<','<<r.tick<<','<<r.bodies<<','<<r.contacts<<','<<r.active<<','<<r.sleeping<<','<<r.quiet<<','<<r.wakes<<','<<r.sleeps<<','<<r.eligible<<','<<r.missed;
            for (double value:{r.energy,r.kinetic,r.potential,r.penetration,r.maxLinear,r.maxAngular,r.y,r.vy,r.upwardCorrection,
                r.gravityWork,r.responseWork,r.responseMeasured,r.integrationPotential,r.integrationKinetic,r.positionPotential,r.sleepWork,r.residual,
                r.minNormal,r.maxCoulombExcess,r.maxFrictionWork,r.maxImpulseError}) qualificationRows<<','<<value;
            qualificationRows<<','<<r.events<<','<<r.observerNs<<','<<r.capacityBytes<<','<<r.capacityGrowth
                <<','<<r.boundaryNs<<','<<r.eventSinkNs<<','<<r.eventObservationNs<<'\n';
            for (;failureExampleWritten<failureExampleCount;++failureExampleWritten) {
                const auto& f=failureExamples[failureExampleWritten]; const auto& e=f.event;
                events<<f.episode<<','<<f.tick<<','<<failureExampleWritten<<','<<int(e.operation)<<','<<f.gate<<','<<GateNames[f.gate]<<','<<f.value<<','<<f.limit;
                for (const auto& state:{e.beforeA,e.beforeB,e.afterA,e.afterB}) {
                    CsvVector(events,state.position); events<<','<<state.orientation.w<<','<<state.orientation.x<<','<<state.orientation.y<<','<<state.orientation.z;
                    CsvVector(events,state.linear); CsvVector(events,state.angular);
                }
                for (const auto& v:{e.normal,e.anchorA,e.anchorB,e.linearImpulseA,e.linearImpulseB,e.angularImpulseA,e.angularImpulseB}) CsvVector(events,v);
                for (double value:e.accumulatedBefore) events<<','<<value;
                for (double value:e.accumulatedAfter) events<<','<<value;
                events<<','<<e.restitution<<','<<e.closingSpeed<<','<<e.penetration<<','<<e.duration;
                if (postPolicy) {
                    for (auto value:f.identity) events<<','<<value;
                    events<<','<<e.slot<<','<<e.constraintToken<<','<<e.featureA<<','<<e.featureB<<','<<e.appliedImpulsesKnown;
                    for (const auto& v:{e.localA,e.localB,e.tangentU,e.tangentV}) CsvVector(events,v);
                    for (const auto& row:{e.normalRow,e.tangentRowU,e.tangentRowV}) for (auto value:row) events<<','<<value;
                }
                events<<'\n';
            }
            tickPending=false;
            if (!cpuRows || !qualificationRows || !events) Fail("conditional compact evidence write failed");
        }
        void ConditionalNarrowphase(const PhysicsWorld& world,ManifoldCollector& manifolds) noexcept
        {
            const auto& all=world.GetPhysicsBodies(); if (all.empty()) return;
            const unsigned groundIndex=episode==0?4u:0u;
            std::array<bool,185> admitted{};
            for (auto& manifold:manifolds.m_Manifolds) for (int j=0;j<manifold.GetNumContacts();++j) {
                const auto c=manifold.GetContact(j); const auto a=Index(c.m_BodyA),b=Index(c.m_BodyB);
                if (a==groundIndex && b<admitted.size()) admitted[b]=true;
                if (b==groundIndex && a<admitted.size()) admitted[a]=true;
            }
            for (unsigned i=1;i<all.size();++i) {
                const auto& b=*all[i]; if (b.IsSleeping() || b.GetInverseMass()<=0 || b.m_Shape->GetShapeType()!=ShapeType::Sphere) continue;
                const float radius=static_cast<const ShapeSphere*>(b.m_Shape)->GetRadius();
                if (std::abs(b.m_Position.x)>50-radius || std::abs(b.m_Position.z)>50-radius) continue;
                const double penetration=.5+radius-b.m_Position.y;
                if (penetration>1e-4) { ++currentTick.eligible; if (!admitted[i]) ++currentTick.missed; Gate(13,admitted[i],penetration,1e-4); }
            }
        }
        void ConditionalEndTick(const PhysicsWorld& world,ManifoldCollector& manifolds,std::size_t bytes)
        {
            auto& r=currentTick; const auto& all=world.GetPhysicsBodies(); r.bodies=unsigned(all.size());
            if (all.size()!=inputs.size() || previous.size()!=all.size()) { Fail("conditional body membership mismatch"); return; }
            for (unsigned i=0;i<all.size();++i) {
                const auto& b=*all[i]; const auto state=PD::Capture(b);
                if (!b.HasFiniteState()) { Fail("conditional nonfinite body state"); return; }
                if (b.GetInverseMass()>0) {
                    const double speed=glm::length(glm::dvec3(state.linear)),spin=glm::length(glm::dvec3(state.angular));
                    r.active+=!state.sleeping; r.sleeping+=state.sleeping;
                    const auto& thresholds=b.GetSleepSettings();
                    r.quiet+=state.sleeping || (speed<=thresholds.linearSpeedThreshold && spin<=thresholds.angularSpeedThreshold);
                    r.wakes+=previous[i].sleeping && !state.sleeping; r.sleeps+=!previous[i].sleeping && state.sleeping;
                    r.kinetic+=Kinetic(b,state); r.potential+=Potential(b,state);
                    r.maxLinear=std::max(r.maxLinear,speed); r.maxAngular=std::max(r.maxAngular,spin);
                    double bottom=state.position.y;
                    if (b.m_Shape->GetShapeType()==ShapeType::Sphere) bottom-=static_cast<const ShapeSphere*>(b.m_Shape)->GetRadius();
                    else {
                        const auto bounds=b.m_Shape->GetBounds(); bottom=std::numeric_limits<double>::max();
                        for (int x=0;x<2;++x) for (int y=0;y<2;++y) for (int z=0;z<2;++z) {
                            const Vec3f p(x?bounds.maxs.x:bounds.mins.x,y?bounds.maxs.y:bounds.mins.y,z?bounds.maxs.z:bounds.mins.z);
                            bottom=std::min(bottom,double((state.position+state.orientation*p).y));
                        }
                    }
                    if (std::abs(state.position.x)<49 && std::abs(state.position.z)<49) r.penetration=std::max(r.penetration,.5-bottom);
                    if (i==1) { r.y=state.position.y; r.vy=state.linear.y; }
                }
                previous[i]=state;
            }
            for (auto& manifold:manifolds.m_Manifolds) for (int j=0;j<manifold.GetNumContacts();++j) {
                const auto c=manifold.GetContact(j);
                if (!world.IsBodyIdentityValid(c.m_BodyA->GetIdentity()) || !world.IsBodyIdentityValid(c.m_BodyB->GetIdentity())) { Fail("conditional stale contact identity"); return; }
                const RigidBody3D observedA=*c.m_BodyA,observedB=*c.m_BodyB;
                const auto a=observedA.BodySpaceToWorldSpace(c.ptOnA_LocalSpace),b=observedB.BodySpaceToWorldSpace(c.ptOnB_LocalSpace);
                r.penetration=std::max(r.penetration,-double(glm::dot(a-b,c.normal))); ++r.contacts;
            }
            debugLineCapacity=std::max(std::size_t{32768},all.size()*63+std::size_t(r.contacts)*50);
            r.energy=r.kinetic+r.potential;
            if (!std::isfinite(r.energy)) { Fail("conditional nonfinite energy"); return; }
            r.residual=r.energy-previousEnergy-r.gravityWork-r.responseWork-r.integrationPotential-r.positionPotential-r.sleepWork;
            if (!LightInterval() && (episode==1 || episode==7 || episode==9)) {
                const double allowance=1e-4*std::max({1.,previousKinetic,std::abs(r.integrationPotential+r.positionPotential)});
                Gate(5,std::abs(r.residual)<=allowance,std::abs(r.residual),allowance);
            }
            if (episode==0) Gate(12,r.energy<=30391.2,r.energy,30391.2);
            if (episode==9) {
                const auto work=GetPhysicsProfileSnapshot();
                const auto secondary=(work.spinResistanceImpulseCount-profileBefore.spinResistanceImpulseCount)+
                    (work.rollingResistanceImpulseCount-profileBefore.rollingResistanceImpulseCount);
                Gate(3,secondary==0,double(secondary),0);
            }
            if (episode==1) {
                if (tick>60) Gate(9,r.upwardCorrection<=.005,r.upwardCorrection,.005);
                if (tick==181) { restWindowEnergy=r.energy; restMinY=restMaxY=r.y; }
                if (tick>=181) {
                    restMinY=std::min(restMinY,r.y); restMaxY=std::max(restMaxY,r.y);
                    Gate(6,r.penetration<=.035,r.penetration,.035); Gate(7,restMaxY-restMinY<=.005,restMaxY-restMinY,.005);
                    Gate(8,std::abs(r.vy)<=.01,std::abs(r.vy),.01); Gate(12,r.energy-restWindowEnergy<=.001,r.energy-restWindowEnergy,.001);
                }
            }
            const unsigned quietTicks=episode==8?120:60;
            if ((episode==1 || episode==2 || episode==8 || episode==10) && tick>TickLimit()-quietTicks) {
                Gate(10,r.quiet==r.active+r.sleeping,r.active+r.sleeping-r.quiet,0);
                Gate(6,r.penetration<=.035,r.penetration,.035); Gate(11,r.wakes==0,r.wakes,0);
            }
            if (tick==TickLimit() && episode>=3 && episode<=6) Gate(14,impactResponses>0,impactResponses,1);
            previousEnergy=r.energy; previousKinetic=r.kinetic;
            if (bytes>lastStorage) storageGrowth+=bytes-lastStorage; lastStorage=bytes;
            r.capacityBytes=bytes; r.capacityGrowth=storageGrowth;
            pendingTick=r; tickPending=true;
            if (failed || tick==TickLimit()) { episodeDone=true; scene->SetPaused(true); }
        }

        unsigned TickLimit() const noexcept { return conditional?ConditionalDurations[episode]:Durations[episode]; }
        const char* EpisodeName() const noexcept { return conditional?ConditionalNames[episode]:Names[episode]; }
        bool LightInterval() const noexcept { return conditional && (episode==0 || episode==8 || episode==10); }
        double StepSeconds() const noexcept { return conditional && episode!=0?1.0/60.0:1.0/120.0; }
        void RetireOwnedScene() noexcept
        {
            if (!sixtyHzScene) return;
            PD::observer=nullptr; PD::queryObserver=nullptr;
            sixtyHzScene->OnRuntimeStop(); // Retire body borrowers before shapes/resources.
            entities.clear(); sixtyHzScene.reset(); ownedShapes.clear();
            scene=applicationScene;
        }

        explicit Session(const RestingContactDiagnostic& c):config(c) {}
        void Fail(const char* reason) noexcept
        {
            if (!failed) {
                failed=true; failure=reason;
                if (!postPolicy) Log::GetCoreLogger()->error("PHASE19 diagnostic: {}",reason);
            }
        }
        bool Open()
        {
            if (const char* request=std::getenv("GENGINE_P20_CONDITIONAL_FOCUSED_REQUEST")) {
                const auto expected=(std::filesystem::path(config.outputDirectory).parent_path()/"focused-request.txt").lexically_normal();
                const auto path=std::filesystem::path(request);
                std::ifstream in(path); std::string magic,role,latticeHash,replayHash,end;
                unsigned cases{},calls{},direct{},ca{},projection{},episodes{},ticks{},seconds{},wall{};
                if (!path.is_absolute() || path.lexically_normal()!=expected ||
                    !(in>>magic>>role>>latticeHash>>replayHash>>cases>>calls>>direct>>ca>>projection>>episodes>>ticks>>seconds>>wall>>end) ||
                    (magic!="P20_CONDITIONAL_FOCUSED_V1" && magic!="P20_POST_CANDIDATE_FOCUSED_V1") || (role!="control" && role!="candidate") ||
                    latticeHash!="79eb5a76b06726b77333fff93ddb47999671df025ef1c75322494b8ac62116c6" ||
                    replayHash!="0285e8dd4303d7aee1c4ebc41a504734faccf4b1b2309faf3bfb3a854b719ee6" ||
                    cases!=25 || calls!=50 || direct!=16 || ca!=12 || projection!=4 || episodes!=11 ||
                    ticks!=4980 || seconds!=68 || wall!=120 || end!="END") { Fail("invalid conditional focused request"); return false; }
                in>>std::ws; if (!in.eof() || config.observation!=Observation::Observed) { Fail("conditional request trailing bytes or observer mode"); return false; }
                conditional=true; candidate=role=="candidate"; postPolicy=magic=="P20_POST_CANDIDATE_FOCUSED_V1";
                if (postPolicy) failure.reserve(256); // Failure recording cannot allocate in a Physics callback.
                if (postPolicy && !candidate) { Fail("post-candidate policy requires candidate role"); return false; }
            }
            if (!LoadLattice(config.latticePath,lattice)) { Fail("invalid frozen lattice input"); return false; }
            const auto dir=std::filesystem::path(config.outputDirectory);
            // The runner creates an empty contained output directory. Never overwrite evidence.
            const char* names[]{"bodies.bin","stages.bin","contacts.csv","timing.csv","metrics.csv",
                "events.csv","frames.csv","initial.csv","islands.csv","result.json"};
            std::error_code ec;
            if (!std::filesystem::is_directory(dir,ec) || ec) return false;
            for (auto name:names) if (std::filesystem::exists(dir/name,ec) || ec) return false;
            bodies.open(dir/"bodies.bin",std::ios::binary); stages.open(dir/"stages.bin",std::ios::binary);
            contacts.open(dir/"contacts.csv"); timing.open(dir/"timing.csv"); metrics.open(dir/"metrics.csv");
            events.open(dir/"events.csv"); frames.open(dir/"frames.csv"); initial.open(dir/"initial.csv");
            islandRows.open(dir/"islands.csv");
            for (auto* out:{&contacts,&timing,&metrics,&events,&frames,&initial}) *out<<std::setprecision(17);
            contacts<<"episode,tick,a,b,feature_a,feature_b,ax,ay,az,bx,by,bz,nx,ny,nz,depth,lax,lay,laz,lbx,lby,lbz\n";
            islandRows<<"episode,tick,island,role,a,b\n";
            timing<<"episode,tick,stage,interval_ns,boundary_ns,detailed_ns,storage_bytes\n";
            metrics<<"episode,tick,bodies,contacts,islands,active,sleeping,wakes,sleeps,max_linear,max_angular,drift,energy,penetration,capacity_growth_bytes,finite\n";
            events<<(conditional?"episode,tick,example,operation,gate,gate_name,value,limit":"episode,tick,sequence,operation,a,b,nx,ny,nz,ax,ay,az,bx,by,bz,jx,jy,jz,tx,ty,tz,l0_before,l1_before,l2_before,l0_after,l1_after,l2_after,restitution,closing_speed,penetration");
            for (auto prefix:{"a_before","b_before","a_after","b_after"}) {
                if (conditional) for (auto field:{"px","py","pz","qw","qx","qy","qz","vx","vy","vz","wx","wy","wz"}) events<<','<<prefix<<'_'<<field;
                else for (auto field:{"px","py","pz","vx","vy","vz","wx","wy","wz"}) events<<','<<prefix<<'_'<<field;
            }
            if (conditional) {
                for (auto prefix:{"normal","anchor_a","anchor_b","linear_impulse_a","linear_impulse_b","angular_impulse_a","angular_impulse_b"})
                    for (char axis:std::string("xyz")) events<<','<<prefix<<'_'<<axis;
                events<<",l0_before,l1_before,l2_before,l0_after,l1_after,l2_after,restitution,closing_speed,penetration,duration";
                if (postPolicy) {
                    events<<",a_slot,a_generation,b_slot,b_generation,slot,token,feature_a,feature_b,impulses_known";
                    for (auto prefix:{"local_a","local_b","tangent_u","tangent_v"}) for (char axis:std::string("xyz")) events<<','<<prefix<<'_'<<axis;
                    for (auto prefix:{"jn","ju","jv"}) for (unsigned i=0;i<12;++i) events<<','<<prefix<<'_'<<i;
                }
            }
            events<<'\n';
            frames<<"episode,tick,elapsed,pending,discarded,total_steps,steps_this_frame,interpolation\n";
            initial<<"episode,index,slot,generation,shape,type,inverse_mass,elasticity,friction,layer,mask,radius,px,py,pz,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,spin_length,rolling_length,linear_sleep,angular_sleep,dwell,ixx,iyy,izz\n";
            sink={this,config.observation==Observation::Observed,&Select,&Event,&Boundary};
            if (postPolicy) {
                postAudit=std::make_unique<PostCandidateAudit>(); // Before any measured Physics interval.
                for (auto name:{"post-events.csv","post-stages.csv","post-ticks.csv"})
                    if (std::filesystem::exists(dir/name,ec) || ec) { Fail("post-candidate evidence destination exists"); return false; }
                postEvents.open(dir/"post-events.csv"); postStages.open(dir/"post-stages.csv"); postTicks.open(dir/"post-ticks.csv");
                for (auto* stream:{&postEvents,&postStages,&postTicks}) *stream<<std::setprecision(17);
                postEvents<<"episode,tick,sequence,operation,a_slot,a_generation,b_slot,b_generation,slot,previous_slot,reason,token,feature_a,feature_b,impulses_known";
                for (auto prefix:{"a_before","b_before","a_after","b_after"}) PostStateHeader(postEvents,prefix);
                for (auto prefix:{"normal","anchor_a","anchor_b","local_a","local_b","tangent_u","tangent_v","linear_a","linear_b","angular_a","angular_b"})
                    for (char axis:std::string("xyz")) postEvents<<','<<prefix<<'_'<<axis;
                for (auto prefix:{"before","after"}) for (unsigned i=0;i<3;++i) postEvents<<','<<prefix<<'_'<<i;
                for (auto prefix:{"jn","ju","jv"}) for (unsigned i=0;i<12;++i) postEvents<<','<<prefix<<'_'<<i;
                postEvents<<",restitution,closing_speed,penetration,time,duration,bias\n";
                postStages<<"episode,tick,stage,slot,generation"; PostStateHeader(postStages,"state"); postStages<<'\n';
                postTicks<<"episode,tick,sliding,legacy_positive,infeasible_old,explained,max_f_upper,max_gap_upper,max_increment,max_intermediate_cone,before_k,after_k,signed_work,balance\n";
                if (!postEvents || !postStages || !postTicks) { Fail("post-candidate evidence open failed"); return false; }
                sink.violation=[](void* context) noexcept { static_cast<Session*>(context)->Fail("post-candidate observer altered physical state"); };
            }
            if (conditional) {
                for (auto name:{"focused-queries.csv","focused-inputs.csv","focused-helpers.jsonl","focused-cpu.csv","focused-qualification.csv","focused-gates.csv"})
                    if (std::filesystem::exists(dir/name,ec) || ec) { Fail("conditional evidence destination exists"); return false; }
                queryRows.open(dir/"focused-queries.csv"); queryInputs.open(dir/"focused-inputs.csv");
                helperRows.open(dir/"focused-helpers.jsonl"); cpuRows.open(dir/"focused-cpu.csv"); qualificationRows.open(dir/"focused-qualification.csv");
                for (auto* out:{&queryRows,&queryInputs,&helperRows,&cpuRows,&qualificationRows}) *out<<std::setprecision(17);
                queryRows<<"case,mode,hit,geometry_ok,pointers_ok,unchanged,elapsed_ns,observer_ns";
                for (unsigned i=0;i<19;++i) queryRows<<",contact_bits_"<<i; queryRows<<'\n';
                queryInputs<<"case,body,shape,type,inverse_mass_bits,elasticity_bits,friction_bits,layer,mask,spin_bits,rolling_bits,linear_sleep_bits,angular_sleep_bits,dwell_bits,sleeping,inactive_bits,radius_bits";
                for (unsigned i=0;i<13;++i) queryInputs<<",pose_bits_"<<i;
                for (const char* prefix:{"com","bounds_min","bounds_max"}) for (char axis:std::string("xyz")) queryInputs<<','<<prefix<<'_'<<axis<<"_bits";
                for (unsigned i=0;i<9;++i) queryInputs<<",inertia_bits_"<<i; queryInputs<<'\n';
                cpuRows<<"episode,tick"; for (const auto& [name,field]:ProfileFields) cpuRows<<','<<name;
                cpuRows<<",manifoldCount,manifoldContactCount,solverConstraintCount,observer_ns\n";
                qualificationRows<<"episode,tick,bodies,contacts,active,sleeping,quiet,wakes,sleeps,eligible_awake_ground,missed_awake_ground,energy,kinetic,potential,penetration,max_linear,max_angular,y,vy,upward_correction,gravity_work,response_work,response_measured,integration_potential,integration_kinetic,position_potential,sleep_work,energy_residual,min_normal,max_coulomb_excess,max_friction_work,max_impulse_error,events,observer_ns,capacity_bytes,capacity_growth,boundary_ns,event_sink_ns,event_observation_ns\n";
                if (!queryRows || !queryInputs || !helperRows || !cpuRows || !qualificationRows) { Fail("conditional evidence open failed"); return false; }
            }
            return bodies && stages && contacts && timing && metrics && events && frames && initial && islandRows;
        }
        unsigned Index(const RigidBody3D* b) const noexcept
        {
            if (!b) return std::numeric_limits<unsigned>::max();
            const auto slot=b->GetIdentity().GetSlot();
            // Every episode is a freshly owned world; verify this mapping at import.
            return slot>0 && slot<=inputs.size() ? static_cast<unsigned>(slot-1) : std::numeric_limits<unsigned>::max();
        }
        bool Selected(unsigned index) const noexcept
        { return episode!=0 || index==10 || index==12 || index==22 || index==24; }
        static bool Select(void* context,const RigidBody3D* a,const RigidBody3D* b) noexcept
        {
            const auto& self=*static_cast<Session*>(context);
            return self.Selected(self.Index(a)) || self.Selected(self.Index(b));
        }
        static void Event(void* context,const PD::Event& e) noexcept
        {
            auto& self=*static_cast<Session*>(context);
            const auto start=PD::Nanoseconds();
            if (self.conditional) { self.ConditionalEvent(e); self.detailedNs+=PD::Nanoseconds()-start; return; }
            auto& out=self.events;
            out<<self.episode<<','<<self.tick<<','<<++self.sequence<<','<<int(e.operation)<<','<<self.Index(e.a)<<','<<self.Index(e.b);
            for (const auto& v:{e.normal,e.anchorA,e.anchorB,e.linearImpulseB,e.angularImpulseB}) CsvVector(out,v);
            for (auto v:e.accumulatedBefore) out<<','<<v;
            for (auto v:e.accumulatedAfter) out<<','<<v;
            out<<','<<e.restitution<<','<<e.closingSpeed<<','<<e.penetration;
            for (const auto& state:{e.beforeA,e.beforeB,e.afterA,e.afterB}) {
                CsvVector(out,state.position); CsvVector(out,state.linear); CsvVector(out,state.angular);
            }
            out<<'\n'; ++self.eventCount;
            if (!out) self.Fail("contact trace write failed");
            self.detailedNs+=PD::Nanoseconds()-start;
        }
        static void PostBoundary(void* context,PD::Stage stage,const PhysicsWorld& world,
                                 ManifoldCollector& manifolds,std::span<const ContactIsland> islands,
                                 std::size_t bytes,int passes) noexcept
        {
            auto& self=*static_cast<Session*>(context);
            const auto start=PD::Nanoseconds(),oldRecorder=self.recorderNs;
            Boundary(context,stage,world,manifolds,islands,bytes,passes);
            auto& audit=*self.postAudit; const auto& all=world.GetPhysicsBodies();
            if (stage==PD::Stage::Begin) {
                audit.current={}; audit.current.episode=self.episode; audit.current.tick=self.tick;
                audit.responseOpen=audit.responseComplete=false; audit.cache={};
            }
            if ((self.episode==2 || self.episode==9) && self.tick<=6) for (const auto* body:all) {
                if (audit.stageCount==audit.stages.size()) { self.overflow=true; self.Fail("post-candidate stage capacity"); break; }
                auto& row=audit.stages[audit.stageCount++]; row.state=PD::Capture(*body);
                row.episode=self.episode; row.tick=self.tick; row.stage=unsigned(stage);
                row.slot=body->GetIdentity().GetSlot(); row.generation=body->GetIdentity().GetGeneration();
            }
            if (stage==PD::Stage::Narrowphase) {
                self.Gate(17,all.size()==2,all.size(),2);
                if (all.size()==2) for (unsigned i=0;i<2;++i) {
                    audit.responsePose[i]=PD::Capture(*all[i]); audit.current.beforeK+=self.Kinetic(*all[i],audit.responsePose[i]);
                }
                audit.responseWorkStart=self.currentTick.responseWork; audit.responseOpen=true;
                unsigned pairs=0;
                for (auto& manifold:manifolds.m_Manifolds) {
                    if (manifold.GetNumContacts()==0) continue;
                    ++pairs; self.Gate(17,pairs==1 && manifold.GetNumContacts()<=4,pairs,1);
                    for (int slot=0;slot<manifold.GetNumContacts() && slot<4;++slot) {
                        const auto c=manifold.GetContact(slot); auto& cache=audit.cache[slot];
                        cache.present=true; cache.a=c.m_BodyA; cache.b=c.m_BodyB;
                        cache.localA=c.ptOnA_LocalSpace; cache.localB=c.ptOnB_LocalSpace;
                    }
                }
            }
            if (stage==PD::Stage::Solve) {
                bool pose=audit.responseOpen && all.size()==2;
                if (all.size()==2) for (unsigned i=0;i<2;++i) {
                    const auto state=PD::Capture(*all[i]); const auto& before=audit.responsePose[i];
                    pose=pose && state.position==before.position && state.orientation==before.orientation;
                    audit.current.afterK+=self.Kinetic(*all[i],state);
                }
                self.Gate(17,pose,pose?0:1,0);
                audit.current.work=self.currentTick.responseWork-audit.responseWorkStart;
                const double delta=audit.current.afterK-audit.current.beforeK,tau=1e-5*std::max(1.,audit.current.beforeK);
                audit.current.balance=delta-audit.current.work;
                self.Gate(18,pose && std::isfinite(delta) && delta<=tau,delta,tau);
                self.Gate(19,pose && std::isfinite(audit.current.balance) && std::abs(audit.current.balance)<=tau,std::abs(audit.current.balance),tau);
                audit.responseOpen=false; audit.responseComplete=true;
            }
            if (stage==PD::Stage::End) {
                self.Gate(17,audit.responseComplete && !audit.responseOpen,audit.responseComplete?0:1,0);
                if (audit.pendingCount==audit.pending.size()) { self.overflow=true; self.Fail("post-candidate tick capacity"); }
                else audit.pending[audit.pendingCount++]=audit.current;
                ++audit.ticks;
            }
            const auto elapsed=PD::Nanoseconds()-start,oldCost=self.recorderNs-oldRecorder;
            const auto extra=elapsed>=oldCost?elapsed-oldCost:0;
            self.callbackNs+=extra; self.recorderNs+=extra;
            if (stage==PD::Stage::End) {
                self.pendingTick.boundaryNs=self.callbackNs;
                self.pendingTick.observerNs=self.callbackNs+self.pendingTick.eventObservationNs;
            }
        }
        static void PostStateHeader(std::ostream& out,const char* prefix)
        {
            for (const char* field:{"px","py","pz","qw","qx","qy","qz","vx","vy","vz","wx","wy","wz","inactive","sleeping"}) out<<','<<prefix<<'_'<<field;
        }
        static void PostStateRow(std::ostream& out,const PD::BodyState& state)
        {
            CsvVector(out,state.position); out<<','<<state.orientation.w<<','<<state.orientation.x<<','<<state.orientation.y<<','<<state.orientation.z;
            CsvVector(out,state.linear); CsvVector(out,state.angular); out<<','<<state.inactive<<','<<state.sleeping;
        }
        void FlushPostAudit()
        {
            if (!postAudit) return;
            auto& audit=*postAudit;
            for (;audit.eventWritten<audit.eventCount;++audit.eventWritten) {
                const auto& row=audit.events[audit.eventWritten]; const auto& e=row.event;
                postEvents<<row.episode<<','<<row.tick<<','<<row.sequence<<','<<int(e.operation);
                for (auto value:row.identity) postEvents<<','<<value;
                postEvents<<','<<e.slot<<','<<e.previousSlot<<','<<e.reason<<','<<e.constraintToken<<','<<e.featureA<<','<<e.featureB<<','<<e.appliedImpulsesKnown;
                for (const auto& state:{e.beforeA,e.beforeB,e.afterA,e.afterB}) PostStateRow(postEvents,state);
                for (const auto& vector:{e.normal,e.anchorA,e.anchorB,e.localA,e.localB,e.tangentU,e.tangentV,
                    e.linearImpulseA,e.linearImpulseB,e.angularImpulseA,e.angularImpulseB}) CsvVector(postEvents,vector);
                for (const auto& values:{e.accumulatedBefore,e.accumulatedAfter}) for (auto value:values) postEvents<<','<<value;
                for (const auto& values:{e.normalRow,e.tangentRowU,e.tangentRowV}) for (auto value:values) postEvents<<','<<value;
                for (auto value:{e.restitution,e.closingSpeed,e.penetration,e.time,e.duration,e.bias}) postEvents<<','<<value;
                postEvents<<'\n';
            }
            for (;audit.stageWritten<audit.stageCount;++audit.stageWritten) {
                const auto& row=audit.stages[audit.stageWritten];
                postStages<<row.episode<<','<<row.tick<<','<<row.stage<<','<<row.slot<<','<<row.generation;
                PostStateRow(postStages,row.state); postStages<<'\n';
            }
            for (unsigned i=0;i<audit.pendingCount;++i) {
                const auto& row=audit.pending[i];
                postTicks<<row.episode<<','<<row.tick<<','<<row.sliding<<','<<row.legacyPositive<<','<<row.infeasible<<','<<row.explained;
                for (double value:{row.maxF,row.maxGap,row.maxIncrement,row.maxIntermediateCone,row.beforeK,row.afterK,row.work,row.balance}) postTicks<<','<<value;
                postTicks<<'\n';
            }
            audit.pendingCount=0;
            if (!postEvents || !postStages || !postTicks) Fail("post-candidate evidence write failed");
            if (failed && !postFailureLogged) {
                Log::GetCoreLogger()->error("PHASE20 post-candidate diagnostic: {}",failure); postFailureLogged=true;
            }
        }
        void RecordBodies(const PhysicsWorld& world)
        {
            if (conditional) return;
            const auto& all=world.GetPhysicsBodies();
            if (all.size()!=inputs.size()) { Fail("unexpected body membership"); return; }
            for (unsigned i=0;i<all.size();++i) {
                const auto* b=all[i];
                Binary(bodies,episode); Binary(bodies,tick); Binary(bodies,i);
                Binary(bodies,b->GetIdentity().GetSlot()); Binary(bodies,b->GetIdentity().GetGeneration());
                BinaryState(bodies,PD::Capture(*b));
            }
        }
        static void Boundary(void* context,PD::Stage stage,const PhysicsWorld& world,
                             ManifoldCollector& manifolds,std::span<const ContactIsland> islands,
                             std::size_t bytes,int passes) noexcept
        {
            auto& self=*static_cast<Session*>(context);
            const auto now=PD::Nanoseconds();
            if (self.conditional) {
                if (stage==PD::Stage::Begin) {
                    self.FlushConditionalTick();
                    ++self.tick; self.currentTick={}; self.currentTick.episode=self.episode; self.currentTick.tick=self.tick;
                    self.toiNormalImpulse=0; self.toiBodyA=self.toiBodyB=nullptr;
                    self.profileBefore=GetPhysicsProfileSnapshot(); self.callbackNs=self.detailedNs=0;
                    self.eventObservationBefore=self.sink.observationNs;
                    if (passes!=1 || self.tick>self.TickLimit()) self.Fail("conditional tick/pass budget mismatch");
                }
                if (stage==PD::Stage::Narrowphase) self.ConditionalNarrowphase(world,manifolds);
                if (stage==PD::Stage::End) self.ConditionalEndTick(world,manifolds,bytes);
                const auto cost=PD::Nanoseconds()-now; self.callbackNs+=cost; self.recorderNs+=cost;
                if (stage==PD::Stage::End) {
                    self.pendingTick.boundaryNs=self.callbackNs;
                    self.pendingTick.eventSinkNs=self.detailedNs;
                    self.pendingTick.eventObservationNs=self.sink.observationNs-self.eventObservationBefore;
                    self.pendingTick.observerNs=self.callbackNs+self.pendingTick.eventObservationNs;
                }
                return;
            }
            if (stage==PD::Stage::Begin) {
                ++self.tick; self.stageClock=self.stepClock=now; self.callbackNs=0; self.detailedNs=0;
                if (passes!=1 || self.tick>self.TickLimit()) self.Fail("tick/pass budget mismatch");
            }
            const auto interval=now-self.stageClock;
            const auto& all=world.GetPhysicsBodies();
            for (unsigned i=0;i<all.size();++i) if (self.Selected(i)) {
                Binary(self.stages,self.episode); Binary(self.stages,self.tick);
                Binary(self.stages,static_cast<unsigned>(stage)); Binary(self.stages,i);
                BinaryState(self.stages,PD::Capture(*all[i]));
            }
            if (stage==PD::Stage::End) self.EndTick(world,manifolds,islands,bytes);
            const auto cost=PD::Nanoseconds()-now;
            self.callbackNs+=cost; self.recorderNs+=cost;
            self.timing<<self.episode<<','<<self.tick<<','<<int(stage)<<','<<interval<<','<<cost<<','<<self.detailedNs<<','<<bytes<<'\n';
            if (!self.stages || !self.timing) self.Fail("boundary trace write failed");
            self.stageClock=PD::Nanoseconds();
        }
        void EndTick(const PhysicsWorld& world,ManifoldCollector& manifolds,
                     std::span<const ContactIsland> islands,std::size_t bytes)
        {
            double energy=0,maxLinear=0,maxAngular=0,drift=0,penetration=0;
            unsigned active=0,sleeping=0,contactCount=0;
            bool finite=true;
            const auto& all=world.GetPhysicsBodies();
            if (all.size()!=inputs.size() || previous.size()!=all.size()) { Fail("unexpected tick membership"); return; }
            for (unsigned i=0;i<all.size();++i) {
                const auto& b=*all[i]; const auto s=PD::Capture(b);
                finite &= b.HasFiniteState();
                if (b.GetInverseMass()>0) {
                    active+=!s.sleeping; sleeping+=s.sleeping;
                    wakeCount+=previous[i].sleeping && !s.sleeping;
                    sleepCount+=!previous[i].sleeping && s.sleeping;
                    const glm::dvec3 v(s.linear),w(s.angular);
                    const auto rotation=glm::dmat3(glm::mat3_cast(s.orientation));
                    const auto localW=glm::transpose(rotation)*w;
                    const auto mass=1.0/double(b.GetInverseMass());
                    energy+=mass*(.5*glm::dot(v,v)+.5*glm::dot(localW,glm::dmat3(b.m_Shape->InertiaTensor())*localW)-
                                  glm::dot(glm::dvec3(world.GetGravity()),glm::dvec3(s.position)));
                    maxLinear=std::max(maxLinear,glm::length(v)); maxAngular=std::max(maxAngular,glm::length(w));
                    drift=std::max(drift,glm::length(glm::dvec3(s.position)-glm::dvec3(inputs[i].position)));
                }
                previous[i]=s;
            }
            for (auto& manifold:manifolds.m_Manifolds) for (int j=0;j<manifold.GetNumContacts();++j) {
                const auto c=manifold.GetContact(j);
                if (!world.IsBodyIdentityValid(c.m_BodyA->GetIdentity()) ||
                    !world.IsBodyIdentityValid(c.m_BodyB->GetIdentity())) { Fail("stale contact identity"); return; }
                const auto a=c.m_BodyA->BodySpaceToWorldSpace(c.ptOnA_LocalSpace);
                const auto b=c.m_BodyB->BodySpaceToWorldSpace(c.ptOnB_LocalSpace);
                const double depth=-glm::dot(a-b,c.normal);
                penetration=std::max(penetration,depth); ++contactCount;
                contacts<<episode<<','<<tick<<','<<Index(c.m_BodyA)<<','<<Index(c.m_BodyB)<<','<<c.featureA<<','<<c.featureB;
                CsvVector(contacts,a); CsvVector(contacts,b); CsvVector(contacts,c.normal); contacts<<','<<depth;
                CsvVector(contacts,c.ptOnA_LocalSpace); CsvVector(contacts,c.ptOnB_LocalSpace); contacts<<'\n';
            }
            for (unsigned i=0;i<islands.size();++i) {
                for (auto id:islands[i].dynamicBodies) islandRows<<episode<<','<<tick<<','<<i<<",dynamic,"<<id.GetSlot()-1<<",-1\n";
                for (auto id:islands[i].boundaryBodies) islandRows<<episode<<','<<tick<<','<<i<<",boundary,"<<id.GetSlot()-1<<",-1\n";
                for (const auto& pair:islands[i].contactPairs) islandRows<<episode<<','<<tick<<','<<i<<",pair,"<<pair.bodyA.GetSlot()-1<<','<<pair.bodyB.GetSlot()-1<<'\n';
            }
            // Accepted fixtures contain only spheres/boxes: at most 63 body lines
            // and exactly 50 lines per contact. Preserve every overlay primitive.
            debugLineCapacity=std::max(std::size_t{32768},all.size()*63+std::size_t(contactCount)*50);
            if (bytes>lastStorage) storageGrowth+=bytes-lastStorage;
            lastStorage=bytes;
            metrics<<episode<<','<<tick<<','<<all.size()<<','<<contactCount<<','<<islands.size()<<','<<active<<','<<sleeping<<','
                <<wakeCount<<','<<sleepCount<<','<<maxLinear<<','<<maxAngular<<','<<drift<<','<<energy<<','<<penetration<<','<<storageGrowth<<','<<finite<<'\n';
            RecordBodies(world);
            if (!finite || !std::isfinite(energy)) Fail("non-finite physical state");
            if (!bodies || !contacts || !metrics || !islandRows) Fail("common recording failed");
            if (failed || tick==TickLimit()) {
                episodeDone=true; scene->SetPaused(true);
            }
        }
        bool BeginEpisode(_Scene& target,SceneRenderResources& resources,
                          const SceneAssets& sceneAssets,std::vector<std::unique_ptr<PhysicalShape>>& shapes)
        {
            struct SetupGuard { Session& s; bool done{}; ~SetupGuard() { if (!done && s.conditional) s.RetireOwnedScene(); } } setup{*this};
            PD::observer=nullptr;
            applicationScene=&target; assets=sceneAssets;
            if (!entities.empty()) {
                scene->OnRuntimeStop();
                for (auto entity:entities) if (auto removed=scene->DestroyEntity(entity);!removed) {
                    Fail("fixture entity retirement failed"); return false;
                }
                entities.clear();
                if (sixtyHzScene) ownedShapes.clear(); else shapes.clear();
            }
            if (conditional && episode!=0 && !sixtyHzScene) {
                sixtyHzScene.reset(new(std::nothrow) _Scene);
                if (!sixtyHzScene || sixtyHzScene->GetPhysicsStep().count()!=1.0/60.0) { Fail("fresh 60Hz fixture Scene unavailable"); RetireOwnedScene(); return false; }
            }
            scene=sixtyHzScene?sixtyHzScene.get():&target;
            auto& shapeOwners=sixtyHzScene?ownedShapes:shapes;
            inputs=episode==0?lattice:Focused(episode);
            if (conditional && episode==9) { inputs=Focused(1); inputs[1].linear={.5f,0,0}; }
            if (conditional && episode==10) { inputs=Focused(2); inputs[1].shape=2; }
            entities.resize(inputs.size());
            // EnTT's packed component view visits reverse insertion order. Author
            // in reverse so PhysicsWorld construction matches export order exactly.
            for (std::size_t n=inputs.size();n>0;--n) {
                const auto i=n-1; const auto& b=inputs[i];
                auto created=scene->CreateEntity("P19 "+std::string(EpisodeName())+" "+std::to_string(i));
                if (!created) { Fail("fixture entity creation failed"); return false; }
                auto entity=*created; entities[i]=entity;
                auto& transform=entity.GetComponent<Component::Transform3DComponent>();
                transform.Translation=b.position; transform.QuatRotation=b.orientation;
                if (b.shape!=0) {
                    Vec3f low=b.points.front(),high=low;
                    for (auto p:b.points) { low=glm::min(low,p); high=glm::max(high,p); }
                    transform.Scale=high-low;
                }
                const auto mesh=b.shape==0?assets.smoothSphereGeo:assets.boxMesh;
                const auto material=b.type==0?assets.floorMaterial:(b.shape==0?assets.sphereMaterial:assets.boxMaterial);
                if (auto assigned=resources.AssignRenderable(entity,{mesh,material});!assigned) {
                    Fail("fixture render assignment failed"); return false;
                }
                if (b.shape!=0) if (auto attached=resources.AttachPhysicsShape(entity,"Box");!attached) {
                    Fail("fixture box attachment failed"); return false;
                }
                Component::RigidBody3DComponent rb; rb.Type=static_cast<Component::BodyType>(b.type);
                rb.CollisionLayer=b.layer; rb.CollisionMask=b.mask;
                entity.AddOrReplaceComponent<Component::RigidBody3DComponent>(rb);
                Component::Fixture3DProperty property;
                property.m_Position=b.position; property.m_Orientation=b.orientation;
                property.m_LinearVelocity=b.linear; property.m_AngularVelocity=b.angular;
                property.m_InvMass=b.inverseMass; property.m_Elasticity=b.elasticity; property.m_Friction=b.friction;
                if (b.shape==0) {
                    Component::SphereFixture3DComponent fixture; fixture.Radius=b.radius; fixture.Property=property;
                    entity.AddOrReplaceComponent<Component::SphereFixture3DComponent>(fixture);
                } else if (b.shape==1) {
                    Component::BoxFixture3DComponent fixture; fixture.Property=property;
                    entity.AddOrReplaceComponent<Component::BoxFixture3DComponent>(fixture);
                } else {
                    Component::ConvexFixture3DComponent fixture; fixture.Property=property;
                    entity.AddOrReplaceComponent<Component::ConvexFixture3DComponent>(fixture);
                }
            }
            shapeOwners.reserve(inputs.size());
            if (auto started=scene->OnRuntimeStart();!started) { Fail("fixture runtime construction failed"); return false; }
            auto* system=scene->GetPhysicsSystem(); auto* world=system->GetPhysicsWorld();
            const auto& all=world->GetPhysicsBodies();
            for (auto* b:all) shapeOwners.emplace_back(b->m_Shape);
            if (all.size()!=inputs.size() || system->GetSolverIterations()!=1 || !system->IsSleepingEnabled()) {
                Fail("fixture membership or Physics policy differs"); return false;
            }
            world->SetGravity(episode>=3 && episode<=6?Vec3f(0):Vec3f(0,-12,0));
            for (unsigned i=0;i<all.size();++i) {
                auto& b=*all[i]; const auto& source=inputs[i];
                if (&b!=entities[i].GetComponent<Component::RigidBody3DComponent>().RuntimeBody || Index(&b)!=i) {
                    Fail("runtime creation order differs from frozen input"); return false;
                }
                // The historical text contains float quaternion bits. Configure
                // those exact initial fields before the first normal Scene tick.
                b.m_Position=source.position; b.m_Orientation=source.orientation;
                if (source.shape!=0) b.m_Shape->Build(source.points);
                const float resistance=conditional && episode==9?0.f:.05f;
                if (!b.m_Shape->IsValid() || !b.SetSpinResistanceLength(resistance) || !b.SetRollingResistanceLength(resistance) ||
                    b.m_LinearVelocity!=source.linear || b.m_AngularVelocity!=source.angular ||
                    b.m_InvMass!=source.inverseMass || b.m_Elasticity!=source.elasticity || b.m_Friction!=source.friction ||
                    b.GetSleepSettings()!=RigidBody3D::SleepSettings{}) {
                    Fail("runtime input interpretation differs"); return false;
                }
                const auto inertia=b.m_Shape->InertiaTensor();
                initial<<episode<<','<<i<<','<<b.GetIdentity().GetSlot()<<','<<b.GetIdentity().GetGeneration()<<','
                    <<source.shape<<','<<int(b.Type)<<','<<b.m_InvMass<<','<<b.m_Elasticity<<','<<b.m_Friction<<','
                    <<b.m_CollisionLayer<<','<<b.m_CollisionMask<<','<<source.radius;
                CsvVector(initial,b.m_Position);
                initial<<','<<b.m_Orientation.w<<','<<b.m_Orientation.x<<','<<b.m_Orientation.y<<','<<b.m_Orientation.z;
                CsvVector(initial,b.m_LinearVelocity); CsvVector(initial,b.m_AngularVelocity);
                initial<<','<<b.GetSpinResistanceLength()<<','<<b.GetRollingResistanceLength()<<','
                    <<b.GetSleepSettings().linearSpeedThreshold<<','<<b.GetSleepSettings().angularSpeedThreshold<<','
                    <<b.GetSleepSettings().inactivitySeconds<<','<<inertia[0][0]<<','<<inertia[1][1]<<','<<inertia[2][2]<<'\n';
            }
            scene->SetPaused(false);
            scene->Update(Timestep(0.0)); // Synchronize authoritative initial poses; no Physics tick.
            tick=0; sequence=0; wakeCount=sleepCount=0; lastStorage=storageGrowth=0;
            previous.clear(); previous.reserve(all.size());
            for (auto* b:all) previous.push_back(PD::Capture(*b));
            if (conditional) {
                previousEnergy=previousKinetic=0; impactResponses=0; restWindowEnergy=restMinY=restMaxY=0;
                for (auto* b:all) { const double kinetic=Kinetic(*b,PD::Capture(*b)); previousKinetic+=kinetic; previousEnergy+=kinetic+Potential(*b,PD::Capture(*b)); }
            }
            RecordBodies(*world);
            episodeDone=false;
            if (conditional && episode==0 && !queriesComplete && !RunConditionalQueries(*this)) return false;
            sink.detailed=config.observation==Observation::Observed && !LightInterval();
            sink.targeted=conditional && !LightInterval();
            sink.boundary=postPolicy && !LightInterval()?&PostBoundary:&Boundary;
            PD::observer=&sink;
            Log::GetCoreLogger()->info("PHASE19_EPISODE_BEGIN {} bodies={} ticks={}",EpisodeName(),all.size(),TickLimit());
            setup.done=!failed && bodies && initial;
            return !failed && bodies && initial;
        }
        bool FrameComplete(double elapsed)
        {
            if (conditional && sixtyHzScene && !complete && !failed) scene->Update(Timestep(elapsed));
            FlushConditionalTick();
            FlushPostAudit();
            const auto& t=scene->GetPhysicsTiming();
            frames<<episode<<','<<tick<<','<<elapsed<<','<<t.pendingSeconds<<','<<t.discardedSeconds<<','
                <<t.totalSteps<<','<<t.stepsLastUpdate<<','<<scene->GetRenderInterpolationAlpha()<<'\n';
            if (!frames) Fail("frame accounting write failed");
            if (episodeDone && !failed) {
                if (t.totalSteps!=TickLimit() || scene->SetPhysicsStep(Seconds{StepSeconds()==1.0/60.0?1.0/120.0:1.0/60.0}) ||
                    scene->GetPhysicsStep().count()!=StepSeconds()) { Fail("clock immutability/tick count failed"); RetireOwnedScene(); return false; }
                ++completedEpisodes;
                completedTicks+=tick;
                Log::GetCoreLogger()->info("PHASE19_EPISODE_COMPLETE {} ticks={}",EpisodeName(),tick);
                if (completedEpisodes==(conditional?11u:9u)) { complete=true; PD::observer=nullptr; RetireOwnedScene(); }
            }
            if (failed) RetireOwnedScene();
            if (conditional) {
                std::uint64_t total=queryOperandExportBytes;
                for (auto* stream:{&bodies,&stages,&contacts,&timing,&metrics,&events,&frames,&initial,&islandRows,&queryRows,&queryInputs,&helperRows,&cpuRows,&qualificationRows,&postEvents,&postStages,&postTicks})
                    if (stream->is_open()) { const auto n=stream->tellp(); if (n<0) Fail("conditional export position failed"); else total+=static_cast<std::uint64_t>(n); }
                if (total>8*1024*1024-4096) { overflow=true; Fail("conditional eight MiB export bound"); RetireOwnedScene(); }
            }
            return !failed;
        }
        bool FinishAfterAppShutdown(bool clean)
        {
            PD::observer=nullptr;
            FlushPostAudit();
            for (auto* out:{&bodies,&stages,&contacts,&timing,&metrics,&events,&frames,&initial,&islandRows}) {
                out->flush(); if (!*out) Fail("final evidence flush failed"); out->close();
            }
            std::ofstream out(std::filesystem::path(config.outputDirectory)/"result.json");
            for (auto* stream:{&queryRows,&queryInputs,&helperRows,&cpuRows,&qualificationRows,&postEvents,&postStages,&postTicks}) if (stream->is_open()) { stream->flush(); if (!*stream) Fail("conditional evidence flush failed"); stream->close(); }
            if (conditional) {
                std::ofstream gates(std::filesystem::path(config.outputDirectory)/"focused-gates.csv");
                gates<<"gate,name,checks,failures,examples\n";
                for (unsigned i=0;i<(postPolicy?GateNames.size():15);++i) gates<<i<<','<<GateNames[i]<<','<<gateChecks[i]<<','<<gateFailures[i]<<','<<std::min<std::uint64_t>(2,gateFailures[i])<<'\n';
                gates.flush(); if (!gates) Fail("conditional gate aggregate write failed");
            }
            const bool ok=clean && complete && !failed && completedEpisodes==(conditional?11u:9u) &&
                (!conditional || (queriesComplete && completedTicks==4980 && profiledTicks==4980 && physicsWorldNs>0 && !overflow && collisionCalls==50 && directCalls==16 && caCalls==12 && projectionCalls==4)) &&
                (!postPolicy || (postAudit->ticks==1680 && postAudit->stageCount==288 && postAudit->sliding>0 && gateChecks[15]>0 && gateChecks[16]>0 && gateChecks[20]>0));
            out<<"{\"schema\":1,\"complete\":"<<(ok?"true":"false")<<",\"episodes\":"<<completedEpisodes
                <<",\"detailed_events\":"<<eventCount<<",\"common_recorder_ns\":"<<recorderNs
                <<",\"shutdown\":"<<(clean?"true":"false")<<",\"conditional\":"<<(conditional?"true":"false")
                <<",\"candidate\":"<<(candidate?"true":"false")<<",\"ticks\":"<<completedTicks<<",\"collision_calls\":"<<collisionCalls
                <<",\"direct_gjk_calls\":"<<directCalls<<",\"ca_helper_calls\":"<<caCalls<<",\"projection_helper_calls\":"<<projectionCalls
                <<",\"overflow\":"<<(overflow?"true":"false")<<",\"profiling_enabled\":"<<(IsPhysicsProfilingEnabled()?"true":"false")
                <<",\"profiled_ticks\":"<<profiledTicks<<",\"physics_world_ns\":"<<physicsWorldNs
                <<",\"post_candidate_policy\":"<<(postPolicy?"true":"false")<<",\"post_storage_bytes\":"<<(postPolicy?sizeof(PostCandidateAudit):0)
                <<",\"post_ticks\":"<<(postAudit?postAudit->ticks:0)<<",\"post_event_records\":"<<(postAudit?postAudit->eventCount:0)
                <<",\"post_stage_records\":"<<(postAudit?postAudit->stageCount:0)<<",\"post_sliding\":"<<(postAudit?postAudit->sliding:0)
                <<",\"legacy_positive_sliding\":"<<(postAudit?postAudit->legacyPositive:0)<<",\"explained_infeasible_old\":"<<(postAudit?postAudit->explained:0)
                <<",\"physical_failures\":"<<physicalFailures<<",\"failure\":\""<<failure<<"\"}\n";
            return ok && bool(out);
        }
    };
    inline std::unique_ptr<Session> session;

    // Separate normal-scene observation. It owns evidence only, never a body,
    // world, render lease, clock, camera or Physics setting.
    // Fixed-size, value-only capture. Raw words avoid decimal round-trip ambiguity
    // and padding-dependent equality when deduplicating repeated EPA hulls.
    struct EpaCapture
    {
        static constexpr std::size_t Header=58, PointStart=Header,
            FaceStart=PointStart+PD::EpaCaptureMaxPoints*9,
            Words=FaceStart+PD::EpaCaptureMaxFaces*3, Capacity=13*11;
        using Snapshot=std::array<std::uint32_t,Words>;
        struct Attempt { unsigned tick{},iteration{},snapshot{}; };
        std::array<Snapshot,Capacity> snapshots{};
        std::array<Attempt,Capacity> attempts{};
        PD::BarycentricEvent bary{};
        unsigned snapshotCount{},attemptCount{};
        bool hasBary{},overflow{},missingBary{};
        void Capture(unsigned tick,const PD::EpaProjectionEvent& e) noexcept
        {
            if (attemptCount==Capacity) { overflow=true; return; }
            Snapshot packed{}; unsigned n=0;
            auto u=[&](auto v){packed[n++]=static_cast<std::uint32_t>(v);};
            auto f=[&](float v){u(std::bit_cast<std::uint32_t>(v));};
            auto v=[&](const Vec3f& p){f(p.x);f(p.y);f(p.z);};
            u(e.pointCount);u(e.faceCount);u(e.selectedFace);u(e.epaIterations);
            u(e.convergenceReason);u(e.failedPredicates);u(e.overflow);
            f(e.bias);f(e.originDistance);f(e.lambdaSum);f(e.projectionErrorSquared);f(e.projectionScaleSquared);
            v(e.normal);v(e.lambdas);v(e.projectedPoint);v(e.expectedProjection);
            v(e.lastSupport.xyz);v(e.lastSupport.ptA);v(e.lastSupport.ptB);v(e.lastSupportNormal);
            f(e.lastOriginDistance);f(e.lastExpansionDistance);f(e.lastConvergenceTolerance);u(e.lastSelectedFace);
            const bool matched=hasBary && bary.iteration==e.iteration;
            missingBary=missingBary || !matched;
            u(bary.exit);u(bary.validFields);f(bary.normalLengthSquared);f(bary.dominantArea);u(bary.dominantAxis);
            v(bary.projectedOrigin);v(bary.subareas);v(bary.rawWeights);v(bary.returnedWeights);u(matched);
            if (n!=Header || e.overflow || e.pointCount>PD::EpaCaptureMaxPoints ||
                e.faceCount>PD::EpaCaptureMaxFaces || e.points.size()!=e.pointCount || e.faces.size()!=e.faceCount) overflow=true;
            n=PointStart;
            for (const auto& p:e.points.first(std::min(e.points.size(),PD::EpaCaptureMaxPoints))) {v(p.xyz);v(p.ptA);v(p.ptB);}
            n=FaceStart;
            for (const auto& face:e.faces.first(std::min(e.faces.size(),PD::EpaCaptureMaxFaces))) {u(face.a);u(face.b);u(face.c);}
            unsigned index=0;
            while (index<snapshotCount && snapshots[index]!=packed) ++index;
            if (index==snapshotCount) {
                if (snapshotCount==Capacity) {overflow=true;return;}
                snapshots[snapshotCount++]=packed;
            }
            attempts[attemptCount++]={tick,e.iteration,index};
        }
        bool Write(const std::filesystem::path& dir) const
        {
            std::ofstream rows(dir/"epa-projections.csv"), points(dir/"epa-points.csv"),
                faces(dir/"epa-faces.csv"), calls(dir/"epa-attempts.csv");
            rows<<"snapshot,point_count,face_count,selected_face,epa_iterations,convergence_reason,failed_predicates,overflow,bias_bits,origin_distance_bits,lambda_sum_bits,projection_error_squared_bits,projection_scale_squared_bits";
            for (const char* name:{"normal","lambdas","projected","expected","last_support_xyz","last_support_a","last_support_b","last_support_normal"})
                for (char axis:std::string("xyz")) rows<<','<<name<<'_'<<axis<<"_bits";
            rows<<",last_origin_distance_bits,last_expansion_distance_bits,last_tolerance_bits,last_selected_face,bary_exit,bary_valid_fields,bary_normal_squared_bits,bary_area_bits,bary_axis";
            for (const char* name:{"bary_projected","bary_subareas","bary_raw_weights","bary_returned_weights"})
                for (char axis:std::string("xyz")) rows<<','<<name<<'_'<<axis<<"_bits";
            rows<<",bary_matched\n";
            points<<"snapshot,point";
            for (const char* name:{"xyz","a","b"}) for (char axis:std::string("xyz")) points<<','<<name<<'_'<<axis<<"_bits";
            points<<'\n';faces<<"snapshot,face,a,b,c\n";calls<<"attempt,tick,iteration,snapshot\n";
            for (unsigned i=0;i<snapshotCount;++i) {
                const auto& p=snapshots[i];rows<<i;
                for (unsigned j=0;j<Header;++j) rows<<','<<p[j];rows<<'\n';
                for (unsigned j=0;j<std::min<std::size_t>(p[0],PD::EpaCaptureMaxPoints);++j) {
                    points<<i<<','<<j;for(unsigned k=0;k<9;++k) points<<','<<p[PointStart+j*9+k];points<<'\n';
                }
                for (unsigned j=0;j<std::min<std::size_t>(p[1],PD::EpaCaptureMaxFaces);++j)
                    faces<<i<<','<<j<<','<<p[FaceStart+j*3]<<','<<p[FaceStart+j*3+1]<<','<<p[FaceStart+j*3+2]<<'\n';
            }
            for(unsigned i=0;i<attemptCount;++i) {const auto& a=attempts[i];calls<<i<<','<<a.tick<<','<<a.iteration<<','<<a.snapshot<<'\n';}
            rows.flush();points.flush();faces.flush();calls.flush();
            return bool(rows)&&bool(points)&&bool(faces)&&bool(calls);
        }
    };

    // Phase20's accepted query replay. Copies are never registered in a world.
    struct QueryReplay
    {
        static constexpr const char* InputHash="0285e8dd4303d7aee1c4ebc41a504734faccf4b1b2309faf3bfb3a854b719ee6";
        struct Input { unsigned tick; std::array<std::uint32_t,13> bits; };
        inline static constexpr std::array<Input,13> Inputs{{
            {566, {0x3f441671u, 0x3fb8dd29u, 0x3fc6a02du, 0xbeb6d2e1u, 0x3e07a4aeu, 0xbdab3477u, 0xbf6bbcacu, 0xbeaff5b2u, 0xbf83f758u, 0x3dab1049u, 0x3dc5218cu, 0xbe179a69u, 0x3eaea752u}},
            {579, {0x3f2fa723u, 0x3fbd63fcu, 0x3fc936deu, 0xbea55860u, 0x3e161b9fu, 0xbd805b34u, 0xbf6ed237u, 0xbed2aeddu, 0xbe6e244cu, 0x3dd389efu, 0x3dea306au, 0xbe1745cdu, 0x3ed1661bu}},
            {580, {0x3f2dd948u, 0x3fbd5700u, 0x3fc970bdu, 0xbea3c8c7u, 0x3e172e7fu, 0xbd793810u, 0xbf6f141eu, 0xbed87edau, 0xbe723ce6u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {581, {0x3f2c0b6du, 0x3fbcd5ceu, 0x3fc9aa9cu, 0xbea23890u, 0x3e1840cdu, 0xbd71b8c6u, 0xbf6f551eu, 0xbed87edau, 0xbedf84dau, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {582, {0x3f2a3d92u, 0x3fbbe762u, 0x3fc9e47bu, 0xbea0a7bcu, 0x3e195288u, 0xbd6a3894u, 0xbf6f9537u, 0xbed87edau, 0xbf22f5a0u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {583, {0x3f286fb7u, 0x3fba8bbcu, 0x3fca1e5au, 0xbe9f164cu, 0x3e1a63afu, 0xbd62b77eu, 0xbf6fd468u, 0xbed87edau, 0xbf5628d4u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {584, {0x3f26a1dcu, 0x3fb8c2dcu, 0x3fca5839u, 0xbe9d8442u, 0x3e1b7441u, 0xbd5b358cu, 0xbf7012b1u, 0xbed87edau, 0xbf84ae04u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {585, {0x3f24d401u, 0x3fb68cc2u, 0x3fca9218u, 0xbe9bf1a0u, 0x3e1c843cu, 0xbd53b2c7u, 0xbf705013u, 0xbed87edau, 0xbf9e479eu, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {586, {0x3f230626u, 0x3fb3e96eu, 0x3fcacbf7u, 0xbe9a5e67u, 0x3e1d93a0u, 0xbd4c2f35u, 0xbf708c8bu, 0xbed87edau, 0xbfb7e138u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {587, {0x3f21384bu, 0x3fb0d8e0u, 0x3fcb05d6u, 0xbe98ca9au, 0x3e1ea26du, 0xbd44aae0u, 0xbf70c81cu, 0xbed87edau, 0xbfd17ad2u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {588, {0x3f1f6a70u, 0x3fad5b18u, 0x3fcb3fb5u, 0xbe973639u, 0x3e1fb0a0u, 0xbd3d25cbu, 0xbf7102c3u, 0xbed87edau, 0xbfeb146cu, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {589, {0x3f1d9c95u, 0x3fa97016u, 0x3fcb7994u, 0xbe95a146u, 0x3e20be38u, 0xbd359ffeu, 0xbf713c81u, 0xbed87edau, 0xc0025703u, 0x3dd904c5u, 0x3df1e0cbu, 0xbe17356au, 0x3ed735c9u}},
            {590, {0x3f1bcc30u, 0x3fae9029u, 0x3fcbb6d8u, 0xbe9406e5u, 0x3e21cf59u, 0xbd2e44a5u, 0xbf7175c9u, 0xbed9b3dau, 0xbe4dd7beu, 0x3de5bf0bu, 0x3de5dc8cu, 0xbe173a31u, 0x3ed9202bu}},
        }};
        struct Decision { unsigned tick{}; PD::QueryEvent event{}; };
        struct Answer {
            unsigned tick{}, mode{}; bool hit{}, pointers{}, unchanged{};
            std::uint64_t elapsedNs{}, observerNs{};
            std::array<std::uint32_t,19> bits{};
        };
        static constexpr std::size_t DecisionCapacity=4096;
        std::array<Decision,DecisionCapacity> decisions{};
        std::array<Answer,26> answers{};
        PD::QueryObserver sink{};
        std::unique_ptr<EpaCapture> epa;
        bool conditional{};
        inline static constexpr std::array<unsigned,13> ConditionalOrder{2,11,0,1,3,4,5,6,7,8,9,10,12};
        std::size_t used{};
        unsigned activeTick{}, calls{}, pairChecks{};
        bool overflow{}, attempted{}, pairsMatch{true}, controlsMatch{true}, answersMatch{true}, liveUnchanged{};
        std::uint64_t sceneTicks{}, observerNs{};

        static std::array<std::uint32_t,13> PoseBits(const PD::BodyState& b) noexcept
        {
            const std::array<float,13> values{b.position.x,b.position.y,b.position.z,
                b.orientation.w,b.orientation.x,b.orientation.y,b.orientation.z,
                b.linear.x,b.linear.y,b.linear.z,b.angular.x,b.angular.y,b.angular.z};
            std::array<std::uint32_t,13> out{};
            for (unsigned i=0;i<out.size();++i) out[i]=std::bit_cast<std::uint32_t>(values[i]);
            return out;
        }
        static bool SameBody(const PD::BodyState& a,const PD::BodyState& b) noexcept
        {
            return PoseBits(a)==PoseBits(b) && a.sleeping==b.sleeping &&
                std::bit_cast<std::uint64_t>(a.inactive)==std::bit_cast<std::uint64_t>(b.inactive);
        }
        static std::array<std::uint32_t,19> ContactBits(const contact_t& c) noexcept
        {
            std::array<std::uint32_t,19> out{}; unsigned n=0;
            for (const auto& v:{c.ptOnA_WorldSpace,c.ptOnB_WorldSpace,c.ptOnA_LocalSpace,c.ptOnB_LocalSpace,c.normal})
                for (int i=0;i<3;++i) out[n++]=std::bit_cast<std::uint32_t>(v[i]);
            out[n++]=std::bit_cast<std::uint32_t>(c.separationDistance);
            out[n++]=std::bit_cast<std::uint32_t>(c.timeOfImpact);
            out[n++]=c.featureA; out[n++]=c.featureB; return out;
        }
        static void Load(RigidBody3D& b,const Input& input) noexcept
        {
            std::array<float,13> f{};
            for (unsigned i=0;i<f.size();++i) f[i]=std::bit_cast<float>(input.bits[i]);
            b.m_Position={f[0],f[1],f[2]}; b.m_Orientation=Quat(f[3],f[4],f[5],f[6]);
            b.m_LinearVelocity={f[7],f[8],f[9]}; b.m_AngularVelocity={f[10],f[11],f[12]};
        }
        static bool ValidRequest(const std::filesystem::path& path,bool conditional=false)
        {
            std::ifstream in(path); std::string magic,hash,end;
            unsigned count{},queries{},pairs{},ticks{},wall{};
            if (!(in>>magic>>hash>>count>>queries>>pairs>>ticks>>wall) ||
                magic!=(conditional?"P20_CONDITIONAL_DIAGNOSIS_V1":"P20_QUERY_REPLAY_V1") || hash!=InputHash || count!=13 || queries!=26 || pairs!=13 || ticks!=0 || wall!=60) return false;
            for (unsigned i=0;i<Inputs.size();++i) {
                const auto& expected=Inputs[conditional?ConditionalOrder[i]:i];
                Input row{}; in>>std::dec>>row.tick;
                for (auto& b:row.bits) in>>std::hex>>b;
                if (!in || row.tick!=expected.tick || row.bits!=expected.bits) return false;
            }
            if (!(in>>end) || end!="END") return false;
            in>>std::ws; return in.eof();
        }
        static void Observe(void* context,const PD::QueryEvent& event) noexcept
        {
            auto& s=*static_cast<QueryReplay*>(context);
            if (s.epa && event.kind==PD::QueryKind::Begin) s.epa->hasBary=false;
            if (s.used==s.decisions.size()) { s.overflow=true; return; }
            s.decisions[s.used++]={s.activeTick,event};
            // Query-local addresses expire after the call. Final answer pointer
            // roles are checked separately; retained decisions contain values only.
            s.decisions[s.used-1].event.contact.m_BodyA=nullptr;
            s.decisions[s.used-1].event.contact.m_BodyB=nullptr;
        }
        static void Barycentric(void* context,const PD::BarycentricEvent& event) noexcept
        {auto& s=*static_cast<QueryReplay*>(context);if(s.epa){s.epa->bary=event;s.epa->hasBary=true;}}
        static void Projection(void* context,const PD::EpaProjectionEvent& event) noexcept
        {auto& s=*static_cast<QueryReplay*>(context);if(s.epa)s.epa->Capture(s.activeTick,event);}
        static void State(std::ostream& out,const PD::BodyState& s)
        {
            for (auto b:PoseBits(s)) out<<','<<b;
            out<<','<<s.sleeping<<','<<std::bit_cast<std::uint64_t>(s.inactive);
        }
        bool Write(const std::filesystem::path& dir)
        {
            std::ofstream queries(dir/"replay-queries.csv"), events(dir/"replay-decisions.csv");
            queries<<"tick,mode,hit,pointer_roles_ok,clones_unchanged,elapsed_ns,observer_ns";
            for (unsigned i=0;i<19;++i) queries<<",contact_bits_"<<i;
            queries<<'\n';
            for (unsigned i=0;i<calls;++i) {
                const auto& a=answers[i]; queries<<a.tick<<','<<a.mode<<','<<a.hit<<','<<a.pointers<<','<<a.unchanged<<','<<a.elapsedNs<<','<<a.observerNs;
                for (auto b:a.bits) queries<<','<<b; queries<<'\n';
            }
            events<<"sequence,tick,kind,iteration,exit,status,gjk_termination,gjk_iterations,epa_termination,epa_iterations,hit,remaining,toi,projected_speed,angular_a,angular_b,speed,time_to_go";
            for (const char* v:{"witness_a","witness_b","direction","relative_velocity"}) for (char axis:std::string("xyz")) events<<','<<v<<'_'<<axis;
            for (const char* name:{"a","b"}) {
                for (unsigned i=0;i<13;++i) events<<','<<name<<"_state_bits_"<<i;
                events<<','<<name<<"_sleeping,"<<name<<"_inactive_bits";
            }
            for (unsigned i=0;i<19;++i) events<<",contact_bits_"<<i;
            events<<'\n'<<std::setprecision(17);
            for (std::size_t i=0;i<used;++i) {
                const auto& r=decisions[i]; const auto& q=r.event;
                events<<i<<','<<r.tick<<','<<int(q.kind)<<','<<q.iteration<<','<<int(q.exit)<<','<<q.contactStatus
                    <<','<<int(q.diagnostics.termination)<<','<<q.diagnostics.iterations<<','<<int(q.diagnostics.epa.termination)
                    <<','<<q.diagnostics.epa.iterations<<','<<q.hit<<','<<q.remaining<<','<<q.toi<<','<<q.projectedSpeed
                    <<','<<q.angularA<<','<<q.angularB<<','<<q.speed<<','<<q.timeToGo;
                for (const auto& v:{q.witnessA,q.witnessB,q.direction,q.relativeVelocity}) CsvVector(events,v);
                State(events,q.a); State(events,q.b);
                for (auto b:ContactBits(q.contact)) events<<','<<b; events<<'\n';
            }
            queries.flush(); events.flush(); return bool(queries) && bool(events) && (!epa || epa->Write(dir));
        }
    };
    static_assert(sizeof(QueryReplay)<=4*1024*1024,"accepted replay storage bound");
    static_assert(sizeof(QueryReplay)+sizeof(EpaCapture)<=4*1024*1024,"accepted combined EPA/replay storage bound");

    // Fixed Phase20 matrix. Query copies are never installed in a PhysicsWorld.
    // The source pins every construction; exports bind all interpreted float bits
    // before a query. No reference contact is fed back into collision execution.
    struct ConditionalQueryTrace
    {
        EpaCapture* epa{}; unsigned caseIndex{},events{};
        PD::QueryEvent contact{},distance{},exit{};
        static void Observe(void* context,const PD::QueryEvent& e) noexcept
        {
            auto& s=*static_cast<ConditionalQueryTrace*>(context); ++s.events;
            if (e.kind==PD::QueryKind::Begin && s.epa) s.epa->hasBary=false;
            if (e.kind==PD::QueryKind::ContactSearch) s.contact=e;
            if (e.kind==PD::QueryKind::ClosestPoints) s.distance=e;
            if (e.kind==PD::QueryKind::Exit) s.exit=e;
        }
        static void Barycentric(void* context,const PD::BarycentricEvent& e) noexcept
        { auto& s=*static_cast<ConditionalQueryTrace*>(context); if(s.epa){s.epa->bary=e;s.epa->hasBary=true;} }
        static void Projection(void* context,const PD::EpaProjectionEvent& e) noexcept
        { auto& s=*static_cast<ConditionalQueryTrace*>(context); if(s.epa)s.epa->Capture(s.caseIndex,e); }
    };
    class ConditionalInvalidSupport final:public ShapeSphere
    {
    public:
        mutable unsigned calls{}; unsigned failAt{};
        Vec3f Support(const Vec3f& direction,const Vec3f& position,const Quat& orientation,float bias) const override
        { if (++calls==failAt) return Vec3f(std::numeric_limits<float>::quiet_NaN()); return ShapeSphere::Support(direction,position,orientation,bias); }
    };
    inline void ConditionalJsonVector(std::ostream& out,const Vec3f& v)
    { out<<'['<<v.x<<','<<v.y<<','<<v.z<<']'; }
    inline bool RunConditionalQueries(Session& s)
    {
        if (!s.conditional || s.queriesComplete || PD::queryObserver || s.scene->GetPhysicsTiming().totalSteps!=0) { s.Fail("conditional query entry invariant"); return false; }
        if (!IsPhysicsProfilingEnabled()) { s.Fail("conditional original Physics timers disabled"); return false; }
        const auto& world=s.scene->GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies();
        if (world.size()!=185) { s.Fail("conditional original world membership"); return false; }
        const auto unitPoints=Box({},Vec3f(2),true).points;
        auto boxResult=ShapeBox::Create(unitPoints);
        if (!boxResult) { s.Fail("conditional unit Box construction"); return false; }
        ShapeBox unitBox=std::move(*boxResult);
        ShapeConvex unitConvex(unitPoints),tetra(std::vector<Vec3f>{{-1,-1,-1},{1,-1,-1},{0,-1,1},{0,1,0}});
        if (!unitConvex.IsValid() || !tetra.IsValid()) { s.Fail("conditional Convex construction"); return false; }
        const auto f=[](std::uint32_t bits){return std::bit_cast<float>(bits);};
        const Quat rotation(f(0x3f7ba1b6),f(0x3d495ddc),f(0x3dc95ddc),f(0x3e170665));
        const Quat tilt(f(0x3f7dcf55),0,0,f(0x3e05a8a9));
        if (rotation!=glm::angleAxis(.37f,Math::NormalizeOr(Vec3f(1,2,3))) || tilt!=glm::angleAxis(glm::radians(15.f),Vec3f(0,0,1))) {
            s.Fail("conditional pinned GLM rotation differs"); return false;
        }
        const Vec3f axis=rotation*Vec3f(1,0,0);
        std::array<PD::BodyState,185> liveBefore{};
        for (unsigned i=0;i<world.size();++i) liveBefore[i]=PD::Capture(*world[i]);
        auto capture=std::unique_ptr<EpaCapture>(new(std::nothrow) EpaCapture);
        if (!capture) { s.Fail("conditional bounded query recorder allocation"); return false; }
        ConditionalQueryTrace trace; PD::QueryObserver observer{}; observer.context=&trace; observer.event=&ConditionalQueryTrace::Observe;
        observer.barycentric=&ConditionalQueryTrace::Barycentric; observer.epaProjection=&ConditionalQueryTrace::Projection;
        const auto bodyInput=[&](unsigned id,unsigned role,const RigidBody3D& body) {
            auto& out=s.queryInputs; out<<id<<','<<role<<','<<int(body.m_Shape->GetShapeType())<<','<<int(body.Type);
            for (float x:{body.m_InvMass,body.m_Elasticity,body.m_Friction}) out<<','<<std::bit_cast<std::uint32_t>(x);
            out<<','<<body.m_CollisionLayer<<','<<body.m_CollisionMask;
            const auto& sleep=body.GetSleepSettings();
            for(float x:{body.GetSpinResistanceLength(),body.GetRollingResistanceLength(),sleep.linearSpeedThreshold,sleep.angularSpeedThreshold}) out<<','<<std::bit_cast<std::uint32_t>(x);
            out<<','<<std::bit_cast<std::uint64_t>(sleep.inactivitySeconds)<<','<<body.IsSleeping()<<','<<std::bit_cast<std::uint64_t>(body.GetInactiveSeconds());
            out<<','<<std::bit_cast<std::uint32_t>(body.m_Shape->GetShapeType()==ShapeType::Sphere?static_cast<const ShapeSphere*>(body.m_Shape)->GetRadius():0.f);
            for (auto x:QueryReplay::PoseBits(PD::Capture(body))) out<<','<<x;
            const auto bounds=body.m_Shape->GetBounds();
            for (const auto& v:{body.m_Shape->GetCenterOfMass(),bounds.mins,bounds.maxs}) for (int k=0;k<3;++k) out<<','<<std::bit_cast<std::uint32_t>(v[k]);
            const auto inertia=body.m_Shape->InertiaTensor();
            for (int c=0;c<3;++c) for(int r=0;r<3;++r) out<<','<<std::bit_cast<std::uint32_t>(inertia[c][r]); out<<'\n';
        };
        const auto shapeContains=[&](const RigidBody3D& body,const Vec3f& witness,float toi) {
            constexpr double tolerance=.0025;
            const auto position=body.m_Position+body.GetLinearVelocity()*toi;
            const glm::dvec3 p=glm::transpose(glm::dmat3(glm::mat3_cast(body.m_Orientation)))*(glm::dvec3(witness)-glm::dvec3(position));
            if (body.m_Shape->GetShapeType()==ShapeType::Sphere) return std::abs(glm::length(p)-static_cast<const ShapeSphere*>(body.m_Shape)->GetRadius())<=tolerance;
            if (body.m_Shape==&tetra) {
                const std::array<glm::dvec3,4> vertices{{{-1,-1,-1},{1,-1,-1},{0,-1,1},{0,1,0}}};
                double nearest=std::numeric_limits<double>::max();
                for (int omitted=0;omitted<4;++omitted) {
                    std::array<int,3> ids{};int n=0;for(int i=0;i<4;++i)if(i!=omitted)ids[n++]=i;
                    const auto a=vertices[ids[0]];auto normal=glm::normalize(glm::cross(vertices[ids[1]]-a,vertices[ids[2]]-a));
                    if(glm::dot(normal,vertices[omitted]-a)>0)normal=-normal;
                    const double excess=glm::dot(normal,p-a); if(excess>tolerance)return false;nearest=std::min(nearest,std::abs(excess));
                }
                return nearest<=tolerance;
            }
            const auto bounds=body.m_Shape->GetBounds(); double nearest=std::numeric_limits<double>::max();
            for (int k=0;k<3;++k) {
                if (p[k]<bounds.mins[k]-tolerance || p[k]>bounds.maxs[k]+tolerance) return false;
                nearest=std::min({nearest,std::abs(p[k]-bounds.mins[k]),std::abs(p[k]-bounds.maxs[k])});
            }
            return nearest<=tolerance;
        };
        for (unsigned id=0;id<25 && !s.failed;++id) {
            RigidBody3D a=*world[4],b=*world[5]; Vec3f expectedNormal(0,-1,0); double expectedDepth{}; float horizon=1.f/60.f;
            bool separated=false,sweep=id>=21;
            if (id<17) {
                QueryReplay::Load(b,QueryReplay::Inputs[id<13?id:2]);
                expectedDepth=1.5-b.m_Position.y;
                if (id==13) { std::swap(a,b);expectedNormal=-expectedNormal; }
                if (id==14) { a.m_Position+=Vec3f(8,4,-8);b.m_Position+=Vec3f(8,4,-8); }
                if (id==15) {
                    for (auto* body:{&a,&b}) {
                        body->m_Position=tilt*body->m_Position;body->m_Orientation=tilt*body->m_Orientation;
                        body->m_LinearVelocity=tilt*body->m_LinearVelocity;body->m_AngularVelocity=tilt*body->m_AngularVelocity;
                    }
                    expectedNormal=tilt*expectedNormal;
                }
                if (id==16) { a.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic,1);a.m_LinearVelocity={.25f,0,0};a.m_AngularVelocity={0,.5f,0}; }
            } else {
                a=RigidBody3D{};b=RigidBody3D{};a.m_Shape=&unitBox;b.m_Shape=&unitBox;
                a.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic,1);b.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic,1);
                a.m_Elasticity=b.m_Elasticity=a.m_Friction=b.m_Friction=.5f;
                a.m_Orientation=b.m_Orientation=rotation;expectedNormal=-axis;
                b.m_Position=rotation*Vec3f(1.2f,.3f,.2f);expectedDepth=.8;
                if (id==18) a.m_Shape=b.m_Shape=&unitConvex;
                if (id==19) { a=*world[4];b.m_Shape=&tetra;b.m_Orientation=Quat(1,0,0,0);b.m_Position={0,1.25f,0};expectedNormal={0,-1,0};expectedDepth=.25; }
                if (id==20) { a.m_Shape=&unitConvex;b.m_Position=rotation*Vec3f(3,.3f,.2f);separated=true;expectedDepth=-1; }
                if (sweep) {
                    a.m_Shape=id>=23?static_cast<PhysicalShape*>(&unitConvex):static_cast<PhysicalShape*>(&unitBox);
                    a.m_Position=Vec3f(0);b.m_Position=4.f*axis;a.m_LinearVelocity=3.f*axis;b.m_LinearVelocity=-axis;
                    b.SetBodyTypeAndInverseMass(Component::BodyType::Kinematic,0);horizon=.6f;expectedDepth=0;
                    if (id==22 || id==24) {std::swap(a,b);expectedNormal=-expectedNormal;}
                }
            }
            for (auto* body:{&a,&b}) { body->SetSpinResistanceLength(0); body->SetRollingResistanceLength(0); }
            bodyInput(id,0,a);bodyInput(id,1,b);
            std::array<std::uint32_t,19> offBits{};bool offHit{};
            capture->snapshotCount=capture->attemptCount=0;capture->overflow=capture->missingBary=capture->hasBary=false;
            for (unsigned mode=0;mode<2 && !s.failed;++mode) {
                RigidBody3D qa=a,qb=b; contact_t c{}; const auto beforeA=PD::Capture(qa),beforeB=PD::Capture(qb);
                trace={};trace.caseIndex=id;trace.epa=capture.get();observer.iteration=0;observer.observationNs=0;
                PD::queryObserver=mode?&observer:nullptr;const auto start=PD::Nanoseconds();
                const bool hit=Collision::Intersect(&qa,&qb,horizon,c);const auto elapsed=PD::Nanoseconds()-start;PD::queryObserver=nullptr;++s.collisionCalls;
                const auto contactBits=QueryReplay::ContactBits(c);
                const bool pointers=c.m_BodyA==&qa && c.m_BodyB==&qb;
                const bool unchanged=QueryReplay::SameBody(beforeA,PD::Capture(qa)) && QueryReplay::SameBody(beforeB,PD::Capture(qb));
                bool geometry=separated?!hit:hit;
                if (hit) {
                    geometry=geometry && Math::IsFinite(c.normal) && std::abs(glm::length(glm::dvec3(c.normal))-1)<=1e-4;
                    const double cosine=glm::dot(glm::normalize(glm::dvec3(c.normal)),glm::normalize(glm::dvec3(expectedNormal)));
                    geometry=geometry && (sweep?glm::length(glm::dvec3(c.normal-expectedNormal))<=.002:std::acos(std::clamp(cosine,-1.,1.))<=(id<17?.001:.002));
                    geometry=geometry && (sweep?std::abs(c.timeOfImpact-.5f)<=.002f:c.timeOfImpact==0);
                    if (!sweep) geometry=geometry && std::abs(-double(c.separationDistance)-expectedDepth)<=.0045;
                    for (unsigned role=0;role<2;++role) {
                        const auto& body=role?qb:qa;const auto local=role?c.ptOnB_LocalSpace:c.ptOnA_LocalSpace;const auto witness=role?c.ptOnB_WorldSpace:c.ptOnA_WorldSpace;
                        const auto reconstructed=body.m_Position+body.GetLinearVelocity()*c.timeOfImpact+body.m_Orientation*(local+body.m_Shape->GetCenterOfMass());
                        geometry=geometry && Math::IsFinite(witness) && glm::length(glm::dvec3(reconstructed-witness))<=(sweep?2e-5:1e-4) && shapeContains(body,witness,c.timeOfImpact);
                    }
                }
                s.queryRows<<id<<','<<mode<<','<<hit<<','<<geometry<<','<<pointers<<','<<unchanged<<','<<elapsed<<','<<observer.observationNs;
                for (auto word:contactBits)s.queryRows<<','<<word;s.queryRows<<'\n';
                if (capture->overflow) s.overflow=true;
                if (!pointers || !unchanged || capture->overflow || capture->missingBary) s.Fail("conditional query identity/capture integrity");
                if (mode) { if (offHit!=hit || offBits!=contactBits) s.Fail("conditional off/on query output differs"); }
                else {offHit=hit;offBits=contactBits;}
                if (id==0 || id==11) {
                    constexpr std::array<std::uint32_t,19> c566{1061446240u,1056998163u,1069971920u,1061446261u,1055026910u,1069971924u,1061446240u,1056998163u,1069971920u,3206750400u,1060850488u,3196072454u,934662938u,3212836864u,923437173u,3178543680u,0u,0u,0u};
                    constexpr std::array<std::uint32_t,19> c589{1058937088u,1056998162u,1070283552u,1058937042u,1050983206u,1070283550u,1058937088u,1056998162u,1070283552u,3205099175u,1062431328u,3191226898u,3078686327u,3212836864u,3048440686u,3191379452u,0u,0u,0u};
                    if (!hit || contactBits!=(id==0?c566:c589)) s.Fail("conditional successful historical contact bits changed");
                }
                if (!s.candidate && id<13 && id!=0 && id!=11 &&
                    (hit || !std::all_of(contactBits.begin(),contactBits.end(),[](auto word){return word==0;})))
                    s.Fail("conditional frozen failed historical output changed");
                if (!geometry) { ++s.physicalFailures;if (s.candidate) s.Fail("conditional required query geometry"); }
            }
            if (!s.candidate && id<13 && capture->attemptCount!=((id==0 || id==11)?1u:11u))
                s.Fail("conditional frozen historical EPA attempt count changed");
            if (capture->attemptCount) {
                const auto dir=std::filesystem::path(s.config.outputDirectory)/("query-epa-"+std::to_string(id));std::error_code ec;
                if (!std::filesystem::create_directory(dir,ec) || ec || !capture->Write(dir)) s.Fail("conditional query operands export");
                for (const char* file:{"epa-projections.csv","epa-points.csv","epa-faces.csv","epa-attempts.csv"}) {
                    const auto bytes=std::filesystem::file_size(dir/file,ec);if(ec)s.Fail("conditional operand export size unavailable");else s.queryOperandExportBytes+=bytes;
                }
            }
        }
        // Exactly eight producer-contract cases, each disconnected/connected.
        ConditionalInvalidSupport bad;
        std::array<std::string,8> directSignatures{};
        for (unsigned id=0;id<8 && !s.failed;++id) for (unsigned mode=0;mode<2 && !s.failed;++mode) {
            RigidBody3D a,b;a.m_Shape=id>=6?static_cast<PhysicalShape*>(&bad):static_cast<PhysicalShape*>(&unitBox);b.m_Shape=&unitBox;
            a.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic,1);b.SetBodyTypeAndInverseMass(Component::BodyType::Dynamic,1);b.m_Position={1.2f,.3f,.2f};
            bad.calls=0;bad.failAt=id>=6?id-5:0;Vec3f pa(99),pb(99);GjkDiagnostics diagnostics;GjkContactStatus status=GjkContactStatus::Failed;
            const auto beforeA=PD::Capture(a),beforeB=PD::Capture(b);
            trace={};observer.iteration=0;PD::queryObserver=mode?&observer:nullptr;
            if (id<2) status=GJK_GetContact(&a,&b,.001f,pa,pb,&diagnostics,id);
            else if(id<4) GJK_ClosestPoints(&a,&b,pa,pb,&diagnostics,id-2);
            else status=GJK_GetContact(&a,&b,.001f,pa,pb,&diagnostics,GjkMaxIterations,id<6?id-4:EpaMaxIterations);
            PD::queryObserver=nullptr;++s.directCalls;
            const bool reason=id<4?diagnostics.termination==GjkTermination::IterationLimit && diagnostics.iterations==id%2:
                id<6?diagnostics.termination==GjkTermination::ContactExpansionFailed && diagnostics.epa.termination==EpaTermination::IterationLimit && diagnostics.epa.iterations==id-4:
                diagnostics.termination==GjkTermination::InvalidSupport && diagnostics.iterations==id-6;
            const bool ok=reason && status==GjkContactStatus::Failed && pa==Vec3f(0) && pb==Vec3f(0) && diagnostics.iterations<=GjkMaxIterations && diagnostics.epa.iterations<=EpaMaxIterations &&
                QueryReplay::SameBody(beforeA,PD::Capture(a)) && QueryReplay::SameBody(beforeB,PD::Capture(b));
            std::ostringstream signature;signature<<int(status)<<','<<int(diagnostics.termination)<<','<<diagnostics.iterations<<','<<int(diagnostics.epa.termination)<<','<<diagnostics.epa.iterations;
            for (auto v:{pa,pb}) for(int k=0;k<3;++k) signature<<','<<std::bit_cast<std::uint32_t>(v[k]);
            if (mode) {if(signature.str()!=directSignatures[id])s.Fail("conditional direct GJK off/on mismatch");}else directSignatures[id]=signature.str();
            s.helperRows<<"{\"kind\":\"gjk\",\"case\":"<<id<<",\"mode\":"<<mode<<",\"pass\":"<<(ok?"true":"false")<<",\"status\":"<<int(status)<<",\"termination\":"<<int(diagnostics.termination)<<",\"iterations\":"<<diagnostics.iterations<<",\"epa_termination\":"<<int(diagnostics.epa.termination)<<",\"epa_iterations\":"<<diagnostics.epa.iterations<<",\"a\":";
            ConditionalJsonVector(s.helperRows,pa);s.helperRows<<",\"b\":";ConditionalJsonVector(s.helperRows,pb);s.helperRows<<"}\n";
            if(!ok)s.Fail("conditional direct GJK producer contract");
        }
        std::array<std::string,6> caSignatures{};
        for (unsigned id=0;id<6 && !s.failed;++id) for (unsigned mode=0;mode<2 && !s.failed;++mode) {
            RigidBody3D a=*world[4],b=*world[5];QueryReplay::Load(b,QueryReplay::Inputs[2]);
            for (auto* body:{&a,&b}) { body->SetSpinResistanceLength(0); body->SetRollingResistanceLength(0); }
            const auto ba=PD::Capture(a),bb=PD::Capture(b);
            trace={};PD::queryObserver=mode?&observer:nullptr;const auto result=PD::EvaluateCaContract(id,&a,&b);PD::queryObserver=nullptr;++s.caCalls;
            constexpr unsigned beforeAdvances[]{1,2,1,1,1,0},afterAdvances[]{0,1,0,0,0,0};
            constexpr unsigned queryCounts[]{1,2,1,1,1,0};constexpr int outcomes[]{2,2,2,2,1,-1},reasons[]{7,7,8,10,0,0},beforeExits[]{0,0,0,0,0,6},afterExits[]{7,7,8,10,10,6};
            constexpr float beforeRemaining[]{.75f,.5f,1,1,1,0},afterRemaining[]{1,.75f,1,1,1,0},beforeToi[]{.25f,.5f,0,0,1,0},afterToi[]{0,.25f,0,0,1,0};
            constexpr GjkTermination rawReasons[]{GjkTermination::ContactExpansionFailed,GjkTermination::ContactExpansionFailed,
                GjkTermination::InvalidSupport,GjkTermination::DuplicateSupport,GjkTermination::DuplicateSupport,GjkTermination::InvalidInput};
            const bool raw=result.diagnostics.termination==rawReasons[id] && result.diagnostics.iterations==0 &&
                result.diagnostics.epa.termination==(id<2?EpaTermination::InvalidProjection:EpaTermination::NotRun) && result.diagnostics.epa.iterations==0;
            const bool intact=QueryReplay::SameBody(ba,PD::Capture(a)) && QueryReplay::SameBody(bb,PD::Capture(b));
            const auto words=QueryReplay::ContactBits(result.contact);
            const bool cleared=std::all_of(words.begin(),words.end(),[](auto word){return word==0;});
            const bool pointers=result.contact.m_BodyA==&a && result.contact.m_BodyB==&b;
            const bool unadvanced=QueryReplay::SameBody(ba,result.predictedA) && QueryReplay::SameBody(bb,result.predictedB);
            const bool behavior=raw && result.advances==(s.candidate?afterAdvances[id]:beforeAdvances[id]) && result.queries==queryCounts[id] &&
                result.queryOutcome==outcomes[id] && int(result.queryReason)==reasons[id] && int(result.exit)==(s.candidate?afterExits[id]:beforeExits[id]) &&
                result.remaining==(s.candidate?afterRemaining[id]:beforeRemaining[id]) && result.toi==(s.candidate?afterToi[id]:beforeToi[id]);
            const bool requirement=result.advances==afterAdvances[id] && intact && pointers && cleared && (id==1 || unadvanced);
            std::ostringstream signature;signature<<result.queryOutcome<<','<<int(result.queryReason)<<','<<int(result.exit)<<','<<result.queries<<','<<result.advances<<','<<std::bit_cast<std::uint32_t>(result.remaining)<<','<<std::bit_cast<std::uint32_t>(result.toi);
            signature<<','<<int(result.diagnostics.termination)<<','<<result.diagnostics.iterations<<','<<int(result.diagnostics.epa.termination)<<','<<result.diagnostics.epa.iterations;
            for(auto word:words)signature<<','<<word;
            for(const auto& state:{result.predictedA,result.predictedB}) {for(auto word:QueryReplay::PoseBits(state))signature<<','<<word;signature<<','<<state.sleeping<<','<<std::bit_cast<std::uint64_t>(state.inactive);}
            if(mode){if(signature.str()!=caSignatures[id])s.Fail("conditional CA off/on mismatch");}else caSignatures[id]=signature.str();
            s.helperRows<<"{\"kind\":\"ca\",\"case\":"<<id<<",\"mode\":"<<mode<<",\"pass\":"<<(requirement?"true":"false")<<",\"expected_build_behavior\":"<<(behavior?"true":"false")<<",\"unchanged\":"<<(intact?"true":"false")<<",\"outcome\":"<<result.queryOutcome<<",\"reason\":"<<int(result.queryReason)<<",\"exit\":"<<int(result.exit)<<",\"queries\":"<<result.queries<<",\"advances\":"<<result.advances<<",\"remaining\":"<<result.remaining<<",\"toi\":"<<result.toi<<",\"gjk_termination\":"<<int(result.diagnostics.termination)<<",\"epa_termination\":"<<int(result.diagnostics.epa.termination)<<",\"contact_bits\":[";
            for(unsigned i=0;i<words.size();++i)s.helperRows<<(i?",":"")<<words[i];s.helperRows<<"],\"predicted_a_bits\":[";
            const auto predictedA=QueryReplay::PoseBits(result.predictedA),predictedB=QueryReplay::PoseBits(result.predictedB);
            for(unsigned i=0;i<predictedA.size();++i)s.helperRows<<(i?",":"")<<predictedA[i];s.helperRows<<"],\"predicted_b_bits\":[";
            for(unsigned i=0;i<predictedB.size();++i)s.helperRows<<(i?",":"")<<predictedB[i];
            s.helperRows<<"],\"predicted_a_sleeping\":"<<(result.predictedA.sleeping?"true":"false")<<",\"predicted_b_sleeping\":"<<(result.predictedB.sleeping?"true":"false")
                <<",\"predicted_a_inactive_bits\":"<<std::bit_cast<std::uint64_t>(result.predictedA.inactive)<<",\"predicted_b_inactive_bits\":"<<std::bit_cast<std::uint64_t>(result.predictedB.inactive)
                <<",\"gjk_iterations\":"<<result.diagnostics.iterations<<",\"epa_iterations\":"<<result.diagnostics.epa.iterations
                <<",\"pointers_ok\":"<<(pointers?"true":"false")<<",\"outputs_cleared\":"<<(cleared?"true":"false")<<",\"unadvanced_state\":"<<(unadvanced?"true":"false")<<"}\n";
            if(!intact || !behavior || (s.candidate && !requirement))s.Fail("conditional CA helper contract differs from frozen build behavior");
            if(!requirement)++s.physicalFailures;
        }
        std::array<std::string,2> projectionSignatures{};
        for (unsigned id=0;id<2 && !s.failed;++id) for(unsigned mode=0;mode<2 && !s.failed;++mode) {
            trace={};PD::queryObserver=mode?&observer:nullptr;const auto result=PD::EvaluateProjectionPrimitive(id);PD::queryObserver=nullptr;++s.projectionCalls;
            const Vec3f expectedWeights=id?Vec3f(.25f,.5f,.25f):Vec3f(.5f,.25f,.25f);
            const bool recovered=(result.termination==EpaTermination::Converged || result.termination==EpaTermination::DuplicateSupport) &&
                result.selectedFace==(id?0:1) && result.normal==Vec3f(0,1,0) && result.originDistance==-.125f &&
                result.projectedPoint==Vec3f(0,.125f,0) && result.expectedProjection==Vec3f(0,.125f,0) && result.lambdaSum==1 &&
                result.projectionErrorSquared==0 && result.projectionScaleSquared==1 && result.penetrationDistance==.125f &&
                result.lambdas==expectedWeights && result.witnessA==Vec3f(0,.125f,0) && result.witnessB==Vec3f(0) && result.failedPredicates==0;
            const bool baseline=result.termination==EpaTermination::InvalidProjection && result.selectedFace==0 &&
                result.lambdas==(id?Vec3f(1,0,0):Vec3f(.75f,.5f,-.25f)) && result.failedPredicates==(id?128u:32u) && result.witnessA==Vec3f(0) && result.witnessB==Vec3f(0);
            std::ostringstream signature;signature<<int(result.termination)<<','<<result.selectedFace<<','<<result.failedPredicates;
            for(auto v:{result.normal,result.lambdas,result.projectedPoint,result.expectedProjection,result.witnessA,result.witnessB})for(int k=0;k<3;++k)signature<<','<<std::bit_cast<std::uint32_t>(v[k]);
            for(float v:{result.originDistance,result.lambdaSum,result.projectionErrorSquared,result.projectionScaleSquared,result.penetrationDistance})signature<<','<<std::bit_cast<std::uint32_t>(v);
            if(mode){if(signature.str()!=projectionSignatures[id])s.Fail("conditional projection off/on mismatch");}else projectionSignatures[id]=signature.str();
            s.helperRows<<"{\"kind\":\"projection\",\"case\":"<<id<<",\"mode\":"<<mode<<",\"pass\":"<<(recovered?"true":"false")<<",\"termination\":"<<int(result.termination)<<",\"selected_face\":"<<result.selectedFace<<",\"failed_predicates\":"<<result.failedPredicates<<",\"weights\":";
            ConditionalJsonVector(s.helperRows,result.lambdas);s.helperRows<<",\"projection\":";ConditionalJsonVector(s.helperRows,result.projectedPoint);s.helperRows<<",\"witness_a\":";ConditionalJsonVector(s.helperRows,result.witnessA);s.helperRows<<",\"witness_b\":";ConditionalJsonVector(s.helperRows,result.witnessB);
            s.helperRows<<",\"normal\":";ConditionalJsonVector(s.helperRows,result.normal);s.helperRows<<",\"expected_projection\":";ConditionalJsonVector(s.helperRows,result.expectedProjection);
            s.helperRows<<",\"origin_distance\":"<<result.originDistance<<",\"lambda_sum\":"<<result.lambdaSum<<",\"penetration_distance\":"<<result.penetrationDistance
                <<",\"error_squared\":"<<result.projectionErrorSquared<<",\"scale_squared\":"<<result.projectionScaleSquared<<"}\n";
            if(!(s.candidate?recovered:baseline))s.Fail("conditional projection primitive frozen requirement");
            if(!recovered)++s.physicalFailures;
        }
        PD::queryObserver=nullptr;
        for(unsigned i=0;i<world.size();++i)if(!QueryReplay::SameBody(liveBefore[i],PD::Capture(*world[i])))s.Fail("conditional queries changed live Scene");
        if (s.scene->GetPhysicsTiming().totalSteps!=0) s.Fail("conditional query work advanced Scene");
        s.queriesComplete=!s.failed && s.collisionCalls==50 && s.directCalls==16 && s.caCalls==12 && s.projectionCalls==4;
        return s.queriesComplete;
    }

    struct TargetSession
    {
        static constexpr unsigned Count=185, Invalid=Count;
        struct Binding {
            RigidBodyIdentity id;
            EntityRenderId render;
            entt::entity entity{entt::null};
            std::uint64_t uuid{}, shapeRevision{};
            std::uintptr_t pointer{}, shape{};
            Mat3 inertia{};
            Vec3f center{}, scale{}, publishedPosition{};
            Quat publishedRotation{1,0,0,0};
            float inverseMass{}, elasticity{}, friction{}, spin{}, rolling{};
            std::uint32_t layer{}, mask{};
            BodyType type{};
            RigidBody3D::SleepSettings sleep;
            PD::BodyState initial{}, last{};
            std::string name;
        };
        struct Pair {
            std::uint64_t serial{};
            std::array<std::uint64_t,4> points{}, old{};
            int pendingSlot{-1}, pendingOld{-1};
            bool patch{};
        };
        struct Record {
            unsigned kind{}, stage{}, a{}, b{};
            std::uint64_t sequence{}, stamp{}, pair{}, point{}, oldPoint{};
            PD::Event event;
        };
        static constexpr std::size_t ScratchBytes=64*1024*1024;
        static constexpr std::size_t Capacity=ScratchBytes/sizeof(Record);
        LaunchConfig::TargetedEntityDiagnostic config;
        _Scene* scene{};
        std::uintptr_t worldToken{};
        std::array<Binding,Count> bindings;
        std::array<std::pair<std::uintptr_t,unsigned>,Count> lookup;
        std::array<Pair,Count*Count> pairs{};
        std::array<bool,Count> direct{}, prior{}, selected{}, presented{};
        std::array<PD::BodyState,Count> callbackBefore{};
        std::unique_ptr<Record[]> scratch;
        std::size_t used{}, peakBytes{};
        PD::Observer sink{};
        std::ofstream records, initial, timing, updates, draws, cameras, bookmarks, renderBindings;
        unsigned target{Invalid}, ground{Invalid}, tick{}, stage{}, firstGround{}, bookmarkTick{};
        std::uint64_t sequence{}, nextPair{}, nextPoint{}, retained{}, frame{}, lastPresented{}, bookmarkFrame{};
        std::uint64_t tickStart{}, boundaryNs{}, totalBoundaryNs{}, frameObserverNs{}, tickObservationStart{};
        std::uint64_t beginNs{PD::Nanoseconds()}, endNs{}, bookmarkCount{}, targetDraws{};
        double totalDiscarded{}, pending{}, lastAlpha{}, lastFrameElapsed{};
        bool failed{}, complete{}, opened{}, visible{}, interpolated{}, sleepingEnabled{};
        bool frameExtracted{}, frameSubmitted{}, physicalEdit{};
        bool startupVerified{}, tickFlushed{};
        bool replayRequested{};
        bool conditionalAttended{};
        std::ofstream attendedGround;
        std::uint64_t attendedEligible{},attendedMissed{},attendedGroundRows{};
        std::unique_ptr<QueryReplay> replay;
        double initialStep{};
        Vec3f initialGravity{};
        int initialPasses{};
        int swapInterval{}, manualCap{};
        const char* failure="";
        const char* stopReason="owner/window close";
        explicit TargetSession(const LaunchConfig::TargetedEntityDiagnostic& c):config(c) {}
        void Fail(const char* why) noexcept { if (!failed) { failed=true; failure=why; } }
        const PhysicsWorld* World() const noexcept
        {
            if (!scene || !scene->GetPhysicsSystem()) return nullptr;
            const auto* w=scene->GetPhysicsSystem()->GetPhysicsWorld();
            return reinterpret_cast<std::uintptr_t>(w)==worldToken?w:nullptr;
        }
        unsigned Index(const RigidBody3D* body) const noexcept
        {
            const auto token=reinterpret_cast<std::uintptr_t>(body);
            auto it=std::lower_bound(lookup.begin(),lookup.end(),token,
                [](const auto& item,std::uintptr_t value){return item.first<value;});
            return it!=lookup.end() && it->first==token?it->second:Invalid;
        }
        bool Valid(const RigidBody3D* body) noexcept
        {
            const auto i=Index(body); const auto* w=World();
            // Establish live membership and generation BEFORE following the address.
            if (i==Invalid || !w || w->GetPhysicsBodies().size()!=Count ||
                w->GetPhysicsBodies()[i]!=body || !w->IsBodyIdentityValid(bindings[i].id)) {
                Fail("body/world membership or generation changed"); return false;
            }
            if (body->GetIdentity()!=bindings[i].id) { Fail("RuntimeBody identity changed"); return false; }
            return true;
        }
        static bool Validate(void* context,const RigidBody3D* a,const RigidBody3D* b) noexcept
        {
            auto& s=*static_cast<TargetSession*>(context); return !s.failed && s.Valid(a) && s.Valid(b);
        }
        static void Violation(void* context) noexcept
        { static_cast<TargetSession*>(context)->Fail("observer changed authoritative body state"); }
        bool Invariants(bool betweenUpdates) noexcept
        {
            const auto* w=World();
            if (!w || w->GetPhysicsBodies().size()!=Count || w->GetGravity()!=Vec3f(0,-12,0) ||
                scene->GetPhysicsStep().count()!=1.0/60.0 || scene->GetPhysicsSystem()->GetSolverIterations()!=1 ||
                scene->GetPhysicsSystem()->IsSleepingEnabled()!=sleepingEnabled || scene->IsRenderInterpolationEnabled()!=interpolated) {
                Fail("normal scene clock, world or policy changed"); return false;
            }
            const auto& registry=scene->ReadRegistry();
            for (unsigned i=0;i<Count;++i) {
                const auto* b=w->GetPhysicsBodies()[i]; const auto& old=bindings[i];
                if (!Valid(b)) return false;
                if (!registry.valid(old.entity) || !registry.all_of<Component::IDComponent,Component::TagComponent,
                    Component::RigidBody3DComponent,Component::Transform3DComponent>(old.entity)) {
                    Fail("bound entity retired or lost required components"); return false;
                }
                const auto& rb=registry.get<Component::RigidBody3DComponent>(old.entity);
                const auto& tr=registry.get<Component::Transform3DComponent>(old.entity);
                if (rb.RuntimeBody!=b || std::uint64_t(registry.get<Component::IDComponent>(old.entity).ID)!=old.uuid ||
                    registry.get<Component::TagComponent>(old.entity).Name!=old.name ||
                    reinterpret_cast<std::uintptr_t>(b->m_Shape)!=old.shape || !b->m_Shape ||
                    b->m_Shape->GetRevision()!=old.shapeRevision || b->m_InvMass!=old.inverseMass ||
                    b->m_Elasticity!=old.elasticity || b->m_Friction!=old.friction || b->Type!=old.type ||
                    b->m_CollisionLayer!=old.layer || b->m_CollisionMask!=old.mask ||
                    b->GetSpinResistanceLength()!=old.spin || b->GetRollingResistanceLength()!=old.rolling ||
                    b->GetSleepSettings()!=old.sleep || tr.Scale!=old.scale) {
                    Fail("bound identity, shape or material/policy changed"); return false;
                }
                if (betweenUpdates && (!PD::SameState(PD::Capture(*b),old.last) ||
                    tr.Translation!=old.publishedPosition || tr.QuatRotation!=old.publishedRotation)) {
                    physicalEdit=true; Fail("physical authoring/state changed between ticks"); return false;
                }
            }
            return true;
        }
        Record* Push(unsigned kind,unsigned a,unsigned b) noexcept
        {
            if (failed) return nullptr;
            if (!scratch || used==Capacity) { Fail("64 MiB tick scratch exhausted; no data dropped silently"); return nullptr; }
            auto& r=scratch[used++]; r=Record{}; r.kind=kind; r.a=a; r.b=b; r.stage=stage;
            r.sequence=++sequence; r.stamp=PD::Nanoseconds();
            peakBytes=std::max(peakBytes,used*sizeof(Record)); return &r;
        }
        Pair& PairFor(unsigned a,unsigned b) noexcept
        { return pairs[std::min(a,b)*Count+std::max(a,b)]; }
        void Touch(unsigned a,unsigned b) noexcept
        {
            if (a==b) return;
            if (a==target) direct[b]=true;
            if (b==target) direct[a]=true;
            if (!firstGround && ((a==target && b==ground)||(b==target && a==ground))) firstGround=tick;
        }
        bool Retain(const Record& row) const noexcept
        {
            // Static support is observed, but is not a causal expansion root for
            // unrelated bodies. Keep every target row and every operation of
            // current/previous dynamic partners, including their other contacts.
            if (row.a==target || row.b==target) return true;
            const bool a=direct[row.a] || prior[row.a], b=direct[row.b] || prior[row.b];
            return (row.a==row.b && a) ||
                (a && bindings[row.a].type==BodyType::Dynamic) ||
                (b && bindings[row.b].type==BodyType::Dynamic);
        }
        static void Event(void* context,const PD::Event& event) noexcept
        {
            auto& s=*static_cast<TargetSession*>(context);
            if (s.failed) return;
            const auto a=s.Index(event.a), b=s.Index(event.b);
            if (a==Invalid || b==Invalid) { s.Fail("unbound operation participant"); return; }
            auto* row=s.Push(1,a,b); if (!row) return;
            row->event=event; row->event.a=row->event.b=nullptr; // Value-only retained data.
            auto& p=s.PairFor(a,b); const auto op=event.operation; const int slot=event.slot;
            if (op==PD::Operation::PairBegin) {
                if (p.serial) s.Fail("duplicate live manifold pair");
                p=Pair{}; p.serial=++s.nextPair;
            }
            row->pair=p.serial;
            if (slot>=0 && slot<4) { row->oldPoint=p.points[slot]; row->point=p.points[slot]; }
            if (op==PD::Operation::PairReset) p.points={};
            else if (op==PD::Operation::PairEnd) p=Pair{};
            else if (op==PD::Operation::PatchBegin) { p.old=p.points; p.points={}; p.patch=true; }
            else if (op==PD::Operation::PointMap) { p.pendingSlot=slot; p.pendingOld=event.previousSlot; }
            else if (op==PD::Operation::PatchEnd) { p.old={}; p.patch=false; p.pendingSlot=p.pendingOld=-1; }
            else if (op==PD::Operation::ContactNew || op==PD::Operation::ContactRefresh) {
                if (!p.serial || slot<0 || slot>=4) { s.Fail("contact has no verified manifold/slot"); return; }
                const auto oldSlot=p.patch?p.pendingOld:slot;
                row->event.previousSlot=oldSlot;
                row->oldPoint=oldSlot>=0 && oldSlot<4?(p.patch?p.old[oldSlot]:p.points[oldSlot]):0;
                if (p.patch && p.pendingSlot!=slot) s.Fail("contact patch mapping missing");
                p.points[slot]=op==PD::Operation::ContactRefresh?row->oldPoint:++s.nextPoint;
                if (!p.points[slot]) s.Fail("refreshed contact has no predecessor");
                row->point=p.points[slot]; s.Touch(a,b);
            } else if (op==PD::Operation::ContactRemove) {
                if (slot<0 || slot>=4 || !p.points[slot]) s.Fail("removed contact has no predecessor");
                else { for (int i=slot;i<3;++i) p.points[i]=p.points[i+1]; p.points[3]=0; }
            } else if (op==PD::Operation::ToiQuery || op<=PD::Operation::ToiPosition) s.Touch(a,b);
        }
        void WriteRecord(const Record& r)
        {
            const auto& e=r.event;
            for (unsigned v:{r.kind,tick,r.stage,r.a,r.b,unsigned(e.operation)}) Binary(records,v);
            for (auto v:{r.sequence,r.stamp,r.pair,r.point,r.oldPoint}) Binary(records,v);
            for (int v:{e.slot,e.previousSlot,e.reason}) Binary(records,v);
            Binary(records,std::uint32_t(e.appliedImpulsesKnown));
            for (double v:{e.time,e.duration,e.restitution,e.closingSpeed,e.penetration,e.bias}) Binary(records,v);
            for (const auto& v:{e.normal,e.anchorA,e.anchorB,e.localA,e.localB,e.tangentU,e.tangentV,
                e.linearImpulseA,e.linearImpulseB,e.angularImpulseA,e.angularImpulseB})
                for (float f:{v.x,v.y,v.z}) Binary(records,f);
            for (const auto& values:{e.accumulatedBefore,e.accumulatedAfter}) for (double v:values) Binary(records,v);
            for (const auto& values:{e.normalRow,e.tangentRowU,e.tangentRowV}) for (double v:values) Binary(records,v);
            Binary(records,e.featureA); Binary(records,e.featureB);
            Binary(records,std::uint64_t(e.constraintToken));
            for (const auto& v:{e.beforeA,e.beforeB,e.afterA,e.afterB}) BinaryState(records,v);
            ++retained;
        }
        void SnapshotContacts(ManifoldCollector& manifolds) noexcept
        {
            for (auto& manifold:manifolds.m_Manifolds) for (int slot=0;slot<manifold.GetNumContacts();++slot) {
                const auto c=manifold.GetContact(slot);
                if (!Validate(this,c.m_BodyA,c.m_BodyB)) return;
                const auto a=Index(c.m_BodyA),b=Index(c.m_BodyB); Touch(a,b);
                auto* r=Push(2,a,b); if (!r) return;
                auto& p=PairFor(a,b); r->pair=p.serial; r->point=p.points[slot];
                if (!r->pair || !r->point) { Fail("snapshot missing manifold continuity"); return; }
                auto& e=r->event; e.operation=PD::Operation::ContactSnapshot; e.slot=slot;
                e.beforeA=e.afterA=PD::Capture(*c.m_BodyA); e.beforeB=e.afterB=PD::Capture(*c.m_BodyB);
                e.localA=c.ptOnA_LocalSpace; e.localB=c.ptOnB_LocalSpace; e.normal=-c.normal;
                // Equivalent observation math on copies; never touch mutable body caches.
                e.anchorA=e.beforeA.position+glm::mat3_cast(Math::QuaternionOrIdentity(e.beforeA.orientation))*(bindings[a].center+e.localA);
                e.anchorB=e.beforeB.position+glm::mat3_cast(Math::QuaternionOrIdentity(e.beforeB.orientation))*(bindings[b].center+e.localB);
                e.penetration=-glm::dot(e.anchorB-e.anchorA,e.normal);
                e.featureA=c.featureA; e.featureB=c.featureB;
            }
        }
        void ObserveAttendedGround(const PhysicsWorld& world,ManifoldCollector& manifolds) noexcept
        {
            std::array<bool,Count> admitted{};
            for (auto& manifold:manifolds.m_Manifolds) for (int slot=0;slot<manifold.GetNumContacts();++slot) {
                const auto contact=manifold.GetContact(slot);
                if (!Validate(this,contact.m_BodyA,contact.m_BodyB)) return;
                const auto a=Index(contact.m_BodyA),b=Index(contact.m_BodyB);
                if (a==ground) admitted[b]=true;
                if (b==ground) admitted[a]=true;
            }
            unsigned eligible=0,missed=0; bool targetEligible=false,targetMissed=false;
            const auto& bodies=world.GetPhysicsBodies();
            for (unsigned i=0;i<Count;++i) {
                const auto& body=*bodies[i];
                if (body.IsSleeping() || body.GetInverseMass()<=0 || body.m_Shape->GetShapeType()!=ShapeType::Sphere) continue;
                const float radius=static_cast<const ShapeSphere*>(body.m_Shape)->GetRadius();
                if (std::abs(body.m_Position.x)>50-radius || std::abs(body.m_Position.z)>50-radius ||
                    .5+radius-body.m_Position.y<=1e-4) continue;
                ++eligible; missed+=!admitted[i];
                if (i==target) { targetEligible=true; targetMissed=!admitted[i]; }
            }
            attendedEligible+=eligible; attendedMissed+=missed; ++attendedGroundRows;
            attendedGround<<tick<<','<<eligible<<','<<missed<<','<<targetEligible<<','<<targetMissed<<'\n';
            if (!attendedGround) Fail("conditional attended ground evidence write failed");
            if (missed) Fail("conditional attended missed penetrating ground contact");
        }
        static void Boundary(void* context,PD::Stage boundary,const PhysicsWorld& world,
            ManifoldCollector& manifolds,std::span<const ContactIsland>,std::size_t,int passes) noexcept
        {
            auto& s=*static_cast<TargetSession*>(context); const auto start=PD::Nanoseconds();
            if (s.failed) return;
            if (boundary==PD::Stage::Begin) {
                ++s.tick; s.stage=0; s.used=0; s.sequence=0; s.boundaryNs=0; s.tickFlushed=false;
                s.tickStart=start; s.tickObservationStart=s.sink.observationNs; s.direct={};
                if (s.tick>(s.conditionalAttended?900u:1800u) || passes!=1 || !s.Invariants(false)) { s.Fail("tick/pass/identity invariant"); return; }
            }
            s.stage=unsigned(boundary);
            if (s.conditionalAttended && boundary==PD::Stage::Narrowphase) s.ObserveAttendedGround(world,manifolds);
            const auto& bodies=world.GetPhysicsBodies();
            for (unsigned i=0;i<Count;++i) {
                if (!s.Valid(bodies[i])) return;
                s.callbackBefore[i]=PD::Capture(*bodies[i]);
                if (!bodies[i]->HasFiniteState()) { s.Fail("nonfinite Physics state"); return; }
                auto* r=s.Push(0,i,i); if (!r) return;
                r->event.beforeA=r->event.afterA=r->event.beforeB=r->event.afterB=s.callbackBefore[i];
            }
            s.SnapshotContacts(manifolds);
            if (boundary==PD::Stage::End && !s.failed) {
                double energy=0;
                for (unsigned i=0;i<Count;++i) {
                    s.selected[i]=i==s.target || s.direct[i] || s.prior[i];
                    const auto state=PD::Capture(*bodies[i]); s.bindings[i].last=state;
                    if (s.bindings[i].inverseMass>0 && s.bindings[i].type==BodyType::Dynamic) {
                        const double mass=1.0/s.bindings[i].inverseMass;
                        const auto localW=glm::transpose(glm::dmat3(glm::mat3_cast(state.orientation)))*glm::dvec3(state.angular);
                        energy+=mass*(.5*glm::dot(glm::dvec3(state.linear),glm::dvec3(state.linear))+
                            .5*glm::dot(localW,glm::dmat3(s.bindings[i].inertia)*localW)+12.0*state.position.y);
                    }
                }
                for (std::size_t i=0;i<s.used;++i) {
                    const auto& row=s.scratch[i];
                    if (s.Retain(row)) s.WriteRecord(row);
                }
                s.tickFlushed=true;
                s.prior=s.direct;
                if (!std::isfinite(energy) || !s.records) s.Fail("trace/energy write failed");
                const auto cost=PD::Nanoseconds()-start;
                s.timing<<s.tick<<','<<start-s.tickStart<<','<<s.boundaryNs+cost<<','
                    <<s.sink.observationNs-s.tickObservationStart<<','<<s.used<<','<<s.sequence<<','<<energy<<'\n';
            }
            for (unsigned i=0;i<Count;++i)
                if (!PD::SameState(s.callbackBefore[i],PD::Capture(*bodies[i]))) s.Fail("boundary callback changed Physics state");
            const auto cost=PD::Nanoseconds()-start; s.boundaryNs+=cost; s.totalBoundaryNs+=cost;
            if (!s.timing) s.Fail("timing write failed");
            // Fixed terminal stop through the existing Scene API; no step or
            // elapsed-time alteration before the final authorized Physics tick.
            if (s.conditionalAttended && boundary==PD::Stage::End && s.tick==900) s.scene->SetPaused(true);
        }
        bool ReplayGeometry() noexcept
        {
            const auto* w=World(); if (!w || !Valid(w->GetPhysicsBodies()[target]) || !Valid(w->GetPhysicsBodies()[ground])) return false;
            const auto* a=w->GetPhysicsBodies()[ground]; const auto* b=w->GetPhysicsBodies()[target];
            if (a->m_Shape->GetShapeType()!=ShapeType::Box || b->m_Shape->GetShapeType()!=ShapeType::Sphere ||
                a->m_Shape->GetRevision()!=1 || b->m_Shape->GetRevision()!=0 ||
                a->m_Position!=Vec3f(0) || a->m_Orientation!=Quat(1,0,0,0) || a->m_LinearVelocity!=Vec3f(0) || a->m_AngularVelocity!=Vec3f(0) ||
                a->Type!=BodyType::Static || a->m_InvMass!=0 || a->m_Elasticity!=.5f || a->m_Friction!=.5f ||
                a->m_CollisionLayer!=1 || a->m_CollisionMask!=~std::uint32_t{0} || a->GetSpinResistanceLength()!=0 ||
                a->GetRollingResistanceLength()!=0 || a->GetSleepSettings()!=RigidBody3D::SleepSettings{} ||
                a->m_Shape->GetCenterOfMass()!=Vec3f(0) || b->m_Shape->GetCenterOfMass()!=Vec3f(0) ||
                b->IsSleeping() || b->GetInactiveSeconds()!=0 || static_cast<const ShapeSphere*>(b->m_Shape)->GetRadius()!=1) return false;
            const auto bounds=a->m_Shape->GetBounds();
            if (bounds.mins!=Vec3f(-50,-.5f,-50) || bounds.maxs!=Vec3f(50,.5f,50)) return false;
            Mat3 gi(0),si(.40000000596046448f); gi[0][0]=gi[2][2]=833.41668701171875f; gi[1][1]=1666.6666259765625f;
            if (a->m_Shape->InertiaTensor()!=gi || b->m_Shape->InertiaTensor()!=si) return false;
            for (int x:{-1,1}) for (int y:{-1,1}) for (int z:{-1,1})
                if (a->m_Shape->Support(Vec3f(float(x),float(y),float(z)),a->m_Position,a->m_Orientation,0)!=Vec3f(50.f*x,.5f*y,50.f*z)) return false;
            return true;
        }
        bool RunReplay()
        {
            if (!replay || replay->attempted || PD::queryObserver || scene->GetPhysicsTiming().totalSteps!=0 || !ReplayGeometry()) {
                Fail("replay geometry, single-use or zero-tick precondition failed"); return false;
            }
            auto& r=*replay; r.attempted=true; r.sink.context=&r; r.sink.event=QueryReplay::Observe;
            if(r.conditional){r.sink.epaProjection=QueryReplay::Projection;r.sink.barycentric=QueryReplay::Barycentric;}
            Log::GetCoreLogger()->info("PHASE20_QUERY_REPLAY_BEGIN states=13 queries=26 live_scene_ticks=0; no owner visual validation");
            const auto dir=std::filesystem::path(config.outputDirectory);
            std::ofstream pairRows(dir/"replay-pairs.csv"); pairRows<<std::setprecision(17)<<"tick,pair_count,a,b";
            for (const char* name:{"ground","target"}) for (const char* bound:{"min","max"}) for (char axis:std::string("xyz")) pairRows<<','<<name<<'_'<<bound<<'_'<<axis;
            pairRows<<'\n';
            for (unsigned inputIndex=0;inputIndex<QueryReplay::Inputs.size();++inputIndex) {
                const auto& input=QueryReplay::Inputs[r.conditional?QueryReplay::ConditionalOrder[inputIndex]:inputIndex];
                RigidBody3D baseA=*World()->GetPhysicsBodies()[ground],baseB=*World()->GetPhysicsBodies()[target];
                QueryReplay::Load(baseB,input);
                if (QueryReplay::PoseBits(PD::Capture(baseB))!=input.bits || baseB.IsSleeping() || baseB.GetInactiveSeconds()!=0) {
                    Fail("replay float-bit input load failed"); break;
                }
                SweepAndPruneBroadphase broadphase; std::vector<RigidBody3D*> bodies{&baseA,&baseB}; std::vector<collisionPair_t> pairs;
                broadphase.FindPairs(bodies,pairs,float(1.0/60.0)); ++r.pairChecks;
                r.pairsMatch=r.pairsMatch && pairs.size()==1 && pairs[0].a==0 && pairs[0].b==1;
                pairRows<<input.tick<<','<<pairs.size()<<','<<(pairs.empty()?-1:pairs[0].a)<<','<<(pairs.empty()?-1:pairs[0].b);
                for (auto* b:bodies) { const auto& bounds=b->GetWorldBounds(); CsvVector(pairRows,bounds.mins); CsvVector(pairRows,bounds.maxs); } pairRows<<'\n';
                for (unsigned mode=0;mode<2;++mode) {
                    RigidBody3D a=baseA,b=baseB; contact_t contact{};
                    const auto beforeA=PD::Capture(a),beforeB=PD::Capture(b);
                    r.activeTick=input.tick; r.sink.iteration=0; const auto beforeNs=r.sink.observationNs;
                    PD::queryObserver=mode?&r.sink:nullptr;
                    const auto start=PD::Nanoseconds();
                    const bool hit=Collision::Intersect(&a,&b,float(1.0/60.0),contact);
                    const auto elapsed=PD::Nanoseconds()-start;
                    PD::queryObserver=nullptr;
                    auto& result=r.answers[r.calls++]; result.tick=input.tick; result.mode=mode; result.hit=hit;
                    result.elapsedNs=elapsed; result.observerNs=r.sink.observationNs-beforeNs;
                    result.pointers=contact.m_BodyA==&a && contact.m_BodyB==&b;
                    result.unchanged=QueryReplay::SameBody(beforeA,PD::Capture(a)) && QueryReplay::SameBody(beforeB,PD::Capture(b));
                    result.bits=QueryReplay::ContactBits(contact);
                    if (r.epa && (r.epa->overflow || r.epa->missingBary)) r.overflow=true;
                    if (!result.pointers || !result.unchanged || r.overflow) { Fail("query identity/state/record bound failed"); break; }
                    if (mode) {
                        const auto& off=r.answers[r.calls-2];
                        r.answersMatch=r.answersMatch && off.hit==result.hit && off.bits==result.bits && off.pointers==result.pointers && off.unchanged==result.unchanged;
                        if (!r.answersMatch) { Fail("connected/disconnected query result differs"); break; }
                    }
                    if (input.tick==566 || input.tick==589) {
                        bool finite=true;
                        for (unsigned k=0;k<17;++k) finite=finite && Math::IsFinite(std::bit_cast<float>(result.bits[k]));
                        r.controlsMatch=r.controlsMatch && hit && finite && contact.timeOfImpact==0;
                    }
                }
                if (failed) break;
            }
            r.observerNs=r.sink.observationNs; r.sceneTicks=scene->GetPhysicsTiming().totalSteps;
            r.liveUnchanged=Invariants(true) && ReplayGeometry();
            for (unsigned i=0;i<Count && r.liveUnchanged;++i) r.liveUnchanged=QueryReplay::SameBody(bindings[i].initial,PD::Capture(*World()->GetPhysicsBodies()[i]));
            if (!r.liveUnchanged || r.sceneTicks!=0) Fail("live Scene changed during query replay");
            pairRows.flush(); if (!pairRows || !r.Write(dir)) Fail("replay evidence write failed");
            if (r.calls!=26 || r.pairChecks!=13) Fail("replay did not complete exact query counts");
            if (r.epa && (r.epa->attemptCount<13 || r.epa->overflow || r.epa->missingBary)) Fail("EPA capture incomplete or overflowed");
            return !failed;
        }
        bool FinishReplay(bool clean)
        {
            if (!replayRequested) return true;
            std::ofstream out(std::filesystem::path(config.outputDirectory)/"replay-result.json");
            const bool ok=replay && replay->attempted && clean && complete && !failed && replay->calls==26 && replay->pairChecks==13 && replay->sceneTicks==0 && replay->answersMatch && replay->liveUnchanged;
            out<<"{\"schema\":1,\"kind\":\"RECONSTRUCTED_NATIVE_QUERY_NOT_HISTORICAL_BRANCH\",\"technical_pass\":"<<(ok?"true":"false")
                <<",\"input_sha256\":\""<<QueryReplay::InputHash<<"\",\"owner_visible_validation\":false,\"clean_shutdown\":"<<(clean?"true":"false")
                <<",\"queries\":"<<(replay?replay->calls:0)<<",\"pair_checks\":"<<(replay?replay->pairChecks:0)
                <<",\"scene_ticks\":"<<(replay?replay->sceneTicks:0)<<",\"decision_rows\":"<<(replay?replay->used:0)
                <<",\"pairs_match\":"<<(replay && replay->pairsMatch?"true":"false")<<",\"known_controls_match\":"<<(replay && replay->controlsMatch?"true":"false")
                <<",\"exact_results_match\":"<<(replay && replay->answersMatch?"true":"false")<<",\"live_state_unchanged\":"<<(replay && replay->liveUnchanged?"true":"false")
                <<",\"observer_copy_sink_ns\":"<<(replay?replay->observerNs:0)<<",\"observer_timing_excludes_optional_gjk_diagnostic_stores\":true"
                <<",\"additional_recorder_bytes\":"<<sizeof(QueryReplay)+(replay && replay->epa?sizeof(EpaCapture):0)<<",\"overflow\":"<<(replay && replay->overflow?"true":"false")
                <<",\"conditional_diagnosis\":"<<(replay && replay->conditional?"true":"false")
                <<",\"epa_attempts\":"<<(replay && replay->epa?replay->epa->attemptCount:0)
                <<",\"epa_snapshots\":"<<(replay && replay->epa?replay->epa->snapshotCount:0)
                <<",\"epa_overflow\":"<<(replay && replay->epa && replay->epa->overflow?"true":"false")
                <<",\"epa_missing_barycentric\":"<<(replay && replay->epa && replay->epa->missingBary?"true":"false")
                <<",\"epa_record_storage_bytes\":"<<(replay && replay->epa?sizeof(EpaCapture):0)
                <<",\"failure\":"<<std::quoted(failure)<<"}\n";
            out.flush(); return ok && bool(out);
        }
        bool Open(_Scene& owner)
        {
            char* requestRaw{}; std::size_t requestSize{};
            if (_dupenv_s(&requestRaw,&requestSize,"GENGINE_P20_QUERY_REPLAY_REQUEST")!=0) { Fail("replay environment read failed"); return false; }
            replayRequested=requestRaw!=nullptr;
            std::string request=requestRaw?requestRaw:""; std::free(requestRaw);
            char* conditionalRaw{};std::size_t conditionalSize{};
            if (_dupenv_s(&conditionalRaw,&conditionalSize,"GENGINE_P20_CONDITIONAL_DIAGNOSIS_REQUEST")!=0) {Fail("conditional diagnosis environment read failed");return false;}
            const bool conditional=conditionalRaw!=nullptr;
            if(conditional && replayRequested){std::free(conditionalRaw);Fail("conflicting Phase20 query requests");return false;}
            if(conditional){request=conditionalRaw;replayRequested=true;}std::free(conditionalRaw);
            if (replayRequested) {
                const auto expected=(std::filesystem::path(config.outputDirectory).parent_path()/(conditional?"diagnosis-request.txt":"replay-request.txt")).lexically_normal();
                if (request.empty() || !std::filesystem::path(request).is_absolute() || std::filesystem::path(request).lexically_normal()!=expected || !QueryReplay::ValidRequest(expected,conditional)) {
                    Fail("invalid explicit Phase20 replay request; no normal stepping fallback"); return false;
                }
                replay.reset(new(std::nothrow) QueryReplay);
                if (!replay) { Fail("bounded replay allocation failed"); return false; }
                replay->conditional=conditional;
                if(conditional){replay->epa.reset(new(std::nothrow) EpaCapture);if(!replay->epa){Fail("bounded EPA capture allocation failed");return false;}}
            }
            if (const char* attended=std::getenv("GENGINE_P20_CONDITIONAL_ATTENDED_REQUEST")) {
                const auto expected=(std::filesystem::path(config.outputDirectory).parent_path()/"attended-request.txt").lexically_normal();
                const auto path=std::filesystem::path(attended);
                std::ifstream input(path); std::string magic,end; unsigned ticks{},seconds{},wall{};
                if (replayRequested || std::getenv("GENGINE_P20_CONDITIONAL_FOCUSED_REQUEST") || !path.is_absolute() ||
                    path.lexically_normal()!=expected || !(input>>magic>>ticks>>seconds>>wall>>end) ||
                    magic!="P20_CONDITIONAL_ATTENDED_V1" || ticks!=900 || seconds!=15 || wall!=60 || end!="END") {
                    Fail("invalid conditional attended request"); return false;
                }
                input>>std::ws;
                if (!input.eof() || IsPhysicsProfilingEnabled()) { Fail("conditional attended requires normal unprofiled product"); return false; }
                conditionalAttended=true;
            }
            scene=&owner; const auto* w=scene->GetPhysicsSystem()->GetPhysicsWorld();
            if (!w || w->GetPhysicsBodies().size()!=Count) { Fail("normal scene must contain 185 Physics bodies"); return false; }
            worldToken=reinterpret_cast<std::uintptr_t>(w);
            initialStep=scene->GetPhysicsStep().count(); initialGravity=w->GetGravity();
            initialPasses=scene->GetPhysicsSystem()->GetSolverIterations();
            sleepingEnabled=scene->GetPhysicsSystem()->IsSleepingEnabled();
            interpolated=scene->IsRenderInterpolationEnabled();
            scratch.reset(new(std::nothrow) Record[Capacity]);
            if (!scratch) { Fail("tick scratch allocation failed"); return false; }
            const auto dir=std::filesystem::path(config.outputDirectory); std::error_code ec;
            if (!std::filesystem::is_directory(dir,ec) || ec || !std::filesystem::is_empty(dir,ec) || ec) {
                Fail("target output must be an existing empty directory"); return false;
            }
            records.open(dir/"trace.bin",std::ios::binary); initial.open(dir/"initial.csv");
            timing.open(dir/"timing.csv"); updates.open(dir/"frames.csv"); draws.open(dir/"draws.csv");
            cameras.open(dir/"cameras.csv"); bookmarks.open(dir/"bookmarks.csv"); renderBindings.open(dir/"render-bindings.csv");
            if (conditionalAttended) {
                attendedGround.open(dir/"attended-ground.csv");
                attendedGround<<"tick,eligible_awake_ground,missed_awake_ground,target_eligible,target_missed\n";
                if (!attendedGround) { Fail("conditional attended evidence open failed"); return false; }
            }
            opened=true;
            for (auto* out:{&initial,&timing,&updates,&draws,&cameras,&bookmarks,&renderBindings}) *out<<std::setprecision(17);
            records.write("P19T60V1",8);
            initial<<"index,name,uuid,entity,slot,generation,pointer_token,shape_token,shape_revision,shape,type,inverse_mass,elasticity,friction,layer,mask,spin,rolling,linear_sleep,angular_sleep,dwell,radius,px,py,pz,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,sx,sy,sz,cmx,cmy,cmz";
            for (int i=0;i<9;++i) initial<<",inertia"<<i;
            initial<<'\n';
            timing<<"tick,begin_to_end_boundary_entry_ns,boundary_observer_ns,operation_observer_ns,scratch_records,total_sequence,energy\n";
            updates<<"frame,event,stamp_ns,tick,elapsed,pending,discarded,total_discarded,steps_last_update,total_steps,alpha,visible,extracted,submitted,last_presented,frame_observer_ns,swap_interval,manual_cap,paused,debug,logical_w,logical_h,pixel_w,pixel_h\n";
            draws<<"frame,tick,index,render_index,render_generation,render_registry,alpha";
            for (int i=0;i<16;++i) draws<<",m"<<i;
            draws<<'\n';
            cameras<<"frame,camera_index,generation,registry,viewport_x,viewport_y,viewport_w,viewport_h,px,py,pz";
            for (int i=0;i<32;++i) cameras<<",matrix"<<i;
            cameras<<'\n';
            bookmarks<<"frame,tick,stamp_ns,input_frame,last_presented,in_flight,extracted,submitted\n";
            renderBindings<<"frame,index,uuid,render_index,generation,registry\n";
            const auto& registry=scene->ReadRegistry(); unsigned bound=0,targetNames=0,groundNames=0,spheres=0;
            for (unsigned i=0;i<Count;++i) {
                const auto* b=w->GetPhysicsBodies()[i]; if (!b || !b->m_Shape || !w->IsBodyIdentityValid(b->GetIdentity())) { Fail("invalid startup body"); return false; }
                auto& row=bindings[i]; row.id=b->GetIdentity(); row.pointer=reinterpret_cast<std::uintptr_t>(b);
                lookup[i]={row.pointer,i}; row.shape=reinterpret_cast<std::uintptr_t>(b->m_Shape);
                row.shapeRevision=b->m_Shape->GetRevision(); row.inertia=b->m_Shape->InertiaTensor(); row.center=b->m_Shape->GetCenterOfMass();
                row.inverseMass=b->m_InvMass; row.elasticity=b->m_Elasticity; row.friction=b->m_Friction;
                row.spin=b->GetSpinResistanceLength(); row.rolling=b->GetRollingResistanceLength(); row.sleep=b->GetSleepSettings();
                row.layer=b->m_CollisionLayer; row.mask=b->m_CollisionMask; row.type=b->Type; row.initial=row.last=PD::Capture(*b);
                unsigned matches=0;
                for (auto e:registry.view<Component::RigidBody3DComponent,Component::IDComponent,Component::TagComponent,Component::Transform3DComponent>()) {
                    if (registry.get<Component::RigidBody3DComponent>(e).RuntimeBody!=b) continue;
                    ++matches; row.entity=e; row.uuid=std::uint64_t(registry.get<Component::IDComponent>(e).ID);
                    row.name=registry.get<Component::TagComponent>(e).Name;
                    const auto& tr=registry.get<Component::Transform3DComponent>(e);
                    row.scale=tr.Scale; row.publishedPosition=tr.Translation; row.publishedRotation=tr.QuatRotation;
                }
                if (matches!=1) { Fail("RuntimeBody must map to exactly one entity UUID"); return false; }
                ++bound;
                if (row.name==config.entityName) { target=i; ++targetNames; }
                if (row.name=="wood_plane") { ground=i; ++groundNames; }
                const bool sphere=b->m_Shape->GetShapeType()==ShapeType::Sphere; spheres+=sphere;
                const double radius=sphere?static_cast<const ShapeSphere*>(b->m_Shape)->GetRadius():0;
                initial<<i<<','<<std::quoted(row.name)<<','<<row.uuid<<','<<std::uint32_t(row.entity)<<','<<row.id.GetSlot()<<','<<row.id.GetGeneration()<<','<<row.pointer<<','<<row.shape<<','<<row.shapeRevision<<','<<int(b->m_Shape->GetShapeType())<<','<<int(row.type)<<','<<row.inverseMass<<','<<row.elasticity<<','<<row.friction<<','<<row.layer<<','<<row.mask<<','<<row.spin<<','<<row.rolling<<','<<row.sleep.linearSpeedThreshold<<','<<row.sleep.angularSpeedThreshold<<','<<row.sleep.inactivitySeconds<<','<<radius;
                CsvVector(initial,row.initial.position); initial<<','<<row.initial.orientation.w<<','<<row.initial.orientation.x<<','<<row.initial.orientation.y<<','<<row.initial.orientation.z;
                CsvVector(initial,row.initial.linear); CsvVector(initial,row.initial.angular); CsvVector(initial,row.scale); CsvVector(initial,row.center);
                for (int c=0;c<3;++c) for (int k=0;k<3;++k) initial<<','<<row.inertia[c][k]; initial<<'\n';
            }
            std::sort(lookup.begin(),lookup.end());
            unsigned allTargetNames=0;
            for (auto e:registry.view<Component::TagComponent>())
                allTargetNames+=registry.get<Component::TagComponent>(e).Name==config.entityName;
            for (unsigned i=0;i<Count;++i) for (unsigned j=0;j<i;++j)
                if (bindings[i].uuid==bindings[j].uuid || bindings[i].id==bindings[j].id) {
                    Fail("ambiguous startup UUID or Body identity"); return false;
                }
            if (allTargetNames!=1) { Fail("target name is ambiguous across the scene"); return false; }
            if (bound!=Count || spheres!=180 || targetNames!=1 || groundNames!=1) { Fail("normal scene/target/ground membership mismatch"); return false; }
            const auto& b=bindings[target]; const auto* body=w->GetPhysicsBodies()[target];
            if (b.initial.position!=Vec3f(2,18,2) || b.initial.orientation!=Quat(1,0,0,0) || b.scale!=Vec3f(1) ||
                b.initial.linear!=Vec3f(0) || b.initial.angular!=Vec3f(0) || b.type!=BodyType::Dynamic || b.inverseMass!=1 ||
                b.elasticity!=.5f || b.friction!=.5f || b.spin!=0 || b.rolling!=0 || b.layer!=1 || b.mask!=~std::uint32_t{0} ||
                body->m_Shape->GetShapeType()!=ShapeType::Sphere || static_cast<const ShapeSphere*>(body->m_Shape)->GetRadius()!=1 ||
                b.sleep!=RigidBody3D::SleepSettings{} || bindings[ground].type!=BodyType::Static) { Fail("target initial state differs from accepted normal construction"); return false; }
            if (PD::observer || !Invariants(false)) { Fail("observer already installed or startup invariant failed"); return false; }
            selected[target]=true;
            for (unsigned i=0;i<Count;++i) {
                auto* row=Push(0,i,i); if (!row) return false;
                row->event.beforeA=row->event.afterA=row->event.beforeB=row->event.afterB=bindings[i].initial;
                WriteRecord(*row);
            }
            used=0; sink.context=this; sink.detailed=true; sink.event=Event; sink.boundary=Boundary;
            sink.targeted=true; sink.valid=Validate; sink.violation=Violation;
            startupVerified=true;
            PD::observer=&sink;
            return initial && records && timing && updates && draws && cameras && bookmarks && renderBindings;
        }
        bool BeforeUpdate(_Scene& owner,double elapsed)
        {
            if (&owner!=scene) { Fail("scene replaced"); return false; }
            if (!Invariants(true)) return false;
            if (replayRequested) {
                ++frame; lastFrameElapsed=elapsed; frameExtracted=frameSubmitted=false;
                complete=RunReplay(); stopReason="Phase20 query replay; zero live Scene ticks";
                return false;
            }
            // BaseApp also renders its terminal iteration after ShutDown. Keep
            // its own frame ID; do not alter that normal presentation behavior.
            ++frame; lastFrameElapsed=elapsed; frameExtracted=frameSubmitted=false;
            if (conditionalAttended) {
                if (tick==900) { complete=true; stopReason="conditional fixed 15 seconds / 900 ticks"; return false; }
                if (PD::Nanoseconds()-beginNs>=50000000000ull) {
                    stopReason="conditional attended wall reserve; incomplete";
                    Fail("conditional attended fixed window incomplete at wall reserve"); return false;
                }
                return !failed;
            }
            if (tick+_Scene::MaxPhysicsStepsPerUpdate>1800 || (bookmarkCount && tick>=bookmarkTick+60) ||
                PD::Nanoseconds()-beginNs>=270000000000ull) {
                complete=true; stopReason=bookmarkCount && tick>=bookmarkTick+60?"bookmark plus 60 ticks":
                    (tick+_Scene::MaxPhysicsStepsPerUpdate>1800?"simulation bound":"wall close reserve"); return false;
            }
            return !failed;
        }
        void FrameRow(int event,bool isVisible,int swap,int cap,bool paused,bool debug,
            unsigned lw,unsigned lh,unsigned pw,unsigned ph)
        {
            const auto start=PD::Nanoseconds(); const auto& t=scene->GetPhysicsTiming();
            if (complete && event==1) event=2; // Terminal presentation; no Scene Update this iteration.
            visible=isVisible; swapInterval=swap; manualCap=cap; lastAlpha=scene->GetRenderInterpolationAlpha();
            pending=t.pendingSeconds; totalDiscarded=t.totalDiscardedSeconds;
            if (event==0) for (auto& binding:bindings) {
                const auto& tr=scene->ReadRegistry().get<Component::Transform3DComponent>(binding.entity);
                binding.publishedPosition=tr.Translation; binding.publishedRotation=tr.QuatRotation;
            }
            if (t.totalSteps!=tick || scene->GetPhysicsStep().count()!=1.0/60.0) Fail("Scene/Physics tick join mismatch");
            updates<<frame<<','<<event<<','<<start<<','<<tick<<','<<lastFrameElapsed<<','<<pending<<','<<(event==2?0.0:t.discardedSeconds)<<','<<totalDiscarded<<','<<(event==2?0:t.stepsLastUpdate)<<','<<t.totalSteps<<','<<lastAlpha<<','<<visible<<','<<frameExtracted<<','<<frameSubmitted<<','<<lastPresented<<','<<frameObserverNs<<','<<swap<<','<<cap<<','<<paused<<','<<debug<<','<<lw<<','<<lh<<','<<pw<<','<<ph<<'\n';
            frameObserverNs+=PD::Nanoseconds()-start;
            if (!updates) Fail("frame timing write failed");
        }
        static void ObserveFrame(void* context,const RenderFrame& frameData) noexcept
        {
            auto& s=*static_cast<TargetSession*>(context); const auto start=PD::Nanoseconds();
            if (s.failed || !s.Invariants(false)) return;
            s.frameExtracted=true;
            for (const auto& draw:frameData.Draws()) {
                auto entity=s.scene->RenderData().Resolve(draw.entity);
                if (!entity) { s.Fail("submitted draw identity cannot resolve"); return; }
                const auto* rb=s.scene->ReadRegistry().try_get<Component::RigidBody3DComponent>(*entity);
                if (!rb) continue;
                if (!s.Valid(rb->RuntimeBody)) return;
                const auto i=s.Index(rb->RuntimeBody); auto& binding=s.bindings[i];
                if (*entity!=binding.entity) { s.Fail("render/UUID/Body join mismatch"); return; }
                if (!binding.render) {
                    binding.render=draw.entity;
                    s.renderBindings<<s.frame<<','<<i<<','<<binding.uuid<<','<<draw.entity.index<<','<<draw.entity.generation<<','<<draw.entity.registry<<'\n';
                } else if (binding.render!=draw.entity) { s.Fail("render identity changed"); return; }
                if (!s.selected[i]) continue;
                if (i==s.target) ++s.targetDraws;
                s.presented[i]=true;
                s.draws<<s.frame<<','<<s.tick<<','<<i<<','<<draw.entity.index<<','<<draw.entity.generation<<','<<draw.entity.registry<<','<<s.scene->GetRenderInterpolationAlpha();
                for (int c=0;c<4;++c) for (int k=0;k<4;++k) s.draws<<','<<draw.worldTransform[c][k];
                s.draws<<'\n';
            }
            for (const auto& camera:frameData.Cameras()) {
                s.cameras<<s.frame<<','<<camera.entity.index<<','<<camera.entity.generation<<','<<camera.entity.registry<<','<<camera.viewportX<<','<<camera.viewportY<<','<<camera.viewportWidth<<','<<camera.viewportHeight;
                CsvVector(s.cameras,camera.worldPosition);
                for (const auto& matrix:{camera.view,camera.projection}) for (int c=0;c<4;++c) for (int k=0;k<4;++k) s.cameras<<','<<matrix[c][k];
                s.cameras<<'\n';
            }
            s.frameObserverNs+=PD::Nanoseconds()-start;
            if (!s.draws || !s.cameras || !s.renderBindings) s.Fail("submitted frame evidence write failed");
        }
        void Bookmark(int inputFrame) noexcept
        {
            const auto start=PD::Nanoseconds();
            if (!bookmarkCount) { bookmarkTick=tick; bookmarkFrame=frame; }
            ++bookmarkCount;
            bookmarks<<frame<<','<<tick<<','<<start<<','<<inputFrame<<','<<lastPresented<<','<<frame<<','<<frameExtracted<<','<<frameSubmitted<<'\n';
            bookmarks.flush(); if (!bookmarks) Fail("F10 bookmark write failed");
            frameObserverNs+=PD::Nanoseconds()-start;
        }
        void Submitted(bool success) noexcept
        { frameSubmitted=success; if (success && visible) lastPresented=frame; }
        void Detach() noexcept { if (PD::observer==&sink) PD::observer=nullptr; scene=nullptr; }
        bool Finish(bool clean)
        {
            endNs=PD::Nanoseconds(); Detach();
            if (!opened) return false;
            // Failure can occur before End. Preserve the value-only partial
            // tick after teardown, explicitly incomplete in the result.
            if (failed && !tickFlushed) for (std::size_t i=0;i<used;++i) {
                const auto& row=scratch[i];
                if (Retain(row)) WriteRecord(row);
            }
            for (auto* out:{&records,&initial,&timing,&updates,&draws,&cameras,&bookmarks,&renderBindings}) {
                out->flush(); if (!*out) Fail("final evidence flush failed"); out->close();
            }
            if (attendedGround.is_open()) { attendedGround.flush(); if (!attendedGround) Fail("conditional attended evidence flush failed"); attendedGround.close(); }
            if (conditionalAttended && (!complete || tick!=900 || attendedGroundRows!=900 || attendedMissed)) Fail("conditional attended fixed window or contact gate incomplete");
            std::ofstream out(std::filesystem::path(config.outputDirectory)/"result.json");
            out<<std::setprecision(17)<<"{\"schema\":1,\"normal_60hz_verified\":"<<(startupVerified?"true":"false")
                <<",\"initial_step_seconds\":"<<initialStep<<",\"initial_solver_passes\":"<<initialPasses
                <<",\"initial_gravity\":["<<initialGravity.x<<','<<initialGravity.y<<','<<initialGravity.z<<']'
                <<",\"sleeping_enabled\":"<<(sleepingEnabled?"true":"false")
                <<",\"render_interpolation_enabled\":"<<(interpolated?"true":"false")<<",\"capture_complete\":"<<(complete?"true":"false")
                <<",\"technical_failure\":"<<(failed?"true":"false")<<",\"shutdown\":"<<(clean?"true":"false")
                <<",\"ticks\":"<<tick<<",\"frames\":"<<frame<<",\"target\":"<<target<<",\"ground\":"<<ground
                <<",\"first_ground_tick\":"<<firstGround<<",\"bookmarks\":"<<bookmarkCount<<",\"bookmark_tick\":"<<bookmarkTick
                <<",\"target_draws\":"<<targetDraws<<",\"records\":"<<retained<<",\"peak_scratch_bytes\":"<<peakBytes
                <<",\"operation_observer_ns\":"<<sink.observationNs<<",\"boundary_observer_ns\":"<<totalBoundaryNs
                <<",\"frame_observer_ns\":"<<frameObserverNs<<",\"total_discarded_seconds\":"<<std::setprecision(17)<<totalDiscarded
                <<",\"pending_seconds\":"<<pending<<",\"begin_ns\":"<<beginNs<<",\"end_ns\":"<<endNs
                <<",\"stop_reason\":"<<std::quoted(stopReason)<<",\"failure\":"<<std::quoted(failure)
                <<",\"conditional_attended\":"<<(conditionalAttended?"true":"false")<<",\"profiling_enabled\":"<<(IsPhysicsProfilingEnabled()?"true":"false")
                <<",\"attended_ground_rows\":"<<attendedGroundRows<<",\"attended_eligible_awake_ground\":"<<attendedEligible<<",\"attended_missed_awake_ground\":"<<attendedMissed
                <<",\"off_on_equivalence_measured\":false}\n";
            const bool replayOk=FinishReplay(clean);
            return clean && !failed && bool(out) && replayOk;
        }
    };
    inline std::unique_ptr<TargetSession> targetSession;
}
