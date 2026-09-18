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
        std::expected<Shader, SubmissionError> Load(std::initializer_list<const char*> names)
        {
            std::vector<std::string> paths;
            for (const auto* name : names)
            {
                auto path = RuntimeAssets::TryFile(std::string("Shaders/") + name);
                if (!path) return std::unexpected(SubmissionError{"submission shader path", path.error()});
                paths.push_back(*path);
            }
            auto shader = Asset::CreateShaderProgramFromFiles(paths);
            if (!shader) return std::unexpected(SubmissionError{"submission shader", shader.error()});
            return std::move(*shader);
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
            else return std::format("sampler code={} registry={} message={}",static_cast<int>(cause.code),static_cast<int>(cause.registry),cause.message);
        },error.cause);
    }
    std::expected<FrameSubmission, SubmissionError> FrameSubmission::Create()
    {
        if (!GLContextThread::IsCurrentOwner()) return Error("create submission",Code::Context);
        FrameSubmission result;
        result.m_Storage.reset(new (std::nothrow) Storage);
        if (!result.m_Storage) return Error("submission storage",Code::Allocation);
        auto point = Load({"point_shadows_depth.vert","point_shadows_depth.gs","point_shadows_depth.frag"});
        if (!point) return std::unexpected(point.error());
        auto cascade = Load({"shadow_mapping_depth.vert","shadow_mapping_depth.gs","shadow_mapping_depth.frag"});
        if (!cascade) return std::unexpected(cascade.error());
        auto pick = Load({"mouse_pick.vert","mouse_pick.frag"});
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
        const auto& pickTarget=desc.picking.Buffer().Description();
        if (!desc.color || !desc.picking || !desc.pointShadow || !desc.cascadeShadow
            || camera.viewportX || camera.viewportY || camera.viewportWidth!=target.Width || camera.viewportHeight!=target.Height
            || pickTarget.Width!=target.Width || pickTarget.Height!=target.Height)
            return Error("submission target dimensions",Code::InvalidTarget);
        if (desc.cascadeSplits.size()!=5 || desc.cascadeSplits.back()!=desc.cameraFar
            || !std::isfinite(desc.cameraFov) || desc.cameraFov<=0 || desc.cameraFov>=glm::pi<float>() || !std::isfinite(desc.cameraAspect)
            || desc.cameraAspect<=0 || !std::isfinite(desc.cameraNear) || desc.cameraNear<=0
            || !std::isfinite(desc.cameraFar) || desc.cameraFar<=desc.cameraNear
            || !std::isfinite(desc.pointNear) || desc.pointNear<=0 || !std::isfinite(desc.pointFar) || desc.pointFar<=desc.pointNear)
            return Error("shadow projection",Code::InvalidShadowSettings);
        float previous=desc.cameraNear;
        for (float split:desc.cascadeSplits)
        {
            if (!std::isfinite(split) || split<=previous || split>desc.cameraFar)
                return Error("cascade splits",Code::InvalidShadowSettings);
            previous=split;
        }
        const auto* directional=frame.DirectionalLights().empty()?nullptr:&frame.DirectionalLights()[0];
        const auto* point=frame.PointLights().empty()?nullptr:&frame.PointLights()[0];
        const float pointFar=point?point->range:desc.pointFar;
        if (pointFar<=desc.pointNear) return Error("point shadow range",Code::InvalidShadowSettings);
        // Extraction stores ray direction; the existing BRDF uses surface-to-light.
        const glm::vec3 lightDirection=directional?-directional->direction:glm::vec3(0,0,1);
        if (directional && directional->shadows.castShadows && std::abs(glm::dot(lightDirection,glm::vec3(0,1,0)))>.99999f)
            return Error("cascade up vector",Code::InvalidShadowSettings);
        std::unique_ptr<PreparedDraw[]> draws(new (std::nothrow) PreparedDraw[frame.Draws().size()]);
        if (!draws && !frame.Draws().empty()) return Error("submission draw metadata",Code::Allocation);
        EntityPickTable candidatePicks;
        std::size_t count=0;
        for (const auto& draw:frame.Draws())
        {
            if (!(draw.layers & camera.visibleLayers)) continue;
            const ScenePipeline* role=nullptr;
            for (const auto& entry:desc.pipelines) if (entry.pipeline==draw.pipeline) { role=&entry; break; }
            if (!role) return Error("unregistered scene pipeline",Code::UnsupportedPipeline);
            if (draw.resources>=frame.Resources().size()) return Error("draw resources",Code::InvalidDraw);
            const auto& resources=frame.Resources()[draw.resources];
            const auto& pipeline=resources.Material().Pipeline().Description();
            if (pipeline.alpha!=AlphaMode::Opaque || !pipeline.depthTest || pipeline.polygon!=PolygonMode::Fill
                || pipeline.frontFace!=FrontFace::CounterClockwise || (pipeline.cull!=CullMode::Back && pipeline.cull!=CullMode::None)
                || (pipeline.depthCompare!=DepthCompare::Less && pipeline.depthCompare!=DepthCompare::LessEqual)
                || !std::isfinite(role->lineWidth) || role->lineWidth<=0)
                return Error("unsupported scene pipeline state",Code::UnsupportedPipeline);
            auto& prepared=draws[count++]; prepared={&draw,&resources,role};
            bool found=false;
            const auto ranges=resources.Mesh()->Submeshes();
            for (std::size_t i=0;i<ranges.size();++i)
                if (ranges[i].firstElement==draw.submesh.firstElement && ranges[i].elementCount==draw.submesh.elementCount
                    && ranges[i].materialSlot==draw.submesh.materialSlot) {prepared.submesh=i;found=true;break;}
            if (!found) return Error("draw submesh",Code::InvalidDraw);
            if (draw.pickable)
            {
                auto pixel=candidatePicks.Encode(draw.entity);
                if (!pixel) return std::unexpected(SubmissionError{"pick encoding",pixel.error()});
                prepared.pixel=*pixel;
            }
        }
        TargetRestore restore;
        auto& storage=*m_Storage;
        FrameSubmissionStats stats;
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
        glDisable(GL_BLEND); glEnable(GL_MULTISAMPLE); glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW); glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glBindBufferBase(GL_UNIFORM_BUFFER,0,storage.matrices);
        std::array<glm::mat4,5> matrices;
        previous=desc.cameraNear;
        for (std::size_t i=0;i<matrices.size();++i)
        {
            matrices[i]=LightMatrix(camera,desc,lightDirection,previous,desc.cascadeSplits[i]);
            previous=desc.cascadeSplits[i];
        }
        glNamedBufferSubData(storage.matrices,0,sizeof(matrices),matrices.data());
        const auto shadowPass=[&](const Shader& shader,bool enabled)->std::expected<void,SubmissionError>
        {
            RenderCounters::RecordPass(RenderCounters::Pass::Shadow);
            glClear(GL_DEPTH_BUFFER_BIT);
            if (!enabled) return {};
            for (std::size_t i=0;i<count;++i) if (draws[i].draw->castShadows && draws[i].role->kind==SceneMaterialKind::Lit)
            {
                auto result=Draw(draws[i],shader); if (!result) return result; ++stats.shadowDraws;
            }
            return {};
        };
        storage.point.Bind(); desc.pointShadow.Bind();
        const auto pointSize=desc.pointShadow.Buffer().Description();
        glViewport(0,0,pointSize.Width,pointSize.Height);
        const glm::vec3 position=point?point->position:glm::vec3(0,15,-10);
        const auto transforms=PointMatrices(position,desc.pointNear,pointFar,float(pointSize.Width)/pointSize.Height);
        for (std::size_t i=0;i<transforms.size();++i) Uniform(storage.point,std::format("shadowMatrices[{}]",i).c_str(),transforms[i]);
        Uniform(storage.point,"lightPos",position); Uniform(storage.point,"far_plane",pointFar);
        if (auto result=shadowPass(storage.point,point && point->shadows.castShadows);!result) return std::unexpected(result.error());
        storage.cascade.Bind(); desc.cascadeShadow.Bind();
        const auto cascadeSize=desc.cascadeShadow.Buffer().Description();
        glViewport(0,0,cascadeSize.Width,cascadeSize.Height); glCullFace(GL_FRONT);
        if (auto result=shadowPass(storage.cascade,directional && directional->shadows.castShadows);!result) return std::unexpected(result.error());
        glCullFace(GL_BACK);
        RenderCounters::RecordPass(RenderCounters::Pass::Picking);
        storage.pick.Bind(); desc.picking.Bind(); glViewport(0,0,target.Width,target.Height);
        glClear(GL_DEPTH_BUFFER_BIT);
        if (auto result=desc.picking.ClearAttachment(0,-1);!result) return std::unexpected(SubmissionError{"clear picking",result.error()});
        CameraUniforms(storage.pick,camera);
        for (std::size_t i=0;i<count;++i) if (draws[i].draw->pickable)
        {
            if (auto result=Draw(draws[i],storage.pick);!result) return std::unexpected(result.error());
            ++stats.pickDraws;
        }
        desc.color.Bind(); glViewport(0,0,target.Width,target.Height);
        glClearColor(.1f,.1f,.1f,1.f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        auto cascadeView=desc.cascadeShadow.DepthView();
        if (!cascadeView) return std::unexpected(SubmissionError{"cascade depth view",cascadeView.error()});
        auto pointView=desc.pointShadow.DepthView();
        if (!pointView) return std::unexpected(SubmissionError{"point depth view",pointView.error()});
        // Preserve source order within each existing pass; the sky pass stays last.
        for (bool skyPass:{false,true}) for (std::size_t i=0;i<count;++i)
        {
            const auto& prepared=draws[i]; const auto kind=prepared.role->kind;
            if ((kind==SceneMaterialKind::Sky)!=skyPass) continue;
            const auto& material=prepared.resources->Material(); const auto& shader=*material.Program();
            const auto& pipeline=material.Pipeline();
            shader.Bind();
            if (pipeline.Description().cull==CullMode::None) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            glDepthFunc(pipeline.Depth().compare==DepthCompare::LessEqual?GL_LEQUAL:GL_LESS);
            glDepthMask(pipeline.Depth().write?GL_TRUE:GL_FALSE);
            CameraUniforms(shader,camera,skyPass);
            if (auto result=MaterialUniforms(material);!result) return std::unexpected(result.error());
            if (kind==SceneMaterialKind::Lit)
            {
                const auto shadowUnit=static_cast<unsigned>(material.Textures().size());
                if (auto result=Asset::TextureView(*cascadeView).Bind(shadowUnit);!result) return std::unexpected(SubmissionError{"cascade binding",result.error()});
                if (auto result=Asset::TextureView(*pointView).Bind(shadowUnit+1);!result) return std::unexpected(SubmissionError{"point binding",result.error()});
                // Target-authored sampling policy (including border/depth behavior).
                glBindSampler(shadowUnit,0); glBindSampler(shadowUnit+1,0);
                Uniform(shader,"shadowMap",int(shadowUnit)); Uniform(shader,"pointShadowDepthMap",int(shadowUnit+1));
                Uniform(shader,"lightDir",lightDirection); Uniform(shader,"lightPos",position);
                Uniform(shader,"directionallightColor",directional?directional->color*directional->intensity:glm::vec3(0));
                Uniform(shader,"pointlightColor",point?point->color*point->intensity:glm::vec3(0));
                Uniform(shader,"frameLights",true); Uniform(shader,"frameDirectional",bool(directional)); Uniform(shader,"framePoint",bool(point));
                Uniform(shader,"frameDirectionalShadows",directional && directional->shadows.castShadows && prepared.draw->receiveShadows);
                Uniform(shader,"framePointShadows",point && point->shadows.castShadows && prepared.draw->receiveShadows);
                Uniform(shader,"farPlane",desc.cameraFar); Uniform(shader,"pointShadowfarPlane",pointFar);
                Uniform(shader,"cascadeCount",int(desc.cascadeSplits.size())); Uniform(shader,"reverse_normals",false);
                for (std::size_t j=0;j<desc.cascadeSplits.size();++j) Uniform(shader,std::format("cascadePlaneDistances[{}]",j).c_str(),desc.cascadeSplits[j]);
            }
            if (kind==SceneMaterialKind::Helper) {glEnable(GL_LINE_SMOOTH); glLineWidth(prepared.role->lineWidth);}
            if (kind==SceneMaterialKind::PointLight)
            {
                if (!point || point->entity!=prepared.draw->entity) continue;
                Uniform(shader,"pointlightColor",point->color*point->intensity);
            }
            if (auto result=Draw(prepared,shader);!result) return std::unexpected(result.error());
            if (skyPass) ++stats.skyDraws; else if (kind==SceneMaterialKind::Helper || kind==SceneMaterialKind::PointLight) ++stats.helperDraws;
            else ++stats.colorDraws;
        }
        glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
        if (const auto error=glGetError(); error!=GL_NO_ERROR)
            return std::unexpected(SubmissionError{std::format("frame submission: driver diagnostic 0x{:x}",error),Code::Driver});
        picks=std::move(candidatePicks);
        return stats;
    }
}
