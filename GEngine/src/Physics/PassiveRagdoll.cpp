#include "gepch.h"
#include "Physics/PassiveRagdoll.h"
#include <algorithm>
#include <cmath>

namespace GEngine
{
    namespace
    {
        constexpr std::array<const char*,6> BoneNames{"Head","Torso","Left Arm","Right Arm","Left Leg","Right Leg"};
        constexpr std::array<const char*,5> JointNames{"Neck","Left Shoulder","Right Shoulder","Left Hip","Right Hip"};
        // Ordered provider endpoints are distinct from the logical parent map.
        constexpr std::array<RagdollBone,5> EndpointA{RagdollBone::Head,RagdollBone::Torso,RagdollBone::Torso,RagdollBone::Torso,RagdollBone::Torso};
        constexpr std::array<RagdollBone,5> EndpointB{RagdollBone::Torso,RagdollBone::LeftArm,RagdollBone::RightArm,RagdollBone::LeftLeg,RagdollBone::RightLeg};
        constexpr float RestTolerance=1.e-5f;

        AngularJointDesc MakeConstraint(const RagdollJointDesc& j, PhysicsBodyHandle a={}, PhysicsBodyHandle b={})
        {
            return {j.Kind(),a,b,j.localAnchorA,j.localAnchorB,j.collideConnected,j.angular};
        }
        bool SatisfiedRest(const JointAngularState& state,JointKind kind)
        {
            const float residual=kind==JointKind::Hinge?state.axisError:state.cvError;
            return residual<=RestTolerance && state.violationU<=RestTolerance && state.violationV<=RestTolerance;
        }
        Vec3f WorldAnchor(const RagdollBodyDesc& b,const Vec3f& local)
        {
            return b.center+b.orientation*(b.colliderCenter+local);
        }
        bool SameFrame(const Quat& a,const Quat& b)
        {
            return std::min(glm::length(a-b),glm::length(a+b))<=RestTolerance;
        }
    }
    const char* RagdollName(RagdollBone key) noexcept
    {
        const auto i=static_cast<std::size_t>(key);
        return i<BoneNames.size()?BoneNames[i]:"Unknown bone";
    }
    const char* RagdollName(RagdollJoint key) noexcept
    {
        const auto i=static_cast<std::size_t>(key);
        return i<JointNames.size()?JointNames[i]:"Unknown joint";
    }
    std::optional<RagdollBone> RagdollParent(RagdollBone key) noexcept
    {
        if(static_cast<std::size_t>(key)>=BoneNames.size() || key==RagdollBone::Torso)return {};
        return RagdollBone::Torso;
    }
    Vec3f RagdollShapeHalfExtents(const RagdollBodyDesc& b) noexcept
    {
        switch(b.shape) {
        case ShapeType::Box:return b.halfExtents;
        case ShapeType::Sphere:return Vec3f(b.radius);
        case ShapeType::Capsule:return {b.radius,b.radius+b.halfLength,b.radius};
        default:return Vec3f(0);
        }
    }
    JointKind RagdollJointDesc::Kind() const noexcept
    {
        if(key==RagdollJoint::LeftShoulder || key==RagdollJoint::RightShoulder)
            return angular.limitsEnabled?JointKind::LimitedConstantVelocity:JointKind::ConstantVelocity;
        return JointKind::Hinge;
    }
    RagdollResult<> ValidateRagdollRest(const PassiveRagdollDesc& d)
    {
        for(std::size_t i=0;i<d.bodies.size();++i) {
            const auto& b=d.bodies[i];
            if(b.key!=static_cast<RagdollBone>(i))
                return std::unexpected(RagdollError{RagdollErrorCode::Topology,"Missing or duplicated stable bone key",b.key});
            const auto extents=RagdollShapeHalfExtents(b);
            if(!Math::IsFinite(b.center) || !Math::IsFinite(b.orientation) ||
                std::abs(glm::dot(b.orientation,b.orientation)-1.f)>8*std::numeric_limits<float>::epsilon() ||
                !Math::IsFinite(b.colliderCenter) || !Math::IsFinite(extents) || glm::any(glm::lessThanEqual(extents,Vec3f(0))) ||
                (b.shape!=ShapeType::Box && b.colliderCenter!=Vec3f(0)) ||
                (b.shape==ShapeType::Capsule && !(b.halfLength>0)))
                return std::unexpected(RagdollError{RagdollErrorCode::Geometry,"Invalid rest transform or primitive dimensions",b.key});
            if(!(b.properties.mass>0) || !std::isfinite(b.properties.mass) ||
                (b.properties.type!=BodyType::Dynamic && b.properties.type!=BodyType::Kinematic && b.properties.type!=BodyType::Static))
                return std::unexpected(RagdollError{RagdollErrorCode::Motion,"Invalid authored body type or mass",b.key});
        }
        for(std::size_t i=0;i<d.joints.size();++i) {
            const auto& j=d.joints[i];
            if(j.key!=static_cast<RagdollJoint>(i) || j.bodyA!=EndpointA[i] || j.bodyB!=EndpointB[i])
                return std::unexpected(RagdollError{RagdollErrorCode::Topology,"Invalid Chapter 21 joint endpoints or key",{},j.key});
            const auto& a=d.bodies[static_cast<std::size_t>(j.bodyA)];
            const auto& b=d.bodies[static_cast<std::size_t>(j.bodyB)];
            if(a.properties.type!=BodyType::Dynamic && b.properties.type!=BodyType::Dynamic)
                return std::unexpected(RagdollError{RagdollErrorCode::Motion,"A joint requires at least one Dynamic body",{},j.key});
            const auto constraint=MakeConstraint(j);
            if(!AngularJoint::Valid(constraint) ||
                !(glm::length(WorldAnchor(a,j.localAnchorA)-WorldAnchor(b,j.localAnchorB))<=RestTolerance) ||
                !SatisfiedRest(AngularJoint::Observe(constraint,a.orientation,b.orientation),constraint.kind))
                return std::unexpected(RagdollError{RagdollErrorCode::Geometry,"Inconsistent COM-local anchors, frames or limits at rest",{},j.key});
        }
        return {};
    }
    PhysicsBodyResult<PhysicsBodyState> PassiveRagdoll::ReadBone(const PhysicsWorld& world,RagdollBone key) const
    {
        const auto i=static_cast<std::size_t>(key);
        if(i>=bodies.size())return std::unexpected(PhysicsBodyError{PhysicsBodyErrorCode::InvalidHandle,{},"Ragdoll bone key"});
        return world.ReadEffectiveBody(bodies[i]);
    }
    JointResult<JointState> PassiveRagdoll::ReadJoint(const PhysicsWorld& world,RagdollJoint key) const
    {
        const auto i=static_cast<std::size_t>(key);
        if(i>=joints.size())return std::unexpected(JointError{JointErrorCode::InvalidHandle,{},"Ragdoll joint key"});
        return world.ReadJoint(joints[i]);
    }
    RagdollResult<> PassiveRagdoll::Retire(PhysicsWorld& world)
    {
        for(auto it=joints.rbegin();it!=joints.rend();++it) {
            if(!*it)continue;
            const auto result=world.DestroyJoint(*it);
            if(!result && result.error().code!=JointErrorCode::StaleHandle)
                return std::unexpected(RagdollError{RagdollErrorCode::Retirement,result.error().operation});
            *it={};
        }
        bodies={};
        return {};
    }
    RagdollResult<PassiveRagdoll> PassiveRagdoll::Bind(PhysicsWorld& world,const PassiveRagdollDesc& d,
        const std::array<PhysicsBodyHandle,6>& handles
#ifdef GENGINE_PHASE29_QUALIFICATION
        ,std::optional<RagdollJoint> rejectEndpointAt
#endif
    )
    {
        if(auto rest=ValidateRagdollRest(d);!rest)return std::unexpected(rest.error());
        PassiveRagdoll result{handles,{}};
        std::optional<Vec3f> placement;
        for(std::size_t i=0;i<handles.size();++i) {
            const auto& b=d.bodies[i];
            if(std::find(handles.begin(),handles.begin()+i,handles[i])!=handles.begin()+i)
                return std::unexpected(RagdollError{RagdollErrorCode::Topology,"Duplicate bound body handle",b.key});
            auto state=result.ReadBone(world,b.key);
            if(!state)return std::unexpected(RagdollError{RagdollErrorCode::Readback,state.error().operation,b.key});
            const auto offset=state->position-b.center;
            if(!placement)placement=offset; // common room floor / authored placement
            if(!Math::IsFinite(offset) || glm::length(offset-*placement)>RestTolerance ||
                !SameFrame(state->orientation,b.orientation) || state->shapeType!=b.shape || state->type!=b.properties.type ||
                glm::length(state->centerOfMass-(state->position+state->orientation*b.colliderCenter))>RestTolerance)
                return std::unexpected(RagdollError{RagdollErrorCode::Readback,"Bound body geometry or rest pose differs from its bone",b.key});
        }
        const auto rollback=[&](RagdollError error)->RagdollResult<PassiveRagdoll> {
            if(auto retired=result.Retire(world);!retired)return std::unexpected(retired.error());
            return std::unexpected(error);
        };
        for(std::size_t i=0;i<d.joints.size();++i) {
            const auto& j=d.joints[i];
            auto expected=MakeConstraint(j,handles[static_cast<std::size_t>(j.bodyA)],handles[static_cast<std::size_t>(j.bodyB)]);
#ifdef GENGINE_PHASE29_QUALIFICATION
            // Exercise the real provider rejection and rollback after every
            // possible prefix without constructing additional bodies or joints.
            if(rejectEndpointAt==j.key)expected.bodyB={};
#endif
            auto created=world.CreateAngularJoint(expected);
            if(!created)return rollback({RagdollErrorCode::Creation,created.error().operation,{},j.key});
            result.joints[i]=*created;
            auto actual=world.ReadJoint(*created);
            if(!actual)return rollback({RagdollErrorCode::Readback,actual.error().operation,{},j.key});
            const auto& v=actual->descriptor;
            if(v.kind!=expected.kind || v.bodyA!=expected.bodyA || v.bodyB!=expected.bodyB ||
                v.collideConnected!=expected.collideConnected || actual->error>RestTolerance ||
                glm::length(v.localAnchorA-j.localAnchorA)>RestTolerance || glm::length(v.localAnchorB-j.localAnchorB)>RestTolerance ||
                !SameFrame(v.angular.localFrameA,j.angular.localFrameA) || !SameFrame(v.angular.localFrameB,j.angular.localFrameB) ||
                v.angular.limitsEnabled!=j.angular.limitsEnabled ||
                (j.angular.limitsEnabled && (v.kind==JointKind::Hinge?
                    std::abs(v.angular.lower-j.angular.lower)>RestTolerance || std::abs(v.angular.upper-j.angular.upper)>RestTolerance :
                    std::abs(v.angular.halfAngleU-j.angular.halfAngleU)>RestTolerance || std::abs(v.angular.halfAngleV-j.angular.halfAngleV)>RestTolerance)) ||
                !SatisfiedRest(actual->angular,v.kind))
                return rollback({RagdollErrorCode::Readback,"Constructed joint differs from the authored rest relation",{},j.key});
        }
        return result;
    }
}
