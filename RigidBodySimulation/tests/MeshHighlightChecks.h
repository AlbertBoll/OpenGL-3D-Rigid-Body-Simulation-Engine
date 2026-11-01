#pragma once
#include "Renderer/PassTiming.h"
#include "../../external/glad/include/glad/glad.h"
#include <chrono>
#include <vector>
namespace Rbs::MeshHighlight
{
    inline FILE* highlightLog{}; inline FILE* rows{};
    inline unsigned tick{},stage{},checks{}; inline bool failed{};
    inline _Entity a,b,c,d,occluder;
    inline MaterialHandle original,masked,transparent;
    inline std::uintptr_t shapeA{},shapeB{};
    inline std::uint64_t context{},cpuStart{};
    inline std::vector<std::byte> pixels,singleB;
    inline std::array<GLuint,64> queries{};
    inline std::array<bool,32> pending{};
    inline std::array<unsigned,32> queryStage{},queryTick{};
    inline std::size_t lastAllocations{};
    inline std::string directory;
    inline std::uint64_t Now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    inline void Check(bool ok,const char* label){++checks;failed|=!ok;std::fprintf(highlightLog,"CHECK\t%s\tstage=%u\t%s\n",ok?"PASS":"FAIL",stage,label);std::fflush(highlightLog);}
    inline void Collect(bool finish=false) {
        if(finish) glFinish();
        for(unsigned i=0;i<32;++i) if(pending[i]) {
            GLint ready{};glGetQueryObjectiv(queries[i*2+1],GL_QUERY_RESULT_AVAILABLE,&ready);
            if(ready) {GLuint64 start{},end{};glGetQueryObjectui64v(queries[i*2],GL_QUERY_RESULT,&start);glGetQueryObjectui64v(queries[i*2+1],GL_QUERY_RESULT,&end);
                std::fprintf(rows,"GPU,%u,%u,%llu\n",queryStage[i],queryTick[i],end-start);pending[i]=false;}
        }
    }
}
#include "MeshHighlightObservation.h"

bool RigidBodySimulationApp::ValidateMeshHighlightUpdate()
{
    using namespace Rbs::MeshHighlight;
    ++tick;
    auto& physics=*m_ActiveScene->GetPhysicsSystem();
    const auto finish=[&](){glad_glDrawArrays=nativeArrays;glad_glDrawElementsInstanced=nativeInstanced;Collect(true);glDeleteQueries(64,queries.data());
        std::fprintf(highlightLog,"SUMMARY\t%s\tchecks=%u\n",failed?"FAIL":"PASS",checks);std::fclose(highlightLog);std::fclose(rows);highlightLog=rows=nullptr;
        if(failed) FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Mesh highlight",{},"focused gate",{},"Mesh highlight validation failed"});else ShutDown();return false;};
    if(tick==1 && stage==0) {
        const char* path=std::getenv("GENGINE_MESH_HIGHLIGHT_OUTPUT");
        if(fopen_s(&highlightLog,path,"w")||!highlightLog)return false;
        directory=std::filesystem::path(path).parent_path().string();
        const auto rowPath=directory+"/highlight-frames.csv";
        if(fopen_s(&rows,rowPath.c_str(),"w")||!rows)return false;
        std::fprintf(highlightLog,"SCOPE\tActual render submission/readback and async GPU timestamps; fixture selection; not Owner visual acceptance. 20 warmup,64 measured frames per case.\n");
        GetWindow()->Timings().Reset();
        Check(bool(GetWindow()->Timings().Initialize(true,(directory+"/highlight-passes.csv").c_str())),"existing CPU/GPU pass timings enabled");
        nativeArrays=glad_glDrawArrays;glad_glDrawArrays=ObserveArrays;
        nativeInstanced=glad_glDrawElementsInstanced;glad_glDrawElementsInstanced=ObserveInstanced;
        std::fprintf(highlightLog,"GPU_IDENTITY\t%s\t%s\t%s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
        glGenQueries(64,queries.data()); Check(queries[0]&&glGetError()==GL_NO_ERROR,"async frame timestamp storage");
        for(auto id:m_ActiveScene->ReadRegistry().view<RigidBody3DComponent,MeshRendererComponent>()){
            _Entity e{id,m_ActiveScene.get()};const auto body=m_ActiveScene->GetBodyBinding(e);if(!body)continue;
            auto shape=physics.ReadBodyCollider(*body);
            if(shape&&shape->type==ShapeType::Sphere)a=e;
            if(shape&&shape->type==ShapeType::Box&&e.GetComponent<RigidBody3DComponent>().Type==BodyType::Dynamic){if(!b)b=e;else if(!c)c=e;else if(!d)d=e;}
        }
        Check(a&&b&&c&&d,"native Sphere and three Box fixtures");if(failed)return finish();
        m_IsPause=true;m_ActiveScene->SetPaused(true);
        Check(bool(m_ActiveScene->SetLocalTranslation(a,{-3,5,-20}))&&bool(m_ActiveScene->SetLocalTranslation(b,{3,5,-20}))&&
              bool(m_ActiveScene->SetLocalTranslation(c,{-3,8,-20}))&&bool(m_ActiveScene->SetLocalTranslation(d,{3,8,-20})),"author controlled render fixtures");
        shapeA=physics.ReadBodyCollider(*m_ActiveScene->GetBodyBinding(a))->identity;shapeB=physics.ReadBodyCollider(*m_ActiveScene->GetBodyBinding(b))->identity;
        original=b.GetComponent<MeshRendererComponent>().material;
        const auto texturePath=directory+"/alpha-fixture.tga";
        std::array<unsigned char,18+16*16*4> texels{};texels[2]=2;texels[12]=16;texels[14]=16;texels[16]=32;texels[17]=8;
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){const auto i=18+(y*16+x)*4;texels[i]=texels[i+1]=texels[i+2]=180;texels[i+3]=((x/4+y/4)%2)?0:255;}
        FILE* textureFile{};Check(!fopen_s(&textureFile,texturePath.c_str(),"wb")&&textureFile,"alpha texture fixture file");if(failed)return finish();
        Check(std::fwrite(texels.data(),1,texels.size(),textureFile)==texels.size(),"alpha fixture bytes");std::fclose(textureFile);
        auto texture=Manager::AssetsManager::LoadTexture(texturePath);
        Asset::SamplerDesc samplerDesc;samplerDesc.minFilter=samplerDesc.magFilter=Asset::SamplerFilter::Nearest;
        auto sampler=Manager::AssetsManager::GetSampler(samplerDesc);Check(texture&&sampler,"actual alpha cutout texture and sampler");if(failed)return finish();
        MaterialAuthoringDesc mat;mat.kind=MaterialKind::Masked;mat.opacity=1;mat.textures[0]=MaterialTextureValue{*texture,*sampler};
        auto m=m_SceneResources->CreateMaterial(mat);mat.kind=MaterialKind::Transparent;mat.opacity=.5f;
        auto t=m_SceneResources->CreateMaterial(mat);Check(m&&t,"masked/transparent fixture material publication");if(failed)return finish();masked=*m;transparent=*t;
        auto blocker=m_ActiveScene->CreateEntity("Selection occlusion fixture");Check(bool(blocker),"occluder render identity");if(failed)return finish();occluder=*blocker;
        Check(bool(m_SceneResources->AssignRenderable(occluder,{b.GetComponent<MeshRendererComponent>().mesh,original}))&&
            bool(m_ActiveScene->SetLocalScale(occluder,{10,10,1}))&&bool(m_ActiveScene->SetLocalTranslation(occluder,{100,100,100})),"render-only occluder outside view");
        Rbs::ColliderOutlineSelection capacity;auto body=*m_ActiveScene->GetBodyBinding(a);
        for(unsigned i=0;i<64;++i){auto h=body;h.generation+=i;capacity.Select(h,true);}
        auto excess=body;excess.generation+=64;capacity.Select(excess,true);
        Check(capacity.count==64&&capacity.capacityReached&&capacity.Contains(body),"existing64 capacity reports overflow without losing members");
        capacity.Select(body,true);Check(capacity.count==63&&!capacity.capacityReached,"capacity toggle removes only requested member");
    }
    if(tick==1){
        secondaryInstances=0;
        ClearColliderSelection();m_IsShowDebugBoundingBox=false;m_ShowSelectedColliderOutlines=false;m_ColliderLayers={false,false,false,false,false};
        Check((!a||bool(m_ActiveScene->SetLocalTranslation(a,{stage==5?-8.f:-3.f,5,-20})))&&bool(m_ActiveScene->SetLocalTranslation(b,{stage==5?8.f:3.f,5,-20})),"fixture separation");
        FocusMaterialCamera(m_EditorCamera_,{0,6,-20},0,Math::Pi,25);
        auto material=stage==7?masked:stage==8?transparent:original;
        Check(bool(m_SceneResources->AssignRenderable(b,{b.GetComponent<MeshRendererComponent>().mesh,material})),"fixture alpha mode");
        if(stage==1) SelectColliderEntity(a,false);
        else if(stage==2)SelectColliderEntity(b,false);
        else if(stage>=3&&stage<=10){SelectColliderEntity(a,false);SelectColliderEntity(b,true);if(stage==4){SelectColliderEntity(a,true);SelectColliderEntity(a,true);}if(stage==6){SelectColliderEntity(c,true);SelectColliderEntity(d,true);}}
        if(stage==11){SelectColliderEntity(a,false);SelectColliderEntity(b,true);SelectColliderEntity(b,true);}
        if(stage==12){SelectColliderEntity(a,false);SelectColliderEntity(b,true);m_ColliderEscapeRequested=true;}
        if(stage==13){SelectColliderEntity(a,false);SelectColliderEntity(b,true);Check(bool(m_ActiveScene->DestroyEntity(a)),"delete selected mesh/body");}
        if(stage==14)ClearColliderSelection();
        if(stage>=15&&stage<=18){SelectColliderEntity(b,false);SelectColliderEntity(c,true);}
        if(stage==15)Check(bool(m_ActiveScene->SetLocalTranslation(occluder,{0,6,-24})),"foreground occlusion stimulus");
        if(stage==16){Check(bool(m_ActiveScene->SetLocalTranslation(occluder,{100,100,100})),"occluder retired from view");m_ShowSelectedColliderOutlines=true;}
        if(stage==17)m_IsShowDebugBoundingBox=true;
        if(stage==18){m_ShowSelectedColliderOutlines=true;m_ColliderLayers.colliders=true;}
        if(stage==19){SelectColliderEntity(b,false);SelectColliderEntity(c,true);ClearColliderSelection();}
        if(stage>=20&&stage<=22) {
            SelectColliderEntity(b,false);
            for(auto id:m_ActiveScene->ReadRegistry().view<RigidBody3DComponent,MeshRendererComponent>()){
                _Entity e{id,m_ActiveScene.get()};const auto h=m_ActiveScene->GetBodyBinding(e);
                if(e!=b&&h&&physics.ReadBodyCollider(*h)->type==ShapeType::Box&&e.GetComponent<RigidBody3DComponent>().Type==BodyType::Dynamic)SelectColliderEntity(e,true);
            }
            SelectColliderEntity(b,true);SelectColliderEntity(b,true);
        }
        if(stage==25||stage==26)SelectColliderEntity(b,false);
        context=physics.ColliderContextRevision();
        std::fprintf(highlightLog,"CASE\t%u\tselected=%zu\tactive=%u\n",stage,m_ColliderOutlines.count,unsigned(m_HoveredEntity));
    }
    if(failed)return finish();
    if(tick==90){
        Check(!a||physics.ReadBodyCollider(*m_ActiveScene->GetBodyBinding(a))->identity==shapeA,"render highlights preserve Sphere Shape");
        Check(physics.ReadBodyCollider(*m_ActiveScene->GetBodyBinding(b))->identity==shapeB&&physics.ColliderContextRevision()==context,"render highlights preserve Box and Physics context");
        if(failed||stage==26)return finish();++stage;tick=0;return true;
    }
    if(stage==10) Check(bool(SetEditorViewport(EditorViewportLogicalSize{720,480},FramebufferScale{1,1})),"resize stimulus accepted");
    Collect();const auto slot=tick%32;
    Check(!pending[slot],"timestamp pool remains bounded without blocking");if(failed)return finish();
    queryStage[slot]=stage;queryTick[slot]=tick;
    glQueryCounter(queries[slot*2],GL_TIMESTAMP);cpuStart=Now();
    return true;
}
void RigidBodySimulationApp::ObserveMeshHighlightSubmission(const ScheduledFrameStats& stats)
{
    using namespace Rbs::MeshHighlight;
    if(!highlightLog||tick==0)return;
    const auto slot=tick%32;glQueryCounter(queries[slot*2+1],GL_TIMESTAMP);pending[slot]=true;
    const auto& s=stats.submission;
    if(tick>=21&&tick<=84){
        std::fprintf(rows,"CPU,%u,%u,%llu,%.3f,%.3f,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%u,%u\n",stage,tick,Now()-cpuStart,
            stats.extraction.preparationMicroseconds,stats.extraction.extractionMicroseconds,s.submittedDrawCalls,s.instancedDrawCalls,
            s.selectionDrawCalls,s.selectionPasses,s.selectionCurrentScratchBytes,s.selectionScratchBytes,s.selectionScratchAllocations,
            s.selectionActiveDraws+s.selectionSecondaryDraws,s.selectionRegion.width,s.selectionRegion.height);
        if(tick==21)lastAllocations=s.selectionScratchAllocations;
        if(s.selectionScratchAllocations!=lastAllocations)Check(false,"steady-state scratch allocation regression");
    }
    if(tick==40){
        const bool empty=stage==0||stage==12||stage==14||stage==19||stage==22||stage==23||stage==24;
        Check(empty ? s.selectionPasses==0&&s.selectionDrawCalls==0 : s.selectionActiveDraws>0&&s.selectionPasses==4,"zero work or existing four-stage sequence");
        if((stage>=3&&stage<=10)||(stage>=15&&stage<=18))Check(s.selectionSecondaryDraws>0,"secondary mesh draws reach actual renderer");
        if(stage==20)Check(secondaryInstances>=4,"actual GPU instanced batch carries Secondary stencil classification");
        if(stage==21)Check(s.instancedDrawCalls==0&&secondaryInstances==0,"serial reference disables instancing");
        if(stage==22)Check(m_ColliderOutlines.count>1&&s.selectionActiveDraws==0&&s.selectionSecondaryDraws==0,"stale render identities cannot produce highlights or alter selected ownership");
        if(stage==11)Check(m_ColliderOutlines.count==1&&s.selectionSecondaryDraws==0,"toggle removes only secondary highlight");
        if(stage==12)Check(!m_HoveredEntity&&!m_ColliderOutlines.count,"Esc clears complete selection presentation");
        if(stage==13)Check(m_ColliderOutlines.count==1&&s.selectionSecondaryDraws==0,"deleted identity pruned from submission");
        if(stage==10)Check(m_RenderTarget->Description().Storage.Width==720&&m_RenderTarget->Description().Storage.Height==480,"actual resized physical viewport720x480");
        if(stage>=16&&stage<=18)Check(m_ColliderOutlines.count==2,"Scope/layer controls preserve complete selected set");
        Check(s.selectionCurrentScratchBytes<=s.selectionScratchBytes,"current/retained GPU accounting");
    }
    if(tick==86){
        const auto& size=m_RenderTarget->Description().Storage;pixels.resize(std::size_t(size.Width)*size.Height*4);
        Check(bool(m_RenderTarget->ReadColor(pixels)),"actual output color readback");
        const auto file=directory+"/highlight-"+std::to_string(stage)+".rgba";FILE* image{};
        if(!fopen_s(&image,file.c_str(),"wb")&&image){std::fwrite(pixels.data(),1,pixels.size(),image);std::fclose(image);}
        std::fprintf(highlightLog,"IMAGE\t%u\t%u\t%u\n",stage,size.Width,size.Height);
        if(stage==2)singleB=pixels;
        if(stage==3){std::size_t changed=0;for(std::size_t i=0;i<pixels.size()&&i<singleB.size();i+=4)changed+=pixels[i]!=singleB[i]||pixels[i+1]!=singleB[i+1]||pixels[i+2]!=singleB[i+2];Check(changed>0,"adding secondary changes real output while active remains selected");}
    }
}

namespace Rbs { unsigned MeshHighlightStage(){return MeshHighlight::stage;} }

namespace Rbs
{
    void MeshHighlightDescriptor(FrameSubmission& submitter,FrameSubmissionDesc& desc)
    {
        using namespace MeshHighlight;
        if(stage==0&&tick==1) {
            auto builder=RenderFrameBuilder::Create();Check(bool(builder),"capacity fixture frame builder");
            if(builder) {
                auto frame=std::move(*builder).Finalize();Check(bool(frame),"capacity fixture immutable frame");
                if(frame) {
                    std::array<EntityRenderId,65> tooMany{};auto invalid=desc;invalid.selectedEntities=tooMany;invalid.measureSubmission=false;
                    EntityPickTable picks;auto result=submitter.Submit(*frame,invalid,picks);
                    Check(!result&&std::get_if<SubmissionCode>(&result.error().cause)&&std::get<SubmissionCode>(result.error().cause)==SubmissionCode::InvalidDraw,"renderer rejects oversize selection explicitly before drawing");
                }
            }
        }
        if(stage==21)desc.instancingEnabled=false;
        if(stage==22) {
            static std::array<EntityRenderId,FrameSubmissionDesc::SelectionCapacity> stale;
            std::copy(desc.selectedEntities.begin(),desc.selectedEntities.end(),stale.begin());
            for(std::size_t i=0;i<desc.selectedEntities.size();++i)++stale[i].generation;
            desc.selectedEntities={stale.data(),desc.selectedEntities.size()};++desc.selectedEntity.generation;
        }
        if(stage==25)desc.selectedEntities={}; // Exact legacy one-identity contract reference.
    }
}
