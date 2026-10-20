#pragma once
#ifdef GE_ENABLE_COUPLED_SOLVER_REPLAY
#ifndef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
#error Coupled solver replay requires the coupled collision observer definition
#endif

#include "CoupledLosslessStream.h"
#include "CoupledCollisionObservation.h"
#include "GEngine/Physics/Manifold.h"
#include "GEngine/Physics/PhysicsWorld.h"
#include "GEngine/Physics/Shape.h"
#include "GEngine/Physics/ShapeBox.h"
#include "GEngine/Physics/Broadphase.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <limits>
#include <span>
#include <type_traits>

namespace Rbs::CoupledReplay
{
    using namespace GEngine;
    namespace PD=::GEngine::PhysicsDiagnostic;
    struct Wire {
        std::array<unsigned char,4096> bytes{};std::size_t used{};bool valid{true};
        template<class T> void Put(T value) noexcept {
            static_assert(std::is_arithmetic_v<T>);
            if(used+sizeof(T)>bytes.size()){valid=false;return;}
            std::memcpy(bytes.data()+used,&value,sizeof(T));used+=sizeof(T);
        }
        void Vec(const Vec3f& v) noexcept {Put(v.x);Put(v.y);Put(v.z);}
        void State(const PD::BodyState& s) noexcept {
            Vec(s.position);for(float x:{s.orientation.w,s.orientation.x,s.orientation.y,s.orientation.z})Put(x);
            Vec(s.linear);Vec(s.angular);Put(s.inactive);Put(std::uint8_t(s.sleeping));
        }
        void Contact(const contact_t& c) noexcept {
            for(const auto& v:{c.ptOnA_WorldSpace,c.ptOnB_WorldSpace,c.ptOnA_LocalSpace,c.ptOnB_LocalSpace,c.normal})Vec(v);
            Put(c.separationDistance);Put(c.timeOfImpact);Put(std::uint64_t(c.featureA));Put(std::uint64_t(c.featureB));
        }
        bool Same(const Wire& other) const noexcept {
            return valid&&other.valid&&used==other.used&&std::memcmp(bytes.data(),other.bytes.data(),used)==0;
        }
    };
}

namespace GEngine::PhysicsDiagnostic
{
    // Only the diagnostic executable grants access. No layout or normal-path
    // operation changes; complete copied constraints retain their private state.
    struct CoupledSolverReplayAccess {
        static int Count(const Manifold& m) noexcept {return m.m_NumContacts;}
        static RigidBody3D* A(const Manifold& m) noexcept {return m.m_BodyA;}
        static RigidBody3D* B(const Manifold& m) noexcept {return m.m_BodyB;}
        static ConstraintPenetration& ConstraintAt(Manifold& m,int slot) noexcept {return m.m_Constraints[slot];}
        static const contact_t& ContactAt(const Manifold& m,int slot) noexcept {return m.m_Contacts[slot];}
        static void FixtureNormalB(Manifold& m,int slot,const Vec3f& value) noexcept {m.m_NormalB[slot]=value;}
        static void FixtureAnchorB(Manifold& m,int slot,const Vec3f& value) noexcept {m.m_Contacts[slot].ptOnB_LocalSpace=value;m.m_Constraints[slot].m_anchorB=value;}
        static void FixturePatch(Manifold& m,RigidBody3D& a,RigidBody3D& b,const contact_t (&contacts)[4]) noexcept {
            m.m_BodyA=&a;m.m_BodyB=&b;m.m_NumContacts=4;
            for(int i=0;i<4;++i)m.WriteContact(i,contacts[i],nullptr);
        }
        template<class Map> static bool Remap(Manifold& m,Map map) noexcept {
            if(m.m_NumContacts<1||m.m_NumContacts>4)return false;
            m.m_BodyA=map(m.m_BodyA);m.m_BodyB=map(m.m_BodyB);
            if(!m.m_BodyA||!m.m_BodyB)return false;
            for(int s=0;s<4;++s){
                auto& c=m.m_Contacts[s];auto& q=m.m_Constraints[s];
                if(c.m_BodyA)c.m_BodyA=map(c.m_BodyA);if(c.m_BodyB)c.m_BodyB=map(c.m_BodyB);
                if(q.m_bodyA)q.m_bodyA=map(q.m_bodyA);if(q.m_bodyB)q.m_bodyB=map(q.m_bodyB);
                if(s<m.m_NumContacts&&(!c.m_BodyA||!c.m_BodyB||!q.m_bodyA||!q.m_bodyB))return false;
            }
            return true;
        }
        template<class Index> static Rbs::CoupledReplay::Wire Image(const Manifold& m,Index index) noexcept {
            Rbs::CoupledReplay::Wire w;
            w.Put(std::uint16_t(index(m.m_BodyA)));w.Put(std::uint16_t(index(m.m_BodyB)));w.Put(std::uint8_t(m.m_NumContacts));
            for(const auto* stamp:{&m.m_StampA,&m.m_StampB}){
                w.Put(stamp->identity.GetSlot());w.Put(stamp->identity.GetGeneration());w.Put(stamp->shapeRevision);
            }
            for(int s=0;s<m.m_NumContacts;++s){
                const auto& c=m.m_Contacts[s];const auto& q=m.m_Constraints[s];
                w.Put(std::uint16_t(index(c.m_BodyA)));w.Put(std::uint16_t(index(c.m_BodyB)));w.Contact(c);
                w.Put(std::uint16_t(index(q.m_bodyA)));w.Put(std::uint16_t(index(q.m_bodyB)));
                w.Vec(q.m_anchorA);w.Vec(q.m_anchorB);w.Vec(q.m_Normal);w.Vec(m.m_NormalB[s]);w.Put(q.m_Friction);
                // Derived-data getters run on copies, including when comparing
                // live state. Export the exact native fixed-pose operands.
                RigidBody3D a=*q.m_bodyA,b=*q.m_bodyB;
                w.Vec(a.GetBodyToWorldRotation()*q.m_Normal);
                w.Vec(a.BodySpaceToWorldSpace(q.m_anchorA));w.Vec(b.BodySpaceToWorldSpace(q.m_anchorB));
                for(int j=0;j<3;++j)w.Put(float(q.m_CachedLambda[j]));
                for(int r=0;r<3;++r)for(int col=0;col<12;++col)w.Put(float(q.m_Jacobian[r][col]));
                w.Put(q.GetSpinImpulse());const auto rolling=q.GetRollingImpulse();w.Put(rolling.x);w.Put(rolling.y);
            }
            return w;
        }
    };
}

namespace Rbs::CoupledReplay
{
    using Access=PD::CoupledSolverReplayAccess;
    struct Session {
        static constexpr unsigned TargetTick=603,PassLimit=32,MaxBodies=185,MaxManifolds=128,MaxEventsPerPass=2048;
        static constexpr std::size_t ReplayExportCap=1024*1024,MaximumOwnedBytes=4*1024*1024;
        static constexpr float Step=1.0f/60.0f;
        std::unique_ptr<RigidBody3D[]> bodies,prewarmBodies;
        ManifoldCollector collector,prewarmCollector;
        std::unique_ptr<Wire[]> initialCache,firstCache;
        std::unique_ptr<unsigned char[]> liveBodies,liveManifolds;
        std::array<RigidBody3D*,MaxBodies> originals{};
        std::array<PD::BodyState,MaxBodies> initialStates{},firstStates{};
        std::array<std::uint64_t,MaxBodies> shapeRevisions{};
        CoupledDiagnosis::LosslessStream output;
        std::ofstream stream;
        PD::Observer observer{};
        std::size_t* combined{};std::size_t sharedLimit{},manifoldCount{},guardSize{},guardCapacity{};
        const Manifold* guardData{};
        unsigned bodyCount{},branch{},pass{},eventCount{},totalEvents{},completedPasses{};
        std::uint64_t elapsedNs{};
        bool opened{},prewarmSaved{},ran{},liveParity{},warmParity{},controlParity{},donorsApplied{},failed{},finished{};
        const char* failure="";

        void Fail(const char* reason) noexcept {if(!failed){failed=true;failure=reason;}}
        bool Good() const noexcept {return !failed;}
        bool Complete() const noexcept {return ran&&liveParity&&warmParity&&controlParity&&donorsApplied&&completedPasses==PassLimit&&!failed;}
        std::size_t OwnedBytes() const noexcept {
            return sizeof(Session)+2*MaxBodies*sizeof(RigidBody3D)+2*MaxManifolds*sizeof(Wire)+
                MaxBodies*sizeof(RigidBody3D)+MaxManifolds*sizeof(Manifold)+
                (collector.m_Manifolds.capacity()+prewarmCollector.m_Manifolds.capacity())*sizeof(Manifold);
        }
        unsigned Index(const RigidBody3D* p) const noexcept {
            for(unsigned i=0;i<bodyCount;++i)if(p==originals[i]||p==&bodies[i]||p==&prewarmBodies[i])return i;
            return MaxBodies;
        }
        RigidBody3D* Remapped(const RigidBody3D* p) noexcept {const auto i=Index(p);return i<bodyCount?&bodies[i]:nullptr;}
        Wire Image(const Manifold& m) const noexcept {return Access::Image(m,[&](const auto* p){return Index(p);});}
        std::size_t Limit() const noexcept {
            if(output.physicalBytes>ReplayExportCap)return *combined;
            return std::min(sharedLimit,*combined+ReplayExportCap-std::size_t(output.physicalBytes));
        }
        bool Write(std::uint8_t kind,const Wire& raw) noexcept {
            if(failed)return false;if(!raw.valid){Fail("replay record overflow");return false;}
            std::array<unsigned char,8192> packed{};std::size_t n=0,i=0;
            while(i<raw.used){const bool zero=raw.bytes[i]==0;std::size_t end=i+1;
                while(end<raw.used&&end-i<128&&(raw.bytes[end]==0)==zero)++end;
                packed[n++]=static_cast<unsigned char>(zero?end-i-1:end-i+127);
                if(!zero){std::memcpy(packed.data()+n,raw.bytes.data()+i,end-i);n+=end-i;}i=end;}
            if(!output.WriteRecord(kind,std::uint16_t(raw.used),packed.data(),n,stream,*combined,Limit())){
                Fail("replay lossless/export bound failure");return false;}return true;
        }
        bool Open(const std::filesystem::path& path,std::size_t& exportBytes,std::size_t exportLimit) {
            if(opened||std::filesystem::exists(path)){Fail("replay path already exists");return false;}
            combined=&exportBytes;sharedLimit=exportLimit;
            if(exportBytes>exportLimit||exportLimit-exportBytes<8){Fail("replay magic exceeds shared cap");return false;}
            bodies.reset(new(std::nothrow) RigidBody3D[MaxBodies]);prewarmBodies.reset(new(std::nothrow) RigidBody3D[MaxBodies]);
            initialCache.reset(new(std::nothrow) Wire[MaxManifolds]);firstCache.reset(new(std::nothrow) Wire[MaxManifolds]);
            liveBodies.reset(new(std::nothrow) unsigned char[MaxBodies*sizeof(RigidBody3D)]);
            liveManifolds.reset(new(std::nothrow) unsigned char[MaxManifolds*sizeof(Manifold)]);
            if(!bodies||!prewarmBodies||!initialCache||!firstCache||!liveBodies||!liveManifolds){Fail("replay bounded allocation failed");return false;}
            try {collector.m_Manifolds.reserve(MaxManifolds);prewarmCollector.m_Manifolds.reserve(MaxManifolds);}catch(...){Fail("replay collector allocation failed");return false;}
            if(collector.m_Manifolds.capacity()>MaxManifolds||prewarmCollector.m_Manifolds.capacity()>MaxManifolds||OwnedBytes()>MaximumOwnedBytes){Fail("replay owned memory bound");return false;}
            stream.open(path,std::ios::binary);stream.write("P20CSR01",8);if(!stream){Fail("replay open failed");return false;}exportBytes+=8;
            observer.context=this;observer.detailed=observer.targeted=true;observer.event=Event;observer.valid=Valid;observer.violation=Violation;
            opened=true;return true;
        }
        static bool Valid(void* context,const RigidBody3D* a,const RigidBody3D* b) noexcept {
            auto& s=*static_cast<Session*>(context);
            auto clone=[&](const auto* p){for(unsigned i=0;i<s.bodyCount;++i)if(p==&s.bodies[i])return true;return false;};
            if(!clone(a)||!clone(b)){s.Fail("replay event escaped cloned bodies");return false;}return !s.failed;
        }
        static void Violation(void* context) noexcept {static_cast<Session*>(context)->Fail("replay observer changed clone state");}
        static void Event(void* context,const PD::Event& e) noexcept {
            auto& s=*static_cast<Session*>(context);if(s.failed)return;
            if(++s.eventCount>MaxEventsPerPass){s.Fail("replay event count overflow");return;}++s.totalEvents;
            Wire w;w.Put(std::uint8_t(s.branch));w.Put(std::uint8_t(s.pass));w.Put(std::uint16_t(s.eventCount));
            w.Put(std::uint8_t(e.operation));w.Put(std::uint16_t(s.Index(e.a)));w.Put(std::uint16_t(s.Index(e.b)));w.Put(std::int32_t(e.slot));
            w.Put(std::uint8_t(e.appliedImpulsesKnown));w.State(e.beforeA);w.State(e.beforeB);w.State(e.afterA);w.State(e.afterB);
            for(const auto& v:{e.normal,e.anchorA,e.anchorB,e.localA,e.localB,e.tangentU,e.tangentV,e.linearImpulseA,e.linearImpulseB,e.angularImpulseA,e.angularImpulseB})w.Vec(v);
            for(const auto& values:{e.accumulatedBefore,e.accumulatedAfter})for(double value:values)w.Put(value);
            for(const auto& values:{e.normalRow,e.tangentRowU,e.tangentRowV})for(double value:values)w.Put(value);
            w.Put(e.restitution);w.Put(e.closingSpeed);w.Put(e.penetration);w.Put(e.bias);w.Put(e.time);w.Put(e.duration);s.Write(23,w);
        }
        struct ObserveScope {
            PD::Observer* oldObserver{PD::observer};PD::QueryObserver* oldQuery{PD::queryObserver};
            PD::Coupled::Sink* oldCoupled{PD::Coupled::sink};
#ifdef GE_ENABLE_PHYSICS_PROFILING
            PhysicsProfileSnapshot profile{PhysicsProfileDetail::MutableSnapshot()};
#endif
            explicit ObserveScope(Session& s) noexcept {PD::observer=&s.observer;PD::queryObserver=nullptr;PD::Coupled::sink=nullptr;}
            ~ObserveScope(){PD::observer=oldObserver;PD::queryObserver=oldQuery;PD::Coupled::sink=oldCoupled;
#ifdef GE_ENABLE_PHYSICS_PROFILING
                PhysicsProfileDetail::MutableSnapshot()=profile;
#endif
            }
        };
        bool Bind(const PhysicsWorld& world,const ManifoldCollector& m) noexcept {
            const auto& list=world.GetPhysicsBodies();
            if(list.size()!=22||list.size()>MaxBodies||m.m_Manifolds.size()>MaxManifolds){Fail("replay requires bounded normal SphereBoxStack");return false;}
            if(bodyCount&&bodyCount!=list.size()){Fail("replay body membership changed");return false;}
            bodyCount=unsigned(list.size());
            for(unsigned i=0;i<bodyCount;++i){if(originals[i]&&originals[i]!=list[i]){Fail("replay body order changed");return false;}originals[i]=list[i];
                if(!list[i]||!list[i]->m_Shape||list[i]->GetSpinResistanceLength()!=0||list[i]->GetRollingResistanceLength()!=0){Fail("replay body/resistance mismatch");return false;}}
            for(const auto& v:m.m_Manifolds)if(Access::Count(v)<1||Access::Count(v)>4){Fail("replay malformed manifold");return false;}
            return true;
        }
        void Guard(const ManifoldCollector& m) noexcept {
            guardData=m.m_Manifolds.data();guardSize=m.m_Manifolds.size();guardCapacity=m.m_Manifolds.capacity();
            for(unsigned i=0;i<bodyCount;++i){std::memcpy(liveBodies.get()+i*sizeof(RigidBody3D),originals[i],sizeof(RigidBody3D));shapeRevisions[i]=originals[i]->m_Shape->GetRevision();}
            if(guardSize)std::memcpy(liveManifolds.get(),guardData,guardSize*sizeof(Manifold));
        }
        void CheckGuard(const ManifoldCollector& m) noexcept {
            if(m.m_Manifolds.data()!=guardData||m.m_Manifolds.size()!=guardSize||m.m_Manifolds.capacity()!=guardCapacity){Fail("replay changed live collector storage");return;}
            if(guardSize&&std::memcmp(liveManifolds.get(),guardData,guardSize*sizeof(Manifold)))Fail("replay changed live manifold bytes");
            for(unsigned i=0;i<bodyCount;++i)if(std::memcmp(liveBodies.get()+i*sizeof(RigidBody3D),originals[i],sizeof(RigidBody3D))||originals[i]->m_Shape->GetRevision()!=shapeRevisions[i])Fail("replay changed live body/shape bytes");
        }
        bool Clone(const ManifoldCollector& from,const RigidBody3D* saved=nullptr) noexcept {
            for(unsigned i=0;i<bodyCount;++i)bodies[i]=saved?saved[i]:*originals[i];
            collector.m_Manifolds.assign(from.m_Manifolds.begin(),from.m_Manifolds.end());
            for(auto& m:collector.m_Manifolds)if(!Access::Remap(m,[&](const auto* p){return Remapped(p);})){Fail("replay pointer remap failed");return false;}
            return true;
        }
        void Save(std::array<PD::BodyState,MaxBodies>& states,Wire* cache) noexcept {
            for(unsigned i=0;i<bodyCount;++i)states[i]=PD::Capture(bodies[i]);
            for(std::size_t i=0;i<collector.m_Manifolds.size();++i)cache[i]=Image(collector.m_Manifolds[i]);
        }
        bool Match(const ManifoldCollector& m,const std::array<PD::BodyState,MaxBodies>& states,const Wire* cache,bool live) const noexcept {
            if(m.m_Manifolds.size()!=manifoldCount)return false;
            for(unsigned i=0;i<bodyCount;++i){Wire a,b;a.State(states[i]);b.State(PD::Capture(live?*originals[i]:bodies[i]));if(!a.Same(b))return false;}
            for(std::size_t i=0;i<manifoldCount;++i)if(!cache[i].Same(Image(m.m_Manifolds[i])))return false;
            return true;
        }
        void Snapshot(unsigned phase) noexcept {
            Wire start;start.Put(std::uint8_t(branch));start.Put(std::uint8_t(pass));start.Put(std::uint8_t(phase));start.Put(std::uint16_t(bodyCount));start.Put(std::uint16_t(collector.m_Manifolds.size()));start.Put(std::uint16_t(eventCount));Write(20,start);
            for(unsigned i=0;i<bodyCount;++i){Wire w;w.Put(std::uint8_t(branch));w.Put(std::uint8_t(pass));w.Put(std::uint8_t(phase));w.Put(std::uint16_t(i));w.State(PD::Capture(bodies[i]));
                RigidBody3D copy=bodies[i];const auto inverse=copy.GetInverseInertiaTensorWorldSpace();const auto rotation=copy.GetBodyToWorldRotation();
                for(int col=0;col<3;++col)for(int row=0;row<3;++row)w.Put(inverse[col][row]);
                for(int col=0;col<3;++col)for(int row=0;row<3;++row)w.Put(rotation[col][row]);
                w.Vec(copy.GetCenterOfMassWorldSpace());Write(21,w);}
            for(std::size_t i=0;i<collector.m_Manifolds.size();++i){auto image=Image(collector.m_Manifolds[i]);Wire w;w.Put(std::uint8_t(branch));w.Put(std::uint8_t(pass));w.Put(std::uint8_t(phase));w.Put(std::uint16_t(i));
                for(std::size_t j=0;j<image.used;++j)w.Put(image.bytes[j]);Write(22,w);}
        }
        void BeforeWarmStart(unsigned tick,const PhysicsWorld& world,ManifoldCollector& m) noexcept {
            if(tick!=TargetTick||failed)return;if(!opened||prewarmSaved||!Bind(world,m)){Fail("replay prewarm admission failed");return;}
            Guard(m);for(unsigned i=0;i<bodyCount;++i)prewarmBodies[i]=*originals[i];
            prewarmCollector.m_Manifolds.assign(m.m_Manifolds.begin(),m.m_Manifolds.end());prewarmSaved=true;CheckGuard(m);
        }
        bool InstallDonors() noexcept {
            struct Donor {unsigned a,b,slot;std::uint64_t fa,fb;std::array<std::uint32_t,3> anchorA,anchorB,normal,lambda;};
            // Frozen exact recipients/WriteContact outputs from donor-warmstart-603.json.
            const std::array<Donor,3> donors{{
                {12,20,2,50,64,{1065353217u,3212836863u,1056558794u},{1064908004u,1065353215u,1064364859u},{3121286177u,3212834896u,3162165312u},{1016567679u,989985625u,974099550u}},
                {12,20,3,48,65,{3212378407u,3212836864u,1057908406u},{3212836864u,1065353214u,1065026847u},{3121286177u,3212834896u,3162165312u},{1023107137u,3119389580u,986120064u}},
                {12,8,2,68,48,{1065353213u,1065353218u,3176563762u},{1062529330u,3212836865u,3182249848u},{991666745u,1065348524u,1019265317u},{1015282899u,990022895u,996523234u}}
            }};
            for(const auto& donor:donors){ConstraintPenetration* found=nullptr;unsigned matches=0;
                for(auto& m:collector.m_Manifolds){if(Index(Access::A(m))!=donor.a||Index(Access::B(m))!=donor.b||donor.slot>=unsigned(Access::Count(m)))continue;
                    auto& q=Access::ConstraintAt(m,int(donor.slot));const auto& c=Access::ContactAt(m,int(donor.slot));
                    if(c.featureA!=donor.fa||c.featureB!=donor.fb)continue;
                    bool exact=true;for(unsigned j=0;j<3;++j)exact&=std::bit_cast<std::uint32_t>(q.m_anchorA[j])==donor.anchorA[j]&&std::bit_cast<std::uint32_t>(q.m_anchorB[j])==donor.anchorB[j]&&std::bit_cast<std::uint32_t>(q.m_Normal[j])==donor.normal[j]&&std::bit_cast<std::uint32_t>(float(q.m_CachedLambda[j]))==0;
                    if(exact){found=&q;++matches;}}
                if(matches!=1||!found){Fail("replay frozen donor recipient mismatch");return false;}
                for(unsigned j=0;j<3;++j)found->m_CachedLambda[j]=std::bit_cast<float>(donor.lambda[j]);
            }
            donorsApplied=true;return true;
        }
        void BeforeSolve(unsigned tick,const PhysicsWorld& world,ManifoldCollector& m) noexcept {
            if(tick!=TargetTick||failed)return;
            if(!prewarmSaved||ran||!Bind(world,m)||PD::Coupled::CurrentContext()){Fail("replay Islands admission failed");return;}
            const auto begin=PD::Nanoseconds();Guard(m);manifoldCount=m.m_Manifolds.size();
            {
                ObserveScope observe(*this);branch=0;pass=eventCount=0;
                if(Clone(m)){Save(initialStates,initialCache.get());Snapshot(0);}
                for(pass=1;pass<=PassLimit&&!failed;++pass){eventCount=0;collector.Solve();Snapshot(2);if(pass==1)Save(firstStates,firstCache.get());completedPasses=pass;}
                if(!failed&&Clone(prewarmCollector,prewarmBodies.get())){
                    branch=1;pass=eventCount=0;Snapshot(0);collector.PreSolve(Step);Snapshot(1);
                    warmParity=Match(collector,initialStates,initialCache.get(),false);
                    if(!warmParity)Fail("replay prewarm control differs at Islands");
                    if(!failed){pass=1;eventCount=0;collector.Solve();Snapshot(2);controlParity=Match(collector,firstStates,firstCache.get(),false);if(!controlParity)Fail("replay prewarm control differs at Solve");}
                }
                if(!failed&&Clone(prewarmCollector,prewarmBodies.get())){
                    branch=2;pass=eventCount=0;if(InstallDonors()){Snapshot(0);collector.PreSolve(Step);Snapshot(1);pass=1;eventCount=0;collector.Solve();Snapshot(2);}
                }
            }
            ran=true;CheckGuard(m);elapsedNs+=PD::Nanoseconds()-begin;
        }
        void AfterSolve(unsigned tick,const PhysicsWorld& world,ManifoldCollector& m) noexcept {
            if(tick!=TargetTick||failed)return;if(!ran||!Bind(world,m)){Fail("replay live parity admission failed");return;}
            const auto begin=PD::Nanoseconds();Guard(m);
            {ObserveScope observe(*this);liveParity=Match(m,firstStates,firstCache.get(),true);}
            CheckGuard(m);elapsedNs+=PD::Nanoseconds()-begin;if(!liveParity)Fail("replay primary pass1 differs from live Solve");
        }
        bool Finish() noexcept {
            if(finished)return Complete();finished=true;
            if(!opened){Fail("replay was not opened");return false;}
            if(!Complete())Fail("replay incomplete or parity failed");
            if(!output.Flush(stream,*combined,Limit()))Fail("replay final flush failed");stream.flush();if(!stream)Fail("replay final stream failure");stream.close();return Complete();
        }
        void Metadata(std::ostream& out) const {
            out<<"{\"target_tick\":603,\"diagnostic_traversal_bound\":32,\"production_cap_choice\":false,\"complete\":"<<(Complete()?"true":"false")
                <<",\"good\":"<<(Good()?"true":"false")<<",\"live_pass1_bit_parity\":"<<(liveParity?"true":"false")<<",\"prewarm_control_islands_bit_parity\":"<<(warmParity?"true":"false")
                <<",\"prewarm_control_solve_bit_parity\":"<<(controlParity?"true":"false")<<",\"donor_branch_applied\":"<<(donorsApplied?"true":"false")
                <<",\"completed_primary_passes\":"<<completedPasses<<",\"events\":"<<totalEvents<<",\"owned_bytes\":"<<OwnedBytes()<<",\"maximum_owned_bytes\":"<<MaximumOwnedBytes
                <<",\"replay_export_limit\":"<<ReplayExportCap<<",\"elapsed_ns\":"<<elapsedNs<<",\"failure\":\""<<failure<<"\",\"stream\":";output.Metadata(out);out<<'}';
        }
    };
    // Focused contact-cache lifecycle checks. These construct contacts on copied
    // runtime boxes; they do not run a second world or change any live body.
    struct LifecycleFixtures {
        static constexpr std::size_t MaximumScratchBytes=64*1024;
        struct Check {const char* name{};bool passed{};};
        struct NormalInput {
            unsigned tick,body[2];std::uint32_t state[2][13],properties[2][19],point[4][58];
        };
        struct NormalResult {
            unsigned tick{},body[2]{},blockEvents{};
            bool inputsMatch{},baselineFeasible{},candidateFeasible{},objectivePassed{},liveUnchanged{},observerValid{true};
            double K[4][4]{},rhs[4]{},old[4]{},baseline[4]{},candidate[4]{};
            double tolerance{},baselineScore{},candidateScore{},baselineResidual{},candidateResidual{},actualNormalResidual{};
            PD::BodyState afterA{},afterB{};
            const RigidBody3D* a{};const RigidBody3D* b{};
            bool Passed() const noexcept {return inputsMatch&&baselineFeasible&&candidateFeasible&&objectivePassed&&
                liveUnchanged&&observerValid&&blockEvents==4&&actualNormalResidual<=tolerance;}
        };
        // Exact float words from current-SphereBoxStack-04: verified complete
        // ticks1193(12/8),1171(4/20),1172(4/18). No missing tail data.
        // Point fields: localA/B, refresh solver normal, block normal, worldA/B,
        // three native Jacobian rows, warmed lambda, historical block lambdaN.
        static constexpr NormalInput NormalInputs[3]={
            {1193,{12,8},
                {{1066662447u,1079973007u,1049335835u,1065343379u,990611708u,3171626874u,3144553521u,3165002913u,3154095613u,1011640140u,1001813339u,983715732u,1008240282u},{1068732972u,1085238368u,1056877764u,1065326161u,985205945u,3177660192u,1002245648u,3175482583u,3151443104u,1021767676u,1005897072u,982789489u,1012914996u}},
                {{1065353216u,1056964608u,0u,0u,0u,0u,0u,1073741824u,1073741824u,1073741824u,1059760811u,0u,0u,0u,1059760811u,0u,0u,0u,1059760811u},{1065353216u,1056964608u,0u,0u,0u,0u,0u,1073741824u,1073741824u,1073741824u,1059760811u,0u,0u,0u,1059760811u,0u,0u,0u,1059760811u}},{
                    {3208997720u,1065353213u,1065353216u,3212836863u,3212836865u,1062757259u,1005138591u,1065352621u,999525147u,1005138592u,1065352621u,999525147u,1051122244u,1083152367u,1067219361u,1051117580u,1083111386u,1067218623u,3152622240u,3212836269u,3147008795u,1064435051u,3156999668u,1062623354u,1005138592u,1065352621u,999525147u,3208298678u,1008991809u,3213423205u,1065314028u,3153243720u,1032524410u,1033483738u,1065386548u,3212722422u,3212797676u,1005760072u,3180008058u,1031933424u,3209421260u,3213003231u,1032528562u,998464759u,3212797967u,3212863306u,3208873008u,3180474102u,3180012210u,3145948407u,1065314319u,3212912079u,1065566321u,3179562333u,1009163178u,3137702218u,3133620992u,1010596053u},
                    {3207606047u,1065353216u,3210209612u,3212836863u,3212836865u,3212836864u,1005138591u,1065352621u,999525147u,1005138592u,1065352621u,999525147u,1057532956u,1083166577u,3206311116u,1057530579u,1083124816u,3206312618u,3152622240u,3212836269u,3147008795u,3210960992u,996444896u,1059134757u,1005138592u,1065352621u,999525147u,1066225290u,3145876691u,3210520949u,1065314028u,3153243720u,1032524410u,1031647140u,3210141853u,3212849272u,3212797676u,1005760072u,3180008058u,1033697592u,1065746855u,3212936715u,1032528562u,998464759u,3212797967u,3212858356u,3207481385u,3180423974u,3180012210u,3145948407u,1065314319u,3212920142u,1064387849u,3179615852u,0u,0u,0u,0u},
                    {1048979278u,1065353213u,1065353217u,1023699683u,3212836865u,1061978112u,1005138591u,1065352621u,999525147u,1005138592u,1065352621u,999525147u,1068345084u,1083136320u,1067807907u,1068345084u,1083136320u,1067807906u,3152622240u,3212836269u,3147008795u,1065482948u,3150988000u,3192268320u,1005138592u,1065352621u,999525147u,3209476304u,1002455930u,3173002376u,1065314028u,3153243720u,1032524410u,1033483739u,1065386549u,3212722422u,3212797676u,1005760072u,3180008058u,1031711682u,3209422575u,3212839681u,1032528562u,998464759u,3212797967u,3212766390u,1049227472u,3179849708u,3180012210u,3145948407u,1065314319u,3212783560u,3154201700u,3180008618u,1037065592u,984784564u,3150799869u,1036946272u},
                    {1052333969u,1065353214u,3209416821u,1028836980u,3212836866u,3212836864u,1005138591u,1065352621u,999525147u,1005138592u,1065352621u,999525147u,1070205422u,1083149868u,3204199093u,1070205422u,1083149868u,3204199092u,3152622240u,3212836269u,3147008795u,3208973019u,1005634399u,3201603529u,1005138592u,1065352621u,999525147u,1065109392u,3154141538u,1044053585u,1065314028u,3153243720u,1032524410u,1031737411u,3209349090u,3212847486u,3212797676u,1005760072u,3180008058u,1033470875u,1065346393u,3212706812u,1032528562u,998464759u,3212797967u,3212754454u,1052582044u,3179789289u,3180012210u,3145948407u,1065314319u,3212795495u,3185371328u,3180069037u,1035244370u,989729621u,1003039598u,1035194905u},
                }},
            {1171,{4,20},
                {{0u,0u,0u,1065353216u,0u,0u,0u,0u,0u,0u,0u,0u,0u},{1073193249u,1069478873u,3182175316u,1065352924u,988638246u,998320806u,998439912u,994937086u,987879439u,3134916738u,3150894791u,975589576u,3159188613u}},
                {{0u,1056964608u,0u,0u,0u,0u,0u,1120403456u,1065353216u,1120403456u,1146116779u,0u,0u,0u,1154504021u,0u,0u,0u,1146116779u},{1065353216u,1056964608u,0u,0u,0u,0u,0u,1073741824u,1073741824u,1073741824u,1059760811u,0u,0u,0u,1059760811u,0u,0u,0u,1059760811u}},{
                    {1064523517u,1056964608u,1064013112u,3212836863u,3212836862u,1065353216u,0u,1065353216u,0u,0u,1065353216u,0u,1064523517u,1056964608u,1064013112u,1064523518u,1056302088u,1064013112u,2147483648u,3212836864u,2147483648u,1064013112u,0u,3212007165u,0u,1065353216u,0u,3212871905u,0u,3212569412u,1065353216u,2147483648u,2147483648u,0u,1064013112u,3204448256u,3212836864u,0u,0u,2147483648u,3212871905u,3212933847u,2147483648u,2147483648u,3212836864u,3204448256u,1064523517u,0u,0u,0u,1065353216u,3212933847u,1065085764u,0u,1047004423u,3165330591u,3165193840u,1046583216u},
                    {1064258503u,1056964608u,3213506285u,3212836862u,3212836865u,3212836865u,0u,1065353216u,0u,0u,1065353216u,0u,1064258503u,1056964608u,3213506285u,1064258505u,1056543080u,3213506286u,2147483648u,3212836864u,2147483648u,3213506285u,0u,3211742151u,0u,1065353216u,0u,1065281874u,0u,3212834425u,1065353216u,2147483648u,2147483648u,2147483648u,3213506285u,3204448256u,3212836864u,0u,0u,0u,1065281874u,3212873599u,2147483648u,2147483648u,3212836864u,3204448256u,1064258503u,0u,0u,0u,1065353216u,3212873599u,1065350777u,0u,1046152890u,3168451217u,1016010539u,1046234273u},
                    {1077728174u,1056964608u,1063749070u,1065353215u,3212836864u,1065353216u,0u,1065353216u,0u,0u,1065353216u,0u,1077728174u,1056964608u,1063749070u,1077728174u,1056839542u,1063749070u,2147483648u,3212836864u,2147483648u,1063749070u,0u,3225211822u,0u,1065353216u,0u,3212642904u,0u,1065485883u,1065353216u,2147483648u,2147483648u,0u,1063749070u,3204448256u,3212836864u,0u,0u,2147483648u,3212642904u,3212762103u,2147483648u,2147483648u,3212836864u,3204448256u,1077728174u,0u,0u,0u,1065353216u,3212762103u,3212969531u,0u,1042723913u,1009263146u,1002906379u,1042948105u},
                    {1075852544u,1056964608u,3213609828u,1058115253u,3212836864u,3212836865u,0u,1065353216u,0u,0u,1065353216u,0u,1075852544u,1056964608u,3213609828u,1075852544u,1056964608u,3213609829u,2147483648u,3212836864u,2147483648u,3213609828u,0u,3223336192u,0u,1065353216u,0u,1065421088u,2147483648u,1058116030u,1065353216u,2147483648u,2147483648u,2147483648u,3213609828u,3204448256u,3212836864u,0u,0u,0u,1065421088u,3212699570u,2147483648u,2147483648u,3212836864u,3204448256u,1075852544u,0u,0u,0u,1065353216u,3212699570u,3205599678u,0u,0u,0u,0u,0u},
                }},
            {1172,{4,18},
                {{0u,0u,0u,1065353216u,0u,0u,0u,0u,0u,0u,0u,0u,0u},{1086189220u,1069471486u,3196362986u,1065351883u,3143692906u,1011145321u,3134824185u,975927232u,3111751536u,3084985088u,3119715264u,3113528064u,3095331840u}},
                {{0u,1056964608u,0u,0u,0u,0u,0u,1120403456u,1065353216u,1120403456u,1146116779u,0u,0u,0u,1154504021u,0u,0u,0u,1146116779u},{1065353216u,1056964608u,0u,0u,0u,0u,0u,1073741824u,1073741824u,1073741824u,1059760811u,0u,0u,0u,1059760811u,0u,0u,0u,1059760811u}},{
                    {1088329379u,1056964608u,1060714905u,1065353216u,3212836864u,1065353216u,0u,1065353216u,0u,0u,1065353216u,0u,1088329379u,1056964608u,1060714905u,1088329379u,1056776256u,1060714905u,2147483648u,3212836864u,2147483648u,1060714905u,0u,3235813027u,0u,1065353216u,0u,3212544526u,0u,1065525244u,1065353216u,2147483648u,2147483648u,0u,1060714905u,3204448256u,3212836864u,0u,0u,2147483648u,3212544526u,3212778972u,2147483648u,2147483648u,3212836864u,3204448256u,1088329379u,0u,0u,0u,1065353216u,3212778972u,3213008892u,0u,1036749216u,1017403884u,3158292110u,1036744407u},
                    {1084035480u,1056964608u,3214747848u,3212836865u,3212836863u,3212836864u,0u,1065353216u,0u,0u,1065353216u,0u,1084035480u,1056964608u,3214747848u,1084035480u,1056546640u,3214747848u,2147483648u,3212836864u,2147483648u,3214747848u,0u,3231519128u,0u,1065353216u,0u,1064829211u,0u,3213063216u,1065353216u,2147483648u,2147483648u,2147483648u,3214747848u,3204448256u,3212836864u,0u,0u,0u,1064829211u,3212865322u,2147483648u,2147483648u,3212836864u,3204448256u,1084035480u,0u,0u,0u,1065353216u,3212865322u,1065579568u,0u,1036163851u,1018308073u,1002166734u,1036176550u},
                    {1088228550u,1056964608u,3215150780u,1065353214u,3212836864u,3212836864u,0u,1065353216u,0u,0u,1065353216u,0u,1088228550u,1056964608u,3215150780u,1088228550u,1056318280u,3215150780u,2147483648u,3212836864u,2147483648u,3215150780u,0u,3235712198u,0u,1065353216u,0u,1065494146u,2147483648u,1064890640u,1065353216u,2147483648u,2147483648u,2147483648u,3215150780u,3204448256u,3212836864u,0u,0u,0u,1065494146u,3212922412u,2147483648u,2147483648u,3212836864u,3204448256u,1088228550u,0u,0u,0u,1065353216u,3212922412u,3212374288u,0u,995612622u,975178436u,3106045879u,996382323u},
                    {1084127501u,1056964608u,1058590427u,3212836862u,3212836864u,1062421959u,0u,1065353216u,0u,0u,1065353216u,0u,1084127501u,1056964608u,1058590427u,1084127501u,1056964608u,1058590427u,2147483648u,3212836864u,2147483648u,1058590427u,0u,3231611149u,0u,1065353216u,0u,3210420048u,0u,3212553400u,1065353216u,2147483648u,2147483648u,0u,1058590427u,3204448256u,3212836864u,0u,0u,2147483648u,3210420048u,3212684796u,2147483648u,2147483648u,3212836864u,3204448256u,1084127501u,0u,0u,0u,1065353216u,3212684796u,1065069752u,0u,0u,0u,0u,0u},
                }},
        };
        std::array<NormalResult,3> normalResults{};
        std::array<Check,32> checks{};
        unsigned count{},failures{};std::uint64_t elapsedNs{};std::size_t storageEstimate{};
        bool complete{},liveUnchanged{},ran{};const char* failure="";
        bool Good() const noexcept {return failures==0&&failure[0]=='\0';}
        bool Complete() const noexcept {return complete&&liveUnchanged&&Good();}
        std::size_t OwnedBytes() const noexcept {return sizeof(LifecycleFixtures);}
        void Add(const char* name,bool passed) noexcept {
            if(count==checks.size()){failure="fixture case capacity exceeded";return;}
            checks[count++]={name,passed};if(!passed)++failures;
        }
        struct ShapeCopy final : ShapeBox {
            Vec3f originalCenter{};
            explicit ShapeCopy(const ShapeBox& source):ShapeBox(source),originalCenter(GetCenterOfMass()){}
            std::size_t SourceCapacity() const noexcept {return m_MeshPoints.capacity();}
            void ChangeRevision() noexcept {MarkGeometryChanged();}
            void Invalidate() noexcept {m_CenterOfMass.x=std::numeric_limits<float>::quiet_NaN();}
            void Restore() noexcept {m_CenterOfMass=originalCenter;}
        };
        static float Float(std::uint32_t word) noexcept {return std::bit_cast<float>(word);}
        static bool Bits(float value,std::uint32_t word) noexcept {return std::bit_cast<std::uint32_t>(value)==word;}
        static Vec3f Vector(const std::uint32_t* words) noexcept {return Vec3f(Float(words[0]),Float(words[1]),Float(words[2]));}
        static bool VectorBits(const Vec3f& value,const std::uint32_t* words) noexcept {
            return Bits(value.x,words[0])&&Bits(value.y,words[1])&&Bits(value.z,words[2]);
        }
        static void SetPhysicalState(RigidBody3D& body,const std::uint32_t* words) noexcept {
            body.WakeUp();body.m_Position=Vector(words);body.m_Orientation=Quat(Float(words[3]),Float(words[4]),Float(words[5]),Float(words[6]));
            body.m_LinearVelocity=Vector(words+7);body.m_AngularVelocity=Vector(words+10);
        }
        static bool NormalValid(void* context,const RigidBody3D* a,const RigidBody3D* b) noexcept {
            auto& r=*static_cast<NormalResult*>(context);const bool valid=a==r.a&&b==r.b;r.observerValid&=valid;return valid;
        }
        static void NormalViolation(void* context) noexcept {static_cast<NormalResult*>(context)->observerValid=false;}
        static void NormalEvent(void* context,const PD::Event& e) noexcept {
            auto& r=*static_cast<NormalResult*>(context);
            if(e.operation!=PD::Operation::BlockNormal)return;
            if(e.slot<0||e.slot>=4||unsigned(e.slot)!=r.blockEvents){r.observerValid=false;return;}
            r.candidate[e.slot]=e.accumulatedAfter[0];++r.blockEvents;r.afterA=e.afterA;r.afterB=e.afterB;
        }
        static double NormalResidual(const double (&K)[4][4],const double (&rhs)[4],const double (&lambda)[4]) noexcept {
            double residual=0;
            for(int i=0;i<4;++i){
                if(!std::isfinite(lambda[i])||lambda[i]<0)return std::numeric_limits<double>::infinity();
                double value=-rhs[i];for(int j=0;j<4;++j)value+=K[i][j]*lambda[j];
                if(!std::isfinite(value))return std::numeric_limits<double>::infinity();
                residual=std::max(residual,std::max(-value,lambda[i]>0?std::abs(value):0.0));
            }return residual;
        }
        void RunNormalCase(const PhysicsWorld& world,unsigned which) {
            const auto& input=NormalInputs[which];auto& result=normalResults[which];
            result.tick=input.tick;result.body[0]=input.body[0];result.body[1]=input.body[1];
            const auto& worldBodies=world.GetPhysicsBodies();
            if(input.body[0]>=worldBodies.size()||input.body[1]>=worldBodies.size())return;
            const auto* originalA=worldBodies[input.body[0]];const auto* originalB=worldBodies[input.body[1]];
            if(!originalA||!originalB||!originalA->m_Shape||!originalB->m_Shape||
                originalA->m_Shape->GetShapeType()!=ShapeType::Box||originalB->m_Shape->GetShapeType()!=ShapeType::Box)return;
            std::array<unsigned char,2*sizeof(RigidBody3D)+2*sizeof(ShapeBox)> original{};
            std::memcpy(original.data(),originalA,sizeof(RigidBody3D));
            std::memcpy(original.data()+sizeof(RigidBody3D),originalB,sizeof(RigidBody3D));
            std::memcpy(original.data()+2*sizeof(RigidBody3D),originalA->m_Shape,sizeof(ShapeBox));
            std::memcpy(original.data()+2*sizeof(RigidBody3D)+sizeof(ShapeBox),originalB->m_Shape,sizeof(ShapeBox));
            RigidBody3D a=*originalA,b=*originalB;
            ShapeCopy shapeA(*static_cast<const ShapeBox*>(originalA->m_Shape)),shapeB(*static_cast<const ShapeBox*>(originalB->m_Shape));
            if(shapeA.SourceCapacity()>36||shapeB.SourceCapacity()>36)return;
            a.m_Shape=&shapeA;b.m_Shape=&shapeB;SetPhysicalState(a,input.state[0]);SetPhysicalState(b,input.state[1]);
            result.a=&a;result.b=&b;result.inputsMatch=true;
            RigidBody3D* copies[2]{&a,&b};
            for(int i=0;i<2;++i){const auto& body=*copies[i];const auto* expected=input.properties[i];
                const auto center=body.m_Shape->GetCenterOfMass();const auto bounds=body.m_Shape->GetBounds();const auto inertia=body.m_Shape->InertiaTensor();
                result.inputsMatch&=Bits(body.GetInverseMass(),expected[0])&&Bits(body.m_Friction,expected[1])&&
                    body.Type==(expected[0]==0?BodyType::Static:BodyType::Dynamic)&&
                    Bits(body.GetSpinResistanceLength(),expected[2])&&Bits(body.GetRollingResistanceLength(),expected[3])&&
                    expected[2]==0&&expected[3]==0&&VectorBits(center,expected+4)&&
                    Bits(bounds.WidthX(),expected[7])&&Bits(bounds.WidthY(),expected[8])&&Bits(bounds.WidthZ(),expected[9]);
                for(int c=0;c<3;++c)for(int r=0;r<3;++r)result.inputsMatch&=Bits(inertia[c][r],expected[10+3*c+r]);
            }
            Manifold manifold;contact_t contacts[4]{};
            for(int i=0;i<4;++i){const auto* p=input.point[i];auto& c=contacts[i];c.m_BodyA=&a;c.m_BodyB=&b;
                c.ptOnA_LocalSpace=Vector(p);c.ptOnB_LocalSpace=Vector(p+3);c.normal=-Vector(p+6);
                c.ptOnA_WorldSpace=Vector(p+12);c.ptOnB_WorldSpace=Vector(p+15);}
            Access::FixturePatch(manifold,a,b,contacts);
            glm::dvec3 normal[4],ra[4],rb[4],ja[4],jb[4];
            const glm::dmat3 IA(a.GetInverseInertiaTensorWorldSpace()),IB(b.GetInverseInertiaTensorWorldSpace());
            const double inverseMass=a.GetInverseMass()+b.GetInverseMass();
            for(int i=0;i<4;++i){const auto* p=input.point[i];auto& q=Access::ConstraintAt(manifold,i);
                q.m_Friction=1.0f;
                for(int r=0;r<3;++r)for(int c=0;c<12;++c)q.m_Jacobian[r][c]=Float(p[18+12*r+c]);
                for(int c=0;c<3;++c)q.m_CachedLambda[c]=Float(p[54+c]);
                const auto n=a.GetBodyToWorldRotation()*q.m_Normal;
                const auto anchorA=a.BodySpaceToWorldSpace(q.m_anchorA),anchorB=b.BodySpaceToWorldSpace(q.m_anchorB);
                result.inputsMatch&=VectorBits(n,p+9)&&VectorBits(anchorA,p+12)&&VectorBits(anchorB,p+15);
                normal[i]=n;ra[i]=glm::dvec3(anchorA)-glm::dvec3(a.GetCenterOfMassWorldSpace());
                rb[i]=glm::dvec3(anchorB)-glm::dvec3(b.GetCenterOfMassWorldSpace());ja[i]=glm::cross(ra[i],normal[i]);jb[i]=glm::cross(rb[i],normal[i]);
                result.old[i]=q.m_CachedLambda[0];result.baseline[i]=Float(p[57]);
                const auto relative=glm::dvec3(b.GetLinearVelocity())-glm::dvec3(a.GetLinearVelocity())+
                    glm::cross(glm::dvec3(b.GetAngularVelocity()),rb[i])-glm::cross(glm::dvec3(a.GetAngularVelocity()),ra[i]);
                result.rhs[i]=-glm::dot(normal[i],relative);
            }
            double velocityScale=1;
            for(int i=0;i<4;++i){for(int j=0;j<4;++j){result.K[i][j]=inverseMass*glm::dot(normal[i],normal[j])+
                    glm::dot(ja[i],IA*ja[j])+glm::dot(jb[i],IB*jb[j]);result.rhs[i]+=result.K[i][j]*result.old[j];
                    result.inputsMatch&=std::isfinite(result.K[i][j]);}
                result.inputsMatch&=std::isfinite(result.rhs[i]);
                velocityScale=std::max(velocityScale,std::abs(result.rhs[i]));}
            result.tolerance=1e-6*velocityScale;
            if(result.inputsMatch){
                PD::Observer observer{};observer.context=&result;observer.detailed=observer.targeted=true;
                observer.valid=NormalValid;observer.event=NormalEvent;observer.violation=NormalViolation;
                struct RestoreObserver {PD::Observer* saved{PD::observer};~RestoreObserver(){PD::observer=saved;}} restore;
                PD::observer=&observer;manifold.Solve();
            }
            result.baselineResidual=NormalResidual(result.K,result.rhs,result.baseline);
            result.candidateResidual=NormalResidual(result.K,result.rhs,result.candidate);
            result.baselineFeasible=result.baselineResidual<=result.tolerance;result.candidateFeasible=result.candidateResidual<=result.tolerance;
            bool unchanged=true;
            for(int i=0;i<4;++i){result.baselineScore+=(result.baseline[i]-result.old[i])*(result.baseline[i]-result.old[i]);
                result.candidateScore+=(result.candidate[i]-result.old[i])*(result.candidate[i]-result.old[i]);
                unchanged&=Bits(float(result.candidate[i]),input.point[i][57]);
                const auto relative=glm::dvec3(result.afterB.linear)-glm::dvec3(result.afterA.linear)+
                    glm::cross(glm::dvec3(result.afterB.angular),rb[i])-glm::cross(glm::dvec3(result.afterA.angular),ra[i]);
                const double vn=glm::dot(normal[i],relative);
                result.observerValid&=std::isfinite(vn);
                result.actualNormalResidual=std::max(result.actualNormalResidual,std::max(-vn,result.candidate[i]>0?std::abs(vn):0.0));}
            result.objectivePassed=which<2?result.candidateScore<result.baselineScore:unchanged;
            result.liveUnchanged=std::memcmp(original.data(),originalA,sizeof(RigidBody3D))==0&&
                std::memcmp(original.data()+sizeof(RigidBody3D),originalB,sizeof(RigidBody3D))==0&&
                std::memcmp(original.data()+2*sizeof(RigidBody3D),originalA->m_Shape,sizeof(ShapeBox))==0&&
                std::memcmp(original.data()+2*sizeof(RigidBody3D)+sizeof(ShapeBox),originalB->m_Shape,sizeof(ShapeBox))==0;
            result.a=result.b=nullptr;
        }
        struct Work {
            const RigidBody3D& sourceA;const RigidBody3D& sourceB;
            RigidBody3D a,b;ShapeCopy shapeA,shapeB;
            ManifoldCollector cache,strict;
            std::array<contact_t,2> patch{};
            float top{},bottom{},span{};
            Work(const RigidBody3D& aa,const RigidBody3D& bb):sourceA(aa),sourceB(bb),a(aa),b(bb),
                shapeA(*static_cast<const ShapeBox*>(aa.m_Shape)),shapeB(*static_cast<const ShapeBox*>(bb.m_Shape)) {
                cache.m_Manifolds.reserve(2);strict.m_Manifolds.reserve(2);
                top=shapeA.GetBounds().maxs.y-shapeA.GetCenterOfMass().y;
                bottom=shapeB.GetCenterOfMass().y-shapeB.GetBounds().mins.y;
                span=.25f*std::min(shapeA.GetBounds().WidthX(),shapeB.GetBounds().WidthX());
            }
            void ResetBodies() noexcept {
                shapeA.Restore();shapeB.Restore();a=sourceA;b=sourceB;
                a.m_Shape=&shapeA;b.m_Shape=&shapeB;a.m_Orientation=b.m_Orientation=Quat(1,0,0,0);
                a.m_Position=-shapeA.GetCenterOfMass();b.m_Position=Vec3f(0,top+bottom,0)-shapeB.GetCenterOfMass();
                a.m_LinearVelocity=b.m_LinearVelocity=a.m_AngularVelocity=b.m_AngularVelocity=Vec3f(0);
                a.WakeUp();b.WakeUp();
            }
            contact_t Contact(float x,float gap,std::uint32_t fa,std::uint32_t fb) noexcept {
                contact_t c{};c.m_BodyA=&a;c.m_BodyB=&b;
                c.ptOnA_LocalSpace=Vec3f(x,top,0);c.ptOnB_LocalSpace=Vec3f(x,-bottom+gap,0);
                c.ptOnA_WorldSpace=a.BodySpaceToWorldSpace(c.ptOnA_LocalSpace);
                c.ptOnB_WorldSpace=b.BodySpaceToWorldSpace(c.ptOnB_LocalSpace);
                c.normal=Vec3f(0,-1,0);c.separationDistance=c.ptOnB_WorldSpace.y-c.ptOnA_WorldSpace.y;
                c.featureA=fa;c.featureB=fb;return c;
            }
            void Seed(float donorX,float survivorX,float donorGap=.001f,float survivorGap=0) noexcept {
                cache.Clear();strict.Clear();ResetBodies();
                const std::array<contact_t,2> old{{Contact(donorX,donorGap,64,48),Contact(survivorX,survivorGap,66,50)}};
                cache.AddContacts(old.data(),2);
                auto& m=cache.m_Manifolds[0];Access::ConstraintAt(m,0).m_CachedLambda[0]=1.25f;
                Access::ConstraintAt(m,1).m_CachedLambda[0]=.5f;
                patch={Contact(donorX,0,64,48),Contact(survivorX,0,66,50)};
            }
            void Seed() noexcept {Seed(-span,span);}
            bool PrepareWithControl() noexcept {
                strict.m_Manifolds.assign(cache.m_Manifolds.begin(),cache.m_Manifolds.end());
                strict.RemoveExpired();return cache.PrepareContactRefresh();
            }
            bool Refresh() noexcept {return cache.RefreshContactPatch(&a,&b,patch.data(),2);}
            int Count() const noexcept {return cache.GetContactCount();}
            float Lambda(int slot) noexcept {return Access::ConstraintAt(cache.m_Manifolds[0],slot).m_CachedLambda[0];}
            bool GeometryMatchesControl() const noexcept {
                if(cache.m_Manifolds.size()!=strict.m_Manifolds.size())return false;
                for(std::size_t i=0;i<cache.m_Manifolds.size();++i){const auto& x=cache.m_Manifolds[i];const auto& y=strict.m_Manifolds[i];
                    if(Access::Count(x)!=Access::Count(y))return false;
                    for(int s=0;s<Access::Count(x);++s){Wire u,v;u.Contact(Access::ContactAt(x,s));v.Contact(Access::ContactAt(y,s));if(!u.Same(v))return false;}}
                return true;
            }
            bool SurvivorOnly() noexcept {
                return cache.m_Manifolds.size()==1&&Count()==1&&
                    Access::ContactAt(cache.m_Manifolds[0],0).featureA==66&&Lambda(0)==.5f;
            }
            std::size_t StorageEstimate() const noexcept {
                // ShapeBox::IsValid fixes its private hull vector at8 points.
                // The independent64KiB cap also reserves allocator/stack slack.
                return sizeof(Work)+(cache.m_Manifolds.capacity()+strict.m_Manifolds.capacity())*sizeof(Manifold)+
                    (shapeA.SourceCapacity()+shapeB.SourceCapacity()+16)*sizeof(Vec3f);
            }
        };
        bool Run(const PhysicsWorld& world) noexcept {
            if(ran){failure="fixture repeated";return false;}ran=true;
            const auto begin=PD::Nanoseconds();
            if(PD::observer||PD::queryObserver||PD::Coupled::sink||PD::Coupled::CurrentContext()){
                failure="fixtures require detached observers";return false;}
#ifdef GE_ENABLE_PHYSICS_PROFILING
            struct ProfileRestore {PhysicsProfileSnapshot saved{PhysicsProfileDetail::MutableSnapshot()};
                ~ProfileRestore(){PhysicsProfileDetail::MutableSnapshot()=saved;}} restoreProfile;
#endif
            const RigidBody3D* aa=nullptr;const RigidBody3D* bb=nullptr;const RigidBody3D* alternate=nullptr;
            for(const auto* body:world.GetPhysicsBodies())if(body&&body->m_Shape&&body->m_Shape->IsValid()&&
                body->Type==BodyType::Dynamic&&body->m_Shape->GetShapeType()==ShapeType::Box){
                if(!aa)aa=body;else if(!bb)bb=body;else if(!alternate)alternate=body;}
            if(!aa||!bb||!alternate){failure="fixture runtime box membership missing";return false;}
            std::array<unsigned char,2*sizeof(RigidBody3D)> originalBodies{};
            std::array<unsigned char,2*sizeof(ShapeBox)> originalShapes{};
            std::memcpy(originalBodies.data(),aa,sizeof(RigidBody3D));std::memcpy(originalBodies.data()+sizeof(RigidBody3D),bb,sizeof(RigidBody3D));
            std::memcpy(originalShapes.data(),aa->m_Shape,sizeof(ShapeBox));std::memcpy(originalShapes.data()+sizeof(ShapeBox),bb->m_Shape,sizeof(ShapeBox));
            try {
                Work w(*aa,*bb);storageEstimate=w.StorageEstimate()+originalBodies.size()+originalShapes.size()+sizeof(LifecycleFixtures);
#ifdef GE_ENABLE_PHYSICS_PROFILING
                storageEstimate+=sizeof(PhysicsProfileSnapshot);
#endif
                if(w.shapeA.SourceCapacity()>36||w.shapeB.SourceCapacity()>36||w.cache.m_Manifolds.capacity()>2||
                    w.strict.m_Manifolds.capacity()>2||storageEstimate>MaximumScratchBytes){failure="fixture scratch/source bound";return false;}
                w.Seed();const bool candidates=w.PrepareWithControl();const bool retained=candidates&&w.Count()==2;
                const bool refreshed=w.Refresh();w.strict.AddContacts(w.patch.data(),2);
                Add("exact donor and donor-before-survivor preserve cold-path geometry/order",
                    retained&&refreshed&&w.GeometryMatchesControl()&&w.Count()==2&&w.Lambda(0)==.5f&&w.Lambda(1)==1.25f&&
                    Access::ConstraintAt(w.strict.m_Manifolds[0],1).m_CachedLambda[0]==0);

                w.Seed(0,.005f);w.patch={w.Contact(0,0,64,48),w.Contact(w.span,0,70,54)};
                w.PrepareWithControl();const bool priority=w.Refresh();w.strict.AddContacts(w.patch.data(),2);
                Add("strict same-face survivor outranks exact positive donor",priority&&w.GeometryMatchesControl()&&w.Count()==2&&w.Lambda(0)==.5f&&w.Lambda(1)==0);

                w.Seed();w.patch[0]=w.Contact(-w.span,0,65,49);w.PrepareWithControl();const bool mismatch=w.Refresh();w.strict.AddContacts(w.patch.data(),2);
                Add("positive donor same-face-only mismatch remains cold",mismatch&&w.GeometryMatchesControl()&&w.Lambda(1)==0);

                w.Seed();auto zero=Access::ContactAt(w.cache.m_Manifolds[0],0);zero.featureA=zero.featureB=0;
                w.cache.Clear();const std::array<contact_t,2> zeroOld{{zero,w.Contact(w.span,0,66,50)}};w.cache.AddContacts(zeroOld.data(),2);
                w.cache.PrepareContactRefresh();Add("unlabelled positive witness cannot survive as donor",w.Count()==1);

                w.Seed(-w.span,w.span,.001f,.001f);const bool noCandidates=!w.cache.PrepareContactRefresh();const bool dead=noCandidates&&w.cache.m_Manifolds.empty();
                const bool restarted=w.Refresh();Add("fully expired pair terminates and restarts cold",dead&&restarted&&w.Count()==2&&w.Lambda(0)==0&&w.Lambda(1)==0);

                w.Seed();Access::FixtureNormalB(w.cache.m_Manifolds[0],0,Vec3f(1,0,0));w.cache.PrepareContactRefresh();
                Add("normal-incoherent donor rejected by original expiry",w.SurvivorOnly());
                w.Seed();
                // Expiry reads contact anchors; rebuild only the isolated donor's
                // mismatched anchor through the existing AddContacts interface.
                auto drift=w.Contact(-w.span,.001f,64,48);drift.ptOnB_LocalSpace.x+=.03f;drift.ptOnB_WorldSpace=w.b.BodySpaceToWorldSpace(drift.ptOnB_LocalSpace);
                w.cache.Clear();const std::array<contact_t,2> driftOld{{drift,w.Contact(w.span,0,66,50)}};w.cache.AddContacts(driftOld.data(),2);
                Access::ConstraintAt(w.cache.m_Manifolds[0],1).m_CachedLambda[0]=.5f;w.cache.PrepareContactRefresh();
                Add("tangentially invalid donor rejected by original expiry",w.SurvivorOnly());
                w.Seed();Access::FixtureAnchorB(w.cache.m_Manifolds[0],0,Vec3f(std::numeric_limits<float>::quiet_NaN(),0,0));
                w.cache.PrepareContactRefresh();Add("nonfinite old anchor cannot survive as donor",w.SurvivorOnly());

                w.Seed();w.b=*alternate;w.b.m_Shape=&w.shapeB;w.cache.PrepareContactRefresh();
                Add("body identity mismatch rejects entire old cache",w.cache.m_Manifolds.empty());
                w.Seed();w.b.m_Shape=&w.shapeA;w.cache.PrepareContactRefresh();
                Add("shape pointer mismatch rejects entire old cache",w.cache.m_Manifolds.empty());
                w.Seed();w.shapeB.ChangeRevision();w.cache.PrepareContactRefresh();
                Add("shape revision mismatch rejects entire old cache",w.cache.m_Manifolds.empty());
                w.Seed();w.shapeB.Invalidate();w.cache.PrepareContactRefresh();
                Add("invalid copied shape rejects entire old cache",w.cache.m_Manifolds.empty());

                for(const char* label:{"failed or separated query strict cleanup","TOI queue strict cleanup","static mask or sleeping skip strict cleanup"}){
                    w.Seed();w.cache.PrepareContactRefresh();w.cache.RemoveExpiredForPair(&w.a,&w.b);Add(label,w.SurvivorOnly());}
                w.Seed();w.cache.PrepareContactRefresh();w.cache.RemoveExpiredForPair(&w.a,&w.b);w.cache.AddContact(w.patch[0]);
                Add("single witness cannot consume discarded donor",w.Count()==2&&w.Lambda(1)==0);

                w.Seed();w.cache.PrepareContactRefresh();std::array<RigidBody3D*,2> pairBodies{{&w.a,&w.b}};
                w.cache.RemoveExpiredOutsidePairs(std::span<const collisionPair_t>{},std::span<RigidBody3D* const>(pairBodies.data(),pairBodies.size()));
                Add("broadphase absent pair strips only donor",w.SurvivorOnly());
                w.Seed();w.cache.PrepareContactRefresh();const std::array<collisionPair_t,1> invalidPair{{{-1,1}}};
                w.cache.RemoveExpiredOutsidePairs(invalidPair,std::span<RigidBody3D* const>(pairBodies.data(),pairBodies.size()));
                Add("invalid candidate index cannot exempt donor cleanup",w.SurvivorOnly());
                w.Seed();w.cache.PrepareContactRefresh();const std::array<collisionPair_t,1> validPair{{{0,1}}};
                w.cache.RemoveExpiredOutsidePairs(validPair,std::span<RigidBody3D* const>(pairBodies.data(),pairBodies.size()));
                Add("validated candidate pair retains donor until final result",w.Count()==2);w.cache.RemoveExpiredForPair(&w.a,&w.b);

                w.Seed();w.cache.PrepareContactRefresh();w.patch[0].normal.x=std::numeric_limits<float>::quiet_NaN();
                Add("invalid incoming patch fails closed and strips donor",!w.Refresh()&&w.SurvivorOnly());
                w.Seed();w.cache.PrepareContactRefresh();w.patch[0].timeOfImpact=.01f;
                Add("temporal patch is not admitted as current donor refresh",!w.Refresh()&&w.SurvivorOnly());
                w.Seed();w.cache.PrepareContactRefresh();w.patch[0].m_BodyB=&w.a;
                Add("wrong incoming pair fails closed and strips donor",!w.Refresh()&&w.SurvivorOnly());
                w.Seed();w.cache.PrepareContactRefresh();
                Add("missing incoming patch fails closed and strips donor",!w.cache.RefreshContactPatch(&w.a,&w.b,nullptr,2)&&w.SurvivorOnly());

                w.Seed();w.cache.PrepareContactRefresh();w.patch[0]=w.Contact(-w.span,.0005f,64,48);
                const bool positive=w.Refresh();const bool retainedFresh=positive&&w.Count()==2&&Access::ContactAt(w.cache.m_Manifolds[0],1).separationDistance>0;
                w.cache.PreSolve(1.0f/60.0f);
                Add("fresh admitted positive clipped point is not post-expired",retainedFresh&&w.Count()==2&&w.Lambda(1)>0);

                w.Seed();w.cache.RemoveExpired();Add("default strict expiry remains unchanged",w.SurvivorOnly());
                w.Seed(-w.span,w.span,0,0);const bool unchanged=!w.cache.PrepareContactRefresh();
                Add("no-donor preparation preserves pair and bypasses extra routing",unchanged&&w.Count()==2&&w.Lambda(0)==1.25f&&w.Lambda(1)==.5f);
                complete=true;
            } catch(...) {failure="fixture allocation or unexpected exception";}
            try {
                RunNormalCase(world,0);Add("captured1193 column singular family improves normal cache distance",normalResults[0].Passed());
                RunNormalCase(world,1);Add("captured1171 ground singular family improves normal cache distance",normalResults[1].Passed());
                RunNormalCase(world,2);Add("captured1172 ground unique active solution preserves native normal loads",normalResults[2].Passed());
                // The three normal cases are sequential, after Work has retired.
                storageEstimate=std::max(storageEstimate,sizeof(LifecycleFixtures)+sizeof(NormalInputs)+
                    4*sizeof(RigidBody3D)+4*sizeof(ShapeBox)+sizeof(Manifold)+4*sizeof(contact_t)+
                    128*sizeof(Vec3f)+20*1024);
                if(storageEstimate>MaximumScratchBytes)failure="captured normal fixture scratch bound";
            } catch(...) {failure="captured normal fixture allocation or unexpected exception";}
            liveUnchanged=std::memcmp(originalBodies.data(),aa,sizeof(RigidBody3D))==0&&
                std::memcmp(originalBodies.data()+sizeof(RigidBody3D),bb,sizeof(RigidBody3D))==0&&
                std::memcmp(originalShapes.data(),aa->m_Shape,sizeof(ShapeBox))==0&&
                std::memcmp(originalShapes.data()+sizeof(ShapeBox),bb->m_Shape,sizeof(ShapeBox))==0;
            Add("original runtime bodies and shape objects remain byte-identical",liveUnchanged);
            elapsedNs=PD::Nanoseconds()-begin;return Complete();
        }
        void Metadata(std::ostream& out) const {
            out<<"{\"complete\":"<<(Complete()?"true":"false")<<",\"good\":"<<(Good()?"true":"false")<<",\"checks\":"<<count<<",\"failures\":"<<failures
                <<",\"live_unchanged\":"<<(liveUnchanged?"true":"false")<<",\"owned_result_bytes\":"<<OwnedBytes()<<",\"scratch_upper_bytes\":"<<MaximumScratchBytes
                <<",\"dynamic_and_local_storage_estimate\":"<<storageEstimate<<",\"elapsed_ns\":"<<elapsedNs<<",\"failure\":\""<<failure<<"\",\"coverage\":\"collector lifecycle plus three captured normal patches; PhysicsSystem route coverage is a separate source audit\",\"cases\":[";
            for(unsigned i=0;i<count;++i){if(i)out<<',';out<<"{\"name\":\""<<checks[i].name<<"\",\"passed\":"<<(checks[i].passed?"true":"false")<<'}';}
            out<<"],\"captured_normal_patches\":[";
            const auto number=[&](double value){if(std::isfinite(value))out<<value;else out<<"null";};
            const auto values=[&](const double* value,unsigned n){out<<'[';for(unsigned j=0;j<n;++j){if(j)out<<',';number(value[j]);}out<<']';};
            for(unsigned i=0;i<normalResults.size();++i){if(i)out<<',';const auto& r=normalResults[i];
                out<<"{\"tick\":"<<r.tick<<",\"bodies\":["<<r.body[0]<<','<<r.body[1]<<"],\"passed\":"<<(r.Passed()?"true":"false")
                    <<",\"captured_geometry_and_model_bits_match\":"<<(r.inputsMatch?"true":"false")<<",\"block_events\":"<<r.blockEvents
                    <<",\"live_unchanged\":"<<(r.liveUnchanged?"true":"false")<<",\"source_tolerance\":";number(r.tolerance);
                out<<",\"baseline_score\":";number(r.baselineScore);out<<",\"candidate_score\":";number(r.candidateScore);
                out<<",\"baseline_residual\":";number(r.baselineResidual);out<<",\"candidate_residual\":";number(r.candidateResidual);
                out<<",\"actual_post_block_normal_residual\":";number(r.actualNormalResidual);
                out<<",\"old\":";values(r.old,4);out<<",\"baseline\":";values(r.baseline,4);out<<",\"candidate\":";values(r.candidate,4);
                out<<",\"rhs\":";values(r.rhs,4);out<<",\"K\":[";
                for(unsigned row=0;row<4;++row){if(row)out<<',';values(r.K[row],4);}out<<"]}";
            }out<<"]}";
        }
    };
    static_assert(sizeof(LifecycleFixtures::Work)+2*sizeof(RigidBody3D)+2*sizeof(ShapeBox)+
        4*sizeof(Manifold)+128*sizeof(Vec3f)+sizeof(LifecycleFixtures)+16*1024<LifecycleFixtures::MaximumScratchBytes,
        "Focused lifecycle fixture storage exceeds its auxiliary scratch reserve");
    static_assert(sizeof(LifecycleFixtures)+sizeof(LifecycleFixtures::NormalInputs)+
        4*sizeof(RigidBody3D)+4*sizeof(ShapeBox)+sizeof(Manifold)+4*sizeof(contact_t)+128*sizeof(Vec3f)+20*1024<
        LifecycleFixtures::MaximumScratchBytes,"Captured normal fixture exceeds its unchanged auxiliary scratch reserve");
#ifdef GE_ENABLE_PHYSICS_PROFILING
    static_assert(sizeof(PhysicsProfileSnapshot)<16*1024,"Fixture profiling backup exceeds its reserved stack slack");
#endif
    static_assert(sizeof(Session)+3*Session::MaxBodies*sizeof(RigidBody3D)+
        2*Session::MaxManifolds*sizeof(Wire)+3*Session::MaxManifolds*sizeof(Manifold)<Session::MaximumOwnedBytes,
        "Replay fixed buffers and reserved collector storage exceed the advertised bound");
}
#endif
