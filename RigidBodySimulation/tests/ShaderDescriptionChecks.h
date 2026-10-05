#pragma once

#include "Renderer/SceneRenderResources.h"
#include "Renderer/RenderExtraction.h"
#include "Scene/_Scene.h"
#include "Scene/_Entity.h"
#include "Core/Log.h"
#include <algorithm>
#ifdef GENGINE_SHADER_DESCRIPTION_BACKEND
#include "../../GEngine/src/Assets/ShaderBackend.h"
#endif

namespace PreEditorValidation
{
    struct ShaderDriverCalls
    {
        std::uint64_t compiles = 0, links = 0, interfaces = 0, locations = 0;
        bool Empty() const { return !(compiles || links || interfaces || locations); }
    };
    void BeginShaderObservation();
    ShaderDriverCalls ShaderObservation();
    void EndShaderObservation();
    bool CheckShaderCachedLocations();
    struct ShaderObservationScope
    {
        ShaderObservationScope() { BeginShaderObservation(); }
        ~ShaderObservationScope() { EndShaderObservation(); }
    };

#ifdef GENGINE_SHADER_DESCRIPTION_BACKEND
    namespace ShaderObservationBackend
    {
        ShaderDriverCalls calls;
        bool observing = false;
        PFNGLCOMPILESHADERPROC compile;
        PFNGLLINKPROGRAMPROC link;
        PFNGLGETPROGRAMINTERFACEIVPROC interfaceQuery;
        PFNGLGETPROGRAMRESOURCEIVPROC resource;
        PFNGLGETPROGRAMRESOURCENAMEPROC resourceName;
        PFNGLGETPROGRAMRESOURCELOCATIONPROC resourceLocation;
        PFNGLGETUNIFORMLOCATIONPROC location;
        PFNGLGETACTIVEUNIFORMPROC activeUniform;
        void APIENTRY Compile(GLuint s) { ++calls.compiles; compile(s); }
        void APIENTRY Link(GLuint p) { ++calls.links; link(p); }
        void APIENTRY Interface(GLuint p, GLenum i, GLenum n, GLint* v) { ++calls.interfaces; interfaceQuery(p,i,n,v); }
        void APIENTRY Resource(GLuint p, GLenum i, GLuint x, GLsizei n, const GLenum* props, GLsizei size, GLsizei* length, GLint* v)
        { ++calls.interfaces; resource(p,i,x,n,props,size,length,v); }
        void APIENTRY Name(GLuint p, GLenum i, GLuint x, GLsizei size, GLsizei* length, GLchar* v)
        { ++calls.interfaces; resourceName(p,i,x,size,length,v); }
        GLint APIENTRY ResourceLocation(GLuint p, GLenum i, const GLchar* n)
        { ++calls.locations; return resourceLocation(p,i,n); }
        GLint APIENTRY Location(GLuint p, const GLchar* n) { ++calls.locations; return location(p,n); }
        void APIENTRY Active(GLuint p, GLuint i, GLsizei b, GLsizei* n, GLint* s, GLenum* t, GLchar* v)
        { ++calls.interfaces; activeUniform(p,i,b,n,s,t,v); }
    }
    void BeginShaderObservation()
    {
        using namespace ShaderObservationBackend;
        ::GEngine::Asset::AssetDetail::RequireInvariant(!observing);
        observing = true; calls = {};
        compile = glad_glCompileShader; glad_glCompileShader = Compile;
        link = glad_glLinkProgram; glad_glLinkProgram = Link;
        interfaceQuery = glad_glGetProgramInterfaceiv; glad_glGetProgramInterfaceiv = Interface;
        resource = glad_glGetProgramResourceiv; glad_glGetProgramResourceiv = Resource;
        resourceName = glad_glGetProgramResourceName; glad_glGetProgramResourceName = Name;
        resourceLocation = glad_glGetProgramResourceLocation; glad_glGetProgramResourceLocation = ResourceLocation;
        location = glad_glGetUniformLocation; glad_glGetUniformLocation = Location;
        activeUniform = glad_glGetActiveUniform; glad_glGetActiveUniform = Active;
    }
    ShaderDriverCalls ShaderObservation() { return ShaderObservationBackend::calls; }
    void EndShaderObservation()
    {
        using namespace ShaderObservationBackend;
        ::GEngine::Asset::AssetDetail::RequireInvariant(observing);
        glad_glCompileShader = compile; glad_glLinkProgram = link;
        glad_glGetProgramInterfaceiv = interfaceQuery; glad_glGetProgramResourceiv = resource;
        glad_glGetProgramResourceName = resourceName; glad_glGetProgramResourceLocation = resourceLocation;
        glad_glGetUniformLocation = location; glad_glGetActiveUniform = activeUniform;
        observing = false;
    }
    bool CheckShaderCachedLocations()
    {
        using namespace ::GEngine::Asset;
        const ShaderSource stages[]{
            {VERTEX, "#version 450 core\nuniform float samples[3];\nvoid main(){gl_Position=vec4(samples[0],samples[1],samples[2],1);}", "array cache vertex"},
            {FRAGMENT, "#version 450 core\nlayout(location=0) out vec4 FragColor;\nvoid main(){FragColor=vec4(1);}", "array cache fragment"}};
        auto shader = CreateShaderProgram(stages);
        if (!shader) return false;
        const auto& cache = ShaderBackendAccess::Uniforms(*shader);
        for (const char* name : {"samples", "samples[0]", "samples[1]", "samples[2]"})
        {
            const auto found = cache.find(std::string_view(name));
            if (found == cache.end() || found->second != glGetUniformLocation(ShaderBackendAccess::Program(*shader), name)) return false;
        }
        if (cache.contains("inactive") || glGetUniformLocation(ShaderBackendAccess::Program(*shader), "inactive") != -1) return false;
        ::GEngine::Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_10_CACHE_LOCATIONS_PASS aliases=4 inactive=true renderer={} version={}",
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)), reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        return glGetError() == GL_NO_ERROR;
    }
#else
    inline std::expected<void, ::GEngine::PlatformError>
    CheckShaderDescriptions(::GEngine::EngineContext& context, ::GEngine::SceneRenderResources& owner,
        ::GEngine::Asset::MeshHandle mesh, const ::GEngine::MaterialShaderDescription& description,
        ::GEngine::MaterialHandle original, std::span<const ::GEngine::Asset::ShaderSource> stages,
        std::span<const ::GEngine::MaterialShaderParameter> parameters,
        std::span<const ::GEngine::Asset::ShaderVariant> variants)
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Asset;
        std::size_t checks = 0;
        const auto require = [&](bool ok, const char* label) {
            ++checks; if (!ok) Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_10_FAIL {}", label); return ok;
        };
#define SH10_CHECK(condition, label) if (!require(bool(condition), label)) return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "Phase 10 shader descriptions", label})
        const auto programOf = [&](SceneRenderResources& resources, MaterialHandle material) {
            auto access = resources.Publication().BeginFrame();
            auto value = resources.Materials().Acquire(access, material);
            return value ? (*value)->Declaration()->State().Description().program : ShaderProgramHandle{};
        };
        const auto errorCode = [](const auto& result, ShaderErrorCode code) {
            if (result) return false;
            const auto* cause = std::get_if<ShaderError>(&result.error().cause);
            return cause && cause->code == code;
        };
        const auto originalProgram = programOf(owner, original);
        SH10_CHECK(originalProgram, "custom published program");
        auto reflected = owner.ShaderInterface(description);
        SH10_CHECK(reflected, "cached authored interface");
        SH10_CHECK(std::any_of(reflected->begin(), reflected->end(), [](const auto& v) {
            return v.name == "optionalDetail" && !v.active && v.type == ShaderValueType::Float;
        }), "optional optimized-out binding stays inactive");
        SH10_CHECK(CheckShaderCachedLocations(), "link-time cached scalar/array/inactive locations");

        std::vector<ShaderSource> reversedStages(stages.rbegin(), stages.rend());
        for (auto& stage : reversedStages) stage.label = "different diagnostic label";
        std::vector<MaterialShaderParameter> reversedParameters(parameters.rbegin(), parameters.rend());
        for (auto& p : reversedParameters) if (p.declaration.name == "gain") p.declaration.defaultValue = .6f;
        std::vector<ShaderVariant> reversedVariants(variants.rbegin(), variants.rend());
        auto reordered = MaterialShaderDescription::Create({reversedStages, reversedParameters, {}, reversedVariants});
        SH10_CHECK(reordered, "canonical reordered description");
        SH10_CHECK((*description.Program().Select())->identity == (*reordered->Program().Select())->identity,
            "canonical stage/schema/variant identity excludes labels and values");
        {
            ShaderObservationScope observation;
            const auto work = Shader::CreationWork();
            auto repeated = owner.CreateMaterial(*reordered);
            auto same = owner.CreateMaterial(description);
            auto clone = owner.CloneMaterial(original);
            SH10_CHECK(repeated && same && clone && *clone != original, "warmed creation and Clone");
            SH10_CHECK(programOf(owner, *repeated) == originalProgram && programOf(owner, *same) == originalProgram &&
                programOf(owner, *clone) == originalProgram, "warmed requests share program identity");
            MaterialAuthoringDesc ordinary;
            auto standard = owner.CreateMaterial(ordinary);
            auto standardClone = standard ? owner.CloneMaterial(*standard) : std::expected<MaterialHandle, SceneResourceError>(std::unexpected(standard.error()));
            SH10_CHECK(standard && standardClone, "ordinary creation/Clone preserved");
            SH10_CHECK(ShaderObservation().Empty() && Shader::CreationWork() == work, "warmed operations have zero shader work");
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_10_WARM_PASS compiles=0 links=0 reflection=0 locations=0 source_processing=0");
        }
        auto scene = CreateRefPtr<_Scene>();
        auto entity = scene->CreateEntity("Phase10 retained shader frame");
        SH10_CHECK(entity && owner.AssignRenderable(*entity, {mesh, original}), "retained-frame source assignment");
        std::optional<RenderFrame> oldFrame;
        {
            auto access = owner.Publication().BeginFrame(); RenderExtractionStats stats;
            auto frame = ExtractRenderFrame(*scene, owner.ForFrame(access), stats);
            SH10_CHECK(frame && frame->Draws().size() == 1, "custom frame extraction");
            oldFrame = std::move(*frame);
        }
        MaterialHandle alternate;
        {
            ShaderObservationScope observation;
            auto next = owner.CreateMaterial(description, {1});
            SH10_CHECK(next, "admitted second variant");
            alternate = *next;
            const auto calls = ShaderObservation();
            SH10_CHECK(calls.compiles == 4 && calls.links == 2, "cold custom variant explicitly uses authored and packed links");
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_10_COLD_PASS compiles={} links={} temporary_validation_links=1", calls.compiles, calls.links);
        }
        SH10_CHECK(programOf(owner, alternate) != originalProgram && owner.AssignRenderable(*entity, {mesh, alternate}), "variant uses distinct immutable program");
        SH10_CHECK(oldFrame->Resources()[oldFrame->Draws()[0].resources].Material().Program().Identity() == originalProgram,
            "retained frame pins original exact program");
        auto invalidVariant = owner.CreateMaterial(description, {99});
        SH10_CHECK(errorCode(invalidVariant, ShaderErrorCode::VariantNotAdmitted), "undeclared variant rejected");
        const auto programsBeforeFailure = owner.ShaderWork().programs;
        std::vector<MaterialShaderParameter> missing(parameters.begin(), parameters.end());
        missing.push_back({{"missingParameter", MaterialParameterType::Float, 1.f}, true});
        auto missingDescription = MaterialShaderDescription::Create({stages, missing, {}, variants});
        SH10_CHECK(missingDescription, "missing semantic CPU description");
        auto missingResult = owner.CreateMaterial(*missingDescription);
        SH10_CHECK(errorCode(missingResult, ShaderErrorCode::MissingBinding), "missing required semantic typed failure");
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_10_EXPECTED_DIAGNOSTIC {}", DescribeSceneResourceError(missingResult.error()));
        auto mismatch = missing; mismatch.pop_back();
        for (auto& p : mismatch) if (p.declaration.name == "gain") p.declaration = {"gain", MaterialParameterType::Float3, std::array<float,3>{1,1,1}};
        auto mismatchDescription = MaterialShaderDescription::Create({stages, mismatch, {}, variants});
        SH10_CHECK(mismatchDescription, "mismatched semantic CPU description");
        auto mismatchResult = owner.CreateMaterial(*mismatchDescription);
        SH10_CHECK(errorCode(mismatchResult, ShaderErrorCode::BindingType), "mismatched required semantic typed failure");
        std::vector<ShaderSource> broken(stages.begin(), stages.end());
        for (auto& stage : broken) if (stage.type == FRAGMENT) stage.source = "#version 450 core\ninvalid shader source\n";
        auto brokenDescription = MaterialShaderDescription::Create({broken, parameters, {}, variants});
        SH10_CHECK(brokenDescription, "invalid source retained until explicit compilation");
        auto brokenResult = owner.CreateMaterial(*brokenDescription);
        SH10_CHECK(errorCode(brokenResult, ShaderErrorCode::Compile), "typed source compilation diagnostic");
        const auto& compileError = std::get<ShaderError>(brokenResult.error().cause);
        SH10_CHECK(compileError.shaderType == FRAGMENT && compileError.source == stages[1].label && !compileError.log.empty(), "diagnostic retains stage label and driver log");
        SH10_CHECK(owner.ShaderWork().programs == programsBeforeFailure && programOf(owner, original) == originalProgram,
            "failed candidates do not publish or damage prior resource");
        std::vector<ShaderVariant> tooMany(17);
        for (unsigned i = 0; i < tooMany.size(); ++i) tooMany[i].key.value = i;
        auto excess = MaterialShaderDescription::Create({stages, parameters, {}, tooMany});
        SH10_CHECK(!excess && excess.error().code == ShaderErrorCode::Capacity, "variant bound enforced before compilation");
        tooMany.resize(16);
        SH10_CHECK(MaterialShaderDescription::Create({stages, parameters, {}, tooMany}), "exact 16-variant boundary admitted");
        tooMany[1].key = {0};
        SH10_CHECK(!MaterialShaderDescription::Create({stages, parameters, {}, tooMany}), "duplicate keys rejected");
        const ShaderVariant duplicateDefines[]{ {{0}, {{"CHOICE",0},{"CHOICE",1}}} };
        SH10_CHECK(!MaterialShaderDescription::Create({stages, parameters, {}, duplicateDefines}), "duplicate defines rejected");
        auto capacity = SceneRenderResources::Create(context);
        SH10_CHECK(capacity, "isolated cache owner");
        for (unsigned i = 0; i < 64; ++i)
        {
            std::string source = std::string(stages[1].source) + "\n// admitted cache entry " + std::to_string(i);
            const ShaderSource request[]{stages[0], {FRAGMENT, source, "bounded capacity fixture"}};
            auto desc = MaterialShaderDescription::Create({request, parameters, {}, variants});
            SH10_CHECK(desc && (*capacity)->CreateMaterial(*desc), "bounded cache entry creation");
        }
        SH10_CHECK((*capacity)->ShaderWork().programs == 64, "cache has exactly 64 entries");
        {
            ShaderObservationScope observation;
            auto overflow = (*capacity)->CreateMaterial(description);
            SH10_CHECK(errorCode(overflow, ShaderErrorCode::Capacity) && ShaderObservation().Empty(), "cache capacity fails without eviction or compilation");
        }
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_10_API_PASS checks={} variants=16 cache_limit=64 retained_frame=true", checks);
#undef SH10_CHECK
        return {};
    }

    inline bool CheckShaderSteadyFrame(const ::GEngine::SceneRenderResources& owner, std::array<unsigned, 2> viewport)
    {
        using namespace ::GEngine;
        static unsigned frames = 0;
        static Asset::ShaderCreationWork before;
        static std::uint64_t packing = 0;
        static std::array<unsigned, 2> initialViewport{};
        ++frames;
        if (frames == 5)
        {
            before = Asset::Shader::CreationWork(); packing = owner.ShaderWork().packingPasses;
            initialViewport = viewport;
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_10_VIEW viewport={},{} warmup=5", viewport[0], viewport[1]);
            BeginShaderObservation();
        }
        if (frames == 35)
        {
            const auto calls = ShaderObservation(); EndShaderObservation();
            const bool ok = calls.Empty() && before == Asset::Shader::CreationWork() && packing == owner.ShaderWork().packingPasses &&
                viewport == initialViewport && viewport[0] && viewport[1];
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_10_STEADY_{} frames=30 compiles={} links={} interfaces={} locations={} descriptions={} packing={}",
                ok ? "PASS" : "FAIL", calls.compiles, calls.links, calls.interfaces, calls.locations,
                Asset::Shader::CreationWork().descriptions-before.descriptions, owner.ShaderWork().packingPasses-packing);
            return ok;
        }
        return true;
    }
#endif
}
