#pragma once
#include "RigidBodySimulation.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <limits>

namespace Rbs {
// Explicit opt-in diagnostic of the actual authored scene. No qualification hooks,
// alternate solver or altered scheduler thresholds. One fixed scene tick per frame.
struct JoltParameterExperiment {
    struct Session {
        std::filesystem::path path;std::ofstream frames,contacts,bodies;unsigned tick{},loggedContacts{},waitingTick{};
        bool waiting{},observed{};std::array<float,4096> initialY{};
    };
    static inline std::unique_ptr<Session> session;
    static void Snapshot(GEngine::PhysicsWorld& world,std::ostream& out,unsigned tick) {
        for(std::size_t i=0;i<world.GetBodyCount();++i){auto h=world.BodyAt(i);if(!h)continue;
            auto b=world.ReadBody(*h);auto p=world.ReadBodyProperties(*h);if(!b||!p)continue;
            out<<tick<<','<<h->index<<','<<int(b->type)<<','<<int(b->shapeType)<<','<<b->position.x<<','<<b->position.y<<','<<b->position.z<<','
                <<b->orientation.w<<','<<b->orientation.x<<','<<b->orientation.y<<','<<b->orientation.z<<','
                <<b->linearVelocity.x<<','<<b->linearVelocity.y<<','<<b->linearVelocity.z<<','<<b->angularVelocity.x<<','<<b->angularVelocity.y<<','<<b->angularVelocity.z<<','
                <<p->mass<<','<<p->material.friction<<','<<p->material.restitution<<','<<p->linearDamping<<','<<p->angularDamping<<','<<p->gravityFactor<<','
                <<p->continuousCollision<<','<<p->allowSleeping<<','<<p->velocityStepsOverride<<','<<p->positionStepsOverride<<','<<b->sleeping<<'\n';
        }
    }
    static void Settings(std::ostream& o,const char* label,const GEngine::PhysicsJoltSettings& p) {
        o<<label<<" velocity="<<p.velocitySteps<<" position="<<p.positionSteps<<" collisionSteps="<<p.collisionSteps
            <<" minRestitutionVelocity="<<p.minRestitutionVelocity<<" baumgarte="<<p.baumgarte<<" slop="<<p.penetrationSlop<<" speculative="<<p.speculativeDistance
            <<" allowSleep="<<p.allowSleeping<<" sleepTime="<<p.timeBeforeSleep<<" pointSpeed="<<p.pointVelocitySleepThreshold
            <<" warmStart="<<p.warmStarting<<" pairCache="<<p.bodyPairCache<<" manifoldReduction="<<p.manifoldReduction<<'\n';
    }
    static void Wait(RigidBodySimulationApp& app,unsigned tick) {
        auto& s=*session;s.waiting=true;s.waitingTick=tick;app.SetSimulationState(SimulationState::Paused);
        std::ofstream(s.path/("capture-"+std::to_string(tick)+".ready"))<<"Actual paused scene tick "<<tick<<'\n';
    }
    static bool Boundary(RigidBodySimulationApp& app) {
        using namespace GEngine;
        if(!session){
            auto next=std::make_unique<Session>();next->path=app.m_Launch.joltParameterOutput;
            // Never overwrite a previous experiment's measurements.
            if(std::filesystem::exists(next->path/"frames.csv")){app.ShutDown();return false;}
            std::filesystem::create_directories(next->path);session=std::move(next);auto& s=*session;
            auto& world=*app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();
            const auto baseline=world.ReadJoltSettings();if(!baseline){app.ShutDown();return false;}
            std::ofstream meta(s.path/"settings.txt");meta<<std::setprecision(9)<<"EXPERIMENTAL profile="<<app.m_Launch.joltParameterProfile<<" dt="<<app.m_ActiveScene->GetPhysicsStep().count()<<" bodies="<<world.GetBodyCount()<<"\ncombine friction=product restitution=product\n";
            Settings(meta,"preserved",*baseline);Settings(meta,"sdk",*world.ReadJoltNativeDefaults());auto p=*baseline;
            const auto& profile=app.m_Launch.joltParameterProfile;
            if(profile=="threshold-025")p.minRestitutionVelocity=.25f;
            if(profile=="velocity-16")p.velocitySteps=16;
            if(profile=="velocity-32")p.velocitySteps=32;
            if(profile=="position-4")p.positionSteps=4;
            if(profile=="position-8")p.positionSteps=8;
            if(profile=="sleep-off")p.allowSleeping=false;
            if(!world.UpdateJoltSettings(p)){app.ShutDown();return false;}
            s.bodies.open(s.path/"bodies.csv");s.bodies<<std::setprecision(9)<<"tick,index,type,shape,x,y,z,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz,mass,friction,restitution,linearDamping,angularDamping,gravityFactor,ccd,allowSleep,velocityOverride,positionOverride,sleeping\n";
            // -1 records exact preserved scene inputs, 0 records the isolated profile.
            Snapshot(world,s.bodies,unsigned(-1));
            for(std::size_t i=0;i<world.GetBodyCount();++i){auto h=world.BodyAt(i);if(!h)continue;auto b=world.ReadBody(*h);if(!b)continue;
                if(h->index<s.initialY.size())s.initialY[h->index]=b->position.y;
                if(profile=="restitution-050"){auto body=*world.ReadBodyProperties(*h);body.material.restitution=std::sqrt(.5f);if(!world.UpdateBodyProperties(*h,body)){app.ShutDown();return false;}}
            }
            Snapshot(world,s.bodies,0);Settings(meta,"effective",*world.ReadJoltSettings());
            s.observed=profile!="baseline-unobserved";
            if(!world.EnableJoltDiagnostics(s.observed)){app.ShutDown();return false;}
            s.frames.open(s.path/"frames.csv");s.frames<<std::setprecision(10)<<"tick,seconds,scene_cpu_ms,jolt_cpu_ms,callbacks,added,overflow,max_depth,min_vn_before,max_vn_after,approaching_below_threshold,active,sleeping,kinetic_energy,mean_y,min_y,max_y,max_abs_vy,max_up_vy,rms_speed,bottom_mean_y,bottom_max_y\n";
            s.contacts.open(s.path/"contacts.csv");s.contacts<<std::setprecision(9)<<"tick,a,b,added,depth,friction,restitution,vn_before,vn_after\n";
            app.m_EditorMode=RigidBodySimulationApp::EditorMode::Simulation;
            app.SetSimulationState(SimulationState::Running);
            if(app.m_Launch.joltParameterCapture)Wait(app,0);
        }
        auto& s=*session;
        if(s.waiting && std::filesystem::exists(s.path/("capture-"+std::to_string(s.waitingTick)+".ack"))){
            s.waiting=false;
            if(s.tick>=app.m_Launch.joltParameterTicks){std::ofstream(s.path/"complete.txt")<<s.tick;app.ShutDown();return false;}
            app.SetSimulationState(SimulationState::Running);
        }
        return true;
    }
    static void AfterTick(RigidBodySimulationApp& app,double sceneMs) {
        using namespace GEngine;auto& s=*session;++s.tick;
        auto& world=*app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld();auto diag=world.ReadJoltDiagnostics();
        if(app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps()!=s.tick || (s.observed && (!diag || diag->step!=s.tick))){
            std::ofstream(s.path/"failed.txt")<<"Scheduler/observer tick mismatch at "<<s.tick;app.ShutDown();return;}

        std::size_t added=0,below=0;float depth=0,minBefore=0,maxAfter=0;
        const auto settings=*world.ReadJoltSettings();
        if(diag && diag->enabled)for(const auto& c:diag->contacts){
            added+=c.added;depth=std::max(depth,c.penetration);minBefore=std::min(minBefore,c.normalVelocityBefore);maxAfter=std::max(maxAfter,c.normalVelocityAfter);
            if(c.normalVelocityBefore<0 && c.normalVelocityBefore>-settings.minRestitutionVelocity)++below;
            if(s.loggedContacts<100000 && (c.added || c.normalVelocityBefore<-.25f)){
                s.contacts<<s.tick<<','<<c.bodyA.index<<','<<c.bodyB.index<<','<<c.added<<','<<c.penetration<<','<<c.friction<<','<<c.restitution<<','<<c.normalVelocityBefore<<','<<c.normalVelocityAfter<<'\n';++s.loggedContacts;
            }
        }
        unsigned active=0,sleeping=0,bottom=0;double energy=0,sumY=0,speed2=0,bottomY=0;float minY=FLT_MAX,maxY=-FLT_MAX,maxVy=0,upVy=0,bottomMax=-FLT_MAX;
        for(std::size_t i=0;i<world.GetBodyCount();++i){auto h=world.BodyAt(i);if(!h)continue;auto b=world.ReadBody(*h);if(!b||b->type!=Component::BodyType::Dynamic)continue;
            if(b->sleeping)++sleeping;else ++active;
            const auto v2=glm::dot(b->linearVelocity,b->linearVelocity);speed2+=v2;
            energy+=.5*v2/b->inverseMass;
            if(std::abs(glm::determinant(b->inverseInertiaWorld))>1e-12f)energy+=.5*glm::dot(b->angularVelocity,glm::inverse(b->inverseInertiaWorld)*b->angularVelocity);
            sumY+=b->position.y;minY=std::min(minY,b->position.y);maxY=std::max(maxY,b->position.y);maxVy=std::max(maxVy,std::abs(b->linearVelocity.y));upVy=std::max(upVy,b->linearVelocity.y);
            if(h->index<s.initialY.size() && s.initialY[h->index]==10.f){++bottom;bottomY+=b->position.y;bottomMax=std::max(bottomMax,b->position.y);}
        }
        const auto count=active+sleeping;
        s.frames<<s.tick<<','<<s.tick/60.0<<','<<sceneMs<<','<<(diag&&diag->enabled?diag->updateMilliseconds:-1)<<','<<(diag?diag->callbacks:0)<<','<<added<<','<<(diag?diag->overflow:0)<<','
            <<depth<<','<<minBefore<<','<<maxAfter<<','<<below<<','<<active<<','<<sleeping<<','<<energy<<','<<sumY/std::max(1u,count)<<','<<minY<<','<<maxY<<','<<maxVy<<','<<upVy<<','<<std::sqrt(speed2/std::max(1u,count))<<','<<bottomY/std::max(1u,bottom)<<','<<bottomMax<<'\n';
        Snapshot(world,s.bodies,s.tick);
        if(app.m_Launch.joltParameterCapture && (s.tick==120 || s.tick==240 || s.tick==600 || s.tick==app.m_Launch.joltParameterTicks)){
            s.frames.flush();s.contacts.flush();s.bodies.flush();Wait(app,s.tick);
        }else if(s.tick>=app.m_Launch.joltParameterTicks){s.frames.flush();s.contacts.flush();s.bodies.flush();std::ofstream(s.path/"complete.txt")<<s.tick;app.ShutDown();}
    }
};
}
