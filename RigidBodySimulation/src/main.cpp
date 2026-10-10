#include "RigidBodySimulation.h"
#include "Core/RuntimeAssets.h"
#include "Core/RenderBaseline.h"
#include <cstdio>
#include <crtdbg.h>
#include <filesystem>
#include <memory>
#ifdef GENGINE_RBS_SCENE_VALIDATION
#include <string_view>
#include <iterator>
#endif

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
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    // Select exactly one RBS scene here; bootstrap supplies all launch settings.
    constexpr auto scenePreset = Rbs::RbsScenePreset::GeometryGallery;
    Rbs::LaunchConfig settings;
    settings.scene = scenePreset;
#ifdef GENGINE_RBS_SCENE_VALIDATION
    // Scene/UI test executable; normal RBS has no scene-selection CLI.
    constexpr Rbs::RbsScenePreset presets[]{
        Rbs::RbsScenePreset::GeometryGallery, Rbs::RbsScenePreset::SphereDiamond,
        Rbs::RbsScenePreset::SphereLattice, Rbs::RbsScenePreset::BoxStack,
        Rbs::RbsScenePreset::SphereBoxStack};
    constexpr std::string_view names[]{"GeometryGallery", "SphereDiamond", "SphereLattice",
                                       "BoxStack", "SphereBoxStack"};
    if (argc < 1 || argc > 2 || !args || (argc == 2 && !args[1]))
        return 1;
    std::size_t selected = 0;
    while (argc == 2 && selected < std::size(names) && names[selected] != args[1])
        ++selected;
    if (selected == std::size(names))
        return 1;
    settings.scene = argc == 1 ? scenePreset : presets[selected];
    if (settings.scene != Rbs::RbsScenePreset::GeometryGallery)
        settings.startupMesh = Rbs::StartupMesh::Barrel;
#endif
#if defined(GENGINE_RBS_SCENE_VALIDATION) || defined(GENGINE_RBS_MODULARITY_VALIDATION)
    // Probe outputs sit one directory below the normal target. This is an
    // explicit diagnostic asset location, never a process-environment override.
    if (argc < 1 || !args || !args[0])
        return 1;
    const auto probeExecutable = std::filesystem::path(args[0]);
    if (!probeExecutable.is_absolute())
    {
        std::fprintf(stderr, "RBS validation requires an absolute executable path\n");
        return 1;
    }
    settings.engine.assetRootUtf8 =
        (probeExecutable.parent_path().parent_path().parent_path() / "assets").generic_string();
#endif
    // All consumers borrow this immutable value; it outlives app/EngineContext.
    const auto launch = std::move(settings);
#ifdef GENGINE_RENDER_BASELINE
    RenderBaseline::Configure(winProp, launch.engine);
#endif
    if (argc < 1 || !args || !args[0])
    {
        std::fprintf(stderr, "Runtime assets: executable path is unavailable\n");
        return 1;
    }
    if (auto assets = RuntimeAssets::Initialize(std::filesystem::path(args[0]).stem().string(),
                                                launch.engine);
        !assets)
    {
        // Bootstrap precedes the engine logger; preserve the complete typed diagnostic.
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
