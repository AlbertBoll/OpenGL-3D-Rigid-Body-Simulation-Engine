#pragma once
#include "RigidBodySimulation.h"
#include <cstdio>
#include <limits>
namespace Rbs {
struct JoltParameterChecks {
static int Run(){
    using namespace ::GEngine;using namespace ::GEngine::Component;
#define JP(x) do{if(!(x)){std::fprintf(stderr,"JOLT_PARAMETER_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(false)
    std::setvbuf(stdout,nullptr,_IONBF,0);
    for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt}){
        LaunchConfig launch;launch.m_Backend=backend;RigidBodySimulationApp app(launch);
        app.m_ActiveScene=CreateRefPtr<_Scene>();auto& scene=*app.m_ActiveScene;JP(scene.ConfigurePhysicsBackend(backend));app.m_Backend=backend;app.m_SceneGeneration=1;
        auto floor=scene.CreateEntity("floor");JP(floor);floor->Transform().SetTranslation({0,-1,0});floor->Transform().SetScale({20,1,20});
        RigidBody3DComponent fixed;fixed.Type=BodyType::Static;floor->AddComponent<RigidBody3DComponent>(fixed);floor->AddComponent<BoxFixture3DComponent>();
        auto entity=scene.CreateEntity("sphere");JP(entity);entity->Transform().SetTranslation({0,3,0});
        RigidBody3DComponent dynamic;dynamic.Type=BodyType::Dynamic;entity->AddComponent<RigidBody3DComponent>(dynamic);entity->AddComponent<SphereFixture3DComponent>();
        JP(scene.OnRuntimeStart());auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();const auto handle=*scene.GetBodyBinding(*entity);
        const auto original=*world.ReadBodyProperties(handle);app.ProcessJoltSettingsCommand();
        if(backend==PhysicsBackend::Native){JP(!world.ReadJoltSettings());JP(!world.UpdateJoltSettings({}));auto p=original;p.velocityStepsOverride=2;JP(!world.UpdateBodyProperties(handle,p));JP(*world.ReadBodyProperties(handle)==original);continue;}
        const auto baseline=*world.ReadJoltSettings();JP(baseline==PhysicsJoltSettings{});JP(*world.ReadJoltNativeDefaults()==baseline);JP(!world.ReadJoltDiagnostics()->enabled);
        auto test=baseline;test.velocitySteps=16;test.positionSteps=4;test.collisionSteps=2;test.minRestitutionVelocity=.25f;test.baumgarte=.3f;test.penetrationSlop=.01f;test.speculativeDistance=.03f;test.allowSleeping=false;test.timeBeforeSleep=1;test.pointVelocitySleepThreshold=.02f;test.warmStarting=false;test.bodyPairCache=false;test.manifoldReduction=false;
        JP(world.UpdateJoltSettings(test));JP(*world.ReadJoltSettings()==test);
        scene.GetPhysicsSystem()->Update(Timestep(1./60.));JP(!world.ReadJoltSettings()->allowSleeping);
        const auto afterTick=*world.ReadBody(handle);JP(!afterTick.sleeping);
        PhysicsBodyErrorCode threadError{};std::thread worker([&]{auto r=world.UpdateJoltSettings(baseline);if(!r)threadError=r.error().code;});worker.join();JP(threadError==PhysicsBodyErrorCode::WrongThread);JP(*world.ReadJoltSettings()==test);
        auto sleep=*world.ReadWorldSleepSettings();sleep.timeBeforeSleep=2;JP(world.UpdateWorldSleepSettings(sleep));auto adjusted=test;adjusted.timeBeforeSleep=2;JP(*world.ReadJoltSettings()==adjusted);JP(world.UpdateJoltSettings(test));
        for(int which=0;which<12;++which){auto bad=test;switch(which){case 0:bad.velocitySteps=1;break;case 1:bad.velocitySteps=256;break;case 2:bad.positionSteps=-1;break;case 3:bad.collisionSteps=0;break;case 4:bad.collisionSteps=17;break;case 5:bad.minRestitutionVelocity=0;break;case 6:bad.minRestitutionVelocity=NAN;break;case 7:bad.baumgarte=1.1f;break;case 8:bad.penetrationSlop=-1;break;case 9:bad.speculativeDistance=INFINITY;break;case 10:bad.timeBeforeSleep=0;break;case 11:bad.pointVelocitySleepThreshold=FLT_MAX;bad.timeBeforeSleep=FLT_MAX;break;}
            JP(!world.UpdateJoltSettings(bad));JP(*world.ReadJoltSettings()==test);}
        auto limits=baseline;limits.velocitySteps=255;limits.positionSteps=255;limits.collisionSteps=16;limits.baumgarte=1;limits.penetrationSlop=0;limits.speculativeDistance=0;JP(world.UpdateJoltSettings(limits));JP(*world.ReadJoltSettings()==limits);
        limits.velocitySteps=2;limits.positionSteps=0;limits.collisionSteps=1;limits.baumgarte=0;JP(world.UpdateJoltSettings(limits));JP(world.UpdateJoltSettings(baseline));
        JP(world.SetBodyVelocity(handle,{.25f,0,0},{0,.1f,0}));const auto beforeProperties=*world.ReadBody(handle);
        auto body=original;body.velocityStepsOverride=16;body.positionStepsOverride=8;JP(world.UpdateBodyProperties(handle,body));JP(*world.ReadBodyProperties(handle)==body);JP(world.ReadBody(handle)->linearVelocity==beforeProperties.linearVelocity && world.ReadBody(handle)->angularVelocity==beforeProperties.angularVelocity);
        for(int value:{-1,1,256}){auto bad=body;bad.velocityStepsOverride=value;JP(!world.UpdateBodyProperties(handle,bad));JP(*world.ReadBodyProperties(handle)==body);}
        auto stat=*world.ReadBodyProperties(*scene.GetBodyBinding(*floor));stat.velocityStepsOverride=2;JP(!world.UpdateBodyProperties(*scene.GetBodyBinding(*floor),stat));
        JP(world.UpdateBodyProperties(handle,original));
        // Deferred UI semantics: running rejected, paused accepted, world-entry baseline retained.
        app.m_JoltSettingsEditor.draft=test;app.m_JoltSettingsEditor.queued=true;app.SetSimulationState(SimulationState::Running);app.ProcessJoltSettingsCommand();JP(*world.ReadJoltSettings()==baseline);
        app.SetSimulationState(SimulationState::Paused);app.m_JoltSettingsEditor.queued=true;app.ProcessJoltSettingsCommand();JP(*world.ReadJoltSettings()==test);JP(app.m_JoltSettingsEditor.baseline==baseline);
        app.m_JoltSettingsEditor.draft=baseline;app.m_JoltSettingsEditor.queued=true;app.ProcessJoltSettingsCommand();JP(*world.ReadJoltSettings()==baseline);
        JP(world.EnableJoltDiagnostics(true));std::size_t contacts=0;
        for(int i=0;i<180;++i){scene.GetPhysicsSystem()->Update(Timestep(1./60.));JP(!scene.GetPhysicsSystem()->GetBackendFailure());auto d=*world.ReadJoltDiagnostics();JP(d.enabled && d.step==unsigned(i+1) && d.overflow==0);contacts+=d.callbacks;for(const auto& c:d.contacts){JP(c.friction==original.material.friction*world.ReadBodyProperties(c.bodyA==handle?c.bodyB:c.bodyA)->material.friction);JP(std::isfinite(c.normalVelocityBefore)&&std::isfinite(c.normalVelocityAfter));}}
        JP(contacts>0);JP(world.EnableJoltDiagnostics(false));JP(!world.ReadJoltDiagnostics()->enabled);JP(world.ReadBody(handle));
        // Existing backend authoring transaction: reject incompatible Native candidate,
        // preserve the source, and retain supported Jolt settings in a fresh world.
        JP(world.UpdateJoltSettings(test));const auto sourceState=*world.ReadBody(handle);
        auto authored=RigidBodySimulationApp::BackendAuthoring::Capture(scene);JP(authored && authored->m_JoltWorld==test);
        for(auto targetBackend:{PhysicsBackend::Native,PhysicsBackend::Jolt}) {
            _Scene target;JP(target.ConfigurePhysicsBackend(targetBackend));
            auto f=target.CreateEntity("floor"),e=target.CreateEntity("sphere");JP(f&&e);
            f->AddComponent<RigidBody3DComponent>(fixed);f->AddComponent<BoxFixture3DComponent>();
            e->AddComponent<RigidBody3DComponent>(dynamic);e->AddComponent<SphereFixture3DComponent>();
            JP(authored->ApplyBeforeStart(target));JP(target.OnRuntimeStart());
            auto applied=authored->ApplyAfterStart(target);JP(bool(applied)==(targetBackend==PhysicsBackend::Jolt));
            JP(world.ReadBody(handle)->position==sourceState.position && *world.ReadJoltSettings()==test);
            auto& replacement=*target.GetPhysicsSystem()->GetPhysicsWorld();JP(!replacement.ReadBody(handle));
            if(applied)JP(*replacement.ReadJoltSettings()==test);
        }
        JP(world.UpdateJoltSettings(baseline));
        ++app.m_SceneGeneration;app.ProcessJoltSettingsCommand();JP(!app.m_JoltSettingsEditor.dirty && app.m_JoltSettingsEditor.generation==2);
    }
    std::puts("JOLT_PARAMETER_PASS defaults boundaries authoritative readback deferred Apply baseline Reset Native separation contact observer lifecycle");return 0;
#undef JP
}
};
}
