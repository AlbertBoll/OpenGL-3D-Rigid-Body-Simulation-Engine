#include "RigidBodySimulation.h"
#include "Core/RuntimeAssets.h"
#include <cstdio>
#include <filesystem>

using namespace GEngine;

WindowProperties winProp{.m_Title = "RigidBodySimulation",
                         .m_Width = 1280,
                         .m_Height = 720,
                         .m_MinWidth = 480,
                         .m_MinHeight = 320,
                         .m_AspectRatio = 16.f / 9.f,
                         .m_WinPos = WindowPos::Center,
                         .m_XPaddingToCenterY = 5,
                         .m_YPaddingToCenterX = 20,
                         .ImGuiWindowProperties = {.bMoveFromTitleBarOnly = true,
                                                   .bDockingEnabled = true,
                                                   .bViewPortEnabled = false}};
int main(int argc, char* args[])
{
    // Select exactly one scene for the normal application.
    constexpr auto scenePreset = Rbs::RbsScenePreset::SphereLattice;
    // All consumers borrow this immutable value; it outlives app/EngineContext.
    const Rbs::LaunchConfig launch{.scene = scenePreset};
    if (argc < 1 || !args || !args[0])
    {
        std::fprintf(stderr, "Runtime assets: executable path is unavailable\n");
        return 1;
    }
    if (auto assets = RuntimeAssets::Initialize(std::filesystem::path(args[0]).stem().string(),
                                                launch.engine);
        !assets)
    {
        // Asset initialization precedes the engine logger.
        std::fprintf(stderr, "Runtime asset failure code=%u operation=%s: %s\n",
                     static_cast<unsigned>(assets.error().code), assets.error().operation.c_str(),
                     assets.error().message.c_str());
        return 1;
    }
    // BaseApp owns EngineContext, which outlives all application resources.
    ScopedPtr<BaseApp> app(new RigidBodySimulationApp(launch));
    if (!app)
    {
        std::fprintf(stderr, "GEngine application allocation failed\n");
        return 1;
    }
    if (auto initialized = app->Initialize({winProp}); !initialized)
    {
        ReportApplicationError(initialized.error());
        return 1;
    }
    if (auto running = app->Run(); !running)
    {
        ReportApplicationError(running.error());
        return 1;
    }
    return 0;
}
