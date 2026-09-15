#include "gepch.h"
#include "Core/SimpleRenderer.h"
#include "Core/RayTracingScene.h"
#include "Camera/RayTracingCamera.h"
#include "Core/GLDebug.h"
#include <array>
#include <atomic>
#include <stdexcept>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

namespace
{
    using namespace GEngine;
    int checks = 0;
    void Require(bool condition, const char* message)
    {
        ++checks;
        if (!condition) throw std::runtime_error(message);
    }
    static_assert(!std::is_copy_constructible_v<SimpleRenderer>);
    static_assert(!std::is_copy_assignable_v<SimpleRenderer>);
    static_assert(std::is_nothrow_move_constructible_v<SimpleRenderer>);
    static_assert(std::is_nothrow_move_assignable_v<SimpleRenderer>);

    struct GLCalls
    {
        inline static SDL_threadID owner;
        inline static std::atomic<unsigned> wrongThread{0}, allocations{0}, updates{0}, deletes{0};
        inline static PFNGLBINDTEXTUREPROC bind;
        inline static PFNGLGENTEXTURESPROC generate;
        inline static PFNGLDELETETEXTURESPROC destroy;
        inline static PFNGLTEXIMAGE2DPROC allocate;
        inline static PFNGLTEXSUBIMAGE2DPROC update;
        static void Check() { if (SDL_ThreadID() != owner) ++wrongThread; }
        static void APIENTRY Bind(GLenum target, GLuint texture) { Check(); bind(target, texture); }
        static void APIENTRY Generate(GLsizei count, GLuint* names) { Check(); generate(count, names); }
        static void APIENTRY Delete(GLsizei count, const GLuint* names) { Check(); deletes += count; destroy(count, names); }
        static void APIENTRY Allocate(GLenum target, GLint level, GLint format, GLsizei width,
            GLsizei height, GLint border, GLenum source, GLenum type, const void* data)
        { Check(); ++allocations; allocate(target, level, format, width, height, border, source, type, data); }
        static void APIENTRY Update(GLenum target, GLint level, GLint x, GLint y, GLsizei width,
            GLsizei height, GLenum format, GLenum type, const void* data)
        { Check(); ++updates; update(target, level, x, y, width, height, format, type, data); }
        GLCalls()
        {
            owner = SDL_ThreadID(); wrongThread = allocations = updates = deletes = 0;
            bind = glad_glBindTexture; generate = glad_glGenTextures; destroy = glad_glDeleteTextures;
            allocate = glad_glTexImage2D; update = glad_glTexSubImage2D;
            glad_glBindTexture = Bind; glad_glGenTextures = Generate; glad_glDeleteTextures = Delete;
            glad_glTexImage2D = Allocate; glad_glTexSubImage2D = Update;
        }
        ~GLCalls()
        {
            glad_glBindTexture = bind; glad_glGenTextures = generate; glad_glDeleteTextures = destroy;
            glad_glTexImage2D = allocate; glad_glTexSubImage2D = update;
        }
    };

    RayTracingScene Scene()
    {
        RayTracingScene scene;
        scene.Materials = {{{0.9f, 0.1f, 0.3f}, 0.8f, 0.0f}, {{0.2f, 0.7f, 0.9f}, 0.65f, 0.0f}};
        scene.Spheres = {{{0, 0, 0}, 1.5f, 0}, {{0, -102, 0}, 100.0f, 1}, {{2, 0, -1}, 0.8f, 1}};
        return scene;
    }

    std::vector<uint8_t> Read(const SimpleRenderer& renderer)
    {
        const auto& image = renderer.GetFinalImage();
        Require(image && image->GetTexID(), "No uploaded final image");
        glBindTexture(GL_TEXTURE_2D, image->GetTexID());
        GLint width = 0, height = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        Require(width == image->GetWidth() && height == image->GetHeight(), "Texture storage size differs from image");
        std::vector<uint8_t> bytes(static_cast<size_t>(width) * height * 4);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
        return bytes;
    }

    uint64_t Checksum(const std::vector<uint8_t>& bytes)
    {
        uint64_t hash = UINT64_C(14695981039346656037);
        for (auto byte : bytes) hash = (hash ^ byte) * UINT64_C(1099511628211);
        return hash;
    }

    void Configure(SimpleRenderer& renderer, RayTracingCamera& camera, uint32_t width = 96, uint32_t height = 64)
    {
        renderer.GetSettings().Acculmate = true;
        renderer.GetSettings().Seed = 1234567;
        renderer.GetBounces() = 6;
        renderer.OnResize(width, height);
        camera.OnResize(width, height);
    }

    void Determinism(const RayTracingScene& scene)
    {
        std::array<std::vector<uint8_t>, 4> reference;
        for (int workers : {1, 2, 3, 8}) for (int repeat = 0; repeat < 3; ++repeat) {
            SimpleRenderer renderer;
            RayTracingCamera camera(45.0f, 0.1f, 100.0f);
            Configure(renderer, camera);
            renderer.GetNumOfThread() = workers;
            for (int frame = 0; frame < 4; ++frame) {
                renderer.Render(scene, camera);
                auto bytes = Read(renderer);
                if (workers == 1 && repeat == 0) reference[frame] = bytes;
                Require(bytes == reference[frame], "Serial/parallel/repeated sample output differs");
                std::cout << "[CHECKSUM] workers=" << workers << " repeat=" << repeat << " sample=" << frame + 1
                    << " value=" << Checksum(bytes) << '\n';
            }
            renderer.ResetFrameIndex();
            renderer.Render(scene, camera);
            Require(Read(renderer) == reference[0], "Reset did not restart the deterministic sample stream");
            renderer.Render(scene, camera);
            renderer.GetSettings().Acculmate = false;
            for (int i = 0; i < 3; ++i) {
                renderer.Render(scene, camera);
                Require(Read(renderer) == reference[0], "Disabled accumulation retained old samples");
            }
            renderer.GetSettings().Seed++;
            renderer.Render(scene, camera);
            Require(Read(renderer) != reference[0], "Changing seed did not change stochastic output");
        }
        Require(reference[0] != reference[1] && reference[1] != reference[3], "Samples do not advance");
        bool varied = false;
        for (size_t i = 0; i < reference[0].size(); i += 4) {
            Require(reference[0][i + 3] == 255, "A pixel was skipped or alpha was corrupted");
            varied |= reference[0][i] != reference[0][0];
        }
        Require(varied, "Checksum scene did not exercise varied hit/miss pixels");
        std::cout << "[PASS] serial/parallel/repeat/worker-count/reset/seed checksums\n";
    }

    void ResizeAndClose(const RayTracingScene& scene)
    {
        GLuint unrelated = 0;
        glGenTextures(1, &unrelated);
        glBindTexture(GL_TEXTURE_2D, unrelated);
        const std::array<uint8_t, 24> guard{11, 12, 13, 255, 21, 22, 23, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, guard.data());
        for (int repeat = 0; repeat < 12; ++repeat) {
            GLuint finalTexture = 0;
            {
                SimpleRenderer renderer;
                RayTracingCamera camera(45.0f, 0.1f, 100.0f);
                renderer.RenderBegin(); renderer.Render(scene, camera); // Before any resize.
                for (const auto size : std::array<std::array<uint32_t, 2>, 5>{{{31, 17}, {1, 1}, {0, 0}, {19, 0}, {23, 13}}}) {
                    Configure(renderer, camera, size[0], size[1]);
                    renderer.GetNumOfThread() = repeat % 2 ? 3 : 1;
                    renderer.OnResize(size[0], size[1]); // Repeated resize must retain pending allocation.
                    glBindTexture(GL_TEXTURE_2D, unrelated);
                    const unsigned before = GLCalls::allocations;
                    renderer.RenderBegin(); renderer.Render(scene, camera);
                    if (size[0] == 0 || size[1] == 0) {
                        Require(!renderer.GetFinalImage() && camera.GetRayDirections().empty(), "Zero size did not suspend/release storage");
                        Require(GLCalls::allocations == before, "Zero size allocated a texture");
                        continue;
                    }
                    Require(GLCalls::allocations == before + 1, "First/resized frame did not allocate exactly once");
                    auto first = Read(renderer);
                    SimpleRenderer fresh;
                    Configure(fresh, camera, size[0], size[1]);
                    fresh.GetNumOfThread() = 1;
                    fresh.Render(scene, camera);
                    Require(first == Read(fresh), "Resize retained old accumulation");
                    const unsigned uploads = GLCalls::updates;
                    glBindTexture(GL_TEXTURE_2D, unrelated);
                    renderer.Render(scene, camera); // No OnResize call required between frames.
                    Require(GLCalls::updates == uploads + 1, "Second frame did not update existing texture");
                    glBindTexture(GL_TEXTURE_2D, unrelated);
                    std::array<uint8_t, 24> actual{};
                    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
                    Require(actual == guard, "Ray upload overwrote another bound texture");
                }
                finalTexture = renderer.GetFinalImage()->GetTexID();
                SimpleRenderer moved(std::move(renderer));
                Require(!renderer.GetFinalImage() && moved.GetFinalImage()->GetTexID() == finalTexture, "Move duplicated image ownership");
                renderer.OnResize(0, 0);
                renderer = std::move(moved);
                Require(!moved.GetFinalImage(), "Move assignment did not transfer image");
            }
            Require(glIsTexture(finalTexture) == GL_FALSE, "Final texture survived renderer destruction");
        }
        glDeleteTextures(1, &unrelated);
        // Same pixel count with different dimensions is still an invalid camera.
        SimpleRenderer renderer;
        RayTracingCamera camera(45.0f, 0.1f, 100.0f);
        Configure(renderer, camera, 8, 4);
        camera.OnResize(4, 8);
        bool rejected = false;
        try { renderer.Render(scene, camera); } catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "Camera/image dimension mismatch was accepted");
        rejected = false;
        try { renderer.OnResize(UINT32_MAX, UINT32_MAX); } catch (const std::length_error&) { rejected = true; }
        Require(rejected, "Overflowing/unsupported dimensions were accepted");
        camera.OnResize(8, 4);
        renderer.Render(scene, camera);
        Require(Read(renderer).size() == 8 * 4 * 4, "Rejected resize damaged previous storage");
        std::cout << "[PASS] resize/zero/restore/pending-upload/binding/move/close stress\n";
    }

    void StorageLifetime(const RayTracingScene& scene)
    {
#ifdef _DEBUG
        RayTracingCamera camera(45.0f, 0.1f, 100.0f);
        camera.OnResize(37, 29);
        const auto churn = [&] {
            for (int i = 0; i < 24; ++i) {
                SimpleRenderer renderer;
                renderer.GetNumOfThread() = 1;
                renderer.OnResize(37, 29);
                renderer.Render(scene, camera);
                renderer.ResetFrameIndex();
                renderer.Render(scene, camera);
            }
        };
        churn(); // Warm driver/logging state before exact application CRT comparison.
        _CrtMemState before{}, after{};
        _CrtMemCheckpoint(&before);
        churn();
        _CrtMemCheckpoint(&after);
        Require(before.lCounts[_NORMAL_BLOCK] == after.lCounts[_NORMAL_BLOCK]
            && before.lSizes[_NORMAL_BLOCK] == after.lSizes[_NORMAL_BLOCK], "Renderer CPU storage leaked across 24 lifetimes");
        std::cout << "[PASS] debug-CRT storage lifetime: zero retained blocks/bytes\n";
#endif
    }

    struct Diagnostics
    {
        unsigned errors = 0, markers = 0;
        static void APIENTRY Receive(GLenum source, GLenum type, GLuint id, GLenum severity,
            GLsizei length, const GLchar* message, const void* user) noexcept
        {
            auto& self = *static_cast<Diagnostics*>(const_cast<void*>(user));
            if (source == GL_DEBUG_SOURCE_APPLICATION && type == GL_DEBUG_TYPE_MARKER && id == 120001) ++self.markers;
            else if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH || severity == GL_DEBUG_SEVERITY_MEDIUM) {
                ++self.errors; std::fprintf(stderr, "[GL] %.*s\n", length, message);
            }
        }
        Diagnostics()
        {
            glEnable(GL_DEBUG_OUTPUT); glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(Receive, this);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 120001,
                GL_DEBUG_SEVERITY_NOTIFICATION, -1, "Phase 12 diagnostic health");
        }
        ~Diagnostics() { glDebugMessageCallback(nullptr, nullptr); }
    };
}

int main(int argc, char** argv)
{
    try {
#ifdef __SANITIZE_ADDRESS__
        std::cout << "[SANITIZER] AddressSanitizer=on\n";
        if (argc == 2 && std::string_view(argv[1]) == "--asan-failure-probe") {
            volatile int index = 4;
            volatile int* data = new int[4];
            data[index] = 123; // Isolated negative control; never executed in the normal suite.
            delete[] data;
            return 1;
        }
#else
        std::cout << "[SANITIZER] AddressSanitizer=off\n";
#endif
        Require(argc == 1, "Unknown argument");
        GEngine::Log::Initialize();
        GEngine::Log::GetCoreLogger()->set_level(spdlog::level::off);
        SDL_SetMainReady();
        struct Video { ~Video() { SDL_Quit(); } } video;
        Require(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
        Require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) == 0
            && SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6) == 0
            && SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) == 0,
            "Cannot configure GL 4.6 core");
        GEngine::GLDebug::ConfigureContext();
        for (int cycle = 0; cycle < 2; ++cycle) {
            {
                std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                    SDL_CreateWindow("Ray tracer validation", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), &SDL_DestroyWindow);
                Require(window != nullptr, SDL_GetError());
                std::unique_ptr<void, decltype(&SDL_GL_DeleteContext)> context(
                    GEngine::GLDebug::CreateContext(window.get()), &SDL_GL_DeleteContext);
                Require(context != nullptr, SDL_GetError());
                Require(gladLoadGLLoader(SDL_GL_GetProcAddress) != 0, "GL loading failed");
                std::cout << "[GL] cycle=" << cycle << " version=" << glGetString(GL_VERSION)
                    << " renderer=" << glGetString(GL_RENDERER) << '\n';
                Diagnostics diagnostics;
                GLCalls calls;
                auto scene = Scene();
                Determinism(scene);
                ResizeAndClose(scene);
                StorageLifetime(scene);
                Require(GLCalls::wrongThread == 0, "Texture GL work escaped the context thread");
                Require(diagnostics.errors == 0 && diagnostics.markers == 1 && glGetError() == GL_NO_ERROR,
                    "GL error or diagnostic failure");
                GEngine::RenderCounters::ForgetContext(context.get());
            }
            Require(SDL_GL_GetCurrentContext() == nullptr, "Context teardown failed");
        }
        std::cout << "[PASS] ray-tracing cycles=2 checks=" << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
