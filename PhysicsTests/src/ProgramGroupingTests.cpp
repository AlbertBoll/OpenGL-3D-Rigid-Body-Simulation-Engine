#include <concepts>
#include <functional>
#include "../../GEngine/src/Assets/ShaderBackend.h"
#include "../../GEngine/src/Scene/SceneBackend.h"
// CPU-only tests of the real scene paths. Synthetic GL names never reach a GL call.
#include <GEngine/Scene/_Entity.h>
#include <GEngine/Assets/Shaders/Shader.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    // Test-only preparation for bounded Scene value/void result migrations.
    template<std::invocable Operation>
    auto SceneOperationChecked(Operation&& operation)
    {
        using Result = std::remove_cvref_t<std::invoke_result_t<Operation>>;
        if constexpr (std::is_void_v<Result>) {
            std::invoke(std::forward<Operation>(operation));
        } else {
            auto result = std::invoke(std::forward<Operation>(operation));
            if constexpr (requires { typename Result::error_type; typename Result::value_type; }) {
                if (!result) {
                    const auto& error = result.error();
                    std::cerr << "[FAIL] Valid Scene fixture: operation=" << error.operation
                        << " code=" << static_cast<unsigned>(error.code) << " entity=" << error.entity
                        << ": " << error.message << '\n';
                    std::exit(1);
                }
                if constexpr (std::is_void_v<typename Result::value_type>) return;
                else return std::move(*result);
            } else return result;
        }
    }

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
        const bool rejected = !action().has_value();
        Require(rejected, "Invalid render-list input was not rejected");
    }

    _Entity Add(_Scene& scene, Asset::Shader* shader, const char* name)
    {
        auto entity = SceneOperationChecked([&] { return scene.CreateEntity(name); });
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
        SceneOperationChecked([&] { return scene.PushToRenderList(a, b, c, d); });
        const auto& groups = SceneDetail::BackendAccess::Groups(scene);
        Require(groups.size() == 3, "Sparse names did not produce exactly three groups");
        auto it = groups.begin();
        Require(it++->first == 1 && it++->first == 4099
            && it->first == std::numeric_limits<unsigned int>::max(), "Program order changed");
        Require(groups.at(4099) == std::vector<_Entity>({ a, b }), "Material sharing or entity order changed");
        Require(a.GetComponent<MaterialComponent>().ShineDamper.Data == 8.0f
            && b.GetComponent<MaterialComponent>().ShineDamper.Data == 32.0f, "Material data changed");
        SceneOperationChecked([&] { return scene.PushToRenderList(a); });
        Require(groups.at(4099) == std::vector<_Entity>({ a, b }), "Repeated publication duplicated/reordered an entity");
        Require(!SceneDetail::BackendAccess::Lights(scene).contains(0)
            && !SceneDetail::BackendAccess::Lights(scene).contains(4099)
            && !SceneDetail::BackendAccess::Lights(scene).contains(std::numeric_limits<unsigned int>::max()),
            "Absent light keys acquired group membership");
        Require(SceneDetail::BackendAccess::Lights(scene).empty(), "Read-only lookup created light groups");
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
        SceneOperationChecked([&] { return scene.PushToRenderList(a, b, light); });
        program.Set(0); // Simulate destruction: deletion must not dereference the current program.
        SceneOperationChecked([&] { return scene.DestroyEntity(light.GetUUID()); });
        Require(SceneDetail::BackendAccess::Lights(scene).empty(), "Destroy left a stale light entry");
        program.Set(800003); // Same Shader wrapper, new GL name: explicitly republish it.
        SceneOperationChecked([&] { return scene.PushToRenderList(a); });
        Require(!SceneDetail::BackendAccess::Groups(scene).contains(71)
            && SceneDetail::BackendAccess::Groups(scene).at(800003) == std::vector<_Entity>({ a }), "Recreation retained old membership");
        a.GetComponent<RenderComponent>().Shader = &other.shader;
        SceneOperationChecked([&] { return scene.PushToRenderList(a); });
        Require(SceneDetail::BackendAccess::Groups(scene).size() == 1
            && SceneDetail::BackendAccess::Groups(scene).at(900001) == std::vector<_Entity>({ b, a }), "Shader reassignment changed groups incorrectly");
        a.RemoveComponent<RenderComponent>();
        SceneOperationChecked([&] { return scene.DestroyEntity(a); });
        Require(SceneDetail::BackendAccess::Groups(scene).at(900001) == std::vector<_Entity>({ b }), "Deletion depended on current render component");
        auto recreated = Add(scene, &program.shader, "new-entity");
        SceneOperationChecked([&] { return scene.PushToRenderList(recreated); });
        SceneOperationChecked([&] { return scene.DestroyEntity(recreated, false, true); });
        Require(SceneDetail::BackendAccess::Groups(scene).size() == 1, "Create/destroy left an empty group");
        SceneOperationChecked([&] { return scene.DestroyEntity(b.GetUUID()); });
        Require(SceneDetail::BackendAccess::Groups(scene).empty(), "Final deletion left render membership");
        program.Set(71); // Reused GL name after all old members have retired.
        auto reused = Add(scene, &program.shader, "reused-name");
        SceneOperationChecked([&] { return scene.PushToRenderList(reused); });
        Require(SceneDetail::BackendAccess::Groups(scene).at(71) == std::vector<_Entity>({ reused }), "Reused name resurrected stale entities");
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
        SceneOperationChecked([&] { return scene->PushToRenderList(mesh, directional, point, spot); });
        Require(SceneDetail::BackendAccess::Groups(*scene).at(12345) == std::vector<_Entity>({ mesh }), "Lights entered the mesh group");
        Require(SceneDetail::BackendAccess::Lights(*scene).at(12345) == std::vector<_Entity>({ directional, spot })
            && SceneDetail::BackendAccess::Lights(*scene).at(998877) == std::vector<_Entity>({ point }), "Light association/order differs");
        auto copied = SceneOperationChecked([&] { return _Scene::Copy(scene); });
        Require(SceneDetail::BackendAccess::Groups(*copied).size() == 1 && SceneDetail::BackendAccess::Groups(*copied).at(12345).size() == 1,
            "Scene copy misclassified render groups");
        Require(SceneDetail::BackendAccess::Lights(*copied).at(12345).size() == 2
            && SceneDetail::BackendAccess::Lights(*copied).at(998877).size() == 1, "Scene copy lost light groups");
        auto copiedPoint = copied->GetEntityByUUID(point.GetUUID());
        auto copiedSpot = copied->GetEntityByUUID(spot.GetUUID());
        Require(copiedPoint.HasAllComponents<PointLightComponent>() && copiedSpot.HasAllComponents<SpotLightComponent>(),
            "Copy dropped light classification components");
        for (auto* groups : { &SceneDetail::BackendAccess::Groups(*copied), &SceneDetail::BackendAccess::Lights(*copied) })
            for (const auto& [name, group] : *groups)
                for (const auto& entity : group)
                    Require(entity.GetSceneContext() == copied.get() && copied->Reg().valid(entity), "Copy retained source-scene entities");
        auto duplicateMesh = SceneOperationChecked([&] { return scene->DuplicateEntity(mesh); });
        auto duplicatePoint = SceneOperationChecked([&] { return scene->DuplicateEntity(point); });
        auto duplicateSpot = SceneOperationChecked([&] { return scene->DuplicateEntity(spot); });
        Require(SceneDetail::BackendAccess::Groups(*scene).at(12345) == std::vector<_Entity>({ mesh, duplicateMesh }), "Mesh duplicate changed grouping");
        Require(SceneDetail::BackendAccess::Lights(*scene).at(998877) == std::vector<_Entity>({ point, duplicatePoint })
            && SceneDetail::BackendAccess::Lights(*scene).at(12345).back() == duplicateSpot, "Light duplicate changed grouping");
        SceneOperationChecked([&] { return scene->DestroyEntity(point.GetUUID()); });
        Require(SceneDetail::BackendAccess::Lights(*scene).at(998877) == std::vector<_Entity>({ duplicatePoint }), "Light removal changed another membership");
        Require(SceneDetail::BackendAccess::Lights(*copied).at(998877).size() == 1, "Original deletion modified scene copy");
        // Recursive deletion crosses mesh/light groups and retires both memberships.
        auto child = Add(*scene, &other.shader, "child");
        child.AddComponent<PointLightComponent>();
        Require(child.SetParent(duplicateMesh).has_value(), "Grouping hierarchy setup failed");
        SceneOperationChecked([&] { return scene->PushToRenderList(child); });
        SceneOperationChecked([&] { return scene->DestroyEntity(duplicateMesh); });
        Require(SceneDetail::BackendAccess::Groups(*scene).at(12345) == std::vector<_Entity>({ mesh })
            && SceneDetail::BackendAccess::Lights(*scene).at(998877) == std::vector<_Entity>({ duplicatePoint }),
            "Recursive deletion retained a cross-group child");
        std::cout << "[PASS] copy/duplicate, all light kinds, cross-group hierarchy deletion\n";
    }

    void TypedPublicationFailures()
    {
        TestProgram program(701);
        auto scene = CreateRefPtr<_Scene>();
        auto parent = Add(*scene, &program.shader, "parent");
        auto bad = Add(*scene, nullptr, "bad");
        Require(bad.SetParent(parent).has_value(), "Failure hierarchy fixture failed");
        const auto children = std::as_const(parent).Children();
        auto duplicate = scene->DuplicateEntity(bad);
        Require(!duplicate && duplicate.error().code == SceneErrorCode::InvalidProgram
            && duplicate.error().operation == "_Scene::PushToRenderList"
            && duplicate.error().message == "Render-list entity requires a nonzero shader program",
            "Duplicate discarded publication diagnostic");
        Require(duplicate.error().entity != 0 && !scene->GetEntityByUUID(duplicate.error().entity)
            && scene->Reg().view<IDComponent>().size() == 2 && std::as_const(parent).Children() == children,
            "Publication failure retained duplicate or damaged source hierarchy");
        auto copied = _Scene::Copy(scene);
        Require(!copied && copied.error().code == SceneErrorCode::InvalidProgram
            && copied.error().entity == static_cast<std::uint64_t>(bad.GetUUID())
            && copied.error().operation == "_Scene::PushToRenderList", "Copy lost publication failure");
        Require(scene->Reg().view<IDComponent>().size() == 2 && bad.GetParent() == parent,
            "Partial copy retirement damaged its source");
        auto tail = Add(*scene, &program.shader, "tail");
        auto batch = scene->PushToRenderList(parent, bad, tail);
        Require(!batch && batch.error().code == SceneErrorCode::InvalidProgram
            && batch.error().entity == static_cast<std::uint64_t>(bad.GetUUID())
            && SceneDetail::BackendAccess::Groups(*scene).at(701) == std::vector<_Entity>{parent},
            "Failed publication batch did not preserve only its successful prefix");
        bad.GetComponent<RenderComponent>().Shader = &program.shader;
        SceneOperationChecked([&] { return scene->PushToRenderList(bad, tail); });
        Require(SceneDetail::BackendAccess::Groups(*scene).at(701) == std::vector<_Entity>({parent, bad, tail}),
            "Successful retry changed insertion order");
        std::cout << "[PASS] typed copy/duplicate publication rollback and batch short-circuit\n";
    }

    void InvalidInputs()
    {
        TestProgram program(91), uncreated(0);
        _Scene scene, foreign;
        auto nullShader = Add(scene, nullptr, "null");
        auto zeroShader = Add(scene, &uncreated.shader, "zero");
        auto foreignEntity = Add(foreign, &program.shader, "foreign");
        RequireRejected([&] { return scene.PushToRenderList(nullShader); });
        RequireRejected([&] { return scene.PushToRenderList(zeroShader); });
        RequireRejected([&] { return scene.PushToRenderList(foreignEntity); });
        RequireRejected([&] { return scene.PushToRenderList(_Entity{}); });
        auto stale = Add(scene, &program.shader, "stale");
        SceneOperationChecked([&] { return scene.DestroyEntity(stale); });
        RequireRejected([&] { return scene.PushToRenderList(stale); });
        Require(SceneDetail::BackendAccess::Groups(scene).empty() && SceneDetail::BackendAccess::Lights(scene).empty(), "Invalid input mutated groups");
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
        TypedPublicationFailures();
        std::cout << "Program grouping regression: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] Program grouping: " << error.what() << '\n';
        return 1;
    }
}
