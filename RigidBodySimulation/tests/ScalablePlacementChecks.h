#pragma once
#include "Physics/PhysicsWorld.h"
#include <cstdio>
namespace Rbs {
struct ScalablePlacementChecks {
    static int Run() {
        using namespace ::GEngine;using namespace ::GEngine::Component;
#define SP(x) do{if(!(x)){std::fprintf(stderr,"SCALABLE_CONTACT_FAIL backend=%s line=%d %s\n",PhysicsBackendName(backend).data(),__LINE__,#x);return 1;}}while(false)
        for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt}) {
            PhysicsWorld world;SP(world.ConfigureBackend(backend));
            const auto create=[&](Vec3f position,BodyType type=BodyType::Dynamic) {
                auto shape=CreateCollisionShape(SphereShapeDesc{.5f},Vec3f(1));
                PhysicsBodyDesc body;body.position=position;body.type=type;return world.CreateBody(body,*shape);
            };
            std::vector<PhysicsPlacementPose> poses;std::vector<PhysicsBodyState> original;
            for(int i=0;i<256;++i) {auto body=create({float(i),100,0});SP(body);original.push_back(*world.ReadBody(*body));poses.push_back({*body,{float(i),105,0}});}
            const auto obstacle=create({256,105,0},BodyType::Static);SP(obstacle);
            PhysicsPlacementSettings options;options.preserveExistingContacts=true;
            auto moved=world.QueryEditorPlacement(poses,options);
            if(!moved)std::fprintf(stderr,"CONTACT_REASON %.*s\n",int(moved.error().operation.size()),moved.error().operation.data());
            SP(moved); // Adjacent exact contacts, including the unselected boundary.
            const auto q=glm::angleAxis(.07f,Vec3f(0,1,0));
            for(std::size_t i=0;i<poses.size();++i) {poses[i].position=Vec3f(0,105,0)+q*Vec3f(float(i),0,0);poses[i].rotation=q;}
            SP(world.QueryEditorPlacement(poses,options));
            for(std::size_t i=0;i<poses.size();++i) {poses[i].position={float(i)*1.1f,110,0};poses[i].scale=Vec3f(1.1f);}
            SP(world.QueryEditorPlacement(poses,options)); // Scale offsets and actual sphere radii together.
            for(std::size_t i=0;i<poses.size();++i) {poses[i]={original[i].handle,{float(i),105,0}};}
            poses.back().position.x=256;SP(!world.QueryEditorPlacement(poses,options)); // Last-member external penetration.
            poses.back().position.x=255;poses.back().scale=Vec3f(2);SP(!world.QueryEditorPlacement(poses,options));poses.back().scale.reset();
            auto joint=world.CreateDistanceJoint({poses[0].body,poses[1].body,{},{},1,false});SP(joint);
            SP(world.QueryEditorPlacement(poses,options));SP(!world.QueryEditorPlacement({poses.data()+1,poses.size()-1},options));
            poses.front().scale=Vec3f(1.01f);SP(!world.QueryEditorPlacement(poses,options));poses.front().scale.reset();
            SP(world.ReadDistanceJoint(*joint));SP(world.DestroyJoint(*joint));
            for(const auto& before:original) {const auto after=world.ReadBody(before.handle);SP(after && after->position==before.position && after->orientation==before.orientation && after->inverseInertiaBody==before.inverseInertiaBody);}
            SP(world.DestroyBody(poses.back().body));SP(!world.QueryEditorPlacement(poses,options));
            std::printf("SCALABLE_CONTACT_PASS backend=%s size=256 selected_touching external_touching T_R_S external_penetration connected_assembly stale_last no_mutation\n",PhysicsBackendName(backend).data());
        }
        return 0;
#undef SP
    }
};
}
