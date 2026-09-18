// CPU fixture linked to the production GEngine library; no GL context is required.
#include <Scene/_Entity.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <limits>

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
        Require(child.SetParent({}).has_value(), "Valid parenting failed");
        Require(child.SetParent(root).has_value(), "Valid parenting failed");
        Require(leaf.SetParent(child).has_value(), "Valid parenting failed");
        Require(child.GetParent() == root && root.Children() == std::vector<UUID>{3}, "Parent link is not reciprocal");
        Require(child.SetParent(root).has_value(), "Valid parenting failed");
        Require(root.Children().size() == 1, "Same-parent operation duplicated child");
        Require(root.IsAncesterOf(leaf) && leaf.IsDescendantOf(root)
            && !leaf.IsAncesterOf(root) && !root.IsAncesterOf(root), "Ancestor query is wrong");
        Require(!child.SetParent(child), "Invalid parenting returned success");
        Require(!root.SetParent(child), "Invalid parenting returned success");
        Require(!root.SetParent(leaf), "Invalid parenting returned success");
        Require(!root.SetParentUUID(leaf.GetUUID()), "Invalid parenting returned success");
        auto alien = foreign.CreateEntityWithUUID(1, "same UUID, foreign scene");
        Require(!child.SetParent(alien), "Invalid parenting returned success");
        Require(!child.SetParentUUID(999), "Invalid parenting returned success");
        auto stale = scene.CreateEntity("stale");
        scene.DestroyEntity(stale);
        Require(!child.SetParent(stale), "Invalid parenting returned success");
        Require(!child.SetParent(_Entity((entt::entity)root, nullptr)), "Invalid parenting returned success");
        Require(!_Entity{}.SetParent(root), "Invalid parenting returned success");
        Require(!stale.SetParent(root), "Invalid parenting returned success");
        auto raw = _Entity(scene.Reg().create(), &scene);
        Require(!child.SetParent(raw), "Invalid parenting returned success");
        Require(!raw.SetParentUUID(999), "Raw entity without an ID accepted a UUID operation");
        Reject([&] { scene.DestroyEntity(raw); });
        scene.Reg().destroy(raw);
        Require(child.GetParent() == root && root.Children() == std::vector<UUID>{3}
            && leaf.GetParent() == child && child.Children() == std::vector<UUID>{4}, "Rejected parenting mutated graph");

        Require(child.SetParentUUID(other.GetUUID()).has_value(), "Valid parenting failed");
        Require(root.Children().empty() && other.Children() == std::vector<UUID>{3}
            && child.GetParent() == other && leaf.GetParent() == child, "Reparenting broke links");
        Require(child.SetParentUUID(0).has_value(), "Valid parenting failed");
        Require(!child.GetParent() && other.Children().empty() && leaf.GetParent() == child, "Null UUID detach broke descendants");
        Require(child.SetParent(root).has_value(), "Valid parenting failed");
        Require(root.RemoveChild(child) && !child.GetParent() && root.Children().empty(), "RemoveChild left a back-link");
        Require(!root.RemoveChild(child) && !root.RemoveChild(alien) && !root.RemoveChild({}), "RemoveChild accepted a non-child");
        Require(child.SetParent(root).has_value(), "Valid parenting failed");
        Require(child.SetParent(_Entity(entt::null, &scene)).has_value(), "Valid parenting failed");
        Require(!child.GetParent() && root.Children().empty(), "Scene-bound null did not detach");

        // Raw component writes are outside SetParent's contract; queries must still terminate.
        root.Children();
        other.Children();
        root.GetComponent<RelationshipComponent>().ParentHandle = other.GetUUID();
        other.GetComponent<RelationshipComponent>().ParentHandle = root.GetUUID();
        Require(!child.SetParent(root), "Invalid parenting returned success");
        Require(!leaf.IsAncesterOf(root), "Malformed ancestor cycle did not terminate");
        other.GetComponent<RelationshipComponent>().ParentHandle = 999;
        Require(!child.SetParent(root), "Invalid parenting returned success");
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
        Require(parent.SetParent(root).has_value(), "Valid parenting failed"); Require(child.SetParent(parent).has_value(), "Valid parenting failed"); Require(grandchild.SetParent(child).has_value(), "Valid parenting failed");
        scene.DestroyEntity(parent, true);
        Require(!parent && child && grandchild && !child.GetParent() && grandchild.GetParent() == child
            && root.Children().empty(), "excludeChildren did not detach surviving children");
        Require(child.SetParent(root).has_value(), "Valid parenting failed");
        scene.DestroyEntity(child, false, false);
        Require(!child && !grandchild && root.Children().empty(), "Legacy first=false left stale links");

        std::vector<_Entity> descendants;
        for (int i = 0; i < 64; ++i)
        {
            auto a = scene.CreateEntity("sibling"); Require(a.SetParent(root).has_value(), "Valid parenting failed");
            auto b = scene.CreateEntity("grandchild"); Require(b.SetParent(a).has_value(), "Valid parenting failed");
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
        auto child = scene->CreateEntity("child"); Require(child.SetParent(root).has_value(), "Valid parenting failed");
        auto leaf = scene->CreateEntity("leaf"); Require(leaf.SetParent(child).has_value(), "Valid parenting failed");
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
    const WorldTransform& World(const WorldTransformUpdate& frame, _Entity entity)
    {
        const auto id = entity.GetSceneContext()->RenderData().Identify(entity);
        Require(id.has_value(), "Entity identity missing");
        const auto found = std::find_if(frame.transforms.begin(), frame.transforms.end(),
            [&](const WorldTransform& transform) { return transform.entity == *id; });
        Require(found != frame.transforms.end(), "World transform missing");
        return *found;
    }

    WorldTransformUpdate Evaluate(_Scene& scene)
    {
        auto result = scene.UpdateWorldTransforms();
        Require(result.has_value(), "Hierarchy evaluation failed");
        return std::move(*result);
    }

    void Close(const Mat4& actual, const Mat4& expected)
    {
        for (int c = 0; c != 4; ++c)
            for (int r = 0; r != 4; ++r)
                Require(std::abs(actual[c][r] - expected[c][r]) < 0.0001f, "World matrix mismatch");
    }

    void Hierarchy()
    {
        auto scene = CreateRefPtr<_Scene>();
        // Deliberately create a child before its parent and use descending UUIDs.
        auto leaf = scene->CreateEntityWithUUID(1, "leaf");
        auto child = scene->CreateEntityWithUUID(2, "child");
        auto root = scene->CreateEntityWithUUID(3, "root");
        auto other = scene->CreateEntityWithUUID(4, "other");
        Require(leaf.SetParent(child).has_value() && child.SetParent(root).has_value(), "Valid hierarchy rejected");
        root.Transform().Translation = {2, 3, 4};
        root.Transform().Scale = {-2, 3, 0.5f};
        root.Transform().SetRotation(Vec3f{0.2f, 0.3f, 0.4f});
        child.Transform().Translation = {1, 2, 3};
        child.Transform().SetRotation(Vec3f{0.5f, 0.1f, 0.2f});
        leaf.Transform().Translation = {4, 5, 6};
        other.Transform().Translation = {20, 0, 0};
        const auto authored = leaf.Transform().GetTransform();
        const auto first = Evaluate(*scene);
        Require(first.recomputed == 4, "Initial frame did not compute every world");
        Require(first.transforms[0].entity == World(first, root).entity
            && first.transforms[1].entity == World(first, child).entity
            && first.transforms[2].entity == World(first, leaf).entity, "Order is not parent before child");
        Close(World(first, leaf).matrix, root.Transform().GetTransform() * child.Transform().GetTransform() * authored);
        Require(leaf.Transform().GetTransform() == authored, "World evaluation changed authored local TRS");
        auto unchanged = Evaluate(*scene);
        Require(unchanged.recomputed == 0 && World(first, leaf).revision == World(unchanged, leaf).revision,
            "Unchanged frame recomputed or changed revision");
        root.Transform().Translation.x += 5;
        auto moved = Evaluate(*scene);
        Require(moved.recomputed == 3 && World(moved, other).revision == World(first, other).revision,
            "Dirty propagation crossed an unrelated root");
        Require(World(moved, leaf).revision == World(first, leaf).revision + 1, "Descendant did not update");
        Require(child.SetParent(other).has_value(), "Reparent failed");
        auto reparented = Evaluate(*scene);
        Require(reparented.recomputed == 2, "Reparent did not update exactly its subtree");
        Close(World(reparented, leaf).matrix, other.Transform().GetTransform() * child.Transform().GetTransform() * authored);
        Require(!root.SetParent(root) && !other.SetParent(leaf), "Self/cycle accepted");
        Require(Evaluate(*scene).recomputed == 0, "Rejected parenting changed worlds");

        // Value snapshots survive later authoring and registry mutation.
        Close(World(first, leaf).matrix, glm::translate(Mat4(1), Vec3f{-5, 0, 0})
            * root.Transform().GetTransform() * child.Transform().GetTransform() * authored);
        auto duplicate = scene->DuplicateEntity(child);
        auto copied = _Scene::Copy(scene);
        auto copiedFrame = Evaluate(*copied);
        Close(World(copiedFrame, copied->GetEntityByUUID(leaf.GetUUID())).matrix, World(reparented, leaf).matrix);
        auto duplicated = Evaluate(*scene);
        Close(World(duplicated, duplicate).matrix, World(duplicated, child).matrix);
        Require(World(copiedFrame, copied->GetEntityByUUID(leaf.GetUUID())).entity.registry
            != World(duplicated, leaf).entity.registry, "Copy retained source runtime identity");

        scene->DestroyEntity(other, true);
        Require(!child.GetParent() && !duplicate.GetParent(), "Destroy/detach retained parent");
        auto detached = Evaluate(*scene);
        Close(World(detached, leaf).matrix, child.Transform().GetTransform() * authored);
        scene->DestroyEntity(child);
        Require(!child && !leaf && duplicate, "Recursive parent destruction missed descendants");
        Evaluate(*scene);
        std::cout << "[PASS] hierarchy composition/reparent/copy/destroy/unchanged\n";
    }

    void HierarchyFailuresAndAnchors()
    {
        _Scene scene, foreign;
        auto parent = scene.CreateEntityWithUUID(10);
        auto child = scene.CreateEntityWithUUID(20);
        Require(child.SetParent(parent).has_value(), "Parent setup failed");
        auto original = Evaluate(scene);
        auto badParent = child.SetParent(foreign.CreateEntity());
        Require(!badParent && badParent.error().code == TransformErrorCode::ForeignEntity, "Foreign error lost type");
        auto self = parent.SetParent(parent);
        Require(!self && self.error().code == TransformErrorCode::Cycle && self.error().entity == parent.GetUUID(),
            "Cycle error lost identity");

        // Corruption through the legacy raw registry is detected without recursion.
        auto& link = parent.AddOrReplaceComponent<RelationshipComponent>();
        link.ParentHandle = child.GetUUID();
        link.ParentIdentity = *scene.RenderData().Identify(child);
        auto cycle = scene.UpdateWorldTransforms();
        Require(!cycle && cycle.error().code == TransformErrorCode::Cycle, "Raw cycle was not rejected");
        Require(parent.SetParent({}).has_value(), "Detach could not repair a malformed graph");
        Require(Evaluate(scene).recomputed == 0, "Invalid graph published caches");

        child.Transform().Translation.x = std::numeric_limits<float>::infinity();
        auto nonfinite = scene.UpdateWorldTransforms();
        Require(!nonfinite && nonfinite.error().code == TransformErrorCode::NonFiniteTransform, "Nonfinite local accepted");
        child.Transform().Translation.x = 0;
        child.Transform().QuatRotation = Quat{0, 0, 0, 0};
        auto rotation = scene.UpdateWorldTransforms();
        Require(!rotation && rotation.error().code == TransformErrorCode::InvalidRotation, "Zero rotation accepted");
        child.Transform().QuatRotation = Quat{1, 0, 0, 0};
        parent.Transform().Scale = Vec3f{std::numeric_limits<float>::max()};
        child.Transform().Scale = Vec3f{2};
        auto overflow = scene.UpdateWorldTransforms();
        Require(!overflow && overflow.error().code == TransformErrorCode::NonFiniteTransform, "Composed overflow accepted");
        parent.Transform().Scale = child.Transform().Scale = Vec3f{1};
        Require(Evaluate(scene).recomputed == 0, "Failed evaluation committed partial cache updates");
        child.RemoveComponent<Transform3DComponent>();
        auto missing = scene.UpdateWorldTransforms();
        Require(!missing && missing.error().code == TransformErrorCode::MissingTransform, "Missing transform accepted");
        child.AddComponent<Transform3DComponent>();

        // Bypass scene destruction deliberately, then recreate the exact UUID.
        const auto oldId = *scene.RenderData().Identify(parent);
        scene.Reg().destroy(parent);
        auto replacement = scene.CreateEntityWithUUID(10);
        auto stale = scene.UpdateWorldTransforms();
        Require(!stale && stale.error().code == TransformErrorCode::InvalidParent && !child.GetParent(),
            "UUID reuse resurrected a dead parent");
        Require(!scene.RenderData().Resolve(oldId), "Destroyed parent render identity survived");
        // A stale child UUID in the replacement's reverse list cannot give it ownership.
        replacement.Children().push_back(child.GetUUID());
        scene.DestroyEntity(replacement);
        Require(child, "Destroying replacement adopted an old generation's child");
        replacement = scene.CreateEntityWithUUID(10);
        Require(child.SetParent(replacement).has_value(), "Explicit repair/reparent failed");

        replacement.Transform().Translation = {100, 0, 0};
        child.Transform().Translation = {3, 0, 0};
        child.AddComponent<RigidBody3DComponent>();
        auto descendant = scene.CreateEntityWithUUID(30);
        descendant.Transform().Translation = {2, 0, 0};
        Require(descendant.SetParent(child).has_value(), "Anchor descendant rejected");
        auto anchored = Evaluate(scene);
        Close(World(anchored, child).matrix, glm::translate(Mat4(1), Vec3f{3, 0, 0}));
        Close(World(anchored, descendant).matrix, glm::translate(Mat4(1), Vec3f{5, 0, 0}));
        replacement.Transform().Translation.x += 1;
        Require(Evaluate(scene).recomputed == 1, "Parent movement altered world-space physics anchor");
        child.RemoveComponent<RigidBody3DComponent>();
        auto composed = Evaluate(scene);
        Close(World(composed, descendant).matrix, glm::translate(Mat4(1), Vec3f{106, 0, 0}));
        std::cout << "[PASS] hierarchy typed failures/transaction/lifetime/physics anchors\n";
    }

    void DeepHierarchy()
    {
        _Scene scene;
        constexpr int depth = 8192;
        std::vector<_Entity> entities;
        for (int i = 0; i != depth; ++i)
        {
            entities.push_back(scene.CreateEntityWithUUID(depth - i));
            entities.back().Transform().Translation = {1, 0, 0};
        }
        // Build from the leaf upward so setup itself is linear, with adversarial UUID order.
        for (int i = depth - 1; i > 0; --i)
            Require(entities[i].SetParent(entities[i - 1]).has_value(), "Deep parent link failed");
        auto frame = Evaluate(scene);
        Require(frame.transforms.size() == depth && frame.recomputed == depth, "Deep graph not fully evaluated");
        for (int i = 0; i != depth; ++i)
        {
            Require(frame.transforms[i].matrix[3].x == static_cast<float>(i + 1), "Deep world/order incorrect");
            Require(frame.transforms[i].entity == *scene.RenderData().Identify(entities[i]), "Deep identity order incorrect");
        }
        Require(Evaluate(scene).recomputed == 0, "Deep unchanged frame recomputed");
        entities[depth / 2].Transform().Translation.x = 2;
        Require(Evaluate(scene).recomputed == depth / 2, "Deep dirty subtree did not propagate");
        Require(!entities.front().SetParent(entities.back()), "Deep cycle was accepted");
        // Duplicate raw reverse links must not double-destroy descendants.
        entities.front().Children().push_back(entities[1].GetUUID());
        scene.DestroyEntity(entities.front());
        Require(Evaluate(scene).transforms.empty(), "Deep destruction left descendants alive");
        std::cout << "[PASS] hierarchy depth=8192 deterministic order and cached subtree\n";
    }
}

int main()
{
    try
    {
        Parenting();
        DestructionAndReuse();
        CopiesAndConstAccess();
        Hierarchy();
        HierarchyFailuresAndAnchors();
        DeepHierarchy();
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
