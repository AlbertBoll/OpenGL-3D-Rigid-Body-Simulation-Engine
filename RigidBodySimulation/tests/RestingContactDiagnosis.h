#pragma once

// Phase 19's opt-in RBS fixture and recorder. The ordinary app creates no session.
#include "RbsLaunchConfig.h"
#include "RbsState.h"
#include "Physics/PhysicsProfile.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/ShapeSphere.h"
#include "Scene/_Scene.h"
#include "Core/Log.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <new>
#include "Renderer/RenderFrame.h"

namespace Rbs::RestDiagnosis
{
    using namespace ::GEngine;
    namespace PD=::GEngine::PhysicsDiagnostic;
    inline constexpr std::array<unsigned,9> Durations{1800,600,600,240,240,240,240,600,2400};
    inline constexpr std::array<const char*,9> Names{"lattice","ground","friction","impact-0.5",
        "impact-1.0","impact-1.5","off-center","spin","stack"};
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
        std::string failure;

        explicit Session(const RestingContactDiagnostic& c):config(c) {}
        void Fail(const char* reason) noexcept
        {
            if (!failed) { failed=true; failure=reason; Log::GetCoreLogger()->error("PHASE19 diagnostic: {}",reason); }
        }
        bool Open()
        {
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
            events<<"episode,tick,sequence,operation,a,b,nx,ny,nz,ax,ay,az,bx,by,bz,jx,jy,jz,tx,ty,tz,l0_before,l1_before,l2_before,l0_after,l1_after,l2_after,restitution,closing_speed,penetration";
            for (auto prefix:{"a_before","b_before","a_after","b_after"})
                for (auto field:{"px","py","pz","vx","vy","vz","wx","wy","wz"}) events<<','<<prefix<<'_'<<field;
            events<<'\n';
            frames<<"episode,tick,elapsed,pending,discarded,total_steps,steps_this_frame,interpolation\n";
            initial<<"episode,index,slot,generation,shape,type,inverse_mass,elasticity,friction,layer,mask,radius,px,py,pz,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,spin_length,rolling_length,linear_sleep,angular_sleep,dwell,ixx,iyy,izz\n";
            sink={this,config.observation==Observation::Observed,&Select,&Event,&Boundary};
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
        void RecordBodies(const PhysicsWorld& world)
        {
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
            if (stage==PD::Stage::Begin) {
                ++self.tick; self.stageClock=self.stepClock=now; self.callbackNs=0; self.detailedNs=0;
                if (passes!=1 || self.tick>Durations[self.episode]) self.Fail("tick/pass budget mismatch");
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
            if (failed || tick==Durations[episode]) {
                episodeDone=true; scene->SetPaused(true);
            }
        }
        bool BeginEpisode(_Scene& target,SceneRenderResources& resources,
                          const SceneAssets& sceneAssets,std::vector<std::unique_ptr<PhysicalShape>>& shapes)
        {
            PD::observer=nullptr;
            scene=&target; assets=sceneAssets;
            if (!entities.empty()) {
                scene->OnRuntimeStop();
                for (auto entity:entities) if (auto removed=scene->DestroyEntity(entity);!removed) {
                    Fail("fixture entity retirement failed"); return false;
                }
                entities.clear(); shapes.clear();
            }
            inputs=episode==0?lattice:Focused(episode);
            entities.resize(inputs.size());
            // EnTT's packed component view visits reverse insertion order. Author
            // in reverse so PhysicsWorld construction matches export order exactly.
            for (std::size_t n=inputs.size();n>0;--n) {
                const auto i=n-1; const auto& b=inputs[i];
                auto created=scene->CreateEntity("P19 "+std::string(Names[episode])+" "+std::to_string(i));
                if (!created) { Fail("fixture entity creation failed"); return false; }
                auto entity=*created; entities[i]=entity;
                auto& transform=entity.GetComponent<Component::Transform3DComponent>();
                transform.Translation=b.position; transform.QuatRotation=b.orientation;
                if (b.shape==1) {
                    Vec3f low=b.points.front(),high=low;
                    for (auto p:b.points) { low=glm::min(low,p); high=glm::max(high,p); }
                    transform.Scale=high-low;
                }
                const auto mesh=b.shape==0?assets.smoothSphereGeo:assets.boxMesh;
                const auto material=b.type==0?assets.floorMaterial:(b.shape==0?assets.sphereMaterial:assets.boxMaterial);
                if (auto assigned=resources.AssignRenderable(entity,{mesh,material});!assigned) {
                    Fail("fixture render assignment failed"); return false;
                }
                if (b.shape==1) if (auto attached=resources.AttachPhysicsShape(entity,"Box");!attached) {
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
                } else {
                    Component::BoxFixture3DComponent fixture; fixture.Property=property;
                    entity.AddOrReplaceComponent<Component::BoxFixture3DComponent>(fixture);
                }
            }
            shapes.reserve(inputs.size());
            if (auto started=scene->OnRuntimeStart();!started) { Fail("fixture runtime construction failed"); return false; }
            auto* system=scene->GetPhysicsSystem(); auto* world=system->GetPhysicsWorld();
            const auto& all=world->GetPhysicsBodies();
            for (auto* b:all) shapes.emplace_back(b->m_Shape);
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
                if (source.shape==1) b.m_Shape->Build(source.points);
                if (!b.m_Shape->IsValid() || !b.SetSpinResistanceLength(.05f) || !b.SetRollingResistanceLength(.05f) ||
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
            RecordBodies(*world);
            episodeDone=false;
            PD::observer=&sink;
            Log::GetCoreLogger()->info("PHASE19_EPISODE_BEGIN {} bodies={} ticks={}",Names[episode],all.size(),Durations[episode]);
            return !failed && bodies && initial;
        }
        bool FrameComplete(double elapsed)
        {
            const auto& t=scene->GetPhysicsTiming();
            frames<<episode<<','<<tick<<','<<elapsed<<','<<t.pendingSeconds<<','<<t.discardedSeconds<<','
                <<t.totalSteps<<','<<t.stepsLastUpdate<<','<<scene->GetRenderInterpolationAlpha()<<'\n';
            if (!frames) Fail("frame accounting write failed");
            if (episodeDone && !failed) {
                if (t.totalSteps!=Durations[episode] || scene->SetPhysicsStep(Seconds{1.0/60.0}) ||
                    scene->GetPhysicsStep().count()!=1.0/120.0) { Fail("clock immutability/tick count failed"); return false; }
                ++completedEpisodes;
                Log::GetCoreLogger()->info("PHASE19_EPISODE_COMPLETE {} ticks={}",Names[episode],tick);
                if (completedEpisodes==Durations.size()) { complete=true; PD::observer=nullptr; }
            }
            return !failed;
        }
        bool FinishAfterAppShutdown(bool clean)
        {
            PD::observer=nullptr;
            for (auto* out:{&bodies,&stages,&contacts,&timing,&metrics,&events,&frames,&initial,&islandRows}) {
                out->flush(); if (!*out) Fail("final evidence flush failed"); out->close();
            }
            std::ofstream out(std::filesystem::path(config.outputDirectory)/"result.json");
            const bool ok=clean && complete && !failed && completedEpisodes==9;
            out<<"{\"schema\":1,\"complete\":"<<(ok?"true":"false")<<",\"episodes\":"<<completedEpisodes
                <<",\"detailed_events\":"<<eventCount<<",\"common_recorder_ns\":"<<recorderNs
                <<",\"shutdown\":"<<(clean?"true":"false")<<",\"failure\":\""<<failure<<"\"}\n";
            return ok && bool(out);
        }
    };
    inline std::unique_ptr<Session> session;

    // Separate normal-scene observation. It owns evidence only, never a body,
    // world, render lease, clock, camera or Physics setting.
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
        static void Boundary(void* context,PD::Stage boundary,const PhysicsWorld& world,
            ManifoldCollector& manifolds,std::span<const ContactIsland>,std::size_t,int passes) noexcept
        {
            auto& s=*static_cast<TargetSession*>(context); const auto start=PD::Nanoseconds();
            if (s.failed) return;
            if (boundary==PD::Stage::Begin) {
                ++s.tick; s.stage=0; s.used=0; s.sequence=0; s.boundaryNs=0; s.tickFlushed=false;
                s.tickStart=start; s.tickObservationStart=s.sink.observationNs; s.direct={};
                if (s.tick>1800 || passes!=1 || !s.Invariants(false)) { s.Fail("tick/pass/identity invariant"); return; }
            }
            s.stage=unsigned(boundary);
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
        }
        bool Open(_Scene& owner)
        {
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
            // BaseApp also renders its terminal iteration after ShutDown. Keep
            // its own frame ID; do not alter that normal presentation behavior.
            ++frame; lastFrameElapsed=elapsed; frameExtracted=frameSubmitted=false;
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
                <<",\"off_on_equivalence_measured\":false}\n";
            return clean && !failed && bool(out);
        }
    };
    inline std::unique_ptr<TargetSession> targetSession;
}
