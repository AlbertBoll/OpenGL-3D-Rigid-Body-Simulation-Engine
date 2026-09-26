#include "Animation/Bone.h"
#include "Animation/AnimationSystem.h"
#include <type_traits>
static_assert(std::is_same_v<decltype(std::declval<::GEngine::Bone&>().Update(0)), ::GEngine::BoneUpdateResult>);
static_assert(std::is_same_v<decltype(std::declval<::GEngine::AnimationSystem&>().UpdateAnimation(0)), ::GEngine::BoneUpdateResult>);
static_assert(std::is_same_v<decltype(std::declval<::GEngine::Bone&>().GetPositionIndex(0)),
    std::expected<std::size_t, ::GEngine::BoneKeyError>>);
#ifndef ANIMATION_SCHEMA_ONLY
#include <array>
#include <cstdlib>
#include <limits>
#include <print>
#include <string_view>

namespace Fixture
{
    bool valid=true;
    unsigned checks=0;
    void Check(bool ok, const char* reason)
    {
        ++checks;
        if (!ok) { valid=false; std::println("[FAIL] {}",reason); }
    }
}

#ifdef ANIMATION_EDITOR
#include "gepch.h"
#include "Core/BaseApp.h"
#include "Core/Scene.h"
#include <sdl2/SDL_ttf.h>
#define main ProductionEntryPoint
#include "EntryPoint.h"
#undef main
#define CreateApp OriginalCreateApp
#include "../GEngineEditor/src/SceneApp.cpp"
#undef CreateApp
namespace Fixture
{
    bool fail=false;
    unsigned updates=0,renders=0,destructors=0;
    class App final : public SceneApp
    {
    public:
        ::GEngine::ApplicationInitializationResult Initialize(const std::initializer_list<::GEngine::WindowProperties>& props) override
        {
            auto initialized=SceneApp::Initialize(props);
            if (!initialized) return initialized;
            SDL_Event event{}; while(SDL_PollEvent(&event)) {}
            m_WindowHidden=false; SetManualFrameRateLimit(0);
            return {};
        }
        void ProcessInput(::GEngine::Timestep) override {}
        void Update(::GEngine::Timestep) override
        {
            ++updates;
            // Real imported keys and real AnimationSystem/Editor Update: a NaN time
            // has no interval for an animated track, matching the former throw case.
            SceneApp::Update(::GEngine::Timestep(fail?std::numeric_limits<float>::quiet_NaN():0.f));
            ShutDown();
        }
        void Render() override { ++renders; }
        ::GEngine::ApplicationRunResult Run() override
        {
            auto result=BaseApp::Run();
            Check(result.has_value()!=fail,"Animation caller lost runtime status");
            Check(updates==1 && renders==(fail?0u:1u),"Animation failure reached later render/submission");
            if(!result)
            {
                const auto& error=result.error();
                Check(error.code==::GEngine::ApplicationRuntimeErrorCode::SubsystemFailure
                    && error.subsystem=="Animation" && error.subsystemCode=="MissingInterval"
                    && error.operation.starts_with("Bone::Get")
                    && error.context.starts_with("bone=") && error.context.find(" id=")!=std::string::npos
                    && error.context.find(" track=")!=std::string::npos && error.context.find("time=nan")!=std::string::npos
                    && error.context.find(" keys=")!=std::string::npos
                    && error.message.starts_with("Animation time has no ") && error.message.ends_with(" key interval"),
                    "Animation caller lost structured diagnostic fields");
            }
            return result;
        }
        ~App() override { ++destructors; }
    };
}
::GEngine::WindowProperties winProp=[] {
    ::GEngine::WindowProperties p; p.m_Title="Animation caller validation";
    p.m_Width=800;p.m_Height=600;p.m_MinWidth=p.m_MinHeight=32;p.m_IsVsync=false;
    p.flag=::GEngine::BitFlags<::GEngine::WindowFlags,uint8_t>{::GEngine::WindowFlags::INVISIBLE};return p;
}();
::GEngine::BaseApp* CreateApp() { return new Fixture::App; }
int main(int argc,char* argv[])
{
    if(argc!=2) return 2;
    const std::string_view mode=argv[1]; if(mode!="failure" && mode!="success") return 2;
    Fixture::fail=mode=="failure";SDL_SetMainReady();
    const int result=ProductionEntryPoint(argc,argv);
    Fixture::Check(result==(Fixture::fail?1:0),"Actual EntryPoint exit status changed");
    Fixture::Check(Fixture::destructors==1 && !::GEngine::EngineContext::TryGet() && SDL_WasInit(0)==0 && TTF_WasInit()==0,
        "Animation caller/root/platform RAII teardown failed");
    if(!Fixture::valid) return 97;
    std::println("[PASS] animation caller {} exit={} updates={} renders={} teardown=1",mode,result,Fixture::updates,Fixture::renders);
    return result;
}
#else
namespace
{
    using namespace ::GEngine;
    using namespace ::GEngine::Math;
    using Fixture::Check;
    bool Near(const Mat4& left,const Mat4& right)
    {
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(std::abs(left[c][r]-right[c][r])>2e-5f)return false;
        return true;
    }
    void Reject(const BoneUpdateResult& result,BoneKeyTrack track,float time,std::size_t count)
    {
        Check(!result,"Expected typed missing interval");if(result)return;
        const auto& e=result.error();
        const std::array operations{"Bone::GetPositionIndex","Bone::GetRotationIndex","Bone::GetScaleIndex"};
        const std::array messages{"Animation time has no position key interval","Animation time has no rotation key interval","Animation time has no scale key interval"};
        const auto index=static_cast<std::size_t>(track);
        Check(e.code==BoneKeyErrorCode::MissingInterval && e.track==track && e.boneName=="fixture" && e.boneId==7
            && (e.animationTime==time || (std::isnan(e.animationTime)&&std::isnan(time))) && e.keyCount==count
            && e.operation==operations[index] && e.message==messages[index],"Bone error lost original/full diagnostics");
    }
}
int main()
{
    const std::vector<KeyPosition> positions{{{0,0,0},0},{{4,8,12},2}};
    const std::vector<KeyRotation> rotations{{Quat(1,0,0,0),0},{glm::angleAxis(glm::radians(90.f),Vec3f(0,0,1)),2}};
    const std::vector<KeyScale> scales{{{1,1,1},0},{{3,3,3},2}};
    const std::vector<KeyPosition> constantPosition{{{1,2,3},0}};
    const std::vector<KeyRotation> constantRotation{{Quat(2,0,0,0),0}};
    const std::vector<KeyScale> constantScale{{{2,3,4},0}};
    Bone valid("fixture",7,positions,rotations,scales);
    Check(valid.Update(1).has_value(),"Midpoint update failed");
    const Mat4 expected=glm::translate(Mat4(1),Vec3f(2,4,6))*glm::rotate(Mat4(1),glm::radians(45.f),Vec3f(0,0,1))*glm::scale(Mat4(1),Vec3f(2));
    Check(Near(valid.GetLocalTransform(),expected),"Interpolation payload changed");
    Check(valid.GetPositionIndex(-1).value()==0
        && valid.GetRotationIndex(0).value()==0 && valid.GetScaleIndex(1).value()==0,"Original interval selection changed");
    Bone constant("fixture",7,constantPosition,constantRotation,constantScale);
    Check(constant.Update(std::numeric_limits<float>::quiet_NaN()).has_value(),"Constant tracks unexpectedly depend on time");
    Check(Near(constant.GetLocalTransform(),glm::translate(Mat4(1),Vec3f(1,2,3))*glm::scale(Mat4(1),Vec3f(2,3,4))),"Constant track normalization/payload changed");
    for(BoneKeyTrack track:{BoneKeyTrack::Position,BoneKeyTrack::Rotation,BoneKeyTrack::Scale})
    {
        Bone bone("fixture",7,track==BoneKeyTrack::Position?positions:constantPosition,
            track==BoneKeyTrack::Rotation?rotations:constantRotation,track==BoneKeyTrack::Scale?scales:constantScale);
        Check(bone.Update(1).has_value(),"Track setup failed");const auto retained=bone.GetLocalTransform();
        for(float time:{2.f,3.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        {
            Reject(bone.Update(time),track,time,2);
            Check(bone.GetLocalTransform()==retained,"Failed interpolation published a partial transform");
        }
        Bone empty("fixture",7,track==BoneKeyTrack::Position?std::vector<KeyPosition>{}:constantPosition,
            track==BoneKeyTrack::Rotation?std::vector<KeyRotation>{}:constantRotation,
            track==BoneKeyTrack::Scale?std::vector<KeyScale>{}:constantScale);
        Reject(empty.Update(0),track,0,0);
        Check(empty.GetLocalTransform()==Mat4(1),"Empty key track changed local transform");
    }
    AnimationSystem idle(nullptr);
    Check(idle.UpdateAnimation(1).has_value() && idle.GetFinalBoneMatrices().size()==100,"No-animation update/ownership behavior changed");
    if(!Fixture::valid)return 97;
    std::println("[PASS] animation key payload/constant/interval/three-track-errors/diagnostics/transactional-transform checks={}",Fixture::checks);
    return 0;
}
#endif
#endif
