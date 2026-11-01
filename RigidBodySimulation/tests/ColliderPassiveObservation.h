#pragma once
#ifdef GENGINE_COLLIDER_PASSIVE_OBSERVATION
#ifndef GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC
#error Passive observation requires the existing isolated collider diagnostic build.
#endif
#include "ColliderReplacementObservation.h"
#include "ColliderReplacementReplay.h"
#include <cstdio>

// Diagnostic fixture only. Scene::Update retains the sole ordinary scheduler.
// No Physics/query sink is installed, no live derived cache is read, and no
// sleeping, timestep, contact, iteration, or body mutation is performed here.
namespace Rbs::ColliderPassive
{
    using namespace ::GEngine;
    namespace PD=PhysicsDiagnostic;
    namespace Detailed=ColliderReplacementObservation;
    enum class Mode { Disabled,Regression,Parity };
    struct Configuration {bool checked{},valid{true};Mode mode{};RbsScenePreset preset{RbsScenePreset::SphereBoxStack};const char* failure="";};
    inline Configuration config;
    inline std::string Environment(const char* name)
    {char* raw=nullptr;std::size_t size{};_dupenv_s(&raw,&size,name);std::string value=raw?raw:"";std::free(raw);return value;}
    inline bool Configure()
    {
        if(config.checked)return config.valid;config.checked=true;
        const auto mode=Environment("GENGINE_COLLIDER_PASSIVE_MODE"),preset=Environment("GENGINE_COLLIDER_PASSIVE_PRESET");
        const auto reject=[](const char* why){config.valid=false;config.failure=why;return false;};
        if(mode.empty()){if(!preset.empty())return reject("passive preset requires explicit passive mode");return true;}
        if(mode=="regression")config.mode=Mode::Regression;else if(mode=="parity")config.mode=Mode::Parity;else return reject("passive mode must be regression or parity");
        if(preset=="SphereLattice")config.preset=RbsScenePreset::SphereLattice;
        else if(preset=="BoxStack")config.preset=RbsScenePreset::BoxStack;
        else if(preset=="SphereBoxStack"||preset.empty())config.preset=RbsScenePreset::SphereBoxStack;
        else return reject("passive preset must be SphereLattice, BoxStack or SphereBoxStack");
        if(!ColliderCapture::Enabled())return reject("passive output directory failed existing confinement admission");
        if(ColliderCapture::AutoFitEnabled())return reject("passive fixture excludes interactive auto-fit commands");
        const bool replay=ColliderReplay::Enabled();
        if(ColliderReplay::request.failed)return reject(ColliderReplay::Failure());
        if(config.mode==Mode::Regression&&replay)return reject("passive regression requires no replay manifest");
        if(config.mode==Mode::Parity&&(!replay||config.preset!=RbsScenePreset::SphereBoxStack))return reject("passive parity requires exact SphereBoxStack replay manifest");
        return true;
    }
    inline double FrameElapsed(double value){return config.mode==Mode::Regression?1./60.:value;}
    struct Session
    {
        static constexpr unsigned MaxBodies=192,WindowSteps=1800,TailSteps=120;
        using Row=Detailed::Session::StateRow;
        _Scene* scene{};std::filesystem::path output;std::ofstream states,frames;
        std::array<const RigidBody3D*,MaxBodies> bodies{};std::array<Row,MaxBodies> rows{};
        unsigned count{};std::uint64_t startSteps{},lastSteps{},frame{},bytes{},updateStart{};
        unsigned tailFrames{},tailDynamic{},tailSleeping{},tailSpeedQuiet{};
        double tailMaxLinear{},tailMaxAngular{};
        bool opened{},failed{},complete{},finished{};const char* failure="";
        PhysicsProfileSnapshot profileBefore{};
        void Fail(const char* reason){if(!failed){failed=true;failure=reason;std::fprintf(stderr,"Collider passive: %s\n",reason);}}
        template<class T>void Write(std::ofstream& stream,const T* data,std::size_t n)
        {
            const auto size=std::uint64_t(sizeof(T))*n;
            const auto prefix=Detailed::session?Detailed::session->bytes:0;
            if(size>Detailed::Session::ExportLimit||bytes>Detailed::Session::ExportLimit-size||prefix>Detailed::Session::ExportLimit-bytes-size){Fail("combined diagnostic binary/CSV export bound");return;}
            stream.write(reinterpret_cast<const char*>(data),size);bytes+=size;if(!stream)Fail("passive evidence write");
        }
        bool NoSinks()
        {
            bool clean=!PD::observer&&!PD::queryObserver;
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
            clean=clean&&!PD::Coupled::sink;
#endif
            if(!clean)Fail("unexpected Physics or query observer in passive window");return clean;
        }
        bool Policy()
        {
            auto* system=scene->GetPhysicsSystem();
            if(!system||!system->GetPhysicsWorld()||system->GetSolverIterations()!=1||!system->IsSleepingEnabled()||scene->GetPhysicsStep().count()!=1./60.||system->GetPhysicsWorld()->GetGravity()!=Vec3f(0,-12,0)){Fail("ordinary default Physics policy changed");return false;}
            if(config.mode==Mode::Regression&&scene->IsPaused()){Fail("passive regression paused");return false;}
            const auto& list=PhysicsTestAccess::Bodies(system->GetPhysicsWorld());
            if(list.size()!=count){Fail("passive body membership changed");return false;}
            for(unsigned i=0;i<count;++i)if(list[i]!=bodies[i]||bodies[i]->GetSleepSettings()!=RigidBody3D::SleepSettings{}){Fail("passive body identity/order or default sleep policy changed");return false;}
            return NoSinks();
        }
        void Open(_Scene& owner)
        {
            scene=&owner;auto* system=owner.GetPhysicsSystem();if(!system||!system->GetPhysicsWorld()){Fail("passive missing Physics world");return;}
            const auto& list=PhysicsTestAccess::Bodies(system->GetPhysicsWorld());count=unsigned(list.size());
            const unsigned expected=config.preset==RbsScenePreset::SphereLattice?185:config.preset==RbsScenePreset::BoxStack?21:22;
            if(count>MaxBodies||count!=expected){Fail("passive preset body-count admission");return;}
            for(unsigned i=0;i<count;++i)bodies[i]=list[i];if(!Policy())return;
            const auto dir=ColliderCapture::directory;
            for(const char* name:{"passive-states.bin","passive-frames.csv","passive-initial-scene.json","passive-layout.json","passive-ready.json","passive-status.json"})if(std::filesystem::exists(dir/name)){Fail("passive evidence destination already exists");return;}
            output=dir;startSteps=lastSteps=owner.GetPhysicsTiming().totalSteps;
            if(config.mode==Mode::Regression&&startSteps!=0){Fail("regression did not begin before first ordinary Physics step");return;}
            states.open(output/"passive-states.bin",std::ios::binary);frames.open(output/"passive-frames.csv");
            frames<<"frame,total_steps,post_steps,steps_last_update,elapsed_seconds,pending_seconds,discarded_seconds,total_discarded_seconds,paused,scene_update_ns,profile_enabled,profile_steps,physics_ns,broadphase_ns,narrowphase_ns,contacts_ns,islands_ns,solver_ns,resolution_ns,gravity_ns,integration_ns,last_step_manifolds,last_step_contacts,dynamic,sleeping,speed_quiet,max_linear,max_angular,terminal_overshoot\n";
            if(!states||!frames){Fail("passive output open");return;}
            {std::ofstream f(output/"passive-initial-scene.json");f<<std::setprecision(17);ColliderCapture::Snapshot(f,owner);if(!f){Fail("passive initial snapshot write");return;}}
            {std::ofstream f(output/"passive-layout.json");f<<"{\"row_size\":"<<sizeof(Row)<<",\"tick_offset\":"<<offsetof(Row,tick)<<",\"stage_offset\":"<<offsetof(Row,stage)<<",\"body_offset\":"<<offsetof(Row,body)<<",\"state_offset\":"<<offsetof(Row,state)<<",\"state_size\":"<<sizeof(PD::BodyState)<<",\"state_fields\":{\"position\":"<<offsetof(PD::BodyState,position)<<",\"linear\":"<<offsetof(PD::BodyState,linear)<<",\"angular\":"<<offsetof(PD::BodyState,angular)<<",\"orientation\":"<<offsetof(PD::BodyState,orientation)<<",\"inactive\":"<<offsetof(PD::BodyState,inactive)<<",\"sleeping\":"<<offsetof(PD::BodyState,sleeping)<<"},\"end_stage\":"<<unsigned(PD::Stage::End)<<",\"tick\":\"totalSteps minus one; completed Scene.Update only; batch intermediate steps excluded\",\"comparison\":\"semantic fields only; native padding is not state\"}\n";if(!f){Fail("passive layout write");return;}}
            {std::ofstream f(output/"passive-ready.json");f<<"{\"ready\":true,\"mode\":\""<<(config.mode==Mode::Regression?"regression":"parity")<<"\",\"preset\":"<<int(config.preset)<<",\"start_steps\":"<<startSteps<<",\"body_count\":"<<count<<",\"post_steps\":1800,\"tail_steps\":120,\"physics_query_sinks\":false,\"profiling_available\":"<<IsPhysicsProfilingEnabled()<<",\"qualification_pass\":false}\n";if(!f){Fail("passive ready write");return;}}
            opened=true;
        }
        void Before(double elapsed)
        {
            if(!opened||failed)return;if(!Policy())return;
            if(++frame>250000){Fail("passive frame bound");return;}
            if(!std::isfinite(elapsed)||elapsed<0){Fail("passive invalid elapsed value");return;}
            profileBefore=GetPhysicsProfileSnapshot();updateStart=PD::Nanoseconds();
        }
        void After(double elapsed)
        {
            const auto updateNs=PD::Nanoseconds()-updateStart;const auto profile=GetPhysicsProfileSnapshot();
            if(!opened||failed)return;if(!Policy())return;
            const auto& t=scene->GetPhysicsTiming();
            if(t.totalSteps<lastSteps||t.totalSteps-lastSteps!=t.stepsLastUpdate){Fail("passive ordinary scheduler accounting mismatch");return;}
            if(config.mode==Mode::Regression&&(t.stepsLastUpdate!=1||t.discardedSeconds!=0||t.totalDiscardedSeconds!=0)){Fail("regression requires exactly one ordinary step per fixed-elapsed frame");return;}
            const auto post=t.totalSteps-startSteps;unsigned dynamic=0,sleeping=0,quiet=0;double maxLinear=0,maxAngular=0;
            for(unsigned i=0;i<count;++i){const auto& b=*bodies[i];const auto s=PD::Capture(b);if(!Detailed::Session::Finite(s)){Fail("nonfinite passive raw body state");return;}
                RigidBody3D copy=b;const auto inertia=copy.GetInverseInertiaTensorWorldSpace();for(int c=0;c<3;++c)if(!Math::IsFinite(inertia[c])){Fail("nonfinite passive copied inertia");return;}
                rows[i]={t.totalSteps?t.totalSteps-1:0,unsigned(PD::Stage::End),i,s};
                if(b.Type!=BodyType::Dynamic||b.m_InvMass<=0)continue;++dynamic;sleeping+=unsigned(s.sleeping);
                const double linear=glm::length(glm::dvec3(s.linear)),angular=glm::length(glm::dvec3(s.angular));const auto policy=b.GetSleepSettings();
                quiet+=unsigned(glm::dot(glm::dvec3(s.linear),glm::dvec3(s.linear))<=double(policy.linearSpeedThreshold)*policy.linearSpeedThreshold&&glm::dot(glm::dvec3(s.angular),glm::dvec3(s.angular))<=double(policy.angularSpeedThreshold)*policy.angularSpeedThreshold);maxLinear=(std::max)(maxLinear,linear);maxAngular=(std::max)(maxAngular,angular);
            }
            const bool advanced=t.totalSteps!=lastSteps;if(advanced)Write(states,rows.data(),count);
            if(advanced&&post>WindowSteps-TailSteps&&post<=WindowSteps){++tailFrames;tailDynamic+=dynamic;tailSleeping+=sleeping;tailSpeedQuiet+=quiet;tailMaxLinear=(std::max)(tailMaxLinear,maxLinear);tailMaxAngular=(std::max)(tailMaxAngular,maxAngular);}
            const bool profiling=IsPhysicsProfilingEnabled();
            const auto delta=[&](std::uint64_t PhysicsProfileSnapshot::* field){if(!profiling)return std::uint64_t{};if(profile.*field<profileBefore.*field){Fail("profiling counter reset inside Scene.Update");return std::uint64_t{};}return profile.*field-profileBefore.*field;};
            const auto profileSteps=delta(&PhysicsProfileSnapshot::stepCount);if(profiling&&profileSteps!=t.stepsLastUpdate)Fail("PhysicsProfile step count differs from ordinary scheduler");
            std::ostringstream out;out<<std::setprecision(17)<<frame<<','<<t.totalSteps<<','<<post<<','<<t.stepsLastUpdate<<','<<elapsed<<','<<t.pendingSeconds<<','<<t.discardedSeconds<<','<<t.totalDiscardedSeconds<<','<<scene->IsPaused()<<','<<updateNs<<','<<profiling<<','<<profileSteps;
            for(auto field:{&PhysicsProfileSnapshot::physicsWorldTimeNs,&PhysicsProfileSnapshot::broadphaseTimeNs,&PhysicsProfileSnapshot::narrowphaseTimeNs,&PhysicsProfileSnapshot::manifoldTimeNs,&PhysicsProfileSnapshot::contactIslandBuildTimeNs,&PhysicsProfileSnapshot::solverTimeNs,&PhysicsProfileSnapshot::contactResolutionTimeNs,&PhysicsProfileSnapshot::gravityTimeNs,&PhysicsProfileSnapshot::integrationTimeNs})out<<','<<delta(field);
            out<<','<<profile.manifoldCount<<','<<profile.manifoldContactCount<<','<<dynamic<<','<<sleeping<<','<<quiet<<','<<maxLinear<<','<<maxAngular<<','<<(post>WindowSteps)<<'\n';const auto line=out.str();Write(frames,line.data(),line.size());
            lastSteps=t.totalSteps;if(frame%30==0){states.flush();frames.flush();if(!states||!frames)Fail("passive periodic flush");}
            if(post>=WindowSteps)complete=true;
        }
        void Finish()
        {
            if(finished)return;finished=true;states.flush();frames.flush();if(opened&&(!states||!frames))Fail("passive final flush");
            if(output.empty())return;std::ofstream f(output/"passive-status.json");f<<std::setprecision(17)<<"{\"status\":\""<<(failed?"FAIL":complete?"CHARACTERIZATION_COMPLETE":"INCOMPLETE")<<"\",\"failure\":\""<<failure<<"\",\"mode\":\""<<(config.mode==Mode::Regression?"regression":"parity")<<"\",\"start_steps\":"<<startSteps<<",\"end_steps\":"<<lastSteps<<",\"observed_frames\":"<<frame<<",\"bytes\":"<<bytes<<",\"tail_observed_step_end_frames\":"<<tailFrames<<",\"tail_dynamic_samples\":"<<tailDynamic<<",\"tail_sleeping_samples\":"<<tailSleeping<<",\"tail_speed_quiet_samples\":"<<tailSpeedQuiet<<",\"tail_max_linear\":"<<tailMaxLinear<<",\"tail_max_angular\":"<<tailMaxAngular<<",\"qualification_pass\":false,\"prefix_observer_incomplete_by_design\":"<<(config.mode==Mode::Parity)<<",\"limits\":\"No intermediate batched-step states or contacts; no automatic settling PASS; CPU values are raw diagnostics without overhead subtraction; no rendering CPU is included in Scene.Update timer\"}\n";
            if(!f)std::fprintf(stderr,"Collider passive: status write failed\n");
        }
        ~Session(){Finish();}
    };
    inline std::unique_ptr<Session> session;
    inline bool UseDetailed(){return config.mode!=Mode::Regression&&!session;}
    inline bool BeforeStep(_Scene& scene,double elapsed)
    {
        if(config.mode==Mode::Disabled)return true;
        if(config.mode==Mode::Parity&&!ColliderReplay::request.performed)return true;
        if(!session){
            session=std::make_unique<Session>();
            if(config.mode==Mode::Parity){
                if(!Detailed::session||!Detailed::session->armed||Detailed::session->failed){session->Fail("passive transfer lacks successful strict replay command admission");return false;}
                Detailed::session->Finish();if(Detailed::session->failed){session->Fail("detailed prefix evidence flush failed");return false;}
            }
            session->Open(scene);
        }
        session->Before(elapsed);if(session->failed){session->Finish();return false;}return true;
    }
    inline bool AfterStep(double elapsed)
    {
        if(!session)return false;session->After(elapsed);if(session->failed||session->complete){session->Finish();return true;}return false;
    }
}
#endif
