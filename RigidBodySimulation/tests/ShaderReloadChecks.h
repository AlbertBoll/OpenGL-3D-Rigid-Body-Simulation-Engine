#pragma once

#include "Renderer/SceneRenderResources.h"
#include "Renderer/RenderExtraction.h"
#include "Renderer/FrameSubmission.h"
#include "Scene/_Scene.h"
#include "Core/Log.h"
#include "ShaderDescriptionChecks.h"
#include <new>
#include <set>

namespace PreEditorValidation
{
    void BeginReloadLifetimeObservation();
    bool EndReloadLifetimeObservation();
    bool CheckReloadRegistryPreparation();

#ifdef GENGINE_SHADER_RELOAD_BACKEND
    namespace ReloadLifetime
    {
        PFNGLCREATEPROGRAMPROC create;
        PFNGLDELETEPROGRAMPROC destroy;
        std::set<GLuint> live, deleted;
        bool duplicate = false;
        GLuint APIENTRY Create()
        {
            auto id = create();
            if (id) { live.insert(id); deleted.erase(id); }
            return id;
        }
        void APIENTRY Destroy(GLuint id)
        {
            if (live.erase(id)) deleted.insert(id);
            else if (deleted.contains(id)) duplicate = true;
            destroy(id);
        }
    }
    void BeginReloadLifetimeObservation()
    {
        using namespace ReloadLifetime;
        live.clear(); deleted.clear(); duplicate = false;
        create = glad_glCreateProgram; destroy = glad_glDeleteProgram;
        glad_glCreateProgram = Create; glad_glDeleteProgram = Destroy;
    }
    bool EndReloadLifetimeObservation()
    {
        using namespace ReloadLifetime;
        glad_glCreateProgram = create; glad_glDeleteProgram = destroy;
        const bool ok = live.empty() && !duplicate && glGetError() == GL_NO_ERROR;
        ::GEngine::Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_11_LIFETIME_{} live={} destroyed={} duplicate={}",
            ok ? "PASS" : "FAIL", live.size(), deleted.size(), duplicate);
        return ok;
    }
    bool CheckReloadRegistryPreparation()
    {
        using namespace ::GEngine::Asset;
        struct Probe
        {
            int value;
            explicit Probe(int v, bool fail = false) : value(v) { if (fail) throw std::bad_alloc(); }
        };
        struct Fence : AssetRetirementFence
        {
            bool& complete;
            explicit Fence(bool& value) : complete(value) {}
            bool IsComplete() const noexcept override { return complete; }
        };
        AssetPublication publication;
        AssetRegistry<MaterialInstanceHandle, Probe> registry(publication, {4, 3, 4});
        MaterialInstanceHandle a, b;
        {
            auto access = publication.BeginPublication();
            auto first = registry.Create(access, 1), second = registry.Create(access, 2);
            if (!first || !second) return false;
            a = *first; b = *second;
            bool caught = false;
            try
            {
                auto prepared = registry.PrepareReplace(access, a, 3);
                if (!prepared || prepared->View()->value != 3) return false;
                (void)registry.PrepareReplace(access, b, 4, true);
            }
            catch (const std::bad_alloc&) { caught = true; }
            if (!caught || registry.Collect(access) != 1) return false;
        }
        bool complete = false;
        {
            auto access = publication.BeginFrame();
            auto first = registry.Acquire(access, a), second = registry.Acquire(access, b);
            if (!first || !second || (*first)->value != 1 || (*second)->value != 2 || first->Revision() != 1) return false;
            if (!registry.ProtectGpuUse(access, *first, std::make_unique<Fence>(complete))) return false;
        }
        {
            auto access = publication.BeginPublication();
            auto first = registry.PrepareReplace(access, a, 3), second = registry.PrepareReplace(access, b, 4);
            if (!first || !second || !registry.CanCommit(access, *first) || !registry.CanCommit(access, *second)) return false;
            registry.CommitPrepared(access, std::move(*first)); registry.CommitPrepared(access, std::move(*second));
            if (registry.CanCommit(access, *first) || registry.Collect(access) != 1) return false;
            complete = true;
            if (registry.Collect(access) != 1) return false;
            auto creation = registry.PrepareCreate(access, 5);
            if (!creation || !registry.CanCommit(access, *creation)) return false;
            auto intervening = registry.Create(access, 6);
            if (!intervening || registry.CanCommit(access, *creation)) return false;
            creation = {}; // Drop candidate before the collection/registry teardown.
            registry.Collect(access);
        }
        {
            using namespace ::GEngine;
            // CPU-only schema fixture: these semantic handles are never submitted.
            PipelineRegistry pipelines(publication);
            MaterialTemplateRegistry declarations(publication);
            PipelineView pipeline;
            MaterialTemplateView oldDeclaration, newDeclaration;
            {
                auto access = publication.BeginPublication();
                auto state = PipelineState::Create({.program = {0, 1, 1}, .programRevision = 1});
                if (!state) return false;
                auto prepared = pipelines.PrepareCreate(access, std::move(*state));
                if (!prepared) return false;
                pipeline = prepared->View();
                pipelines.CommitPrepared(access, std::move(*prepared));
                const MaterialTextureSlotDecl oldSlot[]{ {"optionalMap", false, {}} };
                const MaterialTextureSlotDecl newSlot[]{ {"optionalMap", false,
                    MaterialTextureValue{{0, 1, 1}, {0, 1, 1}}} };
                auto oldValue = MaterialTemplate::Create({pipeline, {}, oldSlot, false, false});
                auto newValue = MaterialTemplate::Create({pipeline, {}, newSlot, false, false});
                if (!oldValue || !newValue) return false;
                auto oldPrepared = declarations.PrepareCreate(access, std::move(*oldValue));
                if (!oldPrepared) return false;
                oldDeclaration = oldPrepared->View();
                declarations.CommitPrepared(access, std::move(*oldPrepared));
                auto newPrepared = declarations.PrepareCreate(access, std::move(*newValue));
                if (!newPrepared) return false;
                newDeclaration = newPrepared->View();
                declarations.CommitPrepared(access, std::move(*newPrepared));
            }
            auto instance = MaterialInstance::Create(oldDeclaration);
            if (!instance) return false;
            auto rebound = instance->WithDeclaration(newDeclaration);
            if (!rebound || rebound->Textures()[0] || rebound->Revision() != instance->Revision()) return false;
        }
        ::GEngine::Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_11_REGISTRY_PASS injected_bad_alloc_unwind=true fence_retention=true consumed_once=true stale_creation=true");
        return true;
    }
#else
    inline std::expected<void, ::GEngine::PlatformError> CheckShaderReload(
        ::GEngine::EngineContext& context, ::GEngine::SceneRenderResources& owner, ::GEngine::_Scene& scene,
        ::GEngine::MaterialHandle original, ::GEngine::MaterialHandle clone,
        const ::GEngine::MaterialShaderDescription& before, const ::GEngine::MaterialShaderDescription& after,
        const ::GEngine::MaterialShaderDescription& broken, const ::GEngine::MaterialShaderDescription& schema,
        ::GEngine::FrameSubmission& submission, ::GEngine::FrameSubmissionDesc targets,
        ::GEngine::EntityPickTable& pick, const ::GEngine::FrameCamera& camera)
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Asset;
        std::size_t checks = 0;
        const auto require = [&](bool ok, const char* detail) {
            ++checks; if (!ok) Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_11_FAIL {}", detail); return ok;
        };
#define SH11_CHECK(condition, detail) if (!require(bool(condition), detail)) return std::unexpected(PlatformError{PlatformErrorCode::Initialization, "Phase 11 shader reload", detail})
        const auto read = [](SceneRenderResources& resources, MaterialHandle handle) {
            auto access = resources.Publication().BeginFrame();
            return resources.Materials().Acquire(access, handle);
        };
        const auto programOf = [&](SceneRenderResources& resources, MaterialHandle handle) {
            auto member = read(resources, handle);
            return member ? (*member)->Declaration()->State().Description().program : ShaderProgramHandle{};
        };
        const auto code = [](const auto& result, SceneResourceCode expected) {
            return !result && std::get_if<SceneResourceCode>(&result.error().cause) &&
                std::get<SceneResourceCode>(result.error().cause) == expected;
        };
        SH11_CHECK(CheckReloadRegistryPreparation(), "prepared registry allocation rollback/fences");
        const auto sourceProgram = programOf(owner, original);
        const auto source = read(owner, original), cloned = read(owner, clone);
        SH11_CHECK(source && cloned && source->Identity() != cloned->Identity(), "independent clone identities");
        const auto cacheBefore = owner.ShaderWork().programs;
        auto malformed = owner.ReloadShader(original, schema);
        SH11_CHECK(code(malformed, SceneResourceCode::ReloadSchema), "incompatible schema rejected");
        auto invalid = owner.ReloadShader(original, before, {99});
        SH11_CHECK(!invalid && std::get_if<ShaderError>(&invalid.error().cause), "undeclared variant typed error");
        SH11_CHECK(!owner.ReloadShader(MaterialHandle{}, after), "invalid target rejected");
        SH11_CHECK(!owner.ReloadShader(MaterialHandle{original.index, original.generation, sourceProgram.registry}, after), "wrong registry domain rejected");
        auto ticket = owner.PrepareShaderReload(original, after);
        SH11_CHECK(ticket && owner.ShaderWork().programs == cacheBefore, "preparation leaves cache unpublished");
        SH11_CHECK(code(owner.PrepareShaderReload(original, after), SceneResourceCode::ReloadBusy), "one outstanding preparation");
        {
            auto access = owner.Publication().BeginFrame();
            SH11_CHECK(code(owner.CommitShaderReload(*ticket), SceneResourceCode::PublicationBusy), "active frame blocks commit");
            SH11_CHECK(code(owner.CancelShaderReload(*ticket), SceneResourceCode::PublicationBusy), "active frame blocks cancellation cleanup");
        }
        SH11_CHECK(owner.CancelShaderReload(*ticket), "explicit cancellation");
        SH11_CHECK(code(owner.CommitShaderReload(*ticket), SceneResourceCode::ReloadCancelled), "cancelled ticket cannot commit");
        SH11_CHECK(programOf(owner, original) == sourceProgram && owner.ShaderWork().programs == cacheBefore, "cancel preserves graph/cache");
        ticket = owner.PrepareShaderReload(original, after);
        SH11_CHECK(ticket && owner.SetMaterialParameter(clone, "gain", .4f), "intervening authored edit");
        SH11_CHECK(code(owner.CommitShaderReload(*ticket), SceneResourceCode::ReloadStale), "changed member rejects whole candidate");
        SH11_CHECK(programOf(owner, original) == sourceProgram, "stale candidate preserves prior graph");
        SH11_CHECK(owner.SetMaterialParameter(clone, "gain", .35f), "restore intended clone value");
        ticket = owner.PrepareShaderReload(original, after);
        auto added = owner.CloneMaterial(original);
        SH11_CHECK(ticket && added, "new dependent after preparation");
        SH11_CHECK(code(owner.CommitShaderReload(*ticket), SceneResourceCode::ReloadStale), "new membership makes preparation stale");
        {
            auto access = owner.Publication().BeginPublication();
            SH11_CHECK(owner.Materials().Destroy(access, *added), "remove temporary dependent");
            owner.Materials().Collect(access);
        }
        added = owner.CloneMaterial(original);
        SH11_CHECK(added, "temporary target creation");
        ticket = owner.PrepareShaderReload(*added, after);
        {
            auto access = owner.Publication().BeginPublication();
            SH11_CHECK(ticket && owner.Materials().Destroy(access, *added), "destroy prepared target");
        }
        SH11_CHECK(code(owner.CommitShaderReload(*ticket), SceneResourceCode::ReloadStale), "destroyed target rejects replacement");
        ticket = owner.PrepareShaderReload(original, after);
        auto isolated = SceneRenderResources::Create(context);
        SH11_CHECK(ticket && isolated && !(*isolated)->CommitShaderReload(*ticket), "foreign reload ticket rejected");
        SH11_CHECK(!(*isolated)->ReloadShader(original, after), "foreign material rejected");
        SH11_CHECK(owner.CancelShaderReload(*ticket), "cancel after foreign probe");
        auto ordinary = owner.CreateMaterial(MaterialAuthoringDesc{.kind = MaterialKind::Unlit});
        SH11_CHECK(ordinary && code(owner.ReloadShader(*ordinary, after), SceneResourceCode::InvalidMaterial), "standard material not a custom reload target");
        auto otherVariant = owner.CreateMaterial(before, {1});
        SH11_CHECK(otherVariant, "independent variant creation");
        const auto variantProgram = programOf(owner, *otherVariant);
        std::optional<RenderFrame> oldFrame, newFrame;
        {
            auto access = owner.Publication().BeginFrame(); RenderExtractionStats stats;
            auto extracted = ExtractRenderFrame(scene, owner.ForFrame(access), stats, {&camera, 1});
            SH11_CHECK(extracted, "retained old frame extraction");
            oldFrame = std::move(*extracted);
        }
        const auto oldClone = read(owner, clone);
        auto replacement = owner.ReloadShader(original, after);
        SH11_CHECK(replacement && replacement->changed && replacement->materials == 2, "one atomic two-material replacement");
        const auto changedProgram = programOf(owner, original);
        const auto changed = read(owner, original), changedClone = read(owner, clone);
        SH11_CHECK(changed && changedClone && changedProgram != sourceProgram && programOf(owner, clone) == changedProgram,
            "shared instances switch together");
        SH11_CHECK(changed->Identity() == original && changedClone->Identity() == clone &&
            (*changed)->Template() == (*source)->Template() && changed->Revision() == source->Revision() + 1 &&
            std::ranges::equal((*changed)->Values(), (*source)->Values()) &&
            std::ranges::equal((*changedClone)->Values(), (*oldClone)->Values()) &&
            (*changedClone)->Revision() == (*oldClone)->Revision(), "handles, effective values and authored revisions retained");
        SH11_CHECK(programOf(owner, *otherVariant) == variantProgram, "distinct variant remains unchanged");
        SH11_CHECK(code(owner.CommitShaderReload(replacement->ticket), SceneResourceCode::ReloadStale), "consumed ticket cannot commit twice");
        bool oldFound = false;
        for (const auto& resource : oldFrame->Resources())
            if (resource.Material().Instance() == original)
                oldFound = resource.Material().Program().Identity() == sourceProgram;
        SH11_CHECK(oldFound, "old frame retains original exact program");
        {
            auto access = owner.Publication().BeginFrame(); RenderExtractionStats stats;
            auto extracted = ExtractRenderFrame(scene, owner.ForFrame(access), stats, {&camera, 1});
            SH11_CHECK(extracted, "new frame extraction");
            newFrame = std::move(*extracted);
            targets.pipelines = owner.Pipelines();
            SH11_CHECK(submission.Submit(*oldFrame, targets, pick), "retained old frame submission after replacement");
            SH11_CHECK(submission.Submit(*newFrame, targets, pick), "new graph frame submission");
        }
        submission.InvalidatePassContents();
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_11_FRAMES_PASS old_and_new_submitted=true retained_original_program=true");
        {
            ShaderObservationScope observation;
            auto unchanged = owner.ReloadShader(original, after);
            auto restored = owner.ReloadShader(original, before);
            auto reapplied = owner.ReloadShader(original, after);
            SH11_CHECK(unchanged && !unchanged->changed && restored && reapplied && ShaderObservation().Empty(), "no-op and warmed reload compile/link/reflect zero");
        }
        const auto programsBeforeFailure = owner.ShaderWork().programs;
        auto bad = owner.ReloadShader(original, broken);
        SH11_CHECK(!bad && std::get_if<ShaderError>(&bad.error().cause) &&
            std::get<ShaderError>(bad.error().cause).code == ShaderErrorCode::Compile, "typed invalid shader compilation");
        SH11_CHECK(programOf(owner, original) == changedProgram && programOf(owner, clone) == changedProgram &&
            owner.ShaderWork().programs == programsBeforeFailure, "compile failure retains entire last-valid graph");
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_11_EXPECTED_DIAGNOSTIC {}", DescribeSceneResourceError(bad.error()));
        const auto selected = before.Program().Select();
        SH11_CHECK(selected && (*selected)->stages.size() == 2, "controlled source stages");
        std::vector<MaterialShaderParameter> parameters;
        for (const auto& parameter : before.Parameters())
        {
            const auto found = std::find_if(before.Program().Bindings().begin(), before.Program().Bindings().end(),
                [&](const auto& binding) { return binding.name == parameter.name; });
            SH11_CHECK(found != before.Program().Bindings().end(), "parameter semantic provider");
            parameters.push_back({parameter, found->required});
        }
        const auto descriptionFor = [&](const std::string& vertex, const std::string& fragment) {
            const ShaderSource stages[]{ {VERTEX, vertex, "reload focused vertex"}, {FRAGMENT, fragment, "reload focused fragment"} };
            return MaterialShaderDescription::Create({stages, parameters, before.Textures(), {}});
        };
        const std::string vertex = (*selected)->stages[0].source;
        const std::string missingFragment = "#version 450 core\nlayout(location=0) out vec4 FragColor;\nuniform vec4 tint;\nvoid main(){FragColor=tint;}\n";
        const std::string mismatchFragment = "#version 450 core\nlayout(location=0) out vec4 FragColor;\nuniform vec4 tint;\nuniform vec3 gain;\nvoid main(){FragColor=vec4(tint.rgb*gain,1);}\n";
        for (const auto& [fragment, expected] : {std::pair{missingFragment, ShaderErrorCode::MissingBinding},
                                               std::pair{mismatchFragment, ShaderErrorCode::BindingType}})
        {
            auto description = descriptionFor(vertex, fragment);
            SH11_CHECK(description, "invalid reflected interface CPU description");
            auto result = owner.ReloadShader(original, *description);
            SH11_CHECK(!result && std::get_if<ShaderError>(&result.error().cause) &&
                std::get<ShaderError>(result.error().cause).code == expected &&
                programOf(owner, original) == changedProgram, "reflected semantic failure retains graph");
        }
        std::string linkVertex = vertex;
        const auto main = linkVertex.find("void main()");
        SH11_CHECK(main != std::string::npos, "controlled vertex main");
        linkVertex.insert(main, "out vec3 reloadLink;\n");
        const auto body = linkVertex.find('{', linkVertex.find("void main()"));
        linkVertex.insert(body + 1, "reloadLink=aPos;");
        const std::string linkFragment = "#version 450 core\nin vec2 reloadLink;\nlayout(location=0) out vec4 FragColor;\nuniform vec4 tint;\nuniform float gain;\nvoid main(){FragColor=vec4(tint.rgb*gain+vec3(reloadLink,0),1);}\n";
        auto linkDescription = descriptionFor(linkVertex, linkFragment);
        SH11_CHECK(linkDescription, "link failure fixture description");
        auto linkFailure = owner.ReloadShader(original, *linkDescription);
        SH11_CHECK(!linkFailure && std::get_if<ShaderError>(&linkFailure.error().cause) &&
            std::get<ShaderError>(linkFailure.error().cause).code == ShaderErrorCode::Link &&
            programOf(owner, original) == changedProgram && owner.ShaderWork().programs == programsBeforeFailure,
            "link failure preserves graph and cache");
        BeginReloadLifetimeObservation();
        {
            auto limited = SceneRenderResources::Create(context, {}, {}, {.materials = {.maxRevision = 2}});
            SH11_CHECK(limited, "limited graph owner");
            auto first = (*limited)->CreateMaterial(before), last = (*limited)->CreateMaterial(before);
            SH11_CHECK(first && last && (*limited)->SetMaterialParameter(*last, "gain", .3f), "last member exhausts revision");
            const auto priorProgram = programOf(**limited, *first);
            auto failure = (*limited)->ReloadShader(*first, after);
            SH11_CHECK(!failure && std::get_if<RegistryError>(&failure.error().cause) &&
                std::get<RegistryError>(failure.error().cause) == RegistryError::RevisionExhausted, "late preparation failure is typed");
            SH11_CHECK(programOf(**limited, *first) == priorProgram && programOf(**limited, *last) == priorProgram &&
                (*limited)->ShaderWork().programs == 1 && read(**limited, *first)->Revision() == 1, "late failure publishes no partial graph or cache entry");
            auto capacity = SceneRenderResources::Create(context, {}, {}, {.programs = {.maxSlots = 1}});
            SH11_CHECK(capacity, "bounded program owner");
            auto one = (*capacity)->CreateMaterial(before);
            SH11_CHECK(one, "bounded initial material");
            auto exhausted = (*capacity)->ReloadShader(*one, after);
            SH11_CHECK(!exhausted && std::get_if<RegistryError>(&exhausted.error().cause) &&
                std::get<RegistryError>(exhausted.error().cause) == RegistryError::SlotsExhausted &&
                (*capacity)->ShaderWork().programs == 1, "program allocation capacity rollback");
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_11_LIMITS_PASS material_revision_exhausted=true program_slots_exhausted=true typed_capacity_not_bad_alloc=true");
            auto cache = SceneRenderResources::Create(context);
            SH11_CHECK(cache, "bounded 64-entry cache owner");
            MaterialHandle selectedMaterial;
            for (unsigned i = 0; i < 64; ++i)
            {
                const std::string fragment = (*selected)->stages[1].source + "\n// reload capacity entry " + std::to_string(i);
                auto description = descriptionFor(vertex, fragment);
                SH11_CHECK(description, "capacity fixture description");
                auto material = (*cache)->CreateMaterial(*description);
                SH11_CHECK(material, "admitted finite cache entry");
                if (!i) selectedMaterial = *material;
            }
            const auto boundedProgram = programOf(**cache, selectedMaterial);
            ShaderObservationScope observation;
            auto full = (*cache)->ReloadShader(selectedMaterial, after);
            SH11_CHECK(!full && std::get_if<ShaderError>(&full.error().cause) &&
                std::get<ShaderError>(full.error().cause).code == ShaderErrorCode::Capacity &&
                (*cache)->ShaderWork().programs == 64 && programOf(**cache, selectedMaterial) == boundedProgram &&
                ShaderObservation().Empty(), "full cache reload has no eviction or compilation");
        }
        SH11_CHECK(EndReloadLifetimeObservation(), "no leaked or duplicate-destroyed programs");
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_11_API_PASS checks={} shared_clone_values=true atomic=true cancelled=true stale=true injected_bad_alloc_unwind=true", checks);
#undef SH11_CHECK
        return {};
    }
#endif
}
