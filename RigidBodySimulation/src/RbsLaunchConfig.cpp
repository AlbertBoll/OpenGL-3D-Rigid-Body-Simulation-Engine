#include "RbsLaunchConfig.h"
#include <algorithm>
#include <filesystem>
#include <charconv>
#include <cmath>
#include <cstdlib>
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
    namespace
    {
        using GEngine::Math::Vec3f;
        using GEngine::Math::Quat;
        AngularPacketError PacketError(AngularField field, std::string_view message)
        { return {field,message}; }
        bool Finite(const Vec3f& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
        bool Finite(const Quat& q) { return std::isfinite(q.w)&&std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z); }
        bool Close(const Vec3f& a,const Vec3f& b)
        { return Finite(a)&&Finite(b)&&glm::length(a-b)<=1.e-5f; }
        bool SameFrame(const Quat& a,const Quat& b)
        { return Finite(a)&&Finite(b)&&std::abs(glm::length(a)-1.f)<=1.e-5f&&
                 std::abs(glm::dot(a,b))>=1.f-1.e-5f; }
        bool IsLimited(RbsScenePreset scene)
        { return scene==RbsScenePreset::HingeLimited || scene==RbsScenePreset::CvLimited; }
        bool IsHinge(RbsScenePreset scene)
        { return scene==RbsScenePreset::HingeBasic || scene==RbsScenePreset::HingeQuaternion || scene==RbsScenePreset::HingeLimited; }
    }

    void SetAngularBarScale(AngularPacket& p,float scale) noexcept
    {
        const float bottom=p.variant==AngularVariant::Contact?.5f:.75f;
        p.bodies[1].halfExtents={.15f*scale,scale,.15f*scale};
        p.bodies[1].center={0.f,bottom+scale,0.f};
        p.bodies[0].center={0.f,bottom+2.f*scale+.25f,0.f};
        p.joint.localAnchorB={0.f,scale,0.f};
    }

    void SetAngularBarMass(AngularPacket& p,float mass) noexcept
    {
        p.bodies[1].authoredMass=mass;
        p.scheduledImpulses[0]={3.f*mass,0.f,0.f};
        p.scheduledImpulses[1]=IsHinge(p.scene)?Vec3f{-3.f*mass,0.f,0.f}:Vec3f{0.f,0.f,3.f*mass};
    }

    void SetAngularBodyPosition(AngularPacket& p,AngularBodyKey key,const Vec3f& center) noexcept
    {
        if(key!=AngularBodyKey::Anchor && key!=AngularBodyKey::Bar)return;
        const Vec3f delta=center-p.bodies[static_cast<unsigned>(key)].center;
        for(unsigned i=0;i<2;++i)p.bodies[i].center+=delta;
    }

    void SetAngularBodyRotation(AngularPacket& p,AngularBodyKey key,const Quat& orientation) noexcept
    {
        if(key!=AngularBodyKey::Anchor && key!=AngularBodyKey::Bar)return;
        const auto& selected=p.bodies[static_cast<unsigned>(key)];
        const Vec3f center=selected.center;
        // Rotate the complete assembly about the selected object's center. The
        // original body-local pin and joint frames travel with each endpoint.
        const Quat delta=orientation*glm::conjugate(selected.orientation);
        for(unsigned i=0;i<2;++i) {
            auto& b=p.bodies[i];
            b.center=center+delta*(b.center-center);
            b.orientation=glm::normalize(delta*b.orientation);
        }
    }

    void SetAngularBodyHalfExtents(AngularPacket& p,AngularBodyKey key,const Vec3f& halfExtents) noexcept
    {
        if(key!=AngularBodyKey::Anchor && key!=AngularBodyKey::Bar)return;
        const bool anchor=key==AngularBodyKey::Anchor;
        auto& b=p.bodies[static_cast<unsigned>(key)];
        auto& localPin=anchor?p.joint.localAnchorA:p.joint.localAnchorB;
        const Vec3f worldPin=b.center+b.orientation*localPin;
        b.halfExtents=halfExtents;
        localPin={0.f,(anchor?-1.f:1.f)*halfExtents.y,0.f};
        b.center=worldPin-b.orientation*localPin;
    }

    void SetAngularAnchorSphere(AngularPacket& p,bool sphere) noexcept
    {
        auto& anchor=p.bodies[0];
        anchor.sphere=sphere;
        if(sphere)SetAngularBodyHalfExtents(p,AngularBodyKey::Anchor,Vec3f(anchor.halfExtents.y));
    }

    void SetAngularJointFrame(AngularPacket& p,const Quat& localA) noexcept
    {
        p.joint.localFrameA=localA;
        p.joint.localFrameB=glm::conjugate(p.bodies[1].orientation)*(p.bodies[0].orientation*localA);
    }

    std::expected<AngularPacket,AngularPacketError> ReferenceAngularPacket(
        RbsScenePreset scene,AngularVariant variant)
    {
        if(!IsAngularScene(scene))return std::unexpected(PacketError(AngularField::Case,"Unknown angular case"));
        if(variant!=AngularVariant::Air && variant!=AngularVariant::Contact)
            return std::unexpected(PacketError(AngularField::Variant,"Unknown angular variant"));
        if(variant==AngularVariant::Contact && !IsLimited(scene))
            return std::unexpected(PacketError(AngularField::Variant,"Contact is admitted for limited hinge/CV only"));
        AngularPacket p;
        p.scene=scene;p.variant=variant;
        p.bodyCount=variant==AngularVariant::Contact?3:2;
        p.bodies[0].key=AngularBodyKey::Anchor;
        p.bodies[0].halfExtents={.25f,.25f,.25f};
        p.bodies[1].key=AngularBodyKey::Bar;
        if(p.bodyCount==3) {
            p.bodies[2].key=AngularBodyKey::Floor;
            p.bodies[2].center={0.f,0.f,0.f};
            p.bodies[2].halfExtents={5.f,.5f,5.f};
        }
        SetAngularBarScale(p,1.f);
        SetAngularBarMass(p,1.f);
        const bool hinge=IsHinge(scene);
        const GEngine::Math::Mat3 frame=hinge?
            GEngine::Math::Mat3(Vec3f{0,0,1},Vec3f{1,0,0},Vec3f{0,1,0}):
            GEngine::Math::Mat3(Vec3f{0,1,0},Vec3f{1,0,0},Vec3f{0,0,-1});
        p.joint.localFrameA=p.joint.localFrameB=glm::quat_cast(frame);
        p.joint.limitsEnabled=IsLimited(scene);
        if(scene==RbsScenePreset::HingeLimited){p.joint.lower=-GEngine::Math::Pi/4.f;p.joint.upper=GEngine::Math::Pi/4.f;}
        return p;
    }

    std::expected<void,AngularPacketError> ValidateAngularPacket(const AngularPacket& p,GEngine::PhysicsBackend backend,bool requireFrozenFixture)
    {
        if(!IsAngularScene(p.scene))return std::unexpected(PacketError(AngularField::Case,"Unknown angular case"));
        if(!GEngine::PhysicsBackendAvailable(backend))return std::unexpected(PacketError(AngularField::Backend,"Requested backend unavailable"));
        const auto reference=ReferenceAngularPacket(p.scene,p.variant);
        if(!reference)return std::unexpected(reference.error());
        const auto& r=*reference;
        if(requireFrozenFixture && p!=r)
            return std::unexpected(PacketError(AngularField::Identity,"Qualification inputs must match the frozen fixture"));
        if(p.referenceRevision!=r.referenceRevision || p.sourceSha256!=r.sourceSha256 || p.fixtureSha256!=r.fixtureSha256)
            return std::unexpected(PacketError(AngularField::Identity,"Reference identity changed"));
        if(p.bodyCount!=r.bodyCount || p.joint.bodyA!=AngularBodyKey::Anchor || p.joint.bodyB!=AngularBodyKey::Bar ||
            p.joint.key!=AngularJointKey::Primary)
            return std::unexpected(PacketError(AngularField::Body,"Fixed two-body joint topology changed"));
        const float scale=p.bodies[1].halfExtents.y,mass=p.bodies[1].authoredMass;
        if(!std::isfinite(scale)||scale<.75f||scale>1.25f)return std::unexpected(PacketError(AngularField::Scale,"Bar scale must be .75..1.25"));
        if(!std::isfinite(mass)||mass<.5f||mass>2.f||!std::isfinite(1.f/mass))
            return std::unexpected(PacketError(AngularField::Mass,"Dynamic mass must be .5..2 kg with finite inverse"));
        AngularPacket expected=r;
        SetAngularBarMass(expected,mass);
        if(p.bodyCount==3 && p.bodies[2]!=r.bodies[2])
            return std::unexpected(PacketError(AngularField::Body,"Contact floor is fixed"));
        const auto within=[](float value,float min,float max) {
            return std::isfinite(value) && value>=min && value<=max;
        };
        for(unsigned i=0;i<2;++i) {
            const auto& b=p.bodies[i];
            const auto& e=expected.bodies[i];
            if(b.key!=e.key || (i==1 && (b.sphere || b.kinematic)) ||
               b.authoredMass!=e.authoredMass || b.initialLinearVelocity!=e.initialLinearVelocity ||
               b.initialAngularVelocity!=e.initialAngularVelocity)
                return std::unexpected(PacketError(AngularField::Body,"Use a fixed anchor and a dynamic beam with the authored initial velocities"));
            for(unsigned axis=0;axis<3;++axis)
                if(!within(b.halfExtents[axis],.75f*e.halfExtents[axis],1.25f*e.halfExtents[axis]))
                    return std::unexpected(PacketError(AngularField::Scale,"Each endpoint dimension must be 75% to 125% of its reference size"));
            if(b.sphere && b.halfExtents!=Vec3f(b.halfExtents.y))
                return std::unexpected(PacketError(AngularField::Scale,"Spherical anchors need a single positive radius"));
            if(!within(b.friction,0.f,1.f) || !within(b.restitution,0.f,1.f))
                return std::unexpected(PacketError(AngularField::Material,"Endpoint friction and restitution must be in [0,1]"));
            if(!within(b.linearDamping,0.f,1.f) || !within(b.angularDamping,0.f,1.f))
                return std::unexpected(PacketError(AngularField::Material,"Linear and angular damping must be in [0,1]"));
            if((backend!=GEngine::PhysicsBackend::Jolt || i!=1) && (b.linearDamping!=0.f || b.angularDamping!=0.f))
                return std::unexpected(PacketError(AngularField::Backend,"Nonzero damping requires a Jolt dynamic body. Apply zero damping before switching to Native."));
            if(!Finite(b.center) || !SameFrame(b.orientation,b.orientation))
                return std::unexpected(PacketError(AngularField::Frame,"Each endpoint requires a finite position and normalized rotation"));
            const auto basis=glm::mat3_cast(b.orientation);
            const Vec3f extent=b.sphere?b.halfExtents:
                glm::abs(basis[0])*b.halfExtents.x+glm::abs(basis[1])*b.halfExtents.y+glm::abs(basis[2])*b.halfExtents.z;
            constexpr float interiorX=SandboxEnvironment::Width*.5f-SandboxEnvironment::WallThickness;
            constexpr float interiorZ=SandboxEnvironment::Depth*.5f-SandboxEnvironment::WallThickness;
            if(std::abs(b.center.x)+extent.x>interiorX || std::abs(b.center.z)+extent.z>interiorZ ||
               b.center.y-extent.y<SandboxEnvironment::FloorTop-1.e-5f || b.center.y+extent.y>SandboxEnvironment::Width)
                return std::unexpected(PacketError(AngularField::Body,"Initial geometry must fit inside the Sandbox footprint, above the floor and below 160 m"));
        }
        if(!Finite(p.gravity)||p.gravity.x!=0 || p.gravity.z!=0 || p.gravity.y< -10.f || p.gravity.y>0.f)
            return std::unexpected(PacketError(AngularField::Gravity,"Gravity must be (0,g,0), g in -10..0"));
        if(p.fixedStepSeconds!=r.fixedStepSeconds)return std::unexpected(PacketError(AngularField::Identity,"Fixed-step policy changed"));
        const auto& a=p.bodies[0];
        const auto& b=p.bodies[1];
        if(!Close(p.joint.localAnchorA,{0,-a.halfExtents.y,0}) || !Close(p.joint.localAnchorB,{0,b.halfExtents.y,0}) ||
            !Close(a.center+a.orientation*p.joint.localAnchorA,b.center+b.orientation*p.joint.localAnchorB) ||
            !SameFrame(p.joint.localFrameA,p.joint.localFrameA) || !SameFrame(p.joint.localFrameB,p.joint.localFrameB) ||
            !SameFrame(a.orientation*p.joint.localFrameA,b.orientation*p.joint.localFrameB) ||
            p.joint.collideConnected)
            return std::unexpected(PacketError(AngularField::Frame,"The two tip anchors and normalized zero-angle frames must coincide; connected collision is disabled"));
        if(p.joint.limitsEnabled!=r.joint.limitsEnabled)
            return std::unexpected(PacketError(AngularField::Limits,"Case limit mode is fixed"));
        if(p.scene==RbsScenePreset::HingeLimited) {
            if(!std::isfinite(p.joint.lower)||!std::isfinite(p.joint.upper)||
                p.joint.lower < -GEngine::Math::Pi/4.f || p.joint.lower>0 ||
                p.joint.upper<0 || p.joint.upper>GEngine::Math::Pi/4.f || p.joint.lower>=p.joint.upper)
                return std::unexpected(PacketError(AngularField::Limits,"Hinge limits outside admitted interval"));
        } else if(p.joint.lower!=r.joint.lower || p.joint.upper!=r.joint.upper)
            return std::unexpected(PacketError(AngularField::Limits,"This case has no editable hinge interval"));
        if(p.scene==RbsScenePreset::CvLimited) {
            constexpr float min=GEngine::Math::Pi/12.f,max=GEngine::Math::Pi/4.f;
            if(!std::isfinite(p.joint.halfAngleU)||!std::isfinite(p.joint.halfAngleV)||
                p.joint.halfAngleU<min||p.joint.halfAngleU>max||p.joint.halfAngleV<min||p.joint.halfAngleV>max)
                return std::unexpected(PacketError(AngularField::Limits,"CV pyramid half angles outside admitted interval"));
        } else if(p.joint.halfAngleU!=r.joint.halfAngleU||p.joint.halfAngleV!=r.joint.halfAngleV)
            return std::unexpected(PacketError(AngularField::Limits,"This case has no editable CV limits"));
        if(!Close(p.scheduledImpulses[0],expected.scheduledImpulses[0])||
            !Close(p.scheduledImpulses[1],expected.scheduledImpulses[1]))
            return std::unexpected(PacketError(AngularField::Action,"Inspection actions must remain within 3m N*s"));
        return {};
    }

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
#ifdef GENGINE_PHASE28_QUALIFICATION
        // The same executable also serves ordinary Editor inspection. Only a
        // named physical qualification case opts out of the interactive room.
        if(const char* selected=std::getenv("GENGINE_PHASE28_CASE"))
            if(std::string_view(selected)!="EDITOR_NATIVE" && std::string_view(selected)!="EDITOR_JOLT")
                return false;
#endif
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
        if(IsAngularScene(scene))
            return (angularVariant==AngularVariant::Air ||
                    (angularVariant==AngularVariant::Contact && IsLimited(scene))) &&
                startupMesh==StartupMesh::None && !restingContactDiagnostic && validation.checks.empty() &&
                !validation.asyncPerformance && !validation.materialAlphaFixture && !validation.subscriptionOutput;
        if(angularVariant!=AngularVariant::Air)return false;
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
        case RbsScenePreset::HingeBasic:return "hinge-basic";
        case RbsScenePreset::HingeQuaternion:return "hinge-quaternion";
        case RbsScenePreset::HingeLimited:return "hinge-limited";
        case RbsScenePreset::CvFree:return "cv-free";
        case RbsScenePreset::CvLimited:return "cv-limited";
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
        bool modeSeen=false,variantSeen=false;
        for(std::size_t i=0;i<args.size();i+=2) {
            if(i+1==args.size()) return std::unexpected("Missing value for startup option");
            const auto option=args[i], value=args[i+1];
            if(option=="--scene" && !sceneSeen) {
                sceneSeen=true; bool found=false;
                for(auto scene:{RbsScenePreset::GeometryGallery,RbsScenePreset::SphereDiamond,
                    RbsScenePreset::SphereLattice,RbsScenePreset::BoxStack,RbsScenePreset::SphereBoxStack,
                    RbsScenePreset::DistancePendulum,RbsScenePreset::DistanceChain,RbsScenePreset::DistanceChainContact,
                    RbsScenePreset::BallSocketSwing,RbsScenePreset::BallSocketPair,RbsScenePreset::BallSocketContact,RbsScenePreset::BallSocketContactEnabled,
                    RbsScenePreset::HingeBasic,RbsScenePreset::HingeQuaternion,RbsScenePreset::HingeLimited,RbsScenePreset::CvFree,RbsScenePreset::CvLimited})
                    if(value==SceneName(scene)) { launch.scene=scene; found=true; break; }
                if(!found)return std::unexpected("Unknown or unimplemented scene preset");
            } else if(option=="--mode" && !modeSeen) {
                modeSeen=true;
                // Older launch commands supplied this option. Both spellings now
                // open the same directly editable scene.
                if(value!="reference" && value!="enhanced")
                    return std::unexpected("Invalid legacy --mode value");
            } else if(option=="--variant" && !variantSeen) {
                variantSeen=true;
                if(value=="air")launch.angularVariant=AngularVariant::Air;
                else if(value=="contact")launch.angularVariant=AngularVariant::Contact;
                else return std::unexpected("Angular variant must be air or contact");
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
                launch.m_BackendExplicit=true;
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
        if(((modeSeen||variantSeen) && !IsAngularScene(launch.scene)) ||
            (linksSeen && (!launch.IsDistanceScene() || launch.scene==RbsScenePreset::DistancePendulum)) ||
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
