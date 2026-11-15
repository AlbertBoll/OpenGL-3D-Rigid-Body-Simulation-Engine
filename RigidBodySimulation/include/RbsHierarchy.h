#pragma once
#include "Scene/_Entity.h"
#include "Scene/RenderEcs.h"
#include "Component/RenderComponents.h"
#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace Rbs::Editor
{
    enum class EntityGroup { Cameras, Lighting, Environment, PhysicsBodies, Other, Count };
    inline constexpr const char* GroupNames[]{"Cameras", "Lighting", "Environment", "Physics Bodies", "Entities"};
    inline unsigned char Fold(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a'-'A') : c; }
    inline bool NameContains(std::string_view name, std::string_view text)
    {
        return std::search(name.begin(), name.end(), text.begin(), text.end(),
            [](auto a, auto b){ return Fold(a)==Fold(b); }) != name.end() || text.empty();
    }
    // Compare digit runs without integer conversion (unbounded names cannot overflow).
    inline int NaturalCompare(std::string_view a, std::string_view b)
    {
        std::size_t i=0,j=0;
        while(i<a.size() && j<b.size())
        {
            const auto digit=[](char c){return c>='0' && c<='9';};
            if(digit(a[i]) && digit(b[j]))
            {
                auto ae=i,be=j;while(ae<a.size() && digit(a[ae]))++ae;while(be<b.size() && digit(b[be]))++be;
                auto av=i,bv=j;while(av<ae && a[av]=='0')++av;while(bv<be && b[bv]=='0')++bv;
                if(ae-av!=be-bv)return ae-av<be-bv?-1:1;
                const auto comparison=a.substr(av,ae-av).compare(b.substr(bv,be-bv));
                if(comparison)return comparison<0?-1:1;
                if(ae-i!=be-j)return ae-i<be-j?-1:1;
                i=ae;j=be;continue;
            }
            if(Fold(a[i])!=Fold(b[j]))return Fold(a[i])<Fold(b[j])?-1:1;
            ++i;++j;
        }
        return i<a.size()?1:j<b.size()?-1:0;
    }
    inline std::string_view EntityName(const GEngine::_Entity& e)
    {
        using namespace GEngine::Component;
        return e.HasAllComponents<TagComponent>()?std::string_view(e.GetComponent<TagComponent>().Name):"Unnamed entity";
    }
    inline EntityGroup Classify(const GEngine::_Entity& e)
    {
        using namespace GEngine::Component;
        if(e.HasAnyComponents<RenderCameraComponent,CameraComponent>())return EntityGroup::Cameras;
        if(e.HasAnyComponents<RenderLightComponent,LightComponent,LightComponents,DirectionalLightComponent,PointLightComponent,SpotLightComponent>())return EntityGroup::Lighting;
        if(e.HasAllComponents<RigidBody3DComponent>())return EntityGroup::PhysicsBodies;
        if(e.HasAnyComponents<SkyBoxComponent,FogComponent>() || EntityName(e)=="grid" || EntityName(e)=="axis" || EntityName(e)=="Environment_SkyBox")return EntityGroup::Environment;
        return EntityGroup::Other;
    }

    // A frame-local, read-only projection. Retained capacity, no cached Scene pointers
    // or borrowed names across builds; children are linked only by actual ECS parents.
    struct HierarchyModel
    {
        static constexpr int None=-1;
        struct Node
        {
            entt::entity entity{entt::null};
            GEngine::EntityRenderId identity{};
            std::string_view name;
            int parent=None,firstChild=None,next=None;
            EntityGroup group=EntityGroup::Other;
            bool visible{},activePath{};
        };
        struct Group { int first=None; unsigned total{},visible{}; bool activePath{}; };
        std::vector<Node> nodes;
        std::vector<int> order, tails;
        std::array<Group,int(EntityGroup::Count)> groups{};

        int Find(entt::entity entity) const
        {
            auto found=std::lower_bound(nodes.begin(),nodes.end(),entity,
                [](const auto& n,auto id){return std::uint32_t(n.entity)<std::uint32_t(id);});
            return found!=nodes.end() && found->entity==entity?int(found-nodes.begin()):None;
        }
        void Build(GEngine::_Scene& scene, std::string_view filter, GEngine::EntityRenderId active={})
        {
            using namespace ::GEngine;using namespace ::GEngine::Component;
            nodes.clear();order.clear();groups={};
            for(auto id:scene.GetAllEntitiesWith<IDComponent>())
            {
                const _Entity e{id,&scene};
                if(auto identity=scene.RenderData().Identify(e))
                    nodes.push_back({id,*identity,EntityName(e)});
            }
            std::sort(nodes.begin(),nodes.end(),[](const auto& a,const auto& b){return std::uint32_t(a.entity)<std::uint32_t(b.entity);});
            tails.assign(nodes.size(),None);
            for(int i=0;i<int(nodes.size());++i)
            {
                const _Entity e{nodes[i].entity,&scene};
                const auto parent=e.GetParent();
                nodes[i].parent=parent?Find(parent):None;
                order.push_back(i);
            }
            std::sort(order.begin(),order.end(),[&](int a,int b)
            {
                // The authored chain anchor is a peer, not an invented parent.
                const bool ar=NaturalCompare(nodes[a].name,"Chain root")==0,br=NaturalCompare(nodes[b].name,"Chain root")==0;
                if(ar!=br)return ar;
                const auto c=NaturalCompare(nodes[a].name,nodes[b].name);
                return c?c<0:std::uint32_t(nodes[a].entity)<std::uint32_t(nodes[b].entity);
            });
            std::array<int,int(EntityGroup::Count)> groupTails;groupTails.fill(None);
            for(auto i:order)
            {
                auto& n=nodes[i];int root=i;
                for(std::size_t d=0;nodes[root].parent!=None && d<nodes.size();++d)root=nodes[root].parent;
                n.group=Classify(_Entity{nodes[root].entity,&scene});
                auto& group=groups[int(n.group)];++group.total;
                int& first=n.parent==None?group.first:nodes[n.parent].firstChild;
                int& tail=n.parent==None?groupTails[int(n.group)]:tails[n.parent];
                if(tail==None)first=i;else nodes[tail].next=i;tail=i;
                if(NameContains(n.name,filter))
                    for(int p=i,d=0;p!=None && d++<int(nodes.size());p=nodes[p].parent)nodes[p].visible=true;
                if(n.identity==active)
                {
                    group.activePath=true;
                    for(int p=i,d=0;p!=None && d++<int(nodes.size());p=nodes[p].parent)nodes[p].activePath=true;
                }
            }
            for(const auto& n:nodes)if(n.visible)++groups[int(n.group)].visible;
        }
    };

    enum class SelectionCommand { Focused, Scene, Filtered, SceneVisible, Type, Children, Invert, Clear, SphereCohort };
    enum class SelectionType { Sphere, Box, Capsule, Convex, Rigidbody, Camera, Light, Mesh };
    inline constexpr const char* SelectionTypeNames[]{"Sphere colliders","Box colliders","Capsule colliders","Convex colliders","Rigidbodies","Cameras","Lights","Meshes"};

    // Shared Hierarchy/Viewport selection identities, including scene-only objects.
    // Physics outlines remain a derived body-handle view; editing targets the last ID.
    struct EntitySelection
    {
        // Presentation bound follows the existing renderer ID/highlight budget;
        // storage grows only for selected entities. Physics admits its own budget.
        static constexpr std::size_t Capacity=4096;
        std::vector<GEngine::EntityRenderId> ids=std::vector<GEngine::EntityRenderId>(std::size_t(0)),sorted=std::vector<GEngine::EntityRenderId>(std::size_t(0));
        std::size_t count{};
        bool capacityReached{};
        GEngine::EntityRenderId anchor{};
        static bool Less(GEngine::EntityRenderId a,GEngine::EntityRenderId b) {
            if(a.registry!=b.registry)return a.registry<b.registry;
            if(a.index!=b.index)return a.index<b.index;
            return a.generation<b.generation;
        }
        void Clear(){ids.clear();sorted.clear();count=0;capacityReached=false;}
        bool Contains(GEngine::EntityRenderId id) const {return std::binary_search(sorted.begin(),sorted.end(),id,Less);}
        void Prune(const GEngine::RenderEcs& scene) {
            std::erase_if(ids,[&](auto id){return !scene.Resolve(id);});
            std::erase_if(sorted,[&](auto id){return !scene.Resolve(id);});count=ids.size();
        }
        enum class SetOperation {Replace, Add, Toggle};
        bool SelectSet(const GEngine::RenderEcs& scene,std::span<const GEngine::EntityRenderId> input,SetOperation op) {
            try {
                std::vector<GEngine::EntityRenderId> unique(input.begin(),input.end());
                std::sort(unique.begin(),unique.end(),Less);unique.erase(std::unique(unique.begin(),unique.end()),unique.end());
                for(auto id:unique)if(!scene.Resolve(id))return false;
                auto lookup=unique;std::vector<unsigned char> visited(unique.size(),0);
                EntitySelection next;next.anchor=anchor;
                if(op!=SetOperation::Replace)for(auto id:ids)if(scene.Resolve(id) &&
                    !(op==SetOperation::Toggle && std::binary_search(lookup.begin(),lookup.end(),id,Less)))next.ids.push_back(id);
                // Preserve request order (and the active endpoint), deduplicating via a sorted visited set.
                for(auto id:input) {
                    auto found=std::lower_bound(unique.begin(),unique.end(),id,Less);
                    if(found==unique.end() || *found!=id)continue;
                    const auto slot=std::size_t(found-unique.begin());if(visited[slot])continue;visited[slot]=1;
                    if(op==SetOperation::Replace || !Contains(id))next.ids.push_back(id);
                }
                if(next.ids.size()>Capacity){capacityReached=true;return false;}
                next.sorted=next.ids;std::sort(next.sorted.begin(),next.sorted.end(),Less);next.count=next.ids.size();
                if(op==SetOperation::Replace)next.anchor=next.count?next.ids.back():GEngine::EntityRenderId{};
                *this=std::move(next);return true;
            } catch(const std::bad_alloc&) {return false;}
        }
        bool Select(const GEngine::RenderEcs& scene,GEngine::EntityRenderId id,bool toggle) {
            if(!SelectSet(scene,{&id,1},toggle?SetOperation::Toggle:SetOperation::Replace))return false;
            anchor=id;return true;
        }
        bool SelectRange(const GEngine::RenderEcs& scene,const std::vector<GEngine::EntityRenderId>& visible,
            GEngine::EntityRenderId clicked,bool additive) {
            const auto last=std::find(visible.begin(),visible.end(),clicked);
            if(last==visible.end() || !scene.Resolve(clicked))return false;
            auto first=std::find(visible.begin(),visible.end(),anchor);
            if(first==visible.end() || !scene.Resolve(anchor))first=last;
            const auto savedAnchor=first==last?clicked:anchor;
            const auto low=std::min(first,last),high=std::max(first,last);
            if(!SelectSet(scene,{&*low,std::size_t(high-low)+1},additive?SetOperation::Add:SetOperation::Replace))return false;
            auto active=std::find(ids.begin(),ids.end(),clicked);std::rotate(active,active+1,ids.end());anchor=savedAnchor;return true;
        }
    };
}
