#include "gepch.h"
#include "Renderer/FrameSubmission.h"
#include "Renderer/PassTiming.h"
#include "Core/RenderTarget.h"
#include "Core/RuntimeAssets.h"
#include "Core/GLContextThread.h"
#include "Core/RenderCounters.h"
#include "../Assets/ShaderBackend.h"
#include "../Assets/TextureBackend.h"
#include "GLStateCache.h"
#include "DrawOrdering.h"
#include "SubmissionUploads.h"
#include <format>
#include <new>
#include <bit>

namespace GEngine
{
    namespace
    {
        using Asset::Shader;
        using State = RenderBackend::GLStateCache;
        using Code = SubmissionCode;
        std::unexpected<SubmissionError> Error(const char* op, Code code)
        { return std::unexpected(SubmissionError{op, code}); }
        void BindProgram(const Shader& shader)
        {
            Asset::ShaderBackendAccess::RequireBindable(shader);
            State::Get().Program(Asset::ShaderBackendAccess::Program(shader));
        }
        GLint Location(const Shader& shader, const char* name)
        { return glGetUniformLocation(Asset::ShaderBackendAccess::Program(shader), name); }
        void Uniform(const Shader& s, const char* n, int v) { glUniform1i(Location(s, n), v); }
        void Uniform(const Shader& s, const char* n, unsigned v) { glUniform1ui(Location(s, n), v); }
        void Uniform(const Shader& s, const char* n, bool v) { Uniform(s, n, int(v)); }
        void Uniform(const Shader& s, const char* n, const glm::mat4& v) { glUniformMatrix4fv(Location(s, n), 1, GL_FALSE, glm::value_ptr(v)); }

        struct TargetRestore
        {
            GLint draw{}, read{}, viewport[4]{}, program{}, vao{};
            TargetRestore()
            {
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
                glGetIntegerv(GL_VIEWPORT, viewport);
                glGetIntegerv(GL_CURRENT_PROGRAM, &program);
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
            }
            ~TargetRestore()
            {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
                glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
                glUseProgram(program);
                glBindVertexArray(vao);
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
            std::array<std::string,3> packed;
            for(std::size_t i=0;i<(picking?2u:3u);++i) {
                auto adapted=RenderBackend::PackedStage(std::string(stages[i].source),false);
                if(!adapted) return Error("coverage packed shader",Code::UnsupportedPipeline);
                packed[i]=std::move(*adapted);stages[i].source=packed[i];
            }
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
        void Toggle(GLenum capability,bool enabled) { State::Get().Toggle(capability,enabled); }
        void Apply(const RenderPassDesc& pass)
        {
            State::Get().Viewport(pass.viewport.x,pass.viewport.y,pass.viewport.width,pass.viewport.height);
            Toggle(GL_SCISSOR_TEST,pass.scissor); State::Get().Scissor(pass.viewport.x,pass.viewport.y,pass.viewport.width,pass.viewport.height);
            Toggle(GL_DEPTH_TEST,pass.depthTest); State::Get().DepthMask(pass.depthWrite); State::Get().DepthFunc(Compare(pass.depthCompare));
            State::Get().ColorMask(pass.colorWrite,pass.colorWrite,pass.colorWrite,pass.colorWrite);
            Toggle(GL_STENCIL_TEST,false); State::Get().StencilMask(pass.stencilWrite?~0u:0u);
            Toggle(GL_BLEND,pass.blend); State::Get().BlendEquation(GL_FUNC_ADD,GL_FUNC_ADD);
            State::Get().BlendFunction(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
            Toggle(GL_CULL_FACE,pass.cull!=CullMode::None); State::Get().CullFace(pass.cull==CullMode::Front?GL_FRONT:GL_BACK);
            State::Get().FrontFace(GL_CCW); State::Get().Polygon(GL_FILL);
            Toggle(GL_RASTERIZER_DISCARD,false); Toggle(GL_POLYGON_OFFSET_FILL,false);
            Toggle(GL_POLYGON_OFFSET_LINE,false); Toggle(GL_POLYGON_OFFSET_POINT,false);
            Toggle(GL_SAMPLE_ALPHA_TO_COVERAGE,false); Toggle(GL_SAMPLE_COVERAGE,false); Toggle(GL_LINE_SMOOTH,false);
            Toggle(GL_SAMPLE_MASK,false); Toggle(GL_DEPTH_CLAMP,false); Toggle(GL_FRAMEBUFFER_SRGB,false);
            Toggle(GL_DITHER,pass.pass!=RenderPass::Picking);
            Toggle(GL_MULTISAMPLE,true); Toggle(GL_TEXTURE_CUBE_MAP_SEAMLESS,true);
            State::Get().DepthRange(0,1); State::Get().ClearDepth(1); State::Get().ClearColor(.1f,.1f,.1f,1.f);
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
        std::expected<void, Asset::TextureError> BindTexture(const Asset::TextureView& texture, unsigned unit)
        {
            auto name=Asset::AssetDetail::TextureBackend::Name(texture);
            if(!name) return std::unexpected(name.error());
            auto& state=State::Get();
            if(unit>=static_cast<unsigned>(state.textureUnits))
                return std::unexpected(Asset::TextureError{Asset::TextureErrorCode::InvalidUnit,{},"Texture unit exceeds the context limit"});
            state.Texture(unit,Asset::AssetDetail::TextureBackend::Target(texture),*name);
            return {};
        }
        std::expected<void, SubmissionError> MaterialTextures(const PreparedMaterialBinding& material)
        {
            const auto& shader = *material.Program();
            const auto& source = *material.Source();
            const auto slots = source.Declaration()->Textures();
            for (std::size_t i=0; i<slots.size(); ++i)
            {
                const auto& texture = material.Textures()[i];
                const auto unit = slots[i].bindingIndex;
                if (auto bound = BindTexture(Asset::TextureView(texture.texture),unit); !bound)
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
        // Exact scalar sequences, split by cause. No padding, hashes, native
        // names, addresses, live scene references or retained heavy resources.
        struct PassInputs
        {
            std::unique_ptr<std::uint64_t[]> words;
            std::array<std::size_t,8> ends{};
            bool valid = false;
            PassDirtyReason pending = PassDirtyReason::None;
            PassDirtyReason Changes(const PassInputs& next) const
            {
                auto reasons=pending;
                if(!valid) return reasons | PassDirtyReason::InitialContent;
                std::size_t a{},b{};
                for(std::size_t i=0;i<ends.size();++i) {
                    if(ends[i]-a!=next.ends[i]-b || !std::equal(words.get()+a,words.get()+ends[i],next.words.get()+b))
                        reasons=reasons | static_cast<PassDirtyReason>(2u<<i);
                    a=ends[i]; b=next.ends[i];
                }
                return reasons;
            }
        };
        struct InputWriter
        {
            std::uint64_t* words{};
            std::size_t size{};
            bool overflow = false;
            void U(std::uint64_t value)
            {
                if(size==(std::numeric_limits<std::size_t>::max)()/sizeof(std::uint64_t)) { overflow=true; return; }
                if(words) words[size]=value;
                ++size;
            }
            void F(float value) { U(std::bit_cast<std::uint32_t>(value)); }
            void V(const glm::vec3& value) { for(int i=0;i<3;++i) F(value[i]); }
            void M(const glm::mat4& value) { for(int c=0;c<4;++c) for(int r=0;r<4;++r) F(value[c][r]); }
            template<class Tag> void Id(Asset::AssetHandle<Tag> value)
            { U(value.index); U(value.generation); U(value.registry); }
        };
        std::expected<PassInputs,SubmissionError> CaptureInputs(RenderPass pass,const RenderFrame& frame,
            const FrameSubmissionDesc& desc,const PreparedDraw* draws,const RenderVisibility& visibility)
        {
            PassInputs result;
            const bool picking=pass==RenderPass::Picking, directional=pass==RenderPass::DirectionalShadow;
            const auto indices=picking?visibility.Picking():visibility.Shadows();
            const auto include=[&](std::size_t i) {return picking || draws[i].role->kind==SceneMaterialKind::Lit;};
            const auto emit=[&](InputWriter& out) {
                const auto& target=picking?desc.picking.Buffer():directional?desc.cascadeShadow.Buffer():desc.pointShadow.Buffer();
                out.Id(target.StorageIdentity());
                result.ends[0]=out.size;
                for(auto i:indices) if(include(i)) {
                    const auto& d=*draws[i].draw;
                    out.Id(d.entity); out.U(d.submesh.firstElement); out.U(d.submesh.elementCount); out.U(d.submesh.materialSlot);
                }
                result.ends[1]=out.size;
                for(auto i:indices) if(include(i)) out.M(draws[i].draw->worldTransform);
                result.ends[2]=out.size;
                for(auto i:indices) if(include(i)) {
                    const auto& mesh=draws[i].resources->Mesh(); out.Id(mesh.Identity()); out.U(mesh.Revision());
                }
                result.ends[3]=out.size;
                for(auto i:indices) if(include(i)) {
                    const auto& material=draws[i].resources->Material();
                    out.Id(material.Instance()); out.U(material.PublicationRevision()); out.U(material.Revision());
                    out.Id(material.Source()->Template()); out.U(material.Source()->TemplateRevision());
                    out.U(material.Source()->Declaration()->PipelineRevision());
                    out.Id(draws[i].draw->pipeline); out.U(static_cast<unsigned>(draws[i].role->kind)); out.F(draws[i].role->opacity);
                    if(picking && draws[i].role->kind==SceneMaterialKind::Helper) out.F(draws[i].role->lineWidth);
                    out.Id(material.Program().Identity()); out.U(material.Program().Revision());
                    out.U(material.Textures().size());
                    for(const auto& texture:material.Textures()) {
                        out.Id(texture.texture.Identity()); out.U(texture.texture.Revision());
                        out.Id(texture.sampler.Identity()); out.U(texture.sampler.Revision());
                    }
                }
                result.ends[4]=out.size;
                if(picking || directional) {
                    const auto& camera=frame.Cameras()[0];
                    out.Id(camera.entity); out.M(camera.view); out.M(camera.projection); out.V(camera.worldPosition);
                    out.U(camera.viewportX); out.U(camera.viewportY); out.U(camera.viewportWidth); out.U(camera.viewportHeight);
                    if(picking) out.U(camera.visibleLayers);
                }
                result.ends[5]=out.size;
                if(directional) for(const auto& light:frame.DirectionalLights()) {
                    out.Id(light.entity); out.U(light.revision); out.V(light.direction); out.U(light.shadows.castShadows);
                }
                if(!picking && !directional) for(const auto& light:frame.PointLights()) {
                    out.Id(light.entity); out.U(light.revision); out.V(light.position); out.F(light.range); out.U(light.shadows.castShadows);
                }
                result.ends[6]=out.size;
                if(directional) {
                    out.F(desc.cameraFov); out.F(desc.cameraAspect); out.F(desc.cameraNear); out.F(desc.cameraFar);
                    for(float split:desc.cascadeSplits) out.F(split);
                }
                if(!picking && !directional) out.F(desc.pointNear);
                result.ends[7]=out.size;
            };
            InputWriter measure; emit(measure);
            if(measure.overflow) return Error("pass input size",Code::Allocation);
            result.words.reset(new (std::nothrow) std::uint64_t[measure.size]);
            if(!result.words) return Error("pass input allocation",Code::Allocation);
            InputWriter writer{result.words.get()}; emit(writer); result.valid=true;
            return result;
        }
        std::expected<void, SubmissionError> Draw(const PreparedDraw& draw, const Shader& shader)
        {
            Uniform(shader,"u_model",draw.draw->worldTransform);
            Uniform(shader,"u_EntityID",draw.pixel);
            const auto primitive = draw.role->kind == SceneMaterialKind::Helper ? MeshPrimitive::Lines : MeshPrimitive::Triangles;
            if(primitive==MeshPrimitive::Lines) State::Get().LineWidth(draw.role->lineWidth);
            auto result = draw.resources->Mesh()->DrawSubmesh(draw.submesh, primitive);
            if (!result) return std::unexpected(SubmissionError{"mesh submission",result.error()});
            PassTiming::Submitted(1,1);
            return {};
        }
    }
    struct FrameSubmission::Storage
    {
        Shader point, cascade, pick;
        RenderBackend::UploadBuffer frameUpload, materialUpload;
        std::size_t materialLimit{};
        RenderBackend::PackedFrame lastFrame;
        std::array<PassInputs,3> inputs;
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        std::thread::id owner = std::this_thread::get_id();
        ~Storage()
        {
            GLContextThread::RequireOwner(owner,"frame submission destruction");
            Asset::AssetDetail::RequireInvariant(SDL_GL_GetCurrentContext() == context);

        }
    };
    FrameSubmission::FrameSubmission(FrameSubmission&&) noexcept = default;
    FrameSubmission& FrameSubmission::operator=(FrameSubmission&&) noexcept = default;
    FrameSubmission::~FrameSubmission() = default;
    void FrameSubmission::InvalidatePassContents() noexcept
    {
        if(m_Storage) {
            GLContextThread::RequireOwner(m_Storage->owner,"invalidate pass contents");
            for(auto& inputs:m_Storage->inputs) inputs.pending=inputs.pending | PassDirtyReason::ExternalWrite;
        }
    }

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
        GLint frameLimit{};GLint64 materialLimit{};
        glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE,&frameLimit);
        glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&materialLimit);
        if(frameLimit<sizeof(RenderBackend::PackedFrame) || materialLimit<32)
            return Error("packed upload capacity",Code::UnsupportedPipeline);
        storage.materialLimit=static_cast<std::size_t>(materialLimit);
        if(const auto error=glGetError();error!=GL_NO_ERROR)
            return Error("packed upload limits",Code::Driver);
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
            // Store the nearest facing occluder. Far-surface depth from front
            // culling leaks directional light at receiver contacts under bias.
            result.colorWrite=false; result.depthLoad=PassLoad::Clear; break;
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
        struct FailedSubmission
        {
            Storage& storage; bool success=false;
            ~FailedSubmission() { if(!success) for(auto& inputs:storage.inputs)
                inputs.pending=inputs.pending | PassDirtyReason::RetryAfterFailure; }
        } transaction{*m_Storage};
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
        FrameSubmissionStats stats; stats.visibility=visibility->Stats();
        std::array<PassInputs,3> candidateInputs;
        constexpr RenderPass cachedPasses[]{RenderPass::DirectionalShadow,RenderPass::PointShadow,RenderPass::Picking};
        const bool requested[]{directionalShadow,pointShadow,desc.pickingEnabled};
        for(std::size_t i=0;i<candidateInputs.size();++i) {
            auto captured=CaptureInputs(cachedPasses[i],frame,desc,draws.get(),*visibility);
            if(!captured) return std::unexpected(captured.error());
            candidateInputs[i]=std::move(*captured);
            const auto reasons=m_Storage->inputs[i].Changes(candidateInputs[i]);
            stats.decisions[i]={cachedPasses[i],reasons,requested[i],requested[i] && reasons!=PassDirtyReason::None};
        }
        EntityPickTable candidatePicks;
        if(desc.pickingEnabled) for(auto index:visibility->Picking()) {
            auto pixel=candidatePicks.Encode(draws[index].draw->entity);
            if(!pixel) return std::unexpected(SubmissionError{"pick encoding",pixel.error()});
            draws[index].pixel=*pixel;
        }
        // Disjoint pass partitions fit in count ordinals. Allocate before GL;
        // failure returns a typed error without partially submitting the frame.
        std::unique_ptr<std::size_t[]> ordering(new (std::nothrow) std::size_t[count]);
        if(count && !ordering) return Error("draw ordering",Code::Allocation);
        std::span<std::size_t> remaining(ordering.get(),count);
        auto copyIndices=[&](std::span<const std::size_t> source) {
            auto result=remaining.first(source.size());
            std::copy(source.begin(),source.end(),result.begin());
            remaining=remaining.subspan(source.size());
            return result;
        };
        auto opaque=copyIndices(visibility->Opaque());
        auto masked=copyIndices(visibility->Masked());
        auto transparent=copyIndices(visibility->Transparent());
        RenderDetail::SortLocality(opaque,frame.Draws());
        RenderDetail::SortLocality(masked,frame.Draws());
        RenderDetail::SortTransparent(transparent,frame.Draws(),camera);
        auto materialBatch=RenderBackend::MaterialBatch::Pack(frame,desc.pipelines,m_Storage->materialLimit);
        if(!materialBatch) return std::unexpected(materialBatch.error());
        RenderBackend::PackedFrame packedFrame;
        if(directionalShadow) packedFrame.cascades=m_Storage->lastFrame.cascades;
        if(pointShadow) packedFrame.pointMatrices=m_Storage->lastFrame.pointMatrices;
        const auto matrixWords=[](const glm::mat4& value) { RenderBackend::MatrixWords words;std::copy_n(glm::value_ptr(value),16,words.begin());return words; };
        const auto lane=[](glm::vec3 value) { return RenderBackend::FloatLane{value.x,value.y,value.z,0}; };
        packedFrame.view=matrixWords(camera.view);packedFrame.projection=matrixWords(camera.projection);
        packedFrame.skyView=matrixWords(glm::mat4(glm::mat3(camera.view)));
        packedFrame.viewPosition=lane(camera.worldPosition);packedFrame.lightDirection=lane(lightDirection);packedFrame.lightPosition=lane(position);
        packedFrame.directionalColor=lane(directional?directional->color*directional->intensity:glm::vec3(0));
        packedFrame.pointColor=lane(point?point->color*point->intensity:glm::vec3(0));
        packedFrame.planes={desc.cameraFar,pointFar,0,0};
        packedFrame.lights={bool(directional),bool(point),directionalShadow,pointShadow};
        previous=desc.cameraNear;
        for(std::size_t i=0;i<desc.cascadeSplits.size();++i) {
            packedFrame.splits[i][0]=desc.cascadeSplits[i];
            if(stats.decisions[0].executed) packedFrame.cascades[i]=matrixWords(LightMatrix(camera,desc,lightDirection,previous,desc.cascadeSplits[i]));
            previous=desc.cascadeSplits[i];
        }
        if(stats.decisions[1].executed) {
            const auto& size=desc.pointShadow.Buffer().Description();
            const auto matrices=PointMatrices(position,desc.pointNear,pointFar,float(size.Width)/size.Height);
            for(std::size_t i=0;i<matrices.size();++i) packedFrame.pointMatrices[i]=matrixWords(matrices[i]);
        }
        RenderBackend::UploadBindings uploadRestore;
        TargetRestore restore;
        State state; // Ends before restore and every external scheduler boundary.
        auto& storage=*m_Storage;
        if(auto uploaded=storage.frameUpload.Update(std::as_bytes(std::span(&packedFrame,1)),GL_STREAM_DRAW);!uploaded)
            return std::unexpected(uploaded.error());
        if(auto uploaded=storage.materialUpload.Update(materialBatch->Bytes(),GL_DYNAMIC_DRAW);!uploaded)
            return std::unexpected(uploaded.error());
        storage.lastFrame=packedFrame;
        glBindBufferBase(GL_UNIFORM_BUFFER,1,storage.frameUpload.Name());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,storage.materialUpload.Name());
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
            Uniform(shader,"geMaterialOffset",materialBatch->offsets[draw.draw->resources]);
            if(!masked) return {};
            auto slots=material.Source()->Declaration()->Textures();
            for(std::size_t i=0;i<slots.size();++i) if(slots[i].declaration.name=="albedoMap") {
                if(auto bound=BindTexture(Asset::TextureView(material.Textures()[i].texture),0);!bound) return std::unexpected(SubmissionError{"coverage texture",bound.error()});
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
        if(stats.decisions[0].executed)
        {
            PassTiming::Scope timing(RenderPass::DirectionalShadow);
            BindProgram(storage.cascade); desc.cascadeShadow.Bind(); begin(RenderPass::DirectionalShadow);
            if(auto result=shadowDraws(storage.cascade);!result) return std::unexpected(result.error());
            timing.Complete();
        }
        if(stats.decisions[1].executed)
        {
            PassTiming::Scope timing(RenderPass::PointShadow);
            BindProgram(storage.point); desc.pointShadow.Bind(); begin(RenderPass::PointShadow);
            if(auto result=shadowDraws(storage.point);!result) return std::unexpected(result.error());
            timing.Complete();
        }
        if(stats.decisions[2].executed)
        {
            PassTiming::Scope timing(RenderPass::Picking);
            BindProgram(storage.pick); desc.picking.Bind(); begin(RenderPass::Picking);
            RenderCounters::RecordPass(RenderCounters::Pass::Picking);
            if(auto result=desc.picking.ClearAttachment(0,-1);!result) return std::unexpected(SubmissionError{"clear picking",result.error()});
            for(auto index:visibility->Picking()) {
                if(auto result=coverage(draws[index],storage.pick);!result) return std::unexpected(result.error());
                // Match the authored color geometry's culling policy.
                Toggle(GL_CULL_FACE,draws[index].resources->Material().Pipeline().Description().cull!=CullMode::None);
                if(auto result=Draw(draws[index],storage.pick);!result) return std::unexpected(result.error());
                ++stats.pickDraws;
            }
            timing.Complete();
        }
        const auto colorDraw=[&](std::size_t index,RenderPass pass)->std::expected<void,SubmissionError> {
            const auto& prepared=draws[index]; const auto kind=prepared.role->kind;
            const bool sky=kind==SceneMaterialKind::Sky, helper=kind==SceneMaterialKind::Helper || kind==SceneMaterialKind::PointLight;
            if((pass==RenderPass::Skybox)!=sky || (pass==RenderPass::Debug)!=helper) return {};
            if(kind==SceneMaterialKind::PointLight && (!point || point->entity!=prepared.draw->entity)) return {};
            const auto& material=prepared.resources->Material(); const auto& shader=*material.Program();
            const auto& pipeline=material.Pipeline(); const auto blend=pipeline.Blend();
            BindProgram(shader);
            Toggle(GL_CULL_FACE,pipeline.Description().cull!=CullMode::None);
            State::Get().CullFace(GL_BACK); State::Get().DepthFunc(Compare(pipeline.Depth().compare));
            State::Get().DepthMask(!sky && pipeline.Depth().write); Toggle(GL_BLEND,blend.enabled);
            State::Get().BlendFunction(Factor(blend.sourceColor),Factor(blend.destinationColor),Factor(blend.sourceAlpha),Factor(blend.destinationAlpha));
            Uniform(shader,"geMaterialOffset",materialBatch->offsets[prepared.draw->resources]);
            if(auto result=MaterialTextures(material);!result) return result;
            if(kind==SceneMaterialKind::Lit)
            {
                const auto unit=static_cast<unsigned>(material.Textures().size());
                if(directionalShadow) {
                    auto view=desc.cascadeShadow.DepthView(); if(!view) return std::unexpected(SubmissionError{"cascade view",view.error()});
                    if(auto result=BindTexture(Asset::TextureView(*view),unit);!result) return std::unexpected(SubmissionError{"cascade binding",result.error()});
                } else { state.Texture(unit,GL_TEXTURE_2D_ARRAY,0); }
                if(pointShadow) {
                    auto view=desc.pointShadow.DepthView(); if(!view) return std::unexpected(SubmissionError{"point view",view.error()});
                    if(auto result=BindTexture(Asset::TextureView(*view),unit+1);!result) return std::unexpected(SubmissionError{"point binding",result.error()});
                } else { state.Texture(unit+1,GL_TEXTURE_CUBE_MAP,0); }
                State::Get().Sampler(unit,0); State::Get().Sampler(unit+1,0);
                Uniform(shader,"shadowMap",int(unit)); Uniform(shader,"pointShadowDepthMap",int(unit+1));
                Uniform(shader,"geReceiveShadows",prepared.draw->receiveShadows);
            }
            Toggle(GL_LINE_SMOOTH,kind==SceneMaterialKind::Helper);
            if(kind==SceneMaterialKind::Helper) State::Get().LineWidth(prepared.role->lineWidth);
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
            PassTiming::Scope timing(pass);
            begin(pass);
            // Sky/Debug retain source order; UI stays in the later scheduler stage.
            std::span<const std::size_t> indices=pass==RenderPass::Opaque?opaque:pass==RenderPass::Masked?masked:
                pass==RenderPass::Transparent?std::span<const std::size_t>(transparent):visibility->Main();
            for(auto index:indices) if(auto result=colorDraw(index,pass);!result) return std::unexpected(result.error());
            timing.Complete();
        }
        State::Get().DepthFunc(GL_LESS); State::Get().DepthMask(GL_TRUE); State::Get().ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); Toggle(GL_BLEND,false);
        if(const auto error=glGetError();error!=GL_NO_ERROR)
            return std::unexpected(SubmissionError{std::format("frame submission: driver diagnostic 0x{:x}",error),Code::Driver});
        // Rebuild the same deterministic table on cache hits; absent requests
        // expose no lookup. Deferred dirty inputs never replace the cached key.
        picks=std::move(candidatePicks);
        for(std::size_t i=0;i<candidateInputs.size();++i)
            if(stats.decisions[i].executed) storage.inputs[i]=std::move(candidateInputs[i]);
        transaction.success=true;
        return stats;
    }
}
