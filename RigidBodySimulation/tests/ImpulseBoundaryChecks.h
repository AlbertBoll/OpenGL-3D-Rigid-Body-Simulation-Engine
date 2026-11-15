#pragma once
#include "RigidBodySimulation.h"
#include <chrono>
#include <cstdio>
#include <algorithm>

namespace Rbs {
struct ImpulseBoundaryChecks {
    static int Run() {
        using namespace ::GEngine;using namespace ::GEngine::Component;
#define IB(x) do {if(!(x)){std::fprintf(stderr,"IMPULSE_BOUNDARY_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(false)
        std::setvbuf(stdout,nullptr,_IONBF,0);
        for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt}) {
            PhysicsSystem system;auto* world=new PhysicsWorld;IB(world->ConfigureBackend(backend));system.SetPhysicsWorld(world);world->SetGravity({0,0,0});
            auto shape=CreateCollisionShape(BoxShapeDesc{Vec3f(1)});IB(shape);PhysicsBodyDesc desc;
            auto h=world->CreateBody(desc,*shape);IB(h);
            auto props=*world->ReadBodyProperties(*h);props.mass=4;IB(world->UpdateBodyProperties(*h,props));
            auto read=world->ReadImpulseMotion(*h);IB(read && read->inverseMass==.25f);
            PhysicsImpulseCommand c{*h,{2,0,0}};IB(world->ApplyImpulseBatch({&c,1}));IB(world->ReadImpulseMotion(*h)->linear==Vec3f(.5f,0,0));
            IB(system.GetCompletedSteps()==0);
            IB(world->SetBodyVelocity(*h,{0,0,0},{0,0,0}));
            if(backend==PhysicsBackend::Jolt)IB(!world->ApplyImpulseBatch({&c,1})); // Pending legacy mirror write remains a real rejection.
            system.Update(Timestep(1./60.));IB(!system.GetBackendFailure());
            IB(world->ReadImpulseMotion(*h));
            for(int i=0;i<120;++i)system.Update(Timestep(1./60.));
            IB(world->ReadImpulseMotion(*h)->sleeping);
            const auto tick=system.GetCompletedSteps();
            c.mode=PhysicsImpulseMode::VelocityChange;c.linear={0,.25f,0};c.angular={.2f,.3f,.4f};
            IB(world->ApplyImpulseBatch({&c,1}));read=world->ReadImpulseMotion(*h);
            IB(read && !read->sleeping && glm::length(read->linear-c.linear)<1.e-5f && glm::length(read->angular-c.angular)<1.e-5f);
            IB(system.GetCompletedSteps()==tick && world->ReadBodyProperties(*h)->mass==4);
            std::printf("IMPULSE_BOUNDARY_PASS backend=%s paused_mass_edit legacy_pending_guard sleeping_wake angular_delta no_added_step\n",PhysicsBackendName(backend).data());
        }
        // Same paused geometry and commands, with no compilation/desktop work in parallel.
        // Measure complete API staging, validation, mutation and readback, not Physics Step.
        for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt})for(std::size_t count:{1u,180u,512u}) {
            PhysicsWorld world;IB(world.ConfigureBackend(backend));std::vector<PhysicsImpulseCommand> commands;
            for(std::size_t i=0;i<count;++i) {
                auto shape=CreateCollisionShape(SphereShapeDesc{.5f});IB(shape);PhysicsBodyDesc d;d.position={float(i)*3,10,0};d.mass=1.f+float(i%4);
                auto h=world.CreateBody(d,*shape);IB(h);commands.push_back({*h,{.001f,0,0},{},{},PhysicsImpulseMode::VelocityChange});
            }
            std::vector<double> times;times.reserve(100);std::size_t resultBytes{};
            for(int repeat=0;repeat<110;++repeat) {
                const auto start=std::chrono::steady_clock::now();auto result=world.ApplyImpulseBatch(commands);
                const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                IB(result && (*result)->bodies.size()==count);resultBytes=sizeof(PhysicsImpulseBatchResult)+(*result)->bodies.capacity()*sizeof(PhysicsImpulseResult);
                if(repeat>=10)times.push_back(ms);
            }
            for(const auto& c:commands)IB(glm::length(world.ReadImpulseMotion(c.body)->linear-Vec3f(.11f,0,0))<1.e-5f);
            std::sort(times.begin(),times.end());
            std::printf("IMPULSE_TIMING backend=%s size=%zu samples=100 median_ms=%.6f p95_ms=%.6f p99_ms=%.6f result_bytes=%zu\n",PhysicsBackendName(backend).data(),count,times[49],times[94],times[98],resultBytes);
        }
        return 0;
#undef IB
    }
};
}
