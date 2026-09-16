// Validation infrastructure only. This executable does not initialize GEngine.
#include <sdl2/SDL.h>
#include <Core/GLDebug.h>
#include <Core/RenderCounters.h>
#include <windows.h>

#include <array>
#include <exception>
#include <expected>
#include <iostream>
#include <memory>
#include <numeric>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <version>

static_assert(_MSVC_LANG >= 202302L, "Phase 26 requires the selected C++23 mode");
static_assert(__cpp_lib_expected >= 202202L, "The selected library must provide std::expected");
static_assert(__cpp_lib_print >= 202207L, "The selected library must provide std::print/println");

namespace
{
    // Shared process contract: pass, failure, usage, unavailable prerequisite.
    constexpr int Pass = 0, Failure = 1, Usage = 2, Unavailable = 3;

    int Cpp23Capability()
    {
        enum class DecodeError { InvalidImage };
        const std::expected<int, DecodeError> loaded{23};
        const std::expected<int, DecodeError> failed{std::unexpected(DecodeError::InvalidImage)};
        const std::expected<void, DecodeError> complete{};
        const std::expected<void, DecodeError> rejected{std::unexpected(DecodeError::InvalidImage)};
        std::expected<std::unique_ptr<int>, DecodeError> owner{std::make_unique<int>(26)};
        auto moved = std::move(owner);
        if (!loaded || *loaded != 23 || failed || failed.error() != DecodeError::InvalidImage
            || !complete || rejected || rejected.error() != DecodeError::InvalidImage
            || !moved || !*moved || **moved != 26 || *owner)
            return Failure;
        std::print("[C++23] expected={} print={} ", __cpp_lib_expected, __cpp_lib_print);
        std::println("lang={} compiler={} STL={} update={}",
            _MSVC_LANG, _MSC_FULL_VER, _MSVC_STL_VERSION, _MSVC_STL_UPDATE);
        std::println("[PASS] cpp23-capability success/failure/void/move-only");
        return Pass;
    }
    struct UnavailableError : std::runtime_error { using std::runtime_error::runtime_error; };

    void Require(bool condition, std::string_view message)
    {
        if (!condition) throw std::runtime_error(std::string(message));
    }

    struct TestCase { std::string_view name; void (*run)(); };

    int Run(std::span<const TestCase> tests)
    {
        int failed = 0, unavailable = 0;
        for (const auto& test : tests)
        {
            try
            {
                test.run();
                std::cout << "[PASS] " << test.name << '\n';
            }
            catch (const UnavailableError& error)
            {
                ++unavailable;
                std::cerr << "[UNAVAILABLE] " << test.name << ": " << error.what() << '\n';
            }
            catch (const std::exception& error)
            {
                ++failed;
                std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
            }
            catch (...)
            {
                ++failed;
                std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
            }
        }
        std::cout << "[SUMMARY] tests=" << tests.size() << " failed=" << failed
            << " unavailable=" << unavailable << '\n';
        return failed ? Failure : unavailable ? Unavailable : Pass;
    }

    void CpuSelfTest()
    {
        // A small real allocation/workload for the runner and sanitizer smoke.
        auto storage = std::make_unique<int[]>(64);
        const std::span<int> values(storage.get(), 64);
        std::iota(values.begin(), values.end(), 0);
        Require(std::accumulate(values.begin(), values.end(), 0) == 2016, "CPU workload result differs");
    }

    void CounterReset()
    {
        namespace counters = GEngine::RenderCounters;
        counters::BeginFrame();
        counters::RecordPass(counters::Pass::Shadow);
        counters::RecordPass(counters::Pass::Shadow);
        counters::RecordPass(counters::Pass::Picking);
        counters::RecordTargetReallocation(false);
        counters::RecordTargetReallocation(true);
        counters::EndFrame();
        const auto first = counters::LastFrame();
        Require(first.frame.shadowPasses == (counters::Enabled ? 2 : 0)
            && first.frame.pickingPasses == (counters::Enabled ? 1 : 0)
            && first.frame.targetReallocations == (counters::Enabled ? 1 : 0), "Pass/reallocation accumulation differs");
        counters::BeginFrame();
        Require(counters::Current().frame.shadowPasses == 0 && counters::Current().frame.pickingPasses == 0
            && counters::Current().frame.targetReallocations == 0, "Frame counters were not reset");
        Require(counters::LastFrame().frame.shadowPasses == first.frame.shadowPasses, "Completed snapshot changed at reset");
        Require(counters::Current().frameNumber == first.frameNumber + (counters::Enabled ? 1 : 0), "Frame sequence differs");
        counters::EndFrame();
    }

    void GlCounters()
    {
        namespace counters = GEngine::RenderCounters;
        Require(gladLoadGLLoader(SDL_GL_GetProcAddress) != 0, "GL loader failed");
        const auto context = SDL_GL_GetCurrentContext();
        GLuint vao = 0, buffer = 0, textures[2]{}, sampler = 0, fbo = 0, rbo = 0;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &buffer);
        glGenTextures(1, textures);
        glCreateTextures(GL_TEXTURE_2D, 1, textures + 1);
        glGenSamplers(1, &sampler);
        glGenFramebuffers(1, &fbo);
        glGenRenderbuffers(1, &rbo);
        const GLuint vertex = glCreateShader(GL_VERTEX_SHADER), fragment = glCreateShader(GL_FRAGMENT_SHADER);
        const GLuint program = glCreateProgram();
        const char* vs = "#version 460 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}";
        const char* fs = "#version 460 core\nout vec4 color;void main(){color=vec4(1,0,0,1);}";
        glShaderSource(vertex, 1, &vs, nullptr); glCompileShader(vertex);
        glShaderSource(fragment, 1, &fs, nullptr); glCompileShader(fragment);
        glAttachShader(program, vertex); glAttachShader(program, fragment); glLinkProgram(program);
        GLint linked = 0; glGetProgramiv(program, GL_LINK_STATUS, &linked);
        Require(linked == GL_TRUE, "Counter fixture program did not link");

        counters::BeginFrame();
        // Repeated requests count even when they repeat the same binding.
        glUseProgram(program); glUseProgram(program);
        glBindVertexArray(vao); glBindVertexArray(vao);
        glBindTexture(GL_TEXTURE_2D, textures[0]); glBindTexture(GL_TEXTURE_2D, textures[0]);
        glBindSampler(0, sampler); glBindSampler(0, sampler);
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
        const GLuint indices[]{ 0, 1, 2, 0, 1, 2 };
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices) * 2, nullptr, GL_STATIC_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(GLuint), indices);
        glViewport(0, 0, 64, 64);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 2);
        glDrawElementsInstanced(GL_TRIANGLES, 3, GL_UNSIGNED_INT, nullptr, 3);
        glDrawArrays(GL_LINES, 0, 2);
        glDrawArrays(GL_TRIANGLES, 0, 0);
        std::array<unsigned char, 4> pixel{};
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        Require(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0, "Instrumented draw/readback changed output");
        counters::EndFrame();
        const auto snapshot = counters::LastFrame();
        const auto& frame = snapshot.frame;
        if constexpr (counters::Enabled)
        {
            Require(frame.draws == 6 && frame.indexedDraws == 2 && frame.submittedTriangles == 8, "Known draw sequence differs");
            Require(frame.programBinds == 2 && frame.vaoBinds == 2 && frame.textureBinds == 2
                && frame.samplerBinds == 2 && frame.framebufferBinds == 2, "Bind accumulation differs");
            Require(frame.bufferAllocationCalls == 2 && frame.bufferUploadCalls == 2
                && frame.bufferUploadBytes == sizeof(indices) + sizeof(GLuint), "Null allocation/upload accounting differs");
            Require(frame.readbackCalls == 1 && frame.readbackCpuNanoseconds > 0, "Readback timing/count differs");
            const std::array<std::uint64_t, 8> expected{ 1, 1, 2, 1, 1, 1, 2, 1 };
            Require(snapshot.liveNames == expected && snapshot.estimatedBufferBytes == sizeof(indices), "Live name/store accounting differs");
        }
        else
        {
            Require(frame.draws == 0 && frame.indexedDraws == 0 && frame.submittedTriangles == 0
                && frame.programBinds == 0 && frame.vaoBinds == 0 && frame.textureBinds == 0
                && frame.samplerBinds == 0 && frame.framebufferBinds == 0 && frame.bufferAllocationCalls == 0
                && frame.bufferUploadCalls == 0 && frame.bufferUploadBytes == 0 && frame.readbackCalls == 0
                && frame.readbackCpuNanoseconds == 0 && snapshot.estimatedBufferBytes == 0,
                "Disabled counters performed bookkeeping");
        }
        counters::BeginFrame();
        Require(counters::Current().frame.draws == 0 && counters::Current().frame.bufferUploadBytes == 0
            && counters::Current().frame.readbackCpuNanoseconds == 0, "GL frame counters were not reset");
        Require(counters::Current().liveNames == snapshot.liveNames
            && counters::Current().estimatedBufferBytes == snapshot.estimatedBufferBytes, "Frame reset lost live resources");
        Require(counters::LastFrame().frame.draws == frame.draws, "Completed GL snapshot was not retained");
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);
        Require(counters::Current().estimatedBufferBytes == 0, "Zero-size buffer replacement retained old estimate");
        glUseProgram(0); glBindVertexArray(0); glBindSampler(0, 0); glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteProgram(program); glDeleteShader(vertex); glDeleteShader(fragment);
        glDeleteBuffers(1, &buffer); glDeleteBuffers(1, &buffer); // Repeated deletion must not underflow.
        glDeleteVertexArrays(1, &vao); glDeleteTextures(2, textures);
        glDeleteSamplers(1, &sampler); glDeleteFramebuffers(1, &fbo); glDeleteRenderbuffers(1, &rbo);
        Require(counters::Current().liveNames == std::array<std::uint64_t, 8>{}, "Deleted resources remain in counters");
        glGenBuffers(1, &buffer);
        counters::ForgetContext(context);
        Require(counters::Current().liveNames == std::array<std::uint64_t, 8>{}, "Context retirement retained names");
        glDeleteBuffers(1, &buffer);
        Require(glGetError() == GL_NO_ERROR, "Counter fixture left a GL error");
        std::cout << "[COUNTERS] enabled=" << counters::Enabled << " draw-sequence=6/2/8 pixel=red reset=ok resources=retired\n";
    }

    void DebugDiagnostics()
    {
        Require(gladLoadGLLoader(SDL_GL_GetProcAddress) != 0, "GL loader failed");
        GLint flags = 0, originalDepth = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        glGetIntegerv(GL_DEBUG_GROUP_STACK_DEPTH, &originalDepth);
#ifdef GENGINE_CONFIG_DEBUG
        // Exercise the capability fallback without changing context/driver state.
        const int core = GLAD_GL_VERSION_4_3, extension = GLAD_GL_KHR_debug;
        GLAD_GL_VERSION_4_3 = GLAD_GL_KHR_debug = 0;
        const bool unavailable = !GEngine::GLDebug::Initialize();
        { const GEngine::GLDebug::Group group("Unavailable diagnostics"); }
        GLAD_GL_VERSION_4_3 = core; GLAD_GL_KHR_debug = extension;
        Require(unavailable, "Unsupported diagnostics did not fall back");
        Require(GEngine::GLDebug::Initialize(), "KHR_debug callback unavailable");
        Require(glIsEnabled(GL_DEBUG_OUTPUT) && glIsEnabled(GL_DEBUG_OUTPUT_SYNCHRONOUS),
            "Debug output must be synchronous on the context-owning thread");
        void* installed = nullptr;
        glGetPointerv(GL_DEBUG_CALLBACK_FUNCTION, &installed);
        Require(installed == reinterpret_cast<void*>(&GEngine::GLDebug::Message), "Production callback was not installed");
        struct Capture
        {
            SDL_threadID owner = SDL_ThreadID();
            unsigned delivered = 0;
            bool correct = true;
            static void APIENTRY Receive(GLenum source, GLenum type, GLuint id, GLenum severity,
                GLsizei length, const GLchar* message, const void* user) noexcept
            {
                auto& capture = *static_cast<Capture*>(const_cast<void*>(user));
                ++capture.delivered;
                capture.correct &= SDL_ThreadID() == capture.owner && source == GL_DEBUG_SOURCE_APPLICATION
                    && type == GL_DEBUG_TYPE_MARKER && id >= 50001 && id <= 50003
                    && severity != GL_DEBUG_SEVERITY_NOTIFICATION && message && length > 0;
                GEngine::GLDebug::Message(source, type, id, severity, length, message, nullptr);
            }
            ~Capture() { glDebugMessageCallback(&GEngine::GLDebug::Message, nullptr); }
        } capture;
        glDebugMessageCallback(&Capture::Receive, &capture);
        const std::array<GLenum, 4> severities{ GL_DEBUG_SEVERITY_HIGH, GL_DEBUG_SEVERITY_MEDIUM,
            GL_DEBUG_SEVERITY_LOW, GL_DEBUG_SEVERITY_NOTIFICATION };
        {
            const GEngine::GLDebug::Group group("Phase 05 validation messages");
            GLint depth = 0;
            glGetIntegerv(GL_DEBUG_GROUP_STACK_DEPTH, &depth);
            Require(depth == originalDepth + 1, "Debug group was not pushed");
            for (unsigned i = 0; i < severities.size(); ++i)
                glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 50001 + i,
                    severities[i], -1, "Phase 05 validation-only marker; no GL error");
        }
        Require(capture.delivered == 3 && capture.correct,
            "Callback fields/thread or notification/group filtering differ");
        try
        {
            const GEngine::GLDebug::Group group("Phase 05 unwind validation");
            throw std::logic_error("validation-only unwind");
        }
        catch (const std::logic_error&) {}
        std::cout << "[GL-DEBUG] callback=active synchronous=yes delivered=3 notifications=filtered groups=balanced debug-context="
            << ((flags & GL_CONTEXT_FLAG_DEBUG_BIT) ? "yes" : "no") << '\n';
#else
        Require(!(flags & GL_CONTEXT_FLAG_DEBUG_BIT), "Release requested a Debug context");
        Require(!GEngine::GLDebug::Initialize(), "Release enabled diagnostics");
        void* installed = nullptr;
        glGetPointerv(GL_DEBUG_CALLBACK_FUNCTION, &installed);
        Require(installed == nullptr && !glIsEnabled(GL_DEBUG_OUTPUT), "Release installed/enabled a callback");
        {
            const GEngine::GLDebug::Group group("Release must not submit groups");
            GLint depth = 0;
            glGetIntegerv(GL_DEBUG_GROUP_STACK_DEPTH, &depth);
            Require(depth == originalDepth, "Release submitted a debug group");
        }
        std::cout << "[GL-DEBUG] callback=disabled groups=disabled debug-context=no\n";
#endif
        GLint finalDepth = 0;
        glGetIntegerv(GL_DEBUG_GROUP_STACK_DEPTH, &finalDepth);
        Require(finalDepth == originalDepth && glGetError() == GL_NO_ERROR,
            "Debug diagnostics left an unbalanced group or GL error");
    }

    void HiddenGlContext(bool diagnostics, bool counters = false)
    {
        // SDL is delay-loaded: neither CPU tests nor help need its runtime DLL.
        using Library = std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>;
        Library library(LoadLibraryW(L"SDL2.dll"), &FreeLibrary);
        if (!library) throw UnavailableError("SDL2.dll is unavailable; use the documented launcher");
        struct VideoCleanup { ~VideoCleanup() { SDL_Quit(); } } video;
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO) != 0) throw UnavailableError(SDL_GetError());
        const auto owner = SDL_ThreadID();
        Require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) == 0
            && SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6) == 0
            && SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) == 0,
            "Cannot configure hidden OpenGL 4.6 core fixture");
        if (diagnostics) GEngine::GLDebug::ConfigureContext();

        // Two cycles exercise creation, scope cleanup, and recreation on one thread.
        for (int cycle = 0; cycle < 2; ++cycle)
        {
            {
                using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
                Window window(SDL_CreateWindow("Rendering validation", 0, 0, 64, 64,
                    SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), &SDL_DestroyWindow);
                if (!window) throw UnavailableError(SDL_GetError());
                using Context = std::unique_ptr<void, decltype(&SDL_GL_DeleteContext)>;
                Context context(diagnostics ? GEngine::GLDebug::CreateContext(window.get())
                    : SDL_GL_CreateContext(window.get()), &SDL_GL_DeleteContext);
                if (!context) throw UnavailableError(SDL_GetError());
                Require(SDL_ThreadID() == owner && SDL_GL_GetCurrentContext() == context.get(),
                    "GL context is not current on its owning thread");
                Require(!(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_SHOWN), "GL window became visible");
                // Resolve only the fixture's queries; no engine/global GL loader state.
                const auto getString = reinterpret_cast<const GLubyte* (APIENTRY*)(GLenum)>(SDL_GL_GetProcAddress("glGetString"));
                const auto getError = reinterpret_cast<GLenum (APIENTRY*)()>(SDL_GL_GetProcAddress("glGetError"));
                Require(getString && getError, "GL query functions are unavailable");
                const auto version = getString(GL_VERSION), renderer = getString(GL_RENDERER);
                Require(version && renderer && getError() == GL_NO_ERROR, "GL context query failed");
                std::cout << "[GL] cycle=" << cycle << " version=" << version << " renderer=" << renderer << '\n';
                if (diagnostics) DebugDiagnostics();
                if (counters) GlCounters();
                // Reverse declaration order deletes context before window, still here.
            }
            Require(SDL_ThreadID() == owner && SDL_GL_GetCurrentContext() == nullptr,
                "GL context remained current after destruction");
        }
    }

#ifdef __SANITIZE_ADDRESS__
    __declspec(noinline) void AsanFailureProbe(std::size_t index)
    {
        // Intentional isolated negative control; never part of the normal suite.
        volatile int* value = new int[1];
        value[index] = 7;
        delete[] value;
    }
#endif
}

int main(int argc, char** argv)
{
    const std::string_view mode = argc == 1 ? "--self-test" : argv[1];
    if (argc > 2)
    {
        std::cerr << "[USAGE] Expected one mode; use --help\n";
        return Usage;
    }
    if (mode == "--help")
    {
        std::cout << "RenderingValidation [--cpp23|--self-test|--counters|--gl|--gl-debug|--gl-counters|--failure-probe|--exception-probe|--asan-failure-probe]\n";
        return Pass;
    }
    if (mode == "--cpp23") return Cpp23Capability();
    const std::array cpu{ TestCase{"cpu-self-test", &CpuSelfTest} };
    if (mode == "--counters")
    {
        const std::array tests{ TestCase{"counter-reset-accumulation", &CounterReset} };
        return Run(tests);
    }
    if (mode == "--gl-counters")
    {
        const std::array tests{ TestCase{"gl-renderer-counters", [] { HiddenGlContext(false, true); }} };
        return Run(tests);
    }
    if (mode == "--self-test")
    {
#ifdef __SANITIZE_ADDRESS__
        std::cout << "[HARNESS] AddressSanitizer=on\n";
#else
        std::cout << "[HARNESS] AddressSanitizer=off\n";
#endif
        return Run(cpu);
    }
    if (mode == "--gl" || mode == "--gl-debug")
    {
        const std::array tests{ mode == "--gl-debug"
            ? TestCase{"gl-debug-diagnostics", [] { HiddenGlContext(true); }}
            : TestCase{"hidden-gl-context", [] { HiddenGlContext(false); }} };
        return Run(tests);
    }
    if (mode == "--failure-probe" || mode == "--exception-probe")
    {
        const std::array tests{
            TestCase{"failure-probe", [] { Require(false, "intentional assertion failure"); }},
            TestCase{"exception-probe", [] { throw std::logic_error("intentional exception"); }}
        };
        return Run(std::span(tests).subspan(mode == "--failure-probe" ? 0 : 1, 1));
    }
    if (mode == "--asan-failure-probe")
    {
#ifdef __SANITIZE_ADDRESS__
        AsanFailureProbe(static_cast<std::size_t>(argc - 1));
        std::cerr << "[FAIL] AddressSanitizer did not stop the intentional heap-buffer-overflow\n";
        return Failure;
#else
        std::cerr << "[UNAVAILABLE] This binary was built without AddressSanitizer\n";
        return Unavailable;
#endif
    }
    std::cerr << "[USAGE] Unknown mode; use --help\n";
    return Usage;
}
