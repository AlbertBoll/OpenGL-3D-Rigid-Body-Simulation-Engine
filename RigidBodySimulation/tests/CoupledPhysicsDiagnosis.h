#pragma once

#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION

#include "RestingContactDiagnosis.h"
#include "CoupledCollisionObservation.h"
#include "CoupledLosslessStream.h"
#include "CoupledSolverReplay.h"
#include <cstring>
#include <optional>

// Diagnostic overlay consumer. The ordinary executable never configures this
// session. No new Physics scheduler, scene construction or response is supplied.
namespace Rbs::CoupledDiagnosis
{
    using namespace ::GEngine;
    namespace PD=::GEngine::PhysicsDiagnostic;
    namespace CQ=::GEngine::PhysicsDiagnostic::Coupled;
    struct Config { RbsScenePreset preset{}; std::string output; unsigned ticks{900}, replayTick{}; };
    inline std::optional<Config> config;
    inline const char* Name(RbsScenePreset p) noexcept {
        return p==RbsScenePreset::SphereLattice?"SphereLattice":p==RbsScenePreset::BoxStack?"BoxStack":"SphereBoxStack";
    }
    // Wire v02: magic P20CPH02/P20CPQ02, then independent lossless blocks
    // {u32 logicalBytes,u32 storedBytes,payload}; equal sizes mean raw fallback.
    // Decoded logical stream remains {u8 kind,u16 rawBytes,u16 packedBytes,payload}.
    // Payload packing is byte-exact: command 0..127 emits command+1 zero bytes;
    // command 128..255 copies command-127 following literal bytes. No scalar
    // quantization. All scalar fields use little-endian Windows native widths.
    // State kind 1: u16 tick,u8 stage,index,phase,u16 mask; XOR u32 values for
    // each set mask bit. Words are px,py,pz,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,
    // inactive low/high words,sleeping. The initial previous words are zero.
    struct Buffer {
        std::array<unsigned char,4096> bytes{}; std::size_t used{}; bool valid{true};
        template<class T> void Put(const T& v) noexcept {
            static_assert(std::is_arithmetic_v<T>);
            if (used+sizeof(T)>bytes.size()) {valid=false;return;}
            std::memcpy(bytes.data()+used,&v,sizeof(T)); used+=sizeof(T);
        }
        void Vec(const Vec3f& v) noexcept {Put(v.x);Put(v.y);Put(v.z);}
        void State(const PD::BodyState& s) noexcept {
            Vec(s.position);for(float v:{s.orientation.w,s.orientation.x,s.orientation.y,s.orientation.z})Put(v);
            Vec(s.linear);Vec(s.angular);Put(s.inactive);Put(std::uint8_t(s.sleeping));
        }
        void Contact(const contact_t& c) noexcept {
            Vec(c.ptOnA_WorldSpace);Vec(c.ptOnB_WorldSpace);Vec(c.ptOnA_LocalSpace);Vec(c.ptOnB_LocalSpace);Vec(c.normal);
            Put(c.separationDistance);Put(c.timeOfImpact);Put(std::uint64_t(c.featureA));Put(std::uint64_t(c.featureB));
        }
    };
    struct Session
    {
        static constexpr unsigned MaxBodies=185, Invalid=MaxBodies;
        static constexpr std::size_t ExportCap=8*1024*1024, QueryMemory=4*1024*1024;
        struct Binding {
            RigidBodyIdentity id{}; EntityRenderId render{}; entt::entity entity{entt::null};
            std::uint64_t uuid{},revision{}; const RigidBody3D* body{}; const PhysicalShape* shape{};
            Vec3f center{},scale{},publishedPosition{}; Quat publishedRotation{1,0,0,0};
            Mat3 inertia{}; float inverseMass{},elasticity{},friction{},spin{},rolling{};
            BodyType type{}; std::uint32_t layer{},mask{}; RigidBody3D::SleepSettings sleep{};
            PD::BodyState initial{},last{}; std::string name;
        };
        struct Pair {
            std::uint64_t serial{}; std::array<std::uint64_t,4> points{},old{};
            std::array<std::array<double,3>,4> lambda{};
            std::array<Vec3f,4> localNormals{};
            int pendingSlot{-1},pendingOld{-1}; bool patch{};
        };
        struct Record {
            PD::Event event{}; std::uint64_t sequence{},pair{},point{},oldPoint{};
            unsigned kind{},stage{},a{},b{};
        };
        struct QueryRecord { CQ::Event event{}; unsigned a{},b{}; };
        static constexpr std::size_t RecordCapacity=24576;
        static constexpr std::size_t QueryCapacity=QueryMemory/sizeof(QueryRecord);
        static constexpr std::size_t PairCount=MaxBodies*MaxBodies;
        static constexpr std::size_t PairMemory=PairCount*sizeof(Pair);
        std::array<Binding,MaxBodies> bindings{};
        std::unique_ptr<Pair[]> pairs;
        std::array<bool,MaxBodies> direct{},prior{},selected{};
        std::array<std::array<std::uint32_t,16>,MaxBodies> serialized{};
        std::unique_ptr<Record[]> records;
        std::unique_ptr<QueryRecord[]> queries;
        std::size_t used{},queryUsed{},peakRecords{},peakQueries{},exportBytes{},queryExportBytes{};
        std::ofstream trace,queryTrace,initial;
        LosslessStream physicsStream,queryStream;
        std::unique_ptr<Rbs::CoupledReplay::Session> replay;
        Rbs::CoupledReplay::LifecycleFixtures lifecycleFixtures;
        _Scene* scene{}; const PhysicsWorld* world{};
        PD::Observer observer{}; CQ::Sink querySink{}; PD::QueryObserver rawQuerySink{};
        unsigned count{},target{Invalid},ground{Invalid},tick{},stage{};
        unsigned rawTicks{},firstImpact{},firstQueryFailure{},firstInvariant{},firstLargeResidual{};
        std::uint64_t sequence{},nextPair{},nextPoint{},frame{},lastPresented{},operations{},queryCount{},queryFailures{};
        std::uint64_t tickOperations{},tickQueries{},tickQueryFailures{},beginNs{PD::Nanoseconds()},boundaryNs{},frameNs{};
        double maxClosing{},maxCone{},maxPenetration{},maxCorrection{},signedWork{},responseWork{},responseStartK{},responseEndK{};
        bool opened{},failed{},complete{},stopped{},flushed{},visible{},extracted{},submitted{},sleeping{},interpolated{};
        bool firstInvariantSaved{}; const char* failure=""; const char* stopReason="";
        std::array<PD::BodyState,MaxBodies> callbackBefore{};
        void Fail(const char* why) noexcept {if(!failed){failed=true;failure=why;}}
        unsigned Index(const RigidBody3D* body) const noexcept {
            for(unsigned i=0;i<count;++i)if(bindings[i].body==body)return i;return Invalid;
        }
        bool Valid(const RigidBody3D* body) noexcept {
            const unsigned i=Index(body);
            if(i==Invalid || !world || world->GetPhysicsBodies().size()!=count ||
                world->GetPhysicsBodies()[i]!=body || !world->IsBodyIdentityValid(bindings[i].id) || body->GetIdentity()!=bindings[i].id){
                Fail("body identity or membership changed");return false;}
            return true;
        }
        static bool Validate(void* context,const RigidBody3D* a,const RigidBody3D* b) noexcept {
            auto& s=*static_cast<Session*>(context);return !s.failed&&s.Valid(a)&&s.Valid(b);
        }
        static void Violation(void* context) noexcept {static_cast<Session*>(context)->Fail("observer mutated authoritative state");}
        Pair& PairFor(unsigned a,unsigned b) noexcept {return pairs[std::min(a,b)*MaxBodies+std::max(a,b)];}
        bool Window() const noexcept {
            if(config->ticks==1200&&tick>=1170&&tick<=1200)return true;
            if(config->preset==RbsScenePreset::SphereLattice)return (tick>=85&&tick<=105)||(tick>=570&&tick<=595);
            return (tick>=1&&tick<=40)||(tick>=300&&tick<=310)||(tick>=600&&tick<=610);
        }
        bool RetainFailedLatticeQueries() const noexcept {
            return config->ticks==1200&&config->preset==RbsScenePreset::SphereLattice;
        }
        bool Keep(unsigned a,unsigned b) const noexcept {
            if(config->preset!=RbsScenePreset::SphereLattice)return true;
            return a==target||b==target||
                ((direct[a]||prior[a])&&bindings[a].type==BodyType::Dynamic)||
                ((direct[b]||prior[b])&&bindings[b].type==BodyType::Dynamic);
        }
        void Touch(unsigned a,unsigned b) noexcept {
            if(a==target&&b!=a)direct[b]=true;if(b==target&&a!=b)direct[a]=true;
        }
        Record* Push(unsigned kind,unsigned a,unsigned b) noexcept {
            if(failed)return nullptr;
            if(used==RecordCapacity){Fail("bounded tick recorder exhausted");return nullptr;}
            auto& r=records[used++];r=Record{};r.kind=kind;r.stage=stage;r.a=a;r.b=b;r.sequence=kind==3?sequence:++sequence;
            peakRecords=std::max(peakRecords,used);return &r;
        }
        bool Write(std::uint8_t kind,const Buffer& raw,bool query=false) noexcept {
            if(!raw.valid){Fail("wire record capacity exceeded");return false;}
            std::array<unsigned char,8192> packed{};std::size_t n=0,i=0;
            while(i<raw.used){
                const bool zero=raw.bytes[i]==0;std::size_t end=i+1;
                while(end<raw.used&&end-i<128&&(raw.bytes[end]==0)==zero)++end;
                packed[n++]=std::uint8_t(zero?end-i-1:127+end-i);
                if(!zero){std::memcpy(packed.data()+n,raw.bytes.data()+i,end-i);n+=end-i;}i=end;
            }
            auto& stream=query?queryStream:physicsStream;
            // Reserve the same 256 KiB for initial identity CSV and metadata.
            const bool ok=stream.WriteRecord(kind,std::uint16_t(raw.used),packed.data(),n,
                query?queryTrace:trace,exportBytes,ExportCap-256*1024);
            queryExportBytes=std::size_t(queryStream.physicalBytes);
            if(!ok)Fail(stream.error==LosslessStream::Error::Capacity?
                "combined 8 MiB export cap exhausted; capture incomplete":"coupled lossless stream write failed");return ok;
        }
        void State(unsigned i,const PD::BodyState& value,unsigned atStage,unsigned phase) noexcept {
            std::array<std::uint32_t,16> words{};unsigned k=0;
            for(float f:{value.position.x,value.position.y,value.position.z,value.orientation.w,value.orientation.x,
                value.orientation.y,value.orientation.z,value.linear.x,value.linear.y,value.linear.z,value.angular.x,value.angular.y,value.angular.z})
                words[k++]=std::bit_cast<std::uint32_t>(f);
            const auto inactive=std::bit_cast<std::uint64_t>(value.inactive);words[13]=std::uint32_t(inactive);words[14]=std::uint32_t(inactive>>32);words[15]=value.sleeping;
            std::uint16_t mask=0;for(unsigned j=0;j<16;++j)if(words[j]!=serialized[i][j])mask|=std::uint16_t(1u<<j);
            Buffer b;b.Put(std::uint16_t(tick));b.Put(std::uint8_t(atStage));b.Put(std::uint8_t(i));b.Put(std::uint8_t(phase));b.Put(mask);
            for(unsigned j=0;j<16;++j)if(mask&(1u<<j))b.Put(words[j]^serialized[i][j]);
            if(Write(1,b))serialized[i]=words;
        }
        double Kinetic(unsigned i,const PD::BodyState& s) const noexcept {
            const auto& b=bindings[i];if(b.type!=BodyType::Dynamic||!(b.inverseMass>0))return 0;
            const glm::dvec3 local=glm::transpose(glm::dmat3(glm::mat3_cast(Math::QuaternionOrIdentity(s.orientation))))*glm::dvec3(s.angular);
            return .5*(glm::dot(glm::dvec3(s.linear),glm::dvec3(s.linear))+glm::dot(local,glm::dmat3(b.inertia)*local))/b.inverseMass;
        }
        double ImpulseWork(unsigned i,const PD::BodyState& state,const Vec3f& linear,const Vec3f& angular) const noexcept {
            if(!(bindings[i].inverseMass>0)||bindings[i].type!=BodyType::Dynamic)return 0;
            RigidBody3D copy=*bindings[i].body;copy.m_Orientation=state.orientation;
            const glm::dvec3 p(linear),l(angular);
            return glm::dot(p,glm::dvec3(state.linear))+glm::dot(l,glm::dvec3(state.angular))+
                .5*(bindings[i].inverseMass*glm::dot(p,p)+glm::dot(l,glm::dmat3(copy.GetInverseInertiaTensorWorldSpace())*l));
        }
        static void Event(void* context,const PD::Event& e) noexcept {
            auto& s=*static_cast<Session*>(context);if(s.failed)return;
            const unsigned a=s.Index(e.a),b=s.Index(e.b);if(a==Invalid||b==Invalid){s.Fail("unbound event");return;}
            auto* row=s.Push(2,a,b);if(!row)return;row->event=e;row->event.a=row->event.b=nullptr;
            ++s.operations;++s.tickOperations;auto& pair=s.PairFor(a,b);const int slot=e.slot;
            if(e.operation==PD::Operation::PairBegin){if(pair.serial)s.Fail("duplicate live pair");pair=Pair{};pair.serial=++s.nextPair;}
            row->pair=pair.serial;if(slot>=0&&slot<4)row->point=row->oldPoint=pair.points[slot];
            if(e.operation==PD::Operation::PairReset){pair.points={};pair.lambda={};}
            else if(e.operation==PD::Operation::PairEnd)pair=Pair{};
            else if(e.operation==PD::Operation::PatchBegin){pair.old=pair.points;pair.points={};pair.patch=true;}
            else if(e.operation==PD::Operation::PointMap){pair.pendingSlot=slot;pair.pendingOld=e.previousSlot;}
            else if(e.operation==PD::Operation::PatchEnd){pair.old={};pair.patch=false;pair.pendingOld=pair.pendingSlot=-1;}
            else if(e.operation==PD::Operation::ContactNew||e.operation==PD::Operation::ContactRefresh){
                if(!pair.serial||slot<0||slot>=4){s.Fail("contact without pair/slot");return;}
                const int old=pair.patch?pair.pendingOld:slot;row->event.previousSlot=old;
                row->oldPoint=old>=0&&old<4?(pair.patch?pair.old[old]:pair.points[old]):0;
                if(pair.patch&&pair.pendingSlot!=slot)s.Fail("missing contact mapping");
                pair.points[slot]=e.operation==PD::Operation::ContactRefresh?row->oldPoint:++s.nextPoint;
                if(!pair.points[slot])s.Fail("refresh without predecessor");row->point=pair.points[slot];pair.lambda[slot]=e.accumulatedAfter;
                pair.localNormals[slot]=Math::NormalizeOr(glm::transpose(glm::mat3_cast(Math::QuaternionOrIdentity(e.beforeA.orientation)))*e.normal);s.Touch(a,b);
            }else if(e.operation==PD::Operation::ContactRemove){
                if(slot<0||slot>=4||!pair.points[slot])s.Fail("remove without predecessor");
                else{for(int i=slot;i<3;++i){pair.points[i]=pair.points[i+1];pair.lambda[i]=pair.lambda[i+1];pair.localNormals[i]=pair.localNormals[i+1];}pair.points[3]=0;pair.lambda[3]={};pair.localNormals[3]={};}
            }else if(slot>=0&&slot<4&&(e.operation==PD::Operation::WarmStart||e.operation==PD::Operation::NormalSliding||e.operation==PD::Operation::ContactSnapshot))pair.lambda[slot]=e.accumulatedAfter;
            else if(slot>=0&&slot<4&&e.operation==PD::Operation::BlockNormal)pair.lambda[slot][0]=e.accumulatedAfter[0];
            if(e.operation==PD::Operation::ToiQuery||e.operation<=PD::Operation::ToiPosition)s.Touch(a,b);
            if(e.operation==PD::Operation::ToiNormal&&!s.firstImpact)s.firstImpact=s.tick;
            if(e.appliedImpulsesKnown){
                const double w=s.ImpulseWork(a,e.beforeA,e.linearImpulseA,e.angularImpulseA)+s.ImpulseWork(b,e.beforeB,e.linearImpulseB,e.angularImpulseB);
                s.signedWork+=w;if(e.operation<=PD::Operation::BlockNormal&&e.operation!=PD::Operation::Position)s.responseWork+=w;
            }
            if(e.operation==PD::Operation::Position)s.maxCorrection=std::max({s.maxCorrection,double(glm::length(e.afterA.position-e.beforeA.position)),double(glm::length(e.afterB.position-e.beforeB.position))});
            if(e.operation==PD::Operation::NormalSliding||e.operation==PD::Operation::WarmStart){
                const double n=e.accumulatedAfter[0],mu=double(s.bindings[a].friction)*s.bindings[b].friction;
                const double excess=std::hypot(e.accumulatedAfter[1],e.accumulatedAfter[2])-mu*n;
                if((n<-1e-6||excess>1e-5*std::max(1.,std::abs(n)))&&!s.firstInvariant)s.firstInvariant=s.tick;
            }
        }
        static void Query(void* context,const CQ::Event& e) noexcept {
            auto& s=*static_cast<Session*>(context);if(s.failed||e.kind==CQ::EventKind::Begin)return;
            const unsigned a=s.Index(e.context.a),b=s.Index(e.context.b);
            if(a==Invalid||b==Invalid){s.Fail("unbound query");return;}
            if(e.kind==CQ::EventKind::Finish){++s.queryCount;++s.tickQueries;if(e.outcome==2){++s.queryFailures;++s.tickQueryFailures;if(!s.firstQueryFailure)s.firstQueryFailure=s.tick;}}
            if(s.RetainFailedLatticeQueries()&&e.kind==CQ::EventKind::Finish&&e.outcome==2){
                // A late TOI-role failure selects already buffered earlier stages.
                // Static endpoints never seed ground-connected contact fanout.
                if(s.bindings[a].type==BodyType::Dynamic&&s.bindings[a].inverseMass>0)s.direct[a]=true;
                if(s.bindings[b].type==BodyType::Dynamic&&s.bindings[b].inverseMass>0)s.direct[b]=true;
            }
            if(e.kind==CQ::EventKind::Admission)s.Touch(a,b);
            if(s.queryUsed==QueryCapacity){s.Fail("4 MiB query tick recorder exhausted");return;}
            auto& row=s.queries[s.queryUsed++];row={e,a,b};row.event.context.a=row.event.context.b=nullptr;
            row.event.contact.m_BodyA=row.event.contact.m_BodyB=nullptr;for(auto& c:row.event.patch)c.m_BodyA=c.m_BodyB=nullptr;
            s.peakQueries=std::max(s.peakQueries,s.queryUsed);
        }
        void WriteEvent(const Record& r) noexcept {
            const auto& e=r.event;State(r.a,e.beforeA,r.stage,1);if(r.b!=r.a)State(r.b,e.beforeB,r.stage,1);
            Buffer b;b.Put(std::uint16_t(tick));b.Put(std::uint8_t(r.stage));b.Put(std::uint8_t(e.operation));b.Put(std::uint8_t(r.a));b.Put(std::uint8_t(r.b));
            for(auto value:{r.sequence,r.pair,r.point,r.oldPoint})b.Put(value);
            b.Put(std::int32_t(e.slot));b.Put(std::int32_t(e.previousSlot));b.Put(std::int32_t(e.reason));b.Put(std::uint8_t(e.appliedImpulsesKnown));
            for(double v:{e.time,e.duration,e.restitution,e.closingSpeed,e.penetration,e.bias})b.Put(v);
            for(const auto& v:{e.normal,e.anchorA,e.anchorB,e.localA,e.localB,e.tangentU,e.tangentV,e.linearImpulseA,e.linearImpulseB,e.angularImpulseA,e.angularImpulseB})b.Vec(v);
            for(const auto& values:{e.accumulatedBefore,e.accumulatedAfter})for(double v:values)b.Put(v);
            for(const auto& values:{e.normalRow,e.tangentRowU,e.tangentRowV})for(double v:values)b.Put(v);
            b.Put(std::uint64_t(e.featureA));b.Put(std::uint64_t(e.featureB));b.Put(std::uint64_t(e.constraintToken));Write(2,b);
            State(r.a,e.afterA,r.stage,2);if(r.b!=r.a)State(r.b,e.afterB,r.stage,2);
        }
        void WriteQuery(const QueryRecord& row,bool full) noexcept {
            const auto& e=row.event;const auto& c=e.context;Buffer b;
            b.Put(std::uint16_t(tick));b.Put(std::uint8_t(e.kind));b.Put(std::uint8_t(c.role));b.Put(std::uint8_t(row.a));b.Put(std::uint8_t(row.b));b.Put(c.query);
            b.Put(std::int32_t(e.outcome));b.Put(std::int32_t(e.reason));b.Put(c.dt);b.Put(c.accumulatedTime);
            b.Put(std::int32_t(e.diagnostics.termination));b.Put(std::uint32_t(e.diagnostics.iterations));
            b.Put(std::int32_t(e.diagnostics.epa.termination));b.Put(std::uint32_t(e.diagnostics.epa.iterations));
            b.Put(std::uint8_t(e.admission));b.Put(std::uint8_t(e.patchCount));
            // Continuous compact query rows retain result geometry, not every private EPA operand.
            b.Contact(e.contact);
            if(full){b.State(e.beforeA);b.State(e.beforeB);b.State(e.afterA);b.State(e.afterB);for(unsigned i=0;i<e.patchCount;++i)b.Contact(e.patch[i]);}
            Write(full?11:10,b,true);
        }
        void WriteSnapshot(const Record& r) noexcept {
            // Internal kind3 reuses Event storage; wire kind4 remains unchanged.
            // All contact scalars stay floats in tangentU/V, including signed zero.
            const auto& e=r.event;Buffer b;
            b.Put(std::uint16_t(tick));b.Put(std::uint8_t(r.stage));b.Put(std::uint8_t(r.a));b.Put(std::uint8_t(r.b));b.Put(std::uint8_t(e.slot));
            b.Put(r.pair);b.Put(r.point);
            for(const auto& v:{e.anchorA,e.anchorB,e.localA,e.localB,e.normal})b.Vec(v);
            b.Put(e.tangentU.x);b.Put(e.tangentU.y);b.Put(std::uint64_t(e.featureA));b.Put(std::uint64_t(e.featureB));
            for(double value:e.accumulatedAfter)b.Put(value);b.Put(e.tangentU.z);b.Put(e.tangentV.x);Write(4,b);
        }
        static std::uint64_t Hash(std::uint64_t hash,const void* bytes,std::size_t count) noexcept {
            const auto* p=static_cast<const unsigned char*>(bytes);for(std::size_t i=0;i<count;++i){hash^=p[i];hash*=1099511628211ull;}return hash;
        }
        void Metrics(PD::Stage at,ManifoldCollector& manifolds) noexcept {
            double kinetic=0,potential=0;float linear=0,angular=0,penetration=0,closing=0,complementarity=0,cone=0;
            unsigned asleep=0,contacts=0,worstA=Invalid,worstB=Invalid;std::uint64_t hash=1469598103934665603ull;
            for(unsigned i=0;i<count;++i){const auto state=PD::Capture(*bindings[i].body);Buffer body;body.State(state);hash=Hash(hash,body.bytes.data(),body.used);
                kinetic+=Kinetic(i,state);if(bindings[i].inverseMass>0)potential+=12.*state.position.y/bindings[i].inverseMass;
                linear=std::max(linear,glm::length(state.linear));angular=std::max(angular,glm::length(state.angular));asleep+=state.sleeping;}
            for(auto& m:manifolds.m_Manifolds)for(int slot=0;slot<m.GetNumContacts();++slot){
                const auto c=m.GetContact(slot);const auto a=Index(c.m_BodyA),b=Index(c.m_BodyB);if(a==Invalid||b==Invalid){Fail("unbound manifold");return;}
                ++contacts;const auto& p=PairFor(a,b);RigidBody3D ca=*c.m_BodyA,cb=*c.m_BodyB;
                const auto pa=ca.BodySpaceToWorldSpace(c.ptOnA_LocalSpace),pb=cb.BodySpaceToWorldSpace(c.ptOnB_LocalSpace);
                const auto n=ca.GetBodyToWorldRotation()*p.localNormals[slot];const float depth=-glm::dot(pb-pa,n);penetration=std::max(penetration,depth);
                const auto va=ca.GetLinearVelocity()+glm::cross(ca.GetAngularVelocity(),pa-ca.GetCenterOfMassWorldSpace());
                const auto vb=cb.GetLinearVelocity()+glm::cross(cb.GetAngularVelocity(),pb-cb.GetCenterOfMassWorldSpace());
                const float vn=glm::dot(vb-va,n);if(-vn>closing){closing=-vn;worstA=a;worstB=b;}
                const auto& l=p.lambda[slot];complementarity=std::max(complementarity,float(std::abs(l[0]*vn)));
                cone=std::max(cone,float(std::hypot(l[1],l[2])-double(bindings[a].friction)*bindings[b].friction*l[0]));
                Buffer h;h.Put(std::uint8_t(a));h.Put(std::uint8_t(b));h.Put(std::uint8_t(slot));h.Put(p.serial);h.Put(p.points[slot]);h.Contact(c);for(double value:l)h.Put(value);hash=Hash(hash,h.bytes.data(),h.used);
                if(at==PD::Stage::Narrowphase||at==PD::Stage::Solve||at==PD::Stage::Correction){
                    if(RetainFailedLatticeQueries()){
                        // Buffer before query-failure selection is known. Existing
                        // Record capacity and lifetime remain fixed and checked.
                        auto* row=Push(3,a,b);if(!row)return;row->pair=p.serial;row->point=p.points[slot];
                        auto& e=row->event;e.slot=slot;e.anchorA=c.ptOnA_WorldSpace;e.anchorB=c.ptOnB_WorldSpace;
                        e.localA=c.ptOnA_LocalSpace;e.localB=c.ptOnB_LocalSpace;e.normal=c.normal;
                        e.featureA=c.featureA;e.featureB=c.featureB;e.accumulatedAfter=l;
                        e.tangentU=Vec3f(c.separationDistance,c.timeOfImpact,vn);e.tangentV.x=depth;
                    }else if((Window()||firstInvariant==tick)&&Keep(a,b)){
                        Buffer row;row.Put(std::uint16_t(tick));row.Put(std::uint8_t(at));row.Put(std::uint8_t(a));row.Put(std::uint8_t(b));row.Put(std::uint8_t(slot));row.Put(p.serial);row.Put(p.points[slot]);row.Contact(c);for(double value:l)row.Put(value);row.Put(vn);row.Put(depth);Write(4,row);
                    }
                }
            }
            if(at==PD::Stage::Narrowphase)responseStartK=kinetic;
            if(at==PD::Stage::Solve){responseEndK=kinetic;if(closing>.05f&&!firstLargeResidual)firstLargeResidual=tick;}
            maxClosing=std::max(maxClosing,double(closing));maxCone=std::max(maxCone,double(cone));maxPenetration=std::max(maxPenetration,double(penetration));
            Buffer b;b.Put(std::uint16_t(tick));b.Put(std::uint8_t(at));b.Put(hash);b.Put(std::uint32_t(contacts));b.Put(std::uint32_t(asleep));
            b.Put(kinetic);b.Put(potential);for(float v:{linear,angular,penetration,closing,complementarity,cone})b.Put(v);
            b.Put(std::uint8_t(worstA));b.Put(std::uint8_t(worstB));b.Put(tickOperations);b.Put(tickQueries);b.Put(tickQueryFailures);b.Put(signedWork);b.Put(responseWork);b.Put(maxCorrection);Write(3,b);
        }
        static void Boundary(void* context,PD::Stage at,const PhysicsWorld& world,ManifoldCollector& manifolds,
            std::span<const ContactIsland>,std::size_t,int passes) noexcept {
            auto& s=*static_cast<Session*>(context);const auto start=PD::Nanoseconds();if(s.failed)return;
            if(&world!=s.world||passes!=1){s.Fail("world or normal solve count changed");return;}
            if(at==PD::Stage::Begin){++s.tick;s.used=s.queryUsed=0;s.sequence=0;s.direct={};s.flushed=false;s.tickOperations=s.tickQueries=s.tickQueryFailures=0;s.signedWork=s.responseWork=s.maxCorrection=0;
                if(s.tick>config->ticks+1){s.Fail("tick budget exceeded");return;}}
            s.stage=unsigned(at);
            for(unsigned i=0;i<s.count;++i){if(!s.Valid(s.bindings[i].body))return;const auto state=PD::Capture(*s.bindings[i].body);s.callbackBefore[i]=state;
                if(auto* r=s.Push(1,i,i))r->event.beforeA=state;
                if(!s.bindings[i].body->HasFiniteState()){if(!s.firstInvariant)s.firstInvariant=s.tick;s.Fail("nonfinite state");}}
            s.Metrics(at,manifolds);
            if(s.replay&&s.tick==config->replayTick){
                if(at==PD::Stage::Narrowphase)s.replay->BeforeWarmStart(s.tick,*s.world,manifolds);
                else if(at==PD::Stage::Islands)s.replay->BeforeSolve(s.tick,*s.world,manifolds);
                else if(at==PD::Stage::Solve)s.replay->AfterSolve(s.tick,*s.world,manifolds);
                if(!s.replay->Good())s.Fail("cloned solver replay integrity failure");
            }
            if(at==PD::Stage::End&&!s.failed){
                const bool failureRaw=s.RetainFailedLatticeQueries()&&s.tickQueryFailures>0;
                const bool raw=s.Window()||(!s.firstInvariantSaved&&s.firstInvariant==s.tick)||failureRaw;
                for(unsigned i=0;i<s.count;++i)s.selected[i]=s.Keep(i,i);
                // Only state coverage expands to direct counterparts; Keep never
                // reads selected, so static ground cannot select other contacts.
                if(failureRaw)for(std::size_t i=0;i<s.used;++i){const auto& r=s.records[i];if(r.kind!=1&&s.Keep(r.a,r.b)){s.selected[r.a]=true;s.selected[r.b]=true;}}
                if(raw){++s.rawTicks;for(std::size_t i=0;i<s.used;++i){const auto& r=s.records[i];
                    if(r.kind==1){if(failureRaw?s.selected[r.a]:s.Keep(r.a,r.b))s.State(r.a,r.event.beforeA,r.stage,0);}
                    else if(s.Keep(r.a,r.b)){if(r.kind==3)s.WriteSnapshot(r);else s.WriteEvent(r);}}}
                for(std::size_t i=0;i<s.queryUsed;++i){const auto& r=s.queries[i];
                    // Full matched-state inputs for every failed query and raw-window selected pair.
                    // Compact result rows are retained for all stack queries; lattice follows focus.
                    if(s.Keep(r.a,r.b)||r.event.outcome==2)s.WriteQuery(r,raw||r.event.outcome==2);}
                for(unsigned i=0;i<s.count;++i){const auto state=PD::Capture(*s.bindings[i].body);s.bindings[i].last=state;
                    if(config->preset!=RbsScenePreset::SphereLattice||s.tick%10==0||i==s.target||(failureRaw&&s.selected[i]))s.State(i,state,unsigned(at),3);}
                if(s.firstInvariant==s.tick)s.firstInvariantSaved=true;s.prior=s.direct;s.flushed=true;
            }
            for(unsigned i=0;i<s.count;++i)if(!CQ::SameBits(s.callbackBefore[i],PD::Capture(*s.bindings[i].body)))s.Fail("boundary observer changed state");
            s.boundaryNs+=PD::Nanoseconds()-start;
        }
        bool Open(_Scene& owner) {
            scene=&owner;world=owner.GetPhysicsSystem()->GetPhysicsWorld();count=world?unsigned(world->GetPhysicsBodies().size()):0;
            const unsigned expected=config->preset==RbsScenePreset::SphereLattice?185:config->preset==RbsScenePreset::BoxStack?21:22;
            if(count!=expected||owner.GetPhysicsTiming().totalSteps!=0||owner.GetPhysicsStep().count()!=1.0/60.0||world->GetGravity()!=Vec3f(0,-12,0)||owner.GetPhysicsSystem()->GetSolverIterations()!=1){Fail("normal preset initial count/clock/policy mismatch");return false;}
            sleeping=owner.GetPhysicsSystem()->IsSleepingEnabled();interpolated=owner.IsRenderInterpolationEnabled();
            records.reset(new(std::nothrow) Record[RecordCapacity]);queries.reset(new(std::nothrow) QueryRecord[QueryCapacity]);
            pairs.reset(new(std::nothrow) Pair[PairCount]);
            if(!records||!queries||!pairs){Fail("bounded recorder allocation failed");return false;}
            const auto dir=std::filesystem::path(config->output);std::error_code ec;
            if(!dir.is_absolute()||!std::filesystem::is_directory(dir,ec)||ec||!std::filesystem::is_empty(dir,ec)||ec){Fail("output must be existing empty absolute directory");return false;}
            trace.open(dir/"physics.bin",std::ios::binary);queryTrace.open(dir/"queries.bin",std::ios::binary);initial.open(dir/"initial.csv");
            trace.write("P20CPH02",8);queryTrace.write("P20CPQ02",8);exportBytes=16;queryExportBytes=8;
            if(!trace||!queryTrace||!initial){Fail("evidence open failed");return false;}opened=true;
            if(config->replayTick){
                if(config->preset!=RbsScenePreset::SphereBoxStack||config->replayTick!=603||config->ticks!=610){Fail("unadmitted replay request");return false;}
                replay.reset(new(std::nothrow) Rbs::CoupledReplay::Session);
                if(!replay||!replay->Open(dir/"replay.bin",exportBytes,ExportCap-256*1024)){Fail("cloned solver replay setup failed");return false;}
            }
            initial<<std::setprecision(17)<<"index,name,uuid,entity,slot,generation,shape,type,inverse_mass,elasticity,friction,spin,rolling,layer,mask,revision,cx,cy,cz,sx,sy,sz,radius";
            for(int i=0;i<9;++i)initial<<",inertia"<<i;initial<<",linear_sleep,angular_sleep,dwell\n";
            const auto& registry=owner.ReadRegistry();unsigned dynamics=0;
            for(unsigned i=0;i<count;++i){const auto* body=world->GetPhysicsBodies()[i];if(!body||!body->m_Shape||!world->IsBodyIdentityValid(body->GetIdentity())){Fail("invalid initial body");return false;}
                auto& b=bindings[i];b.body=body;b.id=body->GetIdentity();b.shape=body->m_Shape;b.revision=b.shape->GetRevision();b.center=b.shape->GetCenterOfMass();b.inertia=b.shape->InertiaTensor();
                b.inverseMass=body->m_InvMass;b.elasticity=body->m_Elasticity;b.friction=body->m_Friction;b.spin=body->GetSpinResistanceLength();b.rolling=body->GetRollingResistanceLength();b.sleep=body->GetSleepSettings();b.type=body->Type;b.layer=body->m_CollisionLayer;b.mask=body->m_CollisionMask;b.initial=b.last=PD::Capture(*body);
                unsigned matches=0;for(auto entity:registry.view<Component::RigidBody3DComponent,Component::IDComponent,Component::TagComponent,Component::Transform3DComponent>()){
                    if(registry.get<Component::RigidBody3DComponent>(entity).RuntimeBody!=body)continue;++matches;b.entity=entity;b.uuid=std::uint64_t(registry.get<Component::IDComponent>(entity).ID);b.name=registry.get<Component::TagComponent>(entity).Name;
                    const auto& tr=registry.get<Component::Transform3DComponent>(entity);b.scale=tr.Scale;b.publishedPosition=tr.Translation;b.publishedRotation=tr.QuatRotation;}
                if(matches!=1){Fail("ambiguous body entity join");return false;}if(b.name=="wood_sphere158")target=i;if(b.name=="wood_plane")ground=i;dynamics+=b.type==BodyType::Dynamic;
                initial<<i<<','<<std::quoted(b.name)<<','<<b.uuid<<','<<std::uint32_t(b.entity)<<','<<b.id.GetSlot()<<','<<b.id.GetGeneration()<<','<<int(b.shape->GetShapeType())<<','<<int(b.type)<<','<<b.inverseMass<<','<<b.elasticity<<','<<b.friction<<','<<b.spin<<','<<b.rolling<<','<<b.layer<<','<<b.mask<<','<<b.revision;
                RestDiagnosis::CsvVector(initial,b.center);RestDiagnosis::CsvVector(initial,b.scale);initial<<','<<(b.shape->GetShapeType()==ShapeType::Sphere?static_cast<const ShapeSphere*>(b.shape)->GetRadius():0);
                for(int c=0;c<3;++c)for(int r=0;r<3;++r)initial<<','<<b.inertia[c][r];initial<<','<<b.sleep.linearSpeedThreshold<<','<<b.sleep.angularSpeedThreshold<<','<<b.sleep.inactivitySeconds<<'\n';
                State(i,b.initial,0,4);
                // Shape bounds are observed on a body copy; enough to reconstruct these box/sphere presets.
                RigidBody3D copy=*body;copy.m_Position=Vec3f(0);copy.m_Orientation=Quat(1,0,0,0);const auto bounds=copy.GetWorldBounds();
                Buffer shape;shape.Put(std::uint8_t(i));shape.Vec(bounds.mins);shape.Vec(bounds.maxs);Write(8,shape);
            }
            if(ground==Invalid||(config->preset==RbsScenePreset::SphereLattice&&target==Invalid)||dynamics!=(count-5)){Fail("preset dynamic membership mismatch");return false;}
            if(PD::observer||CQ::sink||PD::queryObserver){Fail("observer already installed");return false;}
            if(config->ticks==1200&&config->preset==RbsScenePreset::SphereBoxStack&&!lifecycleFixtures.Run(*world)){Fail("contact lifecycle fixture failed");return false;}
            observer.context=this;observer.detailed=true;observer.targeted=true;observer.valid=Validate;observer.violation=Violation;observer.event=Event;observer.boundary=Boundary;
            querySink.context=this;querySink.event=Query;querySink.select=Validate;querySink.violation=Violation;
            // Both compared products gather real producer diagnostics. All raw
            // callbacks remain null: no EPA hull copying or private-event export.
            rawQuerySink.context=this;
            PD::observer=&observer;CQ::sink=&querySink;PD::queryObserver=&rawQuerySink;
            std::ofstream protocol(dir/"protocol.json");protocol<<"{\"schema\":1,\"preset\":"<<std::quoted(Name(config->preset))<<",\"ticks\":"<<config->ticks
                <<",\"physics_step\":0.016666666666666666,\"global_passes\":1,\"internal_wall_seconds\":150,\"scratch_limit\":67108864,\"query_memory_limit\":4194304,\"combined_export_limit\":8388608"
                <<",\"owned_record_bytes\":"<<RecordCapacity*sizeof(Record)<<",\"owned_query_bytes\":"<<QueryCapacity*sizeof(QueryRecord)<<",\"owned_pair_bytes\":"<<PairMemory<<",\"owned_session_bytes\":"<<sizeof(Session)<<",\"auxiliary_buffer_reserve\":1048576"
                <<",\"wire_version\":2,\"lossless_block_bytes\":65536,\"lossless_stream_storage_bytes\":"<<2*sizeof(LosslessStream)<<",\"logical_fnv1a64_basis\":14695981039346656037"
                <<",\"cloned_replay_tick\":"<<config->replayTick<<",\"cloned_replay_owned_bytes\":"<<(replay?replay->OwnedBytes():0)
                <<",\"raw_windows\":"<<(config->preset==RbsScenePreset::SphereLattice?(config->ticks==1200?"[[85,105],[570,595],[1170,1200]]":"[[85,105],[570,595]]"):(config->ticks==1200?"[[1,40],[300,310],[600,610],[1170,1200]]":"[[1,40],[300,310],[600,610]]"))
                <<",\"raw_selection\":"<<std::quoted(config->preset==RbsScenePreset::SphereLattice?"Sphere158 and current/prior dynamic partners, including their other contacts":"all bodies and operations")
                <<",\"failed_query_retention\":"<<std::quoted(RetainFailedLatticeQueries()?"all four Finish failure roles select dynamic endpoints; retrospective whole-tick operations/states and Narrowphase/Solve/Correction contacts; counterpart states only, no static contact fanout":"inactive")
                <<",\"continuous_body_states\":"<<std::quoted(config->preset==RbsScenePreset::SphereLattice?"all bodies every10ticks; Sphere158 everytick":"all bodies everytick")
                <<",\"first_invariant_capture\":true,\"all_stage_metrics\":true,\"raw_producer_diagnostics_enabled\":true,\"query_observation\":\"top-level original-pair finish and admission; no private EPA operands\",\"off_on_equivalence\":false,\"global_earliest_cause_claim\":false}\n";
            initial.flush();if(!protocol||!initial){Fail("initial evidence failed");return false;}return !failed;
        }
        bool Invariants() noexcept {
            if(!scene||scene->GetPhysicsSystem()->GetPhysicsWorld()!=world||scene->GetPhysicsStep().count()!=1.0/60.0||world->GetGravity()!=Vec3f(0,-12,0)||scene->GetPhysicsSystem()->GetSolverIterations()!=1||scene->GetPhysicsSystem()->IsSleepingEnabled()!=sleeping||scene->IsRenderInterpolationEnabled()!=interpolated){Fail("normal scene policy changed");return false;}
            for(unsigned i=0;i<count;++i){const auto& old=bindings[i];const auto* b=old.body;if(!Valid(b))return false;
                if(b->m_Shape!=old.shape||b->m_Shape->GetRevision()!=old.revision||b->m_InvMass!=old.inverseMass||b->m_Elasticity!=old.elasticity||b->m_Friction!=old.friction||b->m_CollisionLayer!=old.layer||b->m_CollisionMask!=old.mask||b->Type!=old.type||b->GetSpinResistanceLength()!=old.spin||b->GetRollingResistanceLength()!=old.rolling||b->GetSleepSettings()!=old.sleep){Fail("physical authoring inputs changed");return false;}}
            return !failed;
        }
        bool BeforeUpdate(_Scene& owner,double elapsed) {
            if(!opened&&!Open(owner))return false;if(&owner!=scene||!Invariants())return false;
            ++frame;extracted=submitted=false;
            if(tick>=config->ticks){complete=true;stopReason="fixed observation ticks completed";return false;}
            if(PD::Nanoseconds()-beginNs>=150000000000ull){Fail("150second wall reserve reached before complete tick window");stopReason="wall reserve";return false;}
            Buffer b;b.Put(frame);b.Put(std::uint16_t(tick));b.Put(std::uint8_t(0));b.Put(elapsed);Write(5,b);return !failed;
        }
        void Frame(bool wasSubmitted,bool isVisible,bool success,int swap,int cap,bool paused) noexcept {
            const auto start=PD::Nanoseconds();if(!opened)return;visible=isVisible;if(wasSubmitted){submitted=success;if(success&&isVisible)lastPresented=frame;}
            const auto& timing=scene->GetPhysicsTiming();if(timing.totalSteps!=tick)Fail("Scene Physics tick join mismatch");
            Buffer b;b.Put(frame);b.Put(std::uint16_t(tick));b.Put(std::uint8_t(wasSubmitted?2:1));
            b.Put(timing.pendingSeconds);b.Put(timing.discardedSeconds);b.Put(timing.totalDiscardedSeconds);b.Put(std::uint32_t(timing.stepsLastUpdate));b.Put(std::uint64_t(timing.totalSteps));b.Put(double(scene->GetRenderInterpolationAlpha()));
            for(bool v:{visible,extracted,submitted,paused})b.Put(std::uint8_t(v));b.Put(std::int32_t(swap));b.Put(std::int32_t(cap));b.Put(lastPresented);Write(5,b);frameNs+=PD::Nanoseconds()-start;
        }
        static void ObserveFrame(void* context,const RenderFrame& rendered) noexcept {
            auto& s=*static_cast<Session*>(context);const auto start=PD::Nanoseconds();if(s.failed||!s.Invariants())return;s.extracted=true;
            for(const auto& draw:rendered.Draws()){
                auto entity=s.scene->RenderData().Resolve(draw.entity);if(!entity){s.Fail("draw identity cannot resolve");return;}
                const auto* rb=s.scene->ReadRegistry().try_get<Component::RigidBody3DComponent>(*entity);if(!rb)continue;if(!s.Valid(rb->RuntimeBody))return;
                const unsigned i=s.Index(rb->RuntimeBody);auto& binding=s.bindings[i];if(*entity!=binding.entity){s.Fail("render body join changed");return;}
                if(binding.render&&binding.render!=draw.entity){s.Fail("render identity changed");return;}binding.render=draw.entity;
                if((config->preset==RbsScenePreset::SphereLattice&&!s.selected[i]&&i!=s.target)||(!s.Window()&&s.tick%10!=0))continue;
                const auto& tr=s.scene->ReadRegistry().get<Component::Transform3DComponent>(*entity);
                Buffer b;b.Put(s.frame);b.Put(std::uint16_t(s.tick));b.Put(std::uint8_t(i));b.Put(std::uint64_t(draw.entity.index));b.Put(std::uint64_t(draw.entity.generation));b.Put(std::uint64_t(draw.entity.registry));b.Put(double(s.scene->GetRenderInterpolationAlpha()));
                b.Vec(tr.Translation);for(float v:{tr.QuatRotation.w,tr.QuatRotation.x,tr.QuatRotation.y,tr.QuatRotation.z})b.Put(v);
                for(int c=0;c<4;++c)for(int r=0;r<4;++r)b.Put(draw.worldTransform[c][r]);s.Write(6,b);
            }
            for(const auto& camera:rendered.Cameras())if(s.Window()||s.tick%10==0){Buffer b;b.Put(s.frame);b.Put(std::uint16_t(s.tick));b.Vec(camera.worldPosition);for(const auto& matrix:{camera.view,camera.projection})for(int c=0;c<4;++c)for(int r=0;r<4;++r)b.Put(matrix[c][r]);s.Write(7,b);}
            s.frameNs+=PD::Nanoseconds()-start;
        }
        void Detach() noexcept {if(PD::observer==&observer)PD::observer=nullptr;if(CQ::sink==&querySink)CQ::sink=nullptr;if(PD::queryObserver==&rawQuerySink)PD::queryObserver=nullptr;scene=nullptr;world=nullptr;}
        bool Finish(bool clean) {
            if(!opened)return false;Detach();
            // Preserve an interrupted current tick without following retired pointers.
            if(!flushed&&used&&exportBytes<ExportCap-256*1024){for(std::size_t i=0;i<used;++i){const auto& r=records[i];if(!Keep(r.a,r.b))continue;if(r.kind==1)State(r.a,r.event.beforeA,r.stage,0);else if(r.kind==3)WriteSnapshot(r);else WriteEvent(r);}}
            if(!flushed&&queryUsed&&exportBytes<ExportCap-256*1024){for(std::size_t i=0;i<queryUsed;++i){const auto& r=queries[i];if(Keep(r.a,r.b)||r.event.outcome==2)WriteQuery(r,true);}}
            const bool physicsFlushed=physicsStream.Flush(trace,exportBytes,ExportCap-256*1024);
            const bool queryFlushed=queryStream.Flush(queryTrace,exportBytes,ExportCap-256*1024);
            if(replay&&(!replay->Finish()||!replay->Complete()))Fail("cloned solver replay incomplete or failed");
            if(!physicsFlushed||!queryFlushed)Fail(physicsStream.error==LosslessStream::Error::Capacity||queryStream.error==LosslessStream::Error::Capacity?
                "combined 8 MiB export cap exhausted; capture incomplete":"final lossless stream flush failed");
            queryExportBytes=std::size_t(queryStream.physicalBytes);
            trace.flush();queryTrace.flush();initial.flush();if(!trace||!queryTrace||!initial)Fail("final export flush failed");trace.close();queryTrace.close();initial.close();
            const auto dir=std::filesystem::path(config->output);std::error_code ec;std::uintmax_t actual=0;for(const auto& entry:std::filesystem::directory_iterator(dir,ec))if(entry.is_regular_file())actual+=entry.file_size();
            if(ec||actual>ExportCap-16384)Fail("final combined export accounting failure");
            std::ofstream out(dir/"result.json");out<<std::setprecision(17)<<"{\"schema\":1,\"preset\":"<<std::quoted(Name(config->preset))<<",\"capture_complete\":"<<(complete?"true":"false")<<",\"technical_failure\":"<<(failed?"true":"false")<<",\"shutdown\":"<<(clean?"true":"false")
                <<",\"requested_ticks\":"<<config->ticks<<",\"ticks\":"<<tick<<",\"frames\":"<<frame<<",\"raw_ticks\":"<<rawTicks<<",\"operations\":"<<operations<<",\"queries\":"<<queryCount<<",\"query_failures\":"<<queryFailures
                <<",\"first_impact_tick\":"<<firstImpact<<",\"first_query_failure_tick\":"<<firstQueryFailure<<",\"first_constraint_invariant_tick\":"<<firstInvariant<<",\"first_large_support_residual_tick\":"<<firstLargeResidual
                <<",\"max_closing_velocity\":"<<maxClosing<<",\"max_cone_excess\":"<<maxCone<<",\"max_penetration\":"<<maxPenetration<<",\"last_response_delta_K\":"<<responseEndK-responseStartK<<",\"last_response_signed_work\":"<<responseWork
                <<",\"peak_record_bytes\":"<<peakRecords*sizeof(Record)<<",\"peak_query_bytes\":"<<peakQueries*sizeof(QueryRecord)<<",\"owned_scratch_bytes\":"<<RecordCapacity*sizeof(Record)+QueryCapacity*sizeof(QueryRecord)+PairMemory+sizeof(Session)+(replay?replay->OwnedBytes():0)<<",\"auxiliary_buffer_reserve\":1048576"
                <<",\"export_bytes_before_result\":"<<actual<<",\"query_export_bytes\":"<<queryExportBytes<<",\"operation_observer_ns\":"<<observer.observationNs<<",\"query_observer_ns\":"<<querySink.observationNs<<",\"boundary_observer_ns\":"<<boundaryNs<<",\"frame_observer_ns\":"<<frameNs
                <<",\"physics_stream\":";physicsStream.Metadata(out);out<<",\"query_stream\":";queryStream.Metadata(out);
            out<<",\"cloned_replay_tick\":"<<config->replayTick<<",\"cloned_replay\":";if(replay)replay->Metadata(out);else out<<"null";
            out<<",\"contact_lifecycle_fixtures\":";if(config->ticks==1200&&config->preset==RbsScenePreset::SphereBoxStack)lifecycleFixtures.Metadata(out);else out<<"null";
            out<<",\"elapsed_seconds\":"<<double(PD::Nanoseconds()-beginNs)/1e9<<",\"failure\":"<<std::quoted(failure)<<",\"stop_reason\":"<<std::quoted(stopReason)<<",\"physics_acceptance_claim\":false,\"off_on_equivalence_measured\":false,\"globally_earliest_cause_proven\":false}\n";
            return clean&&complete&&!failed&&bool(out);
        }
    };
    // Exact complete-type ownership proof; an additional MiB covers bounded
    // stream/path/string buffers and callback encoding stack. Array ownership
    // begins once in Open and all three heaps retire with the Session.
    static_assert(Rbs::CoupledReplay::LifecycleFixtures::MaximumScratchBytes<=1024*1024,"Lifecycle fixture must fit existing auxiliary reserve");
    static_assert(Session::RecordCapacity*sizeof(Session::Record)+
        Session::QueryCapacity*sizeof(Session::QueryRecord)+Session::PairMemory+
        sizeof(Session)+Rbs::CoupledReplay::Session::MaximumOwnedBytes+1024*1024<64*1024*1024,
        "Coupled recorder owned heaps, Session and auxiliary reserve exceed 64 MiB");
    inline std::unique_ptr<Session> session;
}
#endif
