#pragma once

#include "Core/LaunchConfig.h"
#include "Physics/PhysicsBackend.h"
#include <optional>
#include <expected>
#include <span>
#include <string_view>
#include <string>
#include <vector>
#include <array>

namespace Rbs
{
    enum class RbsScenePreset
    {
        GeometryGallery,
        SphereDiamond,
        SphereLattice,
        BoxStack,
        SphereBoxStack,
        DistancePendulum,
        DistanceChain,
        DistanceChainContact
    };

    struct SceneSelection
    {
        RbsScenePreset scene = RbsScenePreset::SphereBoxStack;
        unsigned chainLinks = 8;

        [[nodiscard]] constexpr bool IsDistanceScene() const noexcept
        {
            return scene == RbsScenePreset::DistancePendulum || scene == RbsScenePreset::DistanceChain ||
                   scene == RbsScenePreset::DistanceChainContact;
        }
        constexpr bool operator==(const SceneSelection&) const = default;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            if (chainLinks != 4 && chainLinks != 8 && chainLinks != 16) return false;
            switch (scene)
            {
            case RbsScenePreset::GeometryGallery:
            case RbsScenePreset::SphereDiamond:
            case RbsScenePreset::SphereLattice:
            case RbsScenePreset::BoxStack:
            case RbsScenePreset::SphereBoxStack:
            case RbsScenePreset::DistancePendulum:
            case RbsScenePreset::DistanceChain: return true;
            case RbsScenePreset::DistanceChainContact: return chainLinks == 8;
            }
            return false;
        }
    };

    // Owner-editable ordinary startup default. DistanceChain admits 4/8/16 links;
    // DistanceChainContact admits 8. Valid --scene/--links fields override these
    // values before construction. LaunchConfig{} retains GeometryGallery below.
    inline constexpr SceneSelection DefaultSceneSelection{RbsScenePreset::DistanceChain, 8};
    static_assert(DefaultSceneSelection.IsValid(), "Invalid RBS default scene/link selection");

    enum class RbsEnvironmentPreset { Qualification, InteractiveSandbox };

    // Presentation geometry only. Frozen qualification fixtures do not consume it.
    struct SandboxEnvironment
    {
        struct Box
        {
            const char* name;
            std::array<float, 3> center, size;
            bool floor;
        };
        static constexpr float Width = 160.f, Depth = 160.f, FloorThickness = 1.f;
        static constexpr float WallHeight = 12.f, WallThickness = 1.f;
        static constexpr float FloorTop = FloorThickness * .5f;
        static constexpr std::array<Box, 5> Boxes()
        {
            constexpr float x = (Width - WallThickness) * .5f;
            constexpr float z = (Depth - WallThickness) * .5f;
            constexpr float y = FloorTop + WallHeight * .5f;
            return {{{"Sandbox Floor", {0, 0, 0}, {Width, FloorThickness, Depth}, true},
                     {"Sandbox Wall -X", {-x, y, 0}, {WallThickness, WallHeight, Depth}, false},
                     {"Sandbox Wall +X", {x, y, 0}, {WallThickness, WallHeight, Depth}, false},
                     {"Sandbox Wall -Z", {0, y, -z}, {Width - 2 * WallThickness, WallHeight, WallThickness}, false},
                     {"Sandbox Wall +Z", {0, y, z}, {Width - 2 * WallThickness, WallHeight, WallThickness}, false}}};
        }
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

    struct RestingContactDiagnostic
    {
        Observation observation = Observation::Unobserved;
        std::string latticePath;
        std::string outputDirectory;
    };

    struct LaunchConfig
    {
        GEngine::EngineLaunchConfig engine;
        GEngine::PhysicsBackend m_Backend{GEngine::PhysicsBackend::Native};
        unsigned m_BackendSmokeTicks{}; // Explicit bounded safety check; ordinary owner run is unlimited.
        bool m_BackendSwitchChecks{};
        std::string joltParameterProfile, joltParameterOutput; // Opt-in experimental campaign, never qualification.
        unsigned joltParameterTicks{1800};
        bool joltParameterCapture{};
        RbsScenePreset scene = RbsScenePreset::GeometryGallery;
        StartupMesh startupMesh = StartupMesh::None;
        unsigned chainLinks = 8;
        RbsEnvironmentPreset environment = RbsEnvironmentPreset::InteractiveSandbox;
        ValidationProfile validation;
        std::optional<RestingContactDiagnostic> restingContactDiagnostic;

        [[nodiscard]] bool IsDistanceScene() const noexcept;
        [[nodiscard]] bool InteractiveSandboxEnabled() const noexcept;
        [[nodiscard]] bool ValidSceneConfiguration() const;
        [[nodiscard]] bool AsyncPerformanceEnabled() const noexcept;
        [[nodiscard]] bool ValidRestingContactDiagnostic() const;
    };
    const char* SceneName(RbsScenePreset) noexcept;
    [[nodiscard]] LaunchConfig SelectedLaunchConfig(SceneSelection = DefaultSceneSelection);
    [[nodiscard]] std::expected<LaunchConfig, std::string> ParseSceneArguments(
        std::span<const std::string_view>, LaunchConfig = SelectedLaunchConfig());
    [[nodiscard]] std::expected<LaunchConfig, std::string> ReadLaunchConfig(int argc, char* const argv[]);
    // Bootstrap supplies typed values, then owns a const snapshot until after app
    // teardown. No member is populated from the process environment.
}
