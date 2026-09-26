#include <concepts>
#include <functional>
#include <iostream>
#include "Scene/RenderEcs.h"
#include <cstdlib>
#include <print>
#include <string_view>
#include <type_traits>

#ifdef RENDER_ECS_SCENE_PROBE
#include "Scene/_Entity.h"
#include "Core/GEngine.h"
#include "Core/BaseApp.h"
#include "Core/RenderTarget.h"
#include "Core/RuntimeAssets.h"
#else
#if defined(GL_VERSION_1_0) || defined(GLAD_GL_H_) || defined(SDL_MAJOR_VERSION)
#error Native backend declarations leaked into render ECS
#endif
#endif

namespace
{
    using namespace ::GEngine;
    using namespace ::GEngine::Component;
    int checks = 0;
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

    template<class T> void Check(const T& value, const char* message)
    {
        ++checks;
        if (!static_cast<bool>(value)) { std::println(stderr, "[FAIL] {}", message); std::exit(1); }
    }
    template<class T> void Reject(const T& value, RenderEcsError error)
    { Check(!value && value.error() == error, "Wrong typed error"); }

    void ComponentsAndBoundary()
    {
        static_assert(std::is_trivially_copyable_v<MeshRendererComponent>);
        static_assert(std::is_trivially_copyable_v<RenderCameraComponent>);
        static_assert(std::is_trivially_copyable_v<RenderLightComponent>);
        static_assert(std::is_trivially_copyable_v<VisibilityComponent>);
        static_assert(!std::is_convertible_v<Asset::MeshHandle, EntityRenderId>);
        static_assert(!RenderDataComponent<int>);
        static_assert(!std::is_copy_constructible_v<RenderEcs::ExtractionScope>);
        entt::registry registry;
        RenderEcs ecs(registry);
        Reject(ecs.Identify(entt::null), RenderEcsError::InvalidEntity);
        auto entity = registry.create();
        auto id = ecs.Identify(entity).value();
        Check(ecs.Identify(entity) == id && ecs.Resolve(id) == entity, "Identity must remain stable");
        Reject(ecs.Resolve({}), RenderEcsError::StaleId);
        Reject(ecs.Get<MeshRendererComponent>(id), RenderEcsError::MissingComponent);
        MeshRendererComponent renderer{{7, 8, 9}, {10, 11, 12}, 3, false, true, false};
        Check(ecs.Add(id, renderer), "Add mesh renderer");
        Reject(ecs.Add(id, renderer), RenderEcsError::DuplicateComponent);
        Check(ecs.Add<VisibilityComponent>(id), "Add visibility");
        Check(ecs.Add<RenderCameraComponent>(id), "Add camera intent");
        Check(ecs.Add<RenderLightComponent>(id), "Add light intent");
        auto value = ecs.Get<MeshRendererComponent>(id).value();
        Check(value.mesh == renderer.mesh && value.material == renderer.material && value.submesh == 3
            && !value.castShadows && !value.pickable, "Mesh/material/flags survive storage");
        value.mesh = {30, 31, 32}; value.material = {40, 41, 42};
        Check(ecs.Get<MeshRendererComponent>(id)->mesh == renderer.mesh, "Get must not escape mutable storage");
        Check(ecs.Replace(id, value), "Replace mesh/material");
        Check(ecs.Get<MeshRendererComponent>(id)->mesh == value.mesh
            && ecs.Get<MeshRendererComponent>(id)->material == value.material, "Replacement retained");
        Check(ecs.Resolve(id) == entity, "Resource changes must not change entity identity");
        Reject(ecs.Visit<MeshRendererComponent>([](EntityRenderId, MeshRendererComponent) {}), RenderEcsError::ExtractionRequired);
        const auto unregistered = registry.create();
        {
            auto scope = ecs.BeginExtraction(); Check(scope, "Begin serial extraction");
            auto moved = std::move(*scope);
            Reject(ecs.BeginExtraction(), RenderEcsError::ExtractionActive);
            Reject(ecs.Add(id, value), RenderEcsError::ExtractionActive);
            Reject(ecs.Replace(id, renderer), RenderEcsError::ExtractionActive);
            Reject(ecs.Remove<MeshRendererComponent>(id), RenderEcsError::ExtractionActive);
            Reject(ecs.Identify(unregistered), RenderEcsError::ExtractionActive);
            Check(ecs.Identify(entity) == id, "Existing ID lookup is read-only");
            int visited = 0;
            Check(ecs.Visit<MeshRendererComponent>([&](EntityRenderId renderId, MeshRendererComponent data) {
                ++visited;
                Check(renderId == id && data.mesh == value.mesh, "Serial reads see replacement made before boundary");
                Reject(ecs.Remove<MeshRendererComponent>(id), RenderEcsError::ExtractionActive);
            }), "Visit under serial boundary");
            Check(visited == 1, "Visit exactly the live migrated renderer");
            Check(ecs.Get<MeshRendererComponent>(id)->mesh == value.mesh, "Rejected mutation leaves data intact");
        }
        Check(ecs.Replace(id, renderer), "Mutation after boundary");
        Check(ecs.Remove<MeshRendererComponent>(id), "Remove renderer");
        Reject(ecs.Remove<MeshRendererComponent>(id), RenderEcsError::MissingComponent);
        Reject(ecs.Replace(id, value), RenderEcsError::MissingComponent);
        Check(ecs.Add(id, value), "Re-add renderer");
        Check(ecs.Resolve(id) == entity, "Component remove/re-add preserves identity");
        Check(ecs.Remove<RenderCameraComponent>(id) && ecs.Remove<RenderLightComponent>(id)
            && ecs.Remove<VisibilityComponent>(id), "Remove intent components");
    }

    void PickingAndReuse()
    {
        entt::registry registry, foreignRegistry;
        RenderEcs ecs(registry), foreign(foreignRegistry);
        auto entity = registry.create();
        const auto firstEntity = entity;
        auto first = ecs.Identify(entity).value();
        EntityPickTable oldFrame(2);
        Check(oldFrame.Encode(first) == 0 && oldFrame.Encode(first) == 0, "Deterministic repeated pick mapping");
        Check(ecs.ResolvePick(oldFrame, 0) == first, "Valid picked entity");
        Reject(ecs.ResolvePick(oldFrame, -1), RenderEcsError::InvalidPick);
        Reject(ecs.ResolvePick(oldFrame, INT32_MIN), RenderEcsError::InvalidPick);
        Reject(ecs.ResolvePick(oldFrame, INT32_MAX), RenderEcsError::InvalidPick);
        Reject(oldFrame.Encode({}), RenderEcsError::StaleId);
        auto foreignId = foreign.Identify(foreignRegistry.create()).value();
        Reject(foreign.Resolve(first), RenderEcsError::ForeignId);
        Reject(foreign.ResolvePick(oldFrame, 0), RenderEcsError::ForeignId);
        Check(oldFrame.Encode(foreignId) == 1, "Second pick slot");
        auto extra = ecs.Identify(registry.create()).value();
        Reject(oldFrame.Encode(extra), RenderEcsError::PickCapacity);
        Check(oldFrame.Encode(first) == 0, "Existing entry works at capacity");
        auto wrongGeneration = first; ++wrongGeneration.generation;
        Reject(ecs.Resolve(wrongGeneration), RenderEcsError::StaleId);
        auto wrongSlot = first; wrongSlot.index = UINT32_MAX - 1;
        Reject(ecs.Resolve(wrongSlot), RenderEcsError::StaleId);
        bool enttWrapped = false;
        for (int i = 0; i < 5000; ++i)
        {
            registry.destroy(entity);
            Reject(ecs.Resolve(first), RenderEcsError::StaleId);
            entity = registry.create();
            enttWrapped = enttWrapped || entity == firstEntity;
            auto next = ecs.Identify(entity).value();
            Check(next.index == first.index && next.generation != first.generation, "Reused slot has new generation");
            Reject(ecs.ResolvePick(oldFrame, 0), RenderEcsError::StaleId);
        }
        Check(enttWrapped, "Exercise actual EnTT generation wrap");
        Check(ecs.Add<MeshRendererComponent>(ecs.Identify(entity).value()), "Reused entity accepts renderer");
        registry.clear();
        Reject(ecs.ResolvePick(oldFrame, 0), RenderEcsError::StaleId);
    }

#ifdef RENDER_ECS_SCENE_PROBE
    void SceneIntegration()
    {
        auto scene = CreateRefPtr<_Scene>();
        auto entity = SceneOperationChecked([&] { return scene->CreateEntity("render intent"); });
        auto& ecs = scene->RenderData();
        auto id = ecs.Identify(entity).value();
        MeshRendererComponent component{{2, 3, 4}, {5, 6, 7}};
        Check(ecs.Add(id, component), "Scene-owned facade add");
        Check(entity.HasAllComponents<MeshRendererComponent>(), "Facade uses the actual scene registry");
        entity.AddComponent<RenderCameraComponent>();
        entity.AddComponent<RenderLightComponent>();
        entity.AddComponent<VisibilityComponent>();
        auto duplicate = SceneOperationChecked([&] { return scene->DuplicateEntity(entity); });
        auto copied = SceneOperationChecked([&] { return _Scene::Copy(scene); });
        {
            auto boundary = ecs.BeginExtraction(); Check(boundary, "Scene boundary");
            int count = 0;
            Check(ecs.Visit<MeshRendererComponent>([&](EntityRenderId found, MeshRendererComponent mesh) {
                ++count; Check(mesh.mesh == component.mesh, "Duplicate retains authoring handles");
                if (ecs.Resolve(found).value() == static_cast<entt::entity>(duplicate))
                    Check(found != id, "Duplicate has fresh runtime identity");
            }), "Visit original and duplicate");
            Check(count == 2, "Copied components are registered before extraction");
        }
        {
            auto boundary = copied->RenderData().BeginExtraction(); Check(boundary, "Copied scene boundary");
            int count = 0;
            Check(copied->RenderData().Visit<RenderCameraComponent>([&](EntityRenderId found, RenderCameraComponent) {
                ++count; Check(found.registry != id.registry, "Scene copy has new identity domain");
            }), "Copied cameras");
            Check(count == 2, "Camera components copy");
        }
        EntityPickTable table; Check(table.Encode(id), "Picking original");
        SceneOperationChecked([&] { return scene->DestroyEntity(entity); });
        auto replacement = SceneOperationChecked([&] { return scene->CreateEntity("replacement"); });
        Check(ecs.Identify(replacement) != id, "Scene destroy/reuse advances render identity");
        Reject(ecs.ResolvePick(table, 0), RenderEcsError::StaleId);
    }

    void PickingAttachment()
    {
        RuntimeAssets::Initialize("GEngineEditor");
        EngineContext root;
        WindowProperties p; p.m_Title = "Render ECS picking validation";
        p.m_Width = p.m_Height = 64; p.m_MinWidth = p.m_MinHeight = 32;
        p.m_IsVsync = false; p.flag = BitFlags<WindowFlags, uint8_t>{WindowFlags::INVISIBLE};
        auto initialized = root.Initialize({p});
        if (!initialized) std::visit([](const auto& error) { ReportApplicationError(ApplicationInitializationError{error}); }, initialized.error());
        Check(initialized, "Initialize picking context");
        {
            _Scene scene;
            auto entity = SceneOperationChecked([&] { return scene.CreateEntity(); });
            auto& ecs = scene.RenderData();
            auto id = ecs.Identify(entity).value();
            EntityPickTable table;
            auto pixel = table.Encode(id).value();
            auto target = MousePickFrameBuffer::Create(4, 4);
            Check(target, "Actual mouse-pick target creation");
            Check(target->Buffer().ClearInteger(0, pixel), "Write mapped signed pick token");
            auto read = target->ReadPixel(1, 1); Check(read, "Read actual R32I attachment");
            Check(*read == pixel && ecs.ResolvePick(table, *read) == id, "Attachment roundtrip and identity resolution");
            SceneOperationChecked([&] { return scene.DestroyEntity(entity); });
            auto reuse = SceneOperationChecked([&] { return scene.CreateEntity(); }); Check(ecs.Identify(reuse), "Reused scene identity");
            Reject(ecs.ResolvePick(table, *read), RenderEcsError::StaleId);
            Check(target->Buffer().ClearInteger(0, EntityPickTable::InvalidPixel), "Clear no-hit sentinel");
            Check(target->ReadPixel(1, 1) == EntityPickTable::InvalidPixel, "Signed -1 survives attachment");
            Reject(ecs.ResolvePick(table, target->ReadPixel(1, 1).value()), RenderEcsError::InvalidPick);
        }
    }
#endif
}

int main(int argc, char** argv)
{
#ifdef RENDER_ECS_SCENE_PROBE
    if (argc > 1 && std::string_view(argv[1]) == "--picking")
    {
        std::set_terminate([] { std::println(stderr, "[FAIL] unexpected picking termination"); std::_Exit(1); });
        PickingAttachment(); std::println("[PASS] render-ecs picking checks={}", checks); return 0;
    }
#endif
    if (argc > 1)
    {
        std::set_terminate([] { std::println(stderr, "[EXPECTED] render ECS invariant"); std::_Exit(86); });
        const std::string_view mode = argv[1];
        entt::registry registry; RenderEcs ecs(registry);
        const auto id = ecs.Identify(registry.create()).value();
        if (mode == "--wrong-thread")
        {
            std::thread worker([&] {
                std::set_terminate([] { std::println(stderr, "[EXPECTED] render ECS invariant"); std::_Exit(86); });
                (void)ecs.Resolve(id);
            });
            worker.join();
        }
        else if (mode == "--legacy-mutation") { auto scope = ecs.BeginExtraction(); ecs.RequireMutable(); }
        else if (mode == "--duplicate-facade") { RenderEcs duplicate(registry); }
#ifdef RENDER_ECS_SCENE_PROBE
        else if (mode == "--scene-create") { _Scene scene; auto scope = scene.RenderData().BeginExtraction(); SceneOperationChecked([&] { return scene.CreateEntity(); }); }
        else if (mode == "--scene-destroy") { _Scene scene; auto entity = SceneOperationChecked([&] { return scene.CreateEntity(); }); auto scope = scene.RenderData().BeginExtraction(); SceneOperationChecked([&] { return scene.DestroyEntity(entity); }); }
        else if (mode == "--entity-add") { _Scene scene; auto entity = SceneOperationChecked([&] { return scene.CreateEntity(); }); auto scope = scene.RenderData().BeginExtraction(); entity.AddComponent<VisibilityComponent>(); }
#endif
        return 87;
    }
    ComponentsAndBoundary(); PickingAndReuse();
#ifdef RENDER_ECS_SCENE_PROBE
    SceneIntegration();
#endif
    std::println("[PASS] render-ecs checks={}", checks);
}
