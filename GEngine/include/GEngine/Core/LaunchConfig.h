#pragma once

#include "Renderer/ShadowQuality.h"
#include <optional>
#include <string>

namespace GEngine
{
    // Startup-only adapter. A supplied lookup borrows its context for this call;
    // captured settings own their strings and never retain environment pointers.
    struct LaunchEnvironment
    {
        const void* context = nullptr;
        const char* (*read)(const void*, const char*) = nullptr;
    };

    struct EngineLaunchConfig
    {
        std::optional<std::string> assetRootUtf8;
        std::expected<ShadowQualityDesc, FramebufferError> shadowQuality{ShadowQualityDesc{}};
        bool passTiming = false;
        std::optional<std::string> passTimingOutput;
        bool reportRenderCounters = false;
        std::optional<std::string> baselineOutput;
        bool baselineSceneTarget = false;
        bool pointShadowDiagnostic = false;
        bool pointGpuCapture = false;
    };

    // Capture once before platform startup. A shadow parse failure is retained
    // until BaseApp's existing validation stage, preserving error precedence.
    [[nodiscard]] EngineLaunchConfig CaptureEngineLaunchConfig(const LaunchEnvironment& = {});
}
