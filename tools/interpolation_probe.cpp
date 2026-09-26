#include <concepts>
#include <functional>
#include <cstdlib>
#ifdef RETAINED_SUBMISSION_SCHEMA_ONLY
#include "Core/RenderSystem.h"
#include <type_traits>
static_assert(std::is_same_v<decltype(std::declval<::GEngine::_Scene&>().GetRenderTransform(std::declval<const ::GEngine::_Entity&>())),
    std::expected<::GEngine::_Scene::RenderTransform, ::GEngine::RenderTransformQueryError>>);
static_assert(std::is_same_v<decltype(::GEngine::RenderSystem::SceneRender(nullptr, std::declval<::GEngine::Camera::_EditorCamera&>())),
    ::GEngine::RenderSystemResult>);
#else
#ifdef RETAINED_SUBMISSION_INTERNAL_PROBE
// Compile the unchanged implementation directly only for exact private range-helper coverage.
#include "../GEngine/include/GEngine/Core/RenderSystem.cpp"
#endif
#include "../GEngine/src/Assets/ShaderBackend.h"
#include "../GEngine/src/Scene/SceneBackend.h"
// Production scene/renderer interpolation checks; run through test_interpolation.py.
#include "gepch.h"
#include "Core/BaseApp.h"
#include "Core/Renderer.h"
#include "Core/Scene.h"
#include "Core/GLDebug.h"
#include "Core/RenderSystem.h"
#include "Core/RuntimeAssets.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/ShapeSphere.h"
#include "Windows/SDLWindow.h"
#include <array>
#include <atomic>
#include <stdexcept>
#include <thread>

namespace
{
    using namespace GEngine;
    using namespace ::GEngine::Component;
    int checks = 0;
    constexpr double dt = _Scene::PhysicsStepSeconds;
    void Check(bool ok, const char* message)
    {
        ++checks;
        if (!ok) throw std::runtime_error(message);
    }
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

    // Test-only bridge while bounded callers precede the Scene typed-return migration.
    template<std::invocable Start>
    void StartRuntimeChecked(Start&& start)
    {
        if constexpr (std::is_void_v<std::invoke_result_t<Start>>)
            std::invoke(std::forward<Start>(start));
        else
        {
            auto result=std::invoke(std::forward<Start>(start));
            if(!result)
            {
                std::cerr << "[FAIL] Valid fixture runtime startup: operation=" << result.error().operation
                    << " code=" << static_cast<unsigned>(result.error().code)
                    << " entity=" << result.error().entity << " radius=" << result.error().radius
                    << " points=" << result.error().pointCount << ": " << result.error().message << '\n';
                std::exit(1);
            }
        }
    }
    template<class T, class E> T Take(std::expected<T,E> value)
    { Check(value.has_value(), "Expected typed query/submission success"); return std::move(*value); }
    void Submitted(::GEngine::RenderSystemResult result)
    { Check(result.has_value(), "Expected retained submission success"); }
    void QueryRejected(const ::GEngine::RenderTransformQueryError& error)
    {
        Check(error.code==::GEngine::TransformErrorCode::InvalidEntity
            && error.operation=="_Scene::GetRenderTransform"
            && error.message=="Render transform requires a live entity in this scene", "Typed query lost diagnostics");
    }
    void SubmissionRejected(const ::GEngine::RenderSystemResult& result)
    {
        Check(!result && std::holds_alternative<::GEngine::RenderTransformQueryError>(result.error()), "Submission lost typed query failure");
        QueryRejected(std::get<::GEngine::RenderTransformQueryError>(result.error()));
    }
    bool Near(const Mat4& a, const Mat4& b, float tolerance = 2e-5f)
    {
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > tolerance) return false;
        return true;
    }
    struct MovingScene
    {
        // Physics retains borrowed shapes; retire the scene before the fixture shapes.
        std::vector<std::unique_ptr<PhysicalShape>> shapes;
        _Scene scene;
        _Entity entity;
        explicit MovingScene(bool dynamic = false)
        {
            if (dynamic)
            {
                auto ground = SceneOperationChecked([&] { return scene.CreateEntity("static ground sphere"); });
                ground.Transform().Translation = Vec3f(0, -10, 0);
                ground.AddComponent<RigidBody3DComponent>().Type = BodyType::Static;
                ground.AddComponent<SphereFixture3DComponent>().Radius = 10;
            }
            entity = SceneOperationChecked([&] { return scene.CreateEntity("interpolated body"); });
            entity.AddComponent<RigidBody3DComponent>().Type = dynamic ? BodyType::Dynamic : BodyType::Kinematic;
            if (dynamic) entity.Transform().Translation.y = 2;
            entity.AddComponent<SphereFixture3DComponent>().Radius = .1f;
            Start();
        }
        void Start()
        {
            StartRuntimeChecked([&] { return scene.OnRuntimeStart(); });
            for (auto* body : scene.GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies())
                shapes.emplace_back(body->m_Shape);
            Body()->m_LinearVelocity = Vec3f(6, 0, 0);
        }
        RigidBody3D* Body() { return entity.GetComponent<RigidBody3DComponent>().RuntimeBody; }
        _Scene::RenderTransform Sample() { return Take(scene.GetRenderTransform(entity)); }
    };

    void EndpointsAndDiscontinuities()
    {
        MovingScene f;
        Check(Near(f.Sample().matrix, f.entity.Transform().GetTransform()), "New body must snap to its initial pose");
        f.scene.Update(Timestep(dt));
        auto zero = f.Sample();
        Check(f.scene.GetRenderInterpolationAlpha() == 0 && zero.matrix[3].x == 0
            && std::abs(f.Body()->m_Position.x - .1f) < 1e-6f, "Alpha zero must display the previous fixed pose");
        const auto authoritative = f.entity.Transform().GetTransform();
        f.scene.Update(Timestep(dt * .5));
        auto half = f.Sample();
        Check(std::abs(f.scene.GetRenderInterpolationAlpha() - .5) < 1e-12
            && std::abs(half.matrix[3].x - .05f) < 1e-6f, "Half-tick presentation did not move between fixed poses");
        Check(half.simulationRevision == zero.simulationRevision && half.revision != zero.revision
            && f.Sample().revision == half.revision && Near(f.entity.Transform().GetTransform(), authoritative),
            "Presentation revision or sampling changed authoritative state");
        f.scene.Update(Timestep(dt * .499));
        Check(std::abs(f.Sample().matrix[3].x - .0999f) < 1e-6f, "Near-one alpha is wrong");
        f.entity.Transform().SetTranslation(Vec3f(10, 3, 0));
        Check(f.Sample().matrix[3].x == 10 && f.Body()->m_Position.x < 1,
            "Sampling a pending authored teleport wrote into physics or smeared it");
        const auto ticks = f.scene.GetPhysicsTiming().totalSteps;
        f.scene.Update(Timestep(0.0));
        Check(f.Sample().matrix[3].x == 10 && f.Body()->m_Position.x == 10
            && f.scene.GetPhysicsTiming().totalSteps == ticks, "Teleport synchronization created a tick or stale history");
        Check(f.scene.GetPhysicsSystem()->SetBodyPose(f.Body(), Vec3f(20, 0, 0), Quat(1, 0, 0, 0)), "Body teleport rejected");
        Check(f.Sample().matrix[3].x == 20, "Physics-side teleport was interpolated from stale state");
        f.scene.Update(Timestep(0.0));
        Check(f.Sample().matrix[3].x == 20 && f.entity.Transform().Translation.x == 20, "Body teleport did not reset history");

        MovingScene rotation;
        rotation.Body()->m_LinearVelocity = Vec3f(0);
        rotation.entity.Transform().SetRotation(glm::angleAxis(glm::radians(170.f), Vec3f(0, 0, 1)));
        rotation.scene.Update(Timestep(0.0));
        rotation.Body()->m_AngularVelocity = Vec3f(0, 0, glm::radians(1200.f));
        rotation.scene.Update(Timestep(1.5 * dt));
        const auto rotated = rotation.Sample().matrix;
        Check(std::abs(rotated[0][0] + 1.f) < 2e-5f && std::abs(rotated[0][1]) < 2e-5f,
            "Rotation took the long arc across the 180-degree boundary");
        rotation.entity.Transform().Scale = Vec3f(-2, 3, .5f);
        rotation.scene.Update(Timestep(0.0));
        Check(Near(rotation.Sample().matrix, rotation.entity.Transform().GetTransform()),
            "Non-uniform/negative scale edit must snap without decomposition");
        rotation.scene.Update(Timestep(dt));
        Check(!Near(rotation.Sample().matrix, rotation.entity.Transform().GetTransform()),
            "Reparent fixture needs distinct interpolated and current poses");
        auto parent = SceneOperationChecked([&] { return rotation.scene.CreateEntity("organizational parent"); });
        parent.Transform().SetTranslation(Vec3f(100, 200, 300));
        parent.Transform().Scale = Vec3f(2, 4, 7);
        parent.Transform().SetRotation(Vec3f(.4f, .6f, .8f));
        Check(rotation.entity.SetParent(parent).has_value(), "Reparent was rejected");
        Check(Near(rotation.Sample().matrix, rotation.entity.Transform().GetTransform()),
            "Reparenting composed parent scale/rotation into existing world-space physics");
        Check(rotation.entity.SetParent({}).has_value(), "Detach was rejected");
        Check(Near(rotation.Sample().matrix, rotation.entity.Transform().GetTransform()), "Detach retained old interpolation history");

        MovingScene paused;
        paused.scene.Update(Timestep(1.5 * dt));
        paused.scene.SetPaused(true);
        const auto snapped = paused.Sample();
        const auto pending = paused.scene.GetPhysicsTiming().pendingSeconds;
        paused.scene.Update(Timestep(10));
        paused.scene.SetPaused(false);
        Check(Near(paused.Sample().matrix, snapped.matrix) && paused.scene.GetPhysicsTiming().pendingSeconds == pending,
            "Pause/resume without a tick rewound presentation or accumulated paused time");
        std::cout << "[PASS] endpoints, revisions, shortest-arc rotation, teleports, scale, parenting and pause\n";
    }

    void LifetimeAndBacklog()
    {
        MovingScene f;
        f.scene.Update(Timestep(2 * dt));
        Check(std::abs(f.Sample().matrix[3].x - .1f) < 1e-6f,
            "Two-tick catch-up saved the previous rendered frame instead of the penultimate physics tick");
        f.scene.Update(Timestep(1));
        Check(f.scene.GetRenderInterpolationAlpha() == 1 && f.scene.GetPhysicsTiming().stepsLastUpdate == 2
            && f.scene.GetPhysicsTiming().discardedSeconds >= .75
            && Near(f.Sample().matrix, f.entity.Transform().GetTransform()), "Backlog clamp extrapolated or wrapped alpha");
        for (int i = 0; i < 20; ++i)
        {
            f.scene.Update(Timestep(dt / 4));
            Check(f.scene.GetRenderInterpolationAlpha() >= 0 && f.scene.GetRenderInterpolationAlpha() <= 1
                && f.Sample().matrix[3].x <= f.Body()->m_Position.x + 1e-6f, "Backlog drainage extrapolated");
        }
        const auto stale = f.entity;
        const auto revision = f.Sample().revision;
        SceneOperationChecked([&] { return f.scene.DestroyEntity(stale); });
        const auto staleResult=f.scene.GetRenderTransform(stale);
        Check(!staleResult && !stale, "Destroyed entity retained a presentation sample");
        QueryRejected(staleResult.error());
        auto replacement = SceneOperationChecked([&] { return f.scene.CreateEntity("replacement"); });
        replacement.Transform().Translation = Vec3f(33, 0, 0);
        const auto sample = Take(f.scene.GetRenderTransform(replacement));
        Check(sample.matrix[3].x == 33 && sample.revision > revision, "Reused entity slot inherited stale pose/revision");
        _Scene other;
        const auto foreignResult=other.GetRenderTransform(replacement);
        Check(!foreignResult, "Cross-scene presentation query was accepted");
        QueryRejected(foreignResult.error());
        const auto invalidResult=other.GetRenderTransform(_Entity{});
        Check(!invalidResult, "Invalid presentation query was accepted"); QueryRejected(invalidResult.error());
        auto missing=SceneOperationChecked([&] { return other.CreateEntity("missing transform"); }); missing.RemoveComponent<Transform3DComponent>();
        const auto missingResult=other.GetRenderTransform(missing);
        Check(!missingResult, "Missing transform query was accepted"); QueryRejected(missingResult.error());
        Check(Take(f.scene.GetRenderTransform(replacement)).revision==sample.revision,
            "Rejected queries changed the valid presentation revision");
        std::cout << "[PASS] typed presentation invalid/stale/foreign/missing-transform diagnostics/revision\n";
        MovingScene restarted;
        restarted.scene.Update(Timestep(1.5 * dt));
        restarted.scene.OnRuntimeStop();
        Check(Near(restarted.Sample().matrix, restarted.entity.Transform().GetTransform()), "Stopped scene retained interpolation");
        restarted.Start();
        Check(restarted.scene.GetPhysicsTiming().totalSteps == 0
            && Near(restarted.Sample().matrix, restarted.entity.Transform().GetTransform()), "Restart reused old endpoints");
        auto duplicate = SceneOperationChecked([&] { return restarted.scene.DuplicateEntity(restarted.entity); });
        Check(Near(Take(restarted.scene.GetRenderTransform(duplicate)).matrix, duplicate.Transform().GetTransform()),
            "Duplicate copied runtime presentation history");
        restarted.scene.Update(Timestep(1.5 * dt));
        auto borrowed = RefPtr<_Scene>(&restarted.scene, [](_Scene*) {});
        auto copied = SceneOperationChecked([&] { return _Scene::Copy(borrowed); });
        auto copiedEntity = copied->GetEntityByUUID(restarted.entity.GetUUID());
        Check(Near(Take(copied->GetRenderTransform(copiedEntity)).matrix, copiedEntity.Transform().GetTransform())
            && copied->GetPhysicsTiming().totalSteps == 0, "Scene copy inherited simulation/interpolation history");
        std::cout << "[PASS] catch-up endpoints, clamp/drain, destruction/reuse, cross-scene and restart\n";
    }

    void RefreshAndDeterminism()
    {
        for (int hz : {30, 60, 75, 120, 144, 240})
        {
            MovingScene enabled, disabled;
            disabled.scene.SetRenderInterpolationEnabled(false);
            float previousX = 0;
            unsigned smoothFrames = 0, ticklessMoves = 0;
            auto previousTicks = enabled.scene.GetPhysicsTiming().totalSteps;
            for (int frame = 0; frame < 2 * hz; ++frame)
            {
                enabled.scene.Update(Timestep(1.0 / hz)); disabled.scene.Update(Timestep(1.0 / hz));
                const auto sample = enabled.Sample(); const auto raw = disabled.Sample();
                auto* a = enabled.Body(); auto* b = disabled.Body();
                Check(a->m_Position == b->m_Position && a->m_Orientation == b->m_Orientation
                    && a->m_LinearVelocity == b->m_LinearVelocity && a->m_AngularVelocity == b->m_AngularVelocity
                    && enabled.scene.GetPhysicsTiming().totalSteps == disabled.scene.GetPhysicsTiming().totalSteps,
                    "Presentation changed deterministic body state or fixed-update count");
                Check(Near(raw.matrix, disabled.entity.Transform().GetTransform()), "Disabled interpolation did not show current state");
                if (frame > hz / 10)
                {
                    Check(std::abs(sample.matrix[3].x - previousX - 6.f / hz) < 3e-5f,
                        "Constant-speed presentation visibly stepped at fixed cadence");
                    ++smoothFrames;
                    if (sample.simulationRevision == previousTicks && sample.matrix[3].x > previousX) ++ticklessMoves;
                }
                previousX = sample.matrix[3].x; previousTicks = sample.simulationRevision;
            }
            Check(enabled.scene.GetPhysicsTiming().totalSteps == 120, "Refresh sweep changed fixed rate");
            if (hz > 60) Check(ticklessMoves > 0, "High-refresh rendering did not move on zero-tick frames");
            std::cout << "[PASS] refresh-hz=" << hz << " fixed-ticks=120 smooth-frames=" << smoothFrames
                << " tickless-moves=" << ticklessMoves << " exact-physics=1\n";
        }
        MovingScene enabled(true), disabled(true);
        enabled.Body()->m_LinearVelocity = disabled.Body()->m_LinearVelocity = Vec3f(0);
        disabled.scene.SetRenderInterpolationEnabled(false);
        for (int frame = 0; frame < 720; ++frame)
        {
            enabled.scene.Update(Timestep(dt / 4)); disabled.scene.Update(Timestep(dt / 4));
            (void)enabled.Sample(); (void)disabled.Sample();
            auto* a = enabled.Body(); auto* b = disabled.Body();
            Check(a->m_Position == b->m_Position && a->m_Orientation == b->m_Orientation
                && a->m_LinearVelocity == b->m_LinearVelocity && a->m_AngularVelocity == b->m_AngularVelocity
                && a->IsSleeping() == b->IsSleeping(), "Interpolation changed dynamic collision/sleep results");
        }
        Check(enabled.scene.GetPhysicsTiming().totalSteps == 180 && enabled.Body()->m_Position.y > -1,
            "Dynamic contact fixture did not simulate its supported collision workload");
        std::cout << "[PASS] dynamic contact/sleep comparison: 720 render samples, 180 fixed ticks, exact state\n";
    }

    void BuildShader(Asset::Shader& shader)
    {
        Check(shader.CompileShader(std::string(R"(#version 460 core
            layout(location=0) in vec3 position;
            uniform mat4 u_model;
            void main() { gl_Position = u_model * vec4(position, 1); }
        )"), Asset::VERTEX, "interpolation.vert").has_value(), "Fixture stage compilation failed");
        Check(shader.CompileShader(std::string(R"(#version 460 core
            out vec4 color;
            void main() { color = vec4(1); }
        )"), Asset::FRAGMENT, "interpolation.frag").has_value(), "Fixture stage compilation failed");
        Check(shader.Link().has_value(), "Fixture shader link failed"); Check(shader.IsLinked(), "Presentation shader failed to link");
    }
    void UniformMatches(Asset::Shader& shader, const Mat4& expected)
    {
        Mat4 actual(0);
        const auto location = glGetUniformLocation(::GEngine::Asset::ShaderBackendAccess::Program(shader), "u_model");
        Check(location >= 0, "Model uniform missing");
        glGetUniformfv(::GEngine::Asset::ShaderBackendAccess::Program(shader), location, glm::value_ptr(actual));
        Check(Near(actual, expected), "Render pass submitted a different presentation matrix");
    }
    double PixelCenter()
    {
        std::array<unsigned char, 128 * 128 * 4> pixels{};
        glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        double sum = 0; int count = 0;
        for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x)
            if (pixels[(y * 128 + x) * 4] > 128) { sum += x; ++count; }
        Check(count > 5, "Triangle produced no measurable pixels");
        return sum / count;
    }
    void GLChecks()
    {
        SDL_SetMainReady();
        Check(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
        struct Video { ~Video() { SDL_Quit(); } } video;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        GLDebug::ConfigureContext();
        for (int cycle = 0; cycle < 2; ++cycle)
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Interpolation pixels", 0, 0, 128, 128, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
            Check(window != nullptr, SDL_GetError());
            std::unique_ptr<void, decltype(&SDL_GL_DeleteContext)> context(GLDebug::CreateContext(window.get()), SDL_GL_DeleteContext);
            Check(context != nullptr && gladLoadGLLoader(SDL_GL_GetProcAddress), "GL context/loader failed");
            std::cout << "[GL] " << glGetString(GL_VERSION) << " / " << glGetString(GL_RENDERER) << '\n';
            {
                Asset::Shader shader; BuildShader(shader);
                Geometry geometry;
                geometry.AddAttributes(std::vector<Vec3f>{{-.08f, -.08f, 0}, {.08f, -.08f, 0}, {0, .08f, 0}});
                MovingScene f;
                f.Body()->m_LinearVelocity = Vec3f(24, 0, 0);
                f.entity.AddComponent<RenderComponent>().Shader = &shader;
                f.entity.AddComponent<PreRenderPassComponent>().Shader = &shader;
                f.entity.AddComponent<MeshComponent>(&geometry);
                f.entity.AddComponent<TexturesComponent>();
                SceneOperationChecked([&] { return f.scene.PushToRenderList(f.entity); });
                Camera::_EditorCamera camera;
                glViewport(0, 0, 128, 128); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
                glClearColor(0, 0, 0, 1);
                f.scene.Update(Timestep(dt));
                std::array<double, 3> centers{};
                for (int sample = 0; sample < 3; ++sample)
                {
                    if (sample) f.scene.Update(Timestep(dt * (sample == 1 ? .5 : .49)));
                    const auto expected = f.Sample();
                    glClear(GL_COLOR_BUFFER_BIT);
                    Submitted(RenderSystem::SceneRender(&f.scene, camera));
                    UniformMatches(shader, expected.matrix); centers[sample] = PixelCenter();
                    shader.Bind(); Submitted(RenderSystem::MousePickPreRender(&f.scene, camera, &shader)); UniformMatches(shader, expected.matrix);
                    shader.Bind(); Submitted(RenderSystem::CascadedShadowPreRender(&f.scene)); UniformMatches(shader, expected.matrix);
                    shader.Bind(); Submitted(RenderSystem::PointShadowPreRender(&f.scene, &shader, {}, Vec3f(0), 100)); UniformMatches(shader, expected.matrix);
                    Submitted(RenderSystem::CascadedShadowSceneRender(&f.scene, camera, {}, 100)); UniformMatches(shader, expected.matrix);
                    Submitted(RenderSystem::SkyBoxRender(f.entity, camera)); UniformMatches(shader, expected.matrix);
                    Check(f.Sample().revision == expected.revision && f.scene.GetPhysicsTiming().totalSteps == 1,
                        "Multiple render passes changed presentation or simulation state");
                }
                Check(centers[1] - centers[0] > 8 && centers[2] - centers[1] > 8,
                    "Actual rendered pixels did not move between physics ticks");
                f.entity.AddComponent<PointLightComponent>();
                SceneOperationChecked([&] { return f.scene.PushToRenderList(f.entity); });
                Submitted(RenderSystem::PointLightsVisualize(&f.scene, camera, &shader)); UniformMatches(shader, f.Sample().matrix);
                shader.Bind(); Submitted(RenderSystem::MousePickPreRender(&f.scene, camera, &shader)); UniformMatches(shader, f.Sample().matrix);

                _Scene foreignScene;
                auto foreign=SceneOperationChecked([&] { return foreignScene.CreateEntity("foreign renderer entity"); });
                foreign.AddComponent<RenderComponent>().Shader=&shader;
                foreign.AddComponent<PreRenderPassComponent>().Shader=&shader;
                foreign.AddComponent<MeshComponent>(&geometry);
                foreign.AddComponent<TexturesComponent>();
                SceneDetail::BackendAccess::Groups(f.scene).clear(); SceneDetail::BackendAccess::Lights(f.scene).clear();
                SceneDetail::BackendAccess::Groups(f.scene)[Asset::ShaderBackendAccess::Program(shader)]={foreign};
                const auto before=RenderSystem::GetRenderStats();
                SubmissionRejected(RenderSystem::SceneRender(&f.scene,camera));
                SubmissionRejected(RenderSystem::MousePickPreRender(&f.scene,camera,&shader));
                SubmissionRejected(RenderSystem::CascadedShadowPreRender(&f.scene));
                SubmissionRejected(RenderSystem::PointShadowPreRender(&f.scene,&shader,{},Vec3f(0),100));
                SubmissionRejected(RenderSystem::CascadedShadowSceneRender(&f.scene,camera,{},100));
                auto cascade=Take(CascadeShadowFrameBuffer::Create(32,32,0));
                auto point=Take(PointShadowFrameBuffer::Create(32,32));
                auto picking=Take(MousePickFrameBuffer::Create(32,32));
                SubmissionRejected(RenderSystem::CascadedShadowPass(&f.scene,&shader,cascade));
                GLint bound=0,cull=0;
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&bound); glGetIntegerv(GL_CULL_FACE_MODE,&cull);
                Check(bound==0 && cull==GL_BACK,"Failed cascade pass did not release framebuffer/cull state");
                SubmissionRejected(RenderSystem::PointShadowPass(&f.scene,&shader,point,Vec3f(0),.1f,100));
                SubmissionRejected(RenderSystem::MousePickPass(&f.scene,camera,&shader,picking));
                SubmissionRejected(RenderSystem::MousePickPass(&f.scene,camera,&shader,picking,Vec2f(0),Vec2f(32)));
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&bound);
                Check(bound==0,"Failed point/picking pass did not release framebuffer");
                foreign.RemoveComponent<Transform3DComponent>();
                glDepthFunc(GL_LESS);
                SubmissionRejected(RenderSystem::SkyBoxRender(foreign,camera));
                GLint depth=0; glGetIntegerv(GL_DEPTH_FUNC,&depth);
                Check(depth==GL_LESS,"Rejected skybox changed depth state");
                auto missingPoint=SceneOperationChecked([&] { return f.scene.CreateEntity("missing point transform"); });
                missingPoint.AddComponent<PointLightComponent>(); missingPoint.AddComponent<MeshComponent>(&geometry);
                missingPoint.AddComponent<RenderComponent>().Shader=&shader;
                missingPoint.RemoveComponent<Transform3DComponent>();
                SceneDetail::BackendAccess::Lights(f.scene)[Asset::ShaderBackendAccess::Program(shader)]={missingPoint};
                // Remove the otherwise valid point so the failure test observes no earlier successful draws.
                f.entity.RemoveComponent<PointLightComponent>();
                SubmissionRejected(RenderSystem::PointLightsVisualize(&f.scene,camera,&shader));
                const auto after=RenderSystem::GetRenderStats();
                Check(before.m_ArrayDrawCall==after.m_ArrayDrawCall && before.m_ElementsDrawCall==after.m_ElementsDrawCall,
                    "Rejected transform issued a draw");
                FrameBufferSpecification wrongDescription;
                wrongDescription.Width=wrongDescription.Height=32;
                wrongDescription.Colors[0]=FramebufferFormat::RGBA8; wrongDescription.ColorCount=1;
                const_cast<FrameBuffer&>(picking.Buffer())=Take(FrameBuffer::Create(wrongDescription));
                auto failedClear=RenderSystem::MousePickPass(&f.scene,camera,&shader,picking);
                Check(!failedClear && std::holds_alternative<FramebufferError>(failedClear.error()),"Picking clear error was swallowed");
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&bound); Check(bound==0,"Failed clear retained framebuffer binding");
                std::cout << "[PASS] retained submission typed-query/no-post-failure-draw/pass-state/clear-error\n";
                std::cout << "[PASS] GPU pixels cycle=" << cycle << " centers=" << centers[0] << ',' << centers[1] << ',' << centers[2]
                    << " fixed-ticks=1 scene/picking/shadow/skybox-matrices=equal\n";
            }
            Check(glGetError() == GL_NO_ERROR, "GL error during render sampling or resource retirement");
            RenderCounters::ForgetContext(context.get());
        }
    }


#ifdef RETAINED_SUBMISSION_INTERNAL_PROBE
    void RangeChecks()
    {
        for (float value:{-1.f,std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN(),2147483648.f})
        {
            const auto result=::GEngine::ViewportExtent(value,"range fixture",1);
            Check(!result && result.error().code==RenderSystemRangeCode::ViewportExtent
                && result.error().operation=="range fixture" && result.error().axis==1
                && (result.error().extent==value || (std::isnan(result.error().extent)&&std::isnan(value)))
                && result.error().message=="Framebuffer extent is not representable as GLsizei", "Viewport rejection lost diagnostics");
        }
        Check(Take(::GEngine::ViewportExtent(0,"range fixture",0))==0
            && Take(::GEngine::ViewportExtent(13.75f,"range fixture",0))==13
            && Take(::GEngine::ViewportExtent(2147483520.f,"range fixture",0))==2147483520,"Viewport truncation/boundary changed");
        auto second=::GEngine::ViewportSize(Vec2f(32,-1),"second axis");
        Check(!second && second.error().axis==1,"Height error lost axis");
        const auto limit=static_cast<std::size_t>(std::numeric_limits<GLsizei>::max());
        Check(Take(::GEngine::VertexCount(0))==0 && Take(::GEngine::VertexCount(limit))==limit,"Vertex count success changed");
        for(auto count:{limit+1,std::numeric_limits<std::size_t>::max()})
        {
            auto result=::GEngine::VertexCount(count);
            Check(!result && result.error().code==RenderSystemRangeCode::VertexCount && result.error().vertexCount==count
                && result.error().operation=="RenderSystem::KDTreeVisualize"
                && result.error().message=="Debug line vertex count exceeds GLsizei","Vertex count error lost diagnostics");
        }
        std::cout << "[PASS] retained range boundaries/diagnostics/no-context\n";
    }
#endif
    class SuspensionApp final : public BaseApp
    {
    public:
        MovingScene f;
        int updates = 0, renders = 0;
        _Scene::RenderTransform beforeSuspend;
        std::atomic<bool> queued{false};
        std::jthread events;
        void ProcessInput(Timestep) override {}
        void Update(Timestep ts) override
        {
            ++updates;
            if (updates == 3)
            {
                const auto unchanged = f.Sample();
                Check(unchanged.revision == beforeSuspend.revision && ts.GetSecondsPrecise() < .25,
                    "Suspended loop modified presentation or accumulated suspended wall time");
            }
            f.scene.Update(ts);
            if (updates == 2) Check(f.scene.GetRenderInterpolationAlpha() == 1, "Stall backlog did not clamp presentation alpha");
            Check(f.Sample().matrix[3].x <= f.Body()->m_Position.x + 1e-6f, "Live presentation extrapolated");
        }
        void Render() override
        {
            ++renders;
            if (renders == 1) std::this_thread::sleep_for(std::chrono::milliseconds(350));
            if (renders == 2)
            {
                beforeSuspend = f.Sample();
                const auto id = static_cast<SDLWindow*>(GetWindow())->GetWindowID();
                SDL_Event event{}; event.type = SDL_WINDOWEVENT;
                event.window.windowID = id; event.window.event = SDL_WINDOWEVENT_MINIMIZED;
                Check(SDL_PushEvent(&event) == 1, "Minimize event failed");
                events = std::jthread([this, id] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(900));
                    SDL_Event event{}; event.type = SDL_WINDOWEVENT;
                    event.window.windowID = id; event.window.event = SDL_WINDOWEVENT_RESTORED;
                    queued = SDL_PushEvent(&event) == 1;
                });
            }
            if (renders == 3)
            {
                SDL_Event quit{}; quit.type = SDL_QUIT;
                Check(SDL_PushEvent(&quit) == 1, "Quit event failed");
            }
        }
    };
    void SuspensionChecks()
    {
        SDL_SetMainReady(); RuntimeAssets::Initialize("GEngineEditor");
        {
            SuspensionApp app;
            WindowProperties properties;
            properties.m_Width = properties.m_Height = 64;
            properties.m_MinWidth = properties.m_MinHeight = 32;
            properties.m_IsVsync = false;
            properties.flag = BitFlags<WindowFlags, uint8_t>{WindowFlags::INVISIBLE};
            Check(app.Initialize(properties).has_value(), "Application initialization failed");
            SDL_Event event{}; while (SDL_PollEvent(&event)) {}
            event.type = SDL_WINDOWEVENT; event.window.windowID = static_cast<SDLWindow*>(app.GetWindow())->GetWindowID();
            event.window.event = SDL_WINDOWEVENT_SHOWN;
            Check(SDL_PushEvent(&event) == 1, "Show event failed"); app.PollEvents();
            app.Run(); app.events.join();
            Check(app.queued && app.updates == 3 && app.renders == 3
                && app.f.scene.GetPhysicsTiming().stepsLastUpdate == 2,
                "Suspension repeated work or failed to resume bounded catch-up");
            std::cout << "[PASS] live 350ms stall / 900ms suspend / restore, stable presentation and bounded ticks\n";
        }
        // BaseApp owns the rendering platform; scope exit releases it.
    }
}

int main(int argc, char** argv)
{
    try
    {
        const std::string_view mode = argc == 2 ? argv[1] : "";
        if (mode == "--cpu")
        {
            // Match PhysicsTests setup: Debug collision diagnostics require logging.
            Log::Initialize();
            EndpointsAndDiscontinuities(); LifetimeAndBacklog(); RefreshAndDeterminism();
        }
        else if (mode == "--gl") GLChecks();
#ifdef RETAINED_SUBMISSION_INTERNAL_PROBE
        else if (mode == "--ranges") RangeChecks();
#endif
        else if (mode == "--suspension") SuspensionChecks();
        else throw std::invalid_argument("Expected --cpu, --gl or --suspension");
        std::cout << "[PASS] interpolation " << mode << " checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}

#endif // RETAINED_SUBMISSION_SCHEMA_ONLY
