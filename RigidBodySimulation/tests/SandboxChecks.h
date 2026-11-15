#pragma once
#ifdef GENGINE_PHASE26_SANDBOX_CHECKS
#include "RigidBodySimulation.h"
#include "LaunchConfigChecks.h"
#include "RestingContactDiagnosis.h"
#include "ContactCorrectionMemory.h"
#include "Renderer/SceneRenderResources.h"
#include "Core/RenderCounters.h"
#include "Core/Window.h"
#include "Managers/AssetsManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

namespace Rbs
{
    // A bounded application driver. Observers only read the ordinary production
    // world/frame. All commands use the same application methods as the panel.
    struct SandboxChecks
    {
        using App = RigidBodySimulationApp;
        using State = SimulationState;
        using Preset = RbsScenePreset;
        using Vec = GEngine::Math::Vec3f;
        using Body = GEngine::PhysicsBodyHandle;
        using Joint = GEngine::PhysicsJointHandle;
        enum class Stage { Initial, Inject, FailurePending, Ready, Starting, Run,
                           Pause, Resuming, AfterImpulse, SwitchPending, Done };
        inline static Stage stage = Stage::Initial;
        inline static std::filesystem::path output;
        inline static App* current{};
        inline static const char* failure{};
        inline static FILE *ticks{}, *transitions{}, *render{}, *wallEvents{};
        inline static unsigned frames{}, panels{}, successful{}, presetMask{}, hold{}, sceneFrames{};
        inline static unsigned screenshots{}, pausedTicks{}, rejectedOld{}, boundaryCommands{}, capturedMask{};
        inline static std::uint64_t completed{}, previousSteps{}, pausedSteps{}, impulseStep{};
        inline static bool invalidRejected{}, rollback{}, g8Proven{}, extendedG8{}, slept{}, wake{}, applied{};
        inline static bool queuedBoundary{}, initialized{}, finished{};
        inline static bool wallProbe{}, wallContact{};
        inline static unsigned wallCcdEvents{};
        inline static Body wallProbeBody{};
        inline static unsigned groundMask{}, g8Contacts{}, sleepTick{}, wakeTick{};
        inline static double externalWork{}, maximumJointError{}, maximumSphereOverlap{};
        inline static Vec requestedImpulse{};
        inline static std::vector<std::pair<Body, Vec>> velocities;
        inline static std::weak_ptr<GEngine::_Scene> previousScene, failedScene, finalScene;
        inline static Body previousBody{}, failedBody{};
        inline static Joint previousJoint{}, failedJoint{};
        inline static GEngine::EntityRenderId previousRender{};
        inline static GEngine::Asset::MeshHandle previousMesh{};
        inline static SceneSelection previousSelection{};
        inline static std::array<Body, 5> environmentBodies{};
        inline static std::array<GEngine::EntityRenderId, 5> environmentRender{};
        inline static std::array<GEngine::LocalBounds, 5> environmentBounds{};
        inline static std::array<Body, 8> g8Bodies{};
        inline static Body rootBody{};
        inline static Vec rootPosition{};
        inline static GEngine::PhysicsDiagnostic::Observer observer;
        struct Counts { bool set{}; std::size_t meshes{}, materials{}, bodies{}, joints{}; };
        inline static std::array<Counts, 9> counts{};
        inline static std::array<GEngine::RenderCounters::Snapshot, 9> warmedCounters{};
        inline static std::array<bool, 9> haveWarmedCounters{};
        // Opt-in material restoration check; the historical lifecycle driver below
        // remains the default mode and retains its original workload.
        enum class MaterialStage { Initial, Captures, Ready, Starting, Run, Pause,
                                   Resuming, AfterResume, Switching, Done };
        inline static bool materialMode{};
        inline static MaterialStage materialStage=MaterialStage::Initial;
        inline static FILE *materialLog{}, *uvLog{}, *captureLog{};
        inline static unsigned materialCapture{}, materialCaptureWait{}, materialFrameMask{}, materialSceneChecks{}, materialTriangles{};
        inline static double materialMaxUvError{}, materialMaxUvOrthogonality{};
        inline static std::array<GEngine::MaterialHandle,5> roomMaterials{};
        inline static std::array<GEngine::Asset::MeshHandle,5> roomMeshes{};
        inline static std::array<GEngine::MaterialAuthoringDesc,5> roomDescriptions{};
        inline static std::array<const GEngine::MeshAsset*,5> roomSources{};
        inline static std::array<GEngine::MaterialTextureValue,5> originalTileBindings{};
        inline static constexpr std::array<const char*,5> tileSemantics{
            "albedoMap","normalMap","metallicMap","roughnessMap","aoMap"};
        inline static constexpr std::array<SceneSelection,3> materialSequence{{
            {Preset::DistanceChainContact,8},{Preset::SphereBoxStack,8},{Preset::SphereBoxStack,8}}};
        // Separate opt-in visual comparison/final driver. Historical material and
        // twenty-transition modes retain their own dispatch and workloads.
        enum class VisualStage { Initial, Captures, BeginRun, Failure, Starting,
                                 Run, Pause, Resuming, AfterResume, Switching, Done };
        inline static bool visualMode{}, visualCompare{};
        inline static VisualStage visualStage=VisualStage::Initial;
        inline static unsigned visualVariant{}, visualView{}, visualWait{}, visualDensity{}, visualScope{};
        inline static unsigned visualWidth{}, visualHeight{}, visualOffFrames{}, visualSelectedFrames{}, visualAllFrames{};
        inline static std::size_t visualDebugLines{};
        inline static FILE *visualLog{}, *visualSamples{};
        inline static constexpr std::array<SceneSelection,3> visualSequence{{
            {Preset::DistanceChainContact,8},{Preset::DistanceChain,16},{Preset::DistanceChain,16}}};
        inline static constexpr std::array<unsigned,4> visualDensities{32,32,24,16};
        inline static constexpr std::array<const char*,4> visualNames{"baseline32","candidate32","candidate24","candidate16"};
        inline static constexpr std::array<SceneSelection, 20> sequence{{
            {Preset::DistanceChain,16}, {Preset::DistanceChainContact,8}, {Preset::SphereBoxStack,8},
            {Preset::DistanceChain,8}, {Preset::DistanceChain,16}, {Preset::DistanceChain,4},
            {Preset::SphereDiamond,8}, {Preset::SphereLattice,8}, {Preset::BoxStack,8},
            {Preset::GeometryGallery,8}, {Preset::GeometryGallery,8}, {Preset::DistanceChainContact,8},
            {Preset::DistanceChainContact,8}, {Preset::DistanceChain,4}, {Preset::DistanceChain,8},
            {Preset::DistanceChain,8}, {Preset::SphereBoxStack,8}, {Preset::DistanceChain,16},
            {Preset::DistanceChainContact,8}, {Preset::DistanceChainContact,8}}};

        // Separate, read-only Physics observer for the Owner's bounded stack diagnosis.
        // No historical driver, Physics setting, contact, body or interpolation policy is changed.
        enum class JitterCase { None, Direct, Switch, Qualification, Box, Cadence, Lattice };
        inline static JitterCase jitterCase=JitterCase::None;
        inline static constexpr std::array<const char*,7> jitterNames{
            "none","sandbox-direct","sandbox-switch","qualification-direct","box-stack","sandbox-cadence","sphere-lattice"};
        inline static bool contactRegression{};
        inline static unsigned jitterTarget=2400, jitterBodies{}, jitterStage{}, jitterContacts{}, jitterFinalCaptures{}, jitterHeldFrames{}, jitterTerminalFrame{}, jitterMotionCaptures{};
        inline static std::uint64_t jitterTick{}, jitterRecorded{}, jitterEvents{}, jitterContactRows{}, jitterRenderRows{};
        inline static bool jitterStarted{}, jitterSwitching{}, jitterFinal{}, jitterDone{};
        inline static std::array<FILE*,9> jitterFiles{};
        struct JitterBody
        {
            Body handle{};
            entt::entity entity=entt::null;
            GEngine::EntityRenderId render{};
            GEngine::PhysicsBodyState previous{};
            bool havePrevious{};
        };
        inline static std::array<JitterBody,256> jitterBody{};
        inline static GEngine::PhysicsDiagnostic::Observer jitterObserver{};
        inline static double jitterHostSeconds{}, jitterMaxOverlap{}, jitterMaxSpeed{}, jitterMaxAngular{};

        static void JitterVector(FILE* file, const Vec& value)
        { std::fprintf(file,",%.9g,%.9g,%.9g",value.x,value.y,value.z); }
        static void JitterQuaternion(FILE* file, const GEngine::Math::Quat& value)
        { std::fprintf(file,",%.9g,%.9g,%.9g,%.9g",value.w,value.x,value.y,value.z); }
        template<class Handle> static void JitterHandle(FILE* file, Handle value)
        { std::fprintf(file,",%u,%llu,%llu",value.index,(unsigned long long)value.generation,(unsigned long long)value.registry); }
        static void JitterVectorHeader(FILE* file,const char* name)
        { std::fprintf(file,",%s_x,%s_y,%s_z",name,name,name); }
        static void JitterPoseHeader(FILE* file,const char* name)
        { JitterVectorHeader(file,name);std::fprintf(file,",%s_qw,%s_qx,%s_qy,%s_qz",name,name,name,name); }
        static void JitterPose(FILE* file,const GEngine::SceneBodyPose& value)
        { JitterVector(file,value.translation);JitterQuaternion(file,value.rotation); }
        static void JitterStateHeader(FILE* file,const char* name)
        {
            JitterPoseHeader(file,name);const auto prefix=std::string(name);
            JitterVectorHeader(file,(prefix+"_v").c_str());JitterVectorHeader(file,(prefix+"_w").c_str());
            std::fprintf(file,",%s_inactive,%s_sleeping",name,name);
        }
        static void JitterState(FILE* file,const GEngine::PhysicsDiagnostic::BodyState& value)
        {
            JitterVector(file,value.position);JitterQuaternion(file,value.orientation);JitterVector(file,value.linear);JitterVector(file,value.angular);
            std::fprintf(file,",%.17g,%d",value.inactive,int(value.sleeping));
        }
        static bool JitterDetailed(std::uint64_t tick)
        {
            if(jitterCase==JitterCase::Lattice)return tick>=1 && (tick<=120 || tick+120>jitterTarget);
            return (tick>=1 && tick<=240) || (tick>=601 && tick<=720) ||
                (tick>=1201 && tick<=1320) || (tick>=2161 && tick<=2400);
        }
        static bool ConfigureJitter(std::vector<std::string_view>& args)
        {
            auto at=std::find(args.begin(),args.end(),"--phase26-sandbox-jitter");
            if(at==args.end() || at+1==args.end())return false;
            for(unsigned i=1;i<jitterNames.size();++i)if(*(at+1)==jitterNames[i])jitterCase=JitterCase(i);
            if(jitterCase==JitterCase::None)return false;
            args.erase(at,at+2);
            if(auto cap=std::find(args.begin(),args.end(),"--phase26-sandbox-jitter-ticks");cap!=args.end())
            {
                if(cap+1==args.end())return false;
                unsigned value=0;const auto text=*(cap+1);if(text.empty())return false;
                for(char digit:text){if(digit<'0' || digit>'9' || value>2400)return false;value=value*10+unsigned(digit-'0');}
                if(!value || value>2400)return false;jitterTarget=value;args.erase(cap,cap+2);
            }
            // Direct cases exercise the unchanged explicit scene parser. The switch
            // case leaves the Owner default intact and uses the panel's request API.
            const auto expected=jitterCase==JitterCase::Box?"box-stack":jitterCase==JitterCase::Lattice?"sphere-lattice":"sphere-box-stack";
            if(jitterCase==JitterCase::Switch){if(!args.empty())return false;}
            else if(args.size()!=2 || args[0]!="--scene" || args[1]!=expected)return false;
            if(ContactCorrectionMemory::installed && !ContactCorrectionMemory::Open(output))return false;
            constexpr const char* names[]{"jitter-members.csv","jitter-bodies.csv","jitter-contacts.csv",
                "jitter-events.csv","jitter-stages.csv","jitter-overlaps.csv","jitter-render.csv","jitter-frames.csv","jitter-captures.csv"};
            for(unsigned i=0;i<jitterFiles.size();++i)
                if(!(jitterFiles[i]=std::fopen((output/names[i]).string().c_str(),"wx")))return false;
            auto* f=jitterFiles[0];std::fprintf(f,"ordinal,name,body_slot,body_generation,body_registry,render_slot,render_generation,render_registry,type,shape,inverse_mass,friction,restitution,spin_length,rolling_length,sleep_linear_threshold,sleep_angular_threshold,sleep_inactivity_seconds");
            JitterVectorHeader(f,"local_min");JitterVectorHeader(f,"local_max");JitterPoseHeader(f,"initial");std::fputc('\n',f);
            f=jitterFiles[1];std::fprintf(f,"tick,ordinal,body_slot,body_generation,body_registry,type,shape");
            JitterPoseHeader(f,"physical");JitterVectorHeader(f,"v");JitterVectorHeader(f,"w");
            std::fprintf(f,",inactive_seconds,sleeping,inverse_mass,linear_ke_J,angular_ke_J,potential_J,position_delta_m,angle_delta_rad,velocity_delta,angular_delta\n");
            f=jitterFiles[2];std::fprintf(f,"tick,phase,a_slot,a_generation,a_registry,b_slot,b_generation,b_registry,slot,feature_a,feature_b,separation_m,toi");
            for(auto name:{"normal_b_to_a","anchor_a","anchor_b","local_a","local_b"})JitterVectorHeader(f,name);std::fputc('\n',f);
            f=jitterFiles[3];std::fprintf(f,"tick,sequence,phase,operation,a_slot,a_generation,a_registry,b_slot,b_generation,b_registry,slot,previous_slot,reason,feature_a,feature_b,constraint_token,penetration_m,closing_speed,time,duration,bias,restitution,impulses_known");
            for(auto name:{"normal_solver","anchor_a","anchor_b","local_a","local_b","tangent_u","tangent_v","linear_impulse_a","linear_impulse_b","angular_impulse_a","angular_impulse_b"})JitterVectorHeader(f,name);
            std::fprintf(f,",cache_before_n,cache_before_u,cache_before_v,cache_after_n,cache_after_u,cache_after_v");
            for(auto name:{"before_a","before_b","after_a","after_b"})JitterStateHeader(f,name);std::fputc('\n',f);
            std::fprintf(jitterFiles[4],"tick,phase,bodies,manifolds,contacts,asleep,linear_ke_J,angular_ke_J,potential_J,total_J,max_speed,max_angular_speed,solver_iterations,scratch_bytes\n");
            std::fprintf(jitterFiles[5],"tick,a_slot,a_generation,a_registry,b_slot,b_generation,b_registry,kind,overlap_m,axis_x,axis_y,axis_z\n");
            f=jitterFiles[6];std::fprintf(f,"frame,tick,ordinal,body_slot,body_generation,body_registry,alpha,pending_seconds,draw_present");
            for(auto name:{"physical","previous","current","presentation"})JitterPoseHeader(f,name);
            for(unsigned column=0;column<4;++column)for(unsigned row=0;row<4;++row)std::fprintf(f,",model_%u%u",column,row);
            std::fprintf(f,",presentation_physical_distance_m,presentation_physical_angle_rad,draw_presentation_distance_m,draw_expected_matrix_max_error\n");
            std::fprintf(jitterFiles[7],"frame,tick,host_seconds,pending_seconds,discarded_seconds,steps_this_update,alpha,interpolation_enabled,scene_entities,draws,draw_calls,width,height,camera_x,camera_y,camera_z,camera_pitch,camera_yaw\n");
            std::fprintf(jitterFiles[8],"image,frame,tick,held_frames,width,height\n");
            return true;
        }
        static void ConfigureLaunch(LaunchConfig& launch)
        {
            if(contactRegression)
            {
                // The existing D26 entry protocol does not consume latticePath;
                // keep its typed absolute-path field bound to the real request.
                launch.scene=Preset::GeometryGallery;
                launch.environment=RbsEnvironmentPreset::Qualification;
                launch.restingContactDiagnostic=RestingContactDiagnostic{Observation::Observed,
                    (output.parent_path()/"d26-entry-request.txt").generic_string(),output.generic_string()};
                return;
            }
            if(jitterCase==JitterCase::None)return;
            if(jitterCase==JitterCase::Qualification)launch.environment=RbsEnvironmentPreset::Qualification;
        }
        static double JitterAngle(const GEngine::Math::Quat& a,const GEngine::Math::Quat& b)
        {
            const glm::dquat da(a),db(b);
            if(glm::dot(da,da)<=0 || glm::dot(db,db)<=0)return std::numeric_limits<double>::quiet_NaN();
            const auto delta=glm::conjugate(glm::normalize(da))*glm::normalize(db);
            return 2*std::atan2(glm::length(glm::dvec3(delta.x,delta.y,delta.z)),std::abs(delta.w));
        }
        static std::array<double,3> JitterEnergy(const GEngine::PhysicsBodyState& b,Vec gravity)
        {
            if(b.type!=GEngine::Component::BodyType::Dynamic || b.inverseMass<=0)return {};
            const double mass=1.0/b.inverseMass;const glm::dvec3 v(b.linearVelocity),w(b.angularVelocity);
            const glm::dmat3 inverse(b.inverseInertiaWorld);double angular=0;
            if(std::abs(glm::determinant(inverse))>1e-20)angular=.5*glm::dot(w,glm::inverse(inverse)*w);
            return {.5*mass*glm::dot(v,v),angular,-mass*glm::dot(glm::dvec3(gravity),glm::dvec3(b.centerOfMass))};
        }
        static bool JitterFinite(const GEngine::PhysicsBodyState& b)
        {
            return GEngine::Math::IsFinite(b.position) && GEngine::Math::IsFinite(b.linearVelocity) &&
                GEngine::Math::IsFinite(b.angularVelocity) && std::isfinite(b.orientation.w) &&
                std::isfinite(b.orientation.x) && std::isfinite(b.orientation.y) && std::isfinite(b.orientation.z);
        }
        static void JitterWriteBodies(App& app,std::uint64_t tick)
        {
            auto& world=World(app);auto* f=jitterFiles[1];
            for(unsigned i=0;i<jitterBodies;++i)
            {
                auto& item=jitterBody[i];auto read=world.ReadBody(item.handle);
                Check(bool(read),"jitter body identity disappeared");if(!read)return;const auto& b=*read;
                Check(JitterFinite(b),"jitter nonfinite physical state");const auto energy=JitterEnergy(b,world.GetGravity());
                Check(std::isfinite(energy[0]+energy[1]+energy[2]),"jitter nonfinite energy");
                std::fprintf(f,"%llu,%u",(unsigned long long)tick,i);JitterHandle(f,item.handle);
                std::fprintf(f,",%u,%u",unsigned(b.type),unsigned(b.shapeType));JitterVector(f,b.position);JitterQuaternion(f,b.orientation);
                JitterVector(f,b.linearVelocity);JitterVector(f,b.angularVelocity);
                std::fprintf(f,",%.17g,%d,%.9g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",b.inactiveSeconds,int(b.sleeping),b.inverseMass,
                    energy[0],energy[1],energy[2],item.havePrevious?double(glm::length(b.position-item.previous.position)):0,
                    item.havePrevious?JitterAngle(b.orientation,item.previous.orientation):0,
                    item.havePrevious?double(glm::length(b.linearVelocity-item.previous.linearVelocity)):0,
                    item.havePrevious?double(glm::length(b.angularVelocity-item.previous.angularVelocity)):0);
                item.previous=b;item.havePrevious=true;
                jitterMaxSpeed=(std::max)(jitterMaxSpeed,double(glm::length(b.linearVelocity)));
                jitterMaxAngular=(std::max)(jitterMaxAngular,double(glm::length(b.angularVelocity)));
            }
        }
        struct JitterBox { glm::dvec3 center{},half{};glm::dmat3 axes{1}; };
        static JitterBox JitterObb(const GEngine::PhysicsBodyState& b)
        {
            JitterBox box;box.axes=glm::dmat3(glm::mat3_cast(b.orientation));
            box.half=(glm::dvec3(b.localBounds.maxs)-glm::dvec3(b.localBounds.mins))*.5;
            box.center=glm::dvec3(b.position)+box.axes*((glm::dvec3(b.localBounds.maxs)+glm::dvec3(b.localBounds.mins))*.5);return box;
        }
        static double JitterBoxOverlap(const JitterBox& a,const JitterBox& b,glm::dvec3& minimumAxis)
        {
            double minimum=std::numeric_limits<double>::infinity();const auto delta=b.center-a.center;
            const auto test=[&](glm::dvec3 axis) {
                const double length=glm::length(axis);if(length<1e-10)return true;axis/=length;
                double radius=0;for(unsigned k=0;k<3;++k)radius+=a.half[k]*std::abs(glm::dot(a.axes[k],axis))+b.half[k]*std::abs(glm::dot(b.axes[k],axis));
                const double overlap=radius-std::abs(glm::dot(delta,axis));
                if(overlap<minimum){minimum=overlap;minimumAxis=axis;}return overlap>=0;
            };
            for(unsigned i=0;i<3;++i)if(!test(a.axes[i]) || !test(b.axes[i]))return 0;
            for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)if(!test(glm::cross(a.axes[i],b.axes[j])))return 0;
            return minimum;
        }
        static void JitterOverlaps(App& app,std::uint64_t tick)
        {
            using namespace GEngine;auto& world=World(app);auto* f=jitterFiles[5];
            for(unsigned i=0;i<jitterBodies;++i)for(unsigned j=i+1;j<jitterBodies;++j)
            {
                const auto ar=world.ReadBody(jitterBody[i].handle),br=world.ReadBody(jitterBody[j].handle);
                if(!ar || !br){Check(false,"jitter overlap identity disappeared");return;}const auto& a=*ar;const auto& b=*br;
                if(a.type==Component::BodyType::Static && b.type==Component::BodyType::Static)continue;
                double depth=0;glm::dvec3 axis{};unsigned kind=0;
                if(a.shapeType==ShapeType::Box && b.shapeType==ShapeType::Box){depth=JitterBoxOverlap(JitterObb(a),JitterObb(b),axis);kind=1;}
                else if((a.shapeType==ShapeType::Sphere && b.shapeType==ShapeType::Box) || (a.shapeType==ShapeType::Box && b.shapeType==ShapeType::Sphere))
                {
                    const auto& sphere=a.shapeType==ShapeType::Sphere?a:b;const auto& boxBody=a.shapeType==ShapeType::Box?a:b;
                    const auto box=JitterObb(boxBody);const auto local=glm::transpose(box.axes)*(glm::dvec3(sphere.position)-box.center);
                    const auto q=glm::abs(local)-box.half;const auto outside=glm::max(q,glm::dvec3(0));
                    const double signedDistance=glm::length(outside)+(std::min)((std::max)(q.x,(std::max)(q.y,q.z)),0.0);
                    depth=(sphere.localBounds.maxs.x-sphere.localBounds.mins.x)*.5-signedDistance;kind=2;
                    const auto difference=local-glm::clamp(local,-box.half,box.half);
                    if(glm::length(difference)>1e-12)axis=box.axes*glm::normalize(difference);
                }
                else if(a.shapeType==ShapeType::Sphere && b.shapeType==ShapeType::Sphere)
                {
                    const auto delta=glm::dvec3(b.position)-glm::dvec3(a.position);const double distance=glm::length(delta);
                    depth=.5*double(a.localBounds.maxs.x-a.localBounds.mins.x+b.localBounds.maxs.x-b.localBounds.mins.x)-distance;kind=3;
                    if(distance>1e-12)axis=delta/distance;
                }
                if(depth>0)
                {
                    jitterMaxOverlap=(std::max)(jitterMaxOverlap,depth);std::fprintf(f,"%llu",(unsigned long long)tick);
                    JitterHandle(f,a.handle);JitterHandle(f,b.handle);std::fprintf(f,",%u,%.17g,%.17g,%.17g,%.17g\n",kind,depth,axis.x,axis.y,axis.z);
                }
            }
        }
        static bool JitterPair(App& app,const GEngine::RigidBody3D* a,const GEngine::RigidBody3D* b) noexcept
        { return a && b && World(app).IsBodyValid(GEngine::PhysicsTestAccess::Handle(World(app),*a)) &&
            World(app).IsBodyValid(GEngine::PhysicsTestAccess::Handle(World(app),*b)); }
        static void JitterEvent(App& app,const GEngine::PhysicsDiagnostic::Event& event) noexcept
        {
            ContactCorrectionMemory::Pause recording;
            ContactCorrectionMemory::Refresh(event.reason,event.duration);
            if(failure)return;using namespace GEngine;auto* f=jitterFiles[3];
            // All-tick position-refresh totals are measured without expanding
            // the original full-event CSV windows to every solver operation.
            if(!JitterDetailed(jitterTick) && (event.reason<26001 || event.reason>26003))return;
            // Gravity/integration/sleep state is captured at the immutable stage boundaries.
            if(event.operation==PhysicsDiagnostic::Operation::GravityImpulse || event.operation==PhysicsDiagnostic::Operation::Integrate ||
                event.operation==PhysicsDiagnostic::Operation::ToiQuery)return;
            std::fprintf(f,"%llu,%llu,%u,%u",(unsigned long long)jitterTick,(unsigned long long)++jitterEvents,jitterStage,unsigned(event.operation));
            JitterHandle(f,PhysicsTestAccess::Handle(World(app),*event.a));JitterHandle(f,PhysicsTestAccess::Handle(World(app),*event.b));
            std::fprintf(f,",%d,%d,%d,%llu,%llu,%llu,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%d",event.slot,event.previousSlot,event.reason,
                (unsigned long long)event.featureA,(unsigned long long)event.featureB,(unsigned long long)event.constraintToken,
                event.penetration,event.closingSpeed,event.time,event.duration,event.bias,event.restitution,int(event.appliedImpulsesKnown));
            for(const auto& v:{event.normal,event.anchorA,event.anchorB,event.localA,event.localB,event.tangentU,event.tangentV,
                event.linearImpulseA,event.linearImpulseB,event.angularImpulseA,event.angularImpulseB})JitterVector(f,v);
            for(double v:event.accumulatedBefore)std::fprintf(f,",%.17g",v);for(double v:event.accumulatedAfter)std::fprintf(f,",%.17g",v);
            for(const auto& s:{event.beforeA,event.beforeB,event.afterA,event.afterB})JitterState(f,s);std::fputc('\n',f);
        }
        static void JitterBoundary(App& app,GEngine::PhysicsDiagnostic::Stage stage,const GEngine::PhysicsWorld& world,
            GEngine::ManifoldCollector& manifolds,std::size_t scratch,int iterations) noexcept
        {
            ContactCorrectionMemory::Pause recording;
            using namespace GEngine;if(failure)return;jitterStage=unsigned(stage);
            if(stage==PhysicsDiagnostic::Stage::Begin)
            {
                jitterTick=Steps(app)+1;jitterObserver.detailed=JitterDetailed(jitterTick);
                ContactCorrectionMemory::Begin(0,jitterTick);recording.resume=true;
            }
            ContactCorrectionMemory::stage=unsigned(stage);
            if(stage==PhysicsDiagnostic::Stage::End){ContactCorrectionMemory::End();recording.resume=false;}
            if(ContactCorrectionMemory::opened && stage==PhysicsDiagnostic::Stage::Integration)jitterObserver.detailed=true;
            if(stage==PhysicsDiagnostic::Stage::Correction)jitterObserver.detailed=JitterDetailed(jitterTick);
            std::array<double,3> energy{};double speed=0,angular=0;unsigned asleep=0;
            for(unsigned i=0;i<jitterBodies;++i)
            {
                auto b=world.ReadBody(jitterBody[i].handle);if(!b){Check(false,"jitter stage identity disappeared");return;}
                const auto e=JitterEnergy(*b,world.GetGravity());for(unsigned k=0;k<3;++k)energy[k]+=e[k];
                Check(JitterFinite(*b) && std::isfinite(e[0]+e[1]+e[2]),"jitter nonfinite stage state");
                asleep+=b->type==Component::BodyType::Dynamic && b->sleeping;
                speed=(std::max)(speed,double(glm::length(b->linearVelocity)));angular=(std::max)(angular,double(glm::length(b->angularVelocity)));
            }
            jitterContacts=unsigned(manifolds.GetContactCount());
            std::fprintf(jitterFiles[4],"%llu,%u,%u,%zu,%u,%u,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%d,%zu\n",
                (unsigned long long)jitterTick,unsigned(stage),jitterBodies,manifolds.m_Manifolds.size(),jitterContacts,asleep,
                energy[0],energy[1],energy[2],energy[0]+energy[1]+energy[2],speed,angular,iterations,scratch);
            // Every completed tick has the complete final manifold. Detailed windows
            // additionally retain admission, warm-start and solved manifold phases.
            if(stage==PhysicsDiagnostic::Stage::End || (JitterDetailed(jitterTick) &&
                (stage==PhysicsDiagnostic::Stage::Narrowphase || stage==PhysicsDiagnostic::Stage::WarmStart ||
                 stage==PhysicsDiagnostic::Stage::Solve || stage==PhysicsDiagnostic::Stage::Correction)))
                for(auto& manifold:manifolds.m_Manifolds)for(int slot=0;slot<manifold.GetNumContacts();++slot)
                {
                    const auto c=manifold.GetContact(slot);auto* f=jitterFiles[2];
                    Check(JitterPair(app,c.m_BodyA,c.m_BodyB),"jitter manifold invalid identity");if(failure)return;
                    std::fprintf(f,"%llu,%u",(unsigned long long)jitterTick,unsigned(stage));
                    JitterHandle(f,PhysicsTestAccess::Handle(world,*c.m_BodyA));JitterHandle(f,PhysicsTestAccess::Handle(world,*c.m_BodyB));
                    std::fprintf(f,",%d,%u,%u,%.9g,%.9g",slot,c.featureA,c.featureB,c.separationDistance,c.timeOfImpact);
                    for(const auto& v:{c.normal,c.ptOnA_WorldSpace,c.ptOnB_WorldSpace,c.ptOnA_LocalSpace,c.ptOnB_LocalSpace})JitterVector(f,v);
                    std::fputc('\n',f);++jitterContactRows;
                }
            if(stage==PhysicsDiagnostic::Stage::End)
            {
                Check(Steps(app)==jitterTick && jitterTick==jitterRecorded+1,"jitter completed tick sequence");
                JitterWriteBodies(app,jitterTick);JitterOverlaps(app,jitterTick);jitterRecorded=jitterTick;
            }
        }
        static void JitterBegin(App& app)
        {
            using namespace GEngine;auto& scene=*app.m_ActiveScene;auto& world=World(app);
            Check(Steps(app)==0 && world.GetJointCount()==0 && world.GetBodyCount()<=jitterBody.size(),"jitter initial fixture bounds");
            Check(app.m_ActiveSelection.scene==(jitterCase==JitterCase::Box?Preset::BoxStack:
                jitterCase==JitterCase::Lattice?Preset::SphereLattice:Preset::SphereBoxStack),"jitter wrong fixture");
            Check(app.SandboxEnabled()==(jitterCase!=JitterCase::Qualification),"jitter wrong environment lane");
            if(failure)return;jitterBodies=unsigned(world.GetBodyCount());
            unsigned staticBoxes=0,dynamicBoxes=0,dynamicSpheres=0;
            for(std::size_t i=0;i<world.GetBodyCount();++i)
            {
                const auto b=world.ReadBody(*world.BodyAt(i));Check(bool(b),"jitter initial membership read failed");if(!b)return;
                staticBoxes+=b->type==Component::BodyType::Static && b->shapeType==ShapeType::Box;
                dynamicBoxes+=b->type==Component::BodyType::Dynamic && b->shapeType==ShapeType::Box;
                dynamicSpheres+=b->type==Component::BodyType::Dynamic && b->shapeType==ShapeType::Sphere;
            }
            Check(staticBoxes==5 && dynamicBoxes==(jitterCase==JitterCase::Lattice?0u:16u) &&
                dynamicSpheres==(jitterCase==JitterCase::Lattice?180u:jitterCase==JitterCase::Box?0u:1u) &&
                jitterBodies==staticBoxes+dynamicBoxes+dynamicSpheres,"jitter exact fixture membership differs");
            if(failure)return;
            // One identical presentation camera for every case; no Physics or
            // interpolation setting is touched. Diagnostics retain the native fixture.
            const Vec target{3,3,0},offset{16,12,24};const float distance=glm::length(offset);
            app.m_CameraView=App::CameraView::Editor;
            App::FocusMaterialCamera(app.m_EditorCamera_,target,std::asin(offset.y/distance),std::atan2(-offset.x,offset.z),distance);
            app.m_IsShowDebugBoundingBox=false;app.m_ShowSelectedColliderOutlines=false;
            for(unsigned i=0;i<jitterBodies;++i)jitterBody[i].handle=*world.BodyAt(i);
            for(auto e:scene.ReadRegistry().view<Component::RigidBody3DComponent>())
            {
                const auto handle=scene.ReadRegistry().get<Component::RigidBody3DComponent>(e).RuntimeBody;
                for(unsigned i=0;i<jitterBodies;++i)if(jitterBody[i].handle==handle)
                {
                    jitterBody[i].entity=e;auto identity=scene.RenderData().Identify(e);
                    Check(bool(identity),"jitter render identity missing");if(identity)jitterBody[i].render=*identity;
                }
            }
            for(unsigned i=0;i<jitterBodies;++i)
            {
                const auto& item=jitterBody[i];Check(item.entity!=entt::null,"jitter body/entity binding missing");if(failure)return;
                const auto b=world.ReadBody(item.handle);Check(bool(b),"jitter initial body missing");if(!b)return;
                const auto& name=scene.ReadRegistry().get<Component::TagComponent>(item.entity).Name;auto* f=jitterFiles[0];
                std::fprintf(f,"%u,\"",i);for(char c:name){if(c=='\"')std::fputc('\"',f);std::fputc(c,f);}std::fputc('\"',f);
                JitterHandle(f,item.handle);JitterHandle(f,item.render);
                std::fprintf(f,",%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.17g",unsigned(b->type),unsigned(b->shapeType),b->inverseMass,
                    b->material.friction,b->material.restitution,b->material.spinResistanceLength,b->material.rollingResistanceLength,
                    b->sleep.linearSpeedThreshold,b->sleep.angularSpeedThreshold,b->sleep.inactivitySeconds);
                JitterVector(f,b->localBounds.mins);JitterVector(f,b->localBounds.maxs);JitterVector(f,b->position);JitterQuaternion(f,b->orientation);std::fputc('\n',f);
            }
            jitterObserver={};jitterObserver.context=&app;jitterObserver.targeted=true;
            jitterObserver.select=jitterObserver.valid=[](void* p,const RigidBody3D* a,const RigidBody3D* b) noexcept{return JitterPair(*static_cast<App*>(p),a,b);};
            jitterObserver.event=[](void* p,const PhysicsDiagnostic::Event& e) noexcept{JitterEvent(*static_cast<App*>(p),e);};
            jitterObserver.boundary=[](void* p,PhysicsDiagnostic::Stage s,const PhysicsWorld& w,ManifoldCollector& m,std::span<const ContactIsland>,std::size_t bytes,int iterations) noexcept
                {JitterBoundary(*static_cast<App*>(p),s,w,m,bytes,iterations);};
            jitterObserver.violation=[](void*) noexcept{Check(false,"jitter observer changed Physics state");};
            PhysicsDiagnostic::observer=&jitterObserver;initialized=jitterStarted=true;
            JitterWriteBodies(app,0);JitterOverlaps(app,0);
            if(app.SandboxEnabled())app.SetSimulationState(State::Running);
        }
        static void JitterBeforeUpdate(App& app,GEngine::Timestep& elapsed)
        {
            current=&app;++frames;Check(frames<=6000,"jitter frame cap");if(failure)return;
            if(jitterFinal){jitterHostSeconds=0;elapsed=GEngine::Timestep(0.0);return;}
            if(!jitterStarted)
            {
                if(jitterCase==JitterCase::Switch)
                {
                    if(!jitterSwitching)
                    {
                        Check(app.m_ActiveSelection==SceneSelection{Preset::DistanceChain,8} && app.m_SimulationState==State::Ready && Steps(app)==0,"jitter switch must start C8 Ready");
                        previousScene=app.m_ActiveScene;previousBody=*World(app).BodyAt(0);
                        Check(app.RequestScene({Preset::SphereBoxStack,8}),"jitter scene request rejected");jitterSwitching=true;
                    }
                    elapsed=GEngine::Timestep(0.0);return;
                }
                JitterBegin(app);
            }
            double seconds=jitterCase==JitterCase::Cadence?(frames%2?1.0/120.0:1.0/40.0):1.0/60.0;
            const double remaining=(jitterTarget-Steps(app))*app.m_ActiveScene->GetPhysicsStep().count()-app.m_ActiveScene->GetPhysicsTiming().pendingSeconds;
            seconds=(std::min)(seconds,(std::max)(0.0,remaining));jitterHostSeconds=seconds;elapsed=GEngine::Timestep(seconds);
        }
        static void JitterAfterUpdate(App& app)
        {
            if(failure)return;
            if(jitterSwitching && !jitterStarted)
            {
                Check(previousScene.expired() && !World(app).ReadBody(previousBody) && app.m_SimulationState==State::Ready && Steps(app)==0,"jitter scene transaction did not retire C8");
                ++successful;JitterBegin(app);return;
            }
            completed=Steps(app);Check(completed==jitterRecorded && completed<=jitterTarget,"jitter capture skipped a completed tick");
            const auto& physics=*app.m_ActiveScene->GetPhysicsSystem();
            ContactCorrectionMemory::Update(frames,completed,app.m_ActiveScene->GetPhysicsTiming().stepsLastUpdate,
                physics.GetLastUpdateNanoseconds(),physics.GetContactSolveNanoseconds(),physics.GetJointSolveNanoseconds());
            Check(!ContactCorrectionMemory::failed && !ContactCorrectionMemory::overflow,"contact allocation recorder failure or overflow");
            for(auto* f:jitterFiles)Check(f && !std::ferror(f),"jitter evidence write failed");
            if(completed==jitterTarget && !jitterFinal){jitterFinal=true;jitterTerminalFrame=frames;GEngine::PhysicsDiagnostic::observer=nullptr;}
        }
        static void JitterObserveFrame(App& app,const GEngine::RenderFrame& frame) noexcept
        {
            if(!jitterStarted || failure)return;using namespace GEngine;auto& scene=*app.m_ActiveScene;auto* f=jitterFiles[6];
            for(unsigned i=0;i<jitterBodies;++i)
            {
                const auto& item=jitterBody[i];const _Entity entity(item.entity,&scene);const auto pose=scene.ReadBodyTransform(entity);
                const auto body=World(app).ReadBody(item.handle);Check(bool(pose) && bool(body),"jitter render/body pose read failed");if(!pose || !body)return;
                const auto draw=std::find_if(frame.Draws().begin(),frame.Draws().end(),[&](const auto& d){return d.entity==item.render;});
                const bool visible=draw!=frame.Draws().end();const glm::mat4 model=visible?draw->worldTransform:glm::mat4(0);
                const auto scale=scene.ReadRegistry().get<Component::Transform3DComponent>(item.entity).Scale;
                const auto expected=glm::translate(glm::mat4(1),pose->presentation.translation)*glm::toMat4(pose->presentation.rotation)*glm::scale(glm::mat4(1),scale);
                double matrixError=0;if(visible)for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)matrixError=(std::max)(matrixError,std::abs(double(model[c][r])-expected[c][r]));
                std::fprintf(f,"%u,%llu,%u",frames,(unsigned long long)Steps(app),i);JitterHandle(f,item.handle);
                std::fprintf(f,",%.17g,%.17g,%d",scene.GetRenderInterpolationAlpha(),scene.GetPhysicsTiming().pendingSeconds,int(visible));
                JitterVector(f,body->position);JitterQuaternion(f,body->orientation);JitterPose(f,pose->previous);JitterPose(f,pose->current);JitterPose(f,pose->presentation);
                for(unsigned column=0;column<4;++column)for(unsigned row=0;row<4;++row)std::fprintf(f,",%.9g",model[column][row]);
                std::fprintf(f,",%.17g,%.17g,%.17g,%.17g\n",double(glm::length(pose->presentation.translation-body->position)),JitterAngle(pose->presentation.rotation,body->orientation),
                    visible?double(glm::length(Vec(model[3])-pose->presentation.translation)):0.0,matrixError);++jitterRenderRows;
            }
        }
        static bool JitterCapture(App& app,const std::string& name)
        {
            const auto& target=*app.m_RenderTarget;const auto& size=target.Buffer().Description();
            Check(size.Width && size.Height && size.Width<=4096 && size.Height<=4096,"jitter final capture extent invalid");if(failure)return false;
            std::vector<std::byte> bytes(std::size_t(size.Width)*size.Height*4);Check(bool(target.ReadColor(bytes)),"jitter final capture read failed");if(failure)return false;
            auto* f=std::fopen((output/name).string().c_str(),"wbx");Check(f!=nullptr,"jitter final capture create failed");if(!f)return false;
            bool ok=std::fprintf(f,"P6\n%u %u\n255\n",size.Width,size.Height)>0;
            for(unsigned y=size.Height;y>0;--y)for(unsigned x=0;x<size.Width;++x)ok=(std::fwrite(bytes.data()+((y-1)*size.Width+x)*4,1,3,f)==3) && ok;
            ok=std::fclose(f)==0 && ok;Check(ok,"jitter final capture write failed");
            std::fprintf(jitterFiles[8],"%s,%u,%llu,%u,%u,%u\n",name.c_str(),frames,(unsigned long long)Steps(app),jitterHeldFrames,size.Width,size.Height);return ok;
        }
        static void JitterOnFrame(App& app,const GEngine::ScheduledFrameStats& stats)
        {
            if(!jitterStarted || failure)return;const auto& scene=*app.m_ActiveScene;const auto& timing=scene.GetPhysicsTiming();
            const auto& target=*app.m_RenderTarget;const auto& size=target.Buffer().Description();
            const auto camera=app.m_EditorCamera_.GetPosition();
            std::fprintf(jitterFiles[7],"%u,%llu,%.17g,%.17g,%.17g,%u,%.17g,%d,%zu,%zu,%zu,%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g\n",frames,(unsigned long long)Steps(app),
                jitterHostSeconds,timing.pendingSeconds,timing.discardedSeconds,timing.stepsLastUpdate,scene.GetRenderInterpolationAlpha(),int(scene.IsRenderInterpolationEnabled()),
                stats.extraction.sceneEntities,stats.extraction.draws,stats.submission.submittedDrawCalls,size.Width,size.Height,camera.x,camera.y,camera.z,app.m_EditorCamera_.GetPitch(),app.m_EditorCamera_.GetYaw());
            if(Steps(app)+24>=jitterTarget && (!jitterFinal || frames==jitterTerminalFrame))
            {if(JitterCapture(app,"jitter-motion-"+std::to_string(frames)+".ppm"))++jitterMotionCaptures;}
            if(!jitterFinal || frames==jitterTerminalFrame)return;
            ++jitterHeldFrames;
            if(jitterHeldFrames==1 || jitterHeldFrames==12)
                if(JitterCapture(app,jitterHeldFrames==1?"jitter-frozen-first.ppm":"jitter-final.ppm"))++jitterFinalCaptures;
            if(jitterHeldFrames<12 || failure)return;
            finalScene=app.m_ActiveScene;jitterDone=true;app.ShutDown();
        }
        static bool FinishJitter()
        {
            if(finished)return false;finished=true;current=nullptr;GEngine::PhysicsDiagnostic::observer=nullptr;
            Check(!ContactCorrectionMemory::opened || ContactCorrectionMemory::ordinal==jitterRecorded,"contact allocation tick coverage");
            Check(ContactCorrectionMemory::Finish(output),"contact allocation evidence incomplete");
            for(auto*& f:jitterFiles)if(f){const bool ok=!std::ferror(f);Check(std::fclose(f)==0 && ok,"jitter evidence close failed");f=nullptr;}
            const bool clean=initialized && finalScene.expired() && previousScene.expired();
            const bool pass=!failure && jitterDone && clean && jitterRecorded==jitterTarget && completed==jitterTarget && jitterFinalCaptures==2 && jitterHeldFrames==12;
            auto* f=std::fopen((output/"result.json").string().c_str(),"wx");if(!f)return false;
            std::fprintf(f,"{\"diagnostic_only\":true,\"case\":\"%s\",\"pass\":%s,\"pass_meaning\":\"capture validity only; no jitter or Physics acceptance disposition\",\"failure\":\"%s\",\"clean_shutdown\":%s,\"completed_ticks\":%llu,\"frames\":%u,\"body_count\":%u,\"successful_transitions\":%u,\"events\":%llu,\"contact_rows\":%llu,\"render_rows\":%llu,\"final_captures\":%u,\"frozen_frames\":%u,\"moving_captures\":%u,\"max_actual_overlap_m\":%.17g,\"max_speed\":%.17g,\"max_angular_speed\":%.17g,\"detailed_windows\":\"%s\",\"physics_mutations\":0,\"gpu_timing\":\"NOT_MEASURED\"}\n",
                jitterNames[unsigned(jitterCase)],pass?"true":"false",failure?failure:"none",clean?"true":"false",(unsigned long long)completed,frames,jitterBodies,successful,
                (unsigned long long)jitterEvents,(unsigned long long)jitterContactRows,(unsigned long long)jitterRenderRows,jitterFinalCaptures,jitterHeldFrames,jitterMotionCaptures,jitterMaxOverlap,jitterMaxSpeed,jitterMaxAngular,
                jitterCase==JitterCase::Lattice?"1-120;target-119..target":"1-240;601-720;1201-1320;2161-2400");
            const bool written=!std::ferror(f);return std::fclose(f)==0 && written && pass;
        }

        // Reuse the exact existing D26-ENTRY-02 Session. RbsScene/RbsFrame own
        // construction, episode changes, fixed stepping, the tick300 impulse,
        // termination, and every legacy physical/numerical criterion.
        static void ContactRegressionEvent(void* context,const GEngine::PhysicsDiagnostic::Event& event) noexcept
        {
            ContactCorrectionMemory::Pause recording;
            ContactCorrectionMemory::Refresh(event.reason,event.duration);
            RestDiagnosis::Session::Phase21Event(context,event);
        }
        static void ContactRegressionBoundary(void* context,GEngine::PhysicsDiagnostic::Stage stage,
            const GEngine::PhysicsWorld& world,GEngine::ManifoldCollector& manifolds,
            std::span<const GEngine::ContactIsland> islands,std::size_t bytes,int passes) noexcept
        {
            ContactCorrectionMemory::Pause recording;
            auto& diagnostic=*static_cast<RestDiagnosis::Session*>(context);
            if(stage==GEngine::PhysicsDiagnostic::Stage::Begin)
            {
                ContactCorrectionMemory::Begin(diagnostic.episode,diagnostic.tick+1);recording.resume=true;
            }
            ContactCorrectionMemory::stage=unsigned(stage);
            if(stage==GEngine::PhysicsDiagnostic::Stage::End){ContactCorrectionMemory::End();recording.resume=false;}
            RestDiagnosis::Session::Boundary(context,stage,world,manifolds,islands,bytes,passes);
        }
        static void ContactRegressionBeforeUpdate(App& app,GEngine::Timestep& elapsed)
        {
            current=&app;++frames;elapsed=GEngine::Timestep(1.0/60.0);
            const auto* diagnostic=RestDiagnosis::session.get();
            Check(frames<=6000 && diagnostic && diagnostic->entry26 && diagnostic->phase21 &&
                !diagnostic->failed && diagnostic->phase21->totalTicks<=1800,"contact regression Session/protocol bound");
            if(!initialized)
            {
                initialized=true;finalScene=app.m_ActiveScene;
                Check(!app.SandboxEnabled() && app.m_Launch.restingContactDiagnostic.has_value() &&
                    app.m_ActiveScene && !app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld(),
                    "contact regression must retain the original non-rendered owned fixture");
            }
        }
        static void ContactRegressionAfterUpdate(App& app)
        {
            auto* diagnostic=RestDiagnosis::session.get();
            Check(diagnostic && diagnostic->entry26 && !diagnostic->failed && !ContactCorrectionMemory::failed &&
                !ContactCorrectionMemory::overflow,"contact regression observer failure");
            if(failure || !diagnostic)return;
            // BeginEpisode resets these callbacks. This hook runs after its
            // existing setup and before RbsFrame calls FrameComplete; no update
            // or stimulus is added and the Session remains the observer owner.
            diagnostic->sink.event=&ContactRegressionEvent;
            diagnostic->sink.boundary=&ContactRegressionBoundary;
        }
        static bool FinishContactRegression()
        {
            if(finished)return false;finished=true;current=nullptr;
            auto* diagnostic=RestDiagnosis::session.get();
            const bool clean=initialized && finalScene.expired() && diagnostic &&
                !diagnostic->sixtyHzScene && diagnostic->entities.empty() && diagnostic->ownedShapes.empty();
            completed=diagnostic && diagnostic->phase21?diagnostic->phase21->totalTicks:0;
            Check(ContactCorrectionMemory::ordinal==completed && completed==1800,"contact regression allocation/tick coverage");
            const bool native=diagnostic && diagnostic->entry26 && diagnostic->FinishAfterAppShutdown(clean);
            Check(ContactCorrectionMemory::Finish(output),"contact regression allocation evidence incomplete");
            const bool pass=native && clean && !failure;
            auto* f=std::fopen((output/"adapter-result.json").string().c_str(),"wx");
            if(!f){RestDiagnosis::session.reset();return false;}
            std::fprintf(f,"{\"diagnostic_only\":true,\"case\":\"contact-regression\",\"pass\":%s,\"clean_shutdown\":%s,\"completed_ticks\":%llu,\"frames\":%u,\"failure\":\"%s\",\"protocol\":\"unchanged D26-ENTRY-02 Stack60 1200 plus Wake60 600\",\"cpu_source\":\"original Session metrics.csv; allocation stage-window timing is supplemental\",\"rendered_physics_fixture\":false,\"adapter_physics_mutations\":0}\n",
                pass?"true":"false",clean?"true":"false",(unsigned long long)completed,frames,failure?failure:"none");
            const bool written=!std::ferror(f);const bool closed=std::fclose(f)==0;
            RestDiagnosis::session.reset();return pass && written && closed;
        }

        static void Check(bool ok, const char* message)
        {
            if (!ok && !failure) { failure = message; std::fprintf(stderr,"SANDBOX FAIL: %s\n",message); }
            if (failure && current) current->ShutDown();
        }
        static bool Configure(std::vector<std::string_view>& args)
        {
            auto at=std::find(args.begin(),args.end(),"--phase26-sandbox-output");
            if(at==args.end() || at+1==args.end())return false;
            output=std::string(*(at+1));args.erase(at,at+2);
            if(!output.is_absolute() || !std::filesystem::is_directory(output) ||
                std::filesystem::exists(output/"result.json"))return false;
            auto parser=PreEditorValidation::CheckLaunchConfig();
            if(!parser){failure="typed launch checks";return false;}
            if(auto mode=std::find(args.begin(),args.end(),"--phase26-sandbox-contact-regression");mode!=args.end())
            {
                args.erase(mode);contactRegression=true;
                return args.empty() && !std::filesystem::exists(output/"adapter-result.json") && ContactCorrectionMemory::Open(output);
            }
            if(std::find(args.begin(),args.end(),"--phase26-sandbox-jitter")!=args.end())return ConfigureJitter(args);
            for(const auto option:{"--phase26-sandbox-visual-compare","--phase26-sandbox-visual-final"})
                if(auto mode=std::find(args.begin(),args.end(),option);mode!=args.end())
                {
                    visualMode=true;visualCompare=std::string_view(option)=="--phase26-sandbox-visual-compare";
                    args.erase(mode);
                    if(!visualCompare)
                    {
                        auto density=std::find(args.begin(),args.end(),"--phase26-sandbox-visual-density");
                        if(density==args.end() || density+1==args.end())return false;
                        const auto value=*(density+1);
                        visualDensity=value=="32"?32:value=="24"?24:value=="16"?16:0;
                        if(!visualDensity)return false;
                        args.erase(density,density+2);
                    }
                    return ConfigureVisual();
                }
            if(auto mode=std::find(args.begin(),args.end(),"--phase26-sandbox-materials");mode!=args.end())
            {
                materialMode=true;args.erase(mode);
                return ConfigureMaterials();
            }
            ticks=std::fopen((output/"ticks.csv").string().c_str(),"wx");
            transitions=std::fopen((output/"transitions.csv").string().c_str(),"wx");
            render=std::fopen((output/"render.csv").string().c_str(),"wx");
            if(!ticks || !transitions || !render)return false;
            std::fprintf(ticks,"transition,preset,links,tick,state,bodies,joints,energy_J,external_work_J,max_joint_error_m,sphere_room_overlap_m,g8_contacts,g8_ground_mask,asleep,applications,pending_seconds\n");
            std::fprintf(transitions,"transition,preset,links,old_registry,new_registry,old_scene_expired,stale_body_rejected,stale_joint_rejected,reset\n");
            std::fprintf(render,"frame,transition,preset,links,scene_entities,logical_draws,submitted_draw_calls,instances,meshes,materials,frame_storage_bytes,preparation_us,extraction_us,debug_buffer_bytes,gpu_timing\n");
            std::printf("Sandbox typed launch checks PASS: %u\n",*parser);
            return true;
        }
        static auto& World(App& app) { return *app.m_ActiveScene->GetPhysicsSystem()->GetPhysicsWorld(); }
        static std::uint64_t Steps(App& app) { return app.m_ActiveScene->GetPhysicsSystem()->GetCompletedSteps(); }
        static unsigned Key(SceneSelection value)
        {
            return value.scene==Preset::DistanceChain ? 5+(value.chainLinks==4?0:value.chainLinks==8?1:2) :
                value.scene==Preset::DistanceChainContact ? 8 : static_cast<unsigned>(value.scene);
        }
        static Vec Vector(const std::array<float,3>& v) { return {v[0],v[1],v[2]}; }
        static bool Near(Vec a,Vec b) { return glm::length(a-b)<1e-4f; }
        static void SavePrevious(App& app)
        {
            previousScene=app.m_ActiveScene;previousSelection=app.m_ActiveSelection;
            auto& w=World(app);previousBody=w.GetBodyCount()?*w.BodyAt(0):Body{};
            previousJoint=w.GetJointCount()?*w.JointAt(0):Joint{};
            auto entity=app.m_ActiveScene->FindEntityByName(SandboxEnvironment::Boxes()[0].name);
            if(entity)
            {
                auto identity=app.m_ActiveScene->RenderData().Identify(entity);
                previousRender=identity?*identity:GEngine::EntityRenderId{};
                previousMesh=entity.GetComponent<GEngine::Component::MeshRendererComponent>().mesh;
            }
        }
        static bool ConfigureMaterials()
        {
            ticks=std::fopen((output/"ticks.csv").string().c_str(),"wx");
            transitions=std::fopen((output/"transitions.csv").string().c_str(),"wx");
            render=std::fopen((output/"render.csv").string().c_str(),"wx");
            materialLog=std::fopen((output/"materials.csv").string().c_str(),"wx");
            uvLog=std::fopen((output/"uv.csv").string().c_str(),"wx");
            captureLog=std::fopen((output/"captures.csv").string().c_str(),"wx");
            if(!ticks || !transitions || !render || !materialLog || !uvLog || !captureLog)return false;
            std::fprintf(ticks,"frame,transition,preset,tick,state,bodies,joints,pending_seconds\n");
            std::fprintf(transitions,"transition,preset,old_registry,new_registry,old_scene_expired,stale_body_rejected,stale_joint_rejected,reset\n");
            std::fprintf(render,"frame,transition,draws,submitted_draw_calls,meshes,materials,preparation_us,extraction_us,gpu_timing\n");
            std::fprintf(materialLog,"transition,room,body_index,body_generation,body_registry,mesh_index,mesh_generation,mesh_registry,material_index,material_generation,material_registry,semantic,texture_index,texture_generation,texture_registry,sampler_index,sampler_generation,sampler_registry,width,height,color_space,wrap_u,wrap_v\n");
            std::fprintf(uvLog,"transition,room,triangle,face,world_x,world_y,world_z,u,v,dp_du_x,dp_du_y,dp_du_z,dp_dv_x,dp_dv_y,dp_dv_z,world_units_per_u,world_units_per_v,orthogonality_error\n");
            std::fprintf(captureLog,"file,frame,transition,tick,width,height,camera_x,camera_y,camera_z,pitch,yaw\n");
            if(!visualMode)std::printf("Sandbox focused material mode: three transitions; six captures; maximum24ticks/600frames\n");
            return true;
        }
        static bool ConfigureVisual()
        {
            if(!ConfigureMaterials())return false;
            visualLog=std::fopen((output/"visual-frames.csv").string().c_str(),"wx");
            visualSamples=std::fopen((output/"visual-samples.csv").string().c_str(),"wx");
            if(!visualLog || !visualSamples)return false;
            std::fprintf(visualLog,"frame,transition,variant,density,stage,state,tick,scope,selected_entities,debug_lines,applications,pending_command,point_x,point_y,point_z,point_intensity,point_range,cascade4\n");
            std::fprintf(visualSamples,"sample,transition,variant,density,first_frame,first_material_row,first_uv_row,uv_scale,point_x,point_y,point_z,point_intensity,point_range,cascade4\n");
            std::printf("Sandbox visual %s: maximum1800frames/300ticks; scripted workload %u ticks\n",
                visualCompare?"comparison":"final",visualCompare?0:16);
            return true;
        }
        static const char* VisualVariantName() { return visualCompare?visualNames[visualVariant]:"final"; }
        static unsigned VisualViewCount() { return successful==0?6:successful==1?5:successful==2?1:0; }
        static void VisualApply(App& app)
        {
            using namespace GEngine;
            if(visualCompare)
            {
                visualDensity=visualDensities[visualVariant];
                // Existing publication boundary and typed material edits. Shared
                // wall material is edited once; no texture or mesh replacement.
                Check(app.m_SceneResources->Publication().CanPublish(),"visual variant outside publication boundary");
                for(unsigned i=0;i<roomMaterials.size();++i)
                {
                    auto surface=app.m_SceneResources->DescribeMaterial(roomMaterials[i]);
                    Check(bool(surface),"visual variant material unavailable");if(!surface)return;
                    surface->uvTiling={visualDensity/32.f,visualDensity/32.f};
                    bool duplicate=false;for(unsigned j=0;j<i;++j)duplicate|=roomMaterials[j]==roomMaterials[i];
                    if(!duplicate)Check(bool(app.m_SceneResources->EditMaterial(roomMaterials[i],*surface)),"visual variant edit rejected");
                    roomDescriptions[i]=*surface;
                }
                const bool candidate=visualVariant!=0;
                auto point=app.m_PointLightEntity.GetComponent<Component::RenderLightComponent>();
                point.color={1,1,1};point.intensity=candidate?1.f:.12f;point.range=candidate?200.f:100.f;point.castShadows=false;
                app.m_PointLightEntity.AddOrReplaceComponent<Component::RenderLightComponent>(point);
                app.m_LightPos=candidate?Vec(0,45,0):Vec(0,15,-10);
                const auto moved=app.m_ActiveScene->SetLocalTranslation(app.m_PointLightEntity,app.m_LightPos);
                Check(moved && moved->notification,"visual point-light pose or notification rejected");
                app.m_ShadowCascadeLevels={20,40,100,candidate?320.f:500.f,1000};
            }
            materialFrameMask=sceneFrames=0;visualView=visualWait=0;
            const auto& point=app.m_PointLightEntity.GetComponent<Component::RenderLightComponent>();
            Check(app.m_ShadowCascadeLevels.size()==5,"visual cascade membership changed");if(failure)return;
            std::fprintf(visualSamples,"%u,%u,%s,%u,%u,%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                materialSceneChecks,successful,VisualVariantName(),visualDensity,frames,materialSceneChecks*25,materialTriangles,
                roomDescriptions[0].uvTiling[0],app.m_LightPos.x,app.m_LightPos.y,app.m_LightPos.z,
                point.intensity,point.range,app.m_ShadowCascadeLevels[3]);
        }
        static void VisualBegin(App& app)
        {
            Check(!app.m_IsShowDebugBoundingBox && !app.m_ShowSelectedColliderOutlines,"visual successful scene did not start Off");
            visualScope=visualVariant=0;applied=false;BeginMaterials(app);VisualApply(app);
            visualStage=VisualStage::Captures;
            if(VisualViewCount())VisualCamera(app);
        }
        static void VisualCamera(App& app)
        {
            if(successful==0){materialCapture=visualView;MaterialCamera(app);return;}
            Vec eye,target;
            if(successful==1)
            {
                target=Vec(9,1.5f,0);
                if(visualView==0)eye=Vec(10,5,9);
                else if(visualView==1)eye=Vec(13,10,28);
                else
                {
                    // Nine consecutive camera poses, sampled every third frame.
                    // Identical trajectory in every comparison group; Physics stays Ready.
                    const unsigned motion=(visualView-2)*3+visualWait;
                    eye=Vec(6.f+.3f*motion,5,12);
                }
            }
            else{target=Vec(0,19.5f,0);eye=Vec(22,22,70);}
            const auto offset=eye-target;const auto distance=glm::length(offset);
            app.m_CameraView=App::CameraView::Editor;
            App::FocusMaterialCamera(app.m_EditorCamera_,target,std::asin(offset.y/distance),std::atan2(-offset.x,offset.z),distance);
        }
        static void VisualSetScope(App& app,unsigned value)
        {
            visualScope=value;app.m_IsShowDebugBoundingBox=value==2;app.m_ShowSelectedColliderOutlines=value==1;
            if(value==1)
            {
                auto terminal=app.m_ActiveScene->FindEntityByName("Chain link "+std::to_string(app.m_ActiveSelection.chainLinks));
                Check(bool(terminal),"visual Selected terminal missing");if(terminal)app.SelectColliderEntity(terminal,false);
            }
        }
        static void VisualAdvance(App& app)
        {
            const unsigned limit=visualCompare?2:3;
            if(successful==limit){finalScene=app.m_ActiveScene;visualStage=VisualStage::Done;app.ShutDown();return;}
            SavePrevious(app);Check(app.RequestScene(visualSequence[successful]),"visual scene request rejected");
            visualStage=VisualStage::Switching;
        }
        static void VisualBeforeUpdate(App& app,GEngine::Timestep& elapsed)
        {
            current=&app;++frames;elapsed=GEngine::Timestep(visualCompare?0:1.0/60.0);
            Check(frames<=1800 && completed<=300,"visual execution cap");if(failure){app.ShutDown();return;}
            if(visualStage==VisualStage::Initial)
            {
                initialized=true;Check(app.m_ActiveSelection==DefaultSceneSelection,"visual mode changed Owner startup");
                VisualBegin(app);
            }
            if(visualStage==VisualStage::Captures)
            {
                elapsed=GEngine::Timestep(0);
                if(visualView<VisualViewCount()){VisualCamera(app);return;}
                if(sceneFrames<3)return;
                if(visualCompare)
                {
                    if(visualVariant<3){++visualVariant;VisualApply(app);VisualCamera(app);return;}
                    VisualAdvance(app);return;
                }
                visualStage=VisualStage::BeginRun;
            }
            if(visualStage==VisualStage::BeginRun)
            {
                constexpr std::array<unsigned,4> scopes{2,1,0,2};VisualSetScope(app,scopes[successful]);
                pausedSteps=Steps(app);elapsed=GEngine::Timestep(.2);
                if(successful==0 && !rollback)
                {
                    SavePrevious(app);Queue(app,true);app.m_InjectSceneConstructionFailure=true;
                    Check(app.RequestScene({Preset::DistanceChain,16}),"visual failure injection request rejected");
                    visualStage=VisualStage::Failure;return;
                }
                app.SetSimulationState(State::Running);visualStage=VisualStage::Starting;return;
            }
            if(visualStage==VisualStage::Run && Steps(app)>=2)
            {
                pausedSteps=Steps(app);app.SetSimulationState(State::Paused);Queue(app);
                visualStage=VisualStage::Pause;hold=0;elapsed=GEngine::Timestep(.2);return;
            }
            if(visualStage==VisualStage::Pause)
            {
                elapsed=GEngine::Timestep(.2);
                if(++hold>=3){app.SetSimulationState(State::Running);visualStage=VisualStage::Resuming;}
                return;
            }
            if(visualStage==VisualStage::AfterResume && Steps(app)>=4)
            {
                Check(Steps(app)==4 && applied && app.m_ChainImpulse.AppliedCount()==1,"visual impulse missing or repeated");
                VisualAdvance(app);
            }
        }
        static void VisualAfterUpdate(App& app)
        {
            using namespace GEngine;
            if(failure)return;
            if(visualStage==VisualStage::Failure)
            {
                rollback=app.m_ActiveScene.get()==previousScene.lock().get() && failedScene.expired() &&
                    app.m_ActiveSelection==previousSelection && app.m_SimulationState==State::Paused &&
                    World(app).IsBodyValid(previousBody) && !World(app).ReadBody(failedBody) &&
                    (!failedJoint || !World(app).ReadDistanceJoint(failedJoint)) && !app.m_ChainImpulse.Pending();
                Check(rollback && app.m_IsShowDebugBoundingBox && !app.m_ShowSelectedColliderOutlines,
                    "visual rollback lost old scene or manual All scope");
                visualStage=VisualStage::BeginRun;
            }
            if(visualStage==VisualStage::Switching)
            {
                auto& w=World(app);const auto next=*w.BodyAt(0);
                const bool oldBody=!w.ReadBody(previousBody),oldJoint=!previousJoint || !w.ReadDistanceJoint(previousJoint);
                Check(previousScene.expired() && oldBody && oldJoint && previousBody.registry!=next.registry &&
                    !app.m_ActiveScene->RenderData().Resolve(previousRender) && !app.m_SceneResources->MeshMetadata(previousMesh),
                    "visual switch retained old scene identities");
                Check(app.m_ActiveSelection==visualSequence[successful],"visual switch selection mismatch");
                ++successful;++rejectedOld;
                std::fprintf(transitions,"%u,%u,%llu,%llu,%d,%d,%d,%d\n",successful,unsigned(app.m_ActiveSelection.scene),
                    (unsigned long long)previousBody.registry,(unsigned long long)next.registry,previousScene.expired(),oldBody,oldJoint,
                    previousSelection==app.m_ActiveSelection);
                VisualBegin(app);
            }
            const auto steps=Steps(app);
            Check(app.m_IsShowDebugBoundingBox==(visualScope==2) && app.m_ShowSelectedColliderOutlines==(visualScope==1),
                "visual scope changed during playback or rollback");
            if(visualStage==VisualStage::Captures)Check(steps==0 && app.m_SimulationState==State::Ready,"visual capture advanced Physics");
            if(visualStage==VisualStage::Pause || visualStage==VisualStage::Starting || visualStage==VisualStage::Resuming)
                Check(steps==pausedSteps,"visual pause or first resume accumulated elapsed time");
            if(visualStage==VisualStage::Pause)Check(app.m_ChainImpulse.Pending() && !applied,"visual paused impulse applied early");
            if(visualStage==VisualStage::Starting)visualStage=VisualStage::Run;
            if(visualStage==VisualStage::Resuming)visualStage=VisualStage::AfterResume;
            Check(steps>=previousSteps && steps-previousSteps<=1,"visual fixed tick sequence");
            completed+=steps-previousSteps;previousSteps=steps;
            if(visualCompare)Check(completed==0,"visual comparison changed physical state");
            auto& w=World(app);
            for(std::size_t i=0;i<w.GetBodyCount();++i)
            {
                auto body=w.ReadBody(*w.BodyAt(i));
                Check(body && Math::IsFinite(body->position) && Math::IsFinite(body->orientation) &&
                    Math::IsFinite(body->linearVelocity) && Math::IsFinite(body->angularVelocity),"visual smoke nonfinite body");
            }
            Check(app.m_ActiveScene->GetPhysicsTiming().pendingSeconds==0,"visual smoke retained elapsed backlog");
            std::fprintf(ticks,"%u,%u,%u,%llu,%u,%zu,%zu,%.12g\n",frames,successful,unsigned(app.m_ActiveSelection.scene),
                (unsigned long long)steps,unsigned(app.m_SimulationState),w.GetBodyCount(),w.GetJointCount(),
                app.m_ActiveScene->GetPhysicsTiming().pendingSeconds);
        }
        static void VisualObserveFrame(App& app,const GEngine::RenderFrame& frame) noexcept
        {
            using namespace GEngine;
            MaterialsObserveFrame(app,frame);if(failure)return;
            visualDebugLines=frame.DebugLines().size();
            if(visualScope==0){Check(visualDebugLines==0,"visual Off emitted Physics debug lines");++visualOffFrames;}
            else
            {
                Check(visualDebugLines>0,"visual manual scope emitted no Physics debug lines");
                if(visualScope==1){Check(app.m_ColliderOutlines.count==1,"visual Selected scope lost terminal");++visualSelectedFrames;}
                else ++visualAllFrames;
            }
            const bool candidate=!visualCompare || visualVariant!=0;
            Check(frame.DirectionalLights().size()==1 && frame.PointLights().size()==1,"visual light membership changed");if(failure)return;
            const auto& point=frame.PointLights()[0];const auto& key=frame.DirectionalLights()[0];
            Check(Near(key.direction,-glm::normalize(Vec(20,50,20))) && key.color==Vec(1) && key.intensity==3 && key.shadows.castShadows &&
                Near(point.position,candidate?Vec(0,45,0):Vec(0,15,-10)) && point.color==Vec(1) &&
                point.intensity==(candidate?1.f:.12f) && point.range==(candidate?200.f:100.f) && !point.shadows.castShadows,
                "visual prepared lighting differs from frozen comparison settings");
            Check(app.m_ShadowCascadeLevels==std::vector<float>({20,40,100,candidate?320.f:500.f,1000}),
                "visual cascade configuration differs from frozen comparison settings");
        }
        static void VisualCapture(App& app)
        {
            constexpr std::array<const char*,6> roomNames{"room-wide","floor-close","wall-minus-x-close","wall-plus-x-close","wall-minus-z-close","wall-plus-z-close"};
            constexpr std::array<const char*,5> g8Names{"G8-contact-close","G8-Off","G8-motion-1","G8-motion-2","G8-motion-3"};
            const char* view=successful==0?roomNames[visualView]:successful==1?g8Names[visualView]:"C16-Off";
            const std::string name=std::string(VisualVariantName())+"-"+view+".ppm";
            Check(app.m_RenderTarget!=nullptr,"visual capture target missing");if(failure)return;
            const auto& target=*app.m_RenderTarget;const auto& size=target.Buffer().Description();
            Check(size.Width && size.Height && size.Width<=4096 && size.Height<=4096,"visual capture extent invalid");if(failure)return;
            if(!visualWidth){visualWidth=size.Width;visualHeight=size.Height;}
            Check(size.Width==visualWidth && size.Height==visualHeight,"visual comparison capture resolution changed");
            std::vector<std::byte> rgba(std::size_t(size.Width)*size.Height*4),rgb(std::size_t(size.Width)*size.Height*3);
            Check(bool(target.ReadColor(rgba)),"visual capture read failed");if(failure)return;
            for(unsigned y=0;y<size.Height;++y)for(unsigned x=0;x<size.Width;++x)
                std::memcpy(rgb.data()+(std::size_t(y)*size.Width+x)*3,rgba.data()+(std::size_t(size.Height-y-1)*size.Width+x)*4,3);
            auto* file=std::fopen((output/name).string().c_str(),"wbx");Check(file!=nullptr,"visual capture create failed");if(!file)return;
            bool ok=std::fprintf(file,"P6\n%u %u\n255\n",size.Width,size.Height)>0;
            ok=std::fwrite(rgb.data(),1,rgb.size(),file)==rgb.size() && ok;
            ok=std::fclose(file)==0 && ok;Check(ok,"visual capture write failed");
            const auto p=app.m_EditorCamera_.GetPosition();
            std::fprintf(captureLog,"%s,%u,%u,%llu,%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g\n",name.c_str(),frames,successful,
                (unsigned long long)Steps(app),size.Width,size.Height,p.x,p.y,p.z,app.m_EditorCamera_.GetPitch(),app.m_EditorCamera_.GetYaw());
            ++screenshots;++visualView;visualWait=0;
        }
        static void VisualOnFrame(App& app,const GEngine::ScheduledFrameStats& stats)
        {
            if(!initialized || failure)return;++sceneFrames;
            std::fprintf(render,"%u,%u,%zu,%zu,%zu,%zu,%.6f,%.6f,NOT_MEASURED\n",frames,successful,stats.extraction.draws,
                stats.submission.submittedDrawCalls,app.m_SceneResources->Meshes().Size(),app.m_SceneResources->Materials().Size(),
                stats.extraction.preparationMicroseconds,stats.extraction.extractionMicroseconds);
            Check(stats.extraction.draws>=5 && stats.submission.submittedDrawCalls>0,"visual frame did not submit geometry");
            const auto& pointEntity=app.m_PointLightEntity;
            const auto& point=pointEntity.GetComponent<GEngine::Component::RenderLightComponent>();
            std::fprintf(visualLog,"%u,%u,%s,%u,%u,%u,%llu,%u,%zu,%zu,%u,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                frames,successful,VisualVariantName(),visualDensity,unsigned(visualStage),unsigned(app.m_SimulationState),
                (unsigned long long)Steps(app),visualScope,std::size_t(app.m_ColliderOutlines.count),visualDebugLines,
                app.m_ChainImpulse.AppliedCount(),app.m_ChainImpulse.Pending(),app.m_LightPos.x,app.m_LightPos.y,app.m_LightPos.z,
                point.intensity,point.range,app.m_ShadowCascadeLevels[3]);
            if(!visualCompare && successful>=2 && sceneFrames==3 && GEngine::RenderCounters::Enabled)
            {
                const auto key=Key(app.m_ActiveSelection);const auto counters=GEngine::RenderCounters::Current();
                if(haveWarmedCounters[key])Check(counters.liveNames==warmedCounters[key].liveNames,"visual reset retained GPU names at stable Off frames");
                else{warmedCounters[key]=counters;haveWarmedCounters[key]=true;}
            }
            if(visualStage==VisualStage::Captures && visualView<VisualViewCount() && ++visualWait>=3)VisualCapture(app);
        }
        static bool FinishVisual()
        {
            if(finished)return false;finished=true;current=nullptr;GEngine::PhysicsDiagnostic::observer=nullptr;
            const bool clean=initialized && finalScene.expired() && previousScene.expired() && failedScene.expired();
            for(FILE** file:{&ticks,&transitions,&render,&materialLog,&uvLog,&captureLog,&visualLog,&visualSamples})
                if(*file){Check(std::fclose(*file)==0,"visual evidence close failed");*file=nullptr;}
            const unsigned samples=visualCompare?12:4;
            const bool pass=!failure && visualStage==VisualStage::Done && successful==(visualCompare?2u:3u) &&
                completed==(visualCompare?0u:16u) && screenshots==(visualCompare?48u:12u) && materialSceneChecks==samples &&
                materialTriangles==samples*60 && clean && panels>0 && frames<=1800 && visualOffFrames>0 &&
                (visualCompare || (rollback && boundaryCommands==4 && visualSelectedFrames>0 && visualAllFrames>0));
            auto* result=std::fopen((output/"result.json").string().c_str(),"wx");if(!result)return false;
            std::fprintf(result,"{\"presentation_only\":true,\"visual_mode\":\"%s\",\"pass\":%s,\"failure\":\"%s\",\"clean_shutdown\":%s,\"frames\":%u,\"panels\":%u,\"completed_ticks\":%llu,\"successful_transitions\":%u,\"resets\":%u,\"screenshots\":%u,\"material_samples\":%u,\"sampled_room_channels\":%u,\"uv_triangles\":%u,\"max_uv_world_units_error\":%.12g,\"uv_encoding_limit_m\":0.001,\"max_uv_orthogonality_error\":%.12g,\"construction_rollback\":%s,\"manual_boundary_commands\":%u,\"off_frames\":%u,\"selected_frames\":%u,\"all_frames\":%u,\"gpu_timing\":\"NOT_MEASURED\",\"qualified_reference_performance\":\"INCONCLUSIVE\"}\n",
                visualCompare?"comparison":"final",pass?"true":"false",failure?failure:"none",clean?"true":"false",frames,panels,
                (unsigned long long)completed,successful,visualCompare?0:1,screenshots,materialSceneChecks,materialSceneChecks*25,
                materialTriangles,materialMaxUvError,materialMaxUvOrthogonality,rollback?"true":"false",boundaryCommands,
                visualOffFrames,visualSelectedFrames,visualAllFrames);
            const bool written=std::fclose(result)==0;return pass && written;
        }
        static void BeginMaterials(App& app)
        {
            using namespace GEngine;
            auto& scene=*app.m_ActiveScene;auto& w=World(app);
            Check(app.SandboxEnabled() && app.m_SimulationState==State::Ready && Steps(app)==0 &&
                scene.GetPhysicsTiming().pendingSeconds==0,"material scene did not start Ready at zero ticks");
            sceneFrames=hold=materialFrameMask=0;previousSteps=0;
            presetMask|=1u<<unsigned(app.m_ActiveSelection.scene);
            const auto boxes=SandboxEnvironment::Boxes();
            for(unsigned i=0;i<boxes.size();++i)
            {
                auto entity=scene.FindEntityByName(boxes[i].name);
                Check(bool(entity),"material room entity missing");if(!entity)return;
                auto body=scene.GetBodyBinding(entity);auto renderId=scene.RenderData().Identify(entity);
                Check(bool(body) && bool(renderId),"material room full identity missing");if(!body || !renderId)return;
                environmentBodies[i]=*body;environmentRender[i]=*renderId;
                const auto& component=entity.GetComponent<Component::MeshRendererComponent>();
                roomMaterials[i]=component.material;roomMeshes[i]=component.mesh;
                auto geometry=app.m_SceneResources->GeometrySource(component.mesh);
                auto surface=app.m_SceneResources->DescribeMaterial(component.material);
                Check(bool(geometry) && bool(surface),"material room source missing");if(!geometry || !surface)return;
                roomSources[i]=geometry->source;environmentBounds[i]=geometry->geometry.bounds;roomDescriptions[i]=*surface;
                const auto b=w.ReadBody(*body);const Vec center=Vector(boxes[i].center),half=Vector(boxes[i].size)*.5f;
                Check(b && b->type==Component::BodyType::Static && b->shapeType==ShapeType::Box &&
                    Near(b->worldBounds.mins,center-half) && Near(b->worldBounds.maxs,center+half),"material room collider geometry changed");
                Check(surface->kind==MaterialKind::Standard && surface->mapping==TextureMappingMode::UV &&
                    (visualMode ? std::isfinite(surface->uvTiling[0]) && surface->uvTiling[0]>0 &&
                        surface->uvTiling[0]==surface->uvTiling[1] : surface->uvTiling==std::array<float,2>{1,1}) &&
                    surface->baseColor==std::array<float,4>{1,1,1,1} &&
                    surface->metallic==1 && surface->roughness==1 && surface->normalStrength==1 && surface->aoStrength==1 &&
                    surface->dielectricReflectance==std::array<float,3>{.04f,.04f,.04f} &&
                    surface->normalConvention==NormalMapConvention::NegativeY,"restored tile material scalars differ from frozen plan");
                for(const auto& texture:surface->textures)Check(bool(texture),"restored tile channel unbound");
            }
            if(failure)return;
            // These exact descriptors already exist in the production image and
            // sampler caches. Matching the live floor binding proves cache reuse;
            // no fallback, material edit, or replacement texture is admitted.
            constexpr std::array<const char*,5> paths{
                "PBR/base_white_tile/base-white-tile_albedo","PBR/base_white_tile/base-white-tile_normal-dx",
                "PBR/base_white_tile/base-white-tile_metallic","PBR/base_white_tile/base-white-tile_roughness",
                "PBR/base_white_tile/base-white-tile_ao"};
            for(unsigned semantic=0;semantic<paths.size();++semantic)
            {
                Asset::TextureDesc desc;if(semantic)desc.colorSpace=Asset::TextureColorSpace::Linear;
                auto image=Manager::AssetsManager::GetTexture(paths[semantic],tileSemantics[semantic],".png",desc);
                Check(bool(image),"original tile image cache lookup failed");if(!image)return;
                auto sampled=Manager::AssetsManager::SampleTexture((*image)->View());
                Check(bool(sampled),"original tile sampler cache lookup failed");if(!sampled)return;
                const MaterialTextureValue expected{sampled->TextureIdentity(),sampled->SamplerIdentity()};
                if(successful==0)originalTileBindings[semantic]=expected;
                Check(expected==originalTileBindings[semantic],"original root tile identity changed across scenes");
                for(const auto& material:roomDescriptions)
                    Check(material.textures[semantic] && *material.textures[semantic]==expected,"room replaced an original tile texture or sampler");
            }
            const Counts now{true,app.m_SceneResources->Meshes().Size(),app.m_SceneResources->Materials().Size(),w.GetBodyCount(),w.GetJointCount()};
            auto& previous=counts[Key(app.m_ActiveSelection)];
            if(previous.set)Check(previous.meshes==now.meshes && previous.materials==now.materials &&
                previous.bodies==now.bodies && previous.joints==now.joints,"material reset resource membership grew");
            previous=now;
            if(app.m_ActiveSelection.IsDistanceScene())Check(w.GetBodyCount()==app.m_ActiveSelection.chainLinks+6 &&
                w.GetJointCount()==app.m_ActiveSelection.chainLinks,"material chain membership changed");
            else Check(w.GetBodyCount()==22 && w.GetJointCount()==0,"material ordinary fixture membership changed");
        }
        static void MaterialCamera(App& app)
        {
            constexpr std::array<std::array<float,3>,6> eyes{{{0,235,70},{45,12,48},
                {-61,9,31},{61,9,-31},{31,9,-61},{-31,9,61}}};
            constexpr std::array<std::array<float,3>,6> targets{{{0,0,0},{35,.5f,35},
                {-79,6.5f,25},{79,6.5f,-25},{25,6.5f,-79},{-25,6.5f,79}}};
            const auto target=Vector(targets[materialCapture]);const auto offset=Vector(eyes[materialCapture])-target;
            const float distance=glm::length(offset);
            app.m_CameraView=App::CameraView::Editor;
            App::FocusMaterialCamera(app.m_EditorCamera_,target,std::asin(offset.y/distance),
                std::atan2(-offset.x,offset.z),distance);
        }
        static void MaterialsBeforeUpdate(App& app,GEngine::Timestep& elapsed)
        {
            current=&app;++frames;elapsed=GEngine::Timestep(1.0/60.0);
            Check(frames<=600 && completed<=24,"focused material execution cap");
            if(failure){app.ShutDown();return;}
            switch(materialStage)
            {
            case MaterialStage::Initial:
                initialized=true;Check(app.m_ActiveSelection==DefaultSceneSelection,"material mode changed Owner startup");
                BeginMaterials(app);materialStage=MaterialStage::Captures;
                [[fallthrough]];
            case MaterialStage::Captures:
                if(materialCapture<6){elapsed=GEngine::Timestep(0);MaterialCamera(app);return;}
                pausedSteps=Steps(app);app.SetSimulationState(State::Running);materialStage=MaterialStage::Starting;
                elapsed=GEngine::Timestep(.2);return;
            case MaterialStage::Ready:
                elapsed=GEngine::Timestep(.2);
                if(++hold>=3){pausedSteps=Steps(app);app.SetSimulationState(State::Running);materialStage=MaterialStage::Starting;}
                return;
            case MaterialStage::Run:
                if(Steps(app)>=2){pausedSteps=Steps(app);app.SetSimulationState(State::Paused);
                    materialStage=MaterialStage::Pause;hold=0;elapsed=GEngine::Timestep(.2);}
                return;
            case MaterialStage::Pause:
                elapsed=GEngine::Timestep(.2);
                if(++hold>=3){app.SetSimulationState(State::Running);materialStage=MaterialStage::Resuming;}
                return;
            case MaterialStage::AfterResume:
                if(Steps(app)<4)return;
                Check(Steps(app)==4 && materialFrameMask==31,"material playback or prepared room coverage incomplete");
                if(successful==materialSequence.size())
                {
                    finalScene=app.m_ActiveScene;materialStage=MaterialStage::Done;app.ShutDown();return;
                }
                SavePrevious(app);Check(app.RequestScene(materialSequence[successful]),"material switch request rejected");
                materialStage=MaterialStage::Switching;return;
            default:return;
            }
        }
        static void MaterialsAfterUpdate(App& app)
        {
            using namespace GEngine;
            if(failure)return;
            if(materialStage==MaterialStage::Switching)
            {
                auto& w=World(app);const auto next=*w.BodyAt(0);
                const bool oldBody=!w.ReadBody(previousBody),oldJoint=!previousJoint || !w.ReadDistanceJoint(previousJoint);
                Check(previousScene.expired() && oldBody && oldJoint && previousBody.registry!=next.registry &&
                    !app.m_ActiveScene->RenderData().Resolve(previousRender) && !app.m_SceneResources->MeshMetadata(previousMesh),
                    "material switch did not retire previous scene identities");
                Check(app.m_ActiveSelection==materialSequence[successful],"material switch selection mismatch");
                ++successful;++rejectedOld;
                std::fprintf(transitions,"%u,%u,%llu,%llu,%d,%d,%d,%d\n",successful,unsigned(app.m_ActiveSelection.scene),
                    (unsigned long long)previousBody.registry,(unsigned long long)next.registry,previousScene.expired(),oldBody,oldJoint,
                    previousSelection==app.m_ActiveSelection);
                BeginMaterials(app);materialStage=MaterialStage::Ready;
            }
            const auto steps=Steps(app);
            if(materialStage==MaterialStage::Captures || materialStage==MaterialStage::Ready)
                Check(steps==0,"material Ready or capture advanced Physics");
            if(materialStage==MaterialStage::Pause || materialStage==MaterialStage::Starting || materialStage==MaterialStage::Resuming)
                Check(steps==pausedSteps,"material pause/resume accumulated Physics time");
            if(materialStage==MaterialStage::Starting)materialStage=MaterialStage::Run;
            if(materialStage==MaterialStage::Resuming)materialStage=MaterialStage::AfterResume;
            Check(steps>=previousSteps && steps-previousSteps<=1,"material fixed tick sequence");
            completed+=steps-previousSteps;previousSteps=steps;
            auto& w=World(app);
            for(std::size_t i=0;i<w.GetBodyCount();++i)
            {
                auto body=w.ReadBody(*w.BodyAt(i));
                Check(body && Math::IsFinite(body->position) && Math::IsFinite(body->orientation) &&
                    Math::IsFinite(body->linearVelocity) && Math::IsFinite(body->angularVelocity),"material smoke nonfinite body");
            }
            Check(app.m_ActiveScene->GetPhysicsTiming().pendingSeconds==0,"material smoke retained elapsed backlog");
            std::fprintf(ticks,"%u,%u,%u,%llu,%u,%zu,%zu,%.12g\n",frames,successful,unsigned(app.m_ActiveSelection.scene),
                (unsigned long long)steps,unsigned(app.m_SimulationState),w.GetBodyCount(),w.GetJointCount(),
                app.m_ActiveScene->GetPhysicsTiming().pendingSeconds);
        }
        template<class T> static bool MaterialParameter(const GEngine::PreparedMaterialBinding& packet,
            std::string_view name,const T& expected)
        {
            const auto parameters=packet.Source()->Declaration()->Parameters();const auto values=packet.Source()->Values();
            for(std::size_t i=0;i<parameters.size();++i)if(parameters[i].declaration.name==name)
            {
                const auto* value=std::get_if<T>(&values[i]);return value && *value==expected;
            }
            return false;
        }
        static void MaterialUv(unsigned room,const GEngine::DrawItem& draw)
        {
            using namespace GEngine;
            const auto& mesh=*roomSources[room];const auto layout=mesh.Layout();
            const VertexAttribute *position=nullptr,*uv=nullptr,*normal=nullptr;
            for(const auto& attribute:layout.attributes)
            {
                if(attribute.semantic==VertexSemantic::Position)position=&attribute;
                if(attribute.semantic==VertexSemantic::TexCoord0)uv=&attribute;
                if(attribute.semantic==VertexSemantic::Normal)normal=&attribute;
            }
            Check(position && uv && normal && mesh.VertexCount()==24 && mesh.IndexCount()==36 &&
                mesh.IndexFormat()==MeshIndexFormat::UInt32,"material retained cube layout or topology changed");
            if(failure)return;
            Check(position->scalar==VertexScalarFormat::Float32 && position->components==3 &&
                uv->scalar==VertexScalarFormat::Float32 && uv->components==2 &&
                normal->scalar==VertexScalarFormat::Float32 && normal->components==3,"material cube attribute encoding");
            if(failure)return;
            unsigned faceMask=0;
            for(unsigned triangle=0;triangle<12;++triangle)
            {
                std::array<glm::dvec3,3> p{},n{};std::array<glm::dvec2,3> t{};
                for(unsigned corner=0;corner<3;++corner)
                {
                    std::uint32_t index{};std::memcpy(&index,mesh.Indices().data()+(triangle*3+corner)*sizeof(index),sizeof(index));
                    Check(index<mesh.VertexCount(),"material cube index out of range");if(failure)return;
                    const auto* bytes=mesh.Vertices().data()+index*layout.strideBytes;
                    std::array<float,3> local{},nn{};std::array<float,2> tt{};
                    std::memcpy(local.data(),bytes+position->offsetBytes,sizeof(local));
                    std::memcpy(nn.data(),bytes+normal->offsetBytes,sizeof(nn));
                    std::memcpy(tt.data(),bytes+uv->offsetBytes,sizeof(tt));
                    for(float value:local)Check(std::isfinite(value) && std::abs(value)==.5f,"material cube position changed");
                    for(float value:tt)Check(std::isfinite(value),"material UV nonfinite");
                    p[corner]=glm::dvec3(draw.worldTransform*glm::vec4(local[0],local[1],local[2],1));
                    n[corner]=glm::dvec3(nn[0],nn[1],nn[2]);t[corner]=glm::dvec2(tt[0],tt[1]);
                    if(visualMode)t[corner]*=glm::dvec2(roomDescriptions[room].uvTiling[0],roomDescriptions[room].uvTiling[1]);
                }
                Check(n[0]==n[1] && n[1]==n[2] && glm::length(n[0])==1,"material cube face normal changed");
                const auto dp1=p[1]-p[0],dp2=p[2]-p[0];const auto dt1=t[1]-t[0],dt2=t[2]-t[0];
                const double determinant=dt1.x*dt2.y-dt1.y*dt2.x;
                Check(std::isfinite(determinant) && std::abs(determinant)>1e-12,"material UV triangle collapsed");if(failure)return;
                const auto du=(dp1*dt2.y-dp2*dt1.y)/determinant;
                const auto dv=(dp2*dt1.x-dp1*dt2.x)/determinant;
                const double u=glm::length(du),v=glm::length(dv),orthogonality=std::abs(glm::dot(du,dv)/(u*v));
                const double expected=visualMode?160.0/visualDensity:5.0;
                const double error=(std::max)(std::abs(u-expected),std::abs(v-expected));
                materialMaxUvError=(std::max)(materialMaxUvError,error);
                materialMaxUvOrthogonality=(std::max)(materialMaxUvOrthogonality,orthogonality);
                // Frozen Float32 UV encoding allowance; independent of unchanged
                // Physics contact/joint thresholds (uv-encoding-check-01.json).
                Check(std::isfinite(error) && error<=1e-3 && std::isfinite(orthogonality) && orthogonality<=1e-4,
                    "material principal or edge face UV density differs from expected world units");
                unsigned axis=0;for(unsigned a=1;a<3;++a)if(std::abs(n[0][a])>std::abs(n[0][axis]))axis=a;
                Check(std::abs(n[0][axis])==1,"material cube non-axis face normal");
                const unsigned face=axis*2+(n[0][axis]>0?1:0);faceMask|=1u<<face;++materialTriangles;
                std::fprintf(uvLog,"%u,%u,%u,%u,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g\n",
                    successful,room,triangle,face,p[0].x,p[0].y,p[0].z,t[0].x,t[0].y,du.x,du.y,du.z,dv.x,dv.y,dv.z,u,v,orthogonality);
            }
            Check(faceMask==63,"material cube missing a principal or edge face");
        }
        static void MaterialsObserveFrame(App& app,const GEngine::RenderFrame& frame) noexcept
        {
            using namespace GEngine;
            if(!initialized || failure)return;
            unsigned mask=0;
            for(const auto& draw:frame.Draws())for(unsigned i=0;i<environmentRender.size();++i)if(draw.entity==environmentRender[i])
            {
                mask|=1u<<i;Check(draw.mesh==roomMeshes[i] && draw.material==roomMaterials[i] && draw.resources<frame.Resources().size(),
                    "material submitted mesh/material identity mismatch");if(failure)return;
                Vec lo(std::numeric_limits<float>::max()),hi(-std::numeric_limits<float>::max());
                for(unsigned corner=0;corner<8;++corner)
                {
                    Vec p;for(unsigned axis=0;axis<3;++axis)p[axis]=float((corner&(1u<<axis))?environmentBounds[i].maximum[axis]:environmentBounds[i].minimum[axis]);
                    p=Vec(draw.worldTransform*glm::vec4(p,1));lo=glm::min(lo,p);hi=glm::max(hi,p);
                }
                const auto box=SandboxEnvironment::Boxes()[i];const Vec center=Vector(box.center),half=Vector(box.size)*.5f;
                Check(Near(lo,center-half) && Near(hi,center+half),"material rendered bounds changed from collider");
                const auto& packet=frame.Resources()[draw.resources].Material();const auto& desc=roomDescriptions[i];
                Check(packet.Instance()==roomMaterials[i] && packet.Fallbacks().empty() &&
                    MaterialParameter(packet,"baseColor",desc.baseColor) && MaterialParameter(packet,"u_tiling",desc.uvTiling) &&
                    MaterialParameter(packet,"metallicFactor",desc.metallic) && MaterialParameter(packet,"roughnessScale",desc.roughness) &&
                    MaterialParameter(packet,"normalStrength",desc.normalStrength) && MaterialParameter(packet,"aoStrength",desc.aoStrength) &&
                    MaterialParameter(packet,"metalness",desc.dielectricReflectance) &&
                    MaterialParameter(packet,"normalConvention",std::int32_t(desc.normalConvention)),"material prepared values differ from authored tile material");
                const auto declarations=packet.Source()->Declaration()->Textures();const auto textures=packet.Textures();
                Check(declarations.size()==5 && textures.size()==5,"material prepared channel count");if(failure)return;
                unsigned semanticMask=0;
                for(std::size_t slot=0;slot<declarations.size();++slot)
                {
                    unsigned semantic=0;while(semantic<5 && declarations[slot].declaration.name!=tileSemantics[semantic])++semantic;
                    Check(semantic<5,"material prepared unknown semantic");if(failure)return;
                    semanticMask|=1u<<semantic;const auto& sampled=textures[slot];const auto expected=originalTileBindings[semantic];
                    Check(sampled.texture && sampled.sampler && sampled.texture.Identity()==expected.texture &&
                        sampled.sampler.Identity()==expected.sampler,"material prepared original texture/sampler mismatch");if(failure)return;
                    const auto& image=sampled.texture->Description();const auto& sampler=sampled.sampler->Description();
                    Check(image.width==2048 && image.height==2048 && image.kind==Asset::TextureKind::Image2D &&
                        image.usage==Asset::TextureUsage::Sampled && image.colorSpace==(semantic?Asset::TextureColorSpace::Linear:Asset::TextureColorSpace::SRGB) &&
                        sampler.wrapU==Asset::SamplerWrap::Repeat && sampler.wrapV==Asset::SamplerWrap::Repeat,
                        "material prepared image color space or repeat sampling mismatch");
                    if(!(materialFrameMask&(1u<<i)))std::fprintf(materialLog,
                        "%u,%u,%u,%llu,%llu,%u,%llu,%llu,%u,%llu,%llu,%s,%u,%llu,%llu,%u,%llu,%llu,%d,%d,%u,%u,%u\n",
                        successful,i,environmentBodies[i].index,(unsigned long long)environmentBodies[i].generation,(unsigned long long)environmentBodies[i].registry,
                        roomMeshes[i].index,(unsigned long long)roomMeshes[i].generation,(unsigned long long)roomMeshes[i].registry,
                        roomMaterials[i].index,(unsigned long long)roomMaterials[i].generation,(unsigned long long)roomMaterials[i].registry,tileSemantics[semantic],
                        expected.texture.index,(unsigned long long)expected.texture.generation,(unsigned long long)expected.texture.registry,
                        expected.sampler.index,(unsigned long long)expected.sampler.generation,(unsigned long long)expected.sampler.registry,
                        image.width,image.height,unsigned(image.colorSpace),unsigned(sampler.wrapU),unsigned(sampler.wrapV));
                }
                Check(semanticMask==31,"material prepared semantic coverage");
                if(!(materialFrameMask&(1u<<i))){MaterialUv(i,draw);materialFrameMask|=1u<<i;}
            }
            Check(mask==31 && !frame.DirectionalLights().empty() && !frame.PointLights().empty(),"material frame missing room or existing lights");
            if(sceneFrames==0 && materialFrameMask==31)++materialSceneChecks;
        }
        static void CaptureMaterial(App& app)
        {
            constexpr std::array<const char*,6> names{"material-room-wide.ppm","material-floor-close.ppm",
                "material-wall-minus-x-close.ppm","material-wall-plus-x-close.ppm",
                "material-wall-minus-z-close.ppm","material-wall-plus-z-close.ppm"};
            Check(app.m_RenderTarget!=nullptr && materialCapture<names.size(),"material capture target missing");if(failure)return;
            const auto& target=*app.m_RenderTarget;const auto& size=target.Buffer().Description();
            Check(size.Width && size.Height && size.Width<=4096 && size.Height<=4096,"material capture extent invalid");if(failure)return;
            std::vector<std::byte> bytes(std::size_t(size.Width)*size.Height*4);
            Check(bool(target.ReadColor(bytes)),"material capture read failed");if(failure)return;
            auto* file=std::fopen((output/names[materialCapture]).string().c_str(),"wbx");
            Check(file!=nullptr,"material capture create failed");if(!file)return;
            std::fprintf(file,"P6\n%u %u\n255\n",size.Width,size.Height);bool ok=true;
            for(unsigned y=size.Height;y>0;--y)for(unsigned x=0;x<size.Width;++x)
                ok=std::fwrite(bytes.data()+((y-1)*size.Width+x)*4,1,3,file)==3 && ok;
            ok=std::fclose(file)==0 && ok;Check(ok,"material capture write failed");
            const auto p=app.m_EditorCamera_.GetPosition();
            std::fprintf(captureLog,"%s,%u,%u,%llu,%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g\n",names[materialCapture],frames,successful,
                (unsigned long long)Steps(app),size.Width,size.Height,p.x,p.y,p.z,app.m_EditorCamera_.GetPitch(),app.m_EditorCamera_.GetYaw());
            ++screenshots;++materialCapture;materialCaptureWait=0;
        }
        static void MaterialsOnFrame(App& app,const GEngine::ScheduledFrameStats& stats)
        {
            if(!initialized || failure)return;++sceneFrames;
            std::fprintf(render,"%u,%u,%zu,%zu,%zu,%zu,%.6f,%.6f,NOT_MEASURED\n",frames,successful,stats.extraction.draws,
                stats.submission.submittedDrawCalls,app.m_SceneResources->Meshes().Size(),app.m_SceneResources->Materials().Size(),
                stats.extraction.preparationMicroseconds,stats.extraction.extractionMicroseconds);
            Check(stats.extraction.draws>=5 && stats.submission.submittedDrawCalls>0,"material frame did not submit geometry");
            if(sceneFrames==3 && app.m_ActiveSelection.scene==Preset::SphereBoxStack && GEngine::RenderCounters::Enabled)
            {
                const auto key=Key(app.m_ActiveSelection);const auto counters=GEngine::RenderCounters::Current();
                if(haveWarmedCounters[key])Check(counters.liveNames==warmedCounters[key].liveNames,
                    "material reset retained GPU names after stable frames");
                else{warmedCounters[key]=counters;haveWarmedCounters[key]=true;}
            }
            if(materialStage==MaterialStage::Captures && materialCapture<6 && ++materialCaptureWait>=3)CaptureMaterial(app);
        }
        static bool FinishMaterials()
        {
            if(finished)return false;finished=true;current=nullptr;GEngine::PhysicsDiagnostic::observer=nullptr;
            const bool clean=initialized && finalScene.expired() && previousScene.expired();
            for(FILE** file:{&ticks,&transitions,&render,&materialLog,&uvLog,&captureLog})
                if(*file){Check(std::fclose(*file)==0,"material evidence close failed");*file=nullptr;}
            const bool pass=!failure && materialStage==MaterialStage::Done && successful==3 && completed==16 &&
                screenshots==6 && materialSceneChecks==4 && materialTriangles==240 && clean && panels>0 && frames<=600;
            auto* result=std::fopen((output/"result.json").string().c_str(),"wx");if(!result)return false;
            std::fprintf(result,"{\"presentation_only\":true,\"material_only\":true,\"pass\":%s,\"failure\":\"%s\",\"clean_shutdown\":%s,\"frames\":%u,\"panels\":%u,\"completed_ticks\":%llu,\"successful_transitions\":%u,\"resets\":1,\"screenshots\":%u,\"material_scene_checks\":%u,\"sampled_room_channels\":%u,\"uv_triangles\":%u,\"max_uv_world_units_error\":%.12g,\"uv_encoding_limit_m\":0.001,\"max_uv_orthogonality_error\":%.12g,\"gpu_timing\":\"NOT_MEASURED\",\"qualified_reference_performance\":\"INCONCLUSIVE\"}\n",
                pass?"true":"false",failure?failure:"none",clean?"true":"false",frames,panels,(unsigned long long)completed,successful,screenshots,
                materialSceneChecks,materialSceneChecks*25,materialTriangles,materialMaxUvError,materialMaxUvOrthogonality);
            const bool written=std::fclose(result)==0;return pass && written;
        }
        static void BeforeInjectedFailure(App& app)
        {
            failedScene=app.m_ActiveScene;
            auto& w=World(app);failedBody=w.GetBodyCount()?*w.BodyAt(0):Body{};
            failedJoint=w.GetJointCount()?*w.JointAt(0):Joint{};
        }
        static void BeginScene(App& app)
        {
            using namespace GEngine;
            auto& scene=*app.m_ActiveScene;auto& w=World(app);
            presetMask|=1u<<static_cast<unsigned>(app.m_ActiveSelection.scene);
            sceneFrames=hold=0;previousSteps=Steps(app);externalWork=0;applied=false;
            Check(previousSteps==0,"new scene clock not reset");
            Check(scene.GetPhysicsTiming().pendingSeconds==0,"new scene retained backlog");
            Check(!app.m_ChainImpulse.Pending() && app.m_ChainImpulse.AppliedCount()==0,"new scene retained manual command");
            const auto boxes=SandboxEnvironment::Boxes();
            for(unsigned i=0;i<boxes.size();++i)
            {
                auto entity=scene.FindEntityByName(boxes[i].name);
                Check(bool(entity),"missing room entity");if(!entity)return;
                auto binding=scene.GetBodyBinding(entity);auto id=scene.RenderData().Identify(entity);
                Check(bool(binding) && bool(id),"room identity binding");if(!binding || !id)return;
                environmentBodies[i]=*binding;environmentRender[i]=*id;
                auto body=w.ReadBody(*binding);Check(bool(body),"room body missing");if(!body)return;
                const Vec center=Vector(boxes[i].center),half=Vector(boxes[i].size)*.5f;
                Check(body->type==Component::BodyType::Static && body->shapeType==ShapeType::Box &&
                    Near(body->worldBounds.mins,center-half) && Near(body->worldBounds.maxs,center+half),"room collider bounds");
                const auto mesh=entity.GetComponent<Component::MeshRendererComponent>().mesh;
                auto geometry=app.m_SceneResources->GeometrySource(mesh);
                Check(bool(geometry) && !geometry->geometry.bounds.empty,"room mesh bounds");
                if(geometry)environmentBounds[i]=geometry->geometry.bounds;
            }
            unsigned dynamicBodies=0;
            for(std::size_t i=0;i<w.GetBodyCount();++i)
            {
                auto b=w.ReadBody(*w.BodyAt(i));if(!b || b->type!=Component::BodyType::Dynamic)continue;
                ++dynamicBodies;
                Check(b->worldBounds.mins.y>=SandboxEnvironment::FloorTop-1e-4f &&
                    b->worldBounds.mins.x>=-SandboxEnvironment::Width*.5f+SandboxEnvironment::WallThickness-1e-4f &&
                    b->worldBounds.maxs.x<=SandboxEnvironment::Width*.5f-SandboxEnvironment::WallThickness+1e-4f &&
                    b->worldBounds.mins.z>=-SandboxEnvironment::Depth*.5f+SandboxEnvironment::WallThickness-1e-4f &&
                    b->worldBounds.maxs.z<=SandboxEnvironment::Depth*.5f-SandboxEnvironment::WallThickness+1e-4f,
                    "initial dynamic body intersects room");
            }
            rootBody={};
            if(app.m_ActiveSelection.IsDistanceScene())
            {
                const auto n=app.m_ActiveSelection.chainLinks;
                Check(w.GetBodyCount()==n+6 && w.GetJointCount()==n && dynamicBodies==n,"interactive chain exact membership");
                auto root=scene.FindEntityByName("Chain root");
                auto tip=scene.FindEntityByName("Chain link "+std::to_string(n));
                Check(bool(root) && bool(tip),"chain endpoint entities missing");
                if(root && tip)
                {
                    auto rootHandle=scene.GetBodyBinding(root),tipHandle=scene.GetBodyBinding(tip);
                    Check(bool(rootHandle) && bool(tipHandle),"chain endpoint bindings missing");
                    if(rootHandle && tipHandle)
                    {
                        auto state=w.ReadBody(*rootHandle);
                        Check(state && state->type==Component::BodyType::Static &&
                            app.m_ChainImpulse.Target()==*tipHandle && app.m_ChainImpulse.Enabled(),"chain static root or terminal command binding");
                        rootBody=*rootHandle;if(state)rootPosition=state->position;
                    }
                }
            }
            const auto key=Key(app.m_ActiveSelection);
            Counts now{true,app.m_SceneResources->Meshes().Size(),app.m_SceneResources->Materials().Size(),w.GetBodyCount(),w.GetJointCount()};
            if(counts[key].set)Check(counts[key].meshes==now.meshes && counts[key].materials==now.materials &&
                counts[key].bodies==now.bodies && counts[key].joints==now.joints,"same preset resource membership grew");
            else counts[key]=now;
            extendedG8=app.m_ActiveSelection.scene==Preset::DistanceChainContact && !g8Proven;
            slept=wake=false;groundMask=g8Contacts=sleepTick=wakeTick=0;
            if(app.m_ActiveSelection.scene==Preset::DistanceChainContact)
                for(unsigned i=0;i<8;++i)
                {
                    auto e=scene.FindEntityByName("Chain link "+std::to_string(i+1));
                    Check(bool(e),"G8 link entity missing");if(!e)return;
                    auto h=scene.GetBodyBinding(e);
                    Check(bool(h),"G8 link identity");if(h)g8Bodies[i]=*h;
                }
            wallProbe=app.m_ActiveSelection.scene==Preset::SphereBoxStack && successful==3;
            observer={};observer.context=&app;
            observer.boundary=[](void* context,PhysicsDiagnostic::Stage boundary,const PhysicsWorld& world,
                ManifoldCollector& manifolds,std::span<const ContactIsland>,std::size_t,int) noexcept
                { ObservePhysics(*static_cast<App*>(context),boundary,world,manifolds); };
            observer.detailed=observer.targeted=wallProbe;
            observer.select=observer.valid=[](void* context,const RigidBody3D* a,const RigidBody3D* b) noexcept
                { return SelectWallPair(*static_cast<App*>(context),a,b); };
            observer.event=[](void* context,const PhysicsDiagnostic::Event& event) noexcept
                { ObserveWallCollision(*static_cast<App*>(context),event); };
            observer.violation=[](void*) noexcept { Check(false,"wall observer mutated Physics state"); };
            PhysicsDiagnostic::observer=&observer;
            if(wallProbe)
            {
                // One presentation-only Body Transform stimulus in an existing
                // unjointed sphere fixture; no new body, collider or solver path.
                Check(w.GetJointCount()==0,"wall probe requires unjointed ordinary scene");
                for(std::size_t i=0;i<w.GetBodyCount();++i)
                {
                    const auto h=*w.BodyAt(i);auto b=w.ReadBody(h);
                    if(!b || b->type!=Component::BodyType::Dynamic || b->shapeType!=ShapeType::Sphere)continue;
                    wallProbeBody=h;const float radius=(b->worldBounds.maxs.x-b->worldBounds.mins.x)*.5f;
                    const Vec position(SandboxEnvironment::Width*.5f-SandboxEnvironment::WallThickness-radius-.05f,
                        SandboxEnvironment::FloorTop+radius+2.f,0);
                    Check(bool(scene.GetPhysicsSystem()->SetBodyPose(h,position,b->orientation)) &&
                        bool(w.SetBodyVelocity(h,Vec(5,0,0),Vec(0))),"wall probe Body Transform stimulus rejected");
                    auto* record=std::fopen((output/"wall-stimulus.json").string().c_str(),"wx");
                    Check(record!=nullptr,"wall stimulus evidence create failed");
                    if(record)
                    {
                        std::fprintf(record,"{\"presentation_only\":true,\"transition\":3,\"wall\":\"Sandbox Wall +X\",\"body\":[%u,%llu,%llu],\"position\":[%.9g,%.9g,0],\"velocity\":[5,0,0],\"maximum_check_ticks\":4}\n",
                            h.index,(unsigned long long)h.generation,(unsigned long long)h.registry,position.x,position.y);
                        Check(std::fclose(record)==0,"wall stimulus evidence write failed");
                    }
                    break;
                }
                Check(bool(wallProbeBody),"ordinary wall probe sphere missing");
                wallEvents=std::fopen((output/"wall-ccd.csv").string().c_str(),"wx");
                Check(wallEvents!=nullptr,"wall CCD evidence create failed");
                if(wallEvents)std::fprintf(wallEvents,"tick,sphere_index,sphere_generation,sphere_registry,wall_index,wall_generation,wall_registry,time_s,closing_speed,normal_x,normal_y,normal_z,impulse_x,impulse_y,impulse_z,before_x,before_y,before_z,after_x,after_y,after_z,before_vx,before_vy,before_vz,after_vx,after_vy,after_vz\n");
            }
        }
        static bool SelectWallPair(App& app,const GEngine::RigidBody3D* a,const GEngine::RigidBody3D* b) noexcept
        {
            if(!wallProbe || !a || !b)return false;
            const auto& w=World(app);const auto& bodies=GEngine::PhysicsTestAccess::Bodies(w);
            // Establish current ownership before reading either borrowed pointer.
            if(std::find(bodies.begin(),bodies.end(),a)==bodies.end() ||
                std::find(bodies.begin(),bodies.end(),b)==bodies.end())return false;
            const auto ah=GEngine::PhysicsTestAccess::Handle(w,*a),bh=GEngine::PhysicsTestAccess::Handle(w,*b);
            return w.IsBodyValid(ah) && w.IsBodyValid(bh) &&
                ((ah==wallProbeBody && bh==environmentBodies[2]) || (bh==wallProbeBody && ah==environmentBodies[2]));
        }
        static void ObserveWallCollision(App& app,const GEngine::PhysicsDiagnostic::Event& event) noexcept
        {
            using namespace GEngine;
            if(event.operation!=PhysicsDiagnostic::Operation::ToiNormal)return;
            Check(SelectWallPair(app,event.a,event.b),"wall CCD full identity mismatch");if(failure)return;
            const bool sphereA=PhysicsTestAccess::Handle(World(app),*event.a)==wallProbeBody;
            const auto& before=sphereA?event.beforeA:event.beforeB;
            const auto& after=sphereA?event.afterA:event.afterB;
            const auto& wallBefore=sphereA?event.beforeB:event.beforeA;
            const auto& wallAfter=sphereA?event.afterB:event.afterA;
            const Vec impulse=sphereA?event.linearImpulseA:event.linearImpulseB;
            const auto finite=[](const PhysicsDiagnostic::BodyState& state) noexcept
                { return Math::IsFinite(state.position) && Math::IsFinite(state.orientation) &&
                    Math::IsFinite(state.linear) && Math::IsFinite(state.angular) && std::isfinite(state.inactive); };
            Check(event.appliedImpulsesKnown && Math::IsFinite(impulse) && glm::length2(impulse)>0 &&
                Math::IsFinite(event.normal) && Math::IsFinite(event.anchorA) && Math::IsFinite(event.anchorB) &&
                std::isfinite(event.time) && std::isfinite(event.closingSpeed) && event.closingSpeed<0 &&
                finite(before) && finite(after) && finite(wallBefore) && finite(wallAfter),"wall CCD nonfinite or missing impulse witness");
            Check(impulse.x<0 && before.linear.x>0 && after.linear.x<before.linear.x &&
                PhysicsDiagnostic::SameState(wallBefore,wallAfter) && wallAfter.linear==Vec(0) &&
                wallAfter.position==Vector(SandboxEnvironment::Boxes()[2].center),"wall CCD response or static boundary invalid");
            if(wallEvents)
                Check(std::fprintf(wallEvents,"%llu,%u,%llu,%llu,%u,%llu,%llu,%.12g,%.12g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                    (unsigned long long)(Steps(app)+1),wallProbeBody.index,(unsigned long long)wallProbeBody.generation,(unsigned long long)wallProbeBody.registry,
                    environmentBodies[2].index,(unsigned long long)environmentBodies[2].generation,(unsigned long long)environmentBodies[2].registry,
                    event.time,event.closingSpeed,event.normal.x,event.normal.y,event.normal.z,impulse.x,impulse.y,impulse.z,
                    before.position.x,before.position.y,before.position.z,after.position.x,after.position.y,after.position.z,
                    before.linear.x,before.linear.y,before.linear.z,after.linear.x,after.linear.y,after.linear.z)>0,"wall CCD evidence write failed");
            if(!failure){wallContact=true;++wallCcdEvents;}
        }
        static void ObservePhysics(App& app,GEngine::PhysicsDiagnostic::Stage boundary,
            const GEngine::PhysicsWorld& w,GEngine::ManifoldCollector& manifolds)
        {
            using namespace GEngine;
            if(wallProbe && boundary==PhysicsDiagnostic::Stage::End)
            {
                for(auto& manifold:manifolds.m_Manifolds)
                    for(int i=0;i<manifold.GetNumContacts();++i)
                    {
                        const auto contact=manifold.GetContact(i);
                        if(!contact.m_BodyA || !contact.m_BodyB)continue;
                        const auto a=PhysicsTestAccess::Handle(w,*contact.m_BodyA),b=PhysicsTestAccess::Handle(w,*contact.m_BodyB);
                        if((a==wallProbeBody && b==environmentBodies[2]) || (b==wallProbeBody && a==environmentBodies[2]))
                        {
                            Check(w.IsBodyValid(a) && w.IsBodyValid(b) && std::isfinite(contact.separationDistance),"wall contact full identity/witness");
                            wallContact=true;
                        }
                    }
                auto sphere=w.ReadBody(wallProbeBody),wall=w.ReadBody(environmentBodies[2]);
                Check(sphere && wall && wall->type==Component::BodyType::Static &&
                    sphere->position.x<wall->worldBounds.mins.x &&
                    wall->position==Vector(SandboxEnvironment::Boxes()[2].center) && wall->linearVelocity==Vec(0),
                    "wall probe crossed boundary or moved static wall");
            }
            if(app.m_ActiveSelection.scene!=Preset::DistanceChainContact)return;
            unsigned asleep=0;
            for(auto h:g8Bodies){auto b=w.ReadBody(h);Check(bool(b),"G8 observer stale body");if(b)asleep+=b->sleeping;}
            if(boundary==PhysicsDiagnostic::Stage::Wake && queuedBoundary && applied)
            {
                Check(!extendedG8 || slept,"G8 impulse preceded natural sleep");
                Check(asleep==0,"G8 connected wake");wake=asleep==0;wakeTick=static_cast<unsigned>(Steps(app)+1);
            }
            if(boundary!=PhysicsDiagnostic::Stage::End)return;
            groundMask=g8Contacts=0;
            for(auto& manifold:manifolds.m_Manifolds)
                for(int i=0;i<manifold.GetNumContacts();++i)
                {
                    const auto c=manifold.GetContact(i);
                    Check(c.m_BodyA && c.m_BodyB && std::isfinite(c.separationDistance),"invalid contact witness");
                    if(!c.m_BodyA || !c.m_BodyB)continue;
                    const auto a=PhysicsTestAccess::Handle(w,*c.m_BodyA),b=PhysicsTestAccess::Handle(w,*c.m_BodyB);
                    Check(w.IsBodyValid(a) && w.IsBodyValid(b) && a!=b,"contact registry identity");
                    const auto other=a==environmentBodies[0]?b:b==environmentBodies[0]?a:Body{};
                    for(unsigned j=0;j<8;++j)if(other && other==g8Bodies[j]){++g8Contacts;groundMask|=1u<<j;}
                }
            if(extendedG8 && !applied && asleep==8 && groundMask==255 && !slept)
                {slept=true;sleepTick=static_cast<unsigned>(Steps(app));}
        }
        static void BeforeBoundary(App& app)
        {
            if(materialMode)return;
            auto& w=World(app);queuedBoundary=app.m_ChainImpulse.Pending();velocities.clear();
            for(std::size_t i=0;i<w.GetBodyCount();++i){auto h=*w.BodyAt(i);velocities.emplace_back(h,w.ReadBody(h)->linearVelocity);}
            if(queuedBoundary)
            {
                auto body=w.ReadBody(app.m_ChainImpulse.Target());Check(bool(body),"manual target vanished");
                if(body)externalWork+=glm::dot(glm::dvec3(requestedImpulse),glm::dvec3(body->linearVelocity))+
                    .5*glm::dot(glm::dvec3(requestedImpulse),glm::dvec3(requestedImpulse))*body->inverseMass;
            }
        }
        static void AfterBoundary(App& app)
        {
            if(materialMode)return;
            auto& w=World(app);
            for(auto [handle,velocity]:velocities)
            {
                auto body=w.ReadBody(handle);Check(bool(body),"body retired during manual boundary");if(!body)continue;
                const Vec expected=velocity+(queuedBoundary && handle==app.m_ChainImpulse.Target()?requestedImpulse*body->inverseMass:Vec(0));
                Check(glm::length(body->linearVelocity-expected)<1e-6f,"manual impulse changed wrong body or payload");
            }
            if(queuedBoundary){applied=true;impulseStep=Steps(app)+1;++boundaryCommands;}
            Check(!app.m_ChainImpulse.Pending(),"manual command not consumed once");
        }
        static void Queue(App& app,bool cancellation=false)
        {
            if(!app.m_ActiveSelection.IsDistanceScene())return;
            app.m_ChainImpulseMagnitude=extendedG8?5.f:2.f;
            app.m_ChainImpulseDirection=extendedG8?2:int(successful%4);
            const std::array<Vec,4> axes{Vec(1,0,0),Vec(-1,0,0),Vec(0,0,1),Vec(0,0,-1)};
            requestedImpulse=axes[app.m_ChainImpulseDirection]*app.m_ChainImpulseMagnitude;
            Check(app.RequestDistanceChainImpulse(),cancellation?"cancellation request rejected":"manual request rejected");
            Check(!app.RequestDistanceChainImpulse(),"duplicate command accepted");
            app.m_ChainImpulseMagnitude=1;app.m_ChainImpulseDirection=(app.m_ChainImpulseDirection+1)%4;
        }
        static void BeforeUpdate(App& app,GEngine::Timestep& elapsed)
        {
            if(contactRegression){ContactRegressionBeforeUpdate(app,elapsed);return;}
            if(jitterCase!=JitterCase::None){JitterBeforeUpdate(app,elapsed);return;}
            if(visualMode){VisualBeforeUpdate(app,elapsed);return;}
            if(materialMode){MaterialsBeforeUpdate(app,elapsed);return;}
            current=&app;++frames;elapsed=GEngine::Timestep(1.0/60.0);
#ifdef GENGINE_CONFIG_DEBUG
            constexpr unsigned TickCap=300;
#else
            constexpr unsigned TickCap=1200;
#endif
            Check(frames<=3600 && completed<=TickCap,"sandbox execution cap");
            if(failure){app.ShutDown();return;}
            if(stage==Stage::Initial)
            {
                initialized=true;Check(app.SandboxEnabled(),"sandbox profile disabled");
                Check(app.m_ActiveSelection==DefaultSceneSelection,"owner startup default changed");
                Check(app.m_SimulationState==State::Ready && Steps(app)==0,"startup is not Ready at zero ticks");
                SavePrevious(app);
                invalidRejected=!app.RequestScene({static_cast<Preset>(999),8});Check(invalidRejected,"invalid scene accepted");
                Check(app.m_ActiveScene.get()==previousScene.lock().get() && !app.m_SceneRequest,"invalid request mutated scene");
                stage=Stage::Inject;elapsed=GEngine::Timestep(.2);return;
            }
            if(stage==Stage::Inject)
            {
                Check(Steps(app)==0,"Ready advanced Physics");SavePrevious(app);Queue(app,true);
                app.m_InjectSceneConstructionFailure=true;
                Check(app.RequestScene({Preset::DistanceChain,16}),"failure fixture request rejected");
                stage=Stage::FailurePending;return;
            }
            if(stage==Stage::Ready)
            {
                elapsed=GEngine::Timestep(.2);
                if(++hold>=3){pausedSteps=Steps(app);app.SetSimulationState(State::Running);stage=Stage::Starting;}
                return;
            }
            if(stage==Stage::Run)
            {
                if(extendedG8)Check(slept || Steps(app)<80,"G8 did not naturally sleep by existing deadline");
                if(wallProbe && Steps(app)>=4)Check(wallContact,"ordinary wall contact not observed within four steps");
                if(Steps(app)>=(wallProbe?4u:2u) && (!extendedG8 || slept))
                {
                    app.SetSimulationState(State::Paused);pausedSteps=Steps(app);Queue(app);
                    hold=0;stage=Stage::Pause;elapsed=GEngine::Timestep(.2);
                }
                return;
            }
            if(stage==Stage::Pause)
            {
                elapsed=GEngine::Timestep(.2);
                if(++hold>=3){app.SetSimulationState(State::Running);stage=Stage::Resuming;}
                return;
            }
            if(stage==Stage::AfterImpulse)
            {
                const auto target=extendedG8?impulseStep+30:pausedSteps+2;
                if(Steps(app)<target || sceneFrames<3)return;
                if(app.m_ActiveSelection.IsDistanceScene())Check(app.m_ChainImpulse.AppliedCount()==1 && applied,"single manual impulse missing or repeated");
                if(extendedG8){g8Proven=slept && wake && groundMask!=0;Check(g8Proven,"G8 sleep/wake/contact evidence incomplete");}
                if(successful==sequence.size())
                {
                    Check(presetMask==127,"not all seven presets observed");
                    finalScene=app.m_ActiveScene;GEngine::PhysicsDiagnostic::observer=nullptr;
                    stage=Stage::Done;app.ShutDown();return;
                }
                if(successful%2==0)app.SetSimulationState(State::Paused);
                SavePrevious(app);Queue(app,true);
                Check(app.RequestScene(sequence[successful]),"valid runtime scene request rejected");
                stage=Stage::SwitchPending;
            }
        }
        static void AfterUpdate(App& app)
        {
            if(contactRegression){ContactRegressionAfterUpdate(app);return;}
            if(jitterCase!=JitterCase::None){JitterAfterUpdate(app);return;}
            if(visualMode){VisualAfterUpdate(app);return;}
            if(materialMode){MaterialsAfterUpdate(app);return;}
            using namespace GEngine;
            if(failure)return;
            if(stage==Stage::FailurePending)
            {
                rollback=app.m_ActiveScene.get()==previousScene.lock().get() && failedScene.expired() &&
                    app.m_ActiveSelection==previousSelection && app.m_SimulationState==State::Paused &&
                    World(app).IsBodyValid(previousBody) && !World(app).ReadBody(failedBody) &&
                    (!failedJoint || !World(app).ReadDistanceJoint(failedJoint)) && !app.m_ChainImpulse.Pending();
                Check(rollback,"construction failure did not preserve old scene and retire candidate");
                BeginScene(app);stage=Stage::Ready;return;
            }
            if(stage==Stage::SwitchPending)
            {
                auto& w=World(app);const auto next=w.GetBodyCount()?*w.BodyAt(0):Body{};
                const bool oldBody=!w.ReadBody(previousBody),oldJoint=!previousJoint || !w.ReadDistanceJoint(previousJoint);
                Check(previousScene.expired() && oldBody && oldJoint && previousBody.registry!=next.registry,"retired scene or handles remained live");
                Check(!app.m_ActiveScene->RenderData().Resolve(previousRender) && !app.m_SceneResources->MeshMetadata(previousMesh),"retired render/resource identity accepted");
                Check(app.m_ActiveSelection==sequence[successful] && app.m_SimulationState==State::Ready,"switch did not commit requested Ready scene");
                ++successful;++rejectedOld;
                std::fprintf(transitions,"%u,%u,%u,%llu,%llu,%d,%d,%d,%d\n",successful,unsigned(app.m_ActiveSelection.scene),app.m_ActiveSelection.chainLinks,
                    (unsigned long long)previousBody.registry,(unsigned long long)next.registry,previousScene.expired(),oldBody,oldJoint,previousSelection==app.m_ActiveSelection);
                BeginScene(app);stage=Stage::Ready;return;
            }
            const auto steps=Steps(app);
            if(stage==Stage::Inject || stage==Stage::Ready || stage==Stage::Pause || stage==Stage::Starting || stage==Stage::Resuming)
            {
                const auto expected=(stage==Stage::Inject || stage==Stage::Ready)?0:pausedSteps;
                if(steps!=expected){pausedTicks+=unsigned(steps>expected?steps-expected:1);Check(false,"Ready/pause/resume accumulated Physics time");}
            }
            if(stage==Stage::Starting)stage=Stage::Run;
            else if(stage==Stage::Resuming)stage=Stage::AfterImpulse;
            if(stage==Stage::Inject)return;
            Check(steps>=previousSteps && steps-previousSteps<=2,"fixed scheduler tick count");
            completed+=steps-previousSteps;previousSteps=steps;
            auto& w=World(app);double energy=0,maxError=0,overlap=0;unsigned asleep=0;
            const auto boxes=SandboxEnvironment::Boxes();
            for(std::size_t i=0;i<w.GetBodyCount();++i)
            {
                auto b=w.ReadBody(*w.BodyAt(i));Check(bool(b),"live body read failed");if(!b)continue;
                Check(Math::IsFinite(b->position) && Math::IsFinite(b->linearVelocity) && Math::IsFinite(b->angularVelocity) &&
                    std::isfinite(b->orientation.w) && std::isfinite(b->orientation.x) && std::isfinite(b->orientation.y) && std::isfinite(b->orientation.z),"nonfinite presentation body");
                if(b->type!=Component::BodyType::Dynamic)continue;
                ++asleep;asleep-=!b->sleeping;
                const double mass=1.0/b->inverseMass;
                energy+=.5*mass*glm::dot(glm::dvec3(b->linearVelocity),glm::dvec3(b->linearVelocity))+12*mass*b->position.y;
                const glm::dmat3 inverseInertia(b->inverseInertiaWorld);
                if(std::abs(glm::determinant(inverseInertia))>1e-20)
                    energy+=.5*glm::dot(glm::dvec3(b->angularVelocity),glm::inverse(inverseInertia)*glm::dvec3(b->angularVelocity));
                if(b->shapeType==ShapeType::Sphere)
                    for(const auto& box:boxes)
                    {
                        const Vec center=Vector(box.center),half=Vector(box.size)*.5f;
                        const Vec q=glm::abs(b->position-center)-half;
                        const float signedDistance=glm::length(glm::max(q,Vec(0)))+
                            (std::min)((std::max)(q.x,(std::max)(q.y,q.z)),0.f);
                        overlap=(std::max)(overlap,double((b->worldBounds.maxs.x-b->worldBounds.mins.x)*.5f-signedDistance));
                    }
            }
            for(std::size_t i=0;i<w.GetJointCount();++i)
            {
                auto joint=w.ReadDistanceJoint(*w.JointAt(i));Check(bool(joint),"live joint read failed");if(!joint)continue;
                Check(std::isfinite(joint->error) && std::isfinite(joint->accumulatedImpulse),"nonfinite presentation joint");
                maxError=(std::max)(maxError,double(std::abs(joint->error)));
            }
            Check(std::isfinite(energy),"nonfinite presentation energy");
            if(app.m_ActiveSelection.IsDistanceScene())
            {
                // Existing OD08 transient limits: 1% of 2.25m joint length and
                // 2% of the 2m dynamic-sphere characteristic diameter.
                Check(maxError<=.0225,"presentation joint transient limit");
                Check(overlap<=.04,"presentation sphere/room penetration transient limit");
                auto root=w.ReadBody(rootBody);
                Check(root && root->position==rootPosition && root->linearVelocity==Vec(0) &&
                    root->angularVelocity==Vec(0),"static chain root changed");
                for(unsigned i=0;i<environmentBodies.size();++i)
                {
                    auto body=w.ReadBody(environmentBodies[i]);
                    Check(body && Near(body->position,Vector(boxes[i].center)) && body->linearVelocity==Vec(0) &&
                        body->angularVelocity==Vec(0),"static room body changed");
                }
            }
            maximumJointError=(std::max)(maximumJointError,maxError);maximumSphereOverlap=(std::max)(maximumSphereOverlap,overlap);
            std::fprintf(ticks,"%u,%u,%u,%llu,%u,%zu,%zu,%.12g,%.12g,%.9g,%.9g,%u,%u,%u,%u,%.12g\n",
                successful,unsigned(app.m_ActiveSelection.scene),app.m_ActiveSelection.chainLinks,(unsigned long long)steps,unsigned(app.m_SimulationState),
                w.GetBodyCount(),w.GetJointCount(),energy,externalWork,maxError,overlap,g8Contacts,groundMask,asleep,
                app.m_ChainImpulse.AppliedCount(),app.m_ActiveScene->GetPhysicsTiming().pendingSeconds);
        }
        static void ObserveFrame(App& app,const GEngine::RenderFrame& frame) noexcept
        {
            if(contactRegression)return;
            if(jitterCase!=JitterCase::None){JitterObserveFrame(app,frame);return;}
            if(visualMode){VisualObserveFrame(app,frame);return;}
            if(materialMode){MaterialsObserveFrame(app,frame);return;}
            if(!initialized || stage==Stage::Inject || stage==Stage::FailurePending)return;
            unsigned mask=0;
            for(const auto& draw:frame.Draws())for(unsigned i=0;i<environmentRender.size();++i)
                if(draw.entity==environmentRender[i])
                {
                    mask|=1u<<i;Vec lo(std::numeric_limits<float>::max()),hi(-std::numeric_limits<float>::max());
                    for(unsigned corner=0;corner<8;++corner)
                    {
                        Vec p;for(unsigned axis=0;axis<3;++axis)p[axis]=float((corner&(1u<<axis))?environmentBounds[i].maximum[axis]:environmentBounds[i].minimum[axis]);
                        p=Vec(draw.worldTransform*glm::vec4(p,1));lo=glm::min(lo,p);hi=glm::max(hi,p);
                    }
                    const auto box=SandboxEnvironment::Boxes()[i];const Vec center=Vector(box.center),half=Vector(box.size)*.5f;
                    Check(Near(lo,center-half) && Near(hi,center+half),"rendered room/collider bounds differ");
                }
            Check(mask==31,"render frame missing room members");
            Check(!frame.DirectionalLights().empty() && !frame.PointLights().empty(),"shared sandbox lights missing");
        }
        static void Capture(App& app)
        {
            const auto preset=app.m_ActiveSelection.scene;
            const unsigned bit=preset==Preset::DistanceChain && app.m_ActiveSelection.chainLinks==16?1:
                preset==Preset::DistanceChainContact?2:preset==Preset::SphereBoxStack?4:0;
            if(!bit || (capturedMask&bit) || !app.m_RenderTarget || sceneFrames<3)return;
            const auto& target=*app.m_RenderTarget;const auto& size=target.Buffer().Description();
            Check(size.Width && size.Height && size.Width<=4096 && size.Height<=4096,"capture extent invalid");if(failure)return;
            std::vector<std::byte> bytes(std::size_t(size.Width)*size.Height*4);
            Check(bool(target.ReadColor(bytes)),"scene capture read failed");if(failure)return;
            const auto name=bit==1?"C16.ppm":bit==2?"G8.ppm":"SphereBoxStack.ppm";
            auto* file=std::fopen((output/name).string().c_str(),"wbx");Check(file!=nullptr,"scene capture create failed");if(!file)return;
            std::fprintf(file,"P6\n%u %u\n255\n",size.Width,size.Height);
            bool ok=true;
            for(unsigned y=size.Height;y>0;--y)for(unsigned x=0;x<size.Width;++x)
                ok=std::fwrite(bytes.data()+((y-1)*size.Width+x)*4,1,3,file)==3 && ok;
            ok=std::fclose(file)==0 && ok;Check(ok,"scene capture write failed");capturedMask|=bit;++screenshots;
        }
        static void OnFrame(App& app,const GEngine::ScheduledFrameStats& stats)
        {
            if(contactRegression)return;
            if(jitterCase!=JitterCase::None){JitterOnFrame(app,stats);return;}
            if(visualMode){VisualOnFrame(app,stats);return;}
            if(materialMode){MaterialsOnFrame(app,stats);return;}
            ++sceneFrames;if(!initialized || stage==Stage::Inject || stage==Stage::FailurePending)return;
            const auto counters=GEngine::RenderCounters::Current();
            std::fprintf(render,"%u,%u,%u,%u,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%.6f,%.6f,%llu,NOT_MEASURED\n",frames,successful,
                unsigned(app.m_ActiveSelection.scene),app.m_ActiveSelection.chainLinks,stats.extraction.sceneEntities,stats.extraction.draws,
                stats.submission.submittedDrawCalls,stats.submission.submittedInstances,app.m_SceneResources->Meshes().Size(),
                app.m_SceneResources->Materials().Size(),stats.extraction.frameStorage.capacityBytes,stats.extraction.preparationMicroseconds,
                stats.extraction.extractionMicroseconds,(unsigned long long)counters.estimatedBufferBytes);
            Check(stats.extraction.draws>=5 && stats.submission.submittedDrawCalls>0,"sandbox frame did not submit geometry");
            if(sceneFrames==3 && presetMask==127 && GEngine::RenderCounters::Enabled)
            {
                const auto key=Key(app.m_ActiveSelection);
                if(haveWarmedCounters[key])Check(counters.liveNames==warmedCounters[key].liveNames,
                    "same preset retained GPU resource names after warmup");
                else{warmedCounters[key]=counters;haveWarmedCounters[key]=true;}
            }
            Capture(app);
        }
        static void PanelDrawn() { ++panels; }
        static bool Finish()
        {
            if(contactRegression)return FinishContactRegression();
            if(jitterCase!=JitterCase::None)return FinishJitter();
            if(visualMode)return FinishVisual();
            if(materialMode)return FinishMaterials();
            if(finished)return false;finished=true;current=nullptr;GEngine::PhysicsDiagnostic::observer=nullptr;
            const bool clean=initialized && finalScene.expired() && failedScene.expired() && previousScene.expired();
            if(ticks){std::fclose(ticks);ticks=nullptr;}if(transitions){std::fclose(transitions);transitions=nullptr;}if(render){std::fclose(render);render=nullptr;}
            if(wallEvents){Check(std::fclose(wallEvents)==0,"wall CCD evidence close failed");wallEvents=nullptr;}
            const bool pass=!failure && stage==Stage::Done && successful==20 && presetMask==127 &&
                invalidRejected && rollback && g8Proven && wallContact && clean && screenshots==3 && panels>0 && pausedTicks==0;
            auto* result=std::fopen((output/"result.json").string().c_str(),"wx");if(!result)return false;
            std::fprintf(result,"{\"presentation_only\":true,\"pass\":%s,\"failure\":\"%s\",\"clean_shutdown\":%s,\"frames\":%u,\"panels\":%u,\"completed_ticks\":%llu,\"successful_transitions\":%u,\"preset_mask\":%u,\"invalid_request_rejected\":%s,\"construction_rollback\":%s,\"g8_sleep_wake\":%s,\"wall_contact\":%s,\"wall_ccd_events\":%u,\"paused_ticks\":%u,\"stale_world_rejections\":%u,\"manual_boundary_commands\":%u,\"screenshots\":%u,\"max_joint_error_m\":%.9g,\"max_sphere_room_overlap_m\":%.9g,\"qualified_reference_performance\":\"INCONCLUSIVE\"}\n",
                pass?"true":"false",failure?failure:"none",clean?"true":"false",frames,panels,(unsigned long long)completed,successful,presetMask,
                invalidRejected?"true":"false",rollback?"true":"false",g8Proven?"true":"false",wallContact?"true":"false",wallCcdEvents,pausedTicks,rejectedOld,boundaryCommands,screenshots,maximumJointError,maximumSphereOverlap);
            const bool written=std::fclose(result)==0;return pass && !failure && written;
        }
    };
}
#endif
