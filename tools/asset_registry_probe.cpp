#include "Assets/AssetRegistry.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>
#include <unordered_map>

#if defined(GENGINE_REGISTRY_GL)
#include "gepch.h"
#include "Core/GEngine.h"
#include "Core/RuntimeAssets.h"
#include "Assets/Shaders/Shader.h"
#endif

namespace { thread_local int failAllocationAfter = -1; }
void* operator new(std::size_t size)
{
    if (failAllocationAfter == 0) throw std::bad_alloc();
    if (failAllocationAfter > 0) --failAllocationAfter;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace
{
    using namespace GEngine::Asset;
    int checks = 0;
    bool forbiddenDestruction = false;
    void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
    template<class Exception = std::logic_error, class F> void Reject(F&& operation)
    {
        try { operation(); } catch (const Exception&) { ++checks; return; }
        throw std::runtime_error("Expected rejection did not occur");
    }
    struct Counts
    {
        int created = 0, destroyed = 0;
        bool correctThread = true;
        const std::thread::id owner = std::this_thread::get_id();
    };
    struct Payload
    {
        Counts& counts;
        const int value;
        Payload(Counts& c, int v) : counts(c), value(v)
        {
            if (v < 0) throw std::runtime_error("Deliberate resource creation failure");
            ++counts.created;
        }
        Payload(const Payload&) = delete;
        Payload& operator=(const Payload&) = delete;
        ~Payload()
        {
            if (forbiddenDestruction) std::_Exit(87);
            counts.correctThread &= counts.owner == std::this_thread::get_id();
            ++counts.destroyed;
        }
    };
    using Registry = AssetRegistry<MeshHandle, Payload>;
    static_assert(std::is_trivially_copyable_v<MeshHandle> && !std::is_convertible_v<TextureHandle, MeshHandle>);
    static_assert(!std::is_convertible_v<unsigned, MeshHandle> && !std::is_convertible_v<MeshHandle, unsigned>);
    static_assert(!std::is_copy_constructible_v<Registry> && !std::is_move_constructible_v<Registry>);
    static_assert(std::is_same_v<decltype(std::declval<Registry::Lease>().Get()), const Payload*>);
    static_assert(!std::is_same_v<MeshHandle, SamplerHandle> && !std::is_same_v<ShaderProgramHandle, PipelineHandle>
        && !std::is_same_v<MaterialTemplateHandle, MaterialInstanceHandle>);

#if defined(GENGINE_WRONG_HANDLE_ACCESS)
    void WrongType(Registry& registry, AssetPublication::FrameAccess& frame)
    { (void)registry.Acquire(frame, TextureHandle{}); }
#endif
#if defined(GENGINE_WRONG_HANDLE_ASSIGN)
    MeshHandle wrongTypeAssignment = ShaderProgramHandle{};
#endif

    void IdentityAndVersions()
    {
        Counts c;
        AssetPublication publication;
        MeshHandle stale;
        {
            Registry registry(publication), other(publication);
            MeshHandle handle, foreign;
            Check(!MeshHandle{} && MeshHandle{}.index == MeshHandle::NullIndex, "Null representation changed");
            {
                auto p = publication.BeginPublication();
                handle = registry.Create(p, c, 11); foreign = other.Create(p, c, 77);
                Check(handle.index == foreign.index && handle.generation == foreign.generation
                    && handle.registry != foreign.registry, "Registry lifetime identity collided");
                Reject([&] { (void)publication.BeginFrame(); });
                Reject([&] { (void)publication.BeginPublication(); });
            }
            Registry::Lease old;
            {
                auto frame = publication.BeginFrame();
                old = registry.Acquire(frame, handle);
                Check(old && old->value == 11 && old.Identity() == handle && old.Revision() == 1, "Create/get differs");
                Check(!registry.Acquire(frame, {}) && !registry.Acquire(frame, foreign), "Null/foreign handle resolved");
                auto bad = handle; bad.index = MeshHandle::NullIndex - 1;
                Check(!registry.Acquire(frame, bad), "Out-of-range handle resolved");
                bad = handle; bad.generation = 0; Check(!registry.Acquire(frame, bad), "Zero generation resolved");
                bad = handle; bad.generation += 1; Check(!registry.Acquire(frame, bad), "Invalid generation resolved");
                bad = handle; bad.registry = 0; Check(!registry.Acquire(frame, bad), "Zero registry resolved");
                Reject([&] { (void)publication.BeginPublication(); });
                Reject([&] { Registry forbidden(publication); });
            }
            {
                auto p = publication.BeginPublication();
                Check(registry.Replace(p, handle, c, 22), "Replacement failed");
                Check(registry.Collect(p) == 0 && !registry.Close(p), "Retained old version was retired");
                Check(old->value == 11 && old.Revision() == 1, "Replacement mutated retained version");
            }
            {
                auto frame = publication.BeginFrame();
                const auto current = registry.Acquire(frame, handle);
                Check(current && current.Revision() == 2 && current->value == 22 && current.Get() != old.Get(),
                    "Replacement did not publish a separate revision at stable identity");
            }
            const int beforeWorker = c.destroyed;
            std::thread worker([lease = std::move(old)]() mutable { lease = {}; }); worker.join();
            Check(c.destroyed == beforeWorker, "Worker lease release destroyed a resource");
            {
                auto p = publication.BeginPublication();
                Check(registry.Collect(p) == 1, "Old version did not retire at safe point");
                Check(registry.Destroy(p, handle) && !registry.Destroy(p, handle), "Destroy/double destroy differs");
                stale = handle;
                const auto reused = registry.Create(p, c, 33);
                Check(reused.index == stale.index && reused.generation == stale.generation + 1, "Reuse did not advance generation");
                handle = reused;
                Check(!registry.Replace(p, stale, c, 99) && !registry.Destroy(p, foreign), "Stale/foreign mutation succeeded");
                Check(registry.Collect(p) == 1, "Destroyed version did not retire");
            }
            {
                auto frame = publication.BeginFrame();
                Check(!registry.Acquire(frame, stale) && registry.Acquire(frame, handle)->value == 33, "Reuse revived stale identity");
            }
            AssetPublication unrelated;
            {
                auto p = unrelated.BeginPublication();
                Reject([&] { (void)registry.Create(p, c, 9); });
            }
            {
                auto frame = unrelated.BeginFrame();
                Reject([&] { (void)registry.Acquire(frame, handle); });
            }
            {
                auto p = publication.BeginPublication();
                std::array<bool, 3> rejected{};
                std::thread wrong([&] {
                    try { (void)registry.Create(p, c, 9); } catch (const std::logic_error&) { rejected[0] = true; }
                    try { (void)registry.Size(); } catch (const std::logic_error&) { rejected[1] = true; }
                    try { (void)publication.BeginFrame(); } catch (const std::logic_error&) { rejected[2] = true; }
                }); wrong.join();
                Check(rejected[0] && rejected[1] && rejected[2], "Worker gained registry/publication access");
                Check(registry.Close(p) && registry.Close(p) && other.Close(p), "Close failed or was not idempotent");
                Reject([&] { (void)registry.Create(p, c, 4); });
            }
            { auto f = publication.BeginFrame(); Check(!registry.Acquire(f, handle), "Closed registry resolved a handle"); }
        }
        {
            Registry fresh(publication);
            MeshHandle handle;
            { auto p = publication.BeginPublication(); handle = fresh.Create(p, c, 44); }
            { auto f = publication.BeginFrame(); Check(!fresh.Acquire(f, stale) && handle.registry != stale.registry, "New registry revived old lifetime"); }
        }
        Check(c.created == c.destroyed && c.correctThread, "Identity/version lifetime leaked or destroyed off-thread");
        std::cout << "[PASS] identity/null/stale/foreign/revision/publication/worker-retirement\n";
    }

    void GrowthAndExhaustion()
    {
        Counts c;
        AssetPublication publication;
        {
            Registry registry(publication);
            std::vector<MeshHandle> handles;
            MeshHandle first;
            { auto p = publication.BeginPublication(); first = registry.Create(p, c, 5); }
            Registry::Lease retained;
            { auto f = publication.BeginFrame(); retained = registry.Acquire(f, first); }
            const auto* address = retained.Get();
            {
                auto p = publication.BeginPublication();
                for (int i = 0; i < 20000; ++i) handles.push_back(registry.Create(p, c, i));
            }
            {
                auto f = publication.BeginFrame();
                Check(registry.Acquire(f, first).Get() == address && retained->value == 5, "Storage growth invalidated retained address");
                for (int i = 0; i < 20000; ++i) Check(registry.Acquire(f, handles[i])->value == i, "Growth changed identity/payload");
            }
            std::unordered_map<MeshHandle, int> keys;
            for (int i = 0; i < 20000; ++i) keys.emplace(handles[i], i);
            Check(keys.size() == 20000 && keys.at(handles.back()) == 19999, "Typed hashing/equality collision");
            {
                auto p = publication.BeginPublication();
                for (int i = 0; i < 20000; i += 2) Check(registry.Destroy(p, handles[i]), "Growth destroy failed");
                for (int i = 0; i < 10000; ++i) (void)registry.Create(p, c, i + 30000);
            }
            {
                auto f = publication.BeginFrame();
                for (int i = 0; i < 20000; ++i)
                    Check(bool(registry.Acquire(f, handles[i])) == (i % 2 == 1), "Many reuses revived stale handle");
            }
            retained = {};
            { auto p = publication.BeginPublication(); Check(registry.Close(p), "Growth close failed"); }
        }
        {
            Registry registry(publication, {2, 2, 2});
            auto p = publication.BeginPublication();
            auto h = registry.Create(p, c, 1);
            Check(registry.Replace(p, h, c, 2), "Revision transition failed");
            Reject<std::overflow_error>([&] { (void)registry.Replace(p, h, c, 3); });
            Check(registry.Destroy(p, h), "First generation destroy failed");
            const auto second = registry.Create(p, c, 2);
            Check(second.index == h.index && second.generation == 2, "Generation limit fixture did not reuse slot");
            Check(registry.Destroy(p, second), "Final generation destroy failed");
            const auto quarantined = registry.Create(p, c, 3);
            Check(quarantined.index != h.index && quarantined.generation == 1, "Exhausted slot was reused");
            Check(registry.Destroy(p, quarantined), "Second slot destroy failed");
            const auto last = registry.Create(p, c, 4);
            Check(registry.Destroy(p, last), "Final slot destroy failed");
            Reject<std::overflow_error>([&] { (void)registry.Create(p, c, 5); });
            Check(registry.Size() == 0 && registry.Close(p), "Exhaustion corrupted registry");
        }
        std::atomic<std::uint64_t> sequence{(std::numeric_limits<std::uint64_t>::max)() - 1};
        Check(detail::TakeRegistryIdentity(sequence) == (std::numeric_limits<std::uint64_t>::max)() - 1, "Identity boundary differs");
        Check(detail::TakeRegistryIdentity(sequence) == (std::numeric_limits<std::uint64_t>::max)(), "Final registry ID was skipped");
        Reject<std::overflow_error>([&] { (void)detail::TakeRegistryIdentity(sequence); });
        Check(sequence == 0, "Registry identity wrapped");
        Reject<std::invalid_argument>([&] { Registry invalid(publication, {0, 1, 1}); });
        Check(c.created == c.destroyed && c.correctThread, "Growth/exhaustion leaked resources");
        std::cout << "[PASS] 30001 allocations/stable-addresses/generation-quarantine/revision-and-domain-exhaustion\n";
    }

    struct FenceState { bool complete = false; int polls = 0, destroyed = 0; bool owner = true; std::thread::id thread = std::this_thread::get_id(); };
    struct Fence final : AssetRetirementFence
    {
        FenceState& state;
        explicit Fence(FenceState& s) : state(s) {}
        ~Fence() override { ++state.destroyed; state.owner &= state.thread == std::this_thread::get_id(); }
        bool IsComplete() const noexcept override
        { ++state.polls; state.owner &= state.thread == std::this_thread::get_id(); return state.complete; }
    };
    void FencesAndFailures()
    {
        Counts c;
        AssetPublication publication;
        {
            Registry registry(publication);
            MeshHandle h;
            { auto p = publication.BeginPublication(); h = registry.Create(p, c, 1); }
            FenceState a, b;
            {
                auto f = publication.BeginFrame();
                auto lease = registry.Acquire(f, h);
                registry.ProtectGpuUse(f, lease, std::make_unique<Fence>(a));
                registry.ProtectGpuUse(f, lease, std::make_unique<Fence>(b));
            }
            {
                auto p = publication.BeginPublication();
                Check(registry.Destroy(p, h), "Fenced destroy failed");
                Check(registry.Collect(p) == 0 && !registry.Close(p) && c.destroyed == 0, "Incomplete GPU work retired resource");
                a.complete = true;
                Check(registry.Collect(p) == 0 && a.destroyed == 1 && !b.destroyed, "Independent GPU fences were merged unsafely");
                b.complete = true;
                Check(registry.Collect(p) == 1 && registry.Close(p) && c.destroyed == 1, "Completed GPU work did not retire");
            }
            Check(a.owner && b.owner && a.polls && b.polls && b.destroyed == 1, "Fences polled/destroyed off owner thread");
        }
        int allocationFailures = 0, successes = 0;
        for (int operation = 0; operation < 2; ++operation)
        {
            for (int budget = 0; budget < 12; ++budget)
            {
                Registry registry(publication);
                MeshHandle h, created;
                { auto p = publication.BeginPublication(); h = registry.Create(p, c, 10); }
                bool failed = false;
                {
                    auto p = publication.BeginPublication();
                    failAllocationAfter = budget;
                    try
                    {
                        if (operation) (void)registry.Replace(p, h, c, 20);
                        else created = registry.Create(p, c, 30);
                    }
                    catch (const std::bad_alloc&) { failed = true; }
                    failAllocationAfter = -1;
                }
                allocationFailures += failed; successes += !failed;
                {
                    auto f = publication.BeginFrame();
                    const auto original = registry.Acquire(f, h);
                    Check(original->value == (operation && !failed ? 20 : 10), "Failed allocation changed published resource");
                    Check(original.Revision() == (operation && !failed ? 2 : 1), "Failed allocation advanced revision");
                    Check(registry.Size() == (!operation && !failed ? 2 : 1), "Failed allocation consumed slot");
                }
            }
        }
        Check(allocationFailures >= 4 && successes > 0, "Allocation controls did not cover failure and recovery");
        {
            Registry registry(publication);
            auto p = publication.BeginPublication();
            const auto h = registry.Create(p, c, 1);
            Reject<std::runtime_error>([&] { (void)registry.Create(p, c, -1); });
            Reject<std::runtime_error>([&] { (void)registry.Replace(p, h, c, -1); });
            failAllocationAfter = 0;
            bool failed = false;
            try { (void)registry.Destroy(p, h); } catch (const std::bad_alloc&) { failed = true; }
            failAllocationAfter = -1;
            Check(failed && registry.Size() == 1, "Failed retirement allocation invalidated current handle");
            Check(registry.Destroy(p, h) && registry.Close(p), "Destroy did not recover after allocation failure");
        }
        Check(c.created == c.destroyed && c.correctThread, "Failure unwind leaked/destroyed on wrong thread");
        std::cout << "[PASS] GPU-retention/failure-atomicity allocation-failures=" << allocationFailures << " successes=" << successes << '\n';
    }

    void Rejection(std::string_view mode)
    {
        std::set_terminate([] { std::cerr << "[EXPECTED] unsafe registry teardown rejected\n"; std::_Exit(86); });
        Counts c;
        AssetPublication publication;
        auto registry = std::make_unique<Registry>(publication);
        MeshHandle h;
        { auto p = publication.BeginPublication(); h = registry->Create(p, c, 1); }
        if (mode == "--reject-live-lease")
        {
            Registry::Lease lease;
            { auto f = publication.BeginFrame(); lease = registry->Acquire(f, h); }
            forbiddenDestruction = true; registry.reset();
        }
        else if (mode == "--reject-worker-retirement")
        {
            forbiddenDestruction = true;
            std::thread worker([&] {
                std::set_terminate([] { std::cerr << "[EXPECTED] unsafe registry teardown rejected\n"; std::_Exit(86); });
                registry.reset();
            }); worker.join();
        }
        else if (mode == "--reject-live-fence")
        {
            FenceState state;
            { auto f = publication.BeginFrame(); registry->ProtectGpuUse(f, registry->Acquire(f, h), std::make_unique<Fence>(state)); }
            forbiddenDestruction = true; registry.reset();
        }
        else throw std::invalid_argument("Unknown rejection mode");
        std::_Exit(89);
    }

#if defined(GENGINE_REGISTRY_GL)
    struct GlFence final : AssetRetirementFence
    {
        GLsync sync = nullptr;
        void Signal() { sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); Check(sync != nullptr, "GL fence creation failed"); }
        bool IsComplete() const noexcept override
        {
            if (!sync) return false;
            const auto status = glClientWaitSync(sync, 0, 0);
            return status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED;
        }
        ~GlFence() override { if (sync) glDeleteSync(sync); }
    };
    int deletedPrograms = 0, diagnosticErrors = 0, diagnosticMarkers = 0;
    SDL_GLContext expectedContext = nullptr;
    std::thread::id expectedThread;
    PFNGLDELETEPROGRAMPROC originalDelete = nullptr;
    bool correctDeletion = true;
    void APIENTRY ObserveDelete(GLuint id)
    {
        ++deletedPrograms;
        correctDeletion &= std::this_thread::get_id() == expectedThread && SDL_GL_GetCurrentContext() == expectedContext;
        originalDelete(id);
    }
    void APIENTRY Diagnostics(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* message, const void*)
    {
        if (source == GL_DEBUG_SOURCE_APPLICATION && type == GL_DEBUG_TYPE_MARKER && id == 25001) ++diagnosticMarkers;
        else if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH || severity == GL_DEBUG_SEVERITY_MEDIUM)
        { ++diagnosticErrors; std::cerr << "[GL] " << message << '\n'; }
    }
    Shader Program()
    {
        const std::array<ShaderSource, 2> sources{{
            {VERTEX, "#version 460 core\nvoid main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(1,-1),vec2(0,1));gl_Position=vec4(p[gl_VertexID],0,1);}", "registry.vert"},
            {FRAGMENT, "#version 460 core\nout vec4 color;void main(){color=vec4(1,0,0,1);}", "registry.frag"}}};
        auto result = CreateShaderProgram(sources);
        Check(std::holds_alternative<Shader>(result), "Registry shader creation failed");
        return std::move(std::get<Shader>(result));
    }
    void GlIntegration(bool rejectRoot)
    {
        using namespace ::GEngine;
        RuntimeAssets::Initialize("GEngineEditor");
        ShaderProgramHandle previous{};
        for (int cycle = 0; cycle < 2; ++cycle)
        {
            auto root = std::make_unique<EngineContext>();
            Reject([&] { (void)root->AssetPublications(); });
            WindowProperties properties;
            properties.m_Title = "Phase 25 registry";
            properties.m_Width = properties.m_Height = 64;
            properties.m_MinWidth = properties.m_MinHeight = 32;
            properties.m_IsVsync = false;
            properties.flag = BitFlags<WindowFlags, uint8_t>{WindowFlags::INVISIBLE};
            root->Initialize({properties});
            auto& publication = root->AssetPublications();
            std::cout << "[GL] " << glGetString(GL_VERSION) << " renderer=" << glGetString(GL_RENDERER) << '\n';
            int startupErrors = 0; while (glGetError() != GL_NO_ERROR) ++startupErrors;
            std::cout << "[INFO] existing startup errors isolated=" << startupErrors << '\n';
            glEnable(GL_DEBUG_OUTPUT); glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(Diagnostics, nullptr);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 25001, GL_DEBUG_SEVERITY_LOW, -1, "Registry diagnostics");
            expectedContext = SDL_GL_GetCurrentContext(); expectedThread = std::this_thread::get_id();
            originalDelete = glad_glDeleteProgram; glad_glDeleteProgram = ObserveDelete;
            {
                AssetRegistry<ShaderProgramHandle, Shader> registry(publication);
                ShaderProgramHandle handle;
                { auto p = publication.BeginPublication(); handle = registry.Create(p, Program()); }
                if (rejectRoot)
                {
                    std::set_terminate([] {
                        if (SDL_GL_GetCurrentContext() != expectedContext) std::_Exit(87);
                        std::cerr << "[EXPECTED] root rejected live registry\n"; std::_Exit(86);
                    });
                    root.reset(); std::_Exit(89);
                }
                decltype(registry)::Lease old;
                GLuint oldProgram = 0, replacement = 0, vao = 0;
                glGenVertexArrays(1, &vao); glBindVertexArray(vao);
                {
                    auto f = publication.BeginFrame();
                    old = registry.Acquire(f, handle);
                    Check(!registry.Acquire(f, previous), "New root resolved a prior-root handle");
                    oldProgram = old->GetHandle();
                    auto fence = std::make_unique<GlFence>(); auto* signal = fence.get();
                    registry.ProtectGpuUse(f, old, std::move(fence));
                    old->Bind(); glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, 64, 64);
                    glDrawArrays(GL_TRIANGLES, 0, 3); signal->Signal(); old->UnBind();
                }
                {
                    auto p = publication.BeginPublication();
                    Check(registry.Replace(p, handle, Program()), "Real shader replacement failed");
                    Check(registry.Collect(p) == 0 && glIsProgram(oldProgram), "CPU lease failed to retain real program");
                }
                {
                    auto f = publication.BeginFrame();
                    auto current = registry.Acquire(f, handle); replacement = current->GetHandle();
                    Check(current.Revision() == 2 && current.Identity() == old.Identity() && replacement != oldProgram,
                        "Shader identity/revision/GL name were conflated");
                    current->Bind(); current->UnBind();
                }
                const auto before = deletedPrograms;
                std::thread worker([lease = std::move(old)]() mutable { lease = {}; }); worker.join();
                Check(deletedPrograms == before && glIsProgram(oldProgram), "Worker retired real shader program");
                glFinish();
                {
                    auto p = publication.BeginPublication();
                    Check(registry.Collect(p) == 1 && !glIsProgram(oldProgram), "Completed GPU/CPU use did not retire old shader");
                    Check(registry.Destroy(p, handle) && registry.Collect(p) == 1 && !glIsProgram(replacement)
                        && registry.Close(p), "Real shader close leaked");
                }
                glBindVertexArray(0); glDeleteVertexArrays(1, &vao);
                previous = handle;
            }
            glad_glDeleteProgram = originalDelete;
            Check(correctDeletion && deletedPrograms == (cycle + 1) * 2 && diagnosticErrors == 0
                && diagnosticMarkers == cycle + 1 && glGetError() == GL_NO_ERROR, "Registry GL lifetime/diagnostics failed");
            root.reset();
            Check(SDL_GL_GetCurrentContext() == nullptr && EngineContext::TryGet() == nullptr && !diagnosticErrors,
                "Root teardown failed after registry retirement");
        }
        std::cout << "[PASS] asset-registry-GL cycles=2 shader-programs-deleted=" << deletedPrograms << " checks=" << checks << '\n';
    }
#endif
}

int main(int argc, char** argv)
{
    try
    {
        const std::string_view mode = argc > 1 ? argv[1] : "--cpu";
#if defined(GENGINE_REGISTRY_GL)
        if (mode == "--gl" || mode == "--reject-root") { GlIntegration(mode == "--reject-root"); return 0; }
#endif
        if (mode.starts_with("--reject-")) { Rejection(mode); return 89; }
        if (mode != "--cpu") return 2;
        IdentityAndVersions(); GrowthAndExhaustion(); FencesAndFailures();
        std::cout << "[PASS] asset-registry-CPU checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    { failAllocationAfter = -1; std::cerr << "[FAIL] " << error.what() << '\n'; return 1; }
}
