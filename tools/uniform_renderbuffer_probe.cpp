#include "Core/RenderTarget.h"
#include <type_traits>
static_assert(std::is_same_v<decltype(GEngine::RenderBufferObject::Create(1, 1)),
    std::expected<GEngine::RenderBufferObject, GEngine::FramebufferError>>);
static_assert(!std::is_constructible_v<GEngine::RenderBufferObject, unsigned, unsigned>);
template<class T> concept HasNativeRenderbufferName = requires(const T& t) { t.GetID(); };
static_assert(!HasNativeRenderbufferName<GEngine::RenderBufferObject>);
using SchemaUniform = GEngine::UniformBufferObject<GEngine::UniformType::MATRIX_4_4>;
static_assert(!std::is_constructible_v<SchemaUniform, unsigned>);
static_assert(std::is_same_v<decltype(SchemaUniform::Create(1)), std::expected<SchemaUniform, GEngine::UniformBufferError>>);
template<class T> concept HasNativeUniformName = requires(const T& t) { t.GetUBO(); };
static_assert(!HasNativeUniformName<SchemaUniform>);
#ifndef RENDERBUFFER_SCHEMA_ONLY
#include "../GEngine/src/Core/FramebufferBackend.h"
// Real production-library ownership checks; invoke through test_uniform_renderbuffer.py.
#include "gepch.h"
#include "Core/GLDebug.h"
#include "Core/RenderTarget.h"
#include <array>
#include <stdexcept>
#include <string_view>

#ifdef UBO_STARTUP_PROBE
#include "Core/BaseApp.h"
#include <sdl2/SDL_ttf.h>
#define main ProductionEntryPoint
#include "GEngine/EntryPoint.h"
#undef main
namespace Startup
{
    bool fail = false, valid = true;
    int injections = 0, runs = 0, destructors = 0;
    std::unordered_set<GLuint> buffers;
    PFNGLBUFFERDATAPROC data;
    PFNGLGENBUFFERSPROC generate;
    PFNGLDELETEBUFFERSPROC destroy;
    SDL_GLContext context;
    SDL_threadID thread;
    void Check(bool ok, const char* why) { if (!ok) { valid = false; std::cerr << "[FAIL] " << why << '\n'; } }
    void APIENTRY Data(GLenum target, GLsizeiptr size, const void* input, GLenum usage)
    {
        if (fail && target == GL_UNIFORM_BUFFER && injections == 0) { ++injections; return; }
        data(target, size, input, usage);
    }
    void APIENTRY Generate(GLsizei n, GLuint* ids)
    { generate(n, ids); for (int i=0;i<n;++i) Check(buffers.insert(ids[i]).second, "duplicate buffer owner"); }
    void APIENTRY Destroy(GLsizei n, const GLuint* ids)
    {
        Check(SDL_GL_GetCurrentContext() == context && SDL_ThreadID() == thread, "buffer teardown context/thread");
        for (int i=0;i<n;++i) if (ids[i]) Check(buffers.erase(ids[i]) == 1, "missing/double buffer retirement");
        destroy(n, ids);
    }
    class App final : public ::GEngine::BaseApp
    {
    public:
        ::GEngine::ApplicationInitializationResult Initialize(const std::initializer_list<::GEngine::WindowProperties>& p) override
        {
            auto result = BaseApp::Initialize(p);
            Check(result.has_value() != fail, "actual BaseApp result");
            if (!result)
            {
                const auto* e = std::get_if<::GEngine::UniformBufferError>(&result.error());
                Check(e && e->code == ::GEngine::UniformBufferErrorCode::Storage && e->elementCount == 16
                    && e->bindingPoint == 0 && e->elementBytes == sizeof(::GEngine::Math::Mat4)
                    && e->operation == "UniformBufferObject::Create"
                    && e->message == "Uniform buffer storage allocation failed", "startup diagnostic propagation");
                Check(!m_Initialize && !m_UniformBufferObject, "failed UBO published initialization");
            }
            return result;
        }
        ::GEngine::ApplicationRunResult Run() override { ++runs; return {}; }
        ~App() override { ++destructors; Check(SDL_GL_GetCurrentContext() != nullptr, "application lost context before teardown"); }
    };
}
extern "C" int Phase66RealGladLoadGL(void);
extern "C" int gladLoadGL(void)
{
    const int result = Phase66RealGladLoadGL();
    if (result)
    {
        Startup::context = SDL_GL_GetCurrentContext(); Startup::thread = SDL_ThreadID();
        Startup::data = glad_glBufferData; glad_glBufferData = Startup::Data;
        Startup::generate = glad_glGenBuffers; glad_glGenBuffers = Startup::Generate;
        Startup::destroy = glad_glDeleteBuffers; glad_glDeleteBuffers = Startup::Destroy;
    }
    return result;
}
::GEngine::WindowProperties winProp = [] {
    ::GEngine::WindowProperties p; p.m_Title="UBO startup validation"; p.m_Width=p.m_Height=64;
    p.m_MinWidth=p.m_MinHeight=32; p.m_IsVsync=false;
    p.flag=::GEngine::BitFlags<::GEngine::WindowFlags,uint8_t>{::GEngine::WindowFlags::INVISIBLE}; return p;
}();
::GEngine::BaseApp* CreateApp() { return new Startup::App; }
int main(int argc, char* argv[])
{
    if (argc != 2) return 2;
    Startup::fail = std::string_view(argv[1]) == "failure";
    SDL_SetMainReady();
    const auto status = ProductionEntryPoint(argc, argv);
    Startup::Check(status == (Startup::fail ? 1 : 0), "actual EntryPoint status");
    Startup::Check(Startup::runs == (Startup::fail ? 0 : 1) && Startup::destructors == 1, "post-failure Run or missing teardown");
    Startup::Check(Startup::injections == (Startup::fail ? 1 : 0), "fault was not exercised");
    std::cout << "[OBSERVE] buffers=" << Startup::buffers.size() << " engine=" << (::GEngine::EngineContext::TryGet()!=nullptr)
        << " SDL=" << SDL_WasInit(0) << " TTF=" << TTF_WasInit() << '\n';
    for (auto id : Startup::buffers) std::cout << "[OBSERVE] unretired buffer=" << id << '\n';
    Startup::Check(Startup::buffers.empty() && !::GEngine::EngineContext::TryGet()
        && SDL_WasInit(0) == 0 && TTF_WasInit() == 0, "resource/root/platform survived teardown");
    if (!Startup::valid) return 97;
    std::cout << "[PASS] UBO startup " << argv[1] << " typed-status/no-post-failure-run/RAII\n";
    return status;
}
#else

namespace
{
    using namespace GEngine;
    int checks = 0;
    void Check(bool value, const char* reason)
    { ++checks; if (!value) throw std::runtime_error(reason); }
    template<class T> constexpr bool UniqueMovable = !std::is_copy_constructible_v<T>
        && !std::is_copy_assignable_v<T> && std::is_nothrow_move_constructible_v<T>
        && std::is_nothrow_move_assignable_v<T>;
    static_assert(UniqueMovable<RenderBufferObject>);
    static_assert(!std::is_copy_constructible_v<RenderTarget> && std::is_nothrow_move_constructible_v<RenderTarget>);
    GLuint Bound(GLenum what) { GLint name = 0; glGetIntegerv(what, &name); return name; }
    GLuint Indexed(unsigned point) { GLint name = 0; glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, point, &name); return name; }
    template<class T> T TakeUniform(std::expected<T, UniformBufferError> result)
    { Check(result.has_value(), "Expected UBO success"); return std::move(*result); }


    // Inject deterministic allocation failures without requesting GPU exhaustion.
    // Driver pointers otherwise forward every real call and record exact lifetime.
    struct Observer
    {
        enum class Fault { None, BufferName, BufferStorage, BufferBind, BufferPublish, BufferUnbind, RenderbufferName, RenderbufferStorage, RenderbufferBind };
        inline static Fault fault = Fault::None;
        inline static GLenum pending = GL_NO_ERROR;
        inline static PFNGLGENBUFFERSPROC genBuffer;
        inline static PFNGLBINDBUFFERPROC bindBuffer;
        inline static PFNGLBINDBUFFERBASEPROC bindBufferBase;
        inline static PFNGLDELETEBUFFERSPROC deleteBuffer;
        inline static PFNGLBUFFERDATAPROC bufferData;
        inline static PFNGLGENRENDERBUFFERSPROC genRenderbuffer;
        inline static PFNGLBINDRENDERBUFFERPROC bindRenderbuffer;
        inline static PFNGLDELETERENDERBUFFERSPROC deleteRenderbuffer;
        inline static PFNGLRENDERBUFFERSTORAGEPROC storage;
        inline static PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC multisample;
        inline static PFNGLGETERRORPROC getError;
        inline static std::unordered_set<GLuint> buffers, renderbuffers;
        inline static unsigned created = 0, deleted = 0, deleteCalls = 0, attempts = 0;
        inline static GLsizeiptr requestedBytes = 0;
        inline static bool valid = true;
        inline static SDL_GLContext context;
        inline static std::thread::id thread;
        static bool Fail(Fault kind)
        { if (fault != kind) return false; fault = Fault::None; pending = GL_OUT_OF_MEMORY; return true; }
        static void Add(std::unordered_set<GLuint>& live, GLsizei count, const GLuint* names)
        {
            valid &= SDL_GL_GetCurrentContext() == context && std::this_thread::get_id() == thread;
            for (GLsizei i = 0; i < count; ++i) { valid &= names[i] && live.insert(names[i]).second; ++created; }
        }
        static void Remove(std::unordered_set<GLuint>& live, GLsizei count, const GLuint* names)
        {
            valid &= SDL_GL_GetCurrentContext() == context && std::this_thread::get_id() == thread;
            ++deleteCalls;
            for (GLsizei i = 0; i < count; ++i) { valid &= names[i] && live.erase(names[i]) == 1; ++deleted; }
        }
        static void APIENTRY GenBuffer(GLsizei n, GLuint* ids)
        { ++attempts; if (Fail(Fault::BufferName)) { std::fill_n(ids, n, 0); return; } genBuffer(n, ids); Add(buffers, n, ids); }
        static void APIENTRY BindBuffer(GLenum target, GLuint id)
        {
            if (target == GL_UNIFORM_BUFFER && (id ? Fail(Fault::BufferBind) : Fail(Fault::BufferUnbind))) return;
            bindBuffer(target, id);
        }
        static void APIENTRY BindBufferBase(GLenum target, GLuint point, GLuint id)
        { if (!Fail(Fault::BufferPublish)) bindBufferBase(target, point, id); }
        static void APIENTRY DeleteBuffer(GLsizei n, const GLuint* ids)
        { Remove(buffers, n, ids); deleteBuffer(n, ids); }
        static void APIENTRY BufferData(GLenum target, GLsizeiptr bytes, const void* data, GLenum usage)
        { requestedBytes = bytes; if (!Fail(Fault::BufferStorage)) bufferData(target, bytes, data, usage); }
        static void APIENTRY GenRenderbuffer(GLsizei n, GLuint* ids)
        { ++attempts; if (Fail(Fault::RenderbufferName)) { std::fill_n(ids, n, 0); return; } genRenderbuffer(n, ids); Add(renderbuffers, n, ids); }
        static void APIENTRY BindRenderbuffer(GLenum target, GLuint id)
        { if (!Fail(Fault::RenderbufferBind)) bindRenderbuffer(target, id); }
        static void APIENTRY DeleteRenderbuffer(GLsizei n, const GLuint* ids)
        { Remove(renderbuffers, n, ids); deleteRenderbuffer(n, ids); }
        static void APIENTRY Storage(GLenum target, GLenum format, GLsizei w, GLsizei h)
        { if (!Fail(Fault::RenderbufferStorage)) storage(target, format, w, h); }
        static void APIENTRY Multisample(GLenum target, GLsizei samples, GLenum format, GLsizei w, GLsizei h)
        { if (!Fail(Fault::RenderbufferStorage)) multisample(target, samples, format, w, h); }
        static GLenum APIENTRY Error()
        { if (pending != GL_NO_ERROR) return std::exchange(pending, GL_NO_ERROR); return getError(); }
        Observer()
        {
            buffers.clear(); renderbuffers.clear(); created = deleted = deleteCalls = attempts = 0; valid = true;
            context = SDL_GL_GetCurrentContext(); thread = std::this_thread::get_id();
            genBuffer = glad_glGenBuffers; glad_glGenBuffers = GenBuffer;
            bindBuffer = glad_glBindBuffer; glad_glBindBuffer = BindBuffer;
            bindBufferBase = glad_glBindBufferBase; glad_glBindBufferBase = BindBufferBase;
            deleteBuffer = glad_glDeleteBuffers; glad_glDeleteBuffers = DeleteBuffer;
            bufferData = glad_glBufferData; glad_glBufferData = BufferData;
            genRenderbuffer = glad_glGenRenderbuffers; glad_glGenRenderbuffers = GenRenderbuffer;
            bindRenderbuffer = glad_glBindRenderbuffer; glad_glBindRenderbuffer = BindRenderbuffer;
            deleteRenderbuffer = glad_glDeleteRenderbuffers; glad_glDeleteRenderbuffers = DeleteRenderbuffer;
            storage = glad_glRenderbufferStorage; glad_glRenderbufferStorage = Storage;
            multisample = glad_glRenderbufferStorageMultisample; glad_glRenderbufferStorageMultisample = Multisample;
            getError = glad_glGetError; glad_glGetError = Error;
        }
        ~Observer()
        {
            glad_glGenBuffers = genBuffer; glad_glDeleteBuffers = deleteBuffer; glad_glBufferData = bufferData;
            glad_glBindBuffer = bindBuffer; glad_glBindBufferBase = bindBufferBase;
            glad_glGenRenderbuffers = genRenderbuffer; glad_glDeleteRenderbuffers = deleteRenderbuffer;
            glad_glBindRenderbuffer = bindRenderbuffer;
            glad_glRenderbufferStorage = storage; glad_glRenderbufferStorageMultisample = multisample;
            glad_glGetError = getError;
        }
        static void ErrorObserved()
        { Check(fault == Fault::None && glGetError() == GL_OUT_OF_MEMORY && glGetError() == GL_NO_ERROR, "Allocation error was lost or extra GL errors occurred"); }
        void Verify()
        {
            Check(valid && buffers.empty() && renderbuffers.empty() && created == deleted,
                "UBO/RBO leaked, deleted twice/zero or on a foreign context/thread");
            const auto counters = RenderCounters::Current();
            Check(counters.liveNames[0] == 0 && counters.liveNames[5] == 0 && counters.estimatedBufferBytes == 0,
                "Production UBO/RBO counters did not return to zero");
            std::cout << "[PASS] exactly-once UBO/RBO created=" << created << " deleted=" << deleted
                << " counters=" << GENGINE_RENDER_COUNTERS << '\n';
        }
    };

    struct Diagnostics
    {
        unsigned errors = 0, markers = 0, allocationNotices = 0;
        static void APIENTRY Receive(GLenum source, GLenum type, GLuint id, GLenum severity,
            GLsizei, const GLchar* message, const void* user)
        {
            auto& self = *static_cast<Diagnostics*>(const_cast<void*>(user));
            if (source == GL_DEBUG_SOURCE_APPLICATION && type == GL_DEBUG_TYPE_MARKER && id == 23001) ++self.markers;
            // Observed NVIDIA allocation notice, recorded separately from errors.
            else if (source == GL_DEBUG_SOURCE_API && type == GL_DEBUG_TYPE_OTHER
                && severity == GL_DEBUG_SEVERITY_LOW && id == 131169
                && std::string_view(message).starts_with("Framebuffer detailed info: The driver allocated ")
                && std::string_view(message).find("storage for renderbuffer ") != std::string_view::npos)
                ++self.allocationNotices;
            else if (type == GL_DEBUG_TYPE_ERROR || severity != GL_DEBUG_SEVERITY_NOTIFICATION)
            {
                ++self.errors;
                std::fprintf(stderr, "[GL] source=0x%x type=0x%x severity=0x%x id=%u %s\n", source, type, severity, id, message);
            }
        }
        Diagnostics()
        {
            glEnable(GL_DEBUG_OUTPUT); glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(Receive, this);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 23001,
                GL_DEBUG_SEVERITY_LOW, -1, "Phase 23 diagnostic control");
        }
        ~Diagnostics() { glDebugMessageCallback(nullptr, nullptr); }
    };

    template<UniformType Type> void Uniforms()
    {
        using Buffer = UniformBufferObject<Type>;
        using Code = UniformBufferErrorCode;
        static_assert(UniqueMovable<Buffer>);
        auto source = std::make_unique<Buffer>(TakeUniform(Buffer::Create(3, 0)));
        const auto name = Indexed(0), size = source->GetUniformTypeSize();
        Check(Bound(GL_UNIFORM_BUFFER_BINDING) == 0, "Successful UBO creation did not unbind");
        const std::array<GLuint, 4> values{123, 456, 789, 1011};
        glBindBuffer(GL_UNIFORM_BUFFER, name); glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(values), values.data());
        Buffer moved(std::move(*source));
        Check(!*source && !source->GetUniformTypeSize(), "UBO moved-from state not zero");
        const auto deletes = Observer::deleteCalls; source.reset();
        Check(Observer::deleteCalls == deletes, "Empty UBO issued deletion");
        auto destination = TakeUniform(Buffer::Create(7, 1)); const auto old = Indexed(1);
        destination = std::move(moved);
        Check(!glIsBuffer(old) && !moved && destination && glIsBuffer(name), "UBO assignment ownership failed");
        auto* same = &destination; destination = std::move(*same);
        std::vector<Buffer> owners; owners.reserve(1); owners.push_back(std::move(destination));
        owners.push_back(TakeUniform(Buffer::Create(2, 2)));
        Check(owners.front() && owners.front().GetUniformTypeSize() == size, "UBO relocation lost metadata");
        glBindBuffer(GL_UNIFORM_BUFFER, name);
        const auto verify = [&] {
            std::array<GLuint, 4> actual{}; glGetBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(actual), actual.data());
            GLint64 bytes = 0; glGetBufferParameteri64v(GL_UNIFORM_BUFFER, GL_BUFFER_SIZE, &bytes);
            Check(actual == values && bytes == 3 * size && Indexed(0) == name, "UBO storage/data/binding changed");
        };
        verify();
        const auto reject = [&](const auto& result, Code code, unsigned count, unsigned point) {
            Check(!result && result.error().code == code && result.error().operation == "UniformBufferObject::Create"
                && !result.error().message.empty() && result.error().elementCount == count
                && result.error().bindingPoint == point && result.error().elementBytes == size,
                "UBO error lost typed diagnostic context");
        };
        for (auto failure : {Observer::Fault::BufferName, Observer::Fault::BufferStorage,
            Observer::Fault::BufferBind, Observer::Fault::BufferPublish, Observer::Fault::BufferUnbind})
        {
            Observer::fault = failure;
            const auto code = failure == Observer::Fault::BufferName ? Code::Allocation
                : failure == Observer::Fault::BufferStorage ? Code::Storage : Code::Binding;
            reject(Buffer::Create(5, 0), code, 5, 0);
            Check(owners.front() && Bound(GL_UNIFORM_BUFFER_BINDING) == name && Indexed(0) == name,
                "Failed UBO creation changed previous owner/bindings");
            verify(); Observer::ErrorObserved();
        }
        const auto attempts = Observer::attempts;
        reject(Buffer::Create(0), Code::InvalidDescription, 0, 0);
        const auto maxBindings = Bound(GL_MAX_UNIFORM_BUFFER_BINDINGS);
        reject(Buffer::Create(1, maxBindings), Code::InvalidBinding, 1, maxBindings);
        Check(Observer::attempts == attempts, "Invalid UBO request generated a name");
        Observer::fault = Observer::Fault::BufferStorage;
        reject(Buffer::Create((std::numeric_limits<unsigned int>::max)()), Code::Storage, (std::numeric_limits<unsigned int>::max)(), 0);
        Check(Observer::requestedBytes == static_cast<GLsizeiptr>((std::numeric_limits<unsigned int>::max)()) * size,
            "UBO byte count overflowed");
        Observer::ErrorObserved();
        owners.front() = TakeUniform(Buffer::Create(4, 0));
        Check(!glIsBuffer(name) && owners.front() && Indexed(0) != 0, "UBO recovery did not retire destination");
        owners.front() = std::move(moved);
        Check(!owners.front() && !owners.front().GetUniformTypeSize(), "Empty UBO assignment retained state");
#if GENGINE_RENDER_COUNTERS
        Check(RenderCounters::Current().liveNames[0] == Observer::buffers.size(), "UBO counters differ");
#endif
        std::cout << "[PASS] typed UBO type=" << static_cast<int>(Type) << " moves/data/errors/bindings/recovery\n";
    }

    void RangedUniformBindings()
    {
        using Buffer = UniformBufferObject<UniformType::VEC4F>;
        const auto alignment = Bound(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT);
        auto rangeOwner = TakeUniform(Buffer::Create((alignment + 64 + 15) / 16, 0));
        const auto rangedName = Indexed(0);
        auto genericOwner = TakeUniform(Buffer::Create(4, 1));
        const auto genericName = Indexed(1);
        glBindBufferRange(GL_UNIFORM_BUFFER, 0, rangedName, alignment, 64);
        glBindBuffer(GL_UNIFORM_BUFFER, genericName);
        for (auto fault : {Observer::Fault::BufferPublish, Observer::Fault::BufferUnbind})
        {
            Observer::fault = fault;
            auto failed = Buffer::Create(2, 0);
            Check(!failed && failed.error().code == UniformBufferErrorCode::Binding, "Expected publication rollback");
            GLint64 start = 0, size = 0;
            glGetInteger64i_v(GL_UNIFORM_BUFFER_START, 0, &start);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, 0, &size);
            Check(Indexed(0) == rangedName && start == alignment && size == 64
                && Bound(GL_UNIFORM_BUFFER_BINDING) == genericName, "Rollback lost indexed range or generic binding");
            Observer::ErrorObserved();
        }
        std::cout << "[PASS] UBO exact indexed-range/generic-binding rollback\n";
    }

    RenderBufferObject TakeRbo(std::expected<RenderBufferObject, FramebufferError> result)
    { Check(result.has_value(), "Expected RBO success"); return std::move(*result); }
    template<class T> void RejectRbo(const std::expected<T, FramebufferError>& result,
        FramebufferErrorCode code, unsigned w, unsigned h, unsigned samples)
    {
        Check(!result && result.error().code == code && result.error().message
            && result.error().width == w && result.error().height == h && result.error().samples == samples,
            "Missing typed RBO error or diagnostic context");
    }
    void Storage(const RenderBufferObject& buffer, GLuint name, unsigned width, unsigned height, unsigned samples)
    {
        GLint w = 0, h = 0, s = 0, format = 0;
        glGetNamedRenderbufferParameteriv(name, GL_RENDERBUFFER_WIDTH, &w);
        glGetNamedRenderbufferParameteriv(name, GL_RENDERBUFFER_HEIGHT, &h);
        glGetNamedRenderbufferParameteriv(name, GL_RENDERBUFFER_SAMPLES, &s);
        glGetNamedRenderbufferParameteriv(name, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
        Check(w == static_cast<GLint>(width) && h == static_cast<GLint>(height) && format == GL_DEPTH24_STENCIL8
            && (samples > 1 ? s >= static_cast<GLint>(samples) : s == 0)
            && buffer.GetWidth() == width && buffer.GetHeight() == height && buffer.GetSamples() == samples,
            "Renderbuffer storage or metadata differs");
    }
    void Renderbuffers()
    {
        using Code = FramebufferErrorCode;
        for (unsigned samples : {1u, 4u})
        {
            auto source = std::make_unique<RenderBufferObject>(TakeRbo(RenderBufferObject::Create(16, 12, samples)));
            const auto name = Bound(GL_RENDERBUFFER_BINDING);
            RenderBufferObject moved(std::move(*source));
            Check(!*source && !source->GetWidth() && !source->GetHeight() && !source->GetSamples(), "RBO moved-from state not zero");
            const auto deletes = Observer::deleteCalls; source.reset();
            Check(Observer::deleteCalls == deletes, "Empty RBO issued deletion");
            auto destination = TakeRbo(RenderBufferObject::Create(8, 8)); const auto old = Bound(GL_RENDERBUFFER_BINDING);
            destination = std::move(moved);
            Check(!glIsRenderbuffer(old) && destination && !moved && glIsRenderbuffer(name), "RBO assignment failed");
            auto* same = &destination; destination = std::move(*same);
            std::vector<RenderBufferObject> owners;
            owners.reserve(1); owners.push_back(std::move(destination)); owners.push_back(TakeRbo(RenderBufferObject::Create(3, 3)));
            const auto unrelated = Bound(GL_RENDERBUFFER_BINDING);
            Storage(owners.front(), name, 16, 12, samples);
            for (auto failure : {Observer::Fault::RenderbufferName, Observer::Fault::RenderbufferStorage, Observer::Fault::RenderbufferBind})
            {
                const auto code = failure == Observer::Fault::RenderbufferName ? Code::Allocation
                    : failure == Observer::Fault::RenderbufferStorage ? Code::Storage : Code::InvalidOperation;
                glBindRenderbuffer(GL_RENDERBUFFER, unrelated);
                Observer::fault = failure;
                RejectRbo(owners.front().Resize(20, 24, samples), code, 20, 24, samples);
                Check(owners.front() && Bound(GL_RENDERBUFFER_BINDING) == unrelated, "Failed RBO resize changed owner/binding");
                Storage(owners.front(), name, 16, 12, samples);
                Storage(owners.back(), unrelated, 3, 3, 1); // failed binding must not mutate this storage
                Observer::ErrorObserved();
                Observer::fault = failure;
                RejectRbo(RenderBufferObject::Create(2, 2, samples), code, 2, 2, samples);
                Check(Bound(GL_RENDERBUFFER_BINDING) == unrelated, "Failed creation lost previous binding");
                Observer::ErrorObserved();
            }
            const auto attempts = Observer::attempts;
            RejectRbo(RenderBufferObject::Create(0, 1), Code::InvalidDescription, 0, 1, 1);
            RejectRbo(RenderBufferObject::Create(1, 0), Code::InvalidDescription, 1, 0, 1);
            RejectRbo(RenderBufferObject::Create(1, 1, 0), Code::InvalidDescription, 1, 1, 0);
            const auto maxSize = Bound(GL_MAX_RENDERBUFFER_SIZE), maxSamples = Bound(GL_MAX_SAMPLES);
            RejectRbo(RenderBufferObject::Create(maxSize + 1, 1), Code::Unsupported, maxSize + 1, 1, 1);
            RejectRbo(RenderBufferObject::Create(1, 1, maxSamples + 1), Code::Unsupported, 1, 1, maxSamples + 1);
            Check(Observer::attempts == attempts, "Invalid RBO request generated a name");
            Check(owners.front().Resize(20, 24, samples).has_value(), "Recovery failed");
            Check(!glIsRenderbuffer(name), "Successful RBO resize retained old owner");
            Storage(owners.front(), Bound(GL_RENDERBUFFER_BINDING), 20, 24, samples);
#if GENGINE_RENDER_COUNTERS
            Check(RenderCounters::Current().liveNames[5] == Observer::renderbuffers.size(), "Live RBO counters differ");
#endif
            owners.front() = std::move(moved);
            Check(!owners.front() && !owners.front().GetWidth() && !owners.front().GetSamples(), "Empty RBO assignment retained state");
        }
        std::cout << "[PASS] RBO typed-errors/diagnostics/binding/partial-cleanup/moves/recovery\n";
    }

    void Targets()
    {
        RenderTargetSpecification spec; spec.Width = 17; spec.Height = 19; spec.Samples = 1;
        auto target = RenderTarget::Create(spec).value();
        Check(target.OnResize(20, 16).has_value(), "Target resize failed");
        const auto verify = [&]
        {
            target.Bind(); GLint attached = 0;
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &attached);
            Check(attached == FramebufferDetail::Backend::Renderbuffer(target.Buffer()) && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Target lost its depth RBO");
        };
        verify();
        const auto fbo = FramebufferDetail::Backend::Name(target.Buffer()), color = FramebufferDetail::Backend::Color(target.Buffer()), rbo = FramebufferDetail::Backend::Renderbuffer(target.Buffer());
        for (int operation = 0; operation < 3; ++operation)
        {
            Observer::fault = Observer::Fault::RenderbufferStorage;
            auto result = operation == 0 ? target.OnResize(28,24) : operation == 1 ? target.SetSamples(4) : target.RenderSize({28,24});
            Check(!result && result.error().code == FramebufferErrorCode::Storage, "Target storage failure was not typed");
            Check(FramebufferDetail::Backend::Name(target.Buffer()) == fbo && FramebufferDetail::Backend::Color(target.Buffer()) == color
                && FramebufferDetail::Backend::Renderbuffer(target.Buffer()) == rbo && target.GetWidth() == 20 && target.GetHeight() == 16 && target.GetSamples() == 1, "Failed replacement changed ownership/settings");
            Observer::ErrorObserved(); verify();
        }
        Check(target.OnResize(28,24).has_value(), "Target resize failed"); verify(); Check(!glIsRenderbuffer(rbo), "Old RBO leaked");
        Check(target.RenderSize({30,26}).has_value(), "RenderSize failed"); verify();
        Check(!target.OnResize(8193,24) && !target.RenderSize({(std::numeric_limits<float>::quiet_NaN)(),24}) && !target.SetSamples(0), "Invalid target input accepted");
        Observer::fault = Observer::Fault::RenderbufferStorage;
        const auto before = RenderCounters::Current().liveNames;
        Check(!RenderTarget::Create(12,12,1), "Failed target creation succeeded"); Observer::ErrorObserved();
        Check(RenderCounters::Current().liveNames == before, "Failed target creation leaked resources");
        std::cout << "[PASS] RenderTarget RBO attachment/replacement/resize/failure/constructor-unwind\n";
    }

    void APIENTRY ForbiddenDelete(GLsizei, const GLuint*) { std::_Exit(87); }
    void RejectThread(std::string_view mode)
    {
        using Buffer = UniformBufferObject<UniformType::VEC4F>;
        auto buffer = std::make_unique<Buffer>(TakeUniform(Buffer::Create(1)));
        auto renderbuffer = std::make_unique<RenderBufferObject>(TakeRbo(RenderBufferObject::Create(2, 2)));
        glad_glDeleteBuffers = ForbiddenDelete; glad_glDeleteRenderbuffers = ForbiddenDelete;
        std::thread worker([&]
        {
            std::set_terminate([] { std::_Exit(86); });
            if (mode == "--reject-ubo-delete") buffer.reset(); else renderbuffer.reset();
            std::_Exit(89);
        });
        worker.join();
    }
}

int main(int argc, char** argv)
{
    try
    {
        SDL_SetMainReady();
        struct Video { ~Video() { SDL_Quit(); } } video;
        Check(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        GLDebug::ConfigureContext();
        for (int cycle = 0; cycle < 2; ++cycle)
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Phase 23 UBO/RBO validation",
                0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), &SDL_DestroyWindow);
            Check(window != nullptr, SDL_GetError());
            std::unique_ptr<void, decltype(&SDL_GL_DeleteContext)> context(GLDebug::CreateContext(window.get()), &SDL_GL_DeleteContext);
            Check(context != nullptr && gladLoadGLLoader(SDL_GL_GetProcAddress), "Context/GL loading failed");
            if (argc == 2) { RejectThread(argv[1]); return 89; }
            std::cout << "[GL] cycle=" << cycle << " version=" << glGetString(GL_VERSION) << " renderer=" << glGetString(GL_RENDERER) << '\n';
            Diagnostics diagnostics;
            {
                Observer observer;
                Uniforms<UniformType::VEC2F>(); Uniforms<UniformType::VEC3F>(); Uniforms<UniformType::VEC4F>();
                Uniforms<UniformType::MATRIX_2_2>(); Uniforms<UniformType::MATRIX_3_3>(); Uniforms<UniformType::MATRIX_4_4>();
                RangedUniformBindings(); Renderbuffers(); Targets(); observer.Verify();
            }
            Check(diagnostics.markers == 1 && !diagnostics.errors && glGetError() == GL_NO_ERROR, "Unexpected GL diagnostics/errors");
            std::cout << "[INFO] successful-renderbuffer-allocation-notices=" << diagnostics.allocationNotices << '\n';
            RenderCounters::ForgetContext(context.get());
        }
        std::cout << "[PASS] uniform-renderbuffer-RAII cycles=2 checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "[FAIL] " << error.what() << '\n'; return 1; }
}

#endif // UBO_STARTUP_PROBE
#endif
