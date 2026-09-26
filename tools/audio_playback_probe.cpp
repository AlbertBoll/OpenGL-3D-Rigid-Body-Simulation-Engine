// Exact production SoundEvent and application callers; backend injection is test-only.
#include "Audio/SoundEvent.h"
#include <type_traits>
static_assert(std::is_same_v<decltype(std::declval<const GEngine::Audio::SoundEvent&>().GetPlayState()),
    GEngine::Audio::PlaybackStateResult>);
#ifndef AUDIO_SCHEMA_ONLY
#include "gepch.h"
#ifdef AUDIO_UNIT
#include "Audio/AudioSystem.h"
#endif
#include "Core/RuntimeAssets.h"
#include "Core/BaseApp.h"
#include "Core/Scene.h"
#include <fmod/fmod_studio.hpp>
#include <fmod/fmod_studio.h>
#include <fmod/fmod_errors.h>
#include <sdl2/SDL_ttf.h>
#include <array>
#include <print>
#include <string_view>

#ifndef AUDIO_UNIT
#define main ProductionEntryPoint
#include "EntryPoint.h"
#undef main
// EntryPoint already names the fixture factory. Only the application's factory is renamed.
#define CreateApp OriginalCreateApp
#ifdef AUDIO_EDITOR
#include "../GEngineEditor/src/SceneApp.cpp"
using ActualApp = SceneApp;
constexpr unsigned ownerQueries = 2;
#else
#include "../Breakout/src/BreakoutApp.cpp"
using ActualApp = ::GEngine::BreakoutApp;
constexpr unsigned ownerQueries = 1;
#endif
#undef CreateApp
#endif

namespace Injection {
bool active{}, valid = true;
unsigned queries{}, maintenanceQueries{}, restarts{};
FMOD_RESULT result = FMOD_OK;
FMOD_STUDIO_PLAYBACK_STATE state = FMOD_STUDIO_PLAYBACK_PLAYING;
void Check(bool ok, const char* message) {
    if (!ok) { valid = false; std::println("[FAIL] {}", message); }
}
}
// Deterministic test owner: the packaged bank need not contain this fixture event.
// All production SoundEvent methods and application Update/Run/EntryPoint code stay real.
// These opaque backend identities are never dereferenced or passed into the real DLL.
namespace GEngine::Audio {
namespace { std::array<unsigned, 16> handles{}; }
AudioSystem::AudioSystem() = default;
void AudioSystem::Initialize() {}
void AudioSystem::Shutdown() { mEventInstances.clear(); }
SoundEvent AudioSystem::PlayEvent(const std::string&) {
    const unsigned id = ++sNextID;
    Injection::Check(id < handles.size(), "fixture handle capacity");
    if (id >= handles.size()) return {};
    handles[id] = id;
    mEventInstances.emplace(id, reinterpret_cast<FMOD::Studio::EventInstance*>(&handles[id]));
    return SoundEvent(this, id);
}
FMOD::Studio::EventInstance* AudioSystem::GetEventInstance(unsigned id) {
    const auto found = mEventInstances.find(id);
    return found == mEventInstances.end() ? nullptr : found->second;
}
void AudioSystem::Update(float) {
    for (auto it = mEventInstances.begin(); it != mEventInstances.end();) {
        FMOD_STUDIO_PLAYBACK_STATE state{};
        const auto result = it->second->getPlaybackState(&state);
        Injection::Check(result == FMOD_OK, "owner maintenance query must succeed before caller injection");
        if (result == FMOD_OK && state == FMOD_STUDIO_PLAYBACK_STOPPED) it = mEventInstances.erase(it);
        else ++it;
    }
}
void AudioSystem::LoadBank(const std::string&) {}
void AudioSystem::UnloadBank(const std::string&) {}
void AudioSystem::UnloadAllBanks() { mEventInstances.clear(); }
void AudioSystem::SetListener(const Math::Mat4&) const {}
float AudioSystem::GetBusVolume(const std::string&) const { return 1.f; }
bool AudioSystem::GetBusPaused(const std::string&) const { return false; }
void AudioSystem::SetBusVolume(const std::string&, float) {}
void AudioSystem::SetBusPaused(const std::string&, bool) {}
}
// This test backend produces every state and failure at the native query boundary.
FMOD_RESULT F_API FMOD::Studio::EventInstance::getPlaybackState(FMOD_STUDIO_PLAYBACK_STATE* state) const {
    Injection::Check(Injection::active, "playback query outside the injection window");
    ++Injection::queries;
    if (Injection::queries <= Injection::maintenanceQueries) { *state = FMOD_STUDIO_PLAYBACK_PLAYING; return FMOD_OK; }
    if (Injection::result == FMOD_OK) *state = Injection::state;
    return Injection::result;
}
FMOD_RESULT F_API FMOD::Studio::EventInstance::start() {
    if (Injection::active) ++Injection::restarts;
    return FMOD_OK;
}

#ifdef AUDIO_UNIT
int main() {
    using namespace ::GEngine;
    using namespace ::GEngine::Audio;
    using Injection::Check;
    Log::Initialize(); RuntimeAssets::Initialize("GEngineEditor");
    AudioSystem system;
    system.Initialize();
    struct Cleanup { AudioSystem& system; ~Cleanup() { system.Shutdown(); } } cleanup{system};
    auto event = system.PlayEvent("event:/EnteringValley");
    Check(event.IsValid(), "fixture must own a live event");
    if (!event.IsValid()) return 97;
    Injection::active = true;
    auto defaultState = SoundEvent{}.GetPlayState();
    Check(defaultState && *defaultState == PLAYBACK_STOPPED && Injection::queries == 0, "default stopped without native query");
    const std::array states{
        std::pair{FMOD_STUDIO_PLAYBACK_PLAYING, PLAYBACK_PLAYING},
        std::pair{FMOD_STUDIO_PLAYBACK_SUSTAINING, PLAYBACK_SUSTAINING},
        std::pair{FMOD_STUDIO_PLAYBACK_STOPPED, PLAYBACK_STOPPED},
        std::pair{FMOD_STUDIO_PLAYBACK_STARTING, PLAYBACK_STARTING},
        std::pair{FMOD_STUDIO_PLAYBACK_STOPPING, PLAYBACK_STOPPING}};
    for (auto [native, expected] : states) {
        Injection::state = native; auto result = event.GetPlayState();
        Check(result && *result == expected, "valid playback state mapping");
    }
    Injection::result = FMOD_ERR_INVALID_HANDLE;
    auto failed = event.GetPlayState();
    Check(!failed, "native query failure must not become a stopped state");
    if (!failed) {
        const auto& error = failed.error();
        Check(error.code == PlaybackStateErrorCode::QueryFailed && error.eventId == 1 &&
            error.operation == "SoundEvent::GetPlayState" &&
            error.message.find("result=" + std::to_string(static_cast<int>(FMOD_ERR_INVALID_HANDLE))) != std::string::npos &&
            error.message.find(FMOD_ErrorString(FMOD_ERR_INVALID_HANDLE)) != std::string::npos, "complete native query diagnostic");
    }
    Injection::result = FMOD_OK; Injection::state = FMOD_STUDIO_PLAYBACK_FORCEINT;
    auto invalid = event.GetPlayState();
    Check(!invalid && invalid.error().code == PlaybackStateErrorCode::InvalidState && invalid.error().eventId == 1 &&
        invalid.error().message.find(std::to_string(static_cast<int>(FMOD_STUDIO_PLAYBACK_FORCEINT))) != std::string::npos,
        "unknown backend state is a typed failure");
    Injection::state = FMOD_STUDIO_PLAYBACK_STOPPED;
    system.Update(0); // Retire the fixture event; SoundEvent must observe the absent owner lookup.
    const auto queries = Injection::queries;
    auto retired = event.GetPlayState();
    Check(!event.IsValid() && retired && *retired == PLAYBACK_STOPPED && Injection::queries == queries,
        "retired handle remains successful stopped without native query");
    Injection::active = false;
    Check(SDL_WasInit(SDL_INIT_VIDEO) == 0 && !EngineContext::TryGet(), "unit probe must not create a rendering context");
    if (!Injection::valid) return 97;
    std::println("[PASS] audio default/retired, five states, native failure and invalid-state diagnostics");
    return 0;
}
#else

namespace {
std::string_view mode;
unsigned updates{}, renders{}, destructors{};
class CallerProbe final : public ActualApp {
public:
    ::GEngine::ApplicationInitializationResult Initialize(const std::initializer_list<::GEngine::WindowProperties>& props) override {
        auto result = ActualApp::Initialize(props);
        if (!result) return result;
        SDL_Event event{}; while (SDL_PollEvent(&event)) {}
        m_WindowHidden = false; SetManualFrameRateLimit(0);
        return {};
    }
    void ProcessInput(::GEngine::Timestep) override {}
    void Update(::GEngine::Timestep) override {
        ++updates;
        Injection::queries = Injection::restarts = 0; Injection::maintenanceQueries = ownerQueries;
        Injection::result = mode == "failure" ? FMOD_ERR_INVALID_HANDLE : FMOD_OK;
        Injection::state = mode == "stopping" ? FMOD_STUDIO_PLAYBACK_STOPPING : FMOD_STUDIO_PLAYBACK_PLAYING;
        Injection::active = true;
        ActualApp::Update(::GEngine::Timestep(0.0));
        Injection::active = false;
        Injection::Check(Injection::queries == ownerQueries + 1, "actual owner queries followed by SoundEvent query");
        Injection::Check(Injection::restarts == (mode == "stopping" ? 1u : 0u), "restart only on successful stopping");
        ShutDown();
    }
    void Render() override { ++renders; }
    ::GEngine::ApplicationRunResult Run() override {
        auto result = BaseApp::Run();
        Injection::Check(result.has_value() == (mode != "failure"), "caller propagated query failure");
        Injection::Check(updates == 1 && renders == (mode == "failure" ? 0u : 1u), "failure reached submission");
        if (!result) {
            const auto& e = result.error();
            Injection::Check(e.code == ::GEngine::ApplicationRuntimeErrorCode::SubsystemFailure && e.subsystem == "Audio" &&
                e.subsystemCode == "QueryFailed" && e.operation == "SoundEvent::GetPlayState" && e.context == "event=1" &&
                e.message.find(FMOD_ErrorString(FMOD_ERR_INVALID_HANDLE)) != std::string::npos &&
                e.message.find("result=" + std::to_string(static_cast<int>(FMOD_ERR_INVALID_HANDLE))) != std::string::npos,
                "caller lost complete audio diagnostics");
        }
        return result;
    }
    ~CallerProbe() override { ++destructors; }
};
}
::GEngine::WindowProperties winProp = [] {
    ::GEngine::WindowProperties p; p.m_Title = "Audio caller probe";
    p.m_Width = 800; p.m_Height = 600; p.m_MinWidth = p.m_MinHeight = 32; p.m_IsVsync = false;
    p.flag = ::GEngine::BitFlags<::GEngine::WindowFlags, uint8_t>{::GEngine::WindowFlags::INVISIBLE};
    return p;
}();
::GEngine::BaseApp* CreateApp() { return new CallerProbe; }
int main(int argc, char* argv[]) {
    if (argc != 2) return 2;
    mode = argv[1];
    if (mode != "failure" && mode != "playing" && mode != "stopping") return 2;
    SDL_SetMainReady();
    int result = ProductionEntryPoint(argc, argv);
    Injection::Check(result == (mode == "failure" ? 1 : 0), "actual entry-point process status");
    Injection::Check(destructors == 1 && !::GEngine::EngineContext::TryGet() && SDL_WasInit(0) == 0 && TTF_WasInit() == 0,
        "actual caller/application RAII teardown");
    if (!Injection::valid) return 97;
    std::println("[PASS] audio caller {} exit={} renders={} restarts={} teardown=1", mode, result, renders, Injection::restarts);
    return result;
}
#endif
#endif
