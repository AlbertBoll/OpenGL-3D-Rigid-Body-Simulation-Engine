#pragma once

#include "RbsLaunchConfig.h"
#include "Renderer/ShadowQuality.h"
#include <array>
#include <expected>
#include <string>

namespace PreEditorValidation
{
    inline std::expected<unsigned, std::string> CheckLaunchConfig()
    {
        using Check = Rbs::ValidationCheck;
        constexpr std::array checks{Check::SceneAuthoring,     Check::ResourceOwnership,
                                    Check::GeometryTemplates,  Check::ParametricGeometry,
                                    Check::GeometryAuthoring,  Check::MaterialAuthoring,
                                    Check::ShaderDescriptions, Check::ShaderReload,
                                    Check::AsyncResources,     Check::Subscriptions};
        unsigned count = 0;
        const Rbs::LaunchConfig normal;
        if (normal.scene != Rbs::RbsScenePreset::GeometryGallery ||
            normal.startupMesh != Rbs::StartupMesh::None || normal.AsyncPerformanceEnabled() ||
            !normal.validation.checks.empty() || normal.validation.materialAlphaFixture ||
            normal.validation.subscriptionOutput ||
            normal.validation.materialComparison != Rbs::MaterialComparison::Authored ||
            normal.engine.assetRootUtf8 || normal.engine.passTiming ||
            normal.engine.passTimingOutput || normal.engine.reportRenderCounters ||
            normal.engine.baselineOutput || normal.engine.baselineSceneTarget ||
            normal.engine.pointShadowDiagnostic || normal.engine.pointGpuCapture ||
            !normal.engine.shadowQuality ||
            *normal.engine.shadowQuality != GEngine::ShadowQualityDesc{})
            return std::unexpected("Normal typed defaults changed");
        ++count;

        // Each former presence-based check is independently selectable. Cross-check
        // all other checks, then their simultaneous composition (no lost combinations).
        for (auto selected : checks)
        {
            Rbs::ValidationProfile profile;
            profile.checks = {selected};
            for (auto other : checks)
            {
                if (normal.validation.Includes(other) ||
                    profile.Includes(other) != (selected == other))
                    return std::unexpected("Typed check selection is not independent");
                ++count;
            }
        }
        Rbs::LaunchConfig combined;
        combined.validation.checks.assign(checks.begin(), checks.end());
        combined.startupMesh = Rbs::StartupMesh::Barrel;
        for (auto check : checks)
        {
            if (!combined.validation.Includes(check))
                return std::unexpected("Combined legacy workloads lost a check");
            ++count;
        }
        if (combined.startupMesh != Rbs::StartupMesh::Barrel)
            return std::unexpected("Explicit async mesh startup selection changed");
        ++count;

        // OFF/present-but-not-ON formerly selected unobserved performance, not
        // the normal app. Preserve both observation lanes and admission/full modes.
        for (auto observation : {Rbs::Observation::Unobserved, Rbs::Observation::Observed})
            for (auto workload : {Rbs::AsyncWorkload::Full, Rbs::AsyncWorkload::AdmissionOnly})
            {
                Rbs::LaunchConfig launch;
                launch.validation.asyncPerformance =
                    Rbs::AsyncPerformanceProfile{observation, workload};
                if (!launch.AsyncPerformanceEnabled() ||
                    launch.validation.asyncPerformance->observation != observation ||
                    launch.validation.asyncPerformance->workload != workload)
                    return std::unexpected("Async observation/admission transport changed");
                ++count;
            }

        for (const auto* value : {"", "relative/alpha.png", "C:/fixture/output"})
        {
            Rbs::LaunchConfig source;
            source.validation.materialAlphaFixture = value;
            source.validation.subscriptionOutput = value;
            source.engine.assetRootUtf8 = value;
            source.engine.passTimingOutput = value;
            source.engine.baselineOutput = value;
            const auto owned = source;
            source = {};
            if (owned.validation.materialAlphaFixture != value ||
                owned.validation.subscriptionOutput != value ||
                owned.engine.assetRootUtf8 != value || owned.engine.passTimingOutput != value ||
                owned.engine.baselineOutput != value)
                return std::unexpected(
                    "Explicit empty/relative/absolute paths must remain owned values");
            ++count;
        }
        combined.validation.materialComparison = Rbs::MaterialComparison::Floor;
        combined.engine.passTiming = true;
        combined.engine.reportRenderCounters = true;
        combined.engine.baselineSceneTarget = true;
        combined.engine.pointShadowDiagnostic = true;
        combined.engine.pointGpuCapture = true;
        if (combined.validation.materialComparison != Rbs::MaterialComparison::Floor ||
            !combined.engine.passTiming || !combined.engine.reportRenderCounters ||
            !combined.engine.baselineSceneTarget || !combined.engine.pointShadowDiagnostic ||
            !combined.engine.pointGpuCapture)
            return std::unexpected("Explicit Engine diagnostic/material options changed");
        ++count;

        // Pure legacy value parsing still has its original errors/precedence; RBS
        // supplies a typed descriptor and never calls the environment adapter.
        for (const auto* value : {"0", "8193", "12junk", "", "-1"})
        {
            Rbs::LaunchConfig launch;
            launch.engine.shadowQuality = GEngine::ParseShadowQuality(nullptr, value);
            if (launch.engine.shadowQuality)
                return std::unexpected("Invalid shadow resolution lost its deferred typed error");
            ++count;
        }
        const auto override = GEngine::ParseShadowQuality("Low", "2048");
        if (!override || override->quality != GEngine::ShadowQuality::Custom ||
            override->customResolution != 2048)
            return std::unexpected("Explicit shadow resolution precedence changed");
        ++count;
        if (GEngine::ParseShadowQuality("invalid", nullptr))
            return std::unexpected("Invalid shadow quality lost its typed error");
        ++count;
        return count;
    }
}
