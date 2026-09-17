#include "../GEngine/src/Assets/ShaderBackend.h"
#include "gepch.h"
#include "Assets/Shaders/Shader.h"
#include "Core/GLDebug.h"
#include <array>
#include <cstdlib>
#include <new>

// An actual allocation-denial control for noexcept moves and destruction.
namespace { thread_local bool denyAllocation = false; }
void* operator new(std::size_t size)
{
    if (denyAllocation) throw std::bad_alloc();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace
{
    using namespace GEngine;
    using namespace GEngine::Asset;
    int checks = 0;
    void Check(bool value, const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
    template<class Exception, class F> void Reject(F&& operation)
    {
        try { operation(); } catch (const Exception&) { ++checks; return; }
        throw std::runtime_error("Expected shader creation failure did not occur");
    }
    static_assert(!std::is_copy_constructible_v<Shader> && !std::is_copy_assignable_v<Shader>
        && std::is_nothrow_move_constructible_v<Shader> && std::is_nothrow_move_assignable_v<Shader>);
    constexpr const char* vertex = R"(#version 460 core
layout(location=0) in vec3 position;
uniform mat4 transform;
void main(){ gl_Position=transform*vec4(position,1); }
)";
    constexpr const char* fragment = R"(#version 460 core
uniform vec4 color;
layout(location=0) out vec4 outputColor;
void main(){ outputColor=color; }
)";
    constexpr const char* invalid = "#version 460 core\nthis is not valid GLSL\n";
    const std::array<ShaderSource, 2> validSources{{{VERTEX, vertex, "valid.vert"}, {FRAGMENT, fragment, "valid.frag"}}};

    struct Observer
    {
        inline static Observer* active;
        PFNGLCREATEPROGRAMPROC createProgram = glad_glCreateProgram;
        PFNGLCREATESHADERPROC createShader = glad_glCreateShader;
        PFNGLDELETEPROGRAMPROC deleteProgram = glad_glDeleteProgram;
        PFNGLDELETESHADERPROC deleteShader = glad_glDeleteShader;
        PFNGLGETPROGRAMINTERFACEIVPROC interfaceQuery = glad_glGetProgramInterfaceiv;
        PFNGLGETUNIFORMLOCATIONPROC uniformQuery = glad_glGetUniformLocation;
        PFNGLGETSHADERINFOLOGPROC shaderLog = glad_glGetShaderInfoLog;
        PFNGLGETPROGRAMINFOLOGPROC programLog = glad_glGetProgramInfoLog;
        PFNGLGETSHADERIVPROC shaderQuery = glad_glGetShaderiv;
        std::unordered_set<GLuint> programs, shaders;
        unsigned created = 0, deleted = 0, deleteCalls = 0, discoveries = 0, lookups = 0;
        bool valid = true, failProgram = false, failShader = false, failReflection = false, failLog = false, failOwnership = false;
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        std::thread::id thread = std::this_thread::get_id();
        static GLuint APIENTRY CreateProgram()
        {
            auto& o = *active;
            if (std::exchange(o.failProgram, false)) return 0;
            const auto name = o.createProgram();
            if (name) { o.valid &= o.programs.insert(name).second; ++o.created; }
            return name;
        }
        static GLuint APIENTRY CreateShader(GLenum type)
        {
            auto& o = *active;
            if (std::exchange(o.failShader, false)) return 0;
            const auto name = o.createShader(type);
            if (name) { o.valid &= o.shaders.insert(name).second; ++o.created; }
            return name;
        }
        void Retire(std::unordered_set<GLuint>& live, GLuint name)
        {
            valid &= SDL_GL_GetCurrentContext() == context && std::this_thread::get_id() == thread;
            valid &= name != 0 && live.erase(name) == 1;
            ++deleted; ++deleteCalls;
        }
        static void APIENTRY DeleteProgram(GLuint name)
        { auto& o = *active; o.Retire(o.programs, name); o.deleteProgram(name); }
        static void APIENTRY DeleteShader(GLuint name)
        { auto& o = *active; o.Retire(o.shaders, name); o.deleteShader(name); }
        static void APIENTRY Interface(GLuint name, GLenum kind, GLenum property, GLint* value)
        {
            auto& o = *active;
            if (kind == GL_UNIFORM && property == GL_ACTIVE_RESOURCES)
            {
                ++o.discoveries;
                if (std::exchange(o.failReflection, false)) throw std::bad_alloc();
            }
            o.interfaceQuery(name, kind, property, value);
        }
        static GLint APIENTRY Uniform(GLuint name, const GLchar* uniform)
        { ++active->lookups; return active->uniformQuery(name, uniform); }
        static void APIENTRY ShaderLog(GLuint name, GLsizei size, GLsizei* written, GLchar* log)
        { if (std::exchange(active->failLog, false)) throw std::bad_alloc(); active->shaderLog(name, size, written, log); }
        static void APIENTRY ProgramLog(GLuint name, GLsizei size, GLsizei* written, GLchar* log)
        { if (std::exchange(active->failLog, false)) throw std::bad_alloc(); active->programLog(name, size, written, log); }
        static void APIENTRY ShaderQuery(GLuint name, GLenum property, GLint* value)
        {
            active->shaderQuery(name, property, value);
            if (property == GL_COMPILE_STATUS && *value == GL_TRUE && std::exchange(active->failOwnership, false))
                denyAllocation = true;
        }
        Observer()
        {
            active = this;
            glad_glCreateProgram = CreateProgram; glad_glCreateShader = CreateShader;
            glad_glDeleteProgram = DeleteProgram; glad_glDeleteShader = DeleteShader;
            glad_glGetProgramInterfaceiv = Interface; glad_glGetUniformLocation = Uniform;
            glad_glGetShaderInfoLog = ShaderLog; glad_glGetProgramInfoLog = ProgramLog;
            glad_glGetShaderiv = ShaderQuery;
        }
        ~Observer()
        {
            glad_glCreateProgram = createProgram; glad_glCreateShader = createShader;
            glad_glDeleteProgram = deleteProgram; glad_glDeleteShader = deleteShader;
            glad_glGetProgramInterfaceiv = interfaceQuery; glad_glGetUniformLocation = uniformQuery;
            glad_glGetShaderInfoLog = shaderLog; glad_glGetProgramInfoLog = programLog;
            glad_glGetShaderiv = shaderQuery;
            active = nullptr;
        }
        void Empty()
        {
            Check(valid && programs.empty() && shaders.empty() && created == deleted,
                "Shader/program leak, duplicate deletion or wrong context/thread");
            const auto counters = RenderCounters::Current();
            Check(counters.liveNames[6] == 0 && counters.liveNames[7] == 0, "Shader/program counters did not retire");
        }
    };

    struct Diagnostics
    {
        bool compilingFailure = false;
        unsigned errors = 0, expectedCompilerMessages = 0, markers = 0;
        static void APIENTRY Receive(GLenum source, GLenum type, GLuint id, GLenum severity,
            GLsizei, const GLchar* message, const void* user)
        {
            auto& d = *static_cast<Diagnostics*>(const_cast<void*>(user));
            if (source == GL_DEBUG_SOURCE_APPLICATION && type == GL_DEBUG_TYPE_MARKER && id == 24001) ++d.markers;
            else if (source == GL_DEBUG_SOURCE_SHADER_COMPILER && d.compilingFailure) ++d.expectedCompilerMessages;
            else if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH || severity == GL_DEBUG_SEVERITY_MEDIUM)
            { ++d.errors; std::fprintf(stderr, "[GL] source=0x%x type=0x%x severity=0x%x id=%u %s\n", source, type, severity, id, message); }
        }
        Diagnostics()
        {
            glEnable(GL_DEBUG_OUTPUT); glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(Receive, this);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 24001,
                GL_DEBUG_SEVERITY_LOW, -1, "Phase 24 diagnostic control");
        }
        ~Diagnostics() { glDebugMessageCallback(nullptr, nullptr); }
    };

    Shader Build()
    {
        auto result = Shader::Create(ShaderProgramDesc{validSources});
        Check(result.has_value(), "Valid shader factory failed");
        return std::move(*result);
    }
    void CheckError(const ShaderCreationResult& result, ShaderCreationCode code,
        std::optional<ShaderType> type = {}, const char* source = "")
    {
        const auto* error = result ? nullptr : &result.error();
        Check(error && error->code == code && error->shaderType == type && error->source == source && !error->log.empty(),
            "Creation result lost structured code/stage/source/log");
    }
    void SuccessAndMoves(Observer& o)
    {
        {
            const auto before = o.discoveries;
            auto source = Build(); const auto name = ::GEngine::Asset::ShaderBackendAccess::Program(source);
            Check(source.IsLinked() && o.discoveries == before + 1 && o.shaders.empty(), "Link did not retire stages or duplicated reflection");
            GLint attached = -1; glGetProgramiv(name, GL_ATTACHED_SHADERS, &attached);
            Check(attached == 0, "Linked program retained intermediate shader objects");
            source.Link(); Check(o.discoveries == before + 1, "Idempotent Link repeated uniform discovery");
            source.Bind();
            const Math::Vec4f color(0.1f, 0.2f, 0.3f, 0.4f);
            const auto queries = o.lookups;
            source.SetUniform("color", color);
            const auto location = ::GEngine::Asset::ShaderBackendAccess::Uniforms(source).at("color");
            std::array<float, 4> actual{}; glGetUniformfv(name, location, actual.data());
            Check(actual[0] == color.x && actual[3] == color.w && o.lookups == queries, "Active uniform cache/readback differs");
            source.SetUniform("missing_uniform", color); source.SetUniform("missing_uniform", color);
            Check(o.lookups == queries + 1 && ::GEngine::Asset::ShaderBackendAccess::Uniforms(source).at("missing_uniform") == -1,
                "Missing uniform was not cached once as signed -1");
            source.UnBind();
            Shader destination = Build(); const auto old = ::GEngine::Asset::ShaderBackendAccess::Program(destination);
            bool control = false;
            denyAllocation = true;
            try { void* memory = ::operator new(1); ::operator delete(memory); }
            catch (const std::bad_alloc&) { control = true; }
            Shader empty;
            Shader moved(std::move(source)); destination = std::move(moved);
            auto* same = &destination; destination = std::move(*same);
            source.Destroy(); moved.Destroy();
            denyAllocation = false;
            Check(control && !::GEngine::Asset::ShaderBackendAccess::Program(source) && !source.IsLinked() && !::GEngine::Asset::ShaderBackendAccess::Program(moved)
                && !glIsProgram(old) && ::GEngine::Asset::ShaderBackendAccess::Program(destination) == name, "Allocation-free move contract failed");
            Check(::GEngine::Asset::ShaderBackendAccess::Uniforms(destination).at("missing_uniform") == -1, "Moved uniform cache lost missing lookup");
            std::vector<Shader> relocated; relocated.reserve(1); relocated.push_back(std::move(destination)); relocated.push_back(Build());
            Check(::GEngine::Asset::ShaderBackendAccess::Program(relocated.front()) == name, "Container relocation changed program ownership");
            const auto rejected = relocated.front().CompileShader(vertex, VERTEX, "replacement.vert");
            Check(!rejected && rejected.error().code == ShaderCreationCode::InvalidState
                && rejected.error().shaderType == VERTEX && rejected.error().source == "replacement.vert", "Linked mutation error lost its fields");
            Check(::GEngine::Asset::ShaderBackendAccess::Program(relocated.front()) == name && relocated.front().IsLinked(), "Rejected mutation damaged linked program");
            relocated.front() = std::move(source);
            Check(!glIsProgram(name) && !relocated.front().IsLinked(), "Empty assignment failed to retire live owner");
            denyAllocation = true; relocated.back().Destroy(); relocated.back().Destroy(); denyAllocation = false;
            Check(!::GEngine::Asset::ShaderBackendAccess::Program(relocated.back()) && !relocated.back().IsLinked(), "Destroy did not reset state");
        }
        o.Empty();
        {
            Shader partial; partial.CompileShader(vertex, VERTEX, "pending.vert");
            const auto name = ::GEngine::Asset::ShaderBackendAccess::Program(partial);
            denyAllocation = true;
            Shader moved(std::move(partial)); moved.Destroy(); partial.Destroy();
            denyAllocation = false;
            Check(!glIsProgram(name), "Pending-stage move/destruction leaked");
        }
        o.Empty();
        std::cout << "[PASS] compile/link/reflection/uniforms/moves/relocation/allocation-free-cleanup\n";
    }

    void Failures(Observer& o, Diagnostics& d)
    {
        CheckError(CreateShaderProgram({}), ShaderCreationCode::InvalidInput); o.Empty();
        const std::array<ShaderSource, 1> unsupported{{{static_cast<ShaderType>(0), vertex, "bad.stage"}}};
        CheckError(CreateShaderProgram(unsupported), ShaderCreationCode::InvalidInput, static_cast<ShaderType>(0), "bad.stage"); o.Empty();
        o.failProgram = true;
        CheckError(CreateShaderProgram(validSources), ShaderCreationCode::ProgramAllocation, VERTEX, "valid.vert"); o.Empty();
        o.failShader = true;
        CheckError(CreateShaderProgram(validSources), ShaderCreationCode::ShaderAllocation, VERTEX, "valid.vert"); o.Empty();
        o.failOwnership = true;
        Reject<std::bad_alloc>([&] { (void)CreateShaderProgram(validSources); }); denyAllocation = false; o.Empty();
        const std::array<ShaderSource, 2> badCompile{{{VERTEX, vertex, "ok.vert"}, {FRAGMENT, invalid, "broken.frag"}}};
        const std::array<ShaderSource, 2> badLink{{
            {VERTEX, "#version 460 core\nout vec3 mismatch;void main(){mismatch=vec3(1);gl_Position=vec4(0,0,0,1);}", "mismatch.vert"},
            {FRAGMENT, "#version 460 core\nin vec4 mismatch;out vec4 color;void main(){color=mismatch;}", "mismatch.frag"}}};
        d.compilingFailure = true;
        CheckError(CreateShaderProgram(badCompile), ShaderCreationCode::Compile, FRAGMENT, "broken.frag"); o.Empty();
        CheckError(CreateShaderProgram(badLink), ShaderCreationCode::Link); o.Empty();
        o.failLog = true; Reject<std::bad_alloc>([&] { (void)CreateShaderProgram(badCompile); }); o.Empty();
        o.failLog = true; Reject<std::bad_alloc>([&] { (void)CreateShaderProgram(badLink); }); o.Empty();
        // Failure is cleaned immediately, even when the incremental builder lives on.
        Shader incremental; incremental.CompileShader(vertex, VERTEX, "pending.vert");
        const auto rejected = incremental.CompileShader(invalid, FRAGMENT, "broken.frag");
        Check(!rejected && rejected.error().code == ShaderCreationCode::Compile && rejected.error().shaderType == FRAGMENT
            && rejected.error().source == "broken.frag" && !rejected.error().log.empty(), "Incremental failure lost shader fields");
        Check(!::GEngine::Asset::ShaderBackendAccess::Program(incremental) && !incremental.IsLinked(), "Incremental failure retained partial state"); o.Empty();
        d.compilingFailure = false;
        o.failReflection = true; Reject<std::bad_alloc>([&] { (void)CreateShaderProgram(validSources); }); o.Empty();
        incremental.CompileShader(vertex, VERTEX, "retry.vert"); incremental.CompileShader(fragment, FRAGMENT, "retry.frag");
        incremental.Link(); incremental.Destroy(); o.Empty();
        Check(!incremental.Link(), "Empty incremental link was accepted"); o.Empty();
        std::cout << "[PASS] structured compile/link/input/allocation failures, log/reflection unwind and retry\n";
    }
    void Files(Observer& o)
    {
        { std::ofstream file("fixture.vert"); file << vertex; Check(bool(file), "Cannot write vertex fixture"); }
        { std::ofstream file("fixture.frag"); file << fragment; Check(bool(file), "Cannot write fragment fixture"); }
        const std::array<std::string, 2> paths{{"fixture.vert", "fixture.frag"}};
        { auto result = CreateShaderProgramFromFiles(paths); Check(result.has_value(), "File creation failed"); }
        o.Empty();
        const std::array<std::string, 2> missing{{"fixture.vert", "does-not-exist.frag"}};
        CheckError(CreateShaderProgramFromFiles(missing), ShaderCreationCode::FileRead, FRAGMENT, "does-not-exist.frag"); o.Empty();
        const std::array<std::string, 1> unknown{{"unsupported.extension"}};
        CheckError(CreateShaderProgramFromFiles(unknown), ShaderCreationCode::InvalidInput, {}, "unsupported.extension"); o.Empty();
        std::cout << "[PASS] file creation, missing source and unsupported extension\n";
    }
    void APIENTRY ForbiddenDelete(GLuint) { std::_Exit(87); }
}

int main(int argc, char**)
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
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Phase 24 Shader validation",
                0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), &SDL_DestroyWindow);
            Check(window != nullptr, SDL_GetError());
            std::unique_ptr<void, decltype(&SDL_GL_DeleteContext)> context(GLDebug::CreateContext(window.get()), &SDL_GL_DeleteContext);
            Check(context != nullptr && gladLoadGLLoader(SDL_GL_GetProcAddress), "Context/GL loader failed");
            if (argc == 2)
            {
                auto shader = Build(); glad_glDeleteProgram = ForbiddenDelete;
                std::thread worker([&] { std::set_terminate([] { std::_Exit(86); }); shader.Destroy(); std::_Exit(89); });
                worker.join(); return 89;
            }
            std::cout << "[GL] cycle=" << cycle << " version=" << glGetString(GL_VERSION) << " renderer=" << glGetString(GL_RENDERER) << '\n';
            Diagnostics diagnostics;
            {
                Observer observer; SuccessAndMoves(observer); Failures(observer, diagnostics); Files(observer); observer.Empty();
                std::cout << "[PASS] shader/program created=" << observer.created << " deleted=" << observer.deleted
                    << " counters=" << GENGINE_RENDER_COUNTERS << '\n';
            }
            Check(!diagnostics.errors && diagnostics.markers == 1 && glGetError() == GL_NO_ERROR, "Unexpected GL diagnostics/errors");
            std::cout << "[INFO] expected-compiler-messages=" << diagnostics.expectedCompilerMessages << '\n';
            RenderCounters::ForgetContext(context.get());
        }
        std::cout << "[PASS] shader-program-RAII cycles=2 checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error) { denyAllocation = false; std::cerr << "[FAIL] " << error.what() << '\n'; return 1; }
}
