#include "Shapes/PointLightSegments.h"
#include <type_traits>
static_assert(std::is_same_v<decltype(GEngine::Shape::PointLightSegments::Create()),
    std::expected<GEngine::Shape::PointLightSegments, GEngine::Shape::PointLightSegmentError>>);
static_assert(!std::is_constructible_v<GEngine::Shape::PointLightSegments, int, int>);
static_assert(GEngine::Shape::PointLightSegments{}.Radius() == 32);
#ifndef POINT_LIGHT_SEGMENTS_SCHEMA_ONLY
#include "gepch.h"
#include "Core/GLDebug.h"
#include "Shapes/PointLightHelper.h"
#include <unordered_set>
#include <stdexcept>
#include <algorithm>

namespace
{
    void Require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
    // Observe every real driver deletion, including duplicate names ignored by GL.
    struct Ownership
    {
        inline static PFNGLGENBUFFERSPROC genBuffers;
        inline static PFNGLDELETEBUFFERSPROC deleteBuffers;
        inline static PFNGLGENVERTEXARRAYSPROC genArrays;
        inline static PFNGLDELETEVERTEXARRAYSPROC deleteArrays;
        inline static std::unordered_set<GLuint> buffers, arrays;
        inline static unsigned generated, deleted, deletionCalls;
        inline static bool valid;
        inline static SDL_GLContext context;
        inline static SDL_threadID thread;
        static void CheckThread()
        { valid &= SDL_GL_GetCurrentContext() == context && SDL_ThreadID() == thread; }
        static void Add(std::unordered_set<GLuint>& live, GLsizei n, const GLuint* ids)
        {
            CheckThread();
            for (GLsizei i = 0; i < n; ++i) { valid &= ids[i] != 0 && live.insert(ids[i]).second; ++generated; }
        }
        static void Remove(std::unordered_set<GLuint>& live, GLsizei n, const GLuint* ids)
        {
            CheckThread(); ++deletionCalls;
            for (GLsizei i = 0; i < n; ++i) { valid &= ids[i] != 0 && live.erase(ids[i]) == 1; ++deleted; }
        }
        static void APIENTRY GenBuffers(GLsizei n, GLuint* ids) { genBuffers(n, ids); Add(buffers, n, ids); }
        static void APIENTRY DeleteBuffers(GLsizei n, const GLuint* ids) { Remove(buffers, n, ids); deleteBuffers(n, ids); }
        static void APIENTRY GenArrays(GLsizei n, GLuint* ids) { genArrays(n, ids); Add(arrays, n, ids); }
        static void APIENTRY DeleteArrays(GLsizei n, const GLuint* ids) { Remove(arrays, n, ids); deleteArrays(n, ids); }
        Ownership()
        {
            buffers.clear(); arrays.clear(); generated = deleted = deletionCalls = 0; valid = true;
            context = SDL_GL_GetCurrentContext(); thread = SDL_ThreadID();
            genBuffers = glad_glGenBuffers; deleteBuffers = glad_glDeleteBuffers;
            genArrays = glad_glGenVertexArrays; deleteArrays = glad_glDeleteVertexArrays;
            glad_glGenBuffers = GenBuffers; glad_glDeleteBuffers = DeleteBuffers;
            glad_glGenVertexArrays = GenArrays; glad_glDeleteVertexArrays = DeleteArrays;
        }
        ~Ownership()
        {
            glad_glGenBuffers = genBuffers; glad_glDeleteBuffers = deleteBuffers;
            glad_glGenVertexArrays = genArrays; glad_glDeleteVertexArrays = deleteArrays;
        }
        void Verify()
        {
            Require(valid && buffers.empty() && arrays.empty() && generated == deleted,
                "Resource leaked, deleted twice/zero, or retired on wrong thread/context");
            const auto counters = GEngine::RenderCounters::Current();
            Require(counters.liveNames[0] == 0 && counters.liveNames[1] == 0 && counters.estimatedBufferBytes == 0,
                "Production resource counters did not return to zero");
            std::cout << "[PASS] exactly-once resources=" << generated << " retired=" << deleted
                << " counters=" << GENGINE_RENDER_COUNTERS << '\n';
        }
    };


    using namespace GEngine::Shape;
    void Conversion()
    {
        auto defaults = PointLightSegments::Create();
        Require(defaults && defaults->Radius() == 32 && defaults->Height() == 32, "Default changed");
        for (float value : {0.f, -0.f, 1.9f, -1.9f, 32.f,
            static_cast<float>((std::numeric_limits<int>::min)()),
            std::nextafter(static_cast<float>((std::numeric_limits<int>::max)()), 0.f)})
        {
            auto result = PointLightSegments::Create(value, value);
            Require(result && result->Radius() == static_cast<int>(value)
                && result->Height() == static_cast<int>(value), "Representable domain/truncation changed");
        }
        for (float value : {std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
            static_cast<float>((std::numeric_limits<int>::max)()),
            std::nextafter(static_cast<float>((std::numeric_limits<int>::min)()),
                -std::numeric_limits<float>::infinity())})
        {
            for (auto axis : {PointLightSegmentAxis::Radius, PointLightSegmentAxis::Height})
            {
                auto result = axis == PointLightSegmentAxis::Radius
                    ? PointLightSegments::Create(value, 32.f) : PointLightSegments::Create(32.f, value);
                Require(!result, "Invalid input accepted");
                const auto& e = result.error();
                Require(e.code == PointLightSegmentErrorCode::NotRepresentable && e.axis == axis
                    && (std::isnan(value) ? std::isnan(e.value) : value == e.value)
                    && e.operation == "PointLightSegments::Create"
                    && e.message == "Sphere segment count is not representable as int", "Lost error detail");
            }
        }
        std::cout << "[PASS] segment-conversion/defaults/truncation/int-edges/diagnostics/no-context\n";
    }
    void Geometry()
    {
        static_assert(!std::is_constructible_v<PointLightHelper, float, float, float>);
        static_assert(!std::is_copy_constructible_v<PointLightHelper> && !std::is_move_constructible_v<PointLightHelper>);
        for (const auto counts : {std::pair{32.f,32.f}, std::pair{8.9f,6.4f}})
        {
            auto segments = PointLightSegments::Create(counts.first, counts.second);
            Require(segments.has_value(), "Valid counts rejected");
            PointLightHelper actual(0.5f, *segments);
            Sphere expected(0.5f, static_cast<int>(counts.first), static_cast<int>(counts.second));
            auto a = actual.ExportCpuMesh(); auto e = expected.ExportCpuMesh();
            Require(a && e && a->VertexCount() == e->VertexCount()
                && a->VertexCount() == 6 * segments->Radius() * segments->Height()
                && std::ranges::equal(a->Vertices(), e->Vertices()), "Helper geometry changed");
            Require(actual.GetUniquePoints() == expected.GetUniquePoints(), "Bounds/unique geometry changed");
        }
        PointLightHelper defaults;
        Require(defaults.GetVerticesCount() == 6 * 32 * 32, "Default registry construction changed");
    }
}
int main()
{
    try
    {
        // Test every rejected conversion before any SDL/context/GL initialization.
        Conversion();
        SDL_SetMainReady();
        struct Video { ~Video() { SDL_Quit(); } } video;
        Require(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
        Require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) == 0
            && SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6) == 0
            && SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) == 0,
            "Cannot configure GL");
        GEngine::GLDebug::ConfigureContext();
        for (int cycle = 0; cycle < 2; ++cycle)
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("PointLight segments", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), &SDL_DestroyWindow);
            Require(window != nullptr, SDL_GetError());
            std::unique_ptr<void, decltype(&SDL_GL_DeleteContext)> context(
                GEngine::GLDebug::CreateContext(window.get()), &SDL_GL_DeleteContext);
            Require(context != nullptr, SDL_GetError());
            Require(gladLoadGLLoader(SDL_GL_GetProcAddress) != 0, "GL loading failed");
            Ownership ownership;
            Geometry();
            ownership.Verify();
            Require(glGetError() == GL_NO_ERROR, "Unexpected GL error");
            GEngine::RenderCounters::ForgetContext(context.get());
        }
        std::cout << "[PASS] helper-payload/unique-points/defaults/ownership cycles=2\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "[FAIL] " << error.what() << '\n'; return 1; }
}
#endif
