// CPU fixture linked to the production GEngine library; no GL context is required.
#include <Scene/_Entity.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

int RunProgramGroupingRegression();

namespace
{
    using namespace GEngine;
    using namespace GEngine::Component;
    int checks = 0;

    void Require(bool condition, const char* message)
    {
        ++checks;
        if (!condition) throw std::runtime_error(message);
    }

    template<typename Action>
    void Reject(Action action)
    {
        bool rejected = false;
        try { action(); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "Invalid entity operation was accepted");
    }

    void Parenting()
    {
        _Scene scene, foreign;
        auto root = scene.CreateEntityWithUUID(1, "root");
        auto other = scene.CreateEntityWithUUID(2, "other");
        auto child = scene.CreateEntityWithUUID(3, "child");
        auto leaf = scene.CreateEntityWithUUID(4, "leaf");
        const auto empty = scene.CreateEntity("empty");
        Require(!empty.GetParent() && empty.Children().empty(), "Missing relationships are not empty");
        Require(!empty.HasAllComponents<RelationshipComponent>(), "Const query added a component");
        child.SetParent({});
        child.SetParent(root);
        leaf.SetParent(child);
        Require(child.GetParent() == root && root.Children() == std::vector<UUID>{3}, "Parent link is not reciprocal");
        child.SetParent(root);
        Require(root.Children().size() == 1, "Same-parent operation duplicated child");
        Require(root.IsAncesterOf(leaf) && leaf.IsDescendantOf(root)
            && !leaf.IsAncesterOf(root) && !root.IsAncesterOf(root), "Ancestor query is wrong");
        Reject([&] { child.SetParent(child); });
        Reject([&] { root.SetParent(child); });
        Reject([&] { root.SetParent(leaf); });
        Reject([&] { root.SetParentUUID(leaf.GetUUID()); });
        auto alien = foreign.CreateEntityWithUUID(1, "same UUID, foreign scene");
        Reject([&] { child.SetParent(alien); });
        Reject([&] { child.SetParentUUID(999); });
        auto stale = scene.CreateEntity("stale");
        scene.DestroyEntity(stale);
        Reject([&] { child.SetParent(stale); });
        Reject([&] { child.SetParent(_Entity((entt::entity)root, nullptr)); });
        Reject([&] { _Entity{}.SetParent(root); });
        Reject([&] { stale.SetParent(root); });
        auto raw = _Entity(scene.Reg().create(), &scene);
        Reject([&] { child.SetParent(raw); });
        Reject([&] { scene.DestroyEntity(raw); });
        scene.Reg().destroy(raw);
        Require(child.GetParent() == root && root.Children() == std::vector<UUID>{3}
            && leaf.GetParent() == child && child.Children() == std::vector<UUID>{4}, "Rejected parenting mutated graph");

        child.SetParentUUID(other.GetUUID());
        Require(root.Children().empty() && other.Children() == std::vector<UUID>{3}
            && child.GetParent() == other && leaf.GetParent() == child, "Reparenting broke links");
        child.SetParentUUID(0);
        Require(!child.GetParent() && other.Children().empty() && leaf.GetParent() == child, "Null UUID detach broke descendants");
        child.SetParent(root);
        Require(root.RemoveChild(child) && !child.GetParent() && root.Children().empty(), "RemoveChild left a back-link");
        Require(!root.RemoveChild(child) && !root.RemoveChild(alien) && !root.RemoveChild({}), "RemoveChild accepted a non-child");
        child.SetParent(root);
        child.SetParent(_Entity(entt::null, &scene));
        Require(!child.GetParent() && root.Children().empty(), "Scene-bound null did not detach");

        // Raw component writes are outside SetParent's contract; queries must still terminate.
        root.Children();
        other.Children();
        root.GetComponent<RelationshipComponent>().ParentHandle = other.GetUUID();
        other.GetComponent<RelationshipComponent>().ParentHandle = root.GetUUID();
        Reject([&] { child.SetParent(root); });
        Require(!leaf.IsAncesterOf(root), "Malformed ancestor cycle did not terminate");
        other.GetComponent<RelationshipComponent>().ParentHandle = 999;
        Reject([&] { child.SetParent(root); });
    }

    void DestructionAndReuse()
    {
        _Scene scene, foreign;
        scene.DestroyEntity(_Entity{});
        scene.DestroyEntity(UUID(0));
        scene.DestroyEntity(UUID(999));
        auto entity = scene.CreateEntityWithUUID(10, "lvalue");
        const auto handle = (entt::entity)entity;
        const auto borrowed = entity;
        { auto copy = entity; Require(copy == entity, "Entity copy changed identity"); }
        Require(entity && borrowed, "Wrapper destruction owned an entity");
        scene.DestroyEntity(entity);
        Require(!entity && !borrowed && !scene.GetEntityByUUID(10)
            && !entity.HasAllComponents<IDComponent>(), "Lvalue destroy left a valid entity");
        auto reused = scene.CreateEntityWithUUID(10, "reused UUID");
        Require(entt::to_entity((entt::entity)reused) == entt::to_entity(handle)
            && (entt::entity)reused != handle, "Fixture did not reuse an EnTT slot with a new generation");
        scene.DestroyEntity(entity);
        Require(reused && !entity, "Stale destroy removed reused entity");
        auto alien = foreign.CreateEntityWithUUID(10, "foreign");
        Reject([&] { scene.DestroyEntity(alien); });
        Require(alien && reused, "Foreign destruction mutated either scene");
        scene.DestroyEntity(scene.GetEntityByUUID(10));
        Require(!reused && !scene.GetEntityByUUID(10), "Rvalue destroy differs from lvalue");
        scene.DestroyEntity(scene.CreateEntityWithUUID(11));
        Require(!scene.GetEntityByUUID(11), "Immediate temporary destroy failed");
        Reject([&] { scene.CreateEntityWithUUID(0); });
        auto unique = scene.CreateEntityWithUUID(12);
        Reject([&] { scene.CreateEntityWithUUID(12); });
        Require(scene.GetEntityByUUID(12) == unique, "Duplicate UUID replaced the existing identity");
        scene.DestroyEntity(unique.GetUUID());
        Require(!unique, "UUID destroy left entity valid");

        for (int i = 0; i < 256; ++i)
        {
            auto next = scene.CreateEntityWithUUID(100);
            Require(!entity && next.GetParentUUID() == 0, "Reuse inherited stale validity or relationships");
            scene.DestroyEntity(next);
            Require(!next && !scene.GetEntityByUUID(100), "Repeated reuse failed");
        }

        auto root = scene.CreateEntity("root");
        auto parent = scene.CreateEntity("parent");
        auto child = scene.CreateEntity("child");
        auto grandchild = scene.CreateEntity("grandchild");
        parent.SetParent(root); child.SetParent(parent); grandchild.SetParent(child);
        scene.DestroyEntity(parent, true);
        Require(!parent && child && grandchild && !child.GetParent() && grandchild.GetParent() == child
            && root.Children().empty(), "excludeChildren did not detach surviving children");
        child.SetParent(root);
        scene.DestroyEntity(child, false, false);
        Require(!child && !grandchild && root.Children().empty(), "Legacy first=false left stale links");

        std::vector<_Entity> descendants;
        for (int i = 0; i < 64; ++i)
        {
            auto a = scene.CreateEntity("sibling"); a.SetParent(root);
            auto b = scene.CreateEntity("grandchild"); b.SetParent(a);
            descendants.push_back(a); descendants.push_back(b);
        }
        scene.DestroyEntity(root.GetUUID());
        Require(!root && std::all_of(descendants.begin(), descendants.end(), [] (const auto& e) { return !e; }),
            "Recursive destruction skipped relocated components or siblings");
    }

    void CopiesAndConstAccess()
    {
        auto scene = CreateRefPtr<_Scene>();
        auto root = scene->CreateEntity("root");
        auto child = scene->CreateEntity("child"); child.SetParent(root);
        auto leaf = scene->CreateEntity("leaf"); leaf.SetParent(child);
        auto duplicate = scene->DuplicateEntity(child);
        Require(duplicate.GetParent() == root && duplicate.Children().empty()
            && leaf.GetParent() == child && root.Children().size() == 2, "Duplicate aliased source relationships");
        scene->DestroyEntity(duplicate);
        Require(child && leaf && root.Children() == std::vector<UUID>{child.GetUUID()}, "Destroy duplicate affected original tree");
        auto copied = _Scene::Copy(scene);
        auto copiedLeaf = copied->GetEntityByUUID(leaf.GetUUID());
        Require(copiedLeaf.GetParent() == copied->GetEntityByUUID(child.GetUUID()), "Scene copy lost internal relationships");
        copied->DestroyEntity(root.GetUUID());
        Require(!copiedLeaf && root && child && leaf, "Scene copy borrowed original entity ownership");

        const auto& constant = child;
        static_assert(std::is_same_v<decltype(constant.Transform()), Mat4>);
        static_assert(std::is_same_v<decltype(constant.GetComponent<Transform3DComponent>()), const Transform3DComponent&>);
        static_assert(std::is_same_v<decltype(constant.Children()), const std::vector<UUID>&>);
        child.Transform().Translation = { 3.0f, 4.0f, 5.0f };
        child.Transform().Scale = { 2.0f, 3.0f, 4.0f };
        child.Transform().SetRotation(Vec3f{0.1f, 0.2f, 0.3f});
        const auto& matrix = constant.Transform(); // Lifetime extends the returned value.
        const auto expected = child.Transform().GetTransform();
        Require(matrix == expected && !child.HasAllComponents<TransformComponent>(), "Const access used the wrong transform");
        Require(&constant.GetComponent<Transform3DComponent>() == &child.Transform(), "Const component access returned a copy");
        Require(&constant.Children() == &child.Children() && &constant.Name() == &child.Name(), "Const access returned dangling subobjects");
        child.Transform().Translation.x = 20.0f;
        Require(matrix == expected && constant.Transform() != expected, "Const matrix value did not retain its lifetime");
        const _Entity invalid;
        Require(!invalid && !invalid.GetParent() && invalid.Children().empty() && invalid.Name() == "Unnamed",
            "Null read-only relationship queries are unsafe");
    }
}

int main()
{
    try
    {
        Parenting();
        DestructionAndReuse();
        CopiesAndConstAccess();
        if (RunProgramGroupingRegression() != 0) return 1;
        std::cout << "[PASS] entity-api " << checks << " checks plus program-grouping regression\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] entity-api after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
