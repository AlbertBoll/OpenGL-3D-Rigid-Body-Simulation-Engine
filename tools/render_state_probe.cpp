#include "Scene/_Entity.h"
#include "Core/RenderTarget.h"
#include "Core/GLContextThread.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/ShapeSphere.h"
#include <glad/glad.h>
#include <sdl2/SDL.h>
#include <cstdlib>
#include <print>

namespace
{
    using namespace GEngine;
    using namespace GEngine::Asset;
    using namespace GEngine::Component;
    int checks{};
    template<class T> void Check(const T& value, const char* message)
    { ++checks; if (!static_cast<bool>(value)) { std::println(stderr, "[FAIL] {}", message); std::exit(1); } }
    const EntityRenderState& Find(const SceneRenderState& state, EntityRenderId id)
    {
        for (const auto& entry : state.entities) if (entry.entity == id) return entry;
        Check(false, "Missing entity state"); std::abort();
    }
    void CheckBounds()
    {
        LocalBounds local; local.empty = false; local.minimum = {-2,-3,-4}; local.maximum = {5,6,7};
        for (int i=0; i<120; ++i)
        {
            auto matrix = glm::translate(Mat4(1), Vec3f(10,-20,30))
                * glm::toMat4(glm::angleAxis(float(i)*.07f, glm::normalize(Vec3f(1,2,3))))
                * glm::scale(Mat4(1), Vec3f(-2,.5f,7))
                * glm::toMat4(glm::angleAxis(float(i)*.13f, glm::normalize(Vec3f(3,2,1))));
            const auto bounds = TransformBounds(local, matrix);
            Check(bounds.CanCull(), "Rotation/non-uniform scale/shear bounds valid");
            for (int corner=0; corner<8; ++corner)
            {
                glm::dvec4 point(0,0,0,1);
                for (int axis=0;axis<3;++axis) point[axis] = (corner & (1<<axis)) ? local.maximum[axis] : local.minimum[axis];
                point = glm::dmat4(matrix)*point;
                double squared=0;
                for (int axis=0;axis<3;++axis)
                {
                    Check(point[axis]>=bounds.minimum[axis] && point[axis]<=bounds.maximum[axis], "AABB contains transformed corner");
                    squared += std::pow(point[axis]-bounds.sphereCenter[axis],2);
                }
                Check(std::sqrt(squared)<=bounds.sphereRadius, "Sphere contains transformed corner under shear");
            }
        }
        auto invalid=local; invalid.minimum[0]=std::numeric_limits<double>::quiet_NaN();
        Check(TransformBounds(invalid,Mat4(1)).status==BoundsStatus::Invalid, "NaN local bounds fallback");
        invalid=local; invalid.maximum[1]=-10;
        Check(!TransformBounds(invalid,Mat4(1)).CanCull(), "Inverted bounds never cull");
        auto projective=Mat4(1); projective[0][3]=1;
        Check(!TransformBounds(local,projective).CanCull(), "Non-affine bounds never cull");
        invalid=local; invalid.minimum[0]=-1e308; invalid.maximum[0]=1e308;
        Check(!TransformBounds(invalid,glm::scale(Mat4(1),Vec3f(100))).CanCull(), "Overflow bounds fallback");
        Check(TransformBounds({},Mat4(1)).status==BoundsStatus::Empty, "Known empty distinct from unavailable");
        std::println("[PASS] affine bounds, conservative spheres and invalid fallback");
    }
    struct Vertex { float x,y,z; };
    constexpr VertexAttribute attributes[]{{VertexSemantic::Position,0,VertexScalarFormat::Float32,3,VertexInterpretation::Floating,0}};
    MeshAsset MeshSource(float extent=1, MeshUpdateIntent intent=MeshUpdateIntent::Static)
    {
        const Vertex vertices[]{{-extent,-extent,-extent},{extent,extent,extent},{0,0,0}};
        const SubmeshRange ranges[]{{0,3,0}};
        auto source=MeshSourceData::FromVertices<Vertex>(vertices,attributes); source.submeshes=ranges; source.updateIntent=intent;
        auto result=MeshAsset::Create(source); Check(result,"CPU mesh"); return std::move(*result);
    }
    ShaderProgram Program()
    {
        const ShaderSource sources[]{{VERTEX,"#version 460 core\nvoid main(){gl_Position=vec4(0,0,0,1);}","bounds vertex"},
            {FRAGMENT,"#version 460 core\nlayout(location=0) out vec4 color;void main(){color=vec4(1);}","bounds fragment"}};
        auto program=ShaderProgram::Create({sources}); Check(program,"Shader program"); return std::move(*program);
    }
    TextureResource Image()
    {
        TextureDesc desc; desc.width=desc.height=1; desc.mips=TextureMipIntent::None;
        const std::array<std::byte,4> pixels{std::byte{20},std::byte{40},std::byte{60},std::byte{255}};
        return TextureResource::Create(desc,{pixels}).value();
    }
    struct Fixture
    {
        AssetPublication publication;
        ShaderProgramRegistry programs{publication};
        TextureRegistry textures{publication};
        SamplerRegistry samplers{publication};
        PipelineRegistry pipelines{publication};
        MaterialTemplateRegistry templates{publication};
        MaterialInstanceRegistry materials{publication};
        MeshRegistry meshes{publication};
        AssetRegistry<RenderTargetHandle, RenderTarget> targets{publication};
        _Scene scene;
        MeshHandle mesh;
        MaterialInstanceHandle material;
        RenderTargetHandle target;
        ShaderProgramHandle program;
        TextureHandle texture;
        SamplerHandle sampler;
        MaterialTemplateView declaration;
        Fixture()
        {
            PipelineHandle pipeline;
            { auto p=publication.BeginPublication();
                program=programs.Create(p,Program()).value();
                texture=textures.Create(p,Image()).value(); sampler=samplers.Create(p,GpuSampler::Create({}).value()).value();
                PipelineDesc desc; desc.program=program; desc.programRevision=1;
                pipeline=pipelines.Create(p,PipelineState::Create(desc).value()).value();
                mesh=PublishMesh(meshes,p,MeshSource()).value();
                target=targets.Create(p,RenderTarget::Create(16,16,1).value()).value(); }
            PipelineView pipelineView;
            { auto f=publication.BeginFrame(); pipelineView=pipelines.Acquire(f,pipeline).value(); }
            const MaterialParameterDecl parameters[]{{"roughness",MaterialParameterType::Float,.5f}};
            const MaterialTextureSlotDecl slots[]{{"image",true,MaterialTextureValue{texture,sampler}}};
            auto definition=MaterialTemplate::Create({pipelineView,parameters,slots}).value();
            MaterialTemplateHandle templateId;
            { auto p=publication.BeginPublication(); templateId=templates.Create(p,std::move(definition)).value(); }
            { auto f=publication.BeginFrame(); declaration=templates.Acquire(f,templateId).value(); }
            { auto p=publication.BeginPublication(); material=materials.Create(p,MaterialInstance::Create(declaration).value()).value(); }
        }
        RenderTargetRevision Target(const AssetPublication::FrameAccess& frame)
        { auto view=targets.Acquire(frame,target).value(); return CaptureRenderTargetRevision(view.Identity(),view.Revision(),*view); }
        SceneRenderState Sample()
        {
            auto f=publication.BeginFrame();
            auto result=scene.UpdateRenderState({f,meshes,materials,{programs,textures,samplers},Target(f)});
            Check(result,"Scene snapshot"); return std::move(*result);
        }
        std::pair<_Entity,EntityRenderId> Entity()
        {
            auto entity=scene.CreateEntity(); auto id=scene.RenderData().Identify(entity).value();
            Check(scene.RenderData().Add(id,MeshRendererComponent{mesh,material}),"Add mesh component");
            return {entity,id};
        }
    };
    void Revisions()
    {
        Fixture f;
        auto [entity,id]=f.Entity();
        Check(f.scene.RenderData().Add(id,RenderLightComponent{}),"Add light");
        Check(f.scene.RenderData().Add(id,RenderCameraComponent{}),"Add camera");
        auto first=f.Sample();
        Check(Find(first,id).bounds.CanCull() && Find(first,id).material,"Ready resources resolved");
        Check(f.Sample().revisions==first.revisions,"Unchanged revisions");
        Check(f.scene.RenderData().Replace(id,RenderLightComponent{}) && f.scene.RenderData().Replace(id,RenderCameraComponent{}),"No-op authoring");
        Check(f.scene.RenderData().Replace(id,MeshRendererComponent{f.mesh,f.material}),"No-op mesh intent");
        entity.Transform().Translation={0,0,0};
        Check(f.Sample().revisions==first.revisions,"No-op components and TRS stable");
        auto light=RenderLightComponent{}; light.intensity=2;
        Check(f.scene.RenderData().Replace(id,light),"Light mutation");
        auto changed=f.Sample();
        Check(changed.revisions.light>first.revisions.light && changed.revisions.camera==first.revisions.camera
            && changed.revisions.transform==first.revisions.transform,"Light cause isolated");
        auto camera=RenderCameraComponent{}; camera.nearPlane=.25f;
        Check(f.scene.RenderData().Replace(id,camera),"Camera mutation");
        auto before=changed.revisions; changed=f.Sample();
        Check(changed.revisions.camera>before.camera && changed.revisions.light==before.light,"Camera cause isolated");
        auto edited=MaterialInstance::Create(f.declaration).value(); Check(edited.SetParameter("roughness",.7f),"Material edit");
        { auto p=f.publication.BeginPublication(); Check(f.materials.Replace(p,f.material,edited),"Publish material"); }
        before=changed.revisions; changed=f.Sample();
        Check(changed.revisions.material>before.material && changed.revisions.mesh==before.mesh,"Material publication cause");
        const auto authored=edited.Revision(); Check(edited.SetParameter("roughness",.7f) && edited.Revision()==authored,"Material no-op source");
        Check(f.Sample().revisions==changed.revisions,"No-op material skips publication");
        { auto p=f.publication.BeginPublication(); Check(f.textures.Replace(p,f.texture,Image()),"Ready texture replacement"); }
        before=changed.revisions; changed=f.Sample();
        Check(changed.revisions.material>before.material && changed.revisions.transform==before.transform,"Texture replacement invalidates dependent material");
        { auto p=f.publication.BeginPublication(); Check(f.samplers.Replace(p,f.sampler,GpuSampler::Create({}).value()),"Ready sampler replacement"); }
        before=changed.revisions; changed=f.Sample();
        Check(changed.revisions.material>before.material,"Sampler replacement invalidates dependent material");
        auto* currentWindow=SDL_GL_GetCurrentWindow(); auto currentContext=SDL_GL_GetCurrentContext();
        Check(SDL_GL_MakeCurrent(currentWindow,nullptr)==0,"Detach context for state sampling");
        Check(f.Sample().revisions==changed.revisions,"Bounds and resource resolution issue no GL");
        Check(SDL_GL_MakeCurrent(currentWindow,currentContext)==0,"Restore context");
        { auto p=f.publication.BeginPublication(); Check(f.meshes.Replace(p,f.mesh,GpuMesh::Create(MeshSource(4)).value()),"Mesh ready replacement"); }
        before=changed.revisions; changed=f.Sample();
        Check(changed.revisions.mesh>before.mesh && changed.revisions.bounds>before.bounds && changed.revisions.transform==before.transform,"Replacement invalidates without transform write");
        Check(Find(changed,id).bounds.maximum[0]>=4 && Find(first,id).bounds.maximum[0]<2
            && Find(first,id).mesh.Revision()==1,"Old snapshot retains old mesh and bounds");
        auto parent=f.scene.CreateEntity(); parent.Transform().Scale={2,.5f,3}; parent.Transform().SetRotation(Vec3f(.2f,.4f,.3f));
        Check(entity.SetParent(parent),"Parent mesh"); entity.Transform().Translation={1,2,3}; entity.Transform().Scale={-1,3,2};
        changed=f.Sample();
        Check(Find(changed,id).bounds.CanCull(),"Hierarchical presentation bounds");
        Check(f.Sample().revisions==changed.revisions,"Hierarchy no-op revisions");
        auto stale=f.scene.RenderData().Get<MeshRendererComponent>(id).value(); stale.mesh={55,66,77};
        Check(f.scene.RenderData().Replace(id,stale),"Unavailable mesh reference");
        changed=f.Sample(); Check(Find(changed,id).bounds.status==BoundsStatus::Unavailable && Find(changed,id).meshError,"Missing mesh visible with typed diagnostic");
        stale.mesh=f.mesh; Check(f.scene.RenderData().Replace(id,stale),"Mesh recovery");
        changed=f.Sample(); Check(Find(changed,id).bounds.CanCull(),"Bounds recover after readiness");
        MeshHandle pending;
        { auto p=f.publication.BeginPublication(); pending=f.meshes.Create(p,GpuMesh{}).value(); }
        stale.mesh=pending; Check(f.scene.RenderData().Replace(id,stale),"Pending mesh handle");
        changed=f.Sample(); Check(Find(changed,id).bounds.status==BoundsStatus::Unavailable,"Unready owner is not a known empty mesh");
        { auto p=f.publication.BeginPublication(); Check(f.meshes.Replace(p,pending,GpuMesh::Create(MeshSource(2)).value()),"Publish pending mesh ready"); }
        before=changed.revisions; changed=f.Sample();
        Check(Find(changed,id).bounds.CanCull() && changed.revisions.mesh>before.mesh && changed.revisions.bounds>before.bounds,"Same handle readiness invalidates bounds");
        // Failed scene evaluation must leave all render counters untouched.
        before=changed.revisions; light.intensity=std::numeric_limits<float>::quiet_NaN();
        Check(f.scene.RenderData().Replace(id,light),"Invalid light input");
        { auto frame=f.publication.BeginFrame(); auto bad=f.scene.UpdateRenderState({frame,f.meshes,f.materials,{f.programs,f.textures,f.samplers},f.Target(frame)});
          Check(!bad && bad.error().code==TransformErrorCode::NonFiniteRenderData,"Typed finite-data error"); }
        light.intensity=2; Check(f.scene.RenderData().Replace(id,light),"Restore light");
        Check(f.Sample().revisions==before,"Failed update did not publish revisions");
        const auto translation=entity.Transform().Translation;
        entity.Transform().Translation.x=std::numeric_limits<float>::infinity();
        { auto frame=f.publication.BeginFrame(); auto bad=f.scene.UpdateRenderState({frame,f.meshes,f.materials,{f.programs,f.textures,f.samplers},f.Target(frame)});
          Check(!bad && bad.error().code==TransformErrorCode::NonFiniteTransform,"Typed transform failure"); }
        entity.Transform().Translation=translation;
        Check(f.Sample().revisions==before,"Failed transform leaves published render counters stable");
        { auto p=f.publication.BeginPublication(); Check(f.targets.Replace(p,f.target,RenderTarget::Create(32,16,1).value()),"Target replacement"); }
        before=changed.revisions; changed=f.Sample();
        Check(changed.revisions.target>before.target && changed.revisions.camera==before.camera,"Target source distinct from camera");
        Check(f.Sample().revisions==changed.revisions,"Unchanged target stable");
        {
            auto target=RenderTarget::Create(16,16,1).value();
            const auto source=CaptureRenderTargetRevision(f.target,1,target);
            Check(target.OnResize(16,16),"No-op target resize");
            Check(CaptureRenderTargetRevision(f.target,1,target)==source,"No-op target source stable");
            Check(target.OnResize(20,16),"Effective target resize");
            Check(CaptureRenderTargetRevision(f.target,1,target).storage>source.storage,"Storage revision source advances");
        }
        { auto p=f.publication.BeginPublication(); Check(f.programs.Replace(p,f.program,Program()),"Program replacement"); }
        before=changed.revisions; changed=f.Sample();
        Check(changed.revisions.material>before.material && Find(changed,id).materialError
            && Find(changed,id).materialError->code==MaterialBindingCode::ProgramRevisionMismatch,"Program replacement has structured invalidation diagnosis");
        Check(Find(first,id).material->Program().Revision()==1,"Old snapshot keeps linked program version");
        before=changed.revisions; f.scene.DestroyEntity(entity); changed=f.Sample();
        Check(changed.revisions.mesh>before.mesh && changed.revisions.light>before.light && changed.revisions.camera>before.camera,"Removal propagates categories");
        auto [replacement,replacementId]=f.Entity(); (void)replacement;
        Check(replacementId!=id && f.Sample().entities.size()==2,"Slot reuse has fresh identity");
        auto copied=_Scene::Copy(RefPtr<_Scene>(&f.scene,[](_Scene*){}));
        auto frame=f.publication.BeginFrame(); auto snapshot=copied->UpdateRenderState({frame,f.meshes,f.materials,{f.programs,f.textures,f.samplers},{}});
        Check(snapshot && snapshot->revisions.scene==1,"Copy does not inherit runtime caches");
        std::println("[PASS] revision causes, no-op writes, replacement, retention, failure and identity");
    }
    void Interpolation()
    {
        Fixture f;
        // Borrowed physics shapes outlive the scene's runtime body cleanup below.
        auto [entity,id]=f.Entity();
        entity.AddComponent<RigidBody3DComponent>().Type=BodyType::Kinematic;
        entity.AddComponent<SphereFixture3DComponent>().Radius=.1f;
        auto [child,childId]=f.Entity(); child.Transform().Translation={2,0,0}; Check(child.SetParent(entity),"Presentation child");
        f.scene.OnRuntimeStart();
        auto* body=entity.GetComponent<RigidBody3DComponent>().RuntimeBody;
        std::unique_ptr<PhysicalShape> shape(body->m_Shape); body->m_LinearVelocity={6,0,0};
        f.scene.Update(Timestep(_Scene::PhysicsStepSeconds)); auto zero=f.Sample();
        const auto pose=entity.Transform().GetTransform(); const auto ticks=f.scene.GetPhysicsTiming().totalSteps;
        f.scene.Update(Timestep(_Scene::PhysicsStepSeconds*.5)); auto half=f.Sample();
        Check(f.scene.GetPhysicsTiming().totalSteps==ticks && entity.Transform().GetTransform()==pose,"No new tick or authoritative pose change");
        Check(std::abs(Find(half,id).world[3].x-.05f)<1e-6f && std::abs(Find(half,childId).world[3].x-2.05f)<1e-6f,"Interpolation propagates through hierarchy");
        Check(Find(half,id).revisions.transform>Find(zero,id).revisions.transform
            && Find(half,childId).revisions.bounds>Find(zero,childId).revisions.bounds
            && Find(half,childId).bounds.minimum[0]>Find(zero,childId).bounds.minimum[0],"Tickless movement invalidates descendant bounds");
        Check(f.Sample().revisions==half.revisions,"Same alpha is no-op");
        f.scene.OnRuntimeStop();
        std::println("[PASS] tickless interpolated bounds without simulation mutation");
    }
    void DynamicBounds()
    {
        AssetPublication publication; MeshRegistry meshes(publication);
        MeshHandle handle;
        { auto p=publication.BeginPublication(); handle=PublishMesh(meshes,p,MeshSource(1,MeshUpdateIntent::Dynamic)).value(); }
        const Vertex corner{1,1,1};
        { auto p=publication.BeginPublication(); Check(UpdateMesh(meshes,p,handle,{VertexLayout::For<Vertex>(attributes),2,1,std::as_bytes(std::span(&corner,1))}),"Unit corner update"); }
        { auto frame=publication.BeginFrame(); const auto bounds=meshes.Acquire(frame,handle).value()->Bounds();
          Check(bounds.sphereRadius*bounds.sphereRadius>=3.0,"Dynamic sphere must not round inside the unit cube corner"); }
        const Vertex vertices[]{{20,-30,40}};
        auto records=std::as_bytes(std::span(vertices));
        { auto p=publication.BeginPublication(); Check(UpdateMesh(meshes,p,handle,{VertexLayout::For<Vertex>(attributes),1,1,records}),"Dynamic update"); }
        { auto frame=publication.BeginFrame(); auto mesh=meshes.Acquire(frame,handle).value();
          Check(mesh->Bounds().maximum[0]>=20 && mesh->Bounds().minimum[1]<=-30 && mesh->Bounds().maximum[2]>=40,"Dynamic partial update expands conservative bounds"); }
        auto bad=vertices[0]; bad.x=std::numeric_limits<float>::infinity();
        { auto p=publication.BeginPublication(); Check(!UpdateMesh(meshes,p,handle,{VertexLayout::For<Vertex>(attributes),0,1,std::as_bytes(std::span(&bad,1))}),"Reject nonfinite dynamic update"); }
        { auto frame=publication.BeginFrame(); auto mesh=meshes.Acquire(frame,handle).value(); Check(mesh.Revision()==3 && mesh->Bounds().maximum[0]>=20,"Failed update preserves version and bounds"); }
    }
}
int main()
{
    CheckBounds();
    Check(SDL_Init(SDL_INIT_VIDEO)==0,"SDL init");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    for(int cycle=0;cycle<2;++cycle)
    {
        auto* window=SDL_CreateWindow("Render state validation",0,0,32,32,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN); Check(window,"Hidden window");
        auto context=SDL_GL_CreateContext(window); Check(context,"GL context"); Check(gladLoadGLLoader(SDL_GL_GetProcAddress),"GL loader");
        { Revisions(); Interpolation(); DynamicBounds(); }
        Check(glGetError()==GL_NO_ERROR,"No GL error"); SDL_GL_DeleteContext(context); SDL_DestroyWindow(window);
    }
    SDL_Quit(); std::println("[PASS] render-state checks={}",checks);
}
