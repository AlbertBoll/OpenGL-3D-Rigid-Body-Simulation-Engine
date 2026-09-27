#pragma once
#include "Core/RuntimeAssets.h"
#include "Core/BaseApp.h"
#include "Core/RenderBaseline.h"
#include <filesystem>
#include <cstdio>

using namespace GEngine;
BaseApp* CreateApp();
extern WindowProperties winProp;

int main(int argc, char* args[])
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#ifdef GENGINE_RENDER_BASELINE
    RenderBaseline::Configure(winProp);
#endif
    if (argc < 1 || !args || !args[0]) {
        std::fprintf(stderr, "Runtime assets: executable path is unavailable\n");
        return 1;
    }
    if (auto assets = RuntimeAssets::Initialize(std::filesystem::path(args[0]).stem().string()); !assets) {
        // Bootstrap precedes the engine logger; preserve the complete typed diagnostic.
        std::fprintf(stderr, "Runtime asset failure code=%u operation=%s: %s\n",
            static_cast<unsigned>(assets.error().code), assets.error().operation.c_str(), assets.error().message.c_str());
        return 1;
    }
    // BaseApp owns EngineContext, which outlives all application resources.
    ScopedPtr<BaseApp> app(CreateApp());
    if (!app) {
        std::fprintf(stderr, "GEngine application allocation failed\n");
        return 1;
    }
    if (auto initialized = app->Initialize({winProp}); !initialized) {
        ReportApplicationError(initialized.error());
        return 1;
    }
    if (auto running = app->Run(); !running) {
        ReportApplicationError(running.error());
        return 1;
    }
    return 0;
}
