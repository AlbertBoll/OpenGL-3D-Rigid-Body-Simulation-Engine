#ifdef IMAGE_OWNER_SCHEMA_ONLY
#include "Core/Image.h"
#include "UI/FramebufferImage.h"
#include <type_traits>
static_assert(std::same_as<decltype(GEngine::UI::RasterImage(std::declval<const GEngine::Image&>(),1.f,1.f)),GEngine::ImageResult>);
template<class T> concept ExposesTextureName=requires(const T& image){image.GetTexID();};
static_assert(!std::is_copy_constructible_v<GEngine::Image>);
static_assert(!std::is_copy_assignable_v<GEngine::Image>);
static_assert(std::is_nothrow_move_constructible_v<GEngine::Image>);
static_assert(std::is_nothrow_move_assignable_v<GEngine::Image>);
static_assert(!ExposesTextureName<GEngine::Image>);
#else
#ifdef RAY_RENDERER_SCHEMA_ONLY
#include "Core/SimpleRenderer.h"
#include <type_traits>
static_assert(std::same_as<decltype(std::declval<GEngine::SimpleRenderer&>().OnResize(1,1)),GEngine::ImageResult>);
static_assert(std::same_as<decltype(std::declval<GEngine::SimpleRenderer&>().Render(std::declval<const GEngine::RayTracingScene&>(),std::declval<const GEngine::RayTracingCamera&>())),GEngine::ImageResult>);
#else
#include "gepch.h"
#include "Core/SimpleRenderer.h"
#include "Core/RayTracingScene.h"
#include "Camera/RayTracingCamera.h"
#include "Core/GLDebug.h"
#include "UI/FramebufferImage.h"
#include <array>
#include <set>
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
    void RayChecked(ImageResult result) { Require(bool(result), "Unexpected typed Ray failure"); }
    static_assert(!std::is_copy_constructible_v<SimpleRenderer>);
    static_assert(!std::is_copy_assignable_v<SimpleRenderer>);
    static_assert(std::is_nothrow_move_constructible_v<SimpleRenderer>);
    static_assert(std::is_nothrow_move_assignable_v<SimpleRenderer>);

    struct ImageNameTag { using type=uint32_t Image::*;friend type ImageNameMember(ImageNameTag); };
    template<class Tag,typename Tag::type Member> struct RevealImageMember {
        friend typename Tag::type ImageNameMember(Tag) { return Member; }
    };
    template struct RevealImageMember<ImageNameTag,&Image::m_TexID>;
    uint32_t ImageName(const Image& image) { return image.*ImageNameMember(ImageNameTag{}); }

    struct GLCalls
    {
        inline static SDL_threadID owner;
        inline static std::atomic<unsigned> wrongThread{0}, allocations{0}, updates{0}, deletes{0};
        inline static std::set<GLuint> live;
        inline static unsigned generated{},retired{};
        inline static bool failAllocation{},failGeneration{},failUpdate{};
        inline static GLenum pendingError=GL_NO_ERROR;
        inline static PFNGLGETERRORPROC getError;
        static GLenum APIENTRY Error() {if(pendingError!=GL_NO_ERROR)return std::exchange(pendingError,GL_NO_ERROR);return getError();}
        inline static PFNGLBINDTEXTUREPROC bind;
        inline static PFNGLGENTEXTURESPROC generate;
        inline static PFNGLDELETETEXTURESPROC destroy;
        inline static PFNGLTEXIMAGE2DPROC allocate;
        inline static PFNGLTEXSUBIMAGE2DPROC update;
        static void Check() { if (SDL_ThreadID() != owner) ++wrongThread; }
        static void APIENTRY Bind(GLenum target, GLuint texture) { Check(); bind(target, texture); }
        static void APIENTRY Generate(GLsizei count, GLuint* names) {
            Check();if(failGeneration){failGeneration=false;std::fill_n(names,count,0);return;}
            generate(count,names);for(int i=0;i<count;++i)if(names[i]){Require(live.insert(names[i]).second,"Texture generated twice");++generated;}
        }
        static void APIENTRY Delete(GLsizei count, const GLuint* names) {
            Check();deletes+=count;for(int i=0;i<count;++i)if(names[i]){Require(live.erase(names[i])==1,"Texture retired without unique owner");++retired;}destroy(count,names);
        }
        static void APIENTRY Allocate(GLenum target, GLint level, GLint format, GLsizei width,
            GLsizei height, GLint border, GLenum source, GLenum type, const void* data)
        { Check(); ++allocations; if(failAllocation){failAllocation=false;pendingError=GL_OUT_OF_MEMORY;return;} allocate(target, level, format, width, height, border, source, type, data); }
        static void APIENTRY Update(GLenum target, GLint level, GLint x, GLint y, GLsizei width,
            GLsizei height, GLenum format, GLenum type, const void* data)
        { Check(); ++updates; if(failUpdate){failUpdate=false;pendingError=GL_OUT_OF_MEMORY;return;} update(target, level, x, y, width, height, format, type, data); }
        GLCalls()
        {
            owner = SDL_ThreadID(); wrongThread = allocations = updates = deletes = 0;
            Require(live.empty(),"Prior context texture survived");generated=retired=0;getError=glad_glGetError;glad_glGetError=Error;
            bind = glad_glBindTexture; generate = glad_glGenTextures; destroy = glad_glDeleteTextures;
            allocate = glad_glTexImage2D; update = glad_glTexSubImage2D;
            glad_glBindTexture = Bind; glad_glGenTextures = Generate; glad_glDeleteTextures = Delete;
            glad_glTexImage2D = Allocate; glad_glTexSubImage2D = Update;
        }
        ~GLCalls()
        {
            Require(live.empty() && generated==retired,"Image texture ownership did not drain");
            std::cout<<"[PASS] texture owners generated="<<generated<<" retired="<<retired<<" live=0\n";
            glad_glGetError=getError;
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
        Require(image && ImageName(*image), "No uploaded final image");
        glBindTexture(GL_TEXTURE_2D, ImageName(*image));
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
        RayChecked(renderer.OnResize(width, height));
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
                RayChecked(renderer.Render(scene, camera));
                auto bytes = Read(renderer);
                if (workers == 1 && repeat == 0) reference[frame] = bytes;
                Require(bytes == reference[frame], "Serial/parallel/repeated sample output differs");
                std::cout << "[CHECKSUM] workers=" << workers << " repeat=" << repeat << " sample=" << frame + 1
                    << " value=" << Checksum(bytes) << '\n';
            }
            renderer.ResetFrameIndex();
            RayChecked(renderer.Render(scene, camera));
            Require(Read(renderer) == reference[0], "Reset did not restart the deterministic sample stream");
            RayChecked(renderer.Render(scene, camera));
            renderer.GetSettings().Acculmate = false;
            for (int i = 0; i < 3; ++i) {
                RayChecked(renderer.Render(scene, camera));
                Require(Read(renderer) == reference[0], "Disabled accumulation retained old samples");
            }
            renderer.GetSettings().Seed++;
            RayChecked(renderer.Render(scene, camera));
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
                renderer.RenderBegin(); RayChecked(renderer.Render(scene, camera)); // Before any resize.
                for (const auto size : std::array<std::array<uint32_t, 2>, 5>{{{31, 17}, {1, 1}, {0, 0}, {19, 0}, {23, 13}}}) {
                    Configure(renderer, camera, size[0], size[1]);
                    renderer.GetNumOfThread() = repeat % 2 ? 3 : 1;
                    RayChecked(renderer.OnResize(size[0], size[1])); // Repeated resize must retain pending allocation.
                    glBindTexture(GL_TEXTURE_2D, unrelated);
                    const unsigned before = GLCalls::allocations;
                    renderer.RenderBegin(); RayChecked(renderer.Render(scene, camera));
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
                    RayChecked(fresh.Render(scene, camera));
                    Require(first == Read(fresh), "Resize retained old accumulation");
                    const unsigned uploads = GLCalls::updates;
                    glBindTexture(GL_TEXTURE_2D, unrelated);
                    RayChecked(renderer.Render(scene, camera)); // No OnResize call required between frames.
                    Require(GLCalls::updates == uploads + 1, "Second frame did not update existing texture");
                    glBindTexture(GL_TEXTURE_2D, unrelated);
                    std::array<uint8_t, 24> actual{};
                    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
                    Require(actual == guard, "Ray upload overwrote another bound texture");
                }
                finalTexture = ImageName(*renderer.GetFinalImage());
                SimpleRenderer moved(std::move(renderer));
                Require(!renderer.GetFinalImage() && ImageName(*moved.GetFinalImage()) == finalTexture, "Move duplicated image ownership");
                RayChecked(renderer.OnResize(0, 0));
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
        const auto allocationsBefore=GLCalls::allocations.load(), updatesBefore=GLCalls::updates.load();
        const auto mismatch=renderer.Render(scene,camera);
        Require(!mismatch,"Camera/image dimension mismatch was accepted");
        const auto copied=mismatch;auto moved=std::move(copied);
        const auto& error=moved.error();
        Require(error.code==ImageErrorCode::CameraMismatch && error.operation=="SimpleRenderer::Render"
            && error.message=="Ray camera must be resized before rendering" && error.width==8 && error.height==4
            && error.sourceWidth==4 && error.sourceHeight==8 && error.expectedElements==32
            && error.actualElements==32 && error.format==ImageFormat::RGBA && error.backendCode==0,
            "Camera mismatch diagnostic lost fields");
        const auto overflow=renderer.OnResize(UINT32_MAX,UINT32_MAX);
        Require(!overflow,"Overflowing/unsupported dimensions were accepted");
        const auto& extent=overflow.error();
        Require(extent.code==ImageErrorCode::InvalidExtent && extent.operation=="SimpleRenderer::OnResize"
            && extent.message=="Ray image dimensions exceed supported storage" && extent.width==UINT32_MAX
            && extent.height==UINT32_MAX && extent.sourceWidth==8 && extent.sourceHeight==4
            && extent.actualElements==uint64_t(UINT32_MAX)*UINT32_MAX && extent.expectedElements>0
            && extent.format==ImageFormat::RGBA && extent.backendCode==0,"Extent diagnostic lost fields");
        Require(GLCalls::allocations==allocationsBefore && GLCalls::updates==updatesBefore,
            "Rejected input allocated or uploaded GPU storage");
        std::cout<<"[PASS] typed extent/camera errors complete; no rejected upload; previous storage retained\n";
        camera.OnResize(8, 4);
        RayChecked(renderer.Render(scene, camera));
        Require(Read(renderer).size() == 8 * 4 * 4, "Rejected resize damaged previous storage");
        std::cout << "[PASS] resize/zero/restore/pending-upload/binding/move/close stress\n";
    }

    void ImageOwnership()
    {
        const std::array<uint8_t,16> pixels{31,63,127,255,255,0,0,255,0,255,0,255,0,0,255,255};
        auto invalid=Image::Create(0,2,ImageFormat::RGBA);Require(!invalid && invalid.error().code==ImageErrorCode::InvalidExtent,"Zero Image extent accepted");
        auto format=Image::Create(2,2,ImageFormat::None);Require(!format && format.error().code==ImageErrorCode::InvalidFormat,"Invalid Image format accepted");
        for(auto kind:{ImageFormat::RGBA,ImageFormat::RGBA32F}) {
            auto created=Image::Create(2,2,kind,pixels);Require(bool(created),"Typed Image creation");
            Image first=std::move(*created);const GLuint name=ImageName(first);
            Require(name && !ImageName(*created),"Image move duplicated texture");
            auto badUI=UI::RasterImage(first,0,4);
            Require(!badUI && badUI.error().code==ImageErrorCode::InvalidExtent && badUI.error().operation=="UI::RasterImage","Invalid UI size accepted");
            auto noUI=UI::RasterImage(first,4,4);
            Require(!noUI && noUI.error().code==ImageErrorCode::Context,"Image presentation accepted no active UI frame");
            auto empty=Image::Create(2,2,kind);Require(bool(empty),"Metadata-only Image creation");
            auto unavailable=UI::RasterImage(*empty,4,4);
            Require(!unavailable && unavailable.error().code==ImageErrorCode::Unavailable,"Unallocated image presented");
            const auto before=GLCalls::allocations.load();
            auto shortData=first.UpdateData(std::span(pixels).first(4));
            Require(!shortData && shortData.error().code==ImageErrorCode::InvalidData && shortData.error().expectedElements==16
                && shortData.error().actualElements==4 && GLCalls::allocations==before,"Short Image bytes not rejected completely");
            auto read=[&] {std::array<uint8_t,16> actual{};glBindTexture(GL_TEXTURE_2D,ImageName(first));glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());return actual;};
            Require(read()==pixels,"Image upload changed existing byte-source format semantics");
            GLCalls::failAllocation=true;auto failed=first.SetData(pixels);
            Require(!failed && failed.error().code==ImageErrorCode::Backend && failed.error().backendCode==GL_OUT_OF_MEMORY
                && failed.error().operation=="Image::SetData" && failed.error().sourceWidth==2 && failed.error().sourceHeight==2
                && failed.error().expectedElements==16 && failed.error().actualElements==16 && ImageName(first)==name && read()==pixels,
                "Failed allocation did not retain prior texture/data and complete error");
            GLCalls::failGeneration=true;auto noName=first.SetData(pixels);
            Require(!noName && noName.error().code==ImageErrorCode::Allocation && ImageName(first)==name,"Missing native allocation lost owner");
            GLCalls::failUpdate=true;auto update=first.UpdateData(pixels);
            Require(!update && update.error().code==ImageErrorCode::Backend && update.error().operation=="Image::UpdateData"
                && update.error().backendCode==GL_OUT_OF_MEMORY && read()==pixels,"Failed update lost error/pixels");
            auto* window=SDL_GL_GetCurrentWindow();auto context=SDL_GL_GetCurrentContext();
            Require(SDL_GL_MakeCurrent(window,nullptr)==0,"Detach Image context");auto detached=first.UpdateData(pixels);
            Require(!detached && detached.error().code==ImageErrorCode::Context,"Detached Image upload accepted");
            Require(SDL_GL_MakeCurrent(window,context)==0,"Restore Image context");
            ImageResult worker;
            std::thread task([&]{worker=first.UpdateData(pixels);});task.join();
            Require(!worker && worker.error().code==ImageErrorCode::Context && GLCalls::wrongThread==0,"Image worker issued GL");
            auto other=Image::Create(2,2,kind,pixels);Require(bool(other),"Move assignment target");const auto replaced=ImageName(*other);
            *other=std::move(first);Require(!ImageName(first) && ImageName(*other)==name && !glIsTexture(replaced),"Image move assignment retirement");
            *other=std::move(*other);Require(ImageName(*other)==name,"Image self move lost owner");
            RayChecked(other->Resize(3,2));auto stale=other->UpdateData(std::array<uint8_t,24>{});
            Require(!stale && stale.error().code==ImageErrorCode::Unavailable,"Resized image accepted stale storage update");
            RayChecked(other->SetData(std::array<uint8_t,24>{}));Require(!glIsTexture(name),"Resize old storage survived publication");
        }
        Require(GLCalls::live.empty(),"Image fixture leaked texture names");
        std::cout<<"[PASS] Image typed data/context/backend errors; allocation rollback; move-only exact retirement; byte formats preserved\n";
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
                RayChecked(renderer.OnResize(37, 29));
                RayChecked(renderer.Render(scene, camera));
                renderer.ResetFrameIndex();
                RayChecked(renderer.Render(scene, camera));
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
                ImageOwnership();
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

#endif

#endif
