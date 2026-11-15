#pragma once
#include "RbsHierarchy.h"
#include "RbsComponentEditing.h"
#include "RbsTransformSession.h"
#include "Physics/PhysicsSystem.h"
#include <cstdio>
#include <limits>

namespace Rbs::Owner05Checks
{
    inline int Run()
    {
        using namespace ::GEngine;using namespace ::GEngine::Component;using namespace Rbs::Editor;
#define O5_CHECK(x) do{if(!(x)){std::fprintf(stderr,"OWNER05_CHECK_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(false)
        _Scene rows,foreign;std::vector<EntityRenderId> visible;
        for(int i=0;i<70;++i){auto entity=rows.CreateEntity(i%2?"Duplicate":"Entity");O5_CHECK(entity);visible.push_back(*rows.RenderData().Identify(*entity));}
        EntitySelection selection;
        O5_CHECK(selection.Select(rows.RenderData(),visible[2],false));
        O5_CHECK(selection.SelectRange(rows.RenderData(),visible,visible[6],false) && selection.count==5 && selection.ids[4]==visible[6]);
        O5_CHECK(selection.SelectRange(rows.RenderData(),visible,visible[0],true) && selection.count==7 && selection.ids[6]==visible[0]);
        const auto previous=selection;
        O5_CHECK(selection.SelectRange(rows.RenderData(),visible,visible[69],true) && selection.count==70 && !selection.capacityReached);
        std::vector<EntityRenderId> filtered{visible[10],visible[12],visible[14]};
        O5_CHECK(selection.SelectRange(rows.RenderData(),filtered,visible[12],false) && selection.count==1 && selection.anchor==visible[12]);
        O5_CHECK(selection.SelectRange(rows.RenderData(),filtered,visible[14],false) && selection.count==2);
        const auto selected=selection.ids;O5_CHECK(!selection.SelectRange(foreign.RenderData(),filtered,visible[10],false) && selection.ids==selected);
        RenderCameraComponent camera;O5_CHECK(!ValidateCamera(camera));camera.nearPlane=camera.farPlane;O5_CHECK(ValidateCamera(camera));
        camera={};camera.verticalFovRadians=std::numeric_limits<float>::quiet_NaN();O5_CHECK(ValidateCamera(camera));
        camera={};camera.farPlane=std::numeric_limits<float>::max();O5_CHECK(ValidateCamera(camera));
        RenderLightComponent light;O5_CHECK(!ValidateLight(light));light.kind=RenderLightKind::Spot;light.castShadows=true;O5_CHECK(!ValidateLight(light)); // Owner-authorized real Spot shadows supersede the prior unsupported gate.
        light.castShadows=false;light.innerConeRadians=light.outerConeRadians+1;O5_CHECK(ValidateLight(light));
        light={};light.kind=RenderLightKind::Point;light.range=.1f;O5_CHECK(ValidateLight(light));
        light.range=5;light.castShadows=true;O5_CHECK(!ValidateLight(light));
        light.range=std::numeric_limits<float>::max();O5_CHECK(ValidateLight(light));
        std::puts("OWNER05_RANGE_COMPONENT_PASS visible_ranges additive capacity_atomic hidden_anchor duplicate_names stale_registry typed_camera_light");
        for(auto backend:{PhysicsBackend::Native,PhysicsBackend::Jolt})
        {
            _Scene scene;O5_CHECK(scene.ConfigurePhysicsBackend(backend));
            std::array<_Entity,2> entities;
            for(int i=0;i<2;++i)
            {
                auto entity=scene.CreateEntity("Body");O5_CHECK(entity);entities[i]=*entity;
                entity->Transform().SetTranslation(Vec3f{float(i)*8,10,0});
                RigidBody3DComponent body;body.Type=BodyType::Dynamic;entity->AddComponent<RigidBody3DComponent>(body);
                if(i==0)entity->AddComponent<BoxFixture3DComponent>();
                else entity->AddComponent<CapsuleFixture3DComponent>();
            }
            auto cameraEntity=scene.CreateEntity("Camera");O5_CHECK(cameraEntity);cameraEntity->AddComponent<RenderCameraComponent>();
            O5_CHECK(scene.OnRuntimeStart());scene.SetPaused(true);
            auto& world=*scene.GetPhysicsSystem()->GetPhysicsWorld();world.SetGravity({0,0,0});
            const auto a=*scene.GetBodyBinding(entities[0]),b=*scene.GetBodyBinding(entities[1]);
            const auto joint=world.CreateDistanceJoint({a,b,{},{},8,false});O5_CHECK(joint);
            TransformBatch batch;batch.Resize(3);batch.generation=1;
            for(int i=0;i<3;++i)
            {
                const auto entity=i<2?entities[i]:*cameraEntity;auto& edit=batch.edits[i];
                edit.entity=*scene.RenderData().Identify(entity);edit.body=scene.GetBodyBinding(entity).value_or(PhysicsBodyHandle{});
                edit.before=*scene.ReadEditorTransform(edit.entity);edit.after=edit.before;edit.after.translation+=Vec3f{4,2,1};
            }
            O5_CHECK(scene.PreviewEditorTransforms(batch.Span()));
            O5_CHECK(*scene.ReadEditorTransform(batch.edits[0].entity)==batch.edits[0].before);
            O5_CHECK(Vec3f(scene.GetRenderTransform(entities[0])->matrix[3])==batch.edits[0].after.translation);
            O5_CHECK(scene.PreviewEditorTransforms({}));
            O5_CHECK(Vec3f(scene.GetRenderTransform(entities[0])->matrix[3])==batch.edits[0].before.translation);
            auto invalid=batch;invalid.edits[2].after.translation.x=std::numeric_limits<float>::quiet_NaN();
            O5_CHECK(!scene.ApplyEditorTransforms(invalid.Span()));
            O5_CHECK(world.ReadBody(a)->position==batch.edits[0].before.translation && world.ReadBody(b)->position==batch.edits[1].before.translation);
            invalid=batch;invalid.edits[1].body.generation+=1;O5_CHECK(!scene.ApplyEditorTransforms(invalid.Span()));
            invalid=batch;invalid.edits[1].before.translation.x+=1;O5_CHECK(!scene.ApplyEditorTransforms(invalid.Span()));
            invalid=batch;invalid.edits[1].entity=invalid.edits[0].entity;O5_CHECK(!scene.ApplyEditorTransforms(invalid.Span()));
            O5_CHECK(scene.ApplyEditorTransforms(batch.Span()));
            for(std::size_t i=0;i<batch.count;++i)O5_CHECK(*scene.ReadEditorTransform(batch.edits[i].entity)==batch.edits[i].after);
            O5_CHECK(scene.GetBodyBinding(entities[0])==a && scene.GetBodyBinding(entities[1])==b && world.ReadDistanceJoint(*joint));
            auto inverse=batch;for(std::size_t i=0;i<inverse.count;++i)std::swap(inverse.edits[i].before,inverse.edits[i].after);
            O5_CHECK(scene.ApplyEditorTransforms(inverse.Span()));O5_CHECK(scene.ApplyEditorTransforms(batch.Span()));
            auto rotation=batch;
            for(std::size_t i=0;i<rotation.count;++i)
            {
                rotation.edits[i].before=*scene.ReadEditorTransform(rotation.edits[i].entity);
                rotation.edits[i].after=rotation.edits[i].before;
                rotation.edits[i].after.rotation=glm::angleAxis(glm::radians(31.f),Vec3f(0,1,0));
            }
            bool completeAtNotification=true;unsigned notifications=0;
            auto observed=scene.SubscribeToAuthoritativeTransformChanges([&](const AuthoritativeTransformChange&)
            {
                ++notifications;
                for(std::size_t i=0;i<rotation.count;++i)
                    completeAtNotification&=*scene.ReadEditorTransform(rotation.edits[i].entity)==rotation.edits[i].after;
            });
            O5_CHECK(observed && scene.ApplyEditorTransforms(rotation.Span()) && completeAtNotification && notifications==3);
            O5_CHECK(observed->Reset());
            auto undoRotation=rotation;for(std::size_t i=0;i<undoRotation.count;++i)std::swap(undoRotation.edits[i].before,undoRotation.edits[i].after);
            O5_CHECK(scene.ApplyEditorTransforms(undoRotation.Span()) && scene.ApplyEditorTransforms(rotation.Span()) && scene.ApplyEditorTransforms(undoRotation.Span()));
            const auto originalInertia=world.ReadBody(a)->inverseInertiaBody;
            auto scale=batch;for(std::size_t i=0;i<scale.count;++i){scale.edits[i].before=*scene.ReadEditorTransform(scale.edits[i].entity);scale.edits[i].after=scale.edits[i].before;scale.edits[i].after.scale=Vec3f(2);}
            auto badScale=scale;badScale.edits[1].after.scale={2,3,4};
            O5_CHECK(!scene.ApplyEditorTransforms(badScale.Span()));
            for(const auto& e:badScale.Span())O5_CHECK(*scene.ReadEditorTransform(e.entity)==e.before);
            O5_CHECK(world.ReadBody(a)->inverseInertiaBody==originalInertia);
            // Connected scaling is now explicitly rejected on both backends;
            // independent supported shapes are exercised by continuous-transform-checks.
            O5_CHECK(!scene.ApplyEditorTransforms(scale.Span()));
            for(std::size_t i=0;i<scale.count;++i)O5_CHECK(*scene.ReadEditorTransform(scale.edits[i].entity)==scale.edits[i].before);
            O5_CHECK(world.ReadBody(a)->inverseInertiaBody==originalInertia);
            scene.GetPhysicsSystem()->Update(Timestep(1./60.));O5_CHECK(!scene.GetPhysicsSystem()->GetBackendFailure());
            O5_CHECK(glm::length(world.ReadBody(a)->position-batch.edits[0].after.translation)<.001f && world.ReadDistanceJoint(*joint));
            scene.OnRuntimeStop();O5_CHECK(!scene.ApplyEditorTransforms(batch.Span()));
            std::printf("OWNER05_BATCH_PASS backend=%s preview_cancel atomic_rejection apply rotation undo redo complete_notifications handles joints step stale_world scale=%s\n",PhysicsBackendName(backend).data(),"connected-scale-rejected");
        }
        _Scene tree;auto parent=tree.CreateEntity("Parent"),child=tree.CreateEntity("Child");O5_CHECK(parent&&child&&child->SetParent(*parent));
        parent->Transform().SetTranslation({3,0,0});child->Transform().SetTranslation({0,2,0});
        EditorTransformEdit root;root.entity=*tree.RenderData().Identify(*parent);root.before=*tree.ReadEditorTransform(root.entity);root.after=root.before;root.after.translation.x+=4;
        O5_CHECK(tree.ApplyEditorTransforms({&root,1}));const auto transforms=tree.UpdateWorldTransforms();O5_CHECK(transforms);
        const auto childId=*tree.RenderData().Identify(*child);
        const auto found=std::find_if(transforms->transforms.begin(),transforms->transforms.end(),[&](const auto& t){return t.entity==childId;});
        O5_CHECK(found!=transforms->transforms.end() && Vec3f(found->matrix[3])==Vec3f(7,2,0) && child->Transform().Translation==Vec3f(0,2,0));
        std::puts("OWNER05_CHECK_PASS parent_child_inheritance single_authoritative_commit");return 0;
#undef O5_CHECK
    }
}
