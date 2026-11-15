#pragma once
#include "RbsHierarchy.h"
#include <cstdio>

namespace Rbs::HierarchyChecks
{
    inline int Run()
    {
        using namespace ::GEngine;using namespace ::GEngine::Component;using namespace Rbs::Editor;
#define H_CHECK(x) do{if(!(x)){std::fprintf(stderr,"HIERARCHY_CHECK_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(false)
        _Scene scene,foreign;
        std::array<_Entity,9> chain;
        for(int i=8;i>=0;--i)
        {
            auto e=scene.CreateEntity(i?"Chain link "+std::to_string(i):"Chain root");H_CHECK(e);
            chain[i]=*e;chain[i].AddComponent<RigidBody3DComponent>();
        }
        auto camera=scene.CreateEntity("Camera");H_CHECK(camera);camera->AddComponent<RenderCameraComponent>();
        auto light=scene.CreateEntity("Light");H_CHECK(light);light->AddComponent<RenderLightComponent>();
        auto sky=scene.CreateEntity("Sky");H_CHECK(sky);sky->AddComponent<SkyBoxComponent>();
        auto parent=scene.CreateEntity("Assembly");H_CHECK(parent);
        auto child10=scene.CreateEntity("Part 10"),child2=scene.CreateEntity("Part 2");H_CHECK(child10 && child2);
        H_CHECK(child10->SetParent(*parent) && child2->SetParent(*parent));
        auto duplicate=scene.CreateEntity("Part 2");H_CHECK(duplicate && duplicate->SetParent(*parent));
        const auto childrenBefore=std::as_const(*parent).Children();
        std::array<entt::entity,9> chainIds;for(int i=0;i<9;++i)chainIds[i]=chain[i];
        HierarchyModel model;model.Build(scene,"");
        H_CHECK(model.nodes.size()==16 && model.groups[int(EntityGroup::PhysicsBodies)].total==9);
        H_CHECK(model.groups[int(EntityGroup::Cameras)].total==1 && model.groups[int(EntityGroup::Lighting)].total==1 && model.groups[int(EntityGroup::Environment)].total==1);
        int cursor=model.groups[int(EntityGroup::PhysicsBodies)].first;
        for(int i=0;i<9;++i)
        {
            H_CHECK(cursor!=HierarchyModel::None && model.nodes[cursor].entity==chainIds[i]);
            H_CHECK(model.nodes[cursor].parent==HierarchyModel::None && !chain[i].GetParent());
            cursor=model.nodes[cursor].next;
        }
        H_CHECK(cursor==HierarchyModel::None);
        const auto parentIndex=model.Find(*parent);H_CHECK(parentIndex>=0);
        cursor=model.nodes[parentIndex].firstChild;
        H_CHECK(model.nodes[cursor].name=="Part 2");cursor=model.nodes[cursor].next;
        H_CHECK(model.nodes[cursor].name=="Part 2");cursor=model.nodes[cursor].next;
        H_CHECK(model.nodes[cursor].name=="Part 10" && model.nodes[cursor].next==HierarchyModel::None);
        const auto active=scene.RenderData().Identify(*child10);H_CHECK(active);
        model.Build(scene,"pArT 10",*active);
        unsigned visible{};for(const auto& n:model.nodes)visible+=n.visible;
        H_CHECK(visible==2 && model.nodes[model.Find(*parent)].visible && model.nodes[model.Find(*parent)].activePath);
        H_CHECK(model.groups[int(EntityGroup::Other)].visible==2 && model.groups[int(EntityGroup::Other)].activePath);
        model.Build(scene,"");for(const auto& n:model.nodes)H_CHECK(n.visible);
        H_CHECK(std::as_const(*parent).Children()==childrenBefore);
        H_CHECK(NaturalCompare("LINK 8","link 10")<0 && NaturalCompare("x0008","x8")>0);
        H_CHECK(NaturalCompare("x999999999999999999999","x1000000000000000000000")<0);
        const auto modelCapacity=model.nodes.capacity();
        for(int i=0;i<10;++i)model.Build(scene,"");H_CHECK(model.nodes.capacity()==modelCapacity);

        EntitySelection selection;
        const auto bodyId=scene.RenderData().Identify(chain[1]);const auto cameraId=scene.RenderData().Identify(*camera);
        H_CHECK(bodyId && cameraId && selection.Select(scene.RenderData(),*bodyId,false));
        H_CHECK(selection.Select(scene.RenderData(),*cameraId,true) && selection.count==2 && selection.Contains(*bodyId));
        H_CHECK(selection.Select(scene.RenderData(),*cameraId,true) && selection.count==1);
        auto outsider=foreign.CreateEntity("Outside");H_CHECK(outsider);
        auto outsideId=foreign.RenderData().Identify(*outsider);H_CHECK(outsideId);
        H_CHECK(!selection.Select(scene.RenderData(),*outsideId,false) && selection.Contains(*bodyId));
        H_CHECK(scene.DestroyEntity(chain[1]));
        auto replacement=scene.CreateEntity("Replacement");H_CHECK(replacement);
        H_CHECK(!scene.RenderData().Resolve(*bodyId));
        H_CHECK(!selection.Select(scene.RenderData(),*bodyId,false));
        selection.Prune(scene.RenderData());H_CHECK(selection.count==0);
        model.Build(scene,"");H_CHECK(model.Find(chainIds[1])==HierarchyModel::None);
        std::puts("HIERARCHY_CHECK_PASS entities=16 chain_order=root,1,2,3,4,5,6,7,8 actual_parent_paths=1 duplicate_names=distinct search_clear=1 scene_only_ctrl=1 stale_foreign_rejected=1 ecs_unmodified=1 retained_capacity=1");
        return 0;
#undef H_CHECK
    }
}
