#include "RbsLaunchConfig.h"
#include "RbsScenePresetPayload.h"
#include <array>
#include <cstdio>

int main()
{
    using namespace Rbs;
    using Preset = RbsScenePreset;
    constexpr BookCase expected[]{
        {"sphere-box-stack", Preset::SphereBoxStack, 8},
        {"distance-pendulum", Preset::DistancePendulum, 8},
        {"distance-chain-4", Preset::DistanceChain, 4},
        {"distance-chain-8", Preset::DistanceChain, 8},
        {"distance-chain-16", Preset::DistanceChain, 16},
        {"distance-chain-contact-8", Preset::DistanceChainContact, 8},
        {"ball-socket-s", Preset::BallSocketSwing, 8},
        {"ball-socket-p", Preset::BallSocketPair, 8},
        {"ball-socket-g", Preset::BallSocketContact, 8},
        {"ball-socket-g-contact", Preset::BallSocketContactEnabled, 8},
        {"hinge-basic", Preset::HingeBasic, 8},
        {"hinge-quaternion", Preset::HingeQuaternion, 8},
        {"hinge-limited", Preset::HingeLimited, 8},
        {"cv-free", Preset::CvFree, 8},
        {"cv-limited", Preset::CvLimited, 8},
        {"passive-ragdoll", Preset::PassiveRagdoll, 8},
        {"motor-free", Preset::MotorFree, 8},
        {"motor-contact", Preset::MotorContact, 8},
        {"motor-limit", Preset::MotorLimit, 8},
        {"movers", Preset::Movers, 8},
    };
    static_assert(std::size(expected) == BookCases.size());
    auto parse = [](std::initializer_list<std::string_view> args) {
        return ParseSceneArguments({args.begin(), args.size()});
    };
    for (const auto& item : expected) {
        auto launch = parse({"--scene", "book-conformance", "--case", item.key});
        auto reversed = parse({"--case", item.key, "--backend", "jolt", "--scene", "book-conformance"});
        for (const auto* result : {&launch, &reversed}) {
            if (!*result) return 1;
            const auto& config = **result;
            SceneSelection selection{config.scene, config.chainLinks, config.fromBook};
            const auto* entry = selection.RequestedCase();
            if (!entry || std::string_view(entry->key) != item.key || selection.scene != item.provider ||
                selection.chainLinks != item.chainLinks || !selection.IsValid() ||
                config.angularVariant != AngularVariant::Air || !config.ValidSceneConfiguration()) return 2;
            const auto typed = SelectedLaunchConfig(selection);
            if (typed.scene != item.provider || typed.chainLinks != item.chainLinks || !typed.fromBook) return 8;
        }
        if (!reversed->m_BackendExplicit || reversed->m_Backend != GEngine::PhysicsBackend::Jolt) return 3;
    }
    for (auto rejected : {
        parse({"--scene", "book-conformance", "--links", "4"}), parse({"--case", "cv-free"}),
        parse({"--case", "cv-free", "--scene", "cv-free"}),
        parse({"--scene", "book-conformance", "--case", "unknown"}),
        parse({"--scene", "book-conformance", "--case", "cv-free", "--case", "cv-limited"}),
        parse({"--scene", "book-conformance", "--case", "distance-chain-4", "--links", "8"}),
        parse({"--scene", "book-conformance", "--case", "cv-limited", "--variant", "contact"}),
        parse({"--scene", "book-conformance", "--case", "motor-free", "--motor-case", "contact"})})
        if (rejected) return 4;

    const auto ordinary = parse({});
    const auto sphere = parse({"--scene", "sphere-box-stack"});
    const auto native = parse({"--scene", "book-conformance", "--case", "cv-limited", "--backend", "native"});
    if (!ordinary || ordinary->scene != DefaultSceneSelection.scene || ordinary->chainLinks != DefaultSceneSelection.chainLinks ||
        ordinary->fromBook || LaunchConfig{}.scene != Preset::GeometryGallery || !sphere || sphere->fromBook ||
        sphere->scene != Preset::SphereBoxStack || !native || native->m_Backend != GEngine::PhysicsBackend::Native) return 5;
    const auto integrated = parse({"--scene", "book-conformance"});
    if (!integrated || integrated->scene != Preset::BookConformance || integrated->fromBook ||
        !SceneSelection{Preset::BookConformance, 8}.IsValid() ||
        SceneSelection{Preset::GeometryGallery, 8, true}.IsValid()) return 6;

    // The ordinary in-process drag protocol also accepts bytes from its original V1 layout.
    const std::array<std::uint32_t, 3> original{1, static_cast<std::uint32_t>(Preset::DistanceChain), 16};
    const auto legacy = DecodeScenePreset(original.data(), sizeof(original));
    if (!legacy || legacy->scene != Preset::DistanceChain || legacy->chainLinks != 16 || legacy->fromBook) return 7;
    std::printf("PASS %zu book cases, both option orders, invalid combinations, both backends, defaults and V1 payload\n", std::size(expected));
    return 0;
}
