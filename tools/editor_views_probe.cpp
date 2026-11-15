#define GENGINE_OWNER_REVIEW_PROBE
#define GENGINE_OWNER_REVIEW_ALLOC_IMPLEMENTATION
#include "../RigidBodySimulation/tests/OwnerReviewProbe.h"
#include <concepts>
#include <functional>
#include <cstdlib>
#include <iostream>
#include "Renderer/FrameSubmission.h"
#include "Renderer/RenderExtraction.h"
#include "Renderer/FrameScheduler.h"
#include "Renderer/PassTiming.h"
#include "Renderer/ShadowQuality.h"
#include "RbsComponentEditing.h"
#include <type_traits>
#include "../GEngine/src/Renderer/DrawOrdering.h"
#include "../GEngine/src/Renderer/ShadowCulling.h"
using namespace GEngine;
using namespace GEngine::Asset;
using namespace GEngine::Component;
static_assert(!std::is_copy_constructible_v<FrameSubmission>);
static_assert(!std::is_copy_constructible_v<PassTiming>);
static_assert(std::is_nothrow_move_constructible_v<PassTiming>);
static_assert(!std::is_copy_constructible_v<RenderFrame>);
static_assert(std::same_as<decltype(std::declval<const RenderFrame&>().Draws()),std::span<const DrawItem>>);


#include "Geometry/Geometry.h"
#include "Core/BaseApp.h"
#include "Core/Window.h"
#include "Core/RuntimeAssets.h"
#include "Core/GLContextThread.h"
#include "Managers/AssetsManager.h"
#include "Managers/ShapeManager.h"
#include "Managers/ShaderManager.h"
#include "Scene/_Entity.h"
#include "Physics/PhysicsBody.h"
#include "Physics/Shape.h"
#include "../GEngine/src/Assets/ShaderBackend.h"
#include "../GEngine/src/Renderer/GLStateCache.h"
#include "../GEngine/src/Renderer/SubmissionUploads.h"
#include "../GEngine/src/Core/FramebufferBackend.h"
#include <chrono>
#include <bit>
#include <numeric>
#include <glm/gtx/quaternion.hpp>
#include <print>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <fstream>
#include <imgui/imgui.h>
#include <set>


WindowProperties winProp{};
namespace {
int checks{};
    template<class T> void Check(const T& value,const char* message)
    {
        ++checks;
        if (!static_cast<bool>(value)) {std::println(stderr,"[FAIL] {}",message);std::exit(1);}
    }
    template<class T,class E> T Take(std::expected<T,E>&& value,const char* message)
    {
        if (!value) {
            if constexpr (std::same_as<E,SceneResourceError>) std::println(stderr,"{}",DescribeSceneResourceError(value.error()));
            if constexpr (std::same_as<E,SubmissionError>) std::println(stderr,"{}",DescribeSubmissionError(value.error()));
            if constexpr (std::same_as<E,ScheduleError>) std::println(stderr,"{}",DescribeScheduleError(value.error()));
        }
        Check(value,message);return std::move(*value);
    }

    template<std::invocable Operation>
    auto SceneOperationChecked(Operation&& operation)
    {
        using Result = std::remove_cvref_t<std::invoke_result_t<Operation>>;
        if constexpr (std::is_void_v<Result>) {
            std::invoke(std::forward<Operation>(operation));
        } else {
            auto result = std::invoke(std::forward<Operation>(operation));
            if constexpr (requires { typename Result::error_type; typename Result::value_type; }) {
                if (!result) {
                    const auto& error = result.error();
                    std::cerr << "[FAIL] Valid Scene fixture: operation=" << error.operation
                        << " code=" << static_cast<unsigned>(error.code) << " entity=" << error.entity
                        << ": " << error.message << '\n';
                    std::exit(1);
                }
                if constexpr (std::is_void_v<typename Result::value_type>) return;
                else return std::move(*result);
            } else return result;
        }
    }


    FrameCamera Camera(_Scene& scene)
    {
        auto entity=SceneOperationChecked([&] { return scene.CreateEntity("camera"); });
        entity.AddComponent<RenderCameraComponent>();
        auto id=Take(scene.RenderData().Identify(entity),"camera identity");
        return {id,glm::lookAt(glm::vec3(0,2,8),glm::vec3(0,0,0),glm::vec3(0,1,0)),
            glm::perspective(glm::radians(45.f),1.f,.1f,20.f),{0,2,8},0,0,64,64};
    }
    void ProjectionChecks()
    {
        const std::array<float,5> authored{20,40,100,320,1000};
        const auto unchanged=Rbs::Editor::CameraCascadeSplits(authored,double(.1f),1000,double(.1f),1000);
        Check(unchanged && *unchanged==authored,"default view keeps exact authored cascade fixture");
        for(auto clips:{glm::dvec2(.1,40),glm::dvec2(60,1000)}) {
            const auto slices=Rbs::Editor::CameraCascadeSplits(authored,double(.1f),1000,clips.x,clips.y);
            Check(slices && (*slices)[0]>clips.x && slices->back()==float(clips.y),"edited camera binds valid shadow clip slices");
        }
        for(auto projection:{CameraProjection::Perspective,CameraProjection::Orthographic}) {
            CameraSnapshotDesc d;d.projection=projection;d.position={3,7,-2};
            d.orientation=glm::angleAxis(.61,glm::normalize(glm::dvec3(1,2,3)));
            d.nearPlane=.3;d.farPlane=70;d.verticalFovRadians=1.1;d.orthographicHeight=8;
            d.viewport.pixelSize={1600,900};
            const auto camera=Take(CameraSnapshot::Create(d),"camera projection fixture");
            for(int z:{-1,1})for(int y:{-1,1})for(int x:{-1,1}) {
                const auto corner=Take(camera.NdcToWorld({x,y,z}),"frustum world corner");
                const auto local=glm::conjugate(d.orientation)*(corner-d.position);
                const double depth=z<0?d.nearPlane:d.farPlane;
                const double halfHeight=projection==CameraProjection::Perspective?depth*std::tan(d.verticalFovRadians*.5):d.orthographicHeight*.5;
                Check(std::abs(local.z+depth)<1e-8 && std::abs(local.y-y*halfHeight)<1e-8 && std::abs(local.x-x*halfHeight*1600./900.)<1e-8,"actual frustum matches authored clip planes/FOV/orthographic size");
            }
        }
        const glm::vec3 position(3,7,-2),direction=glm::normalize(glm::vec3(-.2f,-1,-.3f));
        const auto matrix=SpotShadowMatrix(position,direction,40,.6f);Check(matrix,"spot projection fixture");
        const auto farPoint=*matrix*glm::vec4(position+40.f*direction,1);
        Check(std::abs(farPoint.x/farPoint.w)<1e-5f && std::abs(farPoint.y/farPoint.w)<1e-5f && std::abs(farPoint.z/farPoint.w-1)<1e-5f,"spot optical axis and far clip match position/direction/range");
        Check(!SpotShadowMatrix(position,{},40,.6f) && !SpotShadowMatrix(position,direction,0,.6f),"degenerate spot projections rejected");
    }
    std::vector<std::byte> Pixels(RenderTarget& target)
    {
        std::vector<std::byte> pixels(64*64*4);
        Check(target.ReadColor(pixels),"color readback");return pixels;
    }
    auto Extract(_Scene& scene,SceneRenderResources& resources,const AssetPublication::FrameAccess& access,const FrameCamera& camera)
    {
        RenderExtractionStats stats;
        return ExtractRenderFrame(scene,resources.ForFrame(access),stats,{&camera,1});
    }

    std::vector<float> ShadowDepth(const PointShadowFrameBuffer& point,const CascadeShadowFrameBuffer& cascade)
    {
        constexpr GLenum keys[]{GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS,
            GL_PACK_IMAGE_HEIGHT,GL_PACK_SKIP_IMAGES,GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST};
        GLint packBuffer{};std::array<GLint,std::size(keys)> saved{};
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&packBuffer);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
        for(std::size_t i=0;i<saved.size();++i) {glGetIntegerv(keys[i],&saved[i]);glPixelStorei(keys[i],i==0?1:0);}
        const auto layers=cascade.Buffer().Description().Layers;
        std::vector<float> result(256*256*(layers+6));
        glGetTextureImage(FramebufferDetail::Backend::Depth(cascade.Buffer()),0,GL_DEPTH_COMPONENT,GL_FLOAT,
            static_cast<GLsizei>(256*256*layers*sizeof(float)),result.data());
        GLint previousRead{};GLuint readback{};glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead);
        glGenFramebuffers(1,&readback);glBindFramebuffer(GL_READ_FRAMEBUFFER,readback);
        glNamedFramebufferDrawBuffer(readback,GL_NONE);glReadBuffer(GL_NONE);
        for(unsigned face=0;face<6;++face) {
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,
                FramebufferDetail::Backend::Depth(point.Buffer()),0);
            Check(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"culling depth attachment complete");
            glReadPixels(0,0,256,256,GL_DEPTH_COMPONENT,GL_FLOAT,result.data()+256*256*(layers+face));
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER,previousRead);glDeleteFramebuffers(1,&readback);
        for(std::size_t i=0;i<saved.size();++i) glPixelStorei(keys[i],saved[i]);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,packBuffer);
        Check(glGetError()==GL_NO_ERROR,"culling depth readback succeeds");
        Check(std::all_of(result.begin(),result.end(),[](float v){return std::isfinite(v) && v>=0 && v<=1;}),"culling depth finite/normalized");
        return result;
    }

}
namespace ShadowCalls {
    decltype(glad_glGenTextures) gen{}; unsigned allocations{};
    void APIENTRY Gen(GLsizei count,GLuint* names){allocations+=count;gen(count,names);}
    struct Observe { Observe(){allocations=0;gen=glad_glGenTextures;glad_glGenTextures=Gen;} ~Observe(){glad_glGenTextures=gen;} };
}
namespace SpotFailure {
    decltype(glad_glTexImage2D) real{};
    void APIENTRY Image(GLenum t,GLint l,GLint f,GLsizei w,GLsizei h,GLint b,GLenum e,GLenum p,const void* d) {
        if(t!=GL_TEXTURE_2D || w!=1024)real(t,l,f,w,h,b,e,p,d);
    }
}
namespace { void CameraLightSpotChecks(EngineContext& root) {

        constexpr unsigned size=256;
        auto resources=Take(SceneRenderResources::Create(root),"reference resources");
        auto box=Take(resources->PublishShape("Box"),"reference box");
        auto sphere=Take(resources->PublishShape("Sphere"),"reference sphere");
        TextureRegistry* images{};
        {auto access=resources->Publication().BeginFrame();images=&const_cast<TextureRegistry&>(resources->ForFrame(access).bindings.textures);}
        auto texture=[&](std::array<unsigned char,4> rgba,bool checker=false) {
            TextureDesc desc;desc.width=desc.height=4;desc.mips=TextureMipIntent::None;desc.colorSpace=TextureColorSpace::Linear;
            std::array<std::byte,64> bytes{};
            for(unsigned i=0;i<16;++i) for(unsigned c=0;c<4;++c)
                bytes[4*i+c]=std::byte(checker && c==3 && ((i%4+i/4)%2)==0?0:rgba[c]);
            auto value=Take(TextureResource::Create(desc,{bytes}),"reference texture");
            auto publication=resources->Publication().BeginPublication();return Take(images->Create(publication,std::move(value)),"reference texture publication");
        };
        SamplerDesc nearest;nearest.minFilter=nearest.magFilter=SamplerFilter::Nearest;
        const auto sampler=Take(Manager::AssetsManager::GetSampler(nearest),"reference nearest sampler");
        const auto albedo=texture({128,96,64,255}),normal=texture({128,128,255,255});
        const auto metallic=texture({0,0,0,255}),roughness=texture({160,160,160,255}),ao=texture({255,255,255,255});
        const auto coverage=texture({128,96,64,255},true);
        std::array<MaterialTextureAssignment,5> textures{{{"albedoMap",{albedo,sampler}},{"normalMap",{normal,sampler}},
            {"metallicMap",{metallic,sampler}},{"roughnessMap",{roughness,sampler}},{"aoMap",{ao,sampler}}}};
        const MaterialParameterDecl parameters[]{
            {"metalness",MaterialParameterType::Float3,std::array<float,3>{.04f,.04f,.04f}},
            {"u_tiling",MaterialParameterType::Float2,std::array<float,2>{1,1}}};
        const auto opaque=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures}),"reference opaque");
        textures[0].value.texture=coverage;
        const auto masked=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,AlphaMode::Masked,.5f}),"reference masked");
        const auto transparent=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,AlphaMode::Transparent,.5f,.5f}),"reference transparent");
        const auto premultiplied=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,AlphaMode::Transparent,.5f,.5f,TransparentBlend::PremultipliedAlpha}),"reference premultiplied");
        TextureDesc fallbackDesc;fallbackDesc.colorSpace=TextureColorSpace::Linear;fallbackDesc.mips=TextureMipIntent::None;
        const auto fallback=Take(Manager::AssetsManager::FallbackTexture(fallbackDesc),"reference fallback texture");
        auto fallbackView=Take(Manager::AssetsManager::ResolveTexture(fallback),"reference fallback view");
        const auto fallbackSampling=Take(Manager::AssetsManager::SampleTexture(fallbackView),"reference default sampler");
        textures[0].value={fallback,fallbackSampling.SamplerIdentity()};
        const auto fallbackMaterial=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures}),"reference fallback material");
        // Valid-looking foreign identities exercise explicit preparation fallback.
        auto badTextures=textures;badTextures[0].value.texture=albedo;badTextures[0].value.sampler={99,99,sampler.registry};
        const auto badSamplerMaterial=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,badTextures}),"reference invalid sampler material");
        badTextures=textures;badTextures[0].value.texture={99,99,albedo.registry};
        const auto badTextureMaterial=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,badTextures}),"reference invalid texture material");
        RenderTargetDesc td;td.Storage.Width=td.Storage.Height=size;td.Storage.ColorCount=1;
        td.Storage.Colors[0]=FramebufferFormat::RGBA8;td.Storage.Depth=FramebufferFormat::Depth24Stencil8;
        auto target=Take(RenderTarget::Create(td),"reference target");
        auto picking=Take(MousePickFrameBuffer::Create(size,size),"reference picking");
        auto pointDepth=Take(PointShadowFrameBuffer::Create(256,256),"reference point depth");
        auto cascadeDepth=Take(CascadeShadowFrameBuffer::Create(256,256,5),"reference cascade depth");
        const float splits[]{.5f,1.f,2.f,5.f,20.f};
        FrameSubmissionDesc desc{target,picking,pointDepth,cascadeDepth,resources->Pipelines(),splits,glm::radians(45.f),1,.1f,20,.1f,100,false};
        auto submitter=Take(FrameSubmission::Create(),"reference submitter");EntityPickTable picks;
        auto read=[&] {std::vector<std::byte> result(size*size*4);Check(target.ReadColor(result),"reference pixels");return result;};
        auto save=[&](const std::string& label,const std::vector<std::byte>& pixels) {
            std::ofstream out(label+".ppm",std::ios::binary);out<<"P6\n"<<size<<' '<<size<<"\n255\n";
            for(int y=size-1;y>=0;--y) for(unsigned x=0;x<size;++x) out.write(reinterpret_cast<const char*>(pixels.data()+4*(y*size+x)),3);
            Check(out.good(),"reference image saved");
        };
        std::ofstream metadata("reference-metadata.txt");
        metadata<<"GPU="<<glGetString(GL_RENDERER)<<"\nvendor="<<glGetString(GL_VENDOR)<<"\ndriver="<<glGetString(GL_VERSION)
            <<"\nGLSL="<<glGetString(GL_SHADING_LANGUAGE_VERSION)<<"\nsize=256x256\nattachment=RGBA8 linear; framebuffer sRGB disabled; production shader RGB power 1/1.8\n"
            <<"camera=(0,0,8), target=(0,0,0), up=(0,1,0), ortho=(-3,3,-3,3,.1,20); front z=0\n"
            <<"shadow=256x256 Depth32Float, 5 used cascades + 6 cube faces; splits=.5,1,2,5,20\n"
            <<"materials=linear RGBA8 albedo(128,96,64), normal(128,128,255), metallic0, roughness160, AO255; F0=.04\n"
            <<"sampler=nearest/repeat/no mips; fallback=engine 4x4 checker/default linear-repeat sampler\n"
            <<"initial Spot position=(-2,2,3), direction=normalize(2,-2,-3), color=(.8,.55,.3), intensity=.12, cones=(.3,.7), range=10\n"
            <<"timing Spot position=(-1.5,2,3), direction=normalize(.2,-.3,-1), cones=(.3,.8), range=8, resolution=256, bias=.002, PCF=5x5\n"
            <<"timing Directional ray=normalize(-.3,-.4,-1), intensity=.2; Point position=(2,2,3), intensity=.05, range=10; both shadowed\n"
            <<"receiver=6x6x.1 at(0,0,-.05); four .45 casters: x=-.8+.55*i,z=1.1; caster0.y=.6, caster1 masked; moving case caster0.x=-.8+.1*sin(frame*.1)\n"
            <<"timing=144 frames per case, first24 warmup; no Physics/time/jitter/MSAA; GPU query results delayed, final unavailable samples abandoned\n";
        _Scene scene;auto camera=Camera(scene);camera.viewportWidth=camera.viewportHeight=size;
        camera.worldPosition={0,0,8};camera.view=glm::lookAt(camera.worldPosition,glm::vec3(0),glm::vec3(0,1,0));
        camera.projection=glm::ortho(-3.f,3.f,-3.f,3.f,.1f,20.f);
        auto plate=SceneOperationChecked([&] { return scene.CreateEntity("reference plate"); });plate.AddComponent<MeshRendererComponent>(MeshRendererComponent{box,opaque});
        plate.Transform().Scale={6,6,.1f};plate.Transform().Translation.z=-.05f;
        auto lamp=SceneOperationChecked([&] { return scene.CreateEntity("reference light"); });RenderLightComponent light;light.kind=RenderLightKind::Spot;
        light.color={.8f,.55f,.3f};light.intensity=0;light.range=10;light.innerConeRadians=.3f;light.outerConeRadians=.7f;
        lamp.AddComponent<RenderLightComponent>(light);lamp.Transform().Translation={0,0,3};
        auto render=[&](const std::string& name) {
            auto access=resources->Publication().BeginFrame();auto frame=Take(Extract(scene,*resources,access,camera),"reference extraction");
            const auto stats=Take(submitter.Submit(frame,desc,picks),"reference submission");auto pixels=read();
            if(name=="instanced-repeated-objects") Check(stats.instancedDrawCalls>0 && stats.submittedInstances>=6,"reference actually instances repeated geometry");
            Take(submitter.Submit(frame,desc,picks),"reference repeat");Check(read()==pixels,"frozen frame repeat exact");
            save(name,pixels);metadata<<name<<": draws="<<stats.colorDraws<<", shadows="<<stats.shadowDraws<<", instances="<<stats.instancedDrawCalls<<'\n';
            metadata<<" camera eye="<<camera.worldPosition.x<<','<<camera.worldPosition.y<<','<<camera.worldPosition.z<<" view=";
            for(unsigned i=0;i<16;++i) metadata<<glm::value_ptr(camera.view)[i]<<',';
            metadata<<" projection=";for(unsigned i=0;i<16;++i) metadata<<glm::value_ptr(camera.projection)[i]<<',';metadata<<'\n';
            for(const auto& value:frame.DirectionalLights()) metadata<<" directional ray="<<value.direction.x<<','<<value.direction.y<<','<<value.direction.z<<" intensity="<<value.intensity<<" shadows="<<value.shadows.castShadows<<'\n';
            for(const auto& value:frame.PointLights()) metadata<<" point position="<<value.position.x<<','<<value.position.y<<','<<value.position.z<<" intensity="<<value.intensity<<" range="<<value.range<<" shadows="<<value.shadows.castShadows<<'\n';
            for(const auto& value:frame.SpotLights()) metadata<<" spot position="<<value.position.x<<','<<value.position.y<<','<<value.position.z<<" ray="<<value.direction.x<<','<<value.direction.y<<','<<value.direction.z<<" intensity="<<value.intensity<<" range="<<value.range<<" cones="<<value.innerConeRadians<<','<<value.outerConeRadians<<'\n';
            for(const auto& draw:frame.Draws()) {metadata<<" model=";for(unsigned i=0;i<16;++i) metadata<<glm::value_ptr(draw.worldTransform)[i]<<',';metadata<<'\n';}
            return pixels;
        };

#include "camera_light_spot_checks.inl"
#include "editor_views_checks.inl"
} }
int main() {
    RuntimeAssets::Initialize("RigidBodySimulation");EngineContext root;
    WindowProperties p;p.m_Title="Camera Light Spot bounded checks";p.flag={WindowFlags::INVISIBLE};
    p.m_Width=p.m_Height=p.m_MinWidth=p.m_MinHeight=64;p.m_IsVsync=false;
    Check(root.Initialize({p}),"root init");ProjectionChecks();CameraLightSpotChecks(root);
    std::println("[PASS] camera-light-spot resource retirement");
}
