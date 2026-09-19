#include "gepch.h"
#include "Renderer/FrameSubmission.h"
#include "Core/RenderTarget.h"
#include "Core/RuntimeAssets.h"
#include "Core/GLContextThread.h"
#include "Core/RenderCounters.h"
#include "../Assets/ShaderBackend.h"
#include <format>
#include <new>

namespace GEngine
{
    namespace
    {
        using Asset::Shader;
        using Code = SubmissionCode;
        std::unexpected<SubmissionError> Error(const char* op, Code code)
        { return std::unexpected(SubmissionError{op, code}); }
        GLint Location(const Shader& shader, const char* name)
        { return glGetUniformLocation(Asset::ShaderBackendAccess::Program(shader), name); }
        void Uniform(const Shader& s, const char* n, int v) { glUniform1i(Location(s, n), v); }
        void Uniform(const Shader& s, const char* n, unsigned v) { glUniform1ui(Location(s, n), v); }
        void Uniform(const Shader& s, const char* n, bool v) { Uniform(s, n, int(v)); }
        void Uniform(const Shader& s, const char* n, float v) { glUniform1f(Location(s, n), v); }
        void Uniform(const Shader& s, const char* n, const glm::vec2& v) { glUniform2fv(Location(s, n), 1, glm::value_ptr(v)); }
        void Uniform(const Shader& s, const char* n, const glm::vec3& v) { glUniform3fv(Location(s, n), 1, glm::value_ptr(v)); }
        void Uniform(const Shader& s, const char* n, const glm::vec4& v) { glUniform4fv(Location(s, n), 1, glm::value_ptr(v)); }
        void Uniform(const Shader& s, const char* n, const glm::mat4& v) { glUniformMatrix4fv(Location(s, n), 1, GL_FALSE, glm::value_ptr(v)); }

        struct TargetRestore
        {
            GLint draw{}, read{}, viewport[4]{}, program{}, uniformBuffer{};
            TargetRestore()
            {
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
                glGetIntegerv(GL_VIEWPORT, viewport);
                glGetIntegerv(GL_CURRENT_PROGRAM, &program);
                glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &uniformBuffer);
            }
            ~TargetRestore()
            {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
                glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
                glUseProgram(program);
                glBindBufferBase(GL_UNIFORM_BUFFER, 0, uniformBuffer);
            }
        };
        std::expected<Shader, SubmissionError> CoverageProgram(RenderPass pass)
        {
            const bool picking=pass==RenderPass::Picking, point=pass==RenderPass::PointShadow;
            const std::string vertex=std::string(R"(#version 450 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
uniform mat4 u_model, u_view, u_projection;
out vec2 vertexUV;
void main() { vertexUV=uv; gl_Position=)") + (picking?"u_projection*u_view*":"") + "u_model*vec4(position,1.0); }";
            const std::string geometry=point?R"(#version 450 core
layout(triangles) in;
layout(triangle_strip,max_vertices=18) out;
uniform mat4 shadowMatrices[6];
in vec2 vertexUV[];
out vec2 fragmentUV;
out vec4 FragPos;
void main() { for(int face=0;face<6;++face) {
  for(int i=0;i<3;++i) { gl_Layer=face; FragPos=gl_in[i].gl_Position;
    fragmentUV=vertexUV[i]; gl_Position=shadowMatrices[face]*FragPos; EmitVertex(); }
  EndPrimitive(); } }
)":R"(#version 450 core
layout(triangles,invocations=5) in;
layout(triangle_strip,max_vertices=3) out;
layout(std140,binding=0) uniform LightSpaceMatrices { mat4 lightSpaceMatrices[16]; };
in vec2 vertexUV[];
out vec2 fragmentUV;
void main() { for(int i=0;i<3;++i) { gl_Layer=gl_InvocationID;
  fragmentUV=vertexUV[i]; gl_Position=lightSpaceMatrices[gl_InvocationID]*gl_in[i].gl_Position;
  EmitVertex(); } EndPrimitive(); }
)";
            std::string fragment="#version 450 core\n";
            fragment+=picking?"in vec2 vertexUV;\n#define fragmentUV vertexUV\nlayout(location=0) out int pixel; uniform int u_EntityID;\n":"in vec2 fragmentUV;\n";
            if(point) fragment+="in vec4 FragPos; uniform vec3 lightPos; uniform float far_plane;\n";
            fragment+=R"(
uniform bool frameMasked;
uniform sampler2D frameCoverage;
uniform vec2 frameTiling;
uniform float frameAlphaCutoff, frameOpacity;
void main() {
  if(frameMasked && texture(frameCoverage,fragmentUV*frameTiling).a*frameOpacity < frameAlphaCutoff) discard;
)";
            if(picking) fragment+="pixel=u_EntityID;";
            if(point) fragment+="gl_FragDepth=length(FragPos.xyz-lightPos)/far_plane;";
            fragment+="}";
            std::array<Asset::ShaderSource,3> stages{{{Asset::ShaderStage::Vertex,vertex,"frame coverage vertex"},
                {Asset::ShaderStage::Fragment,fragment,"frame coverage fragment"},
                {Asset::ShaderStage::Geometry,geometry,"frame coverage geometry"}}};
            auto result=Shader::Create({std::span(stages).first(picking?2:3)});
            if(!result) return std::unexpected(SubmissionError{"coverage shader",result.error()});
            return std::move(*result);
        }
        GLenum Compare(DepthCompare value)
        {
            switch(value) {
            case DepthCompare::Never:return GL_NEVER; case DepthCompare::Less:return GL_LESS;
            case DepthCompare::Equal:return GL_EQUAL; case DepthCompare::LessEqual:return GL_LEQUAL;
            case DepthCompare::Greater:return GL_GREATER; case DepthCompare::NotEqual:return GL_NOTEQUAL;
            case DepthCompare::GreaterEqual:return GL_GEQUAL; case DepthCompare::Always:return GL_ALWAYS;
            }
            Asset::AssetDetail::RequireInvariant(false); return GL_LESS;
        }
        GLenum Factor(BlendFactor value)
        {
            switch(value) { case BlendFactor::Zero:return GL_ZERO; case BlendFactor::One:return GL_ONE;
            case BlendFactor::SourceAlpha:return GL_SRC_ALPHA; case BlendFactor::OneMinusSourceAlpha:return GL_ONE_MINUS_SRC_ALPHA; }
            Asset::AssetDetail::RequireInvariant(false); return GL_ONE;
        }
        void Toggle(GLenum capability,bool enabled) { if(enabled) glEnable(capability); else glDisable(capability); }
        void Apply(const RenderPassDesc& pass)
        {
            glViewport(pass.viewport.x,pass.viewport.y,pass.viewport.width,pass.viewport.height);
            Toggle(GL_SCISSOR_TEST,pass.scissor); glScissor(pass.viewport.x,pass.viewport.y,pass.viewport.width,pass.viewport.height);
            Toggle(GL_DEPTH_TEST,pass.depthTest); glDepthMask(pass.depthWrite); glDepthFunc(Compare(pass.depthCompare));
            glColorMask(pass.colorWrite,pass.colorWrite,pass.colorWrite,pass.colorWrite);
            glDisable(GL_STENCIL_TEST); glStencilMask(pass.stencilWrite?~0u:0u);
            Toggle(GL_BLEND,pass.blend); glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
            Toggle(GL_CULL_FACE,pass.cull!=CullMode::None); glCullFace(pass.cull==CullMode::Front?GL_FRONT:GL_BACK);
            glFrontFace(GL_CCW); glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
            glDisable(GL_RASTERIZER_DISCARD); glDisable(GL_POLYGON_OFFSET_FILL);
            glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE); glDisable(GL_SAMPLE_COVERAGE); glDisable(GL_LINE_SMOOTH);
            glDisable(GL_SAMPLE_MASK); glDisable(GL_DEPTH_CLAMP); glDisable(GL_FRAMEBUFFER_SRGB);
            Toggle(GL_DITHER,pass.pass!=RenderPass::Picking);
            glEnable(GL_MULTISAMPLE); glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
            glDepthRange(0,1); glClearDepth(1); glClearColor(.1f,.1f,.1f,1.f);
            GLbitfield clear{};
            if(pass.colorLoad==PassLoad::Clear) clear|=GL_COLOR_BUFFER_BIT;
            if(pass.depthLoad==PassLoad::Clear) clear|=GL_DEPTH_BUFFER_BIT;
            if(clear) glClear(clear);
        }
        glm::mat4 LightMatrix(const FrameCamera& camera, const FrameSubmissionDesc& desc,
            glm::vec3 direction, float nearPlane, float farPlane)
        {
            // Identical cascade fitting and z expansion to the old serial path.
            const auto inverse = glm::inverse(glm::perspective(desc.cameraFov, desc.cameraAspect,
                nearPlane, farPlane) * camera.view);
            std::array<glm::vec4, 8> corners;
            glm::vec3 center(0.f);
            std::size_t i = 0;
            for (int x = 0; x != 2; ++x) for (int y = 0; y != 2; ++y) for (int z = 0; z != 2; ++z)
            {
                auto p = inverse * glm::vec4(2.f*x-1, 2.f*y-1, 2.f*z-1, 1);
                corners[i] = p / p.w; center += glm::vec3(corners[i++]);
            }
            center /= 8.f;
            const auto view = glm::lookAt(center + direction, center, glm::vec3(0,1,0));
            glm::vec3 lo((std::numeric_limits<float>::max)()), hi((std::numeric_limits<float>::lowest)());
            for (const auto& p : corners) { const glm::vec3 v(view*p); lo = glm::min(lo,v); hi = glm::max(hi,v); }
            lo.z = lo.z < 0 ? lo.z * 10.f : lo.z / 10.f;
            hi.z = hi.z < 0 ? hi.z / 10.f : hi.z * 10.f;
            return glm::ortho(lo.x, hi.x, lo.y, hi.y, lo.z, hi.z) * view;
        }
        std::array<glm::mat4,6> PointMatrices(glm::vec3 p, float nearPlane, float farPlane, float aspect)
        {
            const auto projection = glm::perspective(glm::radians(90.f), aspect, nearPlane, farPlane);
            const glm::vec3 directions[]{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            const glm::vec3 up[]{{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
            std::array<glm::mat4,6> result;
            for (std::size_t i=0; i<6; ++i) result[i] = projection * glm::lookAt(p,p+directions[i],up[i]);
            return result;
        }
        void CameraUniforms(const Shader& shader, const FrameCamera& camera, bool sky = false)
        {
            Uniform(shader, "u_view", sky ? glm::mat4(glm::mat3(camera.view)) : camera.view);
            Uniform(shader, "u_projection", camera.projection);
            Uniform(shader, "viewPos", camera.worldPosition);
        }
        std::expected<void, SubmissionError> MaterialUniforms(const PreparedMaterialBinding& material)
        {
            const auto& shader = *material.Program();
            const auto& source = *material.Source();
            const auto parameters = source.Declaration()->Parameters();
            for (std::size_t i=0; i<parameters.size(); ++i)
            {
                const char* name = parameters[i].declaration.name.c_str();
                std::visit([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::same_as<T,std::array<float,2>>) Uniform(shader,name,glm::make_vec2(value.data()));
                    else if constexpr (std::same_as<T,std::array<float,3>>) Uniform(shader,name,glm::make_vec3(value.data()));
                    else if constexpr (std::same_as<T,std::array<float,4>>) Uniform(shader,name,glm::make_vec4(value.data()));
                    else if constexpr (std::same_as<T,std::array<float,16>>) Uniform(shader,name,glm::make_mat4(value.data()));
                    else Uniform(shader,name,value);
                }, source.Values()[i]);
            }
            const auto slots = source.Declaration()->Textures();
            for (std::size_t i=0; i<slots.size(); ++i)
            {
                const auto& texture = material.Textures()[i];
                const auto unit = slots[i].bindingIndex;
                if (auto bound = Asset::TextureView(texture.texture).Bind(unit); !bound)
                    return std::unexpected(SubmissionError{"material texture", bound.error()});
                if (auto bound = texture.sampler->Bind(unit); !bound)
                    return std::unexpected(SubmissionError{"material sampler", bound.error()});
                Uniform(shader, slots[i].declaration.name.c_str(), int(unit));
            }
            return {};
        }
        struct PreparedDraw
        {
            const DrawItem* draw{};
            const FrameResources* resources{};
            const ScenePipeline* role{};
            std::size_t submesh{};
            int pixel = -1;
        };
        std::expected<void, SubmissionError> Draw(const PreparedDraw& draw, const Shader& shader)
        {
            Uniform(shader,"u_model",draw.draw->worldTransform);
            Uniform(shader,"u_EntityID",draw.pixel);
            const auto primitive = draw.role->kind == SceneMaterialKind::Helper ? MeshPrimitive::Lines : MeshPrimitive::Triangles;
            auto result = draw.resources->Mesh()->DrawSubmesh(draw.submesh, primitive);
            if (!result) return std::unexpected(SubmissionError{"mesh submission",result.error()});
            return {};
        }
    }
    struct FrameSubmission::Storage
    {
        Shader point, cascade, pick;
        GLuint matrices{};
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        std::thread::id owner = std::this_thread::get_id();
        ~Storage()
        {
            GLContextThread::RequireOwner(owner,"frame submission destruction");
            Asset::AssetDetail::RequireInvariant(SDL_GL_GetCurrentContext() == context);
            if (matrices) glDeleteBuffers(1,&matrices);
        }
    };
    FrameSubmission::FrameSubmission(FrameSubmission&&) noexcept = default;
    FrameSubmission& FrameSubmission::operator=(FrameSubmission&&) noexcept = default;
    FrameSubmission::~FrameSubmission() = default;

    std::string DescribeSubmissionError(const SubmissionError& error)
    {
        return error.operation + ": " + std::visit([](const auto& cause)->std::string {
            using T=std::decay_t<decltype(cause)>;
            if constexpr (std::is_enum_v<T>) return std::format("domain={} code={}",
                std::same_as<T,SubmissionCode> ? "submission" : "render-ecs",static_cast<int>(cause));
            else if constexpr (std::same_as<T,PlatformError>) return std::format("platform code={} operation={} message={}",static_cast<int>(cause.code),cause.operation,cause.message);
            else if constexpr (std::same_as<T,Asset::ShaderError>) return std::format("shader code={} stage={} source={} log={}",
                static_cast<int>(cause.code), cause.shaderType ? static_cast<int>(*cause.shaderType) : 0,cause.source,cause.log);
            else if constexpr (std::same_as<T,GpuMeshError>) return std::format("mesh code={} element={} registry={} message={}",
                static_cast<int>(cause.code),cause.element,static_cast<int>(cause.registry),cause.message);
            else if constexpr (std::same_as<T,FramebufferError>) return std::format("framebuffer code={} width={} height={} samples={} message={}",
                static_cast<int>(cause.code),cause.width,cause.height,cause.samples,cause.message);
            else if constexpr (std::same_as<T,Asset::TextureError>) return std::format("texture code={} source={} system={} registry={} message={}",
                static_cast<int>(cause.code),cause.source,cause.system.message(),static_cast<int>(cause.registry),cause.message);
            else if constexpr (std::same_as<T,Asset::SamplerError>) return std::format("sampler code={} registry={} message={}",static_cast<int>(cause.code),static_cast<int>(cause.registry),cause.message);
            else return std::format("visibility code={} element={}",int(cause.code),cause.element);
        },error.cause);
    }
    std::expected<FrameSubmission, SubmissionError> FrameSubmission::Create()
    {
        if (!GLContextThread::IsCurrentOwner()) return Error("create submission",Code::Context);
        FrameSubmission result;
        result.m_Storage.reset(new (std::nothrow) Storage);
        if (!result.m_Storage) return Error("submission storage",Code::Allocation);
        auto point = CoverageProgram(RenderPass::PointShadow);
        if (!point) return std::unexpected(point.error());
        auto cascade = CoverageProgram(RenderPass::DirectionalShadow);
        if (!cascade) return std::unexpected(cascade.error());
        auto pick = CoverageProgram(RenderPass::Picking);
        if (!pick) return std::unexpected(pick.error());
        auto& storage=*result.m_Storage;
        storage.point=std::move(*point); storage.cascade=std::move(*cascade); storage.pick=std::move(*pick);
        glCreateBuffers(1,&storage.matrices);
        if (!storage.matrices) return Error("shadow matrix allocation",Code::Allocation);
        glNamedBufferData(storage.matrices,16*sizeof(glm::mat4),nullptr,GL_DYNAMIC_DRAW);
        if (const auto error=glGetError(); error!=GL_NO_ERROR)
            return std::unexpected(SubmissionError{std::format("shadow matrix storage: driver diagnostic 0x{:x}",error),Code::Driver});
        return result;
    }

    RenderPassDesc FrameSubmission::DescribePass(RenderPass pass,const FrameSubmissionDesc& desc)
    {
        RenderPassDesc result;
        result.pass=pass;
        const auto& target=desc.color.Description().Storage;
        result.viewport={0,0,target.Width,target.Height};
        result.output=PassTarget::SceneColor;
        result.depthTest=true; result.depthWrite=true; result.cull=CullMode::Back;
        switch(pass)
        {
        case RenderPass::DirectionalShadow: {
            const auto& size=desc.cascadeShadow.Buffer().Description();
            result.viewport={0,0,size.Width,size.Height}; result.output=PassTarget::DirectionalDepth;
            result.colorWrite=false; result.depthLoad=PassLoad::Clear; result.cull=CullMode::Front; break;
        }
        case RenderPass::PointShadow: {
            const auto& size=desc.pointShadow.Buffer().Description();
            result.viewport={0,0,size.Width,size.Height}; result.output=PassTarget::PointDepth;
            result.colorWrite=false; result.depthLoad=PassLoad::Clear; break;
        }
        case RenderPass::Picking:
            result.output=PassTarget::Picking; result.colorLoad=PassLoad::Clear; result.depthLoad=PassLoad::Clear; break;
        case RenderPass::Opaque:
            result.colorLoad=PassLoad::Clear; result.depthLoad=PassLoad::Clear; [[fallthrough]];
        case RenderPass::Masked:
            result.input=PassTarget::DirectionalDepth; result.secondaryInput=PassTarget::PointDepth;
            result.materialOverrides=true; break;
        case RenderPass::Skybox:
            result.depthCompare=DepthCompare::LessEqual; result.depthWrite=false; result.materialOverrides=true; break;
        case RenderPass::Transparent:
            result.input=PassTarget::DirectionalDepth; result.secondaryInput=PassTarget::PointDepth;
            result.depthWrite=false; result.blend=true; result.materialOverrides=true; break;
        case RenderPass::Debug: result.materialOverrides=true; break;
        case RenderPass::Resolve:
            result.input=PassTarget::SceneColor; result.output=PassTarget::ResolvedColor;
            result.depthTest=false; result.depthWrite=false; result.cull=CullMode::None; break;
        case RenderPass::EditorUI:
            result.input=PassTarget::ResolvedColor; result.output=PassTarget::Window;
            result.depthTest=false; result.depthWrite=false; result.blend=true; result.cull=CullMode::None;
            result.boundary=PassBoundary::EstablishBeforeUI; break;
        case RenderPass::Present:
            result.input=PassTarget::Window; result.output=PassTarget::Window;
            result.depthTest=false; result.depthWrite=false; result.colorWrite=false; result.cull=CullMode::None;
            result.boundary=PassBoundary::Presentation; break;
        case RenderPass::LegacyScene:
            result.boundary=PassBoundary::RestoreAfterLegacy; break;
        }
        return result;
    }

    std::expected<FrameSubmissionStats, SubmissionError> FrameSubmission::Submit(
        const RenderFrame& frame,const FrameSubmissionDesc& desc,EntityPickTable& picks)
    {
        if (!m_Storage || !GLContextThread::IsCurrentOwner() || SDL_GL_GetCurrentContext()!=m_Storage->context)
            return Error("submit",Code::Context);
        if (frame.Cameras().size()!=1) return Error("submission camera count",Code::InvalidCamera);
        if (!frame.DebugLines().empty()) return Error("debug lines are outside this scene submission layer",Code::UnsupportedPipeline);
        if (frame.DirectionalLights().size()>1 || frame.PointLights().size()>1 || !frame.SpotLights().empty())
            return Error("legacy lighting capacity (one directional, one point, no spot)",Code::UnsupportedLights);
        const auto& camera=frame.Cameras()[0];
        const auto& target=desc.color.Description().Storage;
        const auto* directional=frame.DirectionalLights().empty()?nullptr:&frame.DirectionalLights()[0];
        const auto* point=frame.PointLights().empty()?nullptr:&frame.PointLights()[0];
        const bool directionalShadow=directional && directional->shadows.castShadows;
        const bool pointShadow=point && point->shadows.castShadows;
        const auto& pickTarget=desc.picking.Buffer().Description();
        if (!desc.color || (desc.pickingEnabled && (!desc.picking || pickTarget.Width!=target.Width || pickTarget.Height!=target.Height))
            || (directionalShadow && !desc.cascadeShadow) || (pointShadow && !desc.pointShadow)
            || target.ColorCount!=1 || target.Colors[0]!=FramebufferFormat::RGBA8 || target.Depth==FramebufferFormat::None
            || (directionalShadow && desc.cascadeShadow.Buffer().Description().Layers<5)
            || camera.viewportX || camera.viewportY || camera.viewportWidth!=target.Width || camera.viewportHeight!=target.Height)
            return Error("submission required target or dimensions",Code::InvalidTarget);
        if (desc.cascadeSplits.size()!=5 || desc.cascadeSplits.back()!=desc.cameraFar
            || !std::isfinite(desc.cameraFov) || desc.cameraFov<=0 || desc.cameraFov>=glm::pi<float>() || !std::isfinite(desc.cameraAspect)
            || desc.cameraAspect<=0 || !std::isfinite(desc.cameraNear) || desc.cameraNear<=0
            || !std::isfinite(desc.cameraFar) || desc.cameraFar<=desc.cameraNear
            || !std::isfinite(desc.pointNear) || desc.pointNear<=0 || !std::isfinite(desc.pointFar) || desc.pointFar<=desc.pointNear)
            return Error("shadow projection",Code::InvalidShadowSettings);
        float previous=desc.cameraNear;
        for(float split:desc.cascadeSplits) {
            if(!std::isfinite(split) || split<=previous || split>desc.cameraFar) return Error("cascade splits",Code::InvalidShadowSettings);
            previous=split;
        }
        const float pointFar=point?point->range:desc.pointFar;
        if(pointFar<=desc.pointNear) return Error("point shadow range",Code::InvalidShadowSettings);
        const glm::vec3 lightDirection=directional?-directional->direction:glm::vec3(0,0,1);
        if(directionalShadow && std::abs(glm::dot(lightDirection,glm::vec3(0,1,0)))>.99999f)
            return Error("cascade up vector",Code::InvalidShadowSettings);
        const glm::vec3 position=point?point->position:glm::vec3(0,15,-10);
        const auto count=frame.Draws().size();
        std::unique_ptr<PreparedDraw[]> draws(new (std::nothrow) PreparedDraw[count]);
        std::unique_ptr<std::size_t[]> conservative(new (std::nothrow) std::size_t[count]);
        if(count && (!draws || !conservative)) return Error("submission metadata",Code::Allocation);
        std::size_t conservativeCount{};
        for(std::size_t index=0;index<count;++index)
        {
            const auto& draw=frame.Draws()[index];
            const ScenePipeline* role=nullptr;
            for(const auto& entry:desc.pipelines) if(entry.pipeline==draw.pipeline) {role=&entry;break;}
            if(!role) return Error("unregistered scene pipeline",Code::UnsupportedPipeline);
            if(draw.resources>=frame.Resources().size()) return Error("draw resources",Code::InvalidDraw);
            const auto& resources=frame.Resources()[draw.resources];
            const auto& pipeline=resources.Material().Pipeline().Description();
            if(!pipeline.depthTest || pipeline.polygon!=PolygonMode::Fill || pipeline.frontFace!=FrontFace::CounterClockwise
                || (pipeline.cull!=CullMode::Back && pipeline.cull!=CullMode::None)
                || (pipeline.depthCompare!=DepthCompare::Less && pipeline.depthCompare!=DepthCompare::LessEqual)
                || !std::isfinite(role->lineWidth) || role->lineWidth<=0 || !std::isfinite(role->opacity) || role->opacity<0 || role->opacity>1
                || (role->kind!=SceneMaterialKind::Lit && pipeline.alpha!=AlphaMode::Opaque))
                return Error("unsupported scene pipeline state",Code::UnsupportedPipeline);
            // Alpha coverage is a semantic material contract across all passes.
            if(pipeline.alpha!=AlphaMode::Opaque) {
                bool coverage=false;
                for(const auto& slot:resources.Material().Source()->Declaration()->Textures())
                    if(slot.declaration.name=="albedoMap") coverage=true;
                if(!coverage) return Error("alpha material needs albedoMap coverage",Code::UnsupportedPipeline);
            }
            auto& prepared=draws[index]; prepared={&draw,&resources,role};
            bool found=false;
            const auto ranges=resources.Mesh()->Submeshes();
            for(std::size_t i=0;i<ranges.size();++i)
                if(ranges[i].firstElement==draw.submesh.firstElement && ranges[i].elementCount==draw.submesh.elementCount
                    && ranges[i].materialSlot==draw.submesh.materialSlot) {prepared.submesh=i;found=true;break;}
            if(!found) return Error("draw submesh",Code::InvalidDraw);
            if(role->kind==SceneMaterialKind::Sky) conservative[conservativeCount++]=index;
        }
        auto visibility=RenderVisibility::Build(frame,0,{conservative.get(),conservativeCount});
        if(!visibility) return std::unexpected(SubmissionError{"frame visibility",visibility.error()});
        EntityPickTable candidatePicks;
        if(desc.pickingEnabled) for(auto index:visibility->Picking()) {
            auto pixel=candidatePicks.Encode(draws[index].draw->entity);
            if(!pixel) return std::unexpected(SubmissionError{"pick encoding",pixel.error()});
            draws[index].pixel=*pixel;
        }
        // Sort only frame-local ordinals; retained frame storage remains immutable.
        std::unique_ptr<std::size_t[]> transparent(new (std::nothrow) std::size_t[visibility->Transparent().size()]);
        if(!transparent && !visibility->Transparent().empty()) return Error("transparent ordering",Code::Allocation);
        std::copy(visibility->Transparent().begin(),visibility->Transparent().end(),transparent.get());
        auto depth=[&](std::size_t i) {return (camera.view*draws[i].draw->worldTransform*glm::vec4(0,0,0,1)).z;};
        if(visibility->Transparent().size()>1) std::sort(transparent.get(),transparent.get()+visibility->Transparent().size(),
            [&](auto a,auto b){const auto za=depth(a),zb=depth(b);return za==zb?a<b:za<zb;});
        TargetRestore restore;
        auto& storage=*m_Storage;
        FrameSubmissionStats stats; stats.visibility=visibility->Stats();
        auto begin=[&](RenderPass pass) {
            auto contract=DescribePass(pass,desc);
            if(contract.input==PassTarget::DirectionalDepth && !directionalShadow) contract.input=PassTarget::None;
            if(contract.secondaryInput==PassTarget::PointDepth && !pointShadow) contract.secondaryInput=PassTarget::None;
            stats.trace.Pass(contract);
            // Signed picking attachment uses its typed clear operation below.
            if(pass==RenderPass::Picking) contract.colorLoad=PassLoad::Load;
            Apply(contract);
        };
        auto coverage=[&](const PreparedDraw& draw,const Shader& shader)->std::expected<void,SubmissionError> {
            const auto& material=draw.resources->Material();
            const auto& pipeline=material.Pipeline();
            const bool masked=pipeline.Alpha()==AlphaMode::Masked;
            Uniform(shader,"frameMasked",masked);
            if(!masked) return {};
            Uniform(shader,"frameAlphaCutoff",pipeline.Description().alphaCutoff);
            Uniform(shader,"frameOpacity",draw.role->opacity);
            glm::vec2 tiling(1);
            auto parameters=material.Source()->Declaration()->Parameters();
            for(std::size_t i=0;i<parameters.size();++i) if(parameters[i].declaration.name=="u_tiling")
                if(auto* value=std::get_if<std::array<float,2>>(&material.Source()->Values()[i])) tiling=glm::make_vec2(value->data());
            Uniform(shader,"frameTiling",tiling);
            auto slots=material.Source()->Declaration()->Textures();
            for(std::size_t i=0;i<slots.size();++i) if(slots[i].declaration.name=="albedoMap") {
                if(auto bound=Asset::TextureView(material.Textures()[i].texture).Bind(0);!bound) return std::unexpected(SubmissionError{"coverage texture",bound.error()});
                if(auto bound=material.Textures()[i].sampler->Bind(0);!bound) return std::unexpected(SubmissionError{"coverage sampler",bound.error()});
                Uniform(shader,"frameCoverage",0); break;
            }
            return {};
        };
        auto shadowDraws=[&](const Shader& shader)->std::expected<void,SubmissionError> {
            RenderCounters::RecordPass(RenderCounters::Pass::Shadow);
            for(auto index:visibility->Shadows()) if(draws[index].role->kind==SceneMaterialKind::Lit) {
                if(auto result=coverage(draws[index],shader);!result) return result;
                if(auto result=Draw(draws[index],shader);!result) return result;
                ++stats.shadowDraws;
            }
            return {};
        };
        if(directionalShadow)
        {
            storage.cascade.Bind(); desc.cascadeShadow.Bind(); begin(RenderPass::DirectionalShadow);
            glBindBufferBase(GL_UNIFORM_BUFFER,0,storage.matrices);
            std::array<glm::mat4,5> matrices;
            previous=desc.cameraNear;
            for(std::size_t i=0;i<matrices.size();++i) {matrices[i]=LightMatrix(camera,desc,lightDirection,previous,desc.cascadeSplits[i]);previous=desc.cascadeSplits[i];}
            glNamedBufferSubData(storage.matrices,0,sizeof(matrices),matrices.data());
            if(auto result=shadowDraws(storage.cascade);!result) return std::unexpected(result.error());
        }
        if(pointShadow)
        {
            storage.point.Bind(); desc.pointShadow.Bind(); begin(RenderPass::PointShadow);
            const auto& size=desc.pointShadow.Buffer().Description();
            const auto matrices=PointMatrices(position,desc.pointNear,pointFar,float(size.Width)/size.Height);
            for(std::size_t i=0;i<matrices.size();++i) Uniform(storage.point,std::format("shadowMatrices[{}]",i).c_str(),matrices[i]);
            Uniform(storage.point,"lightPos",position); Uniform(storage.point,"far_plane",pointFar);
            if(auto result=shadowDraws(storage.point);!result) return std::unexpected(result.error());
        }
        if(desc.pickingEnabled)
        {
            storage.pick.Bind(); desc.picking.Bind(); begin(RenderPass::Picking);
            RenderCounters::RecordPass(RenderCounters::Pass::Picking);
            if(auto result=desc.picking.ClearAttachment(0,-1);!result) return std::unexpected(SubmissionError{"clear picking",result.error()});
            CameraUniforms(storage.pick,camera);
            for(auto index:visibility->Picking()) {
                if(auto result=coverage(draws[index],storage.pick);!result) return std::unexpected(result.error());
                // Match the authored color geometry's culling policy.
                Toggle(GL_CULL_FACE,draws[index].resources->Material().Pipeline().Description().cull!=CullMode::None);
                if(auto result=Draw(draws[index],storage.pick);!result) return std::unexpected(result.error());
                ++stats.pickDraws;
            }
        }
        const auto colorDraw=[&](std::size_t index,RenderPass pass)->std::expected<void,SubmissionError> {
            const auto& prepared=draws[index]; const auto kind=prepared.role->kind;
            const bool sky=kind==SceneMaterialKind::Sky, helper=kind==SceneMaterialKind::Helper || kind==SceneMaterialKind::PointLight;
            if((pass==RenderPass::Skybox)!=sky || (pass==RenderPass::Debug)!=helper) return {};
            if(kind==SceneMaterialKind::PointLight && (!point || point->entity!=prepared.draw->entity)) return {};
            const auto& material=prepared.resources->Material(); const auto& shader=*material.Program();
            const auto& pipeline=material.Pipeline(); const auto blend=pipeline.Blend();
            shader.Bind();
            Toggle(GL_CULL_FACE,pipeline.Description().cull!=CullMode::None);
            glCullFace(GL_BACK); glDepthFunc(Compare(pipeline.Depth().compare));
            glDepthMask(!sky && pipeline.Depth().write); Toggle(GL_BLEND,blend.enabled);
            glBlendFuncSeparate(Factor(blend.sourceColor),Factor(blend.destinationColor),Factor(blend.sourceAlpha),Factor(blend.destinationAlpha));
            CameraUniforms(shader,camera,sky);
            if(kind==SceneMaterialKind::Lit) Uniform(shader,"u_tiling",glm::vec2(1));
            if(auto result=MaterialUniforms(material);!result) return result;
            Uniform(shader,"frameAlphaMode",int(pipeline.Alpha()));
            Uniform(shader,"frameAlphaCutoff",pipeline.Description().alphaCutoff);
            Uniform(shader,"frameOpacity",prepared.role->opacity);
            Uniform(shader,"framePremultiplied",pipeline.Description().transparentBlend==TransparentBlend::PremultipliedAlpha);
            if(kind==SceneMaterialKind::Lit)
            {
                const auto unit=static_cast<unsigned>(material.Textures().size());
                if(directionalShadow) {
                    auto view=desc.cascadeShadow.DepthView(); if(!view) return std::unexpected(SubmissionError{"cascade view",view.error()});
                    if(auto result=Asset::TextureView(*view).Bind(unit);!result) return std::unexpected(SubmissionError{"cascade binding",result.error()});
                } else { glActiveTexture(GL_TEXTURE0+unit); glBindTexture(GL_TEXTURE_2D_ARRAY,0); }
                if(pointShadow) {
                    auto view=desc.pointShadow.DepthView(); if(!view) return std::unexpected(SubmissionError{"point view",view.error()});
                    if(auto result=Asset::TextureView(*view).Bind(unit+1);!result) return std::unexpected(SubmissionError{"point binding",result.error()});
                } else { glActiveTexture(GL_TEXTURE0+unit+1); glBindTexture(GL_TEXTURE_CUBE_MAP,0); }
                glBindSampler(unit,0); glBindSampler(unit+1,0);
                Uniform(shader,"shadowMap",int(unit)); Uniform(shader,"pointShadowDepthMap",int(unit+1));
                Uniform(shader,"lightDir",lightDirection); Uniform(shader,"lightPos",position);
                Uniform(shader,"directionallightColor",directional?directional->color*directional->intensity:glm::vec3(0));
                Uniform(shader,"pointlightColor",point?point->color*point->intensity:glm::vec3(0));
                Uniform(shader,"frameLights",true); Uniform(shader,"frameDirectional",bool(directional)); Uniform(shader,"framePoint",bool(point));
                Uniform(shader,"frameDirectionalShadows",directionalShadow && prepared.draw->receiveShadows);
                Uniform(shader,"framePointShadows",pointShadow && prepared.draw->receiveShadows);
                Uniform(shader,"farPlane",desc.cameraFar); Uniform(shader,"pointShadowfarPlane",pointFar);
                Uniform(shader,"cascadeCount",int(desc.cascadeSplits.size())); Uniform(shader,"reverse_normals",false);
                for(std::size_t j=0;j<desc.cascadeSplits.size();++j) Uniform(shader,std::format("cascadePlaneDistances[{}]",j).c_str(),desc.cascadeSplits[j]);
            }
            Toggle(GL_LINE_SMOOTH,kind==SceneMaterialKind::Helper);
            if(kind==SceneMaterialKind::Helper) glLineWidth(prepared.role->lineWidth);
            if(kind==SceneMaterialKind::PointLight) Uniform(shader,"pointlightColor",point->color*point->intensity);
            if(auto result=Draw(prepared,shader);!result) return result;
            if(sky) ++stats.skyDraws; else if(helper) ++stats.helperDraws; else {
                ++stats.colorDraws;
                if(pass==RenderPass::Opaque) ++stats.opaqueDraws;
                if(pass==RenderPass::Masked) ++stats.maskedDraws;
                if(pass==RenderPass::Transparent) ++stats.transparentDraws;
            }
            return {};
        };
        desc.color.Bind();
        for(auto pass:{RenderPass::Opaque,RenderPass::Masked,RenderPass::Skybox,RenderPass::Transparent,RenderPass::Debug})
        {
            begin(pass);
            auto indices=pass==RenderPass::Opaque?visibility->Opaque():pass==RenderPass::Masked?visibility->Masked():
                pass==RenderPass::Transparent?std::span<const std::size_t>(transparent.get(),visibility->Transparent().size()):visibility->Main();
            for(auto index:indices) if(auto result=colorDraw(index,pass);!result) return std::unexpected(result.error());
        }
        glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDisable(GL_BLEND);
        if(const auto error=glGetError();error!=GL_NO_ERROR)
            return std::unexpected(SubmissionError{std::format("frame submission: driver diagnostic 0x{:x}",error),Code::Driver});
        picks=std::move(candidatePicks); // A skipped picking pass invalidates last frame's lookup.
        return stats;
    }
}
