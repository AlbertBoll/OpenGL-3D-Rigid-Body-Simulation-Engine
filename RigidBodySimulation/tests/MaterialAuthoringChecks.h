#pragma once

#include "Scene/_Entity.h"
#include "Renderer/SceneRenderResources.h"
#include "Renderer/RenderExtraction.h"
#include "Managers/AssetsManager.h"
#include "Core/Log.h"
#include <cmath>
#include <cstdlib>
#include <limits>

#ifdef GENGINE_MATERIAL_AUTHORING_BACKEND
#include <glad/glad.h>
#include "../../GEngine/src/Renderer/SubmissionUploads.h"
#include "Renderer/FrameSubmission.h"
#include "Core/RenderTarget.h"
#include "../../GEngine/src/Assets/TextureBackend.h"
#endif

namespace PreEditorValidation
{
    struct MaterialGpuCalls
    {
        inline static unsigned compiles{}, links{}, textureNames{}, textureStorage{}, textureUploads{};
        MaterialGpuCalls();
        ~MaterialGpuCalls();
        bool Empty() const { return !(compiles || links || textureNames || textureStorage || textureUploads); }
    };
    bool MaterialProjectionGpuChecks();
    bool MaterialCoverageGpuChecks(::GEngine::SceneRenderResources&, ::GEngine::Asset::MeshHandle);
    bool MaterialPackedHeaderChecks(const ::GEngine::RenderFrame&);

#ifdef GENGINE_MATERIAL_AUTHORING_BACKEND
    // Actual GL entry-point observations, also enabled in Release. The serial
    // authoring interval cannot race rendering; every pointer is restored by RAII.
    struct MaterialGpuEntrypoints
    {
        inline static PFNGLCOMPILESHADERPROC compile;
        inline static PFNGLLINKPROGRAMPROC link;
        inline static PFNGLGENTEXTURESPROC gen;
        inline static PFNGLCREATETEXTURESPROC create;
        inline static PFNGLTEXIMAGE2DPROC image;
        inline static PFNGLTEXTURESTORAGE2DPROC storage;
        inline static PFNGLTEXTURESUBIMAGE2DPROC upload;
        static void APIENTRY Compile(GLuint v) { ++MaterialGpuCalls::compiles; compile(v); }
        static void APIENTRY Link(GLuint v) { ++MaterialGpuCalls::links; link(v); }
        static void APIENTRY Gen(GLsizei n, GLuint* v) { MaterialGpuCalls::textureNames += n; gen(n,v); }
        static void APIENTRY Create(GLenum t, GLsizei n, GLuint* v) { MaterialGpuCalls::textureNames += n; create(t,n,v); }
        static void APIENTRY Image(GLenum t, GLint l, GLint f, GLsizei w, GLsizei h, GLint b, GLenum c, GLenum s, const void* p)
        { ++MaterialGpuCalls::textureStorage; if (p) ++MaterialGpuCalls::textureUploads; image(t,l,f,w,h,b,c,s,p); }
        static void APIENTRY Storage(GLuint t, GLsizei l, GLenum f, GLsizei w, GLsizei h)
        { ++MaterialGpuCalls::textureStorage; storage(t,l,f,w,h); }
        static void APIENTRY Upload(GLuint t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum s, const void* p)
        { ++MaterialGpuCalls::textureUploads; upload(t,l,x,y,w,h,f,s,p); }
    };
    MaterialGpuCalls::MaterialGpuCalls()
    {
        using N=MaterialGpuEntrypoints;
            compiles=links=textureNames=textureStorage=textureUploads=0;
            N::compile=glad_glCompileShader; glad_glCompileShader=N::Compile;
            N::link=glad_glLinkProgram; glad_glLinkProgram=N::Link;
            N::gen=glad_glGenTextures; glad_glGenTextures=N::Gen;
            N::create=glad_glCreateTextures; glad_glCreateTextures=N::Create;
            N::image=glad_glTexImage2D; glad_glTexImage2D=N::Image;
            N::storage=glad_glTextureStorage2D; glad_glTextureStorage2D=N::Storage;
            N::upload=glad_glTextureSubImage2D; glad_glTextureSubImage2D=N::Upload;
        }
    MaterialGpuCalls::~MaterialGpuCalls()
    {
        using N=MaterialGpuEntrypoints;
            glad_glCompileShader=N::compile; glad_glLinkProgram=N::link;
            glad_glGenTextures=N::gen; glad_glCreateTextures=N::create;
            glad_glTexImage2D=N::image; glad_glTextureStorage2D=N::storage; glad_glTextureSubImage2D=N::upload;
        }

    // Execute the production GLSL projection function against controlled float
    // images. Compute output avoids a second application/window or raster ambiguity.
    bool MaterialProjectionGpuChecks()
    {
        using namespace ::GEngine;
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_GPU renderer={} vendor={} version={}",
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
            reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        struct Objects
        {
            GLuint shader{}, program{}, texture{}, buffers[2]{};
            GLint previousProgram{}, previousTexture{}, previousSampler{}, previousStorage{}, previousActive{};
            RenderBackend::UploadBindings bindings;
            RenderBackend::UploadBindings::Binding output;
            Objects()
            {
                glGetIntegerv(GL_CURRENT_PROGRAM,&previousProgram);
                glGetIntegerv(GL_ACTIVE_TEXTURE,&previousActive); glActiveTexture(GL_TEXTURE0);
                glGetIntegerv(GL_TEXTURE_BINDING_2D,&previousTexture);
                glGetIntegeri_v(GL_SAMPLER_BINDING,0,&previousSampler);
                glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&previousStorage);
                output=RenderBackend::UploadBindings::Capture(GL_SHADER_STORAGE_BUFFER_BINDING,
                    GL_SHADER_STORAGE_BUFFER_START,GL_SHADER_STORAGE_BUFFER_SIZE,3);
            }
            ~Objects()
            {
                glUseProgram(previousProgram); glBindTexture(GL_TEXTURE_2D,previousTexture); glBindSampler(0,previousSampler);
                glActiveTexture(previousActive);
                RenderBackend::UploadBindings::Restore(GL_SHADER_STORAGE_BUFFER,3,output);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER,previousStorage);
                glDeleteBuffers(2,buffers); glDeleteTextures(1,&texture);
                if(program) glDeleteProgram(program); if(shader) glDeleteShader(shader);
            }
        } objects;
        std::string source="#version 450 core\n";
        source+=RenderBackend::MaterialBlock;
        source+=RenderBackend::MappingFunctions;
        source+=R"(
layout(local_size_x=1) in;
layout(std430,binding=3) buffer Results { vec4 result; };
layout(binding=0) uniform sampler2D fixture;
uniform vec3 surfaceNormal;
uniform float strength, greenSign;
uniform bool scalarProbe;
void main() { result=scalarProbe ? geSampleSurface(fixture,vec2(.17,.31),vec3(.17,.31,.47),surfaceNormal)
    : vec4(geTriplanarNormal(fixture,vec3(.17,.31,.47),surfaceNormal,strength,greenSign),1); }
)";
        objects.shader=glCreateShader(GL_COMPUTE_SHADER);
        const char* text=source.c_str(); glShaderSource(objects.shader,1,&text,nullptr); glCompileShader(objects.shader);
        GLint okay{}; glGetShaderiv(objects.shader,GL_COMPILE_STATUS,&okay);
        if(!okay) { char log[4096]{}; glGetShaderInfoLog(objects.shader,sizeof(log),nullptr,log);
            Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_09_FAIL projection GLSL {}",log); return false; }
        objects.program=glCreateProgram(); glAttachShader(objects.program,objects.shader); glLinkProgram(objects.program);
        glGetProgramiv(objects.program,GL_LINK_STATUS,&okay); if(!okay) return false;
        glCreateBuffers(2,objects.buffers);
        std::array<std::uint32_t,12> header{};
        header[6]=1; header[8]=1; header[9]=std::bit_cast<std::uint32_t>(1.f);
        glNamedBufferData(objects.buffers[0],sizeof(header),header.data(),GL_DYNAMIC_DRAW);
        glNamedBufferData(objects.buffers[1],sizeof(glm::vec4),nullptr,GL_DYNAMIC_READ);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,objects.buffers[0]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,objects.buffers[1]);
        glCreateTextures(GL_TEXTURE_2D,1,&objects.texture);
        glTextureStorage2D(objects.texture,1,GL_RGBA32F,1,1);
        glTextureParameteri(objects.texture,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTextureParameteri(objects.texture,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glBindTextureUnit(0,objects.texture); glBindSampler(0,0); glUseProgram(objects.program);
        const glm::vec3 normals[]{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},
            glm::normalize(glm::vec3(1,2,3)),glm::normalize(glm::vec3(-3,2,-1))};
        const glm::vec3 tangent[]{{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}};
        const glm::vec3 bitangent[]{{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}};
        std::size_t cases=0;
        for(float sharpness:{1.f,4.f,8.f}) for(float sign:{-1.f,1.f})
            for(unsigned fixture=0;fixture<3;++fixture) for(std::size_t i=0;i<std::size(normals);++i)
            {
                // Directional fixture has both channels; a flat map and zero
                // strength independently recover the original surface normal.
                const glm::vec4 pixel=fixture==0 ? glm::vec4(.5,.5,1,1) : glm::vec4(.7,.65,.9,1);
                const float strength=fixture==2 ? 0.f : 1.f;
                header[10]=std::bit_cast<std::uint32_t>(sharpness);
                glNamedBufferSubData(objects.buffers[0],0,sizeof(header),header.data());
                glTextureSubImage2D(objects.texture,0,0,0,1,1,GL_RGBA,GL_FLOAT,&pixel);
                glUniform3fv(glGetUniformLocation(objects.program,"surfaceNormal"),1,&normals[i].x);
                glUniform1f(glGetUniformLocation(objects.program,"strength"),strength);
                glUniform1f(glGetUniformLocation(objects.program,"greenSign"),sign);
                glUniform1ui(glGetUniformLocation(objects.program,"geMaterialOffset"),0);
                glDispatchCompute(1,1,1); glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
                glm::vec4 result; glGetNamedBufferSubData(objects.buffers[1],0,sizeof(result),&result);
                glm::vec3 expected=normals[i];
                if(fixture==1 && i<6) expected=glm::normalize(tangent[i]*.4f+bitangent[i]*(.3f*sign)+normals[i]*.8f);
                if(!std::isfinite(result.x+result.y+result.z) || std::abs(glm::length(glm::vec3(result))-1)>2e-5f ||
                    ((fixture!=1 || i<6) && glm::length(glm::vec3(result)-expected)>2e-5f) ||
                    glm::dot(glm::vec3(result),normals[i])<=0) return false;
                ++cases;
            }
        // Independent four-texel references distinguish all signed projections,
        // scalar channels and mapped alpha. UV must still consume authored tiling.
        glDeleteTextures(1,&objects.texture);
        glCreateTextures(GL_TEXTURE_2D,1,&objects.texture);
        glTextureStorage2D(objects.texture,1,GL_RGBA32F,2,2);
        glTextureParameteri(objects.texture,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTextureParameteri(objects.texture,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        const glm::vec4 pixels[]{{0,.1,.2,0},{.25,.3,.4,1},{.5,.6,.7,1},{.75,.8,.9,0}};
        glTextureSubImage2D(objects.texture,0,0,0,2,2,GL_RGBA,GL_FLOAT,pixels);
        glBindTextureUnit(0,objects.texture);
        glUniform1i(glGetUniformLocation(objects.program,"scalarProbe"),1);
        header[4]=header[5]=std::bit_cast<std::uint32_t>(2.f);
        unsigned scalarCases=0;
        for(unsigned mapping:{0u,1u}) for(float sharpness:{1.f,4.f,8.f}) for(const auto& n:normals)
        {
            header[8]=mapping; header[10]=std::bit_cast<std::uint32_t>(sharpness);
            glNamedBufferSubData(objects.buffers[0],0,sizeof(header),header.data());
            glUniform3fv(glGetUniformLocation(objects.program,"surfaceNormal"),1,&n.x);
            glDispatchCompute(1,1,1); glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            glm::vec4 actual; glGetNamedBufferSubData(objects.buffers[1],0,sizeof(actual),&actual);
            const glm::vec3 raw=glm::pow(glm::abs(n),glm::vec3(sharpness));
            const glm::vec3 weights=raw/(raw.x+raw.y+raw.z);
            const glm::vec4 expected=mapping ? pixels[n.x>=0?1:0]*weights.x+
                pixels[n.y>=0?2:0]*weights.y+pixels[n.z>=0?0:1]*weights.z : pixels[2];
            if(glm::any(glm::greaterThan(glm::abs(actual-expected),glm::vec4(2e-5f)))) return false;
            ++scalarCases;
        }
        if(glGetError()!=GL_NO_ERROR) return false;
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_PROJECTION_PASS cases={} tolerance=0.00002 flat=true directional=true signs=6 sharpness=1,4,8",cases);
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_SCALAR_PASS cases={} channels=RGBA mapping=UV,Triplanar tolerance=0.00002",scalarCases);
        return true;
    }

    bool MaterialCoverageGpuChecks(::GEngine::SceneRenderResources& owner, ::GEngine::Asset::MeshHandle plane)
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Component;
        constexpr unsigned size=128;
        auto scene=CreateRefPtr<_Scene>();
        auto object=scene->CreateEntity("Phase09 coverage plane");
        auto cameraEntity=scene->CreateEntity("Phase09 coverage camera");
        auto directional=scene->CreateEntity("Phase09 coverage directional");
        auto point=scene->CreateEntity("Phase09 coverage point");
        if(!object || !cameraEntity || !directional || !point) return false;
        cameraEntity->AddOrReplaceComponent<RenderCameraComponent>();
        RenderLightComponent light; light.castShadows=true;
        directional->AddOrReplaceComponent<RenderLightComponent>(light);
        directional->Transform().QuatRotation=glm::rotation(glm::vec3(0,0,-1),glm::normalize(glm::vec3(0,-1,-1)));
        light.kind=RenderLightKind::Point; light.range=20;
        point->AddOrReplaceComponent<RenderLightComponent>(light);
        point->Transform().SetTranslation({0,3,2});
        auto identity=scene->RenderData().Identify(*cameraEntity); if(!identity) return false;
        const FrameCamera camera{*identity,glm::lookAt(glm::vec3(0,3,2),glm::vec3(0),glm::vec3(0,1,0)),
            glm::perspective(glm::radians(45.f),1.f,.1f,20.f),{0,3,2},0,0,size,size};
        RenderTargetDesc colorDescription;
        colorDescription.Storage.Width=colorDescription.Storage.Height=size;
        colorDescription.Storage.Samples=1;
        colorDescription.Storage.ColorCount=1;
        colorDescription.Storage.Colors[0]=FramebufferFormat::RGBA8;
        colorDescription.Storage.Depth=FramebufferFormat::Depth24;
        colorDescription.Storage.DepthRenderbuffer=false;
        auto target=RenderTarget::Create(colorDescription);
        auto pick=MousePickFrameBuffer::Create(size,size);
        auto cascade=CascadeShadowFrameBuffer::Create(size,size,4);
        auto pointTarget=PointShadowFrameBuffer::Create(size,size);
        auto submitter=FrameSubmission::Create();
        if(!target || !pick || !cascade || !pointTarget || !submitter) return false;
        const auto* fixture=std::getenv("GENGINE_PRE_EDITOR_MATERIAL_ALPHA_FIXTURE");
        if(!fixture) return false;
        auto image=Manager::AssetsManager::LoadTexture(fixture); if(!image) return false;
        Asset::SamplerDesc nearest; nearest.minFilter=nearest.magFilter=Asset::SamplerFilter::Nearest;
        auto sampler=Manager::AssetsManager::GetSampler(nearest); if(!sampler) return false;
        MaterialAuthoringDesc desc; desc.kind=MaterialKind::Masked; desc.doubleSided=true; desc.uvTiling={2,2};
        const float splits[]{2,4,8,12,20};
        std::array<std::size_t,4> solid{};
        unsigned cases=0;
        auto depthMask=[](const Asset::AttachmentView& view,unsigned layers)->std::optional<std::vector<float>> {
            auto name=Asset::AssetDetail::TextureBackend::Name(Asset::TextureView(view)); if(!name) return {};
            // The observer owns its transfer layout; restore all caller state.
            constexpr GLenum keys[]{GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS,
                GL_PACK_IMAGE_HEIGHT,GL_PACK_SKIP_IMAGES,GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST};
            std::array<GLint,8> saved{}; GLint pack{},active{},cube{};
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack); glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
            for(unsigned i=0;i<saved.size();++i) { glGetIntegerv(keys[i],&saved[i]); glPixelStorei(keys[i],i==0?1:0); }
            glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&cube);
            std::vector<float> values(size*size*layers);
            bool readOk=true;
            if(layers==6)
            {
                // Validation observer only. Never change the production DRAW
                // framebuffer/attachment or clear/write the observed texture.
                GLint previousRead{},previousDraw{};
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead);
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previousDraw);
                GLuint observer{};
                glGenFramebuffers(1,&observer);
                if(observer)
                {
                    glBindFramebuffer(GL_READ_FRAMEBUFFER,observer);
                    glReadBuffer(GL_NONE);
                    for(unsigned face=0;face<6 && readOk;++face)
                    {
                        const GLenum target=GL_TEXTURE_CUBE_MAP_POSITIVE_X+face;
                        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,target,*name,0);
                        GLint object{},type{},level{},layered{},actualFace{};
                        glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&object);
                        glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&type);
                        glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL,&level);
                        glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_LAYERED,&layered);
                        glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE,&actualFace);
                        readOk=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE
                            && object==static_cast<GLint>(*name) && type==GL_TEXTURE && level==0
                            && layered==0 && actualFace==static_cast<GLint>(target);
                        if(readOk)
                            glReadPixels(0,0,size,size,GL_DEPTH_COMPONENT,GL_FLOAT,values.data()+face*size*size);
                    }
                    glBindFramebuffer(GL_READ_FRAMEBUFFER,previousRead);
                    glDeleteFramebuffers(1,&observer);
                }
                else readOk=false;
                GLint restoredRead{},unchangedDraw{};
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&restoredRead);
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&unchangedDraw);
                readOk=readOk && restoredRead==previousRead && unchangedDraw==previousDraw;
            }
            else glGetTextureImage(*name,0,GL_DEPTH_COMPONENT,GL_FLOAT,GLsizei(values.size()*sizeof(float)),values.data());
            glBindTexture(GL_TEXTURE_CUBE_MAP,cube); glActiveTexture(active);
            glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);
            for(unsigned i=0;i<saved.size();++i) glPixelStorei(keys[i],saved[i]);
            for(GLenum error;(error=glGetError())!=GL_NO_ERROR;)
            {
                Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_09_FAIL depth observer GL error {}",error);
                readOk=false;
            }
            if(!readOk) return {};
            return values;
        };
        {
            auto view=pointTarget->DepthView(); if(!view) return false;
            auto name=Asset::AssetDetail::TextureBackend::Name(Asset::TextureView(*view)); if(!name) return false;
            // Before rendering, calibrate every face and texel address with
            // distinct binary-exact fixture values, then restore the original
            // all-face .75 initialization below. This is upload, never a clear.
            std::vector<float> stamp(size*size);
            constexpr GLenum unpackKeys[]{GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_PIXELS,
                GL_UNPACK_SKIP_ROWS,GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_IMAGES,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST};
            std::array<GLint,8> unpackValues{}; GLint unpack{},active{},cube{};
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
            for(unsigned i=0;i<unpackValues.size();++i)
            { glGetIntegerv(unpackKeys[i],&unpackValues[i]); glPixelStorei(unpackKeys[i],i==0?1:0); }
            glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&cube); glBindTexture(GL_TEXTURE_CUBE_MAP,*name);
            for(unsigned face=0;face<6;++face)
            {
                for(unsigned i=0;i<stamp.size();++i) stamp[i]=float(1+face*size*size+i)/(8.f*size*size);
                glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,0,0,size,size,GL_DEPTH_COMPONENT,GL_FLOAT,stamp.data());
            }
            glBindTexture(GL_TEXTURE_CUBE_MAP,cube); glActiveTexture(active);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);
            for(unsigned i=0;i<unpackValues.size();++i) glPixelStorei(unpackKeys[i],unpackValues[i]);
            auto spatial=depthMask(*view,6); if(!spatial) return false;
            std::size_t addressMismatch{};
            for(unsigned i=0;i<spatial->size();++i) addressMismatch+=(*spatial)[i]!=float(i+1)/(8.f*size*size);
            if(addressMismatch)
            {
                Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_09_FAIL point observer calibration mismatches={}",addressMismatch);
                return false;
            }
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_POINT_OBSERVER_CALIBRATION_PASS observer=per_face_read_fbo faces=6 values={} mismatches=0",spatial->size());
            const float known=.75f;
            glClearTexImage(*name,0,GL_DEPTH_COMPONENT,GL_FLOAT,&known);
            auto separate=depthMask(*view,6); if(!separate) return false;
            std::vector<float> bulk(size*size*6);
            glGetTextureImage(*name,0,GL_DEPTH_COMPONENT,GL_FLOAT,GLsizei(bulk.size()*sizeof(float)),bulk.data());
            std::size_t faceMismatch{},bulkMismatch{};
            for(unsigned i=0;i<bulk.size();++i) { faceMismatch+=(*separate)[i]!=known; bulkMismatch+=bulk[i]!=known; }
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_READBACK_CALIBRATION expected=0.75 face_mismatch={} bulk_mismatch={}",faceMismatch,bulkMismatch);
            if(faceMismatch || glGetError()!=GL_NO_ERROR) return false;
        }
        for(unsigned scenario=0;scenario<4;++scenario)
        {
            desc.opacity=scenario==1 ? .2f : 1.f;
            desc.mapping=scenario==3 ? TextureMappingMode::Triplanar : TextureMappingMode::UV;
            if(scenario>=2) desc.textures[0]=MaterialTextureValue{*image,*sampler};
            auto material=owner.CreateMaterial(desc);
            if(!material || !owner.AssignRenderable(*object,{plane,*material})) return false;
            auto access=owner.Publication().BeginFrame(); RenderExtractionStats extraction;
            auto frame=ExtractRenderFrame(*scene,owner.ForFrame(access),extraction,{&camera,1});
            if(!frame) return false;
            FrameSubmissionDesc targets{*target,*pick,*pointTarget,*cascade,owner.Pipelines(),splits,
                glm::radians(45.f),1,.1f,20,.1f,20};
            EntityPickTable table;
            auto result=submitter->Submit(*frame,targets,table);
            if(!result) { Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_09_FAIL coverage submission {}",DescribeSubmissionError(result.error())); return false; }
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_COVERAGE_DECISIONS scenario={} point_lights={} point_requested={} point_executed={} point_draws={}",
                scenario,frame->PointLights().size(),result->decisions[1].requested,result->decisions[1].executed,result->shadowVisibility[1].total.submittedCasters);
            auto colorView=target->Buffer().DepthView(); auto dirView=cascade->DepthView(); auto pointView=pointTarget->DepthView();
            if(!colorView) { Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_09_FAIL color depth view {}",colorView.error().message); return false; }
            if(!dirView) { Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_09_FAIL directional depth view {}",dirView.error().message); return false; }
            if(!pointView) { Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_09_FAIL point depth view {}",pointView.error().message); return false; }
            auto color=depthMask(*colorView,1), dir=depthMask(*dirView,5), pt=depthMask(*pointView,6);
            if(!color || !dir || !pt) return false;
            std::array<std::size_t,4> counts{};
            for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x)
            {
                auto pixel=pick->ReadPixel(x,y); if(!pixel) return false;
                const bool colorCovered=(*color)[y*size+x]<1, pickCovered=*pixel!=-1;
                if(colorCovered!=pickCovered) return false;
                counts[0]+=colorCovered; counts[1]+=pickCovered;
            }
            for(float value:*dir) counts[2]+=value<1;
            for(float value:*pt) counts[3]+=value<1;
            for(unsigned face=0;face<6;++face) {
                std::size_t covered{}; float minimum=1,maximum=0;
                for(unsigned i=face*size*size;i<(face+1)*size*size;++i) { covered+=(*pt)[i]<1; minimum=(std::min)(minimum,(*pt)[i]); maximum=(std::max)(maximum,(*pt)[i]); }
                Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_POINT_FACE scenario={} face={} covered={} min={} max={}",scenario,face,covered,minimum,maximum);
            }
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_COVERAGE scenario={} color={} picking={} directional={} point={}",scenario,counts[0],counts[1],counts[2],counts[3]);
            if(scenario==0) { solid=counts; for(auto count:counts) if(count<16) return false; }
            else if(scenario==1) { for(auto count:counts) if(count) return false; }
            else for(unsigned i=0;i<4;++i) if(counts[i]==0 || counts[i]>=solid[i]) return false;
            ++cases;
        }
        if(glGetError()!=GL_NO_ERROR) return false;
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_COVERAGE_PASS cases={} color_picking_pixel_agreement=true directional_point_partial_coverage=true",cases);
        return true;
    }

    bool MaterialPackedHeaderChecks(const ::GEngine::RenderFrame& frame)
    {
        using namespace ::GEngine;
        std::vector<ScenePipeline> roles;
        for(const auto& resource:frame.Resources())
            roles.push_back({resource.Material().Source()->Declaration()->Pipeline(),SceneMaterialKind::Lit,1,1,true});
        auto packed=RenderBackend::MaterialBatch::Pack(frame,roles,1024*1024);
        if(!packed) return false;
        for(std::size_t i=0;i<frame.Resources().size();++i) {
            const auto* words=packed->words.get()+packed->offsets[i]*4;
            if(!(words[6]==1 && words[7]==31 && words[8]==1 && std::bit_cast<float>(words[9])==2 &&
                std::bit_cast<float>(words[10])==8 && std::bit_cast<float>(words[11])==.8f)) return false;
        }
        return true;
    }
#else
    inline std::expected<void, ::GEngine::PlatformError>
    CheckMaterialAuthoring(::GEngine::SceneRenderResources& owner, ::GEngine::_Scene& active,
                           ::GEngine::Asset::MeshHandle mesh, ::GEngine::MaterialHandle regularFloor,
                           ::GEngine::Asset::MeshHandle plane)
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Component;
        std::size_t checks=0;
        auto require=[&](bool condition,const char* name) {
            ++checks; if(!condition) Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_09_FAIL {}",name); return condition; };
#define MAT09_REQUIRE(condition,name) \
        if(!require(bool(condition),name)) return std::unexpected(PlatformError{PlatformErrorCode::Initialization,"Phase 09 material authoring",name})
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_BEGIN");
        const auto physicsCount=active.GetAllEntitiesWith<RigidBody3DComponent>().size();
        auto scene=CreateRefPtr<_Scene>();
        auto entity=scene->CreateEntity("Material authoring check");
        MAT09_REQUIRE(entity,"test entity");
        // The owner-retained presentation uses rusted iron. Keep the required
        // regular-floor fixture explicit rather than treating it as that input.
        auto sphereEntity=active.FindEntityByName("Phase08 Regenerated");
        auto floorEntity=active.FindEntityByName("wood_plane");
        MAT09_REQUIRE(sphereEntity && floorEntity,"retained sphere and floor sources exist");
        const auto sphereMaterial=sphereEntity.GetComponent<MeshRendererComponent>().material;
        auto sphereDesc=owner.DescribeMaterial(sphereMaterial);
        auto galleryDesc=owner.DescribeMaterial(regularFloor);
        MAT09_REQUIRE(sphereDesc && galleryDesc && *sphereDesc==*galleryDesc,
            "retained gallery clones sphereMaterial complete state");
        MAT09_REQUIRE(sphereDesc->roughness==.9f && sphereDesc->metallic==1.f &&
            sphereDesc->uvTiling[0]==1.f && sphereDesc->uvTiling[1]==1.f &&
            sphereDesc->dielectricReflectance[0]==.8f && sphereDesc->dielectricReflectance[1]==.8f &&
            sphereDesc->dielectricReflectance[2]==.8f,"retained rusted-iron material factors");
        for(const char* name:{"Template Sphere Smooth","Template Sphere Flat","Template Cylinder Smooth",
            "Template Cylinder Flat","Template Cone Smooth","Template Cone Flat","Template Capsule Smooth",
            "Template Capsule Flat","Template Torus Smooth","Template Torus Flat","Template Diamond Smooth",
            "Template Diamond Flat","Phase08 Regenerated"}) {
            auto item=active.FindEntityByName(name);
            MAT09_REQUIRE(item && item.GetComponent<MeshRendererComponent>().material==sphereMaterial,
                "retained template and regenerated assignments use sphereMaterial");
        }
        MaterialHandle triGallery;
        for(const char* name:{"Plane","Cube","Sphere","Capsule","Torus","Diamond"}) {
            auto uv=active.FindEntityByName(std::string(name)+" UV");
            auto tri=active.FindEntityByName(std::string(name)+" Triplanar");
            MAT09_REQUIRE(uv && tri,"six retained comparison pairs exist");
            const auto& u=uv.GetComponent<MeshRendererComponent>();
            const auto& t=tri.GetComponent<MeshRendererComponent>();
            MAT09_REQUIRE(u.mesh==t.mesh && u.material==regularFloor,"pair shares exact mesh and UV material");
            auto td=owner.DescribeMaterial(t.material);
            auto expected=*galleryDesc; expected.mapping=TextureMappingMode::Triplanar;
            MAT09_REQUIRE(td && *td==expected,"retained A/B material differs only by mapping");
            if(triGallery) MAT09_REQUIRE(triGallery==t.material,"shared Triplanar material");
            triGallery=t.material;
            if(std::string_view(name)=="Sphere") {
                auto actual=owner.GeometrySource(u.mesh);
                MAT09_REQUIRE(actual && actual->source->VertexCount()==2143 && actual->source->IndexCount()==11904,
                    "Sphere 64 by 32 actual published geometry");
                Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_SPHERE_DEFAULT_PASS segments=64 rings=32 vertices={} indices={}",
                    actual->source->VertexCount(),actual->source->IndexCount());
            }
        }
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_RETAINED_INPUTS_PASS template_assignments=12 regenerated=1 pairs=6 material=rustediron");
        auto floor=owner.DescribeMaterial(floorEntity.GetComponent<MeshRendererComponent>().material);
        MAT09_REQUIRE(floor && floor->mapping==TextureMappingMode::UV,"UV default migration");
        for(const auto& binding:floor->textures) MAT09_REQUIRE(binding,"full regular floor five maps");
        const auto geometry=owner.GeometrySource(mesh);
        MAT09_REQUIRE(geometry,"retained geometry input");
        const auto geometryWork=owner.GeometryWork();
        const std::vector<std::byte> vertices(geometry->source->Vertices().begin(),geometry->source->Vertices().end());
        MaterialAuthoringDesc edited=*floor;
        edited.mapping=TextureMappingMode::Triplanar; edited.projectionScale.value=2; edited.blendSharpness.value=8;
        edited.baseColor={.3f,.6f,.9f,.8f}; edited.normalStrength=.7f; edited.emissive={.02f,.03f,.04f};
        auto source=owner.CreateMaterial(edited);
        MAT09_REQUIRE(source,"edited material source");
        Asset::SamplerDesc nearest;
        nearest.minFilter=nearest.magFilter=Asset::SamplerFilter::Nearest;
        auto sampler=Manager::AssetsManager::GetSampler(nearest);
        MAT09_REQUIRE(sampler,"alternate sampler prepared outside measurement");
        const auto warmed=owner.MaterialWork().programCreations;
        {
            MaterialGpuCalls observed;
            auto clone=owner.CloneMaterial(*source); auto second=owner.CloneMaterial(*source);
            MAT09_REQUIRE(clone && second && *clone!=*source && *second!=*source && *clone!=*second,"Clone fresh material identities");
            auto clonedState=owner.DescribeMaterial(*clone);
            MAT09_REQUIRE(clonedState && *clonedState==edited,"Clone complete edited semantic state");
            MAT09_REQUIRE(owner.AssignRenderable(*entity,{mesh,*source}),"source assignment");
            auto duplicate=entity->Duplicate();
            MAT09_REQUIRE(duplicate && duplicate->GetComponent<MeshRendererComponent>().material==*source,"Entity Duplicate shares MaterialHandle");
            MAT09_REQUIRE(owner.AssignRenderable(*duplicate,{mesh,*clone}),"explicit Clone assignment");
            std::optional<RenderFrame> oldFrame;
            {
                auto access=owner.Publication().BeginFrame(); RenderExtractionStats stats;
                auto frame=ExtractRenderFrame(*scene,owner.ForFrame(access),stats);
                MAT09_REQUIRE(frame && frame->Draws().size()==2,"retained frame");
                oldFrame.emplace(std::move(*frame));
            }
            const auto& first=oldFrame->Resources()[oldFrame->Draws()[0].resources].Material();
            const auto& other=oldFrame->Resources()[oldFrame->Draws()[1].resources].Material();
            MAT09_REQUIRE(first.Source()->Template()==other.Source()->Template() && first.Program().Identity()==other.Program().Identity(),"Clone shares immutable declaration and program");
            for(std::size_t i=0;i<first.Textures().size();++i)
                MAT09_REQUIRE(first.Textures()[i].texture.Identity()==other.Textures()[i].texture.Identity() &&
                    first.Textures()[i].sampler.Identity()==other.Textures()[i].sampler.Identity(),"Clone shares texture and sampler resources");
            auto changed=edited; changed.roughness=.67f;
            MAT09_REQUIRE(owner.EditMaterial(*clone,changed) && *owner.DescribeMaterial(*source)==edited,"clone scalar independent");
            changed.mapping=TextureMappingMode::UV;
            MAT09_REQUIRE(owner.EditMaterial(*clone,changed) && *owner.DescribeMaterial(*source)==edited,"clone mapping independent");
            changed.textures[0]=edited.textures[3];
            MAT09_REQUIRE(owner.EditMaterial(*clone,changed) && *owner.DescribeMaterial(*source)==edited,"clone texture independent");
            changed.textures[0]->sampler=*sampler;
            MAT09_REQUIRE(owner.EditMaterial(*clone,changed) && *owner.DescribeMaterial(*source)==edited,"clone sampler independent");
            auto sourceChange=edited; sourceChange.opacity=.4f; sourceChange.textures[4].reset();
            MAT09_REQUIRE(owner.EditMaterial(*source,sourceChange) && *owner.DescribeMaterial(*clone)==changed &&
                *owner.DescribeMaterial(*second)==edited,"source edits independent in reverse");
            auto noOp=owner.EditMaterial(*clone,changed);
            MAT09_REQUIRE(noOp && !*noOp,"same-value edit is no publication");
            auto equivalent=owner.CreateMaterial(edited);
            MAT09_REQUIRE(equivalent && owner.MaterialWork().programCreations==warmed && observed.Empty(),"warm authoring has zero shader or texture GPU work");
            MAT09_REQUIRE(first.Source()->Textures().size()==5 && first.PublicationRevision()==1 && other.PublicationRevision()==1,
                "old frame retains original exact material versions");
            MAT09_REQUIRE(MaterialPackedHeaderChecks(*oldFrame),"actual packed mapped coverage header");
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_RESOURCE_WORK_PASS compiles={} links={} texture_names={} texture_storage={} texture_uploads={}",
                observed.compiles,observed.links,observed.textureNames,observed.textureStorage,observed.textureUploads);
        }
        const auto beforeInvalid=*owner.DescribeMaterial(*source);
        for(float value:{-.1f,0.f,1024.1f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
            auto bad=beforeInvalid; bad.projectionScale.value=value;
            MAT09_REQUIRE(!owner.EditMaterial(*source,bad) && *owner.DescribeMaterial(*source)==beforeInvalid,"invalid projection preserves material");
        }
        for(float value:{0.f,8.1f,std::numeric_limits<float>::infinity()}) {
            auto bad=beforeInvalid; bad.blendSharpness.value=value;
            MAT09_REQUIRE(!owner.EditMaterial(*source,bad),"invalid sharpness");
        }
        for(float scale:{.001f,1024.f}) for(float sharpness:{1.f,8.f}) {
            auto endpoint=beforeInvalid; endpoint.projectionScale.value=scale; endpoint.blendSharpness.value=sharpness;
            MAT09_REQUIRE(owner.EditMaterial(*source,endpoint),"projection endpoints");
        }
        auto bad=beforeInvalid; bad.mapping=static_cast<TextureMappingMode>(42);
        MAT09_REQUIRE(!owner.EditMaterial(*source,bad),"invalid mapping enum");
        bad=beforeInvalid; bad.textures[1]=floor->textures[0];
        MAT09_REQUIRE(!owner.EditMaterial(*source,bad),"sRGB Normal rejected");
        auto stale=*source; ++stale.generation; auto foreign=*source; ++foreign.registry;
        MAT09_REQUIRE(!owner.CloneMaterial(stale) && !owner.CloneMaterial(foreign) && !owner.CloneMaterial({}),"stale foreign null material rejection");
        MAT09_REQUIRE(!owner.SetMaterialTexture(*source,static_cast<MaterialTextureSemantic>(99),{}),"unknown texture role rejection");
        {
            auto access=owner.Publication().BeginFrame();
            MAT09_REQUIRE(!owner.CloneMaterial(*source) && !owner.EditMaterial(*source,beforeInvalid),"publication safe point enforced");
        }
        for(unsigned kind=0;kind<5;++kind) {
            MaterialAuthoringDesc desc; desc.kind=static_cast<MaterialKind>(kind);
            auto material=owner.CreateMaterial(desc);
            MAT09_REQUIRE(material && *owner.DescribeMaterial(*material)==desc,"five typed kinds and absent map constants");
            auto access=owner.Publication().BeginFrame(); auto instance=owner.Materials().Acquire(access,*material);
            MAT09_REQUIRE(instance,"kind instance lease");
            const auto& declaration=*(*instance)->Declaration();
            const auto alpha=desc.kind==MaterialKind::Masked ? AlphaMode::Masked : desc.kind==MaterialKind::Transparent ? AlphaMode::Transparent : AlphaMode::Opaque;
            MAT09_REQUIRE(declaration.State().Alpha()==alpha && (desc.kind!=MaterialKind::Transparent || !declaration.State().Depth().write),"alpha pipeline agreement");
            auto typedError=**instance;
            MAT09_REQUIRE(!typedError.SetParameter("roughnessScale",std::array<float,2>{1,1}),"advanced type mismatch rejection");
        }
        bad={}; bad.kind=MaterialKind::Unlit; bad.textures[1]=floor->textures[1];
        MAT09_REQUIRE(!owner.CreateMaterial(bad),"Unlit lit-only role rejected");
        bad={}; bad.kind=MaterialKind::Debug; bad.mapping=TextureMappingMode::Triplanar;
        MAT09_REQUIRE(!owner.CreateMaterial(bad),"Debug mapping rejected");
        MAT09_REQUIRE(!SceneRenderResources::SupportsLocalProjection({},1),"missing required geometry rejected");
        auto after=owner.GeometrySource(mesh);
        MAT09_REQUIRE(after && after->source==geometry->source && after->geometry.revision==geometry->geometry.revision &&
            std::ranges::equal(vertices,after->source->Vertices()) && owner.GeometryWork().sourceRecords==geometryWork.sourceRecords &&
            owner.GeometryWork().sourcePayloadBytes==geometryWork.sourcePayloadBytes,"mapping leaves geometry identity payload UV and cache unchanged");
        MAT09_REQUIRE(active.GetAllEntitiesWith<RigidBody3DComponent>().size()==physicsCount,"Physics membership unchanged");
        MAT09_REQUIRE(MaterialProjectionGpuChecks(),"flat and directional GPU normal projection");
        MAT09_REQUIRE(MaterialCoverageGpuChecks(owner,plane),"mapped alpha through color picking and shadow passes");
        const bool floorComparison=std::getenv("GENGINE_PRE_EDITOR_MATERIAL_FLOOR_COMPARISON")!=nullptr;
        if(floorComparison) {
            // Opt-in validation fixture only. Ordinary startup retains all owner
            // assignments. The separate retained-presentation run never enters here.
            auto triFloor=*floor; triFloor.mapping=TextureMappingMode::Triplanar;
            MAT09_REQUIRE(owner.EditMaterial(regularFloor,*floor) && owner.EditMaterial(triGallery,triFloor),
                "explicit regular-floor comparison fixture installed");
            MAT09_REQUIRE(*owner.DescribeMaterial(regularFloor)==*floor &&
                *owner.DescribeMaterial(triGallery)==triFloor,"five-map floor pair differs only by mapping");
        }
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_COMPARISON_INPUT_PASS material={} uv_tiling={},{} scale={} sharpness={} normal_strength={}",
            floorComparison ? "base_white_tile" : "rustediron",
            floorComparison ? floor->uvTiling[0] : galleryDesc->uvTiling[0],
            floorComparison ? floor->uvTiling[1] : galleryDesc->uvTiling[1],
            galleryDesc->projectionScale.value,galleryDesc->blendSharpness.value,galleryDesc->normalStrength);
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_09_API_PASS checks={} programs={} templates={}",checks,
            owner.MaterialWork().programCreations,owner.MaterialWork().templates);
#undef MAT09_REQUIRE
        return {};
    }
#endif
}
