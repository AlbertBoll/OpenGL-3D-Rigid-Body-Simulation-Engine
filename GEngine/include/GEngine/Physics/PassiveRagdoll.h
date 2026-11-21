#pragma once

#include "PhysicsWorld.h"

namespace GEngine
{
    enum class RagdollBone : std::uint8_t { Head, Torso, LeftArm, RightArm, LeftLeg, RightLeg };
    enum class RagdollJoint : std::uint8_t { Neck, LeftShoulder, RightShoulder, LeftHip, RightHip };
    enum class RagdollAnchorResizePolicy { PreserveLocalMeters, ScaleWithBody };

    const char* RagdollName(RagdollBone) noexcept;
    const char* RagdollName(RagdollJoint) noexcept;
    std::optional<RagdollBone> RagdollParent(RagdollBone) noexcept;

    struct RagdollBodyDesc {
        RagdollBone key{};
        Vec3f center{}, halfExtents{.25f}, colliderCenter{};
        Quat orientation{1,0,0,0};
        ShapeType shape{ShapeType::Box};
        float radius{.25f}, halfLength{.25f};
        Vec3f initialLinearVelocity{}, initialAngularVelocity{};
        PhysicsBodyProperties properties;
        bool visible{true};
        friend bool operator==(const RagdollBodyDesc&, const RagdollBodyDesc&) = default;
    };
    Vec3f RagdollShapeHalfExtents(const RagdollBodyDesc&) noexcept;

    struct RagdollJointDesc {
        RagdollJoint key{};
        RagdollBone bodyA{}, bodyB{};
        Vec3f localAnchorA{}, localAnchorB{}; // COM-local metres
        AngularJointParameters angular{};
        bool collideConnected{};
        JointKind Kind() const noexcept;
        friend bool operator==(const RagdollJointDesc&, const RagdollJointDesc&) = default;
    };
    struct PassiveRagdollDesc {
        std::array<RagdollBodyDesc,6> bodies{};
        std::array<RagdollJointDesc,5> joints{};
        float gravityY{-10.f}, dropOffset{};
        RagdollAnchorResizePolicy anchorResizePolicy{RagdollAnchorResizePolicy::PreserveLocalMeters};
        friend bool operator==(const PassiveRagdollDesc&, const PassiveRagdollDesc&) = default;
    };
    enum class RagdollErrorCode { Topology, Geometry, Motion, Material, Capability, Readback, Creation, Retirement };
    struct RagdollError {
        RagdollErrorCode code;
        std::string_view message;
        std::optional<RagdollBone> bone{};
        std::optional<RagdollJoint> joint{};
    };
    template<class T=void> using RagdollResult = std::expected<T,RagdollError>;
    [[nodiscard]] RagdollResult<> ValidateRagdollRest(const PassiveRagdollDesc&);

    // A non-owning mapping. Scene owns the bound bodies and PhysicsWorld owns
    // the joints. Resolve handles against the supplied world on each operation.
    struct PassiveRagdoll {
        std::array<PhysicsBodyHandle,6> bodies{};
        std::array<PhysicsJointHandle,5> joints{};
        [[nodiscard]] static RagdollResult<PassiveRagdoll> Bind(PhysicsWorld&,
            const PassiveRagdollDesc&, const std::array<PhysicsBodyHandle,6>&
#ifdef GENGINE_PHASE29_QUALIFICATION
            , std::optional<RagdollJoint> rejectEndpointAt = {}
#endif
        );
        [[nodiscard]] PhysicsBodyResult<PhysicsBodyState> ReadBone(const PhysicsWorld&, RagdollBone) const;
        [[nodiscard]] JointResult<JointState> ReadJoint(const PhysicsWorld&, RagdollJoint) const;
        // Retire only owned relations. Member deletion may have retired them first.
        [[nodiscard]] RagdollResult<> Retire(PhysicsWorld&);
    };
}
