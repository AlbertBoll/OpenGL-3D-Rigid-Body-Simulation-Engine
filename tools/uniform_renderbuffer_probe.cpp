#include "../GEngine/src/Core/FramebufferBackend.h"
// Real production-library ownership checks; invoke through test_uniform_renderbuffer.py.
#include "gepch.h"
#include "Core/GLDebug.h"
#include "Core/RenderTarget.h"
#include <array>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace GEngine;
    int checks = 0;
    void Check(bool value, const char* reason)
    { ++checks; if (!value) throw std::runtime_error(reason); }
    template<class Exception, class F> void Reject(F&& operation)
    {
        try { operation(); }
        catch (const Exception&) { ++checks; return; }
        throw std::runtime_error("Expected allocation/argument failure did not occur");
    }
    template<class T> constexpr bool UniqueMovable = !std::is_copy_constructible_v<T>
        && !std::is_copy_assignable_v<T> && std::is_nothrow_move_constructible_v<T>
        && std::is_nothrow_move_assignable_v<T>;
    static_assert(UniqueMovable<RenderBufferObject>);
    static_assert(!std::is_copy_constructible_v<RenderTarget> && std::is_nothrow_move_constructible_v<RenderTarget>);
    GLuint Bound(GLenum what) { GLint name = 0; glGetIntegerv(what, &name); return name; }

    // Inject deterministic allocation failures without requesting GPU exhaustion.
    // Driver pointers otherwise forward every real call and record exact lifetime.
    struct Observer
    {
        enum class Fault { None, BufferName, BufferStorage, RenderbufferName, RenderbufferStorage };
        inline static Fault fault = Fault::None;
        inline static GLenum pending = GL_NO_ERROR;
        inline static PFNGLGENBUFFERSPROC genBuffer;
        inline static PFNGLDELETEBUFFERSPROC deleteBuffer;
        inline static PFNGLBUFFERDATAPROC bufferData;
        inline static PFNGLGENRENDERBUFFERSPROC genRenderbuffer;
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
        static void APIENTRY DeleteBuffer(GLsizei n, const GLuint* ids)
        { Remove(buffers, n, ids); deleteBuffer(n, ids); }
        static void APIENTRY BufferData(GLenum target, GLsizeiptr bytes, const void* data, GLenum usage)
        { requestedBytes = bytes; if (!Fail(Fault::BufferStorage)) bufferData(target, bytes, data, usage); }
        static void APIENTRY GenRenderbuffer(GLsizei n, GLuint* ids)
        { ++attempts; if (Fail(Fault::RenderbufferName)) { std::fill_n(ids, n, 0); return; } genRenderbuffer(n, ids); Add(renderbuffers, n, ids); }
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
            deleteBuffer = glad_glDeleteBuffers; glad_glDeleteBuffers = DeleteBuffer;
            bufferData = glad_glBufferData; glad_glBufferData = BufferData;
            genRenderbuffer = glad_glGenRenderbuffers; glad_glGenRenderbuffers = GenRenderbuffer;
            deleteRenderbuffer = glad_glDeleteRenderbuffers; glad_glDeleteRenderbuffers = DeleteRenderbuffer;
            storage = glad_glRenderbufferStorage; glad_glRenderbufferStorage = Storage;
            multisample = glad_glRenderbufferStorageMultisample; glad_glRenderbufferStorageMultisample = Multisample;
            getError = glad_glGetError; glad_glGetError = Error;
        }
        ~Observer()
        {
            glad_glGenBuffers = genBuffer; glad_glDeleteBuffers = deleteBuffer; glad_glBufferData = bufferData;
            glad_glGenRenderbuffers = genRenderbuffer; glad_glDeleteRenderbuffers = deleteRenderbuffer;
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
        static_assert(UniqueMovable<Buffer>);
        auto source = std::make_unique<Buffer>(3, 0);
        const auto name = source->GetUBO(), size = source->GetUniformTypeSize();
        const std::array<GLuint, 4> values{ 123, 456, 789, 1011 };
        glBindBuffer(GL_UNIFORM_BUFFER, name); glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(values), values.data());
        Buffer moved(std::move(*source));
        Check(!source->GetUBO() && !source->GetUniformTypeSize(), "UBO moved-from state not zero");
        const auto deletes = Observer::deleteCalls; source.reset();
        Check(Observer::deleteCalls == deletes, "Empty UBO issued deletion");
        Buffer destination(7, 1); const auto old = destination.GetUBO();
        destination = std::move(moved);
        Check(!glIsBuffer(old) && !moved.GetUBO() && destination.GetUBO() == name, "UBO assignment ownership failed");
        auto* same = &destination; destination = std::move(*same);
        std::vector<Buffer> owners; owners.reserve(1); owners.push_back(std::move(destination)); owners.emplace_back(2, 2);
        Check(owners.front().GetUBO() == name && owners.front().GetUniformTypeSize() == size, "UBO relocation lost metadata");
        glBindBuffer(GL_UNIFORM_BUFFER, name);
        std::array<GLuint, 4> actual{}; glGetBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(actual), actual.data());
        GLint64 bytes = 0; glGetBufferParameteri64v(GL_UNIFORM_BUFFER, GL_BUFFER_SIZE, &bytes);
        GLint indexed = 0; glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &indexed);
        Check(actual == values && bytes == 3 * size && static_cast<GLuint>(indexed) == name, "UBO storage/data/binding changed after move");
#if GENGINE_RENDER_COUNTERS
        Check(RenderCounters::Current().liveNames[0] == Observer::buffers.size(), "Live UBO counter differs from driver calls");
#endif
        for (auto failure : {Observer::Fault::BufferName, Observer::Fault::BufferStorage})
        {
            Observer::fault = failure;
            Reject<std::runtime_error>([&] { owners.front() = Buffer(5, 0); });
            Check(owners.front().GetUBO() == name && Bound(GL_UNIFORM_BUFFER_BINDING) == name,
                "Failed UBO replacement changed owner/generic binding");
            glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &indexed);
            Check(static_cast<GLuint>(indexed) == name, "Failed UBO replaced published indexed binding");
            Observer::ErrorObserved();
        }
        const auto attempts = Observer::attempts;
        Reject<std::invalid_argument>([] { Buffer empty(0); });
        const auto maxBindings = Bound(GL_MAX_UNIFORM_BUFFER_BINDINGS);
        Reject<std::out_of_range>([&] { Buffer invalid(1, maxBindings); });
        Check(Observer::attempts == attempts, "Invalid UBO request generated a name");
        Observer::fault = Observer::Fault::BufferStorage;
        Reject<std::runtime_error>([] { Buffer huge((std::numeric_limits<unsigned int>::max)()); });
        Check(Observer::requestedBytes == static_cast<GLsizeiptr>((std::numeric_limits<unsigned int>::max)()) * size,
            "UBO byte count overflowed before reaching GL");
        Observer::ErrorObserved();
        // Successful recovery replaces the old name and binding only after allocation.
        owners.front() = Buffer(4, 0);
        Check(!glIsBuffer(name) && owners.front().GetUBO(), "UBO recovery failed to retire destination");
        owners.front() = std::move(moved);
        Check(!owners.front().GetUBO() && !owners.front().GetUniformTypeSize(), "Empty UBO assignment retained state");
        std::cout << "[PASS] UBO type=" << static_cast<int>(Type) << " moves/relocation/data/failures/recovery\n";
    }

    void Storage(const RenderBufferObject& buffer, unsigned width, unsigned height, unsigned samples)
    {
        GLint w = 0, h = 0, s = 0, format = 0;
        glGetNamedRenderbufferParameteriv(buffer.GetID(), GL_RENDERBUFFER_WIDTH, &w);
        glGetNamedRenderbufferParameteriv(buffer.GetID(), GL_RENDERBUFFER_HEIGHT, &h);
        glGetNamedRenderbufferParameteriv(buffer.GetID(), GL_RENDERBUFFER_SAMPLES, &s);
        glGetNamedRenderbufferParameteriv(buffer.GetID(), GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
        Check(w == static_cast<GLint>(width) && h == static_cast<GLint>(height) && format == GL_DEPTH24_STENCIL8
            && (samples > 1 ? s >= static_cast<GLint>(samples) : s == 0)
            && buffer.GetWidth() == width && buffer.GetHeight() == height && buffer.GetSamples() == samples,
            "Renderbuffer allocated storage or metadata differs");
    }

    void Renderbuffers()
    {
        for (unsigned samples : {1u, 4u})
        {
            auto source = std::make_unique<RenderBufferObject>(16, 12, samples);
            const auto name = source->GetID();
            RenderBufferObject moved(std::move(*source));
            Check(!source->GetID() && !source->GetWidth() && !source->GetHeight() && !source->GetSamples(), "RBO moved-from state not zero");
            const auto deletes = Observer::deleteCalls; source.reset();
            Check(Observer::deleteCalls == deletes, "Empty RBO issued deletion");
            RenderBufferObject destination(8, 8); const auto old = destination.GetID();
            destination = std::move(moved);
            Check(!glIsRenderbuffer(old) && destination.GetID() == name, "RBO assignment failed");
            auto* same = &destination; destination = std::move(*same);
            std::vector<RenderBufferObject> owners;
            owners.reserve(1); owners.push_back(std::move(destination)); owners.emplace_back(3, 3);
            Check(owners.front().GetID() == name, "RBO relocation changed identity");
            Storage(owners.front(), 16, 12, samples);
            const auto unrelated = owners.back().GetID();
            for (auto failure : {Observer::Fault::RenderbufferName, Observer::Fault::RenderbufferStorage})
            {
                glBindRenderbuffer(GL_RENDERBUFFER, unrelated);
                Observer::fault = failure;
                Reject<std::runtime_error>([&] { owners.front().Resize(20, 24, samples); });
                Check(owners.front().GetID() == name && Bound(GL_RENDERBUFFER_BINDING) == unrelated, "Failed RBO resize changed owner/binding");
                Storage(owners.front(), 16, 12, samples); Observer::ErrorObserved();
                Observer::fault = failure;
                Reject<std::runtime_error>([&] { RenderBufferObject failed(2, 2, samples); });
                Observer::ErrorObserved();
            }
            const auto attempts = Observer::attempts;
            Reject<std::invalid_argument>([] { RenderBufferObject bad(0, 1); });
            Reject<std::invalid_argument>([] { RenderBufferObject bad(1, 0); });
            Reject<std::invalid_argument>([] { RenderBufferObject bad(1, 1, 0); });
            const auto maxSize = Bound(GL_MAX_RENDERBUFFER_SIZE), maxSamples = Bound(GL_MAX_SAMPLES);
            Reject<std::out_of_range>([&] { RenderBufferObject bad(maxSize + 1, 1); });
            Reject<std::out_of_range>([&] { RenderBufferObject bad(1, 1, maxSamples + 1); });
            Check(Observer::attempts == attempts, "Invalid RBO request generated a name");
            owners.front().Resize(20, 24, samples);
            Check(!glIsRenderbuffer(name), "Successful RBO resize retained old owner");
            Storage(owners.front(), 20, 24, samples);
#if GENGINE_RENDER_COUNTERS
            Check(RenderCounters::Current().liveNames[5] == Observer::renderbuffers.size(), "Live RBO counters differ");
#endif
            owners.front() = std::move(moved);
            Check(!owners.front().GetID() && !owners.front().GetWidth() && !owners.front().GetSamples(), "Empty RBO assignment retained state");
        }
        std::cout << "[PASS] RBO single/multisample moves/relocation/resize/failure/recovery\n";
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
        Check(!target.OnResize(0,24) && !target.RenderSize({(std::numeric_limits<float>::quiet_NaN)(),24}) && !target.SetSamples(0), "Invalid target input accepted");
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
        auto buffer = std::make_unique<Buffer>(1);
        auto renderbuffer = std::make_unique<RenderBufferObject>(2, 2);
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
                Renderbuffers(); Targets(); observer.Verify();
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
