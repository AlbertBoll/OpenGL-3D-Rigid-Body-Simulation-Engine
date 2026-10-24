#pragma once

#include <memory>

namespace GEngine
{
    class BaseApp;
    struct WindowProperties;
}

namespace Rbs
{
    struct LaunchConfig;

    // Explicit opt-in adapters; ordinary startup keeps the Owner's launch values.
    [[nodiscard]] bool ConfigureDiagnosticLaunch(int argc, char* const args[],
                                                LaunchConfig&, GEngine::WindowProperties&);
    void ConfigureDiagnosticApplication(const LaunchConfig&, GEngine::BaseApp&);

    // Diagnostic evidence is completed only after application/resource teardown.
    // Normal startup leaves ownership with main, including on failure.
    bool FinishDiagnosticApplication(const LaunchConfig&, std::unique_ptr<GEngine::BaseApp>&,
                                     bool clean);
}
