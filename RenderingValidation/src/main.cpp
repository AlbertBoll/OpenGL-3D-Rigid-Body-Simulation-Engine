// Validation infrastructure only. This executable does not initialize GEngine.
#include <sdl2/SDL.h>
#include <Core/GLDebug.h>
#include <windows.h>

#include <array>
#include <exception>
#include <iostream>
#include <memory>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace
{
    // Shared process contract: pass, failure, usage, unavailable prerequisite.
    constexpr int Pass = 0, Failure = 1, Usage = 2, Unavailable = 3;
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

    void HiddenGlContext(bool diagnostics)
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
        std::cout << "RenderingValidation [--self-test|--gl|--gl-debug|--failure-probe|--exception-probe|--asan-failure-probe]\n";
        return Pass;
    }
    const std::array cpu{ TestCase{"cpu-self-test", &CpuSelfTest} };
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
