#include "RbsLaunchConfig.h"
#include <algorithm>
#include <filesystem>

namespace Rbs
{
    bool ValidationProfile::Includes(ValidationCheck check) const noexcept
    {
        return std::find(checks.begin(), checks.end(), check) != checks.end();
    }

    bool LaunchConfig::AsyncPerformanceEnabled() const noexcept
    {
        return validation.asyncPerformance.has_value();
    }

    bool LaunchConfig::ValidRestingContactDiagnostic() const
    {
        if (!restingContactDiagnostic) return true;
        const auto& d=*restingContactDiagnostic;
        return !targetedEntityDiagnostic && validation.checks.empty() && !validation.asyncPerformance &&
            !validation.materialAlphaFixture && !validation.subscriptionOutput &&
            startupMesh==StartupMesh::None &&
            (d.observation==Observation::Unobserved || d.observation==Observation::Observed) &&
            std::filesystem::path(d.latticePath).is_absolute() &&
            std::filesystem::path(d.outputDirectory).is_absolute();
    }
    bool LaunchConfig::ValidTargetedEntityDiagnostic() const
    {
        if (!targetedEntityDiagnostic) return true;
        return !restingContactDiagnostic && validation.checks.empty() && !validation.asyncPerformance &&
            !validation.materialAlphaFixture && !validation.subscriptionOutput &&
            startupMesh==StartupMesh::None && scene==RbsScenePreset::SphereLattice &&
            targetedEntityDiagnostic->entityName=="wood_sphere158" &&
            std::filesystem::path(targetedEntityDiagnostic->outputDirectory).is_absolute();
    }
}
