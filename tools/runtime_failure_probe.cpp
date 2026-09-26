// Focused tests of the production Run/EntryPoint contract; no production test hooks.
#include "Core/BaseApp.h"
#include <type_traits>
static_assert(std::is_same_v<decltype(std::declval<::GEngine::BaseApp&>().Run()), ::GEngine::ApplicationRunResult>);
static_assert(std::is_same_v<::GEngine::ApplicationRunResult::error_type, ::GEngine::ApplicationRuntimeError>);
#ifndef RUNTIME_SCHEMA_ONLY
#include "gepch.h"
#include "Core/Scene.h"
#include "Windows/SDLWindow.h"
#include <sdl2/SDL_ttf.h>
#include <print>
#include <string_view>
#define main ProductionEntryPoint
#include "GEngine/EntryPoint.h"
#undef main

namespace {
using namespace GEngine;
std::string_view mode;
bool valid = true;
int controls{}, updates{}, renders{}, destructors{}, gpuDeletes{};
void Check(bool condition, const char* message) {
    if (!condition) { valid = false; std::println("[FAIL] {}", message); }
}
ApplicationRuntimeError Failure() {
    return {ApplicationRuntimeErrorCode::SubsystemFailure, "fixture-subsystem", "fixture-code",
        "fixture-operation", "event=37; resource=music", "full diagnostic: native=81; detail=unavailable"};
}
struct OwnedBuffer {
    GLuint id{};
    ~OwnedBuffer() {
        if (!id) return;
        Check(EngineContext::TryGet() && SDL_GL_GetCurrentContext(), "GPU teardown lost its owning context");
        Check(glIsBuffer(id) == GL_TRUE, "GPU owner no longer owns its buffer");
        glDeleteBuffers(1, &id); ++gpuDeletes;
    }
};
class ProbeApp final : public BaseApp {
    OwnedBuffer buffer;
public:
    ApplicationInitializationResult Initialize(const std::initializer_list<WindowProperties>& properties) override {
        auto result = BaseApp::Initialize(properties);
        if (!result) return result;
        glGenBuffers(1, &buffer.id); glBindBuffer(GL_ARRAY_BUFFER, buffer.id);
        const int value = 37; glBufferData(GL_ARRAY_BUFFER, sizeof(value), &value, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        SDL_Event event{}; while (SDL_PollEvent(&event)) {}
        // Hidden native window; exercise the active production loop without desktop UI.
        m_WindowHidden = false;
        SetManualFrameRateLimit(0);
        if (mode == "close") { event.type = SDL_QUIT; Check(SDL_PushEvent(&event) == 1, "queue normal close"); }
        return {};
    }
    void ProcessInput(Timestep) override {
        ++controls;
        if (mode == "input-failure") { FailRuntime(Failure()); return; }
    }
    void Update(Timestep) override {
        ++updates;
        if (mode == "update-failure") {
            FailRuntime(Failure());
            auto later = Failure(); later.message = "secondary error must not replace first cause";
            FailRuntime(std::move(later));
            return;
        }
        if (mode == "success") ShutDown();
    }
    void Render() override {
        ++renders;
        if (mode == "render-failure") { FailRuntime(Failure()); return; }
    }
    ApplicationRunResult Run() override {
        auto result = BaseApp::Run();
        const bool failing = mode.ends_with("failure");
        Check(result.has_value() != failing, "Run success/failure result");
        if (!result) {
            const auto& error = result.error(); const auto expected = Failure();
            Check(error.code == expected.code && error.subsystem == expected.subsystem &&
                error.subsystemCode == expected.subsystemCode && error.operation == expected.operation &&
                error.context == expected.context && error.message == expected.message, "complete typed diagnostics");
            // A subsequent call cannot resume submission or lose the first cause.
            auto repeated = BaseApp::Run();
            Check(!repeated && repeated.error().message == expected.message, "failure remains sticky");
        }
        Check(controls == (mode == "close" ? 0 : 1), "input count");
        Check(updates == ((mode == "close" || mode == "input-failure") ? 0 : 1), "update count");
        Check(renders == ((mode == "success" || mode == "render-failure") ? 1 : 0), "post-failure render was executed");
        return result;
    }
    ~ProbeApp() override { ++destructors; }
};
}
::GEngine::WindowProperties winProp = [] {
    ::GEngine::WindowProperties p; p.m_Title = "Runtime failure probe";
    p.m_Width = p.m_Height = 64; p.m_MinWidth = p.m_MinHeight = 32; p.m_IsVsync = false;
    p.flag = ::GEngine::BitFlags<::GEngine::WindowFlags, uint8_t>{::GEngine::WindowFlags::INVISIBLE};
    return p;
}();
::GEngine::BaseApp* CreateApp() { return new ProbeApp; }
int main(int argc, char* argv[]) {
    if (argc != 2) return 2;
    mode = argv[1];
    if (mode != "close" && mode != "success" && mode != "input-failure" &&
        mode != "update-failure" && mode != "render-failure") return 2;
    SDL_SetMainReady();
    const int result = ProductionEntryPoint(argc, argv);
    Check(result == (mode.ends_with("failure") ? 1 : 0), "production process status");
    Check(destructors == 1 && gpuDeletes == 1, "derived RAII teardown count");
    Check(!::GEngine::EngineContext::TryGet() && SDL_WasInit(0) == 0 && TTF_WasInit() == 0,
        "engine/platform root survived application destruction");
    if (!valid) return 97;
    std::println("[PASS] runtime {}: exit={} controls={} updates={} renders={} teardown=1", mode, result, controls, updates, renders);
    return result;
}
#endif
