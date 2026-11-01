#pragma once
// Opt-in observation of the ORDINARY docked RBS frame. No special viewport widget,
// descriptor replacement, Physics debug drawing, or synthetic renderer fixture.
#include "Renderer/PassTiming.h"
#include "../../GEngine/src/Assets/TextureBackend.h"
#include "../../external/glad/include/glad/glad.h"
#include <imgui/imgui.h>
#include <chrono>
#include <cstring>
#include <vector>
namespace Rbs::OrdinaryHighlight
{
    inline FILE* highlightLog{}; inline FILE* rows{};
    inline unsigned tick{},stage{},checks{}; inline bool failed{};
    inline std::string directory;
    inline PFNGLSHADERSOURCEPROC nativeShaderSource{};
    inline std::array<_Entity,4> boxes{};
    inline std::array<EntityRenderId,4> ids{};
    inline std::array<std::uintptr_t,4> shapes{};
    inline std::uint64_t context{},cpuStart{};
    inline EntityRenderId active{};
    inline std::array<EntityRenderId,64> submitted{};
    inline std::size_t submittedCount{},lastAllocations{};
    inline std::array<GLuint,64> queries{};
    inline std::array<bool,32> pending{};
    inline std::array<unsigned,32> queryStage{},queryTick{};
    inline std::uint64_t Now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    inline void Check(bool ok,const char* label){++checks;failed|=!ok;std::fprintf(highlightLog,"CHECK\t%s\tstage=%u\t%s\n",ok?"PASS":"FAIL",stage,label);std::fflush(highlightLog);}
    inline void Collect(bool finish=false) {
        if(finish)glFinish();
        for(unsigned i=0;i<32;++i)if(pending[i]){GLint ready{};glGetQueryObjectiv(queries[i*2+1],GL_QUERY_RESULT_AVAILABLE,&ready);
            if(ready){GLuint64 start{},end{};glGetQueryObjectui64v(queries[i*2],GL_QUERY_RESULT,&start);glGetQueryObjectui64v(queries[i*2+1],GL_QUERY_RESULT,&end);
                std::fprintf(rows,"GPU,%u,%u,%llu\n",queryStage[i],queryTick[i],end-start);pending[i]=false;}}
    }
}

// Isolated validation only. Same-frame observation follows the existing Phase24
// GL dispatch interception technique; it is inactive in all timed samples.
namespace Rbs::OrdinaryHighlight
{
    inline PFNGLDRAWARRAYSPROC nativeArrays{};
    inline PFNGLDRAWELEMENTSINSTANCEDPROC nativeInstanced{};
    inline unsigned secondaryInstances{};
    inline void APIENTRY ObserveInstanced(GLenum mode,GLsizei count,GLenum type,const void* indices,GLsizei instances)
    {
        if(tick==40 && glIsEnabled(GL_STENCIL_TEST)) {
            GLint reference{};glGetIntegerv(GL_STENCIL_REF,&reference);
            std::fprintf(highlightLog,"INSTANCED\t%u\t%d\t%d\n",stage,reference,instances);
            if(reference==2)secondaryInstances+=unsigned(instances);
        }
        nativeInstanced(mode,count,type,indices,instances);
    }
    inline unsigned capturedStage=~0u;
    inline void Save(const std::string& suffix,const void* data,std::size_t size)
    {
        FILE* file{}; const auto path=directory+"/same-frame-"+std::to_string(stage)+suffix;
        if(fopen_s(&file,path.c_str(),"wb")||!file){Check(false,"same-frame evidence open");return;}
        Check(std::fwrite(data,1,size,file)==size,"same-frame evidence write");std::fclose(file);
    }
    inline void APIENTRY ObserveArrays(GLenum mode,GLint first,GLsizei count)
    {
        GLint program{};
        if(tick==86 && capturedStage!=stage && mode==GL_TRIANGLES && count==3)
            glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        const GLint selected=program?glGetUniformLocation(program,"selectedMask"):-1;
        if(selected<0){nativeArrays(mode,first,count);return;}
        capturedStage=stage;
        GLint shaderCount{};glGetProgramiv(program,GL_ATTACHED_SHADERS,&shaderCount);
        std::array<GLuint,8> shaders{};GLsizei actual{};glGetAttachedShaders(program,8,&actual,shaders.data());
        for(int i=0;i<actual;++i){GLint length{},type{};glGetShaderiv(shaders[i],GL_SHADER_SOURCE_LENGTH,&length);glGetShaderiv(shaders[i],GL_SHADER_TYPE,&type);
            std::vector<char> source(length);glGetShaderSource(shaders[i],length,nullptr,source.data());Save(type==GL_FRAGMENT_SHADER?"-composite.frag":"-composite.vert",source.data(),source.size());}

        GLint fbo{},origin[2]{},size[2]{},active{},mask{},width{},height{},format{},stencilBits{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
        glGetUniformiv(program,glGetUniformLocation(program,"regionOrigin"),origin);
        glGetUniformiv(program,glGetUniformLocation(program,"regionSize"),size);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&mask);glActiveTexture(active);
        glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_WIDTH,&width);
        glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_HEIGHT,&height);
        glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
        glGetNamedFramebufferAttachmentParameteriv(fbo,GL_STENCIL_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE,&stencilBits);
        std::vector<float> coverage(std::size_t(width)*height*4);
        glGetTextureImage(mask,0,GL_RGBA,GL_FLOAT,GLsizei(coverage.size()*sizeof(float)),coverage.data());
        Save("-mask.f32",coverage.data(),coverage.size()*sizeof(float));
        GLuint texture{},resolved{};glCreateTextures(GL_TEXTURE_2D,1,&texture);
        glTextureStorage2D(texture,1,GL_RGBA8,size[0],size[1]);glCreateFramebuffers(1,&resolved);
        glNamedFramebufferTexture(resolved,GL_COLOR_ATTACHMENT0,texture,0);
        std::vector<unsigned char> before(std::size_t(size[0])*size[1]*4),after(before.size());
        const bool scissor=glIsEnabled(GL_SCISSOR_TEST);
        const auto read=[&](auto& pixels){
            glDisable(GL_SCISSOR_TEST);
            glBlitNamedFramebuffer(fbo,resolved,origin[0],origin[1],origin[0]+size[0],origin[1]+size[1],0,0,size[0],size[1],GL_COLOR_BUFFER_BIT,GL_NEAREST);
            glGetTextureImage(texture,0,GL_RGBA,GL_UNSIGNED_BYTE,GLsizei(pixels.size()),pixels.data());
            if(scissor)glEnable(GL_SCISSOR_TEST);
        };
        read(before);nativeArrays(mode,first,count);read(after);
        Save("-before.rgba",before.data(),before.size());Save("-after.rgba",after.data(),after.size());
        glDeleteFramebuffers(1,&resolved);glDeleteTextures(1,&texture);
        GLint samples{};glGetUniformiv(program,glGetUniformLocation(program,"sampleCount"),&samples);
        std::fprintf(highlightLog,"MSAA\t%u\t%d\n",stage,samples);
        GLfloat dpi[2]{};glGetUniformfv(program,glGetUniformLocation(program,"pixelsPerDip"),dpi);
        std::fprintf(highlightLog,"SAME_FRAME\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.3f\t%.3f\n",
            stage,origin[0],origin[1],size[0],size[1],width,height,format,stencilBits,dpi[0],dpi[1]);
        Check(glGetError()==GL_NO_ERROR,"same-frame color/mask observation preserves GL state");
    }
}

namespace Rbs::OrdinaryHighlight
{
    inline void APIENTRY ObserveShaderSource(GLuint shader,GLsizei count,const GLchar* const* strings,const GLint* lengths)
    {
        std::string source;
        for(GLsizei i=0;i<count;++i)source.append(strings[i],lengths&&lengths[i]>=0?std::size_t(lengths[i]):std::strlen(strings[i]));
        if(source.find("uniform sampler2D selectedMask")!=std::string::npos)Save("-compiled-composite.frag",source.data(),source.size());
        nativeShaderSource(shader,count,strings,lengths);
    }
    inline void BeforePresent(const ImDrawList*,const ImDrawCmd*)
    {
        const auto* data=ImGui::GetDrawData();const int width=int(data->DisplaySize.x*data->FramebufferScale.x),height=int(data->DisplaySize.y*data->FramebufferScale.y);
        GLint fbo{},read{},pack{},buffer{},row{},skipY{},skipX{},samples{};
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&fbo);glGetIntegerv(GL_READ_BUFFER,&read);glGetIntegerv(GL_PACK_ALIGNMENT,&pack);glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&buffer);
        glGetIntegerv(GL_PACK_ROW_LENGTH,&row);glGetIntegerv(GL_PACK_SKIP_ROWS,&skipY);glGetIntegerv(GL_PACK_SKIP_PIXELS,&skipX);glGetIntegerv(GL_SAMPLES,&samples);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glReadBuffer(GL_BACK);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
        std::vector<unsigned char> color(std::size_t(width)*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,color.data());Save("-pre-present.rgba",color.data(),color.size());
        std::fprintf(highlightLog,"WINDOW_MSAA\t%u\t%d\tsampleShading=%d\n",stage,samples,glIsEnabled(GL_SAMPLE_SHADING));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);glReadBuffer(read);glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);glPixelStorei(GL_PACK_ALIGNMENT,pack);glPixelStorei(GL_PACK_ROW_LENGTH,row);glPixelStorei(GL_PACK_SKIP_ROWS,skipY);glPixelStorei(GL_PACK_SKIP_PIXELS,skipX);
        Check(glGetError()==GL_NO_ERROR,"final UI backbuffer observation restores GL state");
    }
}

namespace Rbs
{
    void AuditHighlightUiCallback()
    {
        if(OrdinaryHighlight::highlightLog && OrdinaryHighlight::tick==86)
            ImGui::GetForegroundDrawList(ImGui::GetMainViewport())->AddCallback(OrdinaryHighlight::BeforePresent,nullptr);
    }
    void AuditHighlightDescriptor(FrameSubmissionDesc& desc)
    {
        using namespace OrdinaryHighlight;
        desc.measureSubmission=true; active=desc.selectedEntity;submittedCount=desc.selectedEntities.size();
        std::copy(desc.selectedEntities.begin(),desc.selectedEntities.end(),submitted.begin());
        if(tick!=86)return;
        std::fprintf(highlightLog,"DESCRIPTOR\t%u\t%zu\t%llu:%u:%llu\n",stage,submittedCount,active.registry,active.index,active.generation);
        for(std::size_t i=0;i<submittedCount;++i){auto id=submitted[i];std::fprintf(highlightLog,"SUBMITTED\t%u\t%llu:%u:%llu\tclass=%u\n",stage,id.registry,id.index,id.generation,desc.SelectionClass(id));}
    }
    void AuditHighlightFrame(const RenderFrame& frame)
    {
        using namespace OrdinaryHighlight;
        if(tick!=86)return;
        std::size_t ordinal=0;
        for(const auto& draw:frame.Draws()) {
            unsigned classification=draw.entity==active?1:0;
            if(!classification)for(std::size_t i=0;i<submittedCount;++i)if(draw.entity==submitted[i])classification=2;
            std::fprintf(highlightLog,"DRAW_ORDER\t%u\t%zu\t%llu:%u:%llu\tclass=%u\n",stage,ordinal++,draw.entity.registry,draw.entity.index,draw.entity.generation,classification);
        }
        std::array<unsigned,4> seen{};
        for(const auto& draw:frame.Draws())for(unsigned i=0;i<4;++i)if(draw.entity==ids[i]) {
            ++seen[i];const auto& camera=frame.Cameras()[0];auto clip=camera.projection*camera.view*draw.worldTransform*glm::vec4(0,0,0,1);
            const auto ndc=glm::vec3(clip)/clip.w;
            std::fprintf(highlightLog,"EXTRACTED\t%u\t%u\t%llu:%u:%llu\t%.3f\t%.3f\t%.3f\t%u\t%u\n",stage,i,draw.entity.registry,draw.entity.index,draw.entity.generation,
                (ndc.x*.5f+.5f)*camera.viewportWidth,(ndc.y*.5f+.5f)*camera.viewportHeight,ndc.z,camera.viewportWidth,camera.viewportHeight);
        }
        for(auto n:seen)Check(n>0,"all four ordinary Scene meshes in immutable frame");
    }
}

bool RigidBodySimulationApp::ValidateOrdinaryHighlightUpdate()
{
    using namespace Rbs::OrdinaryHighlight;
    // Await the ordinary material publication before capturing comparable frames.
    // Physics and the audit-only animation clock stay paused during this warmup.
    if(!highlightLog && !m_WoodSettled){m_IsPause=true;m_ActiveScene->SetPaused(true);return true;}
    ++tick; auto& physics=*m_ActiveScene->GetPhysicsSystem();
    const auto finish=[&](){glad_glDrawArrays=nativeArrays;glad_glDrawElementsInstanced=nativeInstanced;glad_glShaderSource=nativeShaderSource;Collect(true);glDeleteQueries(64,queries.data());
        std::fprintf(highlightLog,"SUMMARY\t%s\tchecks=%u\n",failed?"FAIL":"PASS",checks);std::fclose(highlightLog);std::fclose(rows);highlightLog=rows=nullptr;
        if(failed)FailRuntime({ApplicationRuntimeErrorCode::SubsystemFailure,"Ordinary highlight audit",{},"observation",{},"audit failed"});else ShutDown();return false;};
    if(tick==1 && stage==0) {
        const char* path=std::getenv("GENGINE_ORDINARY_HIGHLIGHT_OUTPUT");if(fopen_s(&highlightLog,path,"w")||!highlightLog)return false;
        directory=std::filesystem::path(path).parent_path().string();const auto csv=directory+"/highlight-frames.csv";
        if(fopen_s(&rows,csv.c_str(),"w")||!rows)return false;
        std::fprintf(highlightLog,"SCOPE\tOrdinary RBS Scene + docked viewport + final window framebuffer. Programmatic selection stimulus; NOT physical input or Owner visual acceptance.\n");
        GetWindow()->Timings().Reset();Check(bool(GetWindow()->Timings().Initialize(true,(directory+"/highlight-passes.csv").c_str())),"existing pass timing");
        nativeShaderSource=glad_glShaderSource;glad_glShaderSource=ObserveShaderSource;nativeArrays=glad_glDrawArrays;glad_glDrawArrays=ObserveArrays;nativeInstanced=glad_glDrawElementsInstanced;glad_glDrawElementsInstanced=ObserveInstanced;
        std::fprintf(highlightLog,"GPU_IDENTITY\t%s\t%s\t%s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
        glGenQueries(64,queries.data());Check(queries[0]&&glGetError()==GL_NO_ERROR,"bounded timestamp pool");
        unsigned found=0;
        for(auto id:m_ActiveScene->ReadRegistry().view<RigidBody3DComponent,MeshRendererComponent>()){
            _Entity e{id,m_ActiveScene.get()};const auto h=m_ActiveScene->GetBodyBinding(e);if(!h)continue;
            if(physics.ReadBodyCollider(*h)->type==ShapeType::Sphere)Check(bool(m_ActiveScene->SetLocalTranslation(e,{50,4,-12})),"retire occluding Sphere fixture from view");
            if(physics.ReadBodyCollider(*h)->type==ShapeType::Box && e.GetComponent<RigidBody3DComponent>().Type==BodyType::Dynamic && found<4)boxes[found++]=e;
        }
        Check(found==4,"four ordinary SphereBoxStack Box fixtures");if(failed)return finish();
        m_IsPause=true;m_ActiveScene->SetPaused(true);
        for(unsigned i=0;i<4;++i){Check(bool(m_ActiveScene->SetLocalTranslation(boxes[i],{-4.5f+3.f*i,4.f,-12.f})),"author fixture transform");
            shapes[i]=physics.ReadBodyCollider(*m_ActiveScene->GetBodyBinding(boxes[i]))->identity;ids[i]=*m_ActiveScene->RenderData().Identify(boxes[i]);
            std::fprintf(highlightLog,"FIXTURE_UUID\t%u\t%llu\n",i,static_cast<std::uint64_t>(boxes[i].GetUUID()));}
        FocusMaterialCamera(m_EditorCamera_,{0,4,-12},.12f,Math::Pi,21.f);
        ClearColliderSelection();m_IsShowDebugBoundingBox=false;m_ShowSelectedColliderOutlines=false;m_ColliderLayers={false,false,false,false,false};
        context=physics.ColliderContextRevision();
    }
    if(tick==1){
        // Sequential ordinary selection operations, with membership retained across cases.
        if(stage==1)SelectColliderEntity(boxes[0],false);
        if(stage==2)SelectColliderEntity(boxes[1],true);
        if(stage==3)SelectColliderEntity(boxes[2],true);
        if(stage==4)SelectColliderEntity(boxes[1],true);
        if(stage==5)m_ShowSelectedColliderOutlines=true; // Selected, zero layers.
        if(stage==6){m_ShowSelectedColliderOutlines=false;m_IsShowDebugBoundingBox=true;} // All, zero layers.
        if(stage==7){m_IsShowDebugBoundingBox=false;m_ShowSelectedColliderOutlines=true;m_ColliderLayers.colliders=true;}
        if(stage==8){m_ColliderEscapeRequested=true;m_ViewportActive=true;}
        if(stage==9){m_ShowSelectedColliderOutlines=false;m_ColliderLayers.colliders=false;SelectColliderEntity(boxes[0],false);SelectColliderEntity(boxes[2],true);}
        if(stage==10){SelectColliderEntity(boxes[0],true);SelectColliderEntity(boxes[0],true);}
        if(stage==11){SelectColliderEntity(boxes[0],false);SelectColliderEntity(boxes[3],true);}
        if(stage==12)ClearColliderSelection();
        // Each mesh's own legacy single-selection Gold is the reference, including
        // its material, lighting, projection and final ordinary ImGui composition.
        if(stage>=13 && stage<=15)SelectColliderEntity(boxes[stage-12],false);
        if(stage==16){SelectColliderEntity(boxes[0],false);for(unsigned i=1;i<4;++i)SelectColliderEntity(boxes[i],true);}
        if(stage==17){SelectColliderEntity(boxes[1],true);SelectColliderEntity(boxes[1],true);}
        if(stage==18){SelectColliderEntity(boxes[0],true);SelectColliderEntity(boxes[0],true);}
        if(stage==19)ClearColliderSelection();
        std::fprintf(highlightLog,"CASE\t%u\tselected=%zu\tactive=%u\n",stage,m_ColliderOutlines.count,unsigned(m_HoveredEntity));
    }
    if(failed)return finish();
    if(tick==90){
        for(unsigned i=0;i<4;++i)Check(physics.ReadBodyCollider(*m_ActiveScene->GetBodyBinding(boxes[i]))->identity==shapes[i],"Shape identity unchanged");
        Check(physics.ColliderContextRevision()==context,"no unintended Physics mutation");
        if(failed||stage==19)return finish();++stage;tick=0;return true;
    }
    Collect();const auto slot=tick%32;Check(!pending[slot],"bounded timestamp slot ready");if(failed)return finish();
    queryStage[slot]=stage;queryTick[slot]=tick;glQueryCounter(queries[slot*2],GL_TIMESTAMP);cpuStart=Now();return true;
}
void RigidBodySimulationApp::ObserveOrdinaryHighlightSubmission(const ScheduledFrameStats& stats)
{
    using namespace Rbs::OrdinaryHighlight;
    if(!highlightLog||tick==0)return;
    const auto slot=tick%32;glQueryCounter(queries[slot*2+1],GL_TIMESTAMP);pending[slot]=true;
    const auto& s=stats.submission;
    if(tick>=21&&tick<=84){
        std::fprintf(rows,"CPU,%u,%u,%llu,%.3f,%.3f,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%u,%u\n",stage,tick,Now()-cpuStart,
            stats.extraction.preparationMicroseconds,stats.extraction.extractionMicroseconds,s.submittedDrawCalls,s.instancedDrawCalls,s.selectionDrawCalls,s.selectionPasses,
            s.selectionCurrentScratchBytes,s.selectionScratchBytes,s.selectionScratchAllocations,s.selectionActiveDraws+s.selectionSecondaryDraws,s.selectionRegion.width,s.selectionRegion.height);
        if(tick==21)lastAllocations=s.selectionScratchAllocations;
        if(s.selectionScratchAllocations!=lastAllocations)Check(false,"steady-state scratch allocation");
    }
    if(tick==86){
        const std::array<unsigned,20> expected{0,1,2,3,2,2,2,2,0,2,2,2,0,1,1,1,4,4,4,0};
        Check(m_ColliderOutlines.count==expected[stage]&&submittedCount==expected[stage],"native selected count equals ordinary descriptor count");
        Check(expected[stage]?s.selectionActiveDraws==1&&s.selectionSecondaryDraws==expected[stage]-1:s.selectionPasses==0&&s.selectionDrawCalls==0,"active/secondary draws or zero-selection no work");
        std::fprintf(highlightLog,"DRAW_CLASSES\t%u\t%zu\t%zu\t%zu\t%zu\t%zu\n",stage,s.selectionActiveDraws,s.selectionSecondaryDraws,s.selectionDrawCalls,s.selectionCurrentScratchBytes,s.selectionScratchBytes);
        const auto& target=m_RenderTarget->Description().Storage;
        std::vector<std::byte> color(std::size_t(target.Width)*target.Height*4);
        auto view=m_RenderTarget->ColorView();Check(bool(view),"ordinary UI resolved color view");
        if(view){auto name=Asset::AssetDetail::TextureBackend::Name(Asset::TextureView(*view));Check(bool(name),"actual ImGui texture identity");
            if(name){glGetTextureImage(*name,0,GL_RGBA,GL_UNSIGNED_BYTE,GLsizei(color.size()),color.data());Save("-ui-texture.rgba",color.data(),color.size());}}
        Check(bool(m_RenderTarget->ReadColor(color)),"ordinary resolved render target readback");Save("-target.rgba",color.data(),color.size());
        const auto framebuffer=GetWindow()->GetFramebufferPixelSize();
        GLint readFbo{},readBuffer{},pack{},packBuffer{},rowLength{},skipRows{},skipPixels{};
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);glGetIntegerv(GL_READ_BUFFER,&readBuffer);glGetIntegerv(GL_PACK_ALIGNMENT,&pack);glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&packBuffer);
        glGetIntegerv(GL_PACK_ROW_LENGTH,&rowLength);glGetIntegerv(GL_PACK_SKIP_ROWS,&skipRows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&skipPixels);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glReadBuffer(GL_FRONT);
        std::vector<unsigned char> front(std::size_t(framebuffer.Width)*framebuffer.Height*4);
        glReadPixels(0,0,framebuffer.Width,framebuffer.Height,GL_RGBA,GL_UNSIGNED_BYTE,front.data());Save("-presented.rgba",front.data(),front.size());
        glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);glReadBuffer(readBuffer);glBindBuffer(GL_PIXEL_PACK_BUFFER,packBuffer);glPixelStorei(GL_PACK_ALIGNMENT,pack);
        glPixelStorei(GL_PACK_ROW_LENGTH,rowLength);glPixelStorei(GL_PACK_SKIP_ROWS,skipRows);glPixelStorei(GL_PACK_SKIP_PIXELS,skipPixels);
        const auto origin=ImGui::GetMainViewport()->Pos;const auto logical=GetWindow()->GetLogicalSize();
        std::fprintf(highlightLog,"PRESENTED\t%u\t%u\t%u\t%u\t%u\t%.3f\t%.3f\t%.3f\t%.3f\t%u\t%u\n",stage,
            framebuffer.Width,framebuffer.Height,target.Width,target.Height,m_ViewportBounds[0].x-origin.x,m_ViewportBounds[0].y-origin.y,
            m_ViewportBounds[1].x-origin.x,m_ViewportBounds[1].y-origin.y,logical.Width,logical.Height);
        Check(glGetError()==GL_NO_ERROR,"final presented readback restores GL state");
    }
}
