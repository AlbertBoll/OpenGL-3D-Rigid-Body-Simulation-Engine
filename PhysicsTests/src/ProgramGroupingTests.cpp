#include "../../GEngine/src/Assets/ShaderBackend.h"
// CPU-only tests of the real scene paths. Synthetic GL names never reach a GL call.
#include <GEngine/Scene/_Entity.h>
#include <GEngine/Assets/Shaders/Shader.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace GEngine;
    using namespace GEngine::Component;

    // The existing test target uses this access technique for lifetime fixtures.
    // Keep name injection out of the production Shader API.
    template<typename Tag, typename Tag::Type Member>
    struct GroupingMemberAccess
    {
        friend typename Tag::Type GroupingMember(Tag) { return Member; }
    };
    struct ProgramNameTag
    {
        using Type = std::unique_ptr<Asset::ShaderStorage> Asset::Shader::*;
        friend Type GroupingMember(ProgramNameTag);
    };
    template struct GroupingMemberAccess<ProgramNameTag, &Asset::Shader::m_Storage>;

    struct TestProgram
    {
        Asset::Shader shader;
        explicit TestProgram(unsigned int name) { Set(name); }
        ~TestProgram() { Set(0); } // Shader owns no real GL object in this fixture.
        void Set(unsigned int name)
        {
            auto& storage = shader.*GroupingMember(ProgramNameTag{});
            if (!storage) storage = std::make_unique<Asset::ShaderStorage>();
            storage->program = name;
        }
    };

    int checks = 0;
    void Require(bool condition, const char* message)
    {
        ++checks;
        if (!condition) throw std::runtime_error(message);
    }

    template<typename Action>
    void RequireRejected(Action action)
    {
        bool rejected = false;
        try { action(); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "Invalid render-list input was not rejected");
    }

    _Entity Add(_Scene& scene, Asset::Shader* shader, const char* name)
    {
        auto entity = scene.CreateEntity(name);
        entity.AddComponent<RelationshipComponent>();
        entity.AddComponent<RenderComponent>().Shader = shader;
        return entity;
    }

    void SparseNamesAndMaterials()
    {
        TestProgram low(1), sparse(4099), high(std::numeric_limits<unsigned int>::max());
        _Scene scene;
        auto a = Add(scene, &sparse.shader, "material-a");
        auto b = Add(scene, &sparse.shader, "material-b");
        a.AddComponent<MaterialComponent>().ShineDamper = { "shine", 8.0f };
        b.AddComponent<MaterialComponent>().ShineDamper = { "shine", 32.0f };
        auto c = Add(scene, &high.shader, "high");
        auto d = Add(scene, &low.shader, "low");
        scene.PushToRenderList(a, b, c, d);
        const auto& groups = scene.GetGroupEntities();
        Require(groups.size() == 3, "Sparse names did not produce exactly three groups");
        auto it = groups.begin();
        Require(it++->first == 1 && it++->first == 4099
            && it->first == std::numeric_limits<unsigned int>::max(), "Program order changed");
        Require(groups.at(4099) == std::vector<_Entity>({ a, b }), "Material sharing or entity order changed");
        Require(a.GetComponent<MaterialComponent>().ShineDamper.Data == 8.0f
            && b.GetComponent<MaterialComponent>().ShineDamper.Data == 32.0f, "Material data changed");
        scene.PushToRenderList(a);
        Require(groups.at(4099) == std::vector<_Entity>({ a, b }), "Repeated publication duplicated/reordered an entity");
        Require(scene.GetLightEntitiesWithRenderID(0).empty()
            && scene.GetLightEntitiesWithRenderID(4099).empty()
            && scene.GetLightEntitiesWithRenderID(std::numeric_limits<unsigned int>::max()).empty(),
            "Missing light lookup did not return an empty range");
        Require(scene.GetLightEntities().empty(), "Read-only lookup created light groups");
        std::cout << "[PASS] sparse/full-width names, material sharing, deterministic order, lookup misses\n";
    }

    void RecreationAndRemoval()
    {
        TestProgram program(71), other(900001);
        _Scene scene;
        auto a = Add(scene, &program.shader, "recreated");
        auto b = Add(scene, &other.shader, "survivor");
        auto light = Add(scene, &program.shader, "light");
        light.AddComponent<PointLightComponent>();
        scene.PushToRenderList(a, b, light);
        program.Set(0); // Simulate destruction: deletion must not dereference the current program.
        scene.DestroyEntity(light.GetUUID());
        Require(scene.GetLightEntities().empty(), "Destroy left a stale light entry");
        program.Set(800003); // Same Shader wrapper, new GL name: explicitly republish it.
        scene.PushToRenderList(a);
        Require(!scene.GetGroupEntities().contains(71)
            && scene.GetGroupEntities().at(800003) == std::vector<_Entity>({ a }), "Recreation retained old membership");
        a.GetComponent<RenderComponent>().Shader = &other.shader;
        scene.PushToRenderList(a);
        Require(scene.GetGroupEntities().size() == 1
            && scene.GetGroupEntities().at(900001) == std::vector<_Entity>({ b, a }), "Shader reassignment changed groups incorrectly");
        a.RemoveComponent<RenderComponent>();
        scene.DestroyEntity(a);
        Require(scene.GetGroupEntities().at(900001) == std::vector<_Entity>({ b }), "Deletion depended on current render component");
        auto recreated = Add(scene, &program.shader, "new-entity");
        scene.PushToRenderList(recreated);
        scene.DestroyEntity(recreated, false, true);
        Require(scene.GetGroupEntities().size() == 1, "Create/destroy left an empty group");
        scene.DestroyEntity(b.GetUUID());
        Require(scene.GetGroupEntities().empty(), "Final deletion left render membership");
        program.Set(71); // Reused GL name after all old members have retired.
        auto reused = Add(scene, &program.shader, "reused-name");
        scene.PushToRenderList(reused);
        Require(scene.GetGroupEntities().at(71) == std::vector<_Entity>({ reused }), "Reused name resurrected stale entities");
        std::cout << "[PASS] program recreation/reuse, republishing, all deletion entry points\n";
    }

    void CopyDuplicateAndLightGroups()
    {
        TestProgram program(12345), other(998877);
        auto scene = CreateRefPtr<_Scene>();
        auto mesh = Add(*scene, &program.shader, "mesh");
        auto directional = Add(*scene, &program.shader, "directional");
        auto point = Add(*scene, &other.shader, "point");
        auto spot = Add(*scene, &program.shader, "spot");
        directional.AddComponent<DirectionalLightComponent>();
        point.AddComponent<PointLightComponent>();
        spot.AddComponent<SpotLightComponent>();
        scene->PushToRenderList(mesh, directional, point, spot);
        Require(scene->GetGroupEntities().at(12345) == std::vector<_Entity>({ mesh }), "Lights entered the mesh group");
        Require(scene->GetLightEntitiesWithRenderID(12345) == std::vector<_Entity>({ directional, spot })
            && scene->GetLightEntitiesWithRenderID(998877) == std::vector<_Entity>({ point }), "Light association/order differs");
        auto copied = _Scene::Copy(scene);
        Require(copied->GetGroupEntities().size() == 1 && copied->GetGroupEntities().at(12345).size() == 1,
            "Scene copy misclassified render groups");
        Require(copied->GetLightEntitiesWithRenderID(12345).size() == 2
            && copied->GetLightEntitiesWithRenderID(998877).size() == 1, "Scene copy lost light groups");
        auto copiedPoint = copied->GetEntityByUUID(point.GetUUID());
        auto copiedSpot = copied->GetEntityByUUID(spot.GetUUID());
        Require(copiedPoint.HasAllComponents<PointLightComponent>() && copiedSpot.HasAllComponents<SpotLightComponent>(),
            "Copy dropped light classification components");
        for (auto* groups : { &copied->GetGroupEntities(), &copied->GetLightEntities() })
            for (const auto& [name, group] : *groups)
                for (const auto& entity : group)
                    Require(entity.GetSceneContext() == copied.get() && copied->Reg().valid(entity), "Copy retained source-scene entities");
        auto duplicateMesh = scene->DuplicateEntity(mesh);
        auto duplicatePoint = scene->DuplicateEntity(point);
        auto duplicateSpot = scene->DuplicateEntity(spot);
        Require(scene->GetGroupEntities().at(12345) == std::vector<_Entity>({ mesh, duplicateMesh }), "Mesh duplicate changed grouping");
        Require(scene->GetLightEntitiesWithRenderID(998877) == std::vector<_Entity>({ point, duplicatePoint })
            && scene->GetLightEntitiesWithRenderID(12345).back() == duplicateSpot, "Light duplicate changed grouping");
        scene->DestroyEntity(point.GetUUID());
        Require(scene->GetLightEntitiesWithRenderID(998877) == std::vector<_Entity>({ duplicatePoint }), "Light removal changed another membership");
        Require(copied->GetLightEntitiesWithRenderID(998877).size() == 1, "Original deletion modified scene copy");
        // Recursive deletion crosses mesh/light groups and retires both memberships.
        auto child = Add(*scene, &other.shader, "child");
        child.AddComponent<PointLightComponent>();
        Require(child.SetParent(duplicateMesh).has_value(), "Grouping hierarchy setup failed");
        scene->PushToRenderList(child);
        scene->DestroyEntity(duplicateMesh);
        Require(scene->GetGroupEntities().at(12345) == std::vector<_Entity>({ mesh })
            && scene->GetLightEntitiesWithRenderID(998877) == std::vector<_Entity>({ duplicatePoint }),
            "Recursive deletion retained a cross-group child");
        std::cout << "[PASS] copy/duplicate, all light kinds, cross-group hierarchy deletion\n";
    }

    void InvalidInputs()
    {
        TestProgram program(91), uncreated(0);
        _Scene scene, foreign;
        auto nullShader = Add(scene, nullptr, "null");
        auto zeroShader = Add(scene, &uncreated.shader, "zero");
        auto foreignEntity = Add(foreign, &program.shader, "foreign");
        RequireRejected([&] { scene.PushToRenderList(nullShader); });
        RequireRejected([&] { scene.PushToRenderList(zeroShader); });
        RequireRejected([&] { scene.PushToRenderList(foreignEntity); });
        RequireRejected([&] { scene.PushToRenderList(_Entity{}); });
        auto stale = Add(scene, &program.shader, "stale");
        scene.DestroyEntity(stale);
        RequireRejected([&] { scene.PushToRenderList(stale); });
        Require(scene.GetGroupEntities().empty() && scene.GetLightEntities().empty(), "Invalid input mutated groups");
        std::cout << "[PASS] null/zero program and invalid/foreign/stale entity rejection\n";
    }
}

int RunProgramGroupingRegression()
{
    try
    {
        SparseNamesAndMaterials();
        RecreationAndRemoval();
        CopyDuplicateAndLightGroups();
        InvalidInputs();
        std::cout << "Program grouping regression: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] Program grouping: " << error.what() << '\n';
        return 1;
    }
}
