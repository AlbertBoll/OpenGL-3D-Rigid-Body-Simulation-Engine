#pragma once

#include "Core/LaunchConfig.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/Constraints/BallSocketJoint.h"
#include "Math/Math.h"
#include <optional>
#include <expected>
#include <span>
#include <string_view>
#include <string>
#include <vector>
#include <array>

namespace GEngine {
    struct PassiveRagdollDesc;
    struct RagdollBodyDesc;
    struct RagdollError;
    enum class RagdollBone : std::uint8_t;
    enum class ShapeType;
}
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
        DistanceChainContact,
        BallSocketSwing,
        BallSocketPair,
        BallSocketContact,
        BallSocketContactEnabled,
        HingeBasic,
        HingeQuaternion,
        HingeLimited,
        CvFree,
        CvLimited,
        PassiveRagdoll,
        MotorFree, MotorContact, MotorLimit
    };

    constexpr bool IsMotorScene(RbsScenePreset scene) noexcept
    {
        return scene == RbsScenePreset::MotorFree || scene == RbsScenePreset::MotorContact ||
               scene == RbsScenePreset::MotorLimit;
    }

    struct SceneSelection
    {
        RbsScenePreset scene = RbsScenePreset::SphereBoxStack;
        unsigned chainLinks = 8;

        [[nodiscard]] constexpr bool IsDistanceScene() const noexcept
        {
            return scene == RbsScenePreset::DistancePendulum || scene == RbsScenePreset::DistanceChain ||
                   scene == RbsScenePreset::DistanceChainContact;
        }
        [[nodiscard]] constexpr bool IsBallSocketScene() const noexcept
        {
            return scene==RbsScenePreset::BallSocketSwing || scene==RbsScenePreset::BallSocketPair || scene==RbsScenePreset::BallSocketContact || scene==RbsScenePreset::BallSocketContactEnabled;
        }
        [[nodiscard]] constexpr bool IsAngularScene() const noexcept
        {
            return scene == RbsScenePreset::HingeBasic || scene == RbsScenePreset::HingeQuaternion ||
                   scene == RbsScenePreset::HingeLimited || scene == RbsScenePreset::CvFree ||
                   scene == RbsScenePreset::CvLimited || IsMotorScene(scene);
        }
        [[nodiscard]] constexpr bool IsRagdollScene() const noexcept
        { return scene==RbsScenePreset::PassiveRagdoll; }
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
            case RbsScenePreset::DistanceChain:
            case RbsScenePreset::BallSocketSwing:
            case RbsScenePreset::BallSocketPair:
            case RbsScenePreset::BallSocketContact:
            case RbsScenePreset::BallSocketContactEnabled:
            case RbsScenePreset::HingeBasic:
            case RbsScenePreset::HingeQuaternion:
            case RbsScenePreset::HingeLimited:
            case RbsScenePreset::CvFree:
            case RbsScenePreset::MotorFree:
            case RbsScenePreset::MotorContact:
            case RbsScenePreset::MotorLimit:
            case RbsScenePreset::PassiveRagdoll:
            case RbsScenePreset::CvLimited: return true;
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

    // Phase 28 authoring is app-owned session data, separate from the V1
    // SceneSelection drag payload and the borrowed immutable LaunchConfig.
    enum class AngularVariant : std::uint8_t { Air, Contact };
    enum class AngularBodyKey : std::uint8_t { Anchor, Bar, Floor, Load };
    enum class AngularJointKey : std::uint8_t { Primary };
    enum class AngularField : std::uint8_t {
        Case, Variant, Identity, Body, Mass, Scale, Gravity, Frame, Limits,
        Anchor, Material, Action, Backend, Generation
    };
    struct AngularPacketError {
        AngularField field{AngularField::Case};
        std::string_view message{"Invalid angular packet"};
    };
    struct AngularBodyPacket {
        AngularBodyKey key{AngularBodyKey::Anchor};
        GEngine::Math::Vec3f center{}, halfExtents{.25f};
        GEngine::Math::Quat orientation{1.f,0.f,0.f,0.f};
        GEngine::Math::Vec3f initialLinearVelocity{}, initialAngularVelocity{};
        float authoredMass{1.f}, friction{.5f}, restitution{};
        float linearDamping{}, angularDamping{};
        bool sphere{}, kinematic{};
        friend bool operator==(const AngularBodyPacket&, const AngularBodyPacket&) = default;
    };
    struct AngularJointPacket {
        AngularJointKey key{AngularJointKey::Primary};
        AngularBodyKey bodyA{AngularBodyKey::Anchor}, bodyB{AngularBodyKey::Bar};
        GEngine::Math::Vec3f localAnchorA{0.f,-.25f,0.f}, localAnchorB{0.f,1.f,0.f};
        GEngine::Math::Quat localFrameA{1.f,0.f,0.f,0.f}, localFrameB{1.f,0.f,0.f,0.f};
        float lower{-GEngine::Math::Pi}, upper{GEngine::Math::Pi};
        float halfAngleU{GEngine::Math::Pi/4.f}, halfAngleV{GEngine::Math::Pi/4.f};
        bool limitsEnabled{}, collideConnected{};
        std::optional<GEngine::HingeMotorSettings> motor;
        friend bool operator==(const AngularJointPacket&, const AngularJointPacket&) = default;
    };
    struct AngularPacket {
        RbsScenePreset scene{RbsScenePreset::HingeBasic};
        AngularVariant variant{AngularVariant::Air};
        std::uint32_t referenceRevision{1};
        std::string_view sourceSha256{"6237bcafd5bb648ade5a36563e1ac1599b21d02fa29553b320f7271b66da3c56"};
        std::string_view fixtureSha256{"bc91dde2019284820bd80c209fe1bae8c58ccdf83a4ed595ca28430dfaeae2c5"};
        std::array<AngularBodyPacket,3> bodies{};
        std::uint8_t bodyCount{2};
        GEngine::Math::Vec3f gravity{0.f,-10.f,0.f};
        double fixedStepSeconds{1.0/60.0};
        AngularJointPacket joint{};
        std::array<GEngine::Math::Vec3f,2> scheduledImpulses{};
        friend bool operator==(const AngularPacket&, const AngularPacket&) = default;
    };
    [[nodiscard]] constexpr bool IsAngularScene(RbsScenePreset scene) noexcept
    { return SceneSelection{scene,8}.IsAngularScene(); }
    [[nodiscard]] std::expected<AngularPacket,AngularPacketError> ReferenceAngularPacket(
        RbsScenePreset, AngularVariant = AngularVariant::Air);
    void SetAngularBarScale(AngularPacket&, float) noexcept;
    void SetAngularBarMass(AngularPacket&, float) noexcept;
    void SetAngularBodyPosition(AngularPacket&, AngularBodyKey, const GEngine::Math::Vec3f&) noexcept;
    void SetAngularBodyRotation(AngularPacket&, AngularBodyKey, const GEngine::Math::Quat&) noexcept;
    void SetAngularBodyHalfExtents(AngularPacket&, AngularBodyKey, const GEngine::Math::Vec3f&) noexcept;
    void SetAngularAnchorSphere(AngularPacket&, bool) noexcept;
    void SetAngularJointFrame(AngularPacket&, const GEngine::Math::Quat&) noexcept;
    [[nodiscard]] std::expected<void,AngularPacketError> ValidateAngularPacket(
        const AngularPacket&, GEngine::PhysicsBackend, bool requireFrozenFixture = false);

    // The complete bounded descriptor is declared in Physics/PassiveRagdoll.h.
    using RagdollPacket = GEngine::PassiveRagdollDesc;
    [[nodiscard]] RagdollPacket ReferenceRagdollPacket();
    [[nodiscard]] RagdollPacket TiltRagdollPacket();
    [[nodiscard]] std::expected<void,GEngine::RagdollError> ValidateRagdollPacket(
        const RagdollPacket&, GEngine::PhysicsBackend);
    [[nodiscard]] GEngine::Math::Vec3f RagdollRenderScale(const GEngine::RagdollBodyDesc&) noexcept;
    void SetRagdollBodyPose(RagdollPacket&, GEngine::RagdollBone, const GEngine::Math::Vec3f&, const GEngine::Math::Quat&) noexcept;
    void SetRagdollShapeDimensions(RagdollPacket&, GEngine::RagdollBone, GEngine::ShapeType,
        const GEngine::Math::Vec3f& halfExtents, float radius, float halfLength) noexcept;
    void SetRagdollHalfExtents(RagdollPacket&, GEngine::RagdollBone, const GEngine::Math::Vec3f&) noexcept;
    void SetRagdollColliderCenter(RagdollPacket&, GEngine::RagdollBone, const GEngine::Math::Vec3f&) noexcept;

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
        GEngine::PhysicsBackend m_Backend{GEngine::DefaultPhysicsBackend};
        bool m_BackendExplicit{};
        unsigned m_BackendSmokeTicks{}; // Explicit bounded safety check; ordinary owner run is unlimited.
        bool m_BackendSwitchChecks{};
        std::string joltParameterProfile, joltParameterOutput; // Opt-in experimental campaign, never qualification.
        unsigned joltParameterTicks{1800};
        bool joltParameterCapture{};
        RbsScenePreset scene = RbsScenePreset::GeometryGallery;
        AngularVariant angularVariant{AngularVariant::Air};
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
