#pragma once

#include "Core/LaunchConfig.h"
#include <optional>
#include <string>
#include <vector>

namespace Rbs
{
    enum class RbsScenePreset
    {
        GeometryGallery,
        SphereDiamond,
        SphereLattice,
        BoxStack,
        SphereBoxStack
    };

    enum class ValidationCheck
    {
        SceneAuthoring,
        ResourceOwnership,
        GeometryTemplates,
        ParametricGeometry,
        GeometryAuthoring,
        MaterialAuthoring,
        ShaderDescriptions,
        ShaderReload,
        AsyncResources,
        Subscriptions,
        InputRouting
    };

    enum class Observation
    {
        Unobserved,
        Observed
    };
    enum class AsyncWorkload
    {
        Full,
        AdmissionOnly
    };
    enum class MaterialComparison
    {
        Authored,
        Floor
    };
    enum class StartupMesh
    {
        None,
        Barrel
    };

    struct AsyncPerformanceProfile
    {
        Observation observation = Observation::Unobserved;
        AsyncWorkload workload = AsyncWorkload::Full;
    };

    struct ValidationProfile
    {
        // An empty check set is the normal owner run. Explicit combinations retain
        // the original independent check selection and fixed execution order.
        std::vector<ValidationCheck> checks;
        std::optional<AsyncPerformanceProfile> asyncPerformance;
        std::optional<std::string> materialAlphaFixture;
        MaterialComparison materialComparison = MaterialComparison::Authored;
        std::optional<std::string> subscriptionOutput;

        [[nodiscard]] bool Includes(ValidationCheck check) const noexcept;
    };

    struct LaunchConfig
    {
        GEngine::EngineLaunchConfig engine;
        RbsScenePreset scene = RbsScenePreset::GeometryGallery;
        StartupMesh startupMesh = StartupMesh::None;
        ValidationProfile validation;

        [[nodiscard]] bool AsyncPerformanceEnabled() const noexcept;
    };
    // Bootstrap supplies typed values, then owns a const snapshot until after app
    // teardown. No member is populated from the process environment.
}
