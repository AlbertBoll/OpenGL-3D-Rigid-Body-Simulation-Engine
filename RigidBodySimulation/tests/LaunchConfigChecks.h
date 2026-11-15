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
        using Preset = Rbs::RbsScenePreset;
        static_assert(Rbs::SceneSelection{Preset::DistanceChain, 4}.IsValid());
        static_assert(!Rbs::SceneSelection{Preset::DistanceChainContact, 16}.IsValid());
        static_assert(!Rbs::SceneSelection{static_cast<Preset>(999), 8}.IsValid());
        const std::array<std::string_view,0> empty{};
        const auto selectedDefault = Rbs::ParseSceneArguments(empty);
        if (!selectedDefault || selectedDefault->scene != Rbs::DefaultSceneSelection.scene ||
            selectedDefault->chainLinks != Rbs::DefaultSceneSelection.chainLinks)
            return std::unexpected("Owner enum default was not selected");
        ++count;
        for (auto selection : {Rbs::SceneSelection{Preset::GeometryGallery, 8},
             {Preset::SphereDiamond, 8}, {Preset::SphereLattice, 8}, {Preset::BoxStack, 8},
             {Preset::SphereBoxStack, 8},
             {Preset::DistanceChain, 4}, {Preset::DistanceChain, 8},
             {Preset::DistanceChain, 16}, {Preset::DistanceChainContact, 8}})
        {
            const auto typed = Rbs::SelectedLaunchConfig(selection);
            const auto selected = Rbs::ParseSceneArguments(empty, typed);
            if (!selected || selected->scene != selection.scene ||
                selected->chainLinks != selection.chainLinks || !selected->ValidSceneConfiguration())
                return std::unexpected("Typed scene/link default changed");
            ++count;
        }
        for(auto preset : {Preset::GeometryGallery, Preset::SphereDiamond, Preset::SphereLattice,
            Preset::BoxStack, Preset::SphereBoxStack, Preset::DistanceChain, Preset::DistanceChainContact})
        {
            const std::array<std::string_view,2> args{"--scene",Rbs::SceneName(preset)};
            auto parsed=Rbs::ParseSceneArguments(args,Rbs::SelectedLaunchConfig({Preset::SphereBoxStack,8}));
            if(!parsed || parsed->scene!=preset || !Rbs::SceneSelection{parsed->scene,parsed->chainLinks}.IsValid())
                return std::unexpected("Seven-preset startup selection failed");
            ++count;
        }
        auto qualification=Rbs::SelectedLaunchConfig();
        qualification.environment=Rbs::RbsEnvironmentPreset::Qualification;
        if(qualification.InteractiveSandboxEnabled())
            return std::unexpected("Qualification environment enabled runtime sandbox");
        auto validationOnly=Rbs::SelectedLaunchConfig();
        validationOnly.validation.checks={Check::SceneAuthoring};
        if(validationOnly.InteractiveSandboxEnabled())
            return std::unexpected("Dedicated validation environment enabled runtime sandbox");
#ifdef GENGINE_PHASE26_QUALIFICATION
        if(Rbs::SelectedLaunchConfig().InteractiveSandboxEnabled())
            return std::unexpected("Frozen qualification binary enabled runtime sandbox");
#elif defined(GENGINE_PHASE26_SANDBOX_CHECKS)
        if(!Rbs::SelectedLaunchConfig().InteractiveSandboxEnabled())
            return std::unexpected("Ordinary sandbox profile disabled");
#endif
        const auto room=Rbs::SandboxEnvironment::Boxes();
        unsigned floors=0;
        for(const auto& box:room)
        {
            floors+=box.floor;
            if(!box.name || box.size[0]<=0 || box.size[1]<=0 || box.size[2]<=0)
                return std::unexpected("Invalid typed sandbox box descriptor");
        }
        if(room.size()!=5 || floors!=1)
            return std::unexpected("Sandbox room must contain one floor and four walls");
        count+=4;
        for(unsigned n:{4u,8u,16u}) {
            const std::string number=std::to_string(n);
            const std::array<std::string_view,4> args{"--scene","distance-chain","--links",number};
            auto selected=Rbs::ParseSceneArguments(args);
            if(!selected || !selected->IsDistanceScene() || selected->chainLinks!=n) return std::unexpected("Chain startup selection failed");
            auto overridden=Rbs::ParseSceneArguments(args, Rbs::SelectedLaunchConfig({Preset::DistanceChainContact, 8}));
            if (!overridden || overridden->scene != Preset::DistanceChain || overridden->chainLinks != n)
                return std::unexpected("CLI did not override enum selection");
            ++count;
        }
        auto ordinary=Rbs::ParseSceneArguments(empty, Rbs::SelectedLaunchConfig({Preset::SphereBoxStack, 8}));
        if(!ordinary || ordinary->scene!=Rbs::RbsScenePreset::SphereBoxStack) return std::unexpected("No-argument default changed");
        const std::array<std::string_view,2> linksOnly{"--links", "4"};
        auto partial=Rbs::ParseSceneArguments(linksOnly, Rbs::SelectedLaunchConfig({Preset::DistanceChain, 16}));
        if (!partial || partial->scene != Preset::DistanceChain || partial->chainLinks != 4)
            return std::unexpected("Link override did not retain enum scene");
        const std::array<std::string_view,2> sceneOnly{"--scene", "distance-chain-contact"};
        auto contact=Rbs::ParseSceneArguments(sceneOnly, Rbs::SelectedLaunchConfig({Preset::DistanceChain, 8}));
        if (!contact || contact->scene != Preset::DistanceChainContact || contact->chainLinks != 8 ||
            Rbs::ParseSceneArguments(sceneOnly, Rbs::SelectedLaunchConfig({Preset::DistanceChain, 16})))
            return std::unexpected("Final merged G8 link compatibility changed");
        const std::array<std::string_view,2> stackOnly{"--scene", "sphere-box-stack"};
        auto stack=Rbs::ParseSceneArguments(stackOnly, Rbs::SelectedLaunchConfig({Preset::DistanceChain, 16}));
        if (!stack || stack->scene != Preset::SphereBoxStack)
            return std::unexpected("Ordinary override did not replace enum chain");
        const std::array<std::string_view,4> reversed{"--links", "8", "--scene", "distance-chain-contact"};
        if (!Rbs::ParseSceneArguments(reversed)) return std::unexpected("Option order changed selection");
        count += 5;
        Rbs::LaunchConfig diagnostic;
        diagnostic.scene = Preset::SphereBoxStack;
        diagnostic.engine.assetRootUtf8 = "C:/fixture/assets";
        diagnostic.restingContactDiagnostic = Rbs::RestingContactDiagnostic{
            Rbs::Observation::Observed, "C:/fixture/lattice", "C:/fixture/output"};
        auto preserved=Rbs::ParseSceneArguments(empty, diagnostic);
        if (!preserved || preserved->scene != diagnostic.scene ||
            preserved->engine.assetRootUtf8 != diagnostic.engine.assetRootUtf8 ||
            !preserved->restingContactDiagnostic ||
            preserved->restingContactDiagnostic->outputDirectory != "C:/fixture/output" ||
            Rbs::ParseSceneArguments(stackOnly, diagnostic) || Rbs::ParseSceneArguments(sceneOnly, diagnostic))
            return std::unexpected("Dedicated diagnostic configuration/override protection changed");
        auto incompatible=Rbs::SelectedLaunchConfig({Preset::DistanceChain, 8});
        incompatible.startupMesh=Rbs::StartupMesh::Barrel;
        if (Rbs::ParseSceneArguments(empty, incompatible))
            return std::unexpected("Chain accepted incompatible mesh startup");
        incompatible.startupMesh=Rbs::StartupMesh::None;
        incompatible.validation.checks={Check::MaterialAuthoring};
        if (Rbs::ParseSceneArguments(empty, incompatible) ||
            Rbs::ParseSceneArguments(empty, Rbs::SelectedLaunchConfig({static_cast<Preset>(999), 8})))
            return std::unexpected("Invalid typed scene/validation combination accepted");
        count += 3;
        char executable[] = "RigidBodySimulation";
        char* nullArgument[]{executable, nullptr};
        if (Rbs::ReadLaunchConfig(0, nullptr) || Rbs::ReadLaunchConfig(1, nullptr) ||
            Rbs::ReadLaunchConfig(2, nullArgument))
            return std::unexpected("Invalid bootstrap argument identity accepted");
        ++count;
        for(auto args:{std::vector<std::string_view>{"--scene","future"},
            {"--scene","distance-chain","--links","8junk"}, {"--links","4"},
            {"--scene","distance-chain-contact","--links","16"},
            {"--scene","distance-chain","--links","0"}, {"--scene"},
            {"--scene","distance-chain","--scene","distance-chain"},
            {"--scene","distance-chain","--links","4","--links","8"},
            {"--scene","distance-chain","--links","-1"},
            {"--scene","distance-chain","--links","4294967296"},
            {"--scene","distance-chain","--links",""},
            {"--unknown","8"}, {"--links"}}) {
            if(Rbs::ParseSceneArguments(args, Rbs::SelectedLaunchConfig({Preset::SphereBoxStack, 8})))
                return std::unexpected("Malformed startup accepted");
            ++count;
        }
        return count;
    }
}
