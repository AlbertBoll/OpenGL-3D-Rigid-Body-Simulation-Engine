// Links the production GEngine library; run through test_vertex_buffer.py.
#include "gepch.h"
#include "Core/GLDebug.h"
#include "Mesh/VertexBuffer.h"
#include <array>
#include <span>
#include <stdexcept>

namespace
{
    using GEngine::Buffer::VertexBuffer;
    static_assert(std::is_constructible_v<VertexBuffer, std::span<const float>>);
    static_assert(!std::is_constructible_v<VertexBuffer, const std::vector<float>&, unsigned int>);

    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    template<class Exception, class Action>
    void Reject(Action action)
    {
        try { action(); }
        catch (const Exception&) { return; }
        throw std::runtime_error("Invalid request did not throw the required exception");
    }

    // Observe real driver calls in both configurations, including Release without counters.
    struct Calls
    {
        inline static PFNGLGENBUFFERSPROC generate;
        inline static PFNGLBINDBUFFERPROC bind;
        inline static PFNGLBUFFERDATAPROC allocate;
        inline static PFNGLBUFFERSUBDATAPROC upload;
        inline static unsigned generated, bound, allocated, uploaded;
        inline static GLsizeiptr uploadBytes;
        inline static GLintptr uploadOffset;
        static void APIENTRY Generate(GLsizei count, GLuint* names) { ++generated; generate(count, names); }
        static void APIENTRY Bind(GLenum target, GLuint name) { ++bound; bind(target, name); }
        static void APIENTRY Allocate(GLenum target, GLsizeiptr bytes, const void* data, GLenum usage)
        { ++allocated; allocate(target, bytes, data, usage); }
        static void APIENTRY Upload(GLenum target, GLintptr offset, GLsizeiptr bytes, const void* data)
        { ++uploaded; uploadBytes = bytes; uploadOffset = offset; upload(target, offset, bytes, data); }
        Calls()
        {
            generate = glad_glGenBuffers; bind = glad_glBindBuffer;
            allocate = glad_glBufferData; upload = glad_glBufferSubData;
            glad_glGenBuffers = Generate; glad_glBindBuffer = Bind;
            glad_glBufferData = Allocate; glad_glBufferSubData = Upload;
        }
        ~Calls()
        {
            glad_glGenBuffers = generate; glad_glBindBuffer = bind;
            glad_glBufferData = allocate; glad_glBufferSubData = upload;
        }
        static void Reset() { generated = bound = allocated = uploaded = 0; }
        static void RequireNone()
        {
            Require(generated == 0 && bound == 0 && allocated == 0 && uploaded == 0,
                "Empty/rejected request issued a buffer GL call");
        }
    };

    struct Diagnostics
    {
        SDL_threadID owner = SDL_ThreadID();
        unsigned markers = 0, problems = 0;
        bool ownerThread = true;
        static void APIENTRY Receive(GLenum source, GLenum type, GLuint id, GLenum severity,
            GLsizei length, const GLchar* message, const void* user) noexcept
        {
            auto& state = *static_cast<Diagnostics*>(const_cast<void*>(user));
            state.ownerThread &= SDL_ThreadID() == state.owner;
            if (source == GL_DEBUG_SOURCE_APPLICATION && type == GL_DEBUG_TYPE_MARKER && id == 80001)
                ++state.markers;
            else if (type == GL_DEBUG_TYPE_ERROR || severity != GL_DEBUG_SEVERITY_NOTIFICATION)
            {
                ++state.problems;
                std::fprintf(stderr, "[GL-DIAGNOSTIC] type=0x%x severity=0x%x id=%u %.*s\n",
                    type, severity, id, length, message);
            }
        }
        Diagnostics()
        {
            Require(glDebugMessageCallback && glDebugMessageControl && glDebugMessageInsert, "KHR_debug unavailable");
#ifdef GENGINE_CONFIG_DEBUG
            Require(GEngine::GLDebug::Initialize(), "Production Debug diagnostics unavailable");
#endif
            // Test-only diagnostics also observe Release without changing production policy.
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(Receive, this);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        }
        ~Diagnostics() { glDebugMessageCallback(nullptr, nullptr); }
        void Verify()
        {
            Require(markers == 1 && problems == 0 && ownerThread, "GL diagnostics or callback thread check failed");
            Require(glGetError() == GL_NO_ERROR, "Buffer fixture left a GL error");
        }
    };

    void CheckContents(VertexBuffer& buffer, std::span<const float> expected)
    {
        buffer.Bind();
        GLint64 size = -1;
        glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
        Require(size == static_cast<GLint64>(expected.size_bytes()), "Allocated byte capacity differs");
        std::vector<float> actual(expected.size());
        if (!actual.empty())
            glGetBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(expected.size_bytes()), actual.data());
        Require(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()), "GPU buffer contents differ");
    }

    void BufferUploads()
    {
        Calls calls;
        const std::array initial{ 1.25f, -2.5f, 3.75f, 4.5f, -5.25f, 6.75f };
        VertexBuffer initialized(initial);
        CheckContents(initialized, initial);
        VertexBuffer dynamic(sizeof(initial));
        Calls::Reset();
        // vector -> span compatibility, with the full byte count checked independently.
        dynamic.SetData(std::vector<float>(initial.begin(), initial.end()));
        Require(Calls::uploaded == 1 && Calls::uploadBytes == sizeof(initial) && Calls::uploadOffset == 0,
            "Full upload did not submit the complete float payload in bytes");
        CheckContents(dynamic, initial);
        std::cout << "[PASS] typed-construction/full-upload\n";

        const std::array patch{ 12.5f, -25.0f };
        auto expected = initial;
        dynamic.SetData(patch);
        expected[0] = patch[0]; expected[1] = patch[1];
        CheckContents(dynamic, expected);
        Calls::Reset();
        dynamic.SetData(patch, 2 * sizeof(float));
        Require(Calls::uploaded == 1 && Calls::uploadBytes == sizeof(patch) && Calls::uploadOffset == 2 * sizeof(float),
            "Partial upload size or nonzero byte offset differs");
        expected[2] = patch[0]; expected[3] = patch[1];
        CheckContents(dynamic, expected);
        dynamic.SetData(patch, sizeof(initial) - sizeof(patch));
        expected[4] = patch[0]; expected[5] = patch[1];
        CheckContents(dynamic, expected);
        std::cout << "[PASS] partial/nonzero-offset/exact-end/preserved-neighbors\n";

        initialized.Bind();
        Calls::Reset();
        dynamic.SetData({});
        dynamic.SetData({}, sizeof(float));
        dynamic.SetData({}, sizeof(initial));
        Calls::RequireNone();
        CheckContents(dynamic, expected);
        VertexBuffer empty(std::span<const float>{});
        CheckContents(empty, {});
        VertexBuffer zero(0);
        CheckContents(zero, {});
        Calls::Reset();
        empty.SetData({}); zero.SetData({});
        Calls::RequireNone();
        std::cout << "[PASS] empty-start/interior/end/zero-capacity\n";

        initialized.Bind();
        Calls::Reset();
        Reject<std::out_of_range>([&] { dynamic.SetData(patch, sizeof(initial) - sizeof(patch) + 1); });
        Reject<std::out_of_range>([&] { dynamic.SetData(patch, sizeof(initial)); });
        Reject<std::out_of_range>([&] { dynamic.SetData({}, sizeof(initial) + 1); });
        Reject<std::out_of_range>([&] { dynamic.SetData(patch, (std::numeric_limits<std::size_t>::max)()); });
        Reject<std::out_of_range>([&] { dynamic.SetData({}, (std::numeric_limits<std::size_t>::max)()); });
        const std::array<float, 7> oversized{};
        Reject<std::out_of_range>([&] { dynamic.SetData(oversized); });
        Reject<std::out_of_range>([&] { empty.SetData(patch); });
        Reject<std::out_of_range>([&] { zero.SetData({}, 1); });
        Reject<std::length_error>([] { VertexBuffer invalid((std::numeric_limits<std::size_t>::max)()); });
        Reject<std::length_error>([] {
            VertexBuffer invalid(static_cast<std::size_t>((std::numeric_limits<GLsizeiptr>::max)()) + 1);
        });
        Calls::RequireNone();
        CheckContents(dynamic, expected);
        // Allocation capacity need not be float-aligned: offsets still refer to bytes.
        VertexBuffer odd(sizeof(float) + 1);
        odd.SetData(std::span(patch).first(1), 1);
        float shifted = 0;
        glGetBufferSubData(GL_ARRAY_BUFFER, 1, sizeof(float), &shifted);
        Require(shifted == patch[0], "Byte offset was interpreted as an element offset");
        std::cout << "[PASS] boundary-rejection/overflow-rejection/no-GL-on-rejection/byte-offset\n";
    }
}

int main()
{
    try
    {
        SDL_SetMainReady();
        struct Video { ~Video() { SDL_Quit(); } } video;
        Require(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
        Require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) == 0
            && SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6) == 0
            && SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) == 0,
            "Cannot configure OpenGL 4.6 core");
        GEngine::GLDebug::ConfigureContext();
        const auto owner = SDL_ThreadID();
        for (int cycle = 0; cycle < 2; ++cycle)
        {
            {
                std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                    SDL_CreateWindow("VertexBuffer validation", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN),
                    &SDL_DestroyWindow);
                Require(window != nullptr, SDL_GetError());
                std::unique_ptr<void, decltype(&SDL_GL_DeleteContext)> context(
                    GEngine::GLDebug::CreateContext(window.get()), &SDL_GL_DeleteContext);
                Require(context != nullptr, SDL_GetError());
                Require(SDL_GL_GetCurrentContext() == context.get() && SDL_ThreadID() == owner, "Wrong context/thread");
                Require(gladLoadGLLoader(SDL_GL_GetProcAddress) != 0, "GL loading failed");
                std::cout << "[GL] cycle=" << cycle << " version=" << glGetString(GL_VERSION)
                    << " renderer=" << glGetString(GL_RENDERER) << '\n';
                Diagnostics diagnostics;
                glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 80001,
                    GL_DEBUG_SEVERITY_LOW, -1, "Phase 08 callback health check");
                BufferUploads(); // Every production buffer dies before diagnostics/context teardown.
                diagnostics.Verify();
                GEngine::RenderCounters::ForgetContext(context.get());
                std::cout << "[PASS] GL-debug/errors/owner-thread/destruction\n";
            }
            Require(SDL_GL_GetCurrentContext() == nullptr && SDL_ThreadID() == owner, "Context teardown failed");
        }
        std::cout << "[PASS] vertex-buffer-upload-safety cycles=2\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
