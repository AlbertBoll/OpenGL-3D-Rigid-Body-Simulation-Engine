#pragma once
#include "Scene/_Scene.h"
#include "Scene/_Entity.h"
#include "Physics/ShapeConvex.h"
#include "Math/Math.h"
#include <cstdio>

namespace Rbs::Owner04Checks
{
    inline bool Run()
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Component;
#define O4_CHECK(x) do{if(!(x)){std::fprintf(stderr,"OWNER04_CHECK_FAIL line=%d %s\n",__LINE__,#x);return false;}}while(false)
        for(const auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt})
        {
            _Scene scene;O4_CHECK(scene.ConfigurePhysicsBackend(backend));
            auto diamond=scene.CreateEntity("Diamond");O4_CHECK(diamond);
            diamond->AddOrReplaceComponent<Transform3DComponent>(Vec3f{-30,5,0});
            RigidBody3DComponent rb;rb.Type=BodyType::Dynamic;
            diamond->AddOrReplaceComponent<RigidBody3DComponent>(rb);
            ConvexFixture3DComponent fixture;fixture.Shape.points=Math::GenerateDiamond();
            const auto authoredPoints=fixture.Shape.points.size();
            fixture.Property.m_InvMass=.5f;
            diamond->AddOrReplaceComponent<ConvexFixture3DComponent>(fixture);
            O4_CHECK(scene.OnRuntimeStart());scene.SetPaused(true);
            auto& system=*scene.GetPhysicsSystem();auto& world=*system.GetPhysicsWorld();
            O4_CHECK(world.GetBackend()==backend && world.GetBodyCount()==1);
            const auto h=scene.GetBodyBinding(*diamond);O4_CHECK(h);
            const auto before=world.ReadBody(*h);O4_CHECK(before && before->shapeType==ShapeType::Convex && before->inverseMass==.5f);
            const auto descriptor=system.CopyBodyShapeDescriptor(*h);O4_CHECK(descriptor && std::holds_alternative<ConvexHullShapeDesc>(*descriptor));
            const auto hullPoints=std::get<ConvexHullShapeDesc>(*descriptor).points;
            O4_CHECK(hullPoints.size()>8);
            // Runtime ownership must not borrow fixture storage or local temporaries.
            diamond->GetComponent<ConvexFixture3DComponent>().Shape.points.clear();fixture.Shape.points.clear();
            const auto copy=system.CopyBodyShapeDescriptor(*h);O4_CHECK(copy && std::get<ConvexHullShapeDesc>(*copy).points==hullPoints);
            world.SetGravity({0,0,0});
            O4_CHECK(scene.SetLocalTranslation(*diamond,Vec3f{-20,8,2}));
            O4_CHECK(scene.SetLocalRotation(*diamond,Vec3f{.1f,.2f,.3f}));
            for(int i=0;i<3;++i)system.Update(Timestep(1./60.));
            O4_CHECK(!system.GetBackendFailure());
            const auto actual=scene.ReadBodyTransform(*diamond);O4_CHECK(actual && actual->body==*h);
            const auto current=world.ReadBody(*h);O4_CHECK(current && glm::length(current->position-Vec3f{-20,8,2})<.001f);
            O4_CHECK(current->shapeType==ShapeType::Convex && current->inverseInertiaBody==before->inverseInertiaBody);
            if(backend==PhysicsBackend::Jolt)
            {
                // The continuous-transform campaign added validated Jolt scaling.
                // Check its actual collider/pose/mass contract instead of the retired rejection.
                O4_CHECK(scene.SetLocalScale(*diamond,Vec3f(2)) && diamond->Transform().Scale==Vec3f(2));
                const auto scaled=world.ReadBody(*h);O4_CHECK(scaled && scaled->handle==*h && scaled->shapeType==ShapeType::Convex);
                O4_CHECK(scaled->position==current->position && scaled->orientation==current->orientation && scaled->inverseMass==current->inverseMass);
                O4_CHECK(glm::length(scaled->localBounds.mins-2.f*current->localBounds.mins)<.0001f && glm::length(scaled->localBounds.maxs-2.f*current->localBounds.maxs)<.0001f);
                O4_CHECK(!scene.SetLocalScale(*diamond,Vec3f(0)) && diamond->Transform().Scale==Vec3f(2));
            }
            std::printf("OWNER04_CONVEX_PASS backend=%s authored_points=%zu hull_points=%zu mass=2 owned_vertices=1 identity=stable\n",
                PhysicsBackendName(backend).data(),authoredPoints,hullPoints.size());
            scene.OnRuntimeStop();O4_CHECK(!scene.GetBodyBinding(*diamond));
        }
        _Scene hierarchy;
        auto parent=hierarchy.CreateEntity("same name"),child=hierarchy.CreateEntity("same name"),root=hierarchy.CreateEntity("root");
        O4_CHECK(parent && child && root && child->SetParent(*parent));
        O4_CHECK(child->GetParent()==*parent && !root->GetParent() && !parent->GetParent());
        O4_CHECK(!parent->SetParent(*child));
        O4_CHECK(hierarchy.SetLocalTranslation(*parent,Vec3f{2,0,0}) && hierarchy.SetLocalTranslation(*child,Vec3f{0,3,0}));
        const auto transforms=hierarchy.UpdateWorldTransforms();O4_CHECK(transforms);
        const auto identity=hierarchy.RenderData().Identify(*child);O4_CHECK(identity);
        const auto found=std::find_if(transforms->transforms.begin(),transforms->transforms.end(),[&](const auto& t){return t.entity==*identity;});
        O4_CHECK(found!=transforms->transforms.end() && Vec3f(found->matrix[3])==Vec3f(2,3,0));
        std::size_t entities{};for(auto id:hierarchy.GetAllEntitiesWith<IDComponent>()){(void)id;++entities;}
        O4_CHECK(entities==3);
        std::puts("OWNER04_HIERARCHY_PASS authoritative_entities=3 actual_parent_links=1 duplicate_names=distinct cycle=rejected");
        return true;
#undef O4_CHECK
    }
}
