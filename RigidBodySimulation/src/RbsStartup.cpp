#include "RbsStartup.h"
#include "RigidBodySimulation.h"
#include "RbsLoadingScreen.h"
#include "Core/RuntimeAssets.h"
#include "../tests/BackendChecks.h"
#include "../tests/EditorCorrectionChecks.h"
#include "../tests/HierarchyChecks.h"
#include "../tests/SelectionMarqueeChecks.h"
#include "../tests/Owner05Checks.h"
#include "../tests/NumericInputChecks.h"
#include "../tests/EditorViewsChecks.h"
#include "../tests/TransformShortcutChecks.h"
#include "../tests/SnapChecks.h"
#include "../tests/ContinuousTransformChecks.h"
#include "../tests/ScalableTransformChecks.h"
#include "../tests/ScalablePlacementChecks.h"
#include "../tests/ImpulseEditorChecks.h"
#include "../tests/ImpulseBoundaryChecks.h"
#include "../tests/JoltParameterChecks.h"
#include <cstdio>
#include <filesystem>
#include <sdl2/SDL.h>
#ifdef GENGINE_PLATFORM_WINDOWS
#include <Windows.h>
#endif
#ifdef GENGINE_PHASE26_QUALIFICATION
#include "../tests/DistanceJointChecks.h"
#endif
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
#include "../tests/SandboxChecks.h"
#endif

namespace Rbs
{
    namespace
    {
        GEngine::WindowProperties MakeWindowProperties(const LaunchConfig& launch)
        {
            using namespace GEngine;
            WindowProperties properties{.m_Title = "RigidBodySimulation",
                .m_Width = 1280, .m_Height = 720,
                .m_MinWidth = 480, .m_MinHeight = 320,
                .m_AspectRatio = 16.f / 9.f, .m_WinPos = WindowPos::Center,
                .m_XPaddingToCenterY = 5, .m_YPaddingToCenterX = 20,
                .ImGuiWindowProperties = {.bMoveFromTitleBarOnly = true,
                    .bDockingEnabled = true, .bViewPortEnabled = false}};
#ifdef GENGINE_JOLT_FINAL_AUDIT
            properties.m_Width = 1920; properties.m_Height = 1080; properties.m_IsVsync = false;
#endif
#ifdef GENGINE_OWNER_REVIEW_PROBE
            properties.m_Width = 1920; properties.m_Height = 1080;
            if (SDL_getenv("OWNER_REVIEW_NO_VSYNC")) properties.m_IsVsync = false;
#endif
            if (launch.InteractiveSandboxEnabled())
            {
                properties.m_MinWidth = 1100; properties.m_MinHeight = 700;
                properties.m_DpiAwareMinimum = true; properties.m_PaintBeforeShow = true;
            }
            const std::string title = std::string("RigidBodySimulation - ") + SceneName(launch.scene) +
                (launch.IsDistanceScene() ? " (" + std::to_string(launch.scene == RbsScenePreset::DistancePendulum ? 1 : launch.chainLinks) +
                    (launch.m_Backend == PhysicsBackend::Native ? " links, 60 Hz, Native)" : " links, 60 Hz, Jolt)") : "");
            properties.m_Title = title + " - " + std::string(PhysicsBackendName(launch.m_Backend));
            return properties;
        }
    }

    std::optional<int> TryRunDiagnosticEntry(int argc, char* const args[])
    {
        if (argc != 2) return std::nullopt;
        const std::string_view argument(args[1]);
        if (argument == "--impulse-editor-checks") return ImpulseEditorChecks::Run();
        if (argument == "--impulse-boundary-checks") return ImpulseBoundaryChecks::Run();
        if (argument == "--backend-checks") return BackendChecks::Run();
        if (argument == "--editor-correction-checks") return EditorCorrectionChecks::Run();
        if (argument == "--selection-batch-checks") return SelectionMarqueeChecks::Run();
        if (argument == "--hierarchy-checks") return HierarchyChecks::Run();
        if (argument == "--owner05-checks") return Owner05Checks::Run();
        if (argument == "--numeric-input-checks") return NumericInputChecks::Run();
        if (argument == "--editor-views-checks") return EditorViewsChecks::Run();
        if (argument == "--transform-shortcut-checks") return TransformShortcutChecks::Run();
        if (argument == "--snap-checks") return SnapChecks::Run();
        if (argument == "--scalable-transform-checks") return ScalableTransformChecks::Run();
        if (argument == "--scalable-placement-checks") return ScalablePlacementChecks::Run();
        if (argument == "--continuous-transform-checks") return ContinuousTransformChecks::Run();
        if (argument == "--jolt-parameter-checks") return JoltParameterChecks::Run();
        return std::nullopt;
    }

    void ReportStartupError(const std::string& message) noexcept
    {
        // Diagnostic write failure must not replace the original startup result.
        (void)TryPrintDiagnosticLine(stderr, "RBS startup: {}", message.c_str());
    }

    int RunApplication(LaunchConfig launch, const char* executablePath)
    {
        using namespace GEngine;
#ifdef GENGINE_OWNER_REVIEW_PROBE
        launch.engine.passTiming = true;
        launch.engine.passTimingOutput = std::filesystem::current_path().generic_string();
#endif
        // Frozen qualification profiles retain their established timing setup.
        if (launch.InteractiveSandboxEnabled())
            if (const char* timing = SDL_getenv("GENGINE_PASS_TIMING_OUTPUT"))
            {
                launch.engine.passTiming = true;
                launch.engine.passTimingOutput = timing;
            }
        const auto properties = MakeWindowProperties(launch);
#ifdef GENGINE_PLATFORM_WINDOWS
        // Only the interactive Editor changes the process DPI-awareness policy.
        if (launch.InteractiveSandboxEnabled())
            SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
        std::optional<Editor::LoadingScreen> startup;
        if (launch.InteractiveSandboxEnabled()) {
            startup.emplace(Editor::LoadingScreen::Operation::Startup, SceneName(launch.scene), launch.scene);
            InitializationProgress::Report(InitializationProgress::Stage::Renderer, "Checking runtime assets");
        }
        if (auto assets = RuntimeAssets::Initialize(std::filesystem::path(executablePath).stem().string(), launch.engine); !assets)
        {
            // Asset initialization precedes the logger; output failure does not
            // replace the asset error or prevent its existing modal report.
            (void)TryPrintDiagnosticLine(stderr, "Runtime asset failure code={} operation={}: {}",
                static_cast<unsigned>(assets.error().code), assets.error().operation.c_str(),
                assets.error().message.c_str());
            if (launch.InteractiveSandboxEnabled())
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GEngine initialization failed", assets.error().message.c_str(), nullptr);
            return 1;
        }
        // This local owner is destroyed before the launch parameter; EngineContext
        // continues to outlive all application resources through BaseApp ownership.
        ScopedPtr<BaseApp> app(new RigidBodySimulationApp(launch));
        if (!app)
        {
            (void)TryPrintDiagnosticLine(stderr, "GEngine application allocation failed");
            return 1;
        }
        if (auto initialized = app->Initialize({properties}); !initialized)
        {
            ReportApplicationError(initialized.error());
            if (launch.InteractiveSandboxEnabled())
            {
                const auto* platform = std::get_if<PlatformError>(&initialized.error());
                if (!platform || platform->operation != "startup cancelled")
                {
                    const auto detail = std::visit([](const auto& error) -> std::string {
                        if constexpr (requires { error.message; error.operation; })
                            return std::string(error.operation) + ": " + std::string(error.message);
                        else
                            return "Initialization failed; the detailed operation/error code is recorded in GEngine.log.";
                    }, initialized.error());
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GEngine initialization failed", detail.c_str(), nullptr);
                }
            }
            return 1;
        }
        if (startup) {
            startup->Finish();
            startup.reset();
        }
        if (auto running = app->Run(); !running)
        {
            ReportApplicationError(running.error());
            return 1;
        }
#ifdef GENGINE_PHASE26_QUALIFICATION
        Phase26::BeforeShutdown();
        app.reset();
        return Phase26::Finish(true) ? 0 : 1;
#elif defined(GENGINE_PHASE26_SANDBOX_CHECKS)
        app.reset();
        return SandboxChecks::Finish() ? 0 : 1;
#else
        return 0;
#endif
    }
}
