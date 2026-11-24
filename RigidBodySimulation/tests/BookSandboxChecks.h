#pragma once
#include "RigidBodySimulation.h"
#include "Core/Log.h"
#include "Physics/PhysicsSystem.h"
#include "Renderer/SceneRenderResources.h"
#include <stb_image/stb_image_write.h>
#include <filesystem>

namespace Rbs
{
    // Extends the existing bounded RBS smoke session. The ordinary Sandbox has no sampler.
    struct BookSandboxChecks
    {
        inline static bool captured{}, resetRequested{};
        inline static GEngine::PhysicsBodyHandle originalPlatform{};
        inline static GEngine::PhysicsJointHandle originalDrive{};

        static bool Requested(const RigidBodySimulationApp& app)
        {
            static const bool selected=std::getenv("GENGINE_PHASE32_BOOK_CAPTURE")!=nullptr;
            return selected && app.IntegratedBook() && app.m_Launch.m_BackendSmokeTicks==120;
        }
        static bool Fail(RigidBodySimulationApp& app,const char* message)
        {
            app.FailRuntime({GEngine::ApplicationRuntimeErrorCode::SubsystemFailure,"Physics",
                "Book Conformance","Integration check","Phase32",message});
            return false;
        }
        static void Capture(RigidBodySimulationApp& app)
        {
            using namespace ::GEngine;
            using namespace ::GEngine::Component;
            if(captured || !app.m_WoodSettled || !app.m_RenderTarget)return;
            const auto material=[&](entt::entity id) {
                return _Entity{id,app.m_ActiveScene.get()}.GetComponent<MeshRendererComponent>().material;
            };
            if(material(app.m_MoverBindings.entities[1])!=app.m_AsyncBoxMaterial ||
               material(app.m_Book.angular.back().bindings.entities[1])!=app.m_AsyncBoxMaterial) {
                Fail(app,"Motor and rider lost their authored box surface");return;
            }
            auto skin=app.m_SceneResources->DescribeMaterial(material(app.m_RagdollBindings.entities[0]));
            if(!skin || !std::ranges::all_of(skin->textures,[](const auto& t){return t.has_value();})) {
                Fail(app,"Missing ragdoll PBR maps");return;
            }
            for(auto bone:app.m_RagdollBindings.entities) {
                const auto value=app.m_SceneResources->DescribeMaterial(material(bone));
                if(!value || *value!=*skin){Fail(app,"Ragdoll bone lost its authored rust surface");return;}
            }
            const auto& target=*app.m_RenderTarget;
            const auto size=target.Buffer().Description();
            if(!size.Width || !size.Height || size.Width>4096 || size.Height>4096) {
                Fail(app,"Unusable viewport dimensions");return;
            }
            const auto stride=size_t(size.Width)*4;
            std::vector<std::byte> pixels(stride*size.Height);
            if(!target.ReadColor(pixels)){Fail(app,"Viewport readback failed");return;}
            for(unsigned y=0;y<size.Height/2;++y)
                std::swap_ranges(pixels.begin()+y*stride,pixels.begin()+(y+1)*stride,
                                 pixels.begin()+(size.Height-y-1)*stride);
            auto path=std::filesystem::current_path()/"integrated-viewport.png";
            if(std::filesystem::exists(path) || !stbi_write_png(path.string().c_str(),int(size.Width),
                int(size.Height),4,pixels.data(),int(stride))){Fail(app,"Fresh viewport capture failed");return;}
            originalPlatform=app.m_MoverBindings.bodies[0];
            originalDrive=app.m_Book.angular.back().bindings.joint;
            captured=true;
            Log::GetCoreLogger()->info("BOOK_CAPTURE width={} height={} wood=settled rust_maps=5 world={}",
                size.Width,size.Height,originalPlatform.registry);
        }
        static bool Sample(RigidBodySimulationApp& app,double milliseconds)
        {
            using namespace ::GEngine;
            auto& physics=*app.m_ActiveScene->GetPhysicsSystem();
            auto& world=*physics.GetPhysicsWorld();
            const auto tick=physics.GetCompletedSteps();
            if(!tick || resetRequested)return true;
            auto mover=world.ReadMover(originalPlatform);
            auto drive=world.ReadJoint(originalDrive);
            if(!mover || !drive || !drive->descriptor.angular.motor || mover->rider!=app.m_MoverBindings.bodies[1] ||
               mover->tick!=tick || !std::isfinite(drive->angular.motorSpeed) || !std::isfinite(mover->targetError))
                return Fail(app,"Motor/mover identities or numerical readback differ");
            Log::GetCoreLogger()->info("BOOK_STEP {} {} {} {} {} {} {} {} {}",tick,milliseconds,
                drive->angular.motorSpeed,drive->descriptor.angular.motor->enabled,mover->enabled,mover->reversed,
                mover->phaseTick,mover->targetError,mover->pairInContact);
            // Only the named handle may respond. Both controls operate in this same world.
            if((tick==30 || tick==40) &&
               !world.SetHingeMotor(originalDrive,{tick==40,2.f,100.f}))return Fail(app,"Drive command rejected");
            if(tick==60 && !world.QueueMoverCommand(originalPlatform,MoverCommand::Stop))return Fail(app,"Mover Stop rejected");
            if(tick==75 && (!world.QueueMoverCommand(originalPlatform,MoverCommand::Reverse) ||
               !world.QueueMoverCommand(originalPlatform,MoverCommand::Start)))return Fail(app,"Mover restart rejected");
            return true;
        }
        static void Reset(RigidBodySimulationApp& app)
        {
            app.SetSimulationState(SimulationState::Paused);
            if(!app.RequestScene(app.m_ActiveSelection)){Fail(app,"Explicit Reset rejected");return;}
            resetRequested=true;
        }
        static void Finish(RigidBodySimulationApp& app)
        {
            auto& physics=*app.m_ActiveScene->GetPhysicsSystem();
            auto& world=*physics.GetPhysicsWorld();
            if(physics.GetCompletedSteps()!=0 || world.GetWorldDomain()==originalPlatform.registry ||
               world.ReadBody(originalPlatform) || world.ReadJoint(originalDrive) ||
               !world.ReadMover(app.m_MoverBindings.bodies[0]) || world.GetJointCount()!=15 ||
               world.GetBodyCount()!=48){Fail(app,"Reset retained old identities or lost an assembly");return;}
            GEngine::Log::GetCoreLogger()->info("BOOK_RESET_PASS old_world={} new_world={} ticks=0 bodies=48 joints=15",
                originalPlatform.registry,world.GetWorldDomain());
            app.ShutDown();
        }
    };
}
