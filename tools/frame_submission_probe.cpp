#include "Renderer/FrameSubmission.h"
#include "Renderer/RenderExtraction.h"
#include "Renderer/FrameScheduler.h"
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
#include <fstream>
#include <imgui/imgui.h>

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
            if constexpr (std::same_as<E,ScheduleError>) std::println(stderr,"{}",DescribeScheduleError(value.error()));
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
    void Invalidation(EngineContext& root,SceneRenderResources& resources,FrameSubmissionDesc desc,
        MeshHandle mesh,MaterialInstanceHandle material)
    {
        auto submitter=Take(FrameSubmission::Create(),"invalidation submitter");
        _Scene scene; auto camera=Camera(scene);
        auto body=scene.CreateEntity("cached body");
        body.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh,material});
        body.AddComponent<VisibilityComponent>();
        auto sun=scene.CreateEntity("cached sun"),bulb=scene.CreateEntity("cached bulb");
        RenderLightComponent light;light.castShadows=true;
        sun.AddComponent<RenderLightComponent>(light);
        sun.GetComponent<Transform3DComponent>().QuatRotation=glm::rotation(glm::vec3(0,0,-1),-glm::normalize(glm::vec3(20,50,20)));
        light.kind=RenderLightKind::Point;light.range=100;
        bulb.AddComponent<RenderLightComponent>(light);
        bulb.GetComponent<Transform3DComponent>().Translation={0,3,2};
        auto picking=Take(MousePickFrameBuffer::Create(64,64),"invalidation picking target");
        auto point=Take(PointShadowFrameBuffer::Create(256,256),"invalidation point target");
        auto cascade=Take(CascadeShadowFrameBuffer::Create(256,256,5),"invalidation cascade target");
        FrameSubmissionDesc targets{desc.color,picking,point,cascade,resources.Pipelines(),desc.cascadeSplits,
            desc.cameraFov,desc.cameraAspect,desc.cameraNear,desc.cameraFar,desc.pointNear,desc.pointFar};
        EntityPickTable picks;
        std::vector<ScenePipeline> overrideRoles;
        auto run=[&] {
            auto access=resources.Publication().BeginFrame();
            auto frame=Take(Extract(scene,resources,access,camera),"invalidation extraction");
            targets.pipelines=overrideRoles.empty()?resources.Pipelines():std::span<const ScenePipeline>(overrideRoles);
            return Take(submitter.Submit(frame,targets,picks),"invalidation submission");
        };
        auto expect=[&](PassDirtyReason reason,unsigned mask) {
            auto result=run();
            for(unsigned i=0;i<3;++i) {
                Check(result.decisions[i].executed==bool(mask&(1u<<i)),"exact dirty pass selection");
                if(mask&(1u<<i)) Check(HasDirtyReason(result.decisions[i].reasons,reason),"traceable dirty reason");
                else if(result.decisions[i].requested) Check(result.decisions[i].reasons==PassDirtyReason::None,"unaffected requested pass stays clean");
            }
            return result;
        };
        expect(PassDirtyReason::InitialContent,7);
        const auto color=Pixels(desc.color);
        const auto firstPixel=Take(picking.ReadPixel(32,32),"cached initial pixel");
        auto unchanged=expect(PassDirtyReason::None,0);
        Check(unchanged.shadowDraws==0 && unchanged.pickDraws==0 && Pixels(desc.color)==color,"unchanged frame skips cacheable draws and preserves color");
        Check(Take(picking.ReadPixel(32,32),"cached reused pixel")==firstPixel && picks.Decode(firstPixel),"cache hit keeps image and table paired");
        body.GetComponent<Transform3DComponent>().Translation.x=.25f;
        expect(PassDirtyReason::Transform,7);
        camera.view[3][0]+=.1f;
        expect(PassDirtyReason::Camera,5); // Point shadows are independent of camera.
        camera.projection[0][0]*=1.05f;
        expect(PassDirtyReason::Camera,5);
        bulb.GetComponent<Transform3DComponent>().Translation.x+=.25f;
        expect(PassDirtyReason::Light,2);
        bulb.GetComponent<RenderLightComponent>().range=80;
        expect(PassDirtyReason::Light,2);
        sun.GetComponent<RenderLightComponent>().intensity=.75f;
        expect(PassDirtyReason::Light,1);
        sun.GetComponent<RenderLightComponent>().castShadows=false;
        auto inactive=run();Check(!inactive.decisions[0].requested && !inactive.decisions[0].executed,"disabled shadow defers execution");
        sun.GetComponent<RenderLightComponent>().castShadows=true;
        expect(PassDirtyReason::Light,1);
        targets.pointNear=.2f;
        expect(PassDirtyReason::ShadowSettings,2);
        float splits[]{.6f,1.f,2.f,5.f,20.f};targets.cascadeSplits=splits;
        expect(PassDirtyReason::ShadowSettings,1);
        body.GetComponent<MeshRendererComponent>().pickable=false;
        expect(PassDirtyReason::SceneMembership,4);
        body.GetComponent<MeshRendererComponent>().pickable=true;
        expect(PassDirtyReason::SceneMembership,4);
        body.GetComponent<MeshRendererComponent>().castShadows=false;
        expect(PassDirtyReason::SceneMembership,3);
        body.GetComponent<MeshRendererComponent>().castShadows=true;
        expect(PassDirtyReason::SceneMembership,3);
        body.GetComponent<VisibilityComponent>().layers=2;camera.visibleLayers=1;
        expect(PassDirtyReason::SceneMembership,4);
        camera.visibleLayers=~0u;
        expect(PassDirtyReason::Camera,4); // Directional fit ignores camera layers.
        body.GetComponent<VisibilityComponent>().enabled=false;
        expect(PassDirtyReason::SceneMembership,7);
        Check(Take(picking.ReadPixel(32,32),"empty picking clear")==-1,"empty dirty pass clears old image");
        body.GetComponent<VisibilityComponent>().enabled=true;
        expect(PassDirtyReason::SceneMembership,7);
        {
            auto cpu=Take(root.Shapes().ExportMesh("Box"),"replacement mesh export");
            auto gpu=Take(GpuMesh::Create(cpu),"replacement GPU mesh");
            auto publication=resources.Publication().BeginPublication();
            Check(resources.Meshes().Replace(publication,mesh,std::move(gpu)),"same-handle mesh publication");
        }
        expect(PassDirtyReason::Mesh,7);
        std::optional<MaterialInstance> changed;
        {
            auto access=resources.Publication().BeginFrame();
            changed.emplace(*Take(resources.Materials().Acquire(access,material),"material revision source"));
        }
        Check(changed->SetParameter("u_tiling",std::array<float,2>{2,2}),"edit material value");
        {
            auto publication=resources.Publication().BeginPublication();
            Check(resources.Materials().Replace(publication,material,std::move(*changed)),"same-handle material publication");
        }
        changed.reset();expect(PassDirtyReason::Material,7);
        // The fixture owns these registries through root. Mutate via their real
        // publication APIs to prove dependency revisions independent of material.
        TextureRegistry* images{};SamplerRegistry* samplers{};
        TextureHandle imageId;SamplerHandle samplerId;SamplerDesc samplerDesc;
        {
            auto access=resources.Publication().BeginFrame();auto bindings=resources.ForFrame(access).bindings;
            images=&const_cast<TextureRegistry&>(bindings.textures);samplers=&const_cast<SamplerRegistry&>(bindings.samplers);
            auto frame=Take(Extract(scene,resources,access,camera),"dependency revision sources");
            const auto& texture=frame.Resources()[0].Material().Textures()[0];
            imageId=texture.texture.Identity();samplerId=texture.sampler.Identity();samplerDesc=texture.sampler->Description();
        }
        {
            TextureDesc td;td.width=td.height=1;td.mips=TextureMipIntent::None;
            const std::byte pixel[]{std::byte{255},std::byte{255},std::byte{255},std::byte{255}};
            auto image=Take(TextureResource::Create(td,{pixel}),"texture replacement");
            auto publication=resources.Publication().BeginPublication();
            Check(images->Replace(publication,imageId,std::move(image)),"same-handle texture publication");
        }
        expect(PassDirtyReason::Material,7);
        {
            auto sampler=Take(GpuSampler::Create(samplerDesc),"sampler replacement");
            auto publication=resources.Publication().BeginPublication();
            Check(samplers->Replace(publication,samplerId,std::move(sampler)),"same-handle sampler publication");
        }
        expect(PassDirtyReason::Material,7);
        targets.pickingEnabled=false;
        body.GetComponent<Transform3DComponent>().Translation.y=.25f;
        auto deferred=expect(PassDirtyReason::Transform,3);
        Check(HasDirtyReason(deferred.decisions[2].reasons,PassDirtyReason::Transform) && !deferred.decisions[2].requested,"dirty picking is deferred without a readback request");
        run();targets.pickingEnabled=true;
        expect(PassDirtyReason::Transform,4);
        expect(PassDirtyReason::None,0);
        Check(picking.OnResize(64,64) && point.OnResize(256,256) && cascade.OnResize(256,256),"no-op target resizes");
        expect(PassDirtyReason::None,0);
        const auto identity=picking.Buffer().StorageIdentity();
        auto moved=std::move(picking);picking=std::move(moved);
        Check(picking.Buffer().StorageIdentity()==identity,"storage identity transfers with owner");
        expect(PassDirtyReason::None,0);
        Check(!picking.OnResize(9000,64) && picking.Buffer().StorageIdentity()==identity,"failed target resize preserves allocation identity");
        expect(PassDirtyReason::None,0);
        picking=Take(MousePickFrameBuffer::Create(64,64),"same-size picking recreation");
        expect(PassDirtyReason::TargetStorage,4);
        point=Take(PointShadowFrameBuffer::Create(256,256),"same-size point recreation");
        expect(PassDirtyReason::TargetStorage,2);
        cascade=Take(CascadeShadowFrameBuffer::Create(256,256,5),"same-size cascade recreation");
        expect(PassDirtyReason::TargetStorage,1);
        Check(picking.OnResize(0,0) && !picking.Buffer().StorageIdentity(),"zero-size storage invalidation");
        targets.pickingEnabled=false;run();
        Check(picking.OnResize(64,64),"restore deferred storage");targets.pickingEnabled=true;
        expect(PassDirtyReason::TargetStorage,4);
        Check(point.OnResize(128,128) && cascade.OnResize(128,128),"shadow storage resize");
        expect(PassDirtyReason::TargetStorage,3);
        body.GetComponent<Transform3DComponent>().Translation.z=.25f;camera.view[3][0]+=.1f;
        auto multiple=expect(PassDirtyReason::Transform,7);
        Check(HasDirtyReason(multiple.decisions[0].reasons,PassDirtyReason::Camera)
            && HasDirtyReason(multiple.decisions[2].reasons,PassDirtyReason::Camera),"multiple dirty reasons retained");
        Check(picking.ClearAttachment(0,-1),"external target overwrite");submitter.InvalidatePassContents();
        expect(PassDirtyReason::ExternalWrite,7);
        glBindBufferBase(GL_UNIFORM_BUFFER,0,0);
        const auto before=Pixels(desc.color);expect(PassDirtyReason::None,0);
        Check(Pixels(desc.color)==before,"cached cascade UBO rebound after external state changes");
        body.GetComponent<Transform3DComponent>().Translation.x+=.05f;
        {
            auto access=resources.Publication().BeginFrame();auto frame=Take(Extract(scene,resources,access,camera),"failed submission input");
            glEnable(0xffffffffu); // Pending driver error, consumed by production submission.
            auto failed=submitter.Submit(frame,targets,picks);
            Check(!failed,"injected driver failure is returned");
            const auto* meshError=std::get_if<GpuMeshError>(&failed.error().cause);
            Check(meshError && meshError->code==GpuMeshErrorCode::Driver,"submission preserves the originating mesh driver diagnostic");
        }
        expect(PassDirtyReason::RetryAfterFailure,7);expect(PassDirtyReason::None,0);
        const auto oldId=Take(scene.RenderData().Identify(body),"old entity lifetime");scene.DestroyEntity(body);
        body=scene.CreateEntity("new generation");body.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh,material});
        Check(Take(scene.RenderData().Identify(body),"new entity lifetime")!=oldId,"entity recreation changes generation");
        expect(PassDirtyReason::SceneMembership,7);
        const MaterialParameterDecl helperParameters[]{
            {"u_baseColor",MaterialParameterType::Float4,std::array<float,4>{1,1,1,1}},
            {"u_useVertexColor",MaterialParameterType::Boolean,true}};
        const auto helper=Take(resources.PublishMaterial({SceneMaterialKind::Helper,helperParameters,{},true,3}),"cached helper material");
        body.GetComponent<MeshRendererComponent>()={Take(resources.PublishShape("AxisHelper"),"cached helper mesh"),helper,0,false,false,true};
        run();
        auto pickImage=[&] {
            std::vector<int> result;
            for(int y=0;y<64;++y) for(int x=0;x<64;++x) result.push_back(Take(picking.ReadPixel(x,y),"helper pick image"));
            return result;
        };
        const auto helperImage=pickImage();
        glLineWidth(7);submitter.InvalidatePassContents();run();
        Check(pickImage()==helperImage,"picking establishes helper width independently of previous GL state");
        overrideRoles.assign(resources.Pipelines().begin(),resources.Pipelines().end());
        for(auto& role:overrideRoles) if(role.kind==SceneMaterialKind::Helper) role.lineWidth=7;
        expect(PassDirtyReason::Material,4);
        Check(pickImage()!=helperImage,"helper width edit invalidates and changes picking coverage");
        expect(PassDirtyReason::None,0);
        std::println("[PASS] pass-invalidation unchanged/revisions/targets/deferred/multiple/failure checks={}",checks);
    }
    void Run(EngineContext& root)
    {
        std::println("Image comparison: renderer={} vendor={} version={}; 64x64 RGBA8 linear target; camera eye=(0,2,8), target=(0,0,0), FOV=45deg, aspect=1, near=.1, far=20; same-driver RGB composition tolerance=2.5/255",
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),reinterpret_cast<const char*>(glGetString(GL_VENDOR)),reinterpret_cast<const char*>(glGetString(GL_VERSION)));
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
        // A click is consumed before BeginUI changes the mouse/viewport snapshot.
        // Exercise real integer pixels, this submission's table, and Entity Tags.
        {
            _Scene clickScene;
            const auto clickCamera=Camera(clickScene);
            auto a=clickScene.CreateEntity("Tag A"), b=clickScene.CreateEntity("Tag B");
            a.AddComponent<MeshRendererComponent>(MeshRendererComponent{box,material});
            b.AddComponent<MeshRendererComponent>(MeshRendererComponent{box,material});
            a.GetComponent<Transform3DComponent>().Translation={-2,0,0};
            b.GetComponent<Transform3DComponent>().Translation={2,0,0};
            const auto idA=Take(clickScene.RenderData().Identify(a),"pick A identity");
            const auto idB=Take(clickScene.RenderData().Identify(b),"pick B identity");
            EntityPickTable clickTable;
            RenderContext context{*root.MainWindow(),*root.LegacyEngine().GetWindowManager(),&target};
            FrameSceneInput input{clickScene,*resources,submitter,desc,clickTable,{&clickCamera,1}};
            struct Click {
                _Scene& scene; SceneRenderResources& resources;
                const MousePickFrameBuffer& target; EntityPickTable& table;
                ViewportPixelPosition position{}; EntityRenderId resolved{};
                std::string tag; int reads{},ui{},frame{}; bool fail=false;
            } click{clickScene,*resources,picking,clickTable};
            input.pickingReadback={&click,[](void* user)->ScheduleResult {
                auto& c=*static_cast<Click*>(user); ++c.reads;
                Check(ImGui::GetFrameCount()==c.frame,"click readback precedes BeginUI input advance");
                Check(GLContextThread::IsCurrentOwner(),"click readback stays on the owning context thread");
                Check(c.resources.Publication().CanPublish() && !c.scene.RenderData().IsExtracting(),
                    "click Entity/Tag lookup runs after extraction and frame access retire");
                if(c.fail) return std::unexpected(ScheduleError{FrameStage::Pass,ScheduleCode::InvalidInput});
                auto pixel=c.target.ReadPixel(c.position.X,c.position.Y);
                if(!pixel) return std::unexpected(ScheduleError{FrameStage::Pass,pixel.error()});
                c.resolved={}; c.tag="None";
                if(*pixel!=EntityPickTable::InvalidPixel) {
                    c.resolved=Take(c.scene.RenderData().ResolvePick(c.table,*pixel),"current-frame generation-safe click resolution");
                    auto entity=Take(c.scene.RenderData().Resolve(c.resolved),"current-frame clicked Entity");
                    c.tag=_Entity{entity,&c.scene}.GetName();
                }
                return {};
            }};
            context.editorUI={&click,[](void* user)->ScheduleResult {
                auto& c=*static_cast<Click*>(user); ++c.ui;
                Check(ImGui::GetFrameCount()==c.frame+1,"UI begins after picking readback");
                return {};
            }};
            auto request=[&](glm::vec3 world,EntityRenderId expected,const char* tag) {
                const auto clip=clickCamera.projection*clickCamera.view*glm::vec4(world,1);
                const auto ndc=glm::vec3(clip)/clip.w;
                // Offset logical panel and non-unit/nonuniform target scaling;
                // the public converter performs exactly one top-left -> GL Y flip.
                const float windowX=100+(ndc.x*.5f+.5f)*96;
                const float windowY=50+(1-(ndc.y*.5f+.5f))*48;
                auto pixel=ViewportPixelAt(windowX-100,windowY-50,{96,48},{64,64});
                Check(pixel.has_value(),"window -> local -> target picking coordinate");
                click.position=*pixel; click.frame=ImGui::GetFrameCount();
                input.targets.pickingEnabled=true;
                const auto before=click.reads;
                auto frame=Take(FrameScheduler::Render(context,&input),"scheduled click request");
                Check(click.reads==before+1,"scheduler fulfills each picking request exactly once");
                const auto& decision=frame.submission.decisions[2];
                Check(decision.requested && frame.submission.pickDraws==(decision.executed?2:0),"request is independent of dirty picking work");
                Check(std::any_of(frame.trace.Events().begin(),frame.trace.Events().end(),[](auto event) {
                    return event.stage==FrameStage::Pass && event.pass==RenderPass::Picking;
                })==decision.executed,"only executed picking appears in the pass trace");
                if(before>0) Check(!decision.executed && decision.reasons==PassDirtyReason::None,"repeated readbacks reuse unchanged picking image");
                Check(click.resolved==expected && click.tag==tag,"clicked Entity and Tag match, never the previous result");
                std::println("[PASS] scheduled click ({},{}) -> {}",pixel->X,pixel->Y,click.tag);
            };
            request({-2,0,0},idA,"Tag A");
            request({2,0,0},idB,"Tag B");
            request({0,3,0},{},"None");
            request({-2,0,0},idA,"Tag A");
            auto topLeft=ViewportPixelAt(0,0,{96,48},{64,64});
            auto bottomRight=ViewportPixelAt(95.9f,47.9f,{96,48},{64,64});
            Check(topLeft && topLeft->X==0 && topLeft->Y==63 && bottomRight && bottomRight->X==63 && bottomRight->Y==0,
                "picking coordinate corners preserve framebuffer Y orientation");
            Check(!ViewportPixelAt(-1,0,{96,48},{64,64}) && !ViewportPixelAt(96,0,{96,48},{64,64}),"outside panel does not request a pixel");
            const auto reads=click.reads;
            input.targets.pickingEnabled=false; click.frame=ImGui::GetFrameCount();
            auto skipped=Take(FrameScheduler::Render(context,&input),"no click request");
            Check(click.reads==reads && skipped.submission.pickDraws==0,"no readback or Picking work without a request");
            input.targets.pickingEnabled=true; context.visible=false; click.frame=ImGui::GetFrameCount();
            Take(FrameScheduler::Render(context,&input),"hidden viewport click");
            Check(click.reads==reads,"hidden viewport cannot consume an old picking image");
            context.visible=true; click.fail=true; click.frame=ImGui::GetFrameCount();
            const auto ui=click.ui;
            auto failed=FrameScheduler::Render(context,&input);
            Check(!failed && std::get<ScheduleCode>(failed.error().cause)==ScheduleCode::InvalidInput && click.ui==ui,
                "typed readback failure is returned before UI without leaking active frame state");
            click.fail=false;
            request({2,0,0},idB,"Tag B");
        }
        // Phase 46 exercises the production scheduler, not a stand-in order list.
        {
            _Scene scheduledScene;
            const auto scheduledCamera=Camera(scheduledScene);
            auto skyEntity=scheduledScene.CreateEntity("scheduled sky");
            skyEntity.AddComponent<MeshRendererComponent>(MeshRendererComponent{skyMesh,skyMaterial,0,false,false,false});
            auto surface=scheduledScene.CreateEntity("scheduled surface");
            const auto scheduledOpaque=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures}),"matched opaque fixture");
            surface.AddComponent<MeshRendererComponent>(MeshRendererComponent{sphere,scheduledOpaque});
            surface.AddComponent<VisibilityComponent>(VisibilityComponent{false});
            EntityPickTable scheduledPicks;
            RenderContext context{*root.MainWindow(),*root.LegacyEngine().GetWindowManager(),&target};
            FrameSceneInput input{scheduledScene,*resources,submitter,desc,scheduledPicks,{&scheduledCamera,1}};
            struct Callbacks { SceneRenderResources* resources; _Scene* scene; int updates{},ui{}; bool fail=false; } callbacks{resources.get(),&scheduledScene};
            context.updateResources={&callbacks,[](void* user)->ScheduleResult {
                auto& c=*static_cast<Callbacks*>(user); ++c.updates;
                Check(c.resources->Publication().CanPublish() && !c.scene->RenderData().IsExtracting(),"update runs before resource/ECS freeze");
                if(c.fail) return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources,ScheduleCode::InvalidInput});
                return {};
            }};
            context.editorUI={&callbacks,[](void* user)->ScheduleResult {
                auto& c=*static_cast<Callbacks*>(user); ++c.ui;
                Check(c.resources->Publication().CanPublish() && !c.scene->RenderData().IsExtracting(),"UI starts after frame access and ECS freeze release");
                Check(!glIsEnabled(GL_SCISSOR_TEST) && !glIsEnabled(GL_STENCIL_TEST) && !glIsEnabled(GL_RASTERIZER_DISCARD),"external UI receives established state");
                return {};
            }};
            auto scheduled=Take(FrameScheduler::Render(context,&input),"scheduled sky frame");
            Check(callbacks.updates==1 && callbacks.ui==1,"one update and UI callback");
            auto hasPass=[](const FrameTrace& trace,RenderPass pass) {
                return std::any_of(trace.Events().begin(),trace.Events().end(),[&](auto event){return event.stage==FrameStage::Pass && event.pass==pass;});
            };
            Check(scheduled.trace.events[0].stage==FrameStage::BeginFrame
                && scheduled.trace.events[1].stage==FrameStage::UpdateFrameResources
                && scheduled.trace.events[2].stage==FrameStage::FreezeFrameInputs
                && scheduled.trace.events[3].stage==FrameStage::BuildRenderFrame,"recorded frame stages");
            const RenderPass ordered[]{RenderPass::Picking,RenderPass::Opaque,RenderPass::Masked,RenderPass::Skybox,
                RenderPass::Transparent,RenderPass::Debug,RenderPass::Resolve,RenderPass::EditorUI,RenderPass::Present};
            Check(scheduled.trace.count==std::size(ordered)+4,"recorded active pass count");
            for(std::size_t i=0;i<std::size(ordered);++i) Check(scheduled.trace.events[i+4].pass==ordered[i],"recorded renderer-owned order");
            auto background=Pixels(target);
            surface.GetComponent<VisibilityComponent>().enabled=true;
            auto opaque=Take(FrameScheduler::Render(context,&input),"opaque over sky");
            auto solid=Pixels(target);
            Check(opaque.submission.opaqueDraws==1 && opaque.submission.skyDraws==1,"opaque category and sky");
            // Publish a new material during UpdateFrameResources. This grows the
            // pipeline role vector and must be visible in this very frame.
            struct Publish { SceneRenderResources* resources; _Entity* entity; std::span<const MaterialParameterDecl> parameters;
                std::span<const MaterialTextureAssignment> textures; MaterialInstanceHandle material; } publish{resources.get(),&surface,parameters,textures};
            context.updateResources={&publish,[](void* user)->ScheduleResult {
                auto& p=*static_cast<Publish*>(user);
                auto created=p.resources->PublishMaterial({SceneMaterialKind::Lit,p.parameters,p.textures,false,1,AlphaMode::Transparent,.5f,.5f});
                if(!created) return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources,created.error()});
                p.material=*created;
                p.entity->GetComponent<MeshRendererComponent>().material=*created;
                return {};
            }};
            auto alpha=Take(FrameScheduler::Render(context,&input),"same-frame synchronous material publication");
            Check(alpha.submission.transparentDraws==1 && alpha.submission.opaqueDraws==0,"new replacement visible before extraction");
            Check(alpha.submission.decisions[2].executed && HasDirtyReason(alpha.submission.decisions[2].reasons,PassDirtyReason::Material),
                "new material/template/pipeline/program invalidates picking");
            auto composed=Pixels(target);
            const auto saveImage=[](const char* path,const std::vector<std::byte>& pixels) {
                std::ofstream output(path,std::ios::binary); output << "P6\n64 64\n255\n";
                for(int y=63;y>=0;--y) for(int x=0;x<64;++x) output.write(reinterpret_cast<const char*>(pixels.data()+(y*64+x)*4),3);
                Check(bool(output),"write alpha comparison evidence");
            };
            saveImage("sky-background.ppm",background);saveImage("opaque-over-sky.ppm",solid);saveImage("transparent-over-sky.ppm",composed);
            int blendedPixels{};
            for(std::size_t i=0;i<solid.size();i+=4) {
                bool differs=false;
                for(std::size_t c=0;c<3;++c) if(std::abs(int(solid[i+c])-int(background[i+c]))>12) differs=true;
                if(!differs) continue;
                ++blendedPixels;
                for(std::size_t c=0;c<3;++c)
                    Check(std::abs(int(composed[i+c])-(int(solid[i+c])+int(background[i+c]))*.5f)<=2.5f,"transparent fragment composes over established sky");
            }
            Check(blendedPixels>20,"transparent-over-sky fixture covers visible fragments");
            context.updateResources={&callbacks,[](void* user)->ScheduleResult {
                auto& c=*static_cast<Callbacks*>(user); ++c.updates;
                Check(c.resources->Publication().CanPublish(),"publication safe point remains available");
                if(c.fail) return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources,ScheduleCode::InvalidInput});
                return {};
            }};
            input.extraction.tasks.workers=2; input.extraction.parallelThreshold=0;
            auto parallel=Take(FrameScheduler::Render(context,&input),"scheduler optional frozen parallel extraction");
            Check(parallel.extraction.tasks.lanes==2 && Pixels(target)==composed,"serial/parallel scheduler frame parity");
            input.extraction={};
            // Previous external clients may leave hostile state. Every pass must
            // establish its declared state before clears or draws.
            glEnable(GL_SCISSOR_TEST);glScissor(0,0,0,0);glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_NEVER,0,~0u);glDepthMask(GL_FALSE);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
            glEnable(GL_BLEND);glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);glCullFace(GL_FRONT);glFrontFace(GL_CW);
            glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);glEnable(GL_RASTERIZER_DISCARD);
            glDepthRange(1,0);glClearDepth(0);glEnable(GL_SAMPLE_COVERAGE);glSampleCoverage(0,GL_FALSE);
            Take(FrameScheduler::Render(context,&input),"state contamination recovery");
            Check(Pixels(target)==composed,"pass state independent of preceding external client");
            const auto masked=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,AlphaMode::Masked,.5f,.25f}),"masked material");
            surface.GetComponent<MeshRendererComponent>().material=masked;
            auto maskFrame=Take(FrameScheduler::Render(context,&input),"masked color and picking");
            Check(maskFrame.submission.maskedDraws==1 && Pixels(target)==background,"masked category discards below-cutoff color");
            for(int y=0;y<64;++y) for(int x=0;x<64;++x) Check(Take(picking.ReadPixel(x,y),"masked picking read")==-1,"masked picking discards same coverage");
            auto sun=scheduledScene.CreateEntity("scheduled directional");
            sun.AddComponent<RenderLightComponent>(directional);
            sun.GetComponent<Transform3DComponent>().QuatRotation=glm::rotation(glm::vec3(0,0,-1),-glm::normalize(glm::vec3(20,50,20)));
            auto bulb=scheduledScene.CreateEntity("scheduled point");bulb.AddComponent<RenderLightComponent>(pointLight);
            bulb.GetComponent<Transform3DComponent>().Translation={0,3,2};
            auto shadowed=Take(FrameScheduler::Render(context,&input),"masked directional and point passes");
            Check(shadowed.trace.events[4].pass==RenderPass::DirectionalShadow && shadowed.trace.events[5].pass==RenderPass::PointShadow,"directional precedes point shadow");
            std::vector<float> depth(256*256*cascadeTarget.Buffer().Description().Layers);
            Check(TextureView(Take(cascadeTarget.DepthView(),"cascade view")).Bind(0),"cascade readback bind");
            glGetTexImage(GL_TEXTURE_2D_ARRAY,0,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
            Check(std::all_of(depth.begin(),depth.end(),[](float v){return v==1.f;}),"masked cascade coverage matches color");
            const auto pointDepthIsClear=[&] {
                Check(TextureView(Take(pointTarget.DepthView(),"point view")).Bind(0),"point readback bind");
                GLint cube{},previousRead{};glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&cube);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead);
                GLuint reader{};glGenFramebuffers(1,&reader);glBindFramebuffer(GL_READ_FRAMEBUFFER,reader);
                depth.resize(256*256);
                bool allClear=true;
                // Read each attached face as depth. Raw cube texture downloads
                // on this maintained driver returned undefined data for a cleared
                // face; attachment readback covers the actual depth image.
                for(int face=0;face<6;++face) {
                    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,cube,0);
                    glReadBuffer(GL_NONE);
                    Check(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"point face reader complete");
                    std::fill(depth.begin(),depth.end(),-5.f);
                    glReadPixels(0,0,256,256,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
                    Check(glGetError()==GL_NO_ERROR,"point depth readback has no driver error");
                    allClear &= std::all_of(depth.begin(),depth.end(),[](float v){return v==1.f;});
                }
                glBindFramebuffer(GL_READ_FRAMEBUFFER,previousRead);glDeleteFramebuffers(1,&reader);
                return allClear;
            };
            Check(pointDepthIsClear(),"masked point coverage matches color on all six faces");
            const auto visibleMasked=Take(resources->PublishMaterial({SceneMaterialKind::Lit,parameters,textures,false,1,AlphaMode::Masked,.5f,.75f}),"visible mask positive control");
            surface.GetComponent<MeshRendererComponent>().material=visibleMasked;
            Take(FrameScheduler::Render(context,&input),"above-cutoff masked frame");
            Check(!pointDepthIsClear(),"above-cutoff positive control writes point depth");
            Check(Pixels(target)!=background,"above-cutoff masked color is visible");
            // Required targets fail before any pass, preserving the last image.
            auto smallPick=Take(MousePickFrameBuffer::Create(16,16),"incompatible pick target");
            FrameSubmissionDesc bad{target,smallPick,pointTarget,cascadeTarget,resources->Pipelines(),splits,glm::radians(45.f),1,.1f,20,.1f,100};
            FrameSceneInput badInput{scheduledScene,*resources,submitter,bad,scheduledPicks,{&scheduledCamera,1}};
            auto beforeFailure=Pixels(target);
            auto failed=FrameScheduler::Render(context,&badInput);
            Check(!failed && std::get<SubmissionCode>(std::get<SubmissionError>(failed.error().cause).cause)==SubmissionCode::InvalidTarget,"required picking target failure is typed");
            Check(Pixels(target)==beforeFailure && resources->Publication().CanPublish() && !scheduledScene.RenderData().IsExtracting(),"failed frame releases freeze and preserves image");
            auto missingShadow=Take(CascadeShadowFrameBuffer::Create(0,0,5),"deferred shadow target");
            FrameSubmissionDesc noShadow{target,picking,pointTarget,missingShadow,resources->Pipelines(),splits,glm::radians(45.f),1,.1f,20,.1f,100};
            FrameSceneInput noShadowInput{scheduledScene,*resources,submitter,noShadow,scheduledPicks,{&scheduledCamera,1}};
            auto shadowFailure=FrameScheduler::Render(context,&noShadowInput);
            Check(!shadowFailure && std::get<SubmissionCode>(std::get<SubmissionError>(shadowFailure.error().cause).cause)==SubmissionCode::InvalidTarget,"active directional shadow requires a live target");
            badInput.targets.pickingEnabled=false;
            auto skipped=Take(FrameScheduler::Render(context,&badInput),"optional picking skips unusable target");
            Check(!hasPass(skipped.trace,RenderPass::Picking) && skipped.submission.pickDraws==0,"optional pass omitted from actual order");
            Check(!scheduledScene.RenderData().ResolvePick(scheduledPicks,0),"skipped picking invalidates old lookup");
            callbacks.fail=true; const int priorUI=callbacks.ui;
            auto updateFailure=FrameScheduler::Render(context,&input);
            Check(!updateFailure && updateFailure.error().stage==FrameStage::UpdateFrameResources && callbacks.ui==priorUI,"update failure stops before extraction/UI/present");
            callbacks.fail=false;
            {
                auto held=resources->Publication().BeginFrame();
                const auto priorUpdates=callbacks.updates;
                auto busy=FrameScheduler::Render(context,&input);
                Check(!busy && std::get<ScheduleCode>(busy.error().cause)==ScheduleCode::FrameActive
                    && callbacks.updates==priorUpdates,"active resource frame rejects before update callback");
            }
            auto updateCallback=context.updateResources;
            context.updateResources={&context,[](void* user)->ScheduleResult {
                auto nested=FrameScheduler::Render(*static_cast<RenderContext*>(user));
                Check(!nested && std::get<ScheduleCode>(nested.error().cause)==ScheduleCode::FrameActive,"reentrant scheduler returns typed error");
                return {};
            }};
            Take(FrameScheduler::Render(context,&input),"outer frame survives rejected reentry");
            context.updateResources=updateCallback;
            auto invalidCamera=scheduledCamera; invalidCamera.viewportWidth=32;
            auto oldCameras=input.cameras; input.cameras={&invalidCamera,1};
            auto cameraFailure=FrameScheduler::Render(context,&input);
            Check(!cameraFailure && resources->Publication().CanPublish(),"invalid viewport returns typed failure and releases frame");
            input.cameras=oldCameras;
            bool threadRejected=false;
            std::thread wrongThread([&] { auto result=FrameScheduler::Render(context,&input);
                threadRejected=!result && std::get<ScheduleCode>(result.error().cause)==ScheduleCode::Context; });
            wrongThread.join(); Check(threadRejected,"scheduler rejects worker without issuing GL");
            context.visible=false;
            auto hidden=Take(FrameScheduler::Render(context,&input),"collapsed viewport preserves UI lifecycle");
            Check(!hasPass(hidden.trace,RenderPass::Opaque) && hasPass(hidden.trace,RenderPass::EditorUI) && hasPass(hidden.trace,RenderPass::Present),"collapsed optional scene skip");
            context.visible=true;
            scheduledScene.DestroyEntity(surface);scheduledScene.DestroyEntity(skyEntity);
            scheduledScene.DestroyEntity(sun);scheduledScene.DestroyEntity(bulb);
            input.targets.pickingEnabled=false;
            auto empty=Take(FrameScheduler::Render(context,&input),"empty scene scheduler");
            Check(empty.extraction.draws==0 && empty.submission.colorDraws==0 && empty.submission.shadowDraws==0
                && !hasPass(empty.trace,RenderPass::DirectionalShadow) && !hasPass(empty.trace,RenderPass::PointShadow),"empty scene clears and skips optional shadow work");
            Take(FrameScheduler::Render(context,&noShadowInput),"inactive shadow needs no live target");
            Check(target.SetSamples(4),"multisample target configuration");
            auto resolved=Take(FrameScheduler::Render(context,&input),"scheduled multisample resolve");
            Check(hasPass(resolved.trace,RenderPass::Resolve) && Pixels(target).size()==64*64*4,"resolved output is readable after scheduled resolve");
            Check(target.SetSamples(1),"restore single sample target");
            for(const auto& event:shadowed.trace.Events()) if(event.stage==FrameStage::Pass) {
                Check(!event.contract.scissor && !event.contract.stencilWrite,"declared scissor and stencil policy");
                if(event.pass==RenderPass::Transparent) Check(event.contract.blend && !event.contract.depthWrite,"transparent state declaration");
                if(event.pass==RenderPass::Skybox) Check(!event.contract.depthWrite,"sky retains scene depth");
            }
            Check(root.MainWindow()->IsCurrent(),"scheduler restores owning context");
            std::println("[PASS] frame-scheduler order/skip/targets/publication/freeze/failure/alpha/state/empty checks={}",checks);
        }
        Invalidation(root,*resources,desc,box,material);
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
