#include "Assets/AssetRegistry.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>
#include <unordered_map>

#if defined(GENGINE_REGISTRY_GL)
#include "../GEngine/src/Core/FramebufferBackend.h"
#include "gepch.h"
#include "Core/GEngine.h"
#include "Core/RuntimeAssets.h"
#include "Assets/Shaders/Shader.h"
#include "Managers/AssetsManager.h"
#include "Core/RenderTarget.h"
#include "../GEngine/src/Assets/TextureBackend.h"
#include "stb_image/stb_image_write.h"
#include <fstream>
#include <cstring>
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
    using namespace ::GEngine::Asset;
    int checks = 0;
    bool forbiddenDestruction = false;
    void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
    template<class T, class E> void Check(const std::expected<T, E>& result, const char* message)
    { Check(bool(result), message); }
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
    { (void)registry.Acquire(frame, TextureHandle{}).value(); }
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
                handle = registry.Create(p, c, 11).value(); foreign = other.Create(p, c, 77).value();
                Check(handle.index == foreign.index && handle.generation == foreign.generation
                    && handle.registry != foreign.registry, "Registry lifetime identity collided");
            }
            Registry::Lease old;
            {
                auto frame = publication.BeginFrame();
                old = registry.Acquire(frame, handle).value();
                Check(old && old->value == 11 && old.Identity() == handle && old.Revision() == 1, "Create/get differs");
                Check(!registry.Acquire(frame, {}) && !registry.Acquire(frame, foreign), "Null/foreign handle resolved");
                auto bad = handle; bad.index = MeshHandle::NullIndex - 1;
                Check(!registry.Acquire(frame, bad), "Out-of-range handle resolved");
                bad = handle; bad.generation = 0; Check(!registry.Acquire(frame, bad), "Zero generation resolved");
                bad = handle; bad.generation += 1; Check(!registry.Acquire(frame, bad), "Invalid generation resolved");
                bad = handle; bad.registry = 0; Check(!registry.Acquire(frame, bad), "Zero registry resolved");
            }
            {
                auto p = publication.BeginPublication();
                Check(registry.Replace(p, handle, c, 22), "Replacement failed");
                Check(registry.Collect(p) == 0 && !registry.Close(p), "Retained old version was retired");
                Check(old->value == 11 && old.Revision() == 1, "Replacement mutated retained version");
            }
            {
                auto frame = publication.BeginFrame();
                const auto current = registry.Acquire(frame, handle).value();
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
                const auto reused = registry.Create(p, c, 33).value();
                Check(reused.index == stale.index && reused.generation == stale.generation + 1, "Reuse did not advance generation");
                handle = reused;
                Check(!registry.Replace(p, stale, c, 99) && !registry.Destroy(p, foreign), "Stale/foreign mutation succeeded");
                Check(registry.Collect(p) == 1, "Destroyed version did not retire");
            }
            {
                auto frame = publication.BeginFrame();
                Check(!registry.Acquire(frame, stale) && registry.Acquire(frame, handle).value()->value == 33, "Reuse revived stale identity");
            }
            {
                auto p = publication.BeginPublication();
                Check(registry.Close(p) && registry.Close(p) && other.Close(p), "Close failed or was not idempotent");
                Check(registry.Create(p, c, 4).error() == RegistryError::Closed, "Closed registry accepted publication");
            }
            { auto f = publication.BeginFrame(); Check(!registry.Acquire(f, handle), "Closed registry resolved a handle"); }
        }
        {
            Registry fresh(publication);
            MeshHandle handle;
            { auto p = publication.BeginPublication(); handle = fresh.Create(p, c, 44).value(); }
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
            { auto p = publication.BeginPublication(); first = registry.Create(p, c, 5).value(); }
            Registry::Lease retained;
            { auto f = publication.BeginFrame(); retained = registry.Acquire(f, first).value(); }
            const auto* address = retained.Get();
            {
                auto p = publication.BeginPublication();
                for (int i = 0; i < 20000; ++i) handles.push_back(registry.Create(p, c, i).value());
            }
            {
                auto f = publication.BeginFrame();
                Check(registry.Acquire(f, first).value().Get() == address && retained->value == 5, "Storage growth invalidated retained address");
                for (int i = 0; i < 20000; ++i) Check(registry.Acquire(f, handles[i]).value()->value == i, "Growth changed identity/payload");
            }
            std::unordered_map<MeshHandle, int> keys;
            for (int i = 0; i < 20000; ++i) keys.emplace(handles[i], i);
            Check(keys.size() == 20000 && keys.at(handles.back()) == 19999, "Typed hashing/equality collision");
            {
                auto p = publication.BeginPublication();
                for (int i = 0; i < 20000; i += 2) Check(registry.Destroy(p, handles[i]), "Growth destroy failed");
                for (int i = 0; i < 10000; ++i) (void)registry.Create(p, c, i + 30000).value();
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
            auto h = registry.Create(p, c, 1).value();
            Check(registry.Replace(p, h, c, 2), "Revision transition failed");
            Check(registry.Replace(p, h, c, 3).error() == RegistryError::RevisionExhausted, "Revision error cause lost");
            Check(registry.Destroy(p, h), "First generation destroy failed");
            const auto second = registry.Create(p, c, 2).value();
            Check(second.index == h.index && second.generation == 2, "Generation limit fixture did not reuse slot");
            Check(registry.Destroy(p, second), "Final generation destroy failed");
            const auto quarantined = registry.Create(p, c, 3).value();
            Check(quarantined.index != h.index && quarantined.generation == 1, "Exhausted slot was reused");
            Check(registry.Destroy(p, quarantined), "Second slot destroy failed");
            const auto last = registry.Create(p, c, 4).value();
            Check(registry.Destroy(p, last), "Final slot destroy failed");
            Check(registry.Create(p, c, 5).error() == RegistryError::SlotsExhausted, "Slot error cause lost");
            Check(registry.Size() == 0 && registry.Close(p), "Exhaustion corrupted registry");
        }
        std::atomic<std::uint64_t> sequence{(std::numeric_limits<std::uint64_t>::max)() - 1};
        Check(AssetDetail::TakeRegistryIdentity(sequence) == (std::numeric_limits<std::uint64_t>::max)() - 1, "Identity boundary differs");
        Check(AssetDetail::TakeRegistryIdentity(sequence) == (std::numeric_limits<std::uint64_t>::max)(), "Final registry ID was skipped");
        Check(AssetDetail::TakeRegistryIdentity(sequence).error() == RegistryError::IdentityExhausted, "Identity error cause lost");
        Check(sequence == 0, "Registry identity wrapped");
        { Registry invalid(publication, {0, 1, 1}); auto p = publication.BeginPublication();
            Check(invalid.Create(p, c, 1).error() == RegistryError::InvalidLimits, "Invalid limits were accepted"); }
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
            { auto p = publication.BeginPublication(); h = registry.Create(p, c, 1).value(); }
            FenceState a, b;
            {
                auto f = publication.BeginFrame();
                auto lease = registry.Acquire(f, h).value();
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
                { auto p = publication.BeginPublication(); h = registry.Create(p, c, 10).value(); }
                bool failed = false;
                {
                    auto p = publication.BeginPublication();
                    failAllocationAfter = budget;
                    try
                    {
                        if (operation) (void)registry.Replace(p, h, c, 20);
                        else created = registry.Create(p, c, 30).value();
                    }
                    catch (const std::bad_alloc&) { failed = true; }
                    failAllocationAfter = -1;
                }
                allocationFailures += failed; successes += !failed;
                {
                    auto f = publication.BeginFrame();
                    const auto original = registry.Acquire(f, h).value();
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
            const auto h = registry.Create(p, c, 1).value();
            Reject<std::runtime_error>([&] { (void)registry.Create(p, c, -1).value(); });
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
        { auto p = publication.BeginPublication(); h = registry->Create(p, c, 1).value(); }
        if (mode == "--reject-live-lease")
        {
            Registry::Lease lease;
            { auto f = publication.BeginFrame(); lease = registry->Acquire(f, h).value(); }
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
            { auto f = publication.BeginFrame(); registry->ProtectGpuUse(f, registry->Acquire(f, h).value(), std::make_unique<Fence>(state)); }
            forbiddenDestruction = true; registry.reset();
        }
        else if (mode == "--reject-overlap") { auto p = publication.BeginPublication(); (void)publication.BeginFrame(); }
        else if (mode == "--reject-foreign-scope") { AssetPublication other; auto p = other.BeginPublication(); (void)registry->Create(p, c, 1); }
        else if (mode == "--reject-worker-access") { std::thread worker([&] { std::set_terminate([] { std::cerr << "[EXPECTED] unsafe registry teardown rejected\n"; std::_Exit(86); }); (void)registry->Size(); }); worker.join(); }
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
    struct TextureHooks
    {
        struct Event { GLuint name; int deletes = 0; };
        inline static TextureHooks* active = nullptr;
        std::vector<Event> events;
        PFNGLGENTEXTURESPROC gen = glad_glGenTextures;
        PFNGLDELETETEXTURESPROC del = glad_glDeleteTextures;
        PFNGLTEXIMAGE2DPROC image = glad_glTexImage2D;
        bool failName = false, failStorage = false, correctThread = true;
        TextureHooks()
        {
            active = this;
            glad_glGenTextures = [](GLsizei count, GLuint* names) {
                if (active->failName) { active->failName = false; std::fill_n(names, count, 0); return; }
                active->gen(count, names);
                for (int i = 0; i < count; ++i) if (names[i]) active->events.push_back({names[i]});
            };
            glad_glDeleteTextures = [](GLsizei count, const GLuint* names) {
                active->correctThread &= std::this_thread::get_id() == expectedThread && SDL_GL_GetCurrentContext() == expectedContext;
                for (int i = 0; i < count; ++i)
                {
                    Check(names[i] != 0, "Empty texture owner issued a deletion");
                    for (auto it = active->events.rbegin(); it != active->events.rend(); ++it)
                        if (it->name == names[i]) { ++it->deletes; break; }
                }
                active->del(count, names);
            };
            glad_glTexImage2D = [](GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                GLint border, GLenum format, GLenum type, const void* pixels) {
                if (active->failStorage) { active->failStorage = false; return; }
                active->image(target, level, internal, w, h, border, format, type, pixels);
            };
        }
        ~TextureHooks() { glad_glGenTextures = gen; glad_glDeleteTextures = del; glad_glTexImage2D = image; active = nullptr; }
    };
    bool rejectTextureWorker = false, rejectManagerWorker = false;
    void TextureCases(::GEngine::EngineContext& root, TextureHooks& hooks)
    {
        using namespace GEngine;
        using Manager::AssetsManager;
        static_assert(!std::is_copy_constructible_v<TextureResource> && std::is_nothrow_move_constructible_v<TextureResource>
            && std::is_nothrow_move_assignable_v<TextureResource>);
        static_assert(std::is_copy_constructible_v<TextureView> && std::is_copy_constructible_v<AttachmentView>);
        TextureDesc desc;
        desc.width = 3; desc.height = 2; desc.format = TextureFormat::RGB8;
        desc.colorSpace = TextureColorSpace::Linear; desc.mips = TextureMipIntent::None;
        if (rejectTextureWorker || rejectManagerWorker)
        {
            std::set_terminate([] { std::cerr << "[EXPECTED] texture worker rejected\n"; std::_Exit(86); });
            std::thread worker([&] { std::set_terminate([] { std::cerr << "[EXPECTED] texture worker rejected\n"; std::_Exit(86); }); if (rejectManagerWorker) (void)AssetsManager::GetTexture("white"); else (void)TextureResource::Create(desc); }); worker.join(); std::_Exit(89);
        }
        const std::array<unsigned char, 22> rows{255,0,0, 0,255,0, 0,0,255, 77,88,
                                               4,5,6, 7,8,9, 10,11,12, 66,55};
        auto& publication = root.AssetPublications();
        TextureRegistry registry(publication);
        const auto before = hooks.events.size();
        GLuint unpackBuffer = 0; glGenBuffers(1, &unpackBuffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, 64, nullptr, GL_STATIC_DRAW);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 8); glPixelStorei(GL_UNPACK_ROW_LENGTH, 7);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
        TextureHandle handle;
        {
            auto image = TextureResource::Create(desc, {std::as_bytes(std::span(rows)), 11});
            Check(image, "Padded RGB texture creation failed");
            GLint state = 0; glGetIntegerv(GL_UNPACK_ALIGNMENT, &state); Check(state == 8, "Unpack alignment leaked");
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &state); Check(state == 7, "Unpack row length leaked");
            glGetIntegerv(GL_UNPACK_SKIP_ROWS, &state); Check(state == 1, "Unpack skip leaked");
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &state); Check(state == static_cast<GLint>(unpackBuffer), "Unpack PBO leaked");
            auto occupied = TextureResource::Create(desc).value();
            occupied = std::move(*image);
            occupied = std::move(occupied);
            Check(!bool(*image), "Move retained source ownership");
            auto p = publication.BeginPublication(); handle = registry.Create(p, std::move(occupied)).value();
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); glDeleteBuffers(1, &unpackBuffer);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        GLuint name = 0;
        TextureView retained;
        {
            auto f = publication.BeginFrame(); retained = TextureView(registry.Acquire(f, handle).value());
            name = AssetDetail::TextureBackend::Name(retained).value();
            Check(retained.Bind(3), "View bind failed");
            std::array<unsigned char, 18> read{};
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, read.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            Check(std::equal(read.begin(), read.begin()+9, rows.begin()) && std::equal(read.begin()+9, read.end(), rows.begin()+11),
                "Padded/odd-width RGB upload changed row orientation or pixels");
            auto copy = retained;
            Check(copy.Identity() == handle && copy.Revision() == 1, "View lost typed version identity");
            Check(!copy.Bind((std::numeric_limits<std::uint32_t>::max)()), "Invalid texture unit succeeded");
        }
        { auto p = publication.BeginPublication(); Check(registry.Destroy(p, handle), "Texture destroy failed"); Check(registry.Collect(p) == 0, "Live view retired"); }
        { auto f = publication.BeginFrame(); Check(!registry.Acquire(f, handle), "Destroyed texture still resolves"); }
        std::thread worker([view = std::move(retained)]() mutable { view = {}; }); worker.join();
        Check(glIsTexture(name), "Worker releasing a view deleted the GL image");
        { auto p = publication.BeginPublication(); Check(registry.Collect(p) == 1 && registry.Close(p), "Owner retirement failed"); }
        Check(!glIsTexture(name) && hooks.events[before+1].deletes == 1, "Move destination or final owner leaked");
        auto invalid = desc; invalid.width = 0;
        Check(TextureResource::Create(invalid).error().code == TextureErrorCode::InvalidDescription, "Invalid dimensions lost typed cause");
        Check(TextureResource::Create(desc, {std::as_bytes(std::span(rows)).first(3), 11}).error().code == TextureErrorCode::InvalidPixels, "Short upload span accepted");
        Check(TextureResource::Create(desc, {std::as_bytes(std::span(rows)), (std::numeric_limits<std::size_t>::max)()}).error().code == TextureErrorCode::InvalidPixels, "Stride overflow accepted");
        hooks.failName = true;
        Check(TextureResource::Create(desc).error().code == TextureErrorCode::Allocation, "Name failure lost cause");
        hooks.failStorage = true;
        Check(TextureResource::Create(desc).error().code == TextureErrorCode::Storage && hooks.events.back().deletes == 1,
            "Failed storage did not reclaim its partial name");
        Check(!TextureView{}.Bind(0), "Empty view succeeded");
        {
            TextureRegistry limited(publication, {1,1,1});
            auto p = publication.BeginPublication();
            limited.Create(p, TextureResource::Create(desc).value()).value();
            const auto error = limited.Create(p, TextureResource::Create(desc).value());
            Check(!error && error.error() == RegistryError::SlotsExhausted && hooks.events.back().deletes == 1,
                "Failed publication lost typed cause or leaked the candidate image");
            Check(limited.Close(p), "Limited texture registry did not close");
        }

        const auto file = std::filesystem::absolute("phase26-rows.png");
        Check(stbi_write_png(file.string().c_str(), 3, 2, 3, rows.data(), 11) != 0, "PNG fixture write failed");
        std::filesystem::create_directory("phase26-alias");
        auto options = desc; options.width = options.height = 0;
        const auto first = AssetsManager::LoadTexture(file.string(), options).value();
        const auto equivalent = file.parent_path() / "phase26-alias" / ".." / file.filename();
        Check(AssetsManager::LoadTexture(equivalent.string(), options).value() == first
            && AssetsManager::LoadTexture(file.string(), options).value() == first, "Canonical/repeated lookup did not reuse identity");
        const auto alias = file.parent_path()/"phase26-hardlink.png";
        std::filesystem::remove(alias); std::filesystem::create_hard_link(file, alias);
        Check(AssetsManager::LoadTexture(alias.string(), options).value() == first, "Filesystem-equivalent alias did not reuse identity");
        auto alternate = options; alternate.colorSpace = TextureColorSpace::SRGB;
        Check(AssetsManager::LoadTexture(file.string(), alternate).value() != first, "Color-space options collapsed");
        alternate = options; alternate.format = TextureFormat::RGBA8;
        Check(AssetsManager::LoadTexture(file.string(), alternate).value() != first, "Decode/format options collapsed");
        alternate = options; alternate.mips = TextureMipIntent::Generate;
        Check(AssetsManager::LoadTexture(file.string(), alternate).value() != first, "Mip intent collapsed");
        alternate = options; alternate.orientation = ImageOrientation::TopLeft;
        const auto top = AssetsManager::LoadTexture(file.string(), alternate).value();
        Check(top != first, "Orientation options collapsed");
        {
            auto topView = AssetsManager::ResolveTexture(top).value();
            auto bottomView = AssetsManager::ResolveTexture(first).value();
            std::array<unsigned char, 18> topPixels{}, bottomPixels{};
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            topView.Bind(0).value(); glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, topPixels.data());
            bottomView.Bind(0).value(); glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, bottomPixels.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            Check(std::equal(topPixels.begin(), topPixels.begin()+9, rows.begin())
                && std::equal(bottomPixels.begin(), bottomPixels.begin()+9, rows.begin()+11), "Decoder orientation does not match intent");
        }
        auto* a = AssetsManager::GetTexture(file.string(), "first", ".png", options).value();
        auto* b = AssetsManager::GetTexture(file.string(), "second", ".png", options).value();
        Check(a != b && a->View().Identity() == b->View().Identity() && a->GetUniformName() == "first"
            && b->GetUniformName() == "second", "Image identity and binding label are coupled");
        const auto bad = std::filesystem::absolute("phase26-bad.png");
        { std::ofstream stream(bad, std::ios::binary); stream << "not an image"; }
        const auto count = hooks.events.size();
        Check(AssetsManager::LoadTexture(bad.string()).error().code == TextureErrorCode::Decode
            && AssetsManager::LoadTexture(bad.string()).error().code == TextureErrorCode::Decode
            && hooks.events.size() == count, "Failed decode allocated/published a resource");
        const auto fallback = AssetsManager::FallbackTexture().value();
        Check(AssetsManager::GetTextureOrFallback(bad.string()).value()->View().Identity() == fallback
            && AssetsManager::FallbackTexture().value() == fallback, "Fallback handle is unstable");
        Check(stbi_write_png(bad.string().c_str(), 3, 2, 3, rows.data(), 11) != 0, "Repair fixture failed");
        Check(AssetsManager::LoadTexture(bad.string()).value() != fallback, "Failed source was cached as fallback");
        const auto hdr = std::filesystem::absolute("phase26.hdr");
        const std::array<float, 3> hdrPixels{0.25f, 0.5f, 2.f};
        Check(stbi_write_hdr(hdr.string().c_str(), 1, 1, 3, hdrPixels.data()) != 0, "HDR fixture failed");
        auto hdrOptions = options; hdrOptions.format = TextureFormat::RGB16Float;
        Check(AssetsManager::LoadTexture(hdr.string(), hdrOptions), "HDR decode failed");
        const auto cube = std::filesystem::absolute("phase26-cube"); std::filesystem::create_directory(cube);
        std::filesystem::remove(cube/"negz.png"); // Fixture-owned final face; recreate below in each cycle.
        const std::array<unsigned char, 12> square{255,0,0, 0,255,0, 0,0,255, 255,255,255};
        for (const char* face : {"posx", "negx", "posy", "negy", "posz"})
            Check(stbi_write_png((cube/(std::string(face)+".png")).string().c_str(), 2, 2, 3, square.data(), 6) != 0, "Cube fixture failed");
        auto cubeOptions = options; cubeOptions.kind = TextureKind::Cube; cubeOptions.orientation = ImageOrientation::TopLeft;
        const auto cubeBefore = hooks.events.size();
        Check(!AssetsManager::LoadTexture(cube.string(), cubeOptions) && hooks.events.size() == cubeBefore, "Partial cube allocated/published");
        Check(stbi_write_png((cube/"negz.png").string().c_str(), 2, 2, 3, square.data(), 6) != 0, "Final cube face failed");
        Check(AssetsManager::LoadTexture(cube.string(), cubeOptions), "Complete cube did not recover");
        {
            auto cascade = ::GEngine::CascadeShadowFrameBuffer::Create(16,16,3).value(); auto point = ::GEngine::PointShadowFrameBuffer::Create(16,16).value();
            auto* cascadeBinding = AssetsManager::GetCascadedFrameBufferTexture(cascade).value();
            auto* pointBinding = AssetsManager::GetPointShadowFrameBufferTexture(point).value();
            { auto borrowed = cascadeBinding->View(); Check(borrowed.IsAttachment(), "Attachment was classified as an image owner"); }
            Check(AssetDetail::TextureBackend::Name(cascadeBinding->View()).value() == ::GEngine::FramebufferDetail::Backend::Depth(cascade.Buffer())
                && AssetDetail::TextureBackend::Name(pointBinding->View()).value() == ::GEngine::FramebufferDetail::Backend::Depth(point.Buffer()), "Attachment source differs");
            Check(glIsTexture(::GEngine::FramebufferDetail::Backend::Depth(cascade.Buffer())) && glIsTexture(::GEngine::FramebufferDetail::Backend::Depth(point.Buffer())), "View destruction deleted attachments");
            Check(cascade.OnResize(24,24).has_value(), "Attachment resize failed"); Check(point.OnResize(32,32).has_value(), "Point attachment resize failed");
            Check(AssetDetail::TextureBackend::Name(cascadeBinding->View()).value() == ::GEngine::FramebufferDetail::Backend::Depth(cascade.Buffer())
                && AssetDetail::TextureBackend::Name(pointBinding->View()).value() == ::GEngine::FramebufferDetail::Backend::Depth(point.Buffer()), "Attachment resize left a stale observer");
            pointBinding->View().Bind(0).value(); GLint resized = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &resized);
            Check(resized == 32, "Resized attachment view bound stale storage");
        }
        Check(!AssetsManager::GetTextTexture("", {}, 24) && !AssetsManager::GetTextTexture("text", {}, 13), "Invalid text was published");
        auto* text = AssetsManager::GetTextTexture("Phase 26", {}, 24).value();
        Check(text == AssetsManager::GetTextTexture("Phase 26", {}, 24).value()
            && text == AssetsManager::GetTextTexture("Phase 26", RuntimeAssets::File("Fonts/Carlito-Regular.ttf"), 24).value(),
            "Repeated/canonical text lookup missed");
        Check(glGetError() == GL_NO_ERROR, "Texture checks emitted GL errors");
        std::cout << "[PASS] texture-canonical/options/fallback/decode/create/moves/views/attachments/stride/orientation/HDR/cube/text\n";
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
            Check(root->Initialize({properties}).has_value(), "Platform initialization failed");
            auto& publication = root->AssetPublications();
            std::cout << "[GL] " << glGetString(GL_VERSION) << " renderer=" << glGetString(GL_RENDERER) << '\n';
            int startupErrors = 0; while (glGetError() != GL_NO_ERROR) ++startupErrors;
            std::cout << "[INFO] existing startup errors isolated=" << startupErrors << '\n';
            glEnable(GL_DEBUG_OUTPUT); glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(Diagnostics, nullptr);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 25001, GL_DEBUG_SEVERITY_LOW, -1, "Registry diagnostics");
            expectedContext = SDL_GL_GetCurrentContext(); expectedThread = std::this_thread::get_id();
            TextureHooks textureHooks;
            if (!rejectRoot) TextureCases(*root, textureHooks);
            originalDelete = glad_glDeleteProgram; glad_glDeleteProgram = ObserveDelete;
            {
                AssetRegistry<ShaderProgramHandle, Shader> registry(publication);
                ShaderProgramHandle handle;
                { auto p = publication.BeginPublication(); handle = registry.Create(p, Program()).value(); }
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
                    old = registry.Acquire(f, handle).value();
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
                    auto current = registry.Acquire(f, handle).value(); replacement = current->GetHandle();
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
            Check(textureHooks.correctThread, "Texture retirement left its owning context thread");
            for (const auto& event : textureHooks.events) Check(event.deletes == 1, "Texture leaked or was deleted more than once");
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
        if (mode == "--reject-manager-worker") { rejectManagerWorker = true; GlIntegration(false); return 89; }
        if (mode == "--reject-texture-worker") { rejectTextureWorker = true; GlIntegration(false); return 89; }
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
