#include "RbsLaunchConfig.h"
#include <algorithm>
#include <filesystem>
#include <charconv>
#ifdef GENGINE_COLLIDER_PASSIVE_OBSERVATION
#include "../tests/ColliderPassiveObservation.h"
#endif
#if defined(GENGINE_PHASE26_QUALIFICATION) || defined(GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS)
#include "../tests/DistanceJointChecks.h"
#endif
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
#include "../tests/SandboxChecks.h"
#endif

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

    bool LaunchConfig::InteractiveSandboxEnabled() const noexcept
    {
#if defined(GENGINE_PHASE26_QUALIFICATION) || defined(GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS) || \
    defined(GENGINE_PHASE25_QUALIFICATION) || defined(GENGINE_PHASE24_QUALIFICATION) || \
    defined(GENGINE_PHASE24_VALIDATION) || defined(GENGINE_PHASE24_OUTLINE_QUALIFICATION) || \
    defined(GENGINE_PHASE24_COLLIDER_VALIDATION) || defined(GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC) || \
    defined(GENGINE_COLLIDER_PASSIVE_OBSERVATION) || defined(GENGINE_RBS_SCENE_VALIDATION) || \
    defined(GENGINE_CAMERA_VALIDATION) || defined(GENGINE_LIFECYCLE_VALIDATION) || \
    defined(GENGINE_INPUT_VALIDATION) || defined(GENGINE_COLLIDER_EDITOR_SMOKE) || \
    defined(GENGINE_MESH_HIGHLIGHT_AUDIT) || defined(GENGINE_PHASE23_DEBUG_GALLERY) || \
    defined(GENGINE_PHASE23_DEBUG_REPRO) || defined(GENGINE_PHASE23_DEBUG_VALIDATION) || \
    defined(GENGINE_COLLISION_SHAPE_VALIDATION) || defined(GENGINE_RBS_MODULARITY_VALIDATION) || \
    defined(GENGINE_RBS_TRANSFORM_VALIDATION) || defined(GENGINE_COLLIDER_ADMISSION_VALIDATION) || \
    defined(GENGINE_RENDER_BASELINE)
        return false;
#else
        return environment == RbsEnvironmentPreset::InteractiveSandbox &&
            !restingContactDiagnostic && validation.checks.empty() &&
            !validation.asyncPerformance && !validation.materialAlphaFixture &&
            !validation.subscriptionOutput && !engine.baselineOutput &&
            !engine.pointShadowDiagnostic && !engine.pointGpuCapture;
#endif
    }

    bool LaunchConfig::ValidRestingContactDiagnostic() const
    {
        if (!restingContactDiagnostic) return true;
        const auto& d=*restingContactDiagnostic;
        return validation.checks.empty() && !validation.asyncPerformance &&
            !validation.materialAlphaFixture && !validation.subscriptionOutput &&
            startupMesh==StartupMesh::None &&
            (d.observation==Observation::Unobserved || d.observation==Observation::Observed) &&
            std::filesystem::path(d.latticePath).is_absolute() &&
            std::filesystem::path(d.outputDirectory).is_absolute();
    }
    bool LaunchConfig::IsDistanceScene() const noexcept
    { return scene==RbsScenePreset::DistancePendulum || scene==RbsScenePreset::DistanceChain || scene==RbsScenePreset::DistanceChainContact; }
    bool LaunchConfig::ValidSceneConfiguration() const
    {
        if(!IsDistanceScene()) return true;
        return (chainLinks==4 || chainLinks==8 || chainLinks==16) &&
            (scene!=RbsScenePreset::DistanceChainContact || chainLinks==8) &&
            startupMesh==StartupMesh::None && !restingContactDiagnostic && validation.checks.empty() &&
            !validation.asyncPerformance && !validation.materialAlphaFixture && !validation.subscriptionOutput;
    }
    const char* SceneName(RbsScenePreset s) noexcept
    {
        switch(s) {
        case RbsScenePreset::GeometryGallery:return "geometry-gallery";
        case RbsScenePreset::SphereDiamond:return "sphere-diamond";
        case RbsScenePreset::SphereLattice:return "sphere-lattice";
        case RbsScenePreset::BoxStack:return "box-stack";
        case RbsScenePreset::SphereBoxStack:return "sphere-box-stack";
        case RbsScenePreset::DistancePendulum:return "distance-pendulum";
        case RbsScenePreset::DistanceChain:return "distance-chain";
        case RbsScenePreset::DistanceChainContact:return "distance-chain-contact";
        case RbsScenePreset::BallSocketSwing:return "ball-socket-s";
        case RbsScenePreset::BallSocketPair:return "ball-socket-p";
        case RbsScenePreset::BallSocketContact:return "ball-socket-g";
        case RbsScenePreset::BallSocketContactEnabled:return "ball-socket-g-contact";
        }
        return "invalid";
    }
    LaunchConfig SelectedLaunchConfig(SceneSelection selected)
    {
        LaunchConfig launch;
        launch.scene = selected.scene;
        launch.chainLinks = selected.chainLinks;
        return launch;
    }

    std::expected<LaunchConfig,std::string> ParseSceneArguments(
        std::span<const std::string_view> args, LaunchConfig launch)
    {
        if (launch.restingContactDiagnostic && !args.empty())
            return std::unexpected("Dedicated contact diagnostic does not admit scene overrides");
        bool sceneSeen=false, linksSeen=false, backendSeen=false, smokeSeen=false, switchChecksSeen=false;
        for(std::size_t i=0;i<args.size();i+=2) {
            if(i+1==args.size()) return std::unexpected("Missing value for startup option");
            const auto option=args[i], value=args[i+1];
            if(option=="--scene" && !sceneSeen) {
                sceneSeen=true; bool found=false;
                for(auto scene:{RbsScenePreset::GeometryGallery,RbsScenePreset::SphereDiamond,
                    RbsScenePreset::SphereLattice,RbsScenePreset::BoxStack,RbsScenePreset::SphereBoxStack,
                    RbsScenePreset::DistancePendulum,RbsScenePreset::DistanceChain,RbsScenePreset::DistanceChainContact,
                    RbsScenePreset::BallSocketSwing,RbsScenePreset::BallSocketPair,RbsScenePreset::BallSocketContact,RbsScenePreset::BallSocketContactEnabled})
                    if(value==SceneName(scene)) { launch.scene=scene; found=true; break; }
                if(!found)return std::unexpected("Unknown or unimplemented scene preset");
            } else if(option=="--links" && !linksSeen) {
                linksSeen=true;
                auto parsed=std::from_chars(value.data(),value.data()+value.size(),launch.chainLinks);
                if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size())
                    return std::unexpected("Invalid deterministic link count");
            } else if(option=="--backend" && !backendSeen) {
                backendSeen=true;
                if(value=="native")launch.m_Backend=GEngine::PhysicsBackend::Native;
                else if(value=="jolt")launch.m_Backend=GEngine::PhysicsBackend::Jolt;
                else return std::unexpected("Unknown Physics backend (native or jolt required)");
                if(!GEngine::PhysicsBackendAvailable(launch.m_Backend))return std::unexpected("Requested backend is not enabled in this build");
            } else if(option=="--backend-switch-checks" && !switchChecksSeen) {
                switchChecksSeen=true;
                if(value!="true")return std::unexpected("Backend switch checks require true");
                launch.m_BackendSwitchChecks=true;
            } else if(option=="--physics-smoke-ticks" && !smokeSeen) {
                smokeSeen=true;
                auto parsed=std::from_chars(value.data(),value.data()+value.size(),launch.m_BackendSmokeTicks);
                if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || launch.m_BackendSmokeTicks==0 || launch.m_BackendSmokeTicks>120)
                    return std::unexpected("Safety smoke requires 1..120 completed ticks");
            } else if(option=="--jolt-parameter-profile" && launch.joltParameterProfile.empty()) {
                if(value!="baseline" && value!="baseline-unobserved" && value!="threshold-025" && value!="restitution-050" && value!="velocity-16" && value!="velocity-32" && value!="position-4" && value!="position-8" && value!="sleep-off")return std::unexpected("Unknown experimental Jolt profile");
                launch.joltParameterProfile=value;
            } else if(option=="--jolt-parameter-output" && launch.joltParameterOutput.empty()) {
                launch.joltParameterOutput=value;
            } else if(option=="--jolt-parameter-ticks") {
                auto parsed=std::from_chars(value.data(),value.data()+value.size(),launch.joltParameterTicks);
                if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || launch.joltParameterTicks<1 || launch.joltParameterTicks>3600)return std::unexpected("Experimental budget requires 1..3600 ticks");
            } else if(option=="--jolt-parameter-capture" && value=="true") {launch.joltParameterCapture=true;
            } else return std::unexpected("Unknown or duplicate startup option");
        }
        if(!launch.joltParameterProfile.empty() && (!launch.InteractiveSandboxEnabled() || launch.m_Backend!=GEngine::PhysicsBackend::Jolt || launch.scene!=RbsScenePreset::SphereLattice || launch.joltParameterOutput.empty() || launch.m_BackendSmokeTicks || launch.m_BackendSwitchChecks))
            return std::unexpected("Jolt experiment requires isolated interactive SphereLattice/Jolt and an output directory");
        if(launch.joltParameterProfile.empty() && (!launch.joltParameterOutput.empty() || launch.joltParameterCapture || launch.joltParameterTicks!=1800))return std::unexpected("Experimental options require a profile");
        if(launch.m_BackendSwitchChecks && (!launch.InteractiveSandboxEnabled() ||
            !GEngine::PhysicsBackendAvailable(GEngine::PhysicsBackend::Jolt) ||
            launch.m_Backend!=GEngine::PhysicsBackend::Native || launch.m_BackendSmokeTicks!=120))
            return std::unexpected("Backend switch checks require Native startup, Jolt available, Sandbox and exactly 120 smoke ticks");
        if((linksSeen && (!launch.IsDistanceScene() || launch.scene==RbsScenePreset::DistancePendulum)) ||
            !SceneSelection{launch.scene, launch.chainLinks}.IsValid() ||
            !launch.ValidSceneConfiguration() || !launch.ValidRestingContactDiagnostic())
            return std::unexpected("Incompatible scene/link configuration (C4/C8/C16 or G8 required)");
        return launch;
    }

    std::expected<LaunchConfig, std::string> ReadLaunchConfig(int argc, char* const argv[])
    {
        if (argc < 1 || !argv || !argv[0])
            return std::unexpected("Executable path is unavailable");
        std::vector<std::string_view> arguments;
        for (int i = 1; i < argc; ++i)
        {
            if (!argv[i]) return std::unexpected("Null startup argument");
            arguments.emplace_back(argv[i]);
        }
        auto launch = SelectedLaunchConfig();
#ifdef GENGINE_COLLIDER_PASSIVE_OBSERVATION
        // This existing dedicated bootstrap owns its preset, including its
        // disabled-mode default. Ordinary scene overrides cannot replace it.
        if (!arguments.empty() || !ColliderPassive::Configure())
            return std::unexpected("Invalid passive diagnostic startup or scene override");
        launch.scene = ColliderPassive::config.preset;
        launch.chainLinks = 8;
#endif
#ifdef GENGINE_PHASE26_QUALIFICATION
        if (!Phase26::Configure(arguments))
            return std::unexpected("Invalid Phase26 qualification arguments");
        if (Phase26::session->target == 120 && !Phase26::Lifecycle())
        {
            Phase26::session->Fail("focused lifecycle/configuration checks");
            Phase26::Finish(false);
            return std::unexpected("Phase26 lifecycle/configuration checks failed");
        }
        launch.engine.assetRootUtf8 = (std::filesystem::absolute(argv[0]).parent_path()
            .parent_path().parent_path() / "assets").generic_string();
#endif
#ifdef GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS
        if (!DistanceChainImpulseChecks::Configure(arguments))
            return std::unexpected("Invalid Phase26 impulse presentation arguments");
        launch.engine.assetRootUtf8 = (std::filesystem::absolute(argv[0]).parent_path()
            .parent_path().parent_path() / "assets").generic_string();
#endif
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
        if (!SandboxChecks::Configure(arguments))
            return std::unexpected("Invalid Phase26 sandbox check arguments");
        SandboxChecks::ConfigureLaunch(launch);
        launch.engine.assetRootUtf8 = (std::filesystem::absolute(argv[0]).parent_path()
            .parent_path().parent_path() / "assets").generic_string();
#endif
        if (!launch.InteractiveSandboxEnabled())
            launch.environment = RbsEnvironmentPreset::Qualification;
        return ParseSceneArguments(arguments, std::move(launch));
    }

}
