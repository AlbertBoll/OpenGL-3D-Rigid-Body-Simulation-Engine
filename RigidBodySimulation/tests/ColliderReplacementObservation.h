#pragma once
#ifdef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
#include "RestingContactDiagnosis.h"
#include "CoupledCollisionObservation.h"
#include "ColliderReplacementCapture.h"
#include <cstddef>
#include <type_traits>


// Same bounded native little-endian Wire format as the preserved replay helper.
// Keep only its value serializer: historical Replay/Lifecycle methods use the old
// pre-typed-world API and must neither compile into nor run in this consumer.
namespace Rbs::ColliderCacheWire
{
    struct Wire {
        std::array<unsigned char,4096> bytes{};std::size_t used{};bool valid{true};
        template<class T>void Put(T value)noexcept{static_assert(std::is_arithmetic_v<T>);if(used+sizeof(T)>bytes.size()){valid=false;return;}std::memcpy(bytes.data()+used,&value,sizeof(T));used+=sizeof(T);}
        void Vec(const ::GEngine::Vec3f& v)noexcept{Put(v.x);Put(v.y);Put(v.z);}
        void State(const ::GEngine::PhysicsDiagnostic::BodyState& s)noexcept{Vec(s.position);for(float x:{s.orientation.w,s.orientation.x,s.orientation.y,s.orientation.z})Put(x);Vec(s.linear);Vec(s.angular);Put(s.inactive);Put(std::uint8_t(s.sleeping));}
        void Contact(const ::GEngine::contact_t& c)noexcept{for(const auto& v:{c.ptOnA_WorldSpace,c.ptOnB_WorldSpace,c.ptOnA_LocalSpace,c.ptOnB_LocalSpace,c.normal})Vec(v);Put(c.separationDistance);Put(c.timeOfImpact);Put(std::uint64_t(c.featureA));Put(std::uint64_t(c.featureB));}
        bool Same(const Wire& b)const noexcept{return valid&&b.valid&&used==b.used&&std::memcmp(bytes.data(),b.bytes.data(),used)==0;}
    };
}

namespace GEngine::PhysicsDiagnostic
{
    // Uses the existing bounded Wire representation, without invoking replay/lifecycle code.
    struct ColliderObservationAccess
    {
        using Wire=Rbs::ColliderCacheWire::Wire;
        template<class ShapeIndex> static Wire Physical(const RigidBody3D& b,unsigned ordinal,ShapeIndex shape)noexcept
        {
            Wire w;w.Put(std::uint32_t(ordinal));w.State(Capture(b));w.Put(std::int32_t(b.Type));
            w.Put(b.m_InvMass);w.Put(b.m_Elasticity);w.Put(b.m_Friction);w.Put(b.m_CollisionLayer);w.Put(b.m_CollisionMask);
            w.Put(std::uint32_t(shape(b.m_Shape)));w.Put(b.m_Shape->GetRevision());w.Put(std::int32_t(b.m_Shape->GetShapeType()));
            w.Put(b.m_SpinResistanceLength);w.Put(b.m_RollingResistanceLength);
            w.Put(b.m_SleepSettings.linearSpeedThreshold);w.Put(b.m_SleepSettings.angularSpeedThreshold);w.Put(b.m_SleepSettings.inactivitySeconds);
            const auto& q=b.m_SleepSource;w.Vec(q.position);w.Vec(q.linearVelocity);w.Vec(q.angularVelocity);
            for(float x:{q.orientation.w,q.orientation.x,q.orientation.y,q.orientation.z})w.Put(x);
            w.Put(q.inverseMass);w.Put(q.elasticity);w.Put(q.friction);w.Put(q.layer);w.Put(q.mask);w.Put(std::uint32_t(shape(q.shape)));w.Put(q.shapeRevision);w.Put(std::int32_t(q.type));
            for(bool x:{b.m_SleepSourceValid,b.m_SleepShapeValid,b.m_WakeRequested,b.m_ExternalMutation,b.m_InPhysicsStep})w.Put(std::uint8_t(x));
            w.Put(std::uint64_t(b.m_ActivationIsland));return w;
        }
        template<class ShapeIndex> static Wire Body(const RigidBody3D& b,unsigned ordinal,ShapeIndex shape)noexcept
        {
            Wire w=Physical(b,ordinal,shape);const auto& d=b.m_DerivedData;
            const auto matrix=[&](const auto& m){for(int c=0;c<3;++c)for(int r=0;r<3;++r)w.Put(m[c][r]);};
            for(float x:{d.rotationSourceOrientation.w,d.rotationSourceOrientation.x,d.rotationSourceOrientation.y,d.rotationSourceOrientation.z})w.Put(x);
            matrix(d.bodyToWorld);matrix(d.worldToBody);w.Put(d.rotationRevision);w.Put(std::uint8_t(d.rotationValid));
            w.Vec(d.centerSourcePosition);w.Put(std::uint32_t(shape(d.centerSourceShape)));w.Put(d.centerSourceShapeRevision);w.Put(d.centerSourceRotationRevision);w.Vec(d.centerOfMassWorld);w.Put(std::uint8_t(d.centerValid));
            w.Put(d.inertiaSourceInverseMass);w.Put(std::uint32_t(shape(d.inertiaSourceShape)));w.Put(d.inertiaSourceShapeRevision);matrix(d.inertiaBody);matrix(d.inverseInertiaBody);w.Put(d.bodyInertiaRevision);w.Put(std::uint8_t(d.bodyInertiaValid));
            w.Put(d.worldInertiaSourceBodyRevision);w.Put(d.worldInertiaSourceRotationRevision);matrix(d.inverseInertiaWorld);matrix(d.inertiaWorld);w.Put(std::uint8_t(d.worldInertiaValid));
            w.Vec(d.boundsSourcePosition);w.Put(std::uint32_t(shape(d.boundsSourceShape)));w.Put(d.boundsSourceShapeRevision);w.Put(d.boundsSourceRotationRevision);w.Vec(d.worldBounds.mins);w.Vec(d.worldBounds.maxs);w.Put(std::uint8_t(d.boundsValid));
            // Compare accessor behavior on copies against fresh derived data on a second copy.
            RigidBody3D cached=b,fresh=b;fresh.m_DerivedData={};
            const auto cachedBody=cached.GetInverseInertiaTensorBodySpace(),freshBody=fresh.GetInverseInertiaTensorBodySpace();
            const auto cachedWorld=cached.GetInverseInertiaTensorWorldSpace(),freshWorld=fresh.GetInverseInertiaTensorWorldSpace();
            bool equalBody=true,equalWorld=true,finite=true;
            for(int c=0;c<3;++c){finite&=Math::IsFinite(cachedBody[c])&&Math::IsFinite(freshBody[c])&&Math::IsFinite(cachedWorld[c])&&Math::IsFinite(freshWorld[c]);for(int r=0;r<3;++r){equalBody&=cachedBody[c][r]==freshBody[c][r];equalWorld&=cachedWorld[c][r]==freshWorld[c][r];}}
            w.Put(std::uint8_t(equalBody));w.Put(std::uint8_t(equalWorld));w.Put(std::uint8_t(finite));matrix(cachedBody);matrix(freshBody);matrix(cachedWorld);matrix(freshWorld);return w;
        }
        template<class BodyIndex,class ShapeIndex,class IdentityIndex> static Wire ManifoldImage(const Manifold& m,BodyIndex body,ShapeIndex shape,IdentityIndex identity)noexcept
        {
            Wire w;w.Put(std::uint32_t(body(m.m_BodyA)));w.Put(std::uint32_t(body(m.m_BodyB)));w.Put(std::uint32_t(m.m_NumContacts));
            for(const auto* q:{&m.m_StampA,&m.m_StampB}){w.Put(std::uint32_t(identity(q->identity)));w.Put(std::uint32_t(shape(q->shape)));w.Put(q->shapeRevision);}
            for(int i=0;i<m.m_NumContacts;++i){const auto& c=m.m_Contacts[i];const auto& q=m.m_Constraints[i];
                w.Put(std::uint32_t(body(c.m_BodyA)));w.Put(std::uint32_t(body(c.m_BodyB)));w.Contact(c);w.Vec(m.m_NormalB[i]);
                w.Put(std::uint32_t(body(q.m_bodyA)));w.Put(std::uint32_t(body(q.m_bodyB)));w.Vec(q.m_anchorA);w.Vec(q.m_anchorB);w.Vec(q.m_Normal);w.Put(q.m_Friction);
                for(int row=0;row<3;++row)for(int col=0;col<12;++col)w.Put(float(q.m_Jacobian[row][col]));for(int j=0;j<3;++j)w.Put(float(q.m_CachedLambda[j]));
                w.Put(std::uint8_t(q.m_PositionCorrectionEnabled));w.Put(q.m_SpinImpulse);w.Put(q.m_SpinResistanceLength);w.Put(q.m_RollingImpulse.x);w.Put(q.m_RollingImpulse.y);w.Put(q.m_RollingResistanceLength);
            }return w;
        }
    };
}

// Isolated observation only. No alternate step, contact solve or lifecycle fixture.
namespace Rbs::ColliderReplacementObservation
{
    using namespace ::GEngine;
    namespace PD=::GEngine::PhysicsDiagnostic;
    struct Session
    {
        static constexpr unsigned MaxBodies=192,PreSteps=120,PostSteps=1800;
        static constexpr unsigned EventCapacity=8192,StateCapacity=16384,ContactCapacity=32768;
        static constexpr std::uint64_t ExportLimit=512ull*1024*1024;
        struct StateRow { std::uint64_t tick{};unsigned stage{},body{};PD::BodyState state{}; };
        struct EventRow { std::uint64_t tick{};unsigned stage{},a{},b{};PD::Event event{}; };
        struct ContactRow { std::uint64_t tick{};unsigned stage{},a{},b{},slot{};contact_t contact{};Vec3f actualA{},actualB{};double projectedDepth{}; };
        struct PreRow { std::uint64_t tick{};unsigned count{};std::array<PD::BodyState,MaxBodies> states{}; };
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
        struct QueryRow { std::uint64_t tick{};unsigned a{},b{};PD::Coupled::Event event{}; };
        std::unique_ptr<QueryRow[]> queries;
        unsigned queryCount{};
        PD::Coupled::Sink querySink{};
        PD::QueryObserver rawQuerySink{};
        std::ofstream queryFile;
        static void Query(void* context,const PD::Coupled::Event& value)noexcept
        {
            auto& s=*static_cast<Session*>(context);if(!s.armed||s.failed||s.done||value.kind==PD::Coupled::EventKind::Begin)return;
            if(s.queryCount==EventCapacity){s.Fail("per-frame query capacity");return;}
            const auto a=s.Index(value.context.a),b=s.Index(value.context.b);if(a==MaxBodies||b==MaxBodies){s.Fail("unbound query pair");return;}
            auto& r=s.queries[s.queryCount++];r.tick=s.tick;r.a=a;r.b=b;r.event=value;r.event.context.a=r.event.context.b=nullptr;r.event.contact.m_BodyA=r.event.contact.m_BodyB=nullptr;
            for(auto& c:r.event.patch)c.m_BodyA=c.m_BodyB=nullptr;
        }
#endif
        _Scene* scene{};std::filesystem::path output;
        using CacheWire=Rbs::ColliderCacheWire::Wire;
        static constexpr unsigned MaxManifolds=256,MaxShapes=MaxBodies*3;
        struct CacheSnapshot {bool valid{};unsigned stage{},bodyCount{},manifoldCount{},shapeCount{};std::uint64_t tick{},frame{};
            CacheWire policy;std::array<CacheWire,MaxBodies> bodies{},physical{};std::array<CacheWire,MaxManifolds> manifolds{};};
        std::unique_ptr<CacheSnapshot> initialCache,lastCache;
        std::array<const PhysicalShape*,MaxShapes> shapeIdentities{};unsigned shapeCount{};
        bool initialCacheWritten{};
        bool firstPostCacheCaptured{},firstPostCacheWritten{};
        unsigned ShapeIndex(const PhysicalShape* shape)noexcept
        {if(!shape)return UINT32_MAX;for(unsigned i=0;i<shapeCount;++i)if(shapeIdentities[i]==shape)return i;Fail("unregistered shape identity in semantic cache image");return UINT32_MAX-1;}
        unsigned IdentityIndex(RigidBodyIdentity identity)noexcept
        {if(!identity.IsValid())return UINT32_MAX;for(unsigned i=0;i<bodyCount;++i)if(bodies[i]->GetIdentity()==identity)return i;Fail("unregistered body identity in semantic cache image");return UINT32_MAX-1;}
        void CaptureCache(CacheSnapshot& out,PD::Stage at,const PhysicsWorld& world,const ManifoldCollector& manifolds)noexcept
        {
            if(manifolds.m_Manifolds.size()>MaxManifolds){Fail("canonical manifold image capacity");return;}
            for(unsigned i=0;i<bodyCount;++i){const auto* shape=bodies[i]->m_Shape;bool known=false;for(unsigned j=0;j<shapeCount;++j)known|=shapeIdentities[j]==shape;
                if(!known){if(shapeCount==MaxShapes){Fail("canonical shape identity capacity");return;}shapeIdentities[shapeCount++]=shape;}}
            out.valid=false;out.stage=unsigned(at);out.tick=tick+(at==PD::Stage::End?1:0);out.frame=frame;out.bodyCount=bodyCount;out.manifoldCount=unsigned(manifolds.m_Manifolds.size());out.shapeCount=shapeCount;
            out.policy={};out.policy.Put(scene->GetPhysicsStep().count());out.policy.Vec(world.GetGravity());out.policy.Put(std::int32_t(scene->GetPhysicsSystem()->GetSolverIterations()));out.policy.Put(std::uint8_t(scene->GetPhysicsSystem()->IsSleepingEnabled()));out.policy.Put(std::uint8_t(scene->IsPaused()));
            auto shape=[&](const PhysicalShape* p){return ShapeIndex(p);};
            for(unsigned i=0;i<bodyCount;++i){out.physical[i]=PD::ColliderObservationAccess::Physical(*bodies[i],i,shape);out.bodies[i]=PD::ColliderObservationAccess::Body(*bodies[i],i,shape);if(!out.physical[i].valid||!out.bodies[i].valid){Fail("canonical body wire overflow");return;}}
            for(unsigned i=0;i<out.manifoldCount;++i){out.manifolds[i]=PD::ColliderObservationAccess::ManifoldImage(manifolds.m_Manifolds[i],[&](const auto* b){return Index(b);},shape,[&](auto id){return IdentityIndex(id);});if(!out.manifolds[i].valid){Fail("canonical manifold wire overflow");return;}}
            out.valid=!failed;
        }
        void DumpCache(const char* name,const CacheSnapshot& image)
        {
            if(!image.valid){Fail("canonical cache image unavailable");return;}const auto path=output/name;if(std::filesystem::exists(path)){Fail("canonical cache destination exists");return;}
            std::ofstream f(path,std::ios::binary);const char magic[8]={'C','R','C','A','C','H','0','1'};Write(f,magic,8);
            const std::uint32_t header[]={1,image.stage,image.bodyCount,image.manifoldCount,image.shapeCount};Write(f,header,5);Write(f,&image.tick,1);Write(f,&image.frame,1);
            const auto wire=[&](const CacheWire& w){const auto size=std::uint32_t(w.used);Write(f,&size,1);Write(f,w.bytes.data(),w.used);};
            wire(image.policy);for(unsigned i=0;i<image.bodyCount;++i){wire(image.physical[i]);wire(image.bodies[i]);}for(unsigned i=0;i<image.manifoldCount;++i)wire(image.manifolds[i]);f.flush();if(!f)Fail("canonical cache evidence write");
        }
        void DurableFlush()
        {
            stateFile.flush();eventFile.flush();contactFile.flush();renderFile.flush();frameFile.flush();
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
            queryFile.flush();
#endif
            if(!stateFile||!eventFile||!contactFile||!renderFile||!frameFile)Fail("periodic evidence flush");
        }

        std::array<const RigidBody3D*,MaxBodies> bodies{};
        std::array<std::uint64_t,MaxBodies> uuids{};
        std::array<entt::entity,MaxBodies> entities{};
        std::array<PreRow,PreSteps> pre{};
        std::unique_ptr<StateRow[]> states;
        std::unique_ptr<EventRow[]> events;
        std::unique_ptr<ContactRow[]> contacts;
        unsigned bodyCount{},stateCount{},eventCount{},contactCount{},preCount{},preCursor{},postCount{},stage{};
        std::uint64_t tick{},frame{},bytes{},boundaryNs{},frameNs{};
        bool opened{},armed{},attempted{},done{},failed{},finished{};
        const char* failure="";
        PD::Observer observer{};
        std::ofstream stateFile,eventFile,contactFile,renderFile,frameFile;
        double elapsed{};
        void Fail(const char* reason)noexcept{if(!failed){failed=true;failure=reason;}}
        unsigned Index(const RigidBody3D* b)const noexcept{for(unsigned i=0;i<bodyCount;++i)if(bodies[i]==b)return i;return MaxBodies;}
        static bool Finite(const PD::BodyState& s)noexcept{return Math::IsFinite(s.position)&&Math::IsFinite(s.linear)&&Math::IsFinite(s.angular)&&Math::IsFinite(s.orientation)&&std::isfinite(s.inactive);}
        template<class T>bool Write(std::ofstream& stream,const T* data,std::size_t count)
        {const auto size=count*sizeof(T);if(bytes+size>ExportLimit){Fail("512MiB export bound");return false;}stream.write(reinterpret_cast<const char*>(data),size);bytes+=size;if(!stream){Fail("evidence write failed");return false;}return true;}
        void Flush()
        {if(stateCount)Write(stateFile,states.get(),stateCount);if(eventCount)Write(eventFile,events.get(),eventCount);if(contactCount)Write(contactFile,contacts.get(),contactCount);stateCount=eventCount=contactCount=0;
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
            if(queryCount)Write(queryFile,queries.get(),queryCount);queryCount=0;
#endif
        }
        void State(unsigned i,unsigned at)
        {if(stateCount==StateCapacity){Fail("per-frame state capacity");return;}auto& row=states[stateCount++];row={tick,at,i,PD::Capture(*bodies[i])};if(!Finite(row.state))Fail("nonfinite authoritative body state");}
        static void Event(void* context,const PD::Event& input)noexcept
        {
            auto& s=*static_cast<Session*>(context);if(!s.armed||s.failed||s.done)return;
            if(s.eventCount==EventCapacity){s.Fail("per-frame event capacity");return;}
            const auto a=s.Index(input.a),b=s.Index(input.b);if(a==MaxBodies||b==MaxBodies){s.Fail("unbound event body");return;}
            auto& row=s.events[s.eventCount++];row.tick=s.tick;row.stage=s.stage;row.a=a;row.b=b;row.event=input;
            // Pointer identity is represented by ordinals and initial typed identity.
            row.event.a=row.event.b=nullptr;
            if(!Finite(input.beforeA)||!Finite(input.beforeB)||!Finite(input.afterA)||!Finite(input.afterB)||
               !Math::IsFinite(input.normal)||!Math::IsFinite(input.linearImpulseA)||!Math::IsFinite(input.linearImpulseB)||
               !Math::IsFinite(input.angularImpulseA)||!Math::IsFinite(input.angularImpulseB))s.Fail("nonfinite event state or impulse");
        }
        static void Boundary(void* context,PD::Stage at,const PhysicsWorld& world,ManifoldCollector& manifolds,std::span<const ContactIsland>,std::size_t,int passes)noexcept
        {
            auto& s=*static_cast<Session*>(context);const auto start=PD::Nanoseconds();s.stage=unsigned(at);
            if(s.failed||s.done)return;
            if(passes!=1||PhysicsTestAccess::Bodies(world).size()!=s.bodyCount){s.Fail("ordinary pass or membership changed");return;}
            const bool keep=at==PD::Stage::Begin||at==PD::Stage::Narrowphase||at==PD::Stage::Solve||at==PD::Stage::Correction||at==PD::Stage::Sleep||at==PD::Stage::End;
            if(s.armed&&keep){
                for(unsigned i=0;i<s.bodyCount;++i)s.State(i,unsigned(at));
                if(at==PD::Stage::Narrowphase||at==PD::Stage::Solve||at==PD::Stage::Correction){
                    for(auto& m:manifolds.m_Manifolds)for(int slot=0;slot<m.GetNumContacts();++slot){
                        if(s.contactCount==ContactCapacity){s.Fail("per-frame contact capacity");return;}
                        const auto c=m.GetContact(slot);const auto a=s.Index(c.m_BodyA),b=s.Index(c.m_BodyB);
                        if(a==MaxBodies||b==MaxBodies){s.Fail("unbound contact body");return;}
                        auto& row=s.contacts[s.contactCount++];row.tick=s.tick;row.stage=unsigned(at);row.a=a;row.b=b;row.slot=slot;row.contact=c;row.contact.m_BodyA=row.contact.m_BodyB=nullptr;
                        // Const live accessors may fill derived caches; inspect copies instead.
                        RigidBody3D ca=*c.m_BodyA,cb=*c.m_BodyB;row.actualA=ca.BodySpaceToWorldSpace(c.ptOnA_LocalSpace);row.actualB=cb.BodySpaceToWorldSpace(c.ptOnB_LocalSpace);
                        row.projectedDepth=-glm::dot(row.actualA-row.actualB,c.normal);
                        if(!Math::IsFinite(row.actualA)||!Math::IsFinite(row.actualB)||!Math::IsFinite(c.normal)||!std::isfinite(row.projectedDepth))s.Fail("nonfinite contact data");
                    }
                }
            }
            if(at==PD::Stage::Begin&&!s.initialCache->valid)s.CaptureCache(*s.initialCache,at,world,manifolds);
            if(at==PD::Stage::End){
                if(!s.armed)s.CaptureCache(*s.lastCache,at,world,manifolds);
                else if(s.postCount==0&&!s.firstPostCacheCaptured){s.CaptureCache(*s.lastCache,at,world,manifolds);s.firstPostCacheCaptured=s.lastCache->valid;}
                if(!s.armed){auto& r=s.pre[s.preCursor];r.tick=s.tick;r.count=s.bodyCount;for(unsigned i=0;i<s.bodyCount;++i){r.states[i]=PD::Capture(*s.bodies[i]);if(!Finite(r.states[i]))s.Fail("nonfinite pre-replacement state");}s.preCursor=(s.preCursor+1)%PreSteps;s.preCount=(std::min)(s.preCount+1,PreSteps);}
                else {for(unsigned i=0;i<s.bodyCount;++i){RigidBody3D copy=*s.bodies[i];const auto inertia=copy.GetInverseInertiaTensorWorldSpace();for(int c=0;c<3;++c)if(!Math::IsFinite(inertia[c]))s.Fail("nonfinite inverse inertia");}if(++s.postCount==PostSteps)s.done=true;}
                ++s.tick;
            }
            s.boundaryNs+=PD::Nanoseconds()-start;
        }
        void Layout()
        {
            std::ofstream f(output/"observer-layout.json");
            f<<"{\"version\":1,\"endian\":\"little\",\"state_row\":"<<sizeof(StateRow)<<",\"event_row\":"<<sizeof(EventRow)<<",\"contact_row\":"<<sizeof(ContactRow)<<",\"body_state\":"<<sizeof(PD::BodyState)<<",\"event\":"<<sizeof(PD::Event)<<",\"contact\":"<<sizeof(contact_t)<<",\"state_fields\":{";
#define FIELD(type,field) f<<"\""#field"\":"<<offsetof(type,field)
            FIELD(PD::BodyState,position);f<<',';FIELD(PD::BodyState,linear);f<<',';FIELD(PD::BodyState,angular);f<<',';FIELD(PD::BodyState,orientation);f<<',';FIELD(PD::BodyState,inactive);f<<',';FIELD(PD::BodyState,sleeping);
            f<<"},\"state_row_fields\":{";FIELD(StateRow,tick);f<<',';FIELD(StateRow,stage);f<<',';FIELD(StateRow,body);f<<',';FIELD(StateRow,state);
            f<<"},\"event_row_fields\":{";FIELD(EventRow,tick);f<<',';FIELD(EventRow,stage);f<<',';FIELD(EventRow,a);f<<',';FIELD(EventRow,b);f<<',';FIELD(EventRow,event);
            f<<"},\"event_fields\":{";FIELD(PD::Event,operation);f<<',';FIELD(PD::Event,beforeA);f<<',';FIELD(PD::Event,beforeB);f<<',';FIELD(PD::Event,afterA);f<<',';FIELD(PD::Event,afterB);f<<',';FIELD(PD::Event,normal);f<<',';FIELD(PD::Event,anchorA);f<<',';FIELD(PD::Event,anchorB);f<<',';FIELD(PD::Event,linearImpulseA);f<<',';FIELD(PD::Event,linearImpulseB);f<<',';FIELD(PD::Event,angularImpulseA);f<<',';FIELD(PD::Event,angularImpulseB);f<<',';FIELD(PD::Event,accumulatedBefore);f<<',';FIELD(PD::Event,accumulatedAfter);f<<',';FIELD(PD::Event,localA);f<<',';FIELD(PD::Event,localB);f<<',';FIELD(PD::Event,featureA);f<<',';FIELD(PD::Event,featureB);f<<',';FIELD(PD::Event,slot);f<<',';FIELD(PD::Event,previousSlot);f<<',';FIELD(PD::Event,reason);f<<',';FIELD(PD::Event,constraintToken);f<<',';FIELD(PD::Event,appliedImpulsesKnown);
            f<<"},\"contact_row_fields\":{";FIELD(ContactRow,tick);f<<',';FIELD(ContactRow,stage);f<<',';FIELD(ContactRow,a);f<<',';FIELD(ContactRow,b);f<<',';FIELD(ContactRow,slot);f<<',';FIELD(ContactRow,contact);f<<',';FIELD(ContactRow,actualA);f<<',';FIELD(ContactRow,actualB);f<<',';FIELD(ContactRow,projectedDepth);
            f<<"},\"contact_fields\":{";FIELD(contact_t,ptOnA_WorldSpace);f<<',';FIELD(contact_t,ptOnB_WorldSpace);f<<',';FIELD(contact_t,ptOnA_LocalSpace);f<<',';FIELD(contact_t,ptOnB_LocalSpace);f<<',';FIELD(contact_t,normal);f<<',';FIELD(contact_t,separationDistance);f<<',';FIELD(contact_t,timeOfImpact);f<<',';FIELD(contact_t,featureA);f<<',';FIELD(contact_t,featureB);f<<"}}\n";
#undef FIELD
        }
        bool Open(_Scene& owner,const std::filesystem::path& dir)
        {
            scene=&owner;output=dir;auto* system=owner.GetPhysicsSystem();if(!system||!system->GetPhysicsWorld()){Fail("no Physics world");return false;}
            const auto& list=PhysicsTestAccess::Bodies(system->GetPhysicsWorld());bodyCount=unsigned(list.size());
            if(bodyCount>MaxBodies||!bodyCount||system->GetSolverIterations()!=1||owner.GetPhysicsStep().count()!=1./60.){Fail("normal default Physics policy admission");return false;}
            if(PD::observer||PD::queryObserver){Fail("existing Physics observer");return false;}
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
            if(PD::Coupled::sink){Fail("existing coupled query observer");return false;}
            if(std::filesystem::exists(dir/"observer-queries.bin")){Fail("query destination exists");return false;}
            queries=std::make_unique<QueryRow[]>(EventCapacity);queryFile.open(dir/"observer-queries.bin",std::ios::binary);
            if(!queryFile){Fail("query file open");return false;}
#endif
            for(const char* name:{"observer-states.bin","observer-events.bin","observer-contacts.bin","observer-render.csv","observer-layout.json","observer-status.json"})if(std::filesystem::exists(dir/name)){Fail("observer evidence destination already exists");return false;}
            initialCache=std::make_unique<CacheSnapshot>();lastCache=std::make_unique<CacheSnapshot>();
            states=std::make_unique<StateRow[]>(StateCapacity);events=std::make_unique<EventRow[]>(EventCapacity);contacts=std::make_unique<ContactRow[]>(ContactCapacity);
            stateFile.open(dir/"observer-states.bin",std::ios::binary);eventFile.open(dir/"observer-events.bin",std::ios::binary);contactFile.open(dir/"observer-contacts.bin",std::ios::binary);renderFile.open(dir/"observer-render.csv");frameFile.open(dir/"observer-frames.csv");frameFile<<"frame,kind,wall_ns,elapsed_seconds,total_steps,steps_last_update,pending_seconds,discarded_seconds,total_discarded_seconds,alpha,success,visible,paused\n";
            if(!stateFile||!eventFile||!contactFile||!renderFile||!frameFile){Fail("observer evidence open");return false;}
            std::ofstream identity(dir/"observer-bodies.csv");identity<<"ordinal,uuid,entity,slot,generation,name\n";
            const auto& registry=owner.ReadRegistry();
            for(unsigned i=0;i<bodyCount;++i){bodies[i]=list[i];unsigned matches=0;for(auto e:registry.view<Component::RigidBody3DComponent,Component::IDComponent,Component::TagComponent>()){
                _Entity entity{e,&owner};if(PhysicsTestAccess::Body(entity)!=list[i])continue;++matches;uuids[i]=std::uint64_t(entity.GetUUID());entities[i]=e;
                identity<<i<<','<<uuids[i]<<','<<std::uint32_t(e)<<','<<list[i]->GetIdentity().GetSlot()<<','<<list[i]->GetIdentity().GetGeneration()<<','<<std::quoted(entity.GetName())<<'\n';}
                if(matches!=1){Fail("ambiguous body entity mapping");return false;}}
            renderFile<<std::setprecision(17)<<"frame,tick,ordinal,alpha,authoritative_px,authoritative_py,authoritative_pz";for(int i=0;i<16;++i)renderFile<<",matrix_"<<i;renderFile<<'\n';
            {std::ofstream initial(dir/"observer-initial-scene.json");initial<<std::setprecision(17);Rbs::ColliderCapture::Snapshot(initial,owner);if(!initial){Fail("initial scene snapshot write");return false;}}
            {std::ofstream coverage(dir/"observer-cache-format.json");coverage<<R"json({"schema":1,"magic":"CRCACH01","byte_order":"little","header":"u32 schema,stage,body_count,manifold_count,shape_count; u64 completed_physics_steps,frame; then u32 length + bytes per Wire","policy_layout":"f64 timestep,3*f32 gravity,i32 passes,u8 sleeping_enabled,u8 paused","record_order":"policy, physical/body pairs in world body order, active manifold images in exact vector order","body_reference":"world body ordinal, independent of UUID and native handle; actual identities remain in observer-bodies.csv","shape_reference":"first observed live shape in world-body order, retained across replacement; UINT32_MAX=null","coverage":["raw pose/velocities/inactivity/sleep","all sleep policy/source validity/wake/external/in-step/activation fields","all derived cache source keys, validity flags, revisions, cached transforms/bounds/inertia tensors","cached-accessor versus forced-fresh copied inertia comparison","active manifold order/body and stamp identities/shapes/revisions","active contact order/full contact/feature values/local transported normal","constraint anchors/normal/friction/Jacobian/cached normal and tangent lambdas","position-correction enable, spin/rolling accumulated impulses and resistance lengths"],"exclusions":["native addresses, vtables, padding, vector capacities and inactive contact slots","Constraint base m_axisA/m_axisB: uninitialized and never read by ConstraintPenetration","world broadphase temporary scratch and pending TOI queue: not persisted across Physics End","render-derived cache warmth can differ between last End image and UI operation; physical image comparison is separate"],"serialization_authority":"ColliderObservationAccess in ColliderReplacementObservation.h uses the exact preserved bounded Wire value format; historical Replay/Lifecycle definitions are not included; no Replay Session or LifecycleFixtures invocation","equivalence_claim":"semantic image is evidence; no automatic whole-program or replay equivalence PASS"})json";}
            Layout();observer.context=this;observer.detailed=true;observer.targeted=true;observer.event=Event;observer.boundary=Boundary;
            observer.valid=[](void* p,const RigidBody3D* a,const RigidBody3D* b)noexcept{auto& s=*static_cast<Session*>(p);if(s.Index(a)==MaxBodies||s.Index(b)==MaxBodies){s.Fail("event identity expired");return false;}return true;};
            observer.select=[](void* p,const RigidBody3D*,const RigidBody3D*)noexcept{const auto& s=*static_cast<Session*>(p);return s.armed&&!s.done&&!s.failed;};
            observer.violation=[](void* p)noexcept{static_cast<Session*>(p)->Fail("observer changed authoritative state");};
            PD::observer=&observer;opened=true;
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
            querySink.context=this;querySink.event=Query;querySink.select=[](void* p,const RigidBody3D*,const RigidBody3D*)noexcept{const auto& s=*static_cast<Session*>(p);return s.armed&&!s.failed&&!s.done;};querySink.violation=observer.violation;
            rawQuerySink.context=this;PD::Coupled::sink=&querySink;PD::queryObserver=&rawQuerySink;
            std::ofstream qlayout(dir/"observer-query-layout.json");qlayout<<"{\"row_size\":"<<sizeof(QueryRow)<<",\"event_offset\":"<<offsetof(QueryRow,event)<<",\"event_size\":"<<sizeof(PD::Coupled::Event)<<",\"context_size\":"<<sizeof(PD::Coupled::Context)<<",\"kind_offset\":"<<offsetof(PD::Coupled::Event,kind)<<",\"context_offset\":"<<offsetof(PD::Coupled::Event,context)<<",\"beforeA_offset\":"<<offsetof(PD::Coupled::Event,beforeA)<<",\"beforeB_offset\":"<<offsetof(PD::Coupled::Event,beforeB)<<",\"afterA_offset\":"<<offsetof(PD::Coupled::Event,afterA)<<",\"afterB_offset\":"<<offsetof(PD::Coupled::Event,afterB)<<",\"outcome_offset\":"<<offsetof(PD::Coupled::Event,outcome)<<",\"reason_offset\":"<<offsetof(PD::Coupled::Event,reason)<<",\"diagnostics_offset\":"<<offsetof(PD::Coupled::Event,diagnostics)<<",\"contact_offset\":"<<offsetof(PD::Coupled::Event,contact)<<",\"patch_offset\":"<<offsetof(PD::Coupled::Event,patch)<<",\"patch_count_offset\":"<<offsetof(PD::Coupled::Event,patchCount)<<",\"admission_offset\":"<<offsetof(PD::Coupled::Event,admission)<<"}\n";
#endif
            std::ofstream ready(dir/"observer-ready.json");ready<<"{\"ready\":true,\"initial_paused\":"<<owner.IsPaused()<<",\"body_count\":"<<bodyCount<<",\"pre_steps\":120,\"post_steps\":1800,\"export_limit_bytes\":"<<ExportLimit<<",\"normal_step_path\":true,\"performance_qualification\":false}\n";return true;
        }
        void Finish()
        {
            if(finished)return;finished=true;if(PD::observer==&observer)PD::observer=nullptr;
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
            if(PD::Coupled::sink==&querySink)PD::Coupled::sink=nullptr;if(PD::queryObserver==&rawQuerySink)PD::queryObserver=nullptr;
#endif
            Flush();stateFile.flush();eventFile.flush();contactFile.flush();renderFile.flush();frameFile.flush();
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
            queryFile.flush();
#endif
            if(!output.empty()){std::ofstream f(output/"observer-status.json");f<<"{\"status\":\""<<(failed?"FAIL":done?"CAPTURE_COMPLETE":"INCOMPLETE")<<"\",\"failure\":\""<<failure<<"\",\"attempted\":"<<attempted<<",\"post_steps\":"<<postCount<<",\"total_observed_steps\":"<<tick<<",\"binary_bytes\":"<<bytes<<",\"event_observer_ns\":"<<observer.observationNs<<",\"boundary_observer_ns\":"<<boundaryNs<<",\"frame_observer_ns\":"<<frameNs<<",\"qualification_pass\":false}\n";}
        }
        ~Session(){Finish();}
    };
    inline std::unique_ptr<Session> session;
    inline bool checked{};
    inline void FrameRecord(unsigned kind,bool success=false,bool visible=false)
    {
        if(!session||!session->opened||session->finished)return;auto& s=*session;const auto& t=s.scene->GetPhysicsTiming();
        std::ostringstream out;out<<std::setprecision(17)<<s.frame<<','<<kind<<','<<PD::Nanoseconds()<<','<<s.elapsed<<','<<t.totalSteps<<','<<t.stepsLastUpdate<<','<<t.pendingSeconds<<','<<t.discardedSeconds<<','<<t.totalDiscardedSeconds<<','<<s.scene->GetRenderInterpolationAlpha()<<','<<success<<','<<visible<<','<<s.scene->IsPaused()<<'\n';const auto text=out.str();s.Write(s.frameFile,text.data(),text.size());
    }
    inline void BeforeFrame(_Scene& scene,double elapsed)
    {
        if(!checked){checked=true;char* raw=nullptr;std::size_t n{};_dupenv_s(&raw,&n,"GENGINE_COLLIDER_CAPTURE_OUTPUT");
            if(raw){std::filesystem::path dir(raw);std::free(raw);if(dir.is_absolute()&&std::filesystem::is_directory(dir)){session=std::make_unique<Session>();session->Open(scene,dir);}}}
        if(session){++session->frame;session->elapsed=elapsed;FrameRecord(0);}
    }
    inline bool AfterFrame()
    {if(!session)return false;const auto start=PD::Nanoseconds();FrameRecord(1);session->Flush();if(session->initialCache&&session->initialCache->valid&&!session->initialCacheWritten){session->DumpCache("observer-cache-initial.bin",*session->initialCache);session->initialCacheWritten=true;}if(session->firstPostCacheCaptured&&!session->firstPostCacheWritten){session->DumpCache("observer-cache-first-post-end.bin",*session->lastCache);session->firstPostCacheWritten=true;}if(session->frame%30==0)session->DurableFlush();session->frameNs+=PD::Nanoseconds()-start;if(session->failed||session->done){session->Finish();return true;}return false;}
    inline void BeginReplacementCapture(_Scene& scene,_Entity)
    {
        if(!session||!session->opened||session->failed||session->attempted)return;auto& s=*session;
        if(s.scene!=&scene){s.Fail("replacement Scene mismatch");return;}s.attempted=true;
        if(!s.lastCache->valid){s.Fail("no completed End cache image before replacement");return;}
        bool physicalMatch=true;for(unsigned i=0;i<s.bodyCount;++i){const auto current=PD::ColliderObservationAccess::Physical(*s.bodies[i],i,[&](const auto* shape){return s.ShapeIndex(shape);});physicalMatch&=current.Same(s.lastCache->physical[i]);}
        s.DumpCache("observer-cache-pre-operation.bin",*s.lastCache);
        {std::ofstream match(s.output/"observer-cache-pre-operation-match.json");match<<"{\"last_completed_physics_steps\":"<<s.lastCache->tick<<",\"observed_tick_count\":"<<s.tick<<",\"physical_sleep_policy_matches_last_End\":"<<physicalMatch<<",\"private_derived_cache_warmth_excluded_from_this_comparison\":true}\n";}
        if(!physicalMatch){s.Fail("authoritative state changed after captured End boundary");return;}
        if(s.scene->GetPhysicsTiming().totalSteps!=s.lastCache->tick){s.Fail("pre-command cached manifold tick mismatch");return;}
        // No Physics transaction intervenes: normal render/selection read the Scene;
        // physical/private-sleep equality above and completed-step identity bind that interval.
        // Refresh every private body cache field at the actual command boundary.
        for(unsigned i=0;i<s.bodyCount;++i){s.lastCache->physical[i]=PD::ColliderObservationAccess::Physical(*s.bodies[i],i,[&](const auto* shape){return s.ShapeIndex(shape);});s.lastCache->bodies[i]=PD::ColliderObservationAccess::Body(*s.bodies[i],i,[&](const auto* shape){return s.ShapeIndex(shape);});}
        s.lastCache->policy={};s.lastCache->policy.Put(scene.GetPhysicsStep().count());s.lastCache->policy.Vec(scene.GetPhysicsSystem()->GetPhysicsWorld()->GetGravity());s.lastCache->policy.Put(std::int32_t(scene.GetPhysicsSystem()->GetSolverIterations()));s.lastCache->policy.Put(std::uint8_t(scene.GetPhysicsSystem()->IsSleepingEnabled()));s.lastCache->policy.Put(std::uint8_t(scene.IsPaused()));
        s.lastCache->stage=100;s.lastCache->frame=s.frame;s.lastCache->tick=s.tick;
        s.DumpCache("observer-cache-command.bin",*s.lastCache);

        const unsigned begin=s.preCount==Session::PreSteps?s.preCursor:0;
        for(unsigned n=0;n<s.preCount;++n){const auto& p=s.pre[(begin+n)%Session::PreSteps];for(unsigned i=0;i<p.count;++i){Session::StateRow row{p.tick,99,i,p.states[i]};s.Write(s.stateFile,&row,1);}}
        for(unsigned i=0;i<s.bodyCount;++i)s.State(i,100);s.Flush();s.DurableFlush();
    }
    inline bool VerifyReplayPreCommandImage(const std::filesystem::path& reference)
    {
        if(!session||session->failed||!session->attempted)return false;auto& s=*session;
        const auto captured=s.output/"observer-cache-command.bin";
        std::error_code ec;const auto size=std::filesystem::file_size(reference,ec);
        if(!reference.is_absolute()||ec||size>4ull*1024*1024||!std::filesystem::is_regular_file(reference)||reference==captured){s.Fail("invalid replay canonical reference");return false;}
        std::ifstream a(reference,std::ios::binary),b(captured,std::ios::binary);bool same=bool(a)&&bool(b)&&std::filesystem::file_size(captured)==size;
        std::array<char,4096> x{},y{};std::uint64_t offset{},firstMismatch=UINT64_MAX;
        while(same&&offset<size){const auto count=std::streamsize((std::min)(std::uint64_t(x.size()),size-offset));a.read(x.data(),count);b.read(y.data(),count);same=bool(a)&&bool(b);
            for(std::streamsize i=0;i<count&&same;++i)if(x[i]!=y[i]){same=false;firstMismatch=offset+i;}offset+=count;}
        std::ofstream result(s.output/"observer-replay-cache-match.json");result<<"{\"exact_canonical_image_match\":"<<same<<",\"reference\":"<<std::quoted(reference.generic_string())<<",\"bytes\":"<<size<<",\"first_mismatch_offset\":"<<firstMismatch<<",\"ignored_fields\":[]}\n";
        if(!same)s.Fail("replay pre-command canonical image mismatch");return same;
    }
    inline void EndReplacementCapture(_Scene& scene,_Entity,bool success)
    {
        if(!session||!session->attempted||session->armed||session->failed)return;auto& s=*session;
        if(s.scene!=&scene||!success){s.Fail("replacement rejected or Scene mismatch");return;}
        for(unsigned i=0;i<s.bodyCount;++i)s.State(i,101);s.Flush();
        // The command may invalidate contacts. Do not present last End's manifold
        // payload as post-command state, or retain borrowed collector pointers.
        // This boundary exports bodies only; the first observed End exports the
        // actual post-command collector through the ordinary boundary callback.
        auto& post=*s.lastCache;post.valid=false;post.stage=101;post.tick=s.tick;post.frame=s.frame;post.bodyCount=s.bodyCount;post.manifoldCount=0;
        for(unsigned i=0;i<s.bodyCount;++i){const auto* shape=s.bodies[i]->m_Shape;bool known=false;for(unsigned j=0;j<s.shapeCount;++j)known|=s.shapeIdentities[j]==shape;
            if(!known){if(s.shapeCount==Session::MaxShapes){s.Fail("post-command shape identity capacity");return;}s.shapeIdentities[s.shapeCount++]=shape;}}
        post.shapeCount=s.shapeCount;post.policy={};post.policy.Put(scene.GetPhysicsStep().count());post.policy.Vec(scene.GetPhysicsSystem()->GetPhysicsWorld()->GetGravity());post.policy.Put(std::int32_t(scene.GetPhysicsSystem()->GetSolverIterations()));post.policy.Put(std::uint8_t(scene.GetPhysicsSystem()->IsSleepingEnabled()));post.policy.Put(std::uint8_t(scene.IsPaused()));
        for(unsigned i=0;i<s.bodyCount;++i){post.physical[i]=PD::ColliderObservationAccess::Physical(*s.bodies[i],i,[&](const auto* shape){return s.ShapeIndex(shape);});post.bodies[i]=PD::ColliderObservationAccess::Body(*s.bodies[i],i,[&](const auto* shape){return s.ShapeIndex(shape);});if(!post.physical[i].valid||!post.bodies[i].valid){s.Fail("post-command body wire overflow");return;}}
        post.valid=!s.failed;if(!post.valid)return;
        s.DumpCache("observer-cache-after-command-bodies.bin",post);
        {const auto path=s.output/"observer-post-cache-coverage.json";if(std::filesystem::exists(path)){s.Fail("post-command cache coverage destination exists");return;}
            std::ofstream coverage(path);coverage<<"{\"schema\":1,\"immediate\":{\"file\":\"observer-cache-after-command-bodies.bin\",\"stage\":101,\"scope\":\"all bodies and private caches immediately after typed command\",\"manifold_payload\":\"NOT OBSERVED at this outside-Physics boundary; zero records is not a live-empty-collector assertion\"},\"first_post_End\":{\"file\":\"observer-cache-first-post-end.bin\",\"stage\":11,\"scope\":\"actual full bodies and active manifolds at first post-command Physics End; exported after Scene.Update\"},\"computed_inertia\":\"Existing serializer compares copied cached accessor results with a second copy whose derived cache is reset; no live getter/cache write\",\"qualification_pass\":false}\n";if(!coverage){s.Fail("post-command cache coverage write");return;}}
        s.DurableFlush();s.armed=!s.failed;
    }
    inline void ObserveFrame(const RenderFrame& frame)
    {
        if(!session||!session->armed||session->failed||session->done)return;auto& s=*session;const auto start=PD::Nanoseconds();std::ostringstream rendered;rendered<<std::setprecision(17);
        for(const auto& draw:frame.Draws()){auto e=s.scene->RenderData().Resolve(draw.entity);if(!e)continue;
            unsigned i=0;for(;i<s.bodyCount;++i)if(s.entities[i]==*e)break;if(i==s.bodyCount)continue;
            const auto state=PD::Capture(*s.bodies[i]);rendered<<s.frame<<','<<s.tick<<','<<i<<','<<s.scene->GetRenderInterpolationAlpha()<<','<<state.position.x<<','<<state.position.y<<','<<state.position.z;
            for(int c=0;c<4;++c)for(int r=0;r<4;++r)rendered<<','<<draw.worldTransform[c][r];rendered<<'\n';
        }
        const auto text=rendered.str();if(text.size()>Session::MaxBodies*1024)s.Fail("render frame output capacity");else s.Write(s.renderFile,text.data(),text.size());s.frameNs+=PD::Nanoseconds()-start;
    }
}
#endif
