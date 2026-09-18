#include "Renderer/FrameSubmission.h"
#include "Renderer/RenderExtraction.h"
#include <type_traits>
using namespace GEngine;
using namespace GEngine::Asset;
using namespace GEngine::Component;
static_assert(!std::is_copy_constructible_v<FrameSubmission>);
static_assert(!std::is_copy_constructible_v<RenderFrame>);
static_assert(std::same_as<decltype(std::declval<const RenderFrame&>().Draws()),std::span<const DrawItem>>);

#ifndef SUBMISSION_SCHEMA_ONLY
#include "Core/BaseApp.h"
#include "Core/Window.h"
#include "Core/RuntimeAssets.h"
#include "Core/GLContextThread.h"
#include "Managers/AssetsManager.h"
#include "Managers/ShapeManager.h"
#include "Managers/ShaderManager.h"
#include "Scene/_Entity.h"
#include "../GEngine/src/Assets/ShaderBackend.h"
#include <glm/gtx/quaternion.hpp>
#include <print>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace
{
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
        }
        Check(value,message);return std::move(*value);
    }
    FrameCamera Camera(_Scene& scene)
    {
        auto entity=scene.CreateEntity("camera");
        entity.AddComponent<RenderCameraComponent>();
        auto id=Take(scene.RenderData().Identify(entity),"camera identity");
        return {id,glm::lookAt(glm::vec3(0,2,8),glm::vec3(0,0,0),glm::vec3(0,1,0)),
            glm::perspective(glm::radians(45.f),1.f,.1f,20.f),{0,2,8},0,0,64,64};
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
    void PickingParity(const RenderFrame& frame,_Scene& scene,const FrameCamera& camera,
        const MousePickFrameBuffer& actual,std::span<const std::pair<MeshHandle,Geometry*>> geometry,Shader& shader)
    {
        std::vector<int> expectedPixels;
        for (int y=0;y<64;++y) for(int x=0;x<64;++x)
            expectedPixels.push_back(Take(actual.ReadPixel(x,y),"submitted pick pixel"));
        auto reference=Take(MousePickFrameBuffer::Create(64,64),"legacy picking reference target");
        reference.Bind();glViewport(0,0,64,64);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);glCullFace(GL_BACK);glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);
        Check(reference.ClearAttachment(0,-1),"clear legacy picking reference");
        shader.Bind();shader.SetUniform("u_view",camera.view);shader.SetUniform("u_projection",camera.projection);
        EntityPickTable table;
        for (const auto& draw:frame.Draws()) if(draw.pickable)
        {
            auto source=std::find_if(geometry.begin(),geometry.end(),[&](const auto& entry){return entry.first==draw.mesh;});
            Check(source!=geometry.end(),"legacy CPU/GPU fixture mapping");
            const auto entity=Take(scene.RenderData().Resolve(draw.entity),"legacy transform entity");
            const auto pose=scene.GetRenderTransform(_Entity{entity,&scene});
            Check(pose.matrix==draw.worldTransform,"legacy presentation transform parity");
            shader.SetUniform("u_model",pose.matrix);
            shader.SetUniform("u_EntityID",Take(table.Encode(draw.entity),"reference generation pixel"));
            auto* mesh=source->second;mesh->BindVAO();
            if(mesh->IsUsingIndexBuffer()) glDrawElements(GL_TRIANGLES,mesh->GetIndicesCount(),GL_UNSIGNED_INT,nullptr);
            else glDrawArrays(GL_TRIANGLES,0,mesh->GetVerticesCount());
        }
        std::size_t i=0;
        for (int y=0;y<64;++y) for(int x=0;x<64;++x)
        {
            const auto pixel=Take(reference.ReadPixel(x,y),"reference pick read");
            if (pixel!=expectedPixels[i]) std::println(stderr,"picking difference ({},{}) reference={} submitted={}",x,y,pixel,expectedPixels[i]);
            Check(pixel==expectedPixels[i++],"pixel-exact legacy picking rasterization");
        }
        reference.UnBind();
    }
    void Run(EngineContext& root)
    {
        auto services=Take(root.SceneServices(),"typed scene services");
        Geometry indexed;
        const std::vector<Vec3f> positions{{-1,0,0},{1,0,0},{0,1,0}};
        const std::vector<Vec3f> colors(3,Vec3f(1,0,0)), normals(3,Vec3f(0,0,1));
        const std::vector<Vec2f> uv{{0,0},{1,0},{.5f,1}};
        const std::vector<unsigned> indices{2,0,1};
        indexed.AddAttributes(positions,colors,uv,normals);indexed.AddIndices(indices);
        auto* window=SDL_GL_GetCurrentWindow();auto context=SDL_GL_GetCurrentContext();
        Check(SDL_GL_MakeCurrent(window,nullptr)==0,"detach for CPU shape export");
        auto indexedCpu=Take(indexed.ExportCpuMesh(),"indexed export without context");
        Check(indexedCpu.IndexFormat()==MeshIndexFormat::UInt32 && indexedCpu.IndexCount()==3
            && indexedCpu.Indices().size()==indices.size()*sizeof(unsigned)
            && std::memcmp(indexedCpu.Indices().data(),indices.data(),indexedCpu.Indices().size())==0,
            "CPU index order and bytes preserved");
        Check(indexedCpu.Submeshes()[0].elementCount==3 && indexedCpu.Layout().attributes.size()==4,
            "indexed range and all semantic attributes preserved");
        for (const auto* name:{"Sphere","Box","Diamond","PointLightHelper","AxisHelper","GridHelper","SkyBox","SmoothSphere"})
        {
            auto cpu=Take(services.shapes.ExportMesh(name),"CPU shape export without GL context");
            Check(cpu.VertexCount()>0 && cpu.Submeshes().size()==1,"complete CPU shape");
            for (const auto& attribute:cpu.Layout().attributes) Check(attribute.slot==AttributeSlot(attribute.semantic),"fixed semantic slot");
        }
        Check(!services.shapes.ExportMesh("missing-shape"),"missing shape typed failure");
        Check(SDL_GL_MakeCurrent(window,context)==0,"restore owner context");
        auto resources=Take(SceneRenderResources::Create(root),"resource publisher");
        const auto sphere=Take(resources->PublishShape("Sphere"),"sphere publication");
        Check(Take(resources->PublishShape("Sphere"),"reuse sphere")==sphere,"shared mesh identity");
        Check(!resources->PublishShape("absent"),"missing shape does not publish");
        Check(resources->Meshes().Size()==1,"failed mesh publication rolls back");
        const auto box=Take(resources->PublishShape("Box"),"box publication");
        const auto diamond=Take(resources->PublishShape("Diamond"),"diamond publication");
        const auto axis=Take(resources->PublishShape("AxisHelper"),"axis publication");
        const auto helperMesh=Take(resources->PublishShape("PointLightHelper"),"point helper publication");
        const std::pair<MeshHandle,Geometry*> referenceGeometry[]{
            {sphere,Manager::ShapeManager::GetShape("Sphere")},{box,Manager::ShapeManager::GetShape("Box")},
            {diamond,Manager::ShapeManager::GetShape("Diamond")},{helperMesh,Manager::ShapeManager::GetShape("PointLightHelper")}};
        for(const auto& [handle,geometry]:referenceGeometry) {MeshComponent legacyUpload(geometry);}
        auto* pickShader=Take(Manager::ShaderManager::GetShaderProgram({RuntimeAssets::File("Shaders/mouse_pick.vert"),
            RuntimeAssets::File("Shaders/mouse_pick.frag")}),"legacy reference picking shader");
        const char* names[]{"albedoMap","normalMap","metallicMap","roughnessMap","aoMap"};
        const char* paths[]{"PBR/rustediron/rustediron2_basecolor","PBR/rustediron/rustediron2_normal",
            "PBR/rustediron/rustediron2_metallic","PBR/rustediron/rustediron2_roughness",
            "PBR/subtle_black_granite/subtle-black-granite_ao"};
        std::vector<MaterialTextureAssignment> textures;
        for (int i=0;i<5;++i)
        {
            auto* image=Take(Manager::AssetsManager::GetTexture(paths[i],names[i]),"fixture texture");
            auto sample=Take(Manager::AssetsManager::SampleTexture(image->View()),"fixture sampling");
            textures.push_back({names[i],{sample.TextureIdentity(),sample.SamplerIdentity()}});
        }
        const MaterialParameterDecl parameters[]{
            {"metalness",MaterialParameterType::Float3,std::array<float,3>{.8f,.8f,.8f}},
            {"u_tiling",MaterialParameterType::Float2,std::array<float,2>{1,1}}};
        const auto material=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures}),"PBR material publication");
        const auto materialCount=resources->Materials().Size();
        const MaterialParameterDecl duplicate[]{{"same",MaterialParameterType::Float,1.f},{"same",MaterialParameterType::Float,2.f}};
        Check(!resources->PublishMaterial({SceneMaterialKind::Lit,duplicate,textures}),"invalid template rolls back dependencies");
        Check(resources->Materials().Size()==materialCount,"material rollback count");
        {
            _Scene physicsScene;
            const auto physicsCamera=Camera(physicsScene);
            auto body=physicsScene.CreateEntity("presentation body");
            body.GetComponent<Transform3DComponent>().Translation={0,5,0};
            body.AddComponent<MeshRendererComponent>(MeshRendererComponent{sphere,material});
            RigidBody3DComponent rigid;rigid.Type=BodyType::Dynamic;
            body.AddComponent<RigidBody3DComponent>(rigid);
            SphereFixture3DComponent fixture;fixture.Radius=1;fixture.Property.m_InvMass=1;
            body.AddComponent<SphereFixture3DComponent>(fixture);
            physicsScene.OnRuntimeStart();physicsScene.Update(Timestep(.025f));
            const auto authoritative=body.GetComponent<Transform3DComponent>().Translation;
            const auto expected=physicsScene.GetRenderTransform(body).matrix;
            auto access=resources->Publication().BeginFrame();
            auto frame=Take(Extract(physicsScene,*resources,access,physicsCamera),"physics presentation extraction");
            Check(frame.Draws().size()==1 && frame.Draws()[0].worldTransform==expected,"fixed-step presentation/interpolation parity");
            Check(body.GetComponent<Transform3DComponent>().Translation==authoritative,"extraction does not change authoritative pose");
        }
        const MaterialParameterDecl helperParameters[]{
            {"u_baseColor",MaterialParameterType::Float4,std::array<float,4>{1,1,1,1}},
            {"u_useVertexColor",MaterialParameterType::Boolean,true}};
        const auto helper=Take(resources->PublishMaterial({SceneMaterialKind::Helper,helperParameters,{},true,3}),"helper material");
        const auto pointMaterial=Take(resources->PublishMaterial({SceneMaterialKind::PointLight,{},{}}),"point material");
        TextureDesc skyDesc;skyDesc.kind=TextureKind::Cube;skyDesc.colorSpace=TextureColorSpace::Linear;
        skyDesc.mips=TextureMipIntent::None;skyDesc.orientation=ImageOrientation::TopLeft;
        auto* skyImage=Take(Manager::AssetsManager::GetTexture("SkyBox/Day/","u_skyBoxDay",".png",skyDesc),"sky texture");
        auto skySample=Take(Manager::AssetsManager::SampleTexture(skyImage->View()),"sky sampling");
        const MaterialTextureAssignment skyBinding{"u_skyBoxDay",{skySample.TextureIdentity(),skySample.SamplerIdentity()}};
        const auto skyMaterial=Take(resources->PublishMaterial({SceneMaterialKind::Sky,{},{&skyBinding,1},true}),"sky material");
        const auto skyMesh=Take(resources->PublishShape("SkyBox"),"sky mesh");
        auto submitter=Take(FrameSubmission::Create(),"serial submitter");
        RenderTargetDesc targetDesc;
        targetDesc.Storage.Width=targetDesc.Storage.Height=64;
        targetDesc.Storage.Colors[0]=FramebufferFormat::RGBA8;targetDesc.Storage.ColorCount=1;
        targetDesc.Storage.Depth=FramebufferFormat::Depth24Stencil8;
        auto target=Take(RenderTarget::Create(targetDesc),"color target");
        auto picking=Take(MousePickFrameBuffer::Create(64,64),"pick target");
        auto pointTarget=Take(PointShadowFrameBuffer::Create(256,256),"point target");
        auto cascadeTarget=Take(CascadeShadowFrameBuffer::Create(256,256,5),"cascade target");
        const float splits[]{.5f,1.f,2.f,5.f,20.f};
        FrameSubmissionDesc desc{target,picking,pointTarget,cascadeTarget,resources->Pipelines(),splits,glm::radians(45.f),1,.1f,20,.1f,100};
        _Scene scene;
        const auto camera=Camera(scene);
        std::array<_Entity,3> bodies;
        const MeshHandle meshes[]{sphere,box,diamond};
        for (int i=0;i<3;++i)
        {
            bodies[i]=scene.CreateEntity(std::string("body-")+std::to_string(i));
            bodies[i].AddComponent<MeshRendererComponent>(MeshRendererComponent{meshes[i],material});
            bodies[i].GetComponent<Transform3DComponent>().Translation={float(i-1)*2,0,0};
        }
        Check(resources->AttachPhysicsShape(bodies[1],"Box"),"legacy physics-only geometry preserved");
        auto axisEntity=scene.CreateEntity("axis");
        axisEntity.AddComponent<MeshRendererComponent>(MeshRendererComponent{axis,helper,0,false,false,true});
        axisEntity.AddComponent<VisibilityComponent>(VisibilityComponent{false});
        auto sky=scene.CreateEntity("sky");
        sky.AddComponent<MeshRendererComponent>(MeshRendererComponent{skyMesh,skyMaterial,0,false,false,false});
        auto dir=scene.CreateEntity("directional");
        RenderLightComponent directional;directional.color={.7f,.7f,.7f};directional.castShadows=true;
        dir.AddComponent<RenderLightComponent>(directional);
        dir.GetComponent<Transform3DComponent>().QuatRotation=glm::rotation(glm::vec3(0,0,-1),-glm::normalize(glm::vec3(20,50,20)));
        auto point=scene.CreateEntity("point");
        RenderLightComponent pointLight;pointLight.kind=RenderLightKind::Point;pointLight.color={.8f,.2f,.1f};pointLight.range=100;pointLight.castShadows=true;
        point.AddComponent<RenderLightComponent>(pointLight);
        point.GetComponent<Transform3DComponent>().Translation={0,3,2};
        point.AddComponent<MeshRendererComponent>(MeshRendererComponent{helperMesh,pointMaterial,0,false,false,true});
        EntityPickTable picks;
        std::vector<std::byte> original;
        std::optional<RenderFrame> retained;
        {
            auto access=resources->Publication().BeginFrame();
            auto frame=Take(Extract(scene,*resources,access,camera),"scene extraction with shared read scope");
            Check(frame.Draws().size()==5,"disabled helper omitted");
            Check(frame.DirectionalLights().size()==1 && frame.PointLights().size()==1,"typed frame lights");
            auto invalidDesc=desc;invalidDesc.cameraFov=0;
            auto invalid=submitter.Submit(frame,invalidDesc,picks);
            Check(!invalid && std::get<SubmissionCode>(invalid.error().cause)==SubmissionCode::InvalidShadowSettings,
                "invalid projection returns typed failure before drawing");
            auto stats=Take(submitter.Submit(frame,desc,picks),"submit complete frame");
            Check(stats.shadowDraws==6 && stats.pickDraws==4 && stats.colorDraws==3 && stats.helperDraws==1 && stats.skyDraws==1,"pass membership and one serial draw per item");
            original=Pixels(target);
            for(const auto& entry:frame.Resources()) if(entry.Material().Instance()==material)
            {
                const auto program=ShaderBackendAccess::Program(*entry.Material().Program());
                float color[3]{};glGetUniformfv(program,glGetUniformLocation(program,"directionallightColor"),color);
                Check(std::abs(color[0]-.7f)<1e-6f && std::abs(color[1]-.7f)<1e-6f,"typed directional color reaches driver");
                float position[3]{};glGetUniformfv(program,glGetUniformLocation(program,"lightPos"),position);
                Check(position[0]==0 && position[1]==3 && position[2]==2,"typed point pose reaches driver");
                float range{};glGetUniformfv(program,glGetUniformLocation(program,"pointShadowfarPlane"),&range);
                Check(range==100,"typed point range reaches shadow consumer");
                break;
            }
            int hits=0;
            for (int y=0;y<64;++y) for (int x=0;x<64;++x)
            {
                auto pixel=Take(picking.ReadPixel(x,y),"pick read");
                if (pixel>=0) {Check(scene.RenderData().ResolvePick(picks,pixel),"generation-safe picking");++hits;}
            }
            Check(hits>0,"pick image contains geometry");
            PickingParity(frame,scene,camera,picking,referenceGeometry,*pickShader);
            // This retained frame cannot observe subsequent authoring or removal.
            retained.emplace(std::move(frame));
        }
        const auto oldTransform=retained->Draws()[0].worldTransform;
        point.GetComponent<RenderLightComponent>().intensity=0;
        dir.GetComponent<RenderLightComponent>().intensity=0;
        {
            auto access=resources->Publication().BeginFrame();
            auto frame=Take(Extract(scene,*resources,access,camera),"zero-intensity light extraction");
            auto stats=Take(submitter.Submit(frame,desc,picks),"zero-intensity lights submit ambient only");
            Check(frame.DirectionalLights().empty() && frame.PointLights().empty() && stats.shadowDraws==0 && stats.helperDraws==0,"disabled contribution and point helper");
            Check(Pixels(target)!=original,"typed light contribution affects image");
        }
        point.GetComponent<RenderLightComponent>().intensity=1;
        dir.GetComponent<RenderLightComponent>().intensity=1;
        std::optional<MaterialInstance> edited;
        {
            auto access=resources->Publication().BeginFrame();
            auto view=Take(resources->Materials().Acquire(access,material),"material authoring lease");
            edited.emplace(*view);
        }
        Check(edited->SetParameter("metalness",std::array<float,3>{.2f,.2f,.2f}),"material edit");
        {
            auto access=resources->Publication().BeginPublication();
            Check(resources->Materials().Replace(access,material,std::move(*edited)),"publish replacement using same handle");
        }
        edited.reset();
        {
            auto access=resources->Publication().BeginPublication();
            Check(!resources->Meshes().Close(access),"live frame prevents premature GPU retirement");
        }
        bodies[0].GetComponent<Transform3DComponent>().Translation.x+=3;
        axisEntity.GetComponent<VisibilityComponent>().enabled=true;
        {
            auto access=resources->Publication().BeginFrame();
            auto frame=Take(Extract(scene,*resources,access,camera),"changed transform and helper visibility");
            Check(frame.Draws().size()==6,"helper appears after visibility edit");
            for(const auto& entry:frame.Resources()) if(entry.Material().Instance()==material)
                Check(entry.Material().PublicationRevision()==2,"new frame resolves replacement version");
            for(const auto& entry:retained->Resources()) if(entry.Material().Instance()==material)
                Check(entry.Material().PublicationRevision()==1,"old frame retains prior resource version");
            Check(retained->Draws()[0].worldTransform==oldTransform,"published transform immutable");
            auto stats=Take(submitter.Submit(frame,desc,picks),"submit edited scene");
            Check(stats.helperDraws==2,"editor helper visible");
            Check(Pixels(target)!=original,"presentation edit changes image");
        }
        scene.DestroyEntity(bodies[0]);scene.DestroyEntity(bodies[1]);scene.DestroyEntity(bodies[2]);
        scene.DestroyEntity(dir);scene.DestroyEntity(point);
        scene.DestroyEntity(sky);
        {
            auto access=resources->Publication().BeginFrame();
            auto stats=Take(submitter.Submit(*retained,desc,picks),"retained frame after ECS destruction");
            Check(stats.colorDraws==3 && Pixels(target)==original,"retained frame exact image independent of ECS");
            Check(!scene.RenderData().ResolvePick(picks,0),"destroyed entity picking is stale");
            auto frame=Take(Extract(scene,*resources,access,camera),"fresh frame after removal");
            Check(frame.DirectionalLights().empty() && frame.PointLights().empty(),"light removal published");
            auto emptyStats=Take(submitter.Submit(frame,desc,picks),"lightless helper frame");
            Check(emptyStats.colorDraws==0 && emptyStats.shadowDraws==0,"removed draw/light contribution");
        }
        auto replacement=scene.CreateEntity("replacement after destroy");
        replacement.AddComponent<MeshRendererComponent>(MeshRendererComponent{box,material});
        const auto replacementId=Take(scene.RenderData().Identify(replacement),"replacement generation");
        for (const auto& old:retained->Draws()) Check(old.entity!=replacementId,"recreated entity has new generation identity");
        {
            auto access=resources->Publication().BeginFrame();
            auto frame=Take(Extract(scene,*resources,access,camera),"new renderable after destruction");
            Check(frame.Draws().size()==2,"new renderable and visible helper extracted");
        }
        auto spot=scene.CreateEntity("unsupported spot");
        RenderLightComponent spotLight;spotLight.kind=RenderLightKind::Spot;
        spot.AddComponent<RenderLightComponent>(spotLight);
        {
            auto access=resources->Publication().BeginFrame();
            auto frame=Take(Extract(scene,*resources,access,camera),"typed spot extraction");
            auto result=submitter.Submit(frame,desc,picks);
            Check(!result && std::get<SubmissionCode>(result.error().cause)==SubmissionCode::UnsupportedLights,"unsupported light schema fails before submission");
        }
        // Dropping CPU leases on a worker cannot perform GPU retirement.
        std::thread release([frame=std::move(retained)]() mutable {frame.reset();});release.join();
        Check(glGetError()==GL_NO_ERROR,"no driver errors");
        std::println("[PASS] frame-submission checks={} (owner/resource destruction follows)",checks);
    }
}
int main()
{
    RuntimeAssets::Initialize("RigidBodySimulation");
    EngineContext root;
    Check(!root.SceneServices(),"uninitialized service typed failure");
    WindowProperties properties;properties.m_Title="Phase 42 hidden submission validation";
    properties.flag={WindowFlags::INVISIBLE};properties.m_Width=properties.m_Height=64;
    properties.m_MinWidth=properties.m_MinHeight=64;properties.m_IsVsync=false;
    Check(root.Initialize({properties}),"owner root initialization");
    Run(root);
    std::println("[PASS] frame-submission-retirement");
}
#endif
