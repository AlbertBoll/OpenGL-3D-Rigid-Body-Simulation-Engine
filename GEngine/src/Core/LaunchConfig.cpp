#include "gepch.h"
#include "Core/LaunchConfig.h"
#include <cstdlib>
#include <string_view>

namespace GEngine
{
    EngineLaunchConfig CaptureEngineLaunchConfig(const LaunchEnvironment& environment)
    {
        const auto read = [&](const char* name)
        {
            return environment.read ? environment.read(environment.context, name)
                                    : SDL_getenv(name);
        };
        const auto text = [&](const char* name) -> std::optional<std::string>
        {
            const auto* value = read(name);
            return value ? std::optional<std::string>{value} : std::nullopt;
        };
        EngineLaunchConfig result;
        result.assetRootUtf8 = text("GENGINE_ASSET_ROOT");
        const auto tier = text("GENGINE_SHADOW_QUALITY");
        const auto resolution = text("GENGINE_SHADOW_RESOLUTION");
        result.shadowQuality = ParseShadowQuality(tier ? tier->c_str() : nullptr,
                                                  resolution ? resolution->c_str() : nullptr);
        result.passTimingOutput = text("GENGINE_PASS_TIMING_OUTPUT");
        const bool timingRequested = read("GENGINE_PASS_TIMING") != nullptr;
        result.passTiming = timingRequested || result.passTimingOutput.has_value();
        const auto counters = text("GENGINE_RENDER_COUNTERS_LOG");
        result.reportRenderCounters = counters && *counters == "1";
        result.baselineOutput = text("GENGINE_BASELINE_OUTPUT");
        result.baselineSceneTarget = read("GENGINE_BASELINE_SCENE_TARGET") != nullptr;
        const auto diagnostic = [&](const char* name)
        {
            const char* value =
                environment.read ? environment.read(environment.context, name) : std::getenv(name);
            return value && std::string_view(value) == "1";
        };
        result.pointShadowDiagnostic = diagnostic("GENGINE_PRE_EDITOR_POINT_SHADOW_DIAGNOSTIC");
        result.pointGpuCapture = diagnostic("GENGINE_PRE_EDITOR_POINT_GPU_CAPTURE");
        return result;
    }
}
