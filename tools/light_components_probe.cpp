#include <concepts>
#include <functional>
#include <cstdlib>
#include <iostream>
#include "../GEngine/src/Assets/ShaderBackend.h"
// Exercise real scene dispatch and driver uniform storage through GEngine.lib.
#include "gepch.h"
#include "Core/GLDebug.h"
#include "Core/RenderSystem.h"
#include "Core/Scene.h"
#include "Light/LightEntity.h"
#include <array>
#include <stdexcept>

namespace
{
    using namespace GEngine;
    using namespace GEngine::Component;
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

    void Require(bool condition, const char* message)
    {
        ++checks;
        if (!condition) throw std::runtime_error(message);
    }

    constexpr std::array<const char*, 5> names{
        "dirDirection", "dirAmbient", "pointPosition", "pointAmbient", "spotDirection"};
    const std::array<Vec3f, 5> values{
        Vec3f(1, 2, 3), Vec3f(4, 5, 6), Vec3f(7, 8, 9), Vec3f(10, 11, 12), Vec3f(13, 14, 15)};
    const Vec3f sentinel(-17, -18, -19);

    void BuildShader(Asset::Shader& shader)
    {
        Require(shader.CompileShader(std::string(R"(#version 460 core
            void main() { gl_Position = vec4(0, 0, 0, 1); }
        )"), Asset::VERTEX, "phase11.vert").has_value(), "Fixture stage compilation failed");
        Require(shader.CompileShader(std::string(R"(#version 460 core
            uniform vec3 dirDirection, dirAmbient, pointPosition, pointAmbient, spotDirection;
            uniform vec3 legacyPosition[2], legacyColor[2], legacyAttenuation[2];
            out vec4 color;
            void main() {
                color = vec4(dirDirection + dirAmbient + pointPosition + pointAmbient + spotDirection
                    + legacyPosition[0] + legacyPosition[1] + legacyColor[0] + legacyColor[1]
                    + legacyAttenuation[0] + legacyAttenuation[1], 1);
            }
        )"), Asset::FRAGMENT, "phase11.frag").has_value(), "Fixture stage compilation failed");
        Require(shader.Link().has_value(), "Fixture shader link failed");
        Require(shader.IsLinked(), "Fixture shader did not link");
        shader.Bind();
    }

    void Expect(Asset::Shader& shader, const char* name, const Vec3f& expected)
    {
        const GLint location = glGetUniformLocation(::GEngine::Asset::ShaderBackendAccess::Program(shader), name);
        Require(location >= 0, "Fixture uniform was optimized out or misnamed");
        Vec3f actual{};
        glGetUniformfv(::GEngine::Asset::ShaderBackendAccess::Program(shader), location, &actual.x);
        if (actual != expected) {
            std::cerr << "Uniform mismatch: " << name << '\n';
            Require(false, "Wrong component data reached the driver");
        }
        Require(true, "Uniform matches");
    }

    void Reset(Asset::Shader& shader)
    {
        shader.Bind();
        for (const auto* name : names) shader.SetUniform(name, sentinel);
    }

    void ExpectMask(Asset::Shader& shader, int mask)
    {
        for (int i = 0; i < 5; ++i) {
            const int type = i < 2 ? 1 : (i < 4 ? 2 : 4);
            Expect(shader, names[i], (mask & type) ? values[i] : sentinel);
        }
    }

    void AddLights(_Entity entity, int mask)
    {
        if (mask & 1) {
            auto& c = entity.AddComponent<DirectionalLightComponent>();
            c.direction = {names[0], values[0]}; c.ambient = {names[1], values[1]};
        }
        if (mask & 2) {
            auto& c = entity.AddComponent<PointLightComponent>();
            c.position = {names[2], values[2]}; c.ambient = {names[3], values[3]};
        }
        if (mask & 4) entity.AddComponent<SpotLightComponent>().direction = {names[4], values[4]};
    }

    void Render(_Scene& scene, bool cascaded)
    {
        Camera::_EditorCamera camera;
        if (cascaded) RenderSystem::CascadedShadowSceneRender(&scene, camera, {}, 100.0f);
        else RenderSystem::SceneRender(&scene, camera);
    }

    void AddReceiver(_Scene& scene, Geometry& geometry, Asset::Shader& shader)
    {
        auto receiver = SceneOperationChecked([&] { return scene.CreateEntity("receiver"); });
        receiver.AddComponent<RenderComponent>().Shader = &shader;
        receiver.AddComponent<MeshComponent>(&geometry);
        SceneOperationChecked([&] { return scene.PushToRenderList(receiver); });
    }

    void SceneCases(Asset::Shader& shader, Geometry& geometry, bool cascaded)
    {
        // All presence masks cover individual types, absent siblings and priority
        // on a single multi-component entity; separate entities cover mixed scenes.
        for (bool separate : {false, true}) for (int mask = 0; mask < 8; ++mask) {
            _Scene scene;
            AddReceiver(scene, geometry, shader);
            for (int type = 1; type <= (separate ? 4 : 1); type *= 2) {
                const int components = separate ? (mask & type) : mask;
                if (components == 0) continue;
                auto light = SceneOperationChecked([&] { return scene.CreateEntity("light"); });
                light.AddComponent<RenderComponent>().Shader = &shader;
                AddLights(light, components);
                SceneOperationChecked([&] { return scene.PushToRenderList(light); });
            }
            Reset(shader);
            Render(scene, cascaded);
            const int selected = separate ? mask : ((mask & 1) ? 1 : ((mask & 2) ? 2 : (mask & 4)));
            ExpectMask(shader, selected);
        }

        for (int type : {1, 2, 4}) {
            _Scene scene;
            AddReceiver(scene, geometry, shader);
            auto light = SceneOperationChecked([&] { return scene.CreateEntity("removed/re-enabled light"); });
            light.AddComponent<RenderComponent>().Shader = &shader;
            AddLights(light, type);
            SceneOperationChecked([&] { return scene.PushToRenderList(light); });
            Reset(shader); Render(scene, cascaded); ExpectMask(shader, type);
            if (type == 1) light.RemoveComponent<DirectionalLightComponent>();
            if (type == 2) light.RemoveComponent<PointLightComponent>();
            if (type == 4) light.RemoveComponent<SpotLightComponent>();
            // Retained group membership with no light component must be harmless.
            Reset(shader); Render(scene, cascaded); ExpectMask(shader, 0);
            AddLights(light, type);
            Reset(shader); Render(scene, cascaded); ExpectMask(shader, type);
            SceneOperationChecked([&] { return scene.DestroyEntity(light); });
            Reset(shader); Render(scene, cascaded); ExpectMask(shader, 0);
        }

        // An existing component outside the published light list submits nothing.
        // There is no ECS Enabled flag in the current API.
        {
            _Scene scene;
            AddReceiver(scene, geometry, shader);
            auto light = SceneOperationChecked([&] { return scene.CreateEntity("unpublished"); });
            AddLights(light, 7);
            Reset(shader); Render(scene, cascaded); ExpectMask(shader, 0);
        }
        // Zero-intensity point/directional lights must overwrite the prior value.
        for (int type : {1, 2}) {
            _Scene scene;
            AddReceiver(scene, geometry, shader);
            auto light = SceneOperationChecked([&] { return scene.CreateEntity("zero intensity"); });
            light.AddComponent<RenderComponent>().Shader = &shader;
            AddLights(light, type);
            SceneOperationChecked([&] { return scene.PushToRenderList(light); });
            Render(scene, cascaded);
            if (type == 1) light.GetComponent<DirectionalLightComponent>().ambient.Data = Vec3f(0);
            else light.GetComponent<PointLightComponent>().ambient.Data = Vec3f(0);
            Render(scene, cascaded);
            Expect(shader, type == 1 ? names[1] : names[3], Vec3f(0));
        }
        std::cout << "[PASS] " << (cascaded ? "cascaded" : "scene")
            << " individual/mixed/priority/missing/removed/unpublished/zero-intensity\n";
    }

    struct BorrowedMaterial : Material
    {
        explicit BorrowedMaterial(Asset::Shader& shader) { m_Shader = &shader; }
    };
    struct LegacyCamera : CameraBase
    {
        void OnResize(int, int) override {}
        void OnResize(float) override {}
        void OnScroll(float) override {}
    };

    void LegacyCases(Asset::Shader& shader, Geometry& geometry)
    {
        // Default-constructed lights use stable indices 0/1 and survive both cycles.
        static LightEntity first, second;
        first.SetPos("legacyPosition")->SetColor("legacyColor", Vec3f(2, 3, 4))
            ->SetAttenuation("legacyAttenuation", Vec3f(1, 0, 0));
        second.SetPos("legacyPosition")->SetColor("legacyColor", Vec3f(5, 6, 7))
            ->SetAttenuation("legacyAttenuation", Vec3f(1, 2, 3));
        first.SetPosition(3, 4, 5); second.SetPosition(6, 7, 8);
        BorrowedMaterial material(shader);
        LegacyCamera camera;
        for (int count = 0; count <= 2; ++count) {
            Scene scene;
            scene.Push(new Group<Entity>(&geometry, &material)); // Scene owns the empty group.
            if (count >= 1) scene.Push(&first);
            if (count >= 2) scene.Push(&second);
            shader.Bind();
            shader.SetUniform("legacyColor[0]", sentinel);
            shader.SetUniform("legacyColor[1]", sentinel);
            Renderer::RenderScene(&scene, &camera);
            Expect(shader, "legacyColor[0]", count >= 1 ? Vec3f(2, 3, 4) : sentinel);
            Expect(shader, "legacyColor[1]", count >= 2 ? Vec3f(5, 6, 7) : sentinel);
            if (count >= 1) {
                Expect(shader, "legacyPosition[0]", Vec3f(3, 4, 5));
                Expect(shader, "legacyAttenuation[0]", Vec3f(1, 0, 0));
            }
            if (count >= 2) {
                Expect(shader, "legacyPosition[1]", Vec3f(6, 7, 8));
                Expect(shader, "legacyAttenuation[1]", Vec3f(1, 2, 3));
                first.SetColor("legacyColor", Vec3f(0));
                Renderer::RenderScene(&scene, &camera);
                Expect(shader, "legacyColor[0]", Vec3f(0));
                Expect(shader, "legacyColor[1]", Vec3f(5, 6, 7));
            }
        }
        std::cout << "[PASS] legacy empty/single/mixed/zero-intensity uniform upload\n";
    }

    void PointVisualization(Asset::Shader& shader, Geometry& geometry)
    {
        _Scene scene;
        for (int type : {1, 2, 4}) {
            auto light = SceneOperationChecked([&] { return scene.CreateEntity("visual light"); });
            light.AddComponent<RenderComponent>().Shader = &shader;
            light.AddComponent<MeshComponent>(&geometry);
            AddLights(light, type);
            SceneOperationChecked([&] { return scene.PushToRenderList(light); });
        }
        Camera::_EditorCamera camera;
        Reset(shader);
        RenderSystem::PointLightsVisualize(&scene, camera, &shader);
        ExpectMask(shader, 2);
        std::cout << "[PASS] point visualization excludes spot/directional components\n";
    }

    struct Diagnostics
    {
        SDL_threadID owner = SDL_ThreadID();
        unsigned errors = 0, markers = 0;
        static void APIENTRY Receive(GLenum source, GLenum type, GLuint id, GLenum severity,
            GLsizei length, const GLchar* message, const void* user) noexcept
        {
            auto& self = *static_cast<Diagnostics*>(const_cast<void*>(user));
            if (SDL_ThreadID() != self.owner) ++self.errors;
            if (source == GL_DEBUG_SOURCE_APPLICATION && type == GL_DEBUG_TYPE_MARKER && id == 110001) ++self.markers;
            else if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH || severity == GL_DEBUG_SEVERITY_MEDIUM) {
                ++self.errors;
                std::fprintf(stderr, "[GL] %.*s\n", length, message);
            }
        }
        Diagnostics()
        {
            Require(glDebugMessageCallback && glDebugMessageInsert, "GL diagnostics unavailable");
            glEnable(GL_DEBUG_OUTPUT); glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(Receive, this);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 110001,
                GL_DEBUG_SEVERITY_NOTIFICATION, -1, "Phase 11 diagnostic health");
        }
        ~Diagnostics() { glDebugMessageCallback(nullptr, nullptr); }
        void Verify() { Require(errors == 0 && markers == 1 && glGetError() == GL_NO_ERROR, "GL error/thread/diagnostic failure"); }
    };
}

int main()
{
    try {
        SDL_SetMainReady();
        struct Video { ~Video() { SDL_Quit(); } } video;
        Require(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
        Require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) == 0
            && SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6) == 0
            && SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) == 0,
            "Cannot configure GL 4.6 core");
        GEngine::GLDebug::ConfigureContext();
        for (int cycle = 0; cycle < 2; ++cycle) {
            {
                std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                    SDL_CreateWindow("Light component validation", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN),
                    &SDL_DestroyWindow);
                Require(window != nullptr, SDL_GetError());
                std::unique_ptr<void, decltype(&SDL_GL_DeleteContext)> context(
                    GEngine::GLDebug::CreateContext(window.get()), &SDL_GL_DeleteContext);
                Require(context != nullptr, SDL_GetError());
                Require(gladLoadGLLoader(SDL_GL_GetProcAddress) != 0, "GL loading failed");
                std::cout << "[GL] cycle=" << cycle << " version=" << glGetString(GL_VERSION)
                    << " renderer=" << glGetString(GL_RENDERER) << '\n';
                Diagnostics diagnostics;
                {
                    Asset::Shader shader;
                    BuildShader(shader);
                    Geometry geometry; // A real empty VAO is enough to exercise upload dispatch.
                    SceneCases(shader, geometry, false);
                    SceneCases(shader, geometry, true);
                    PointVisualization(shader, geometry);
                    LegacyCases(shader, geometry);
                } // All GL owners retire on this thread, before context teardown.
                diagnostics.Verify();
                GEngine::RenderCounters::ForgetContext(context.get());
            }
            Require(SDL_GL_GetCurrentContext() == nullptr, "Context teardown failed");
        }
        std::cout << "[PASS] light-components cycles=2 checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
