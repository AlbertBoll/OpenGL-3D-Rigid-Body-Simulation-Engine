#include "../GEngine/src/Assets/ShaderBackend.h"
// Production-library sampler/cache/material checks; run through test_sampler.py.
#include "gepch.h"
#include "Assets/Samplers/Sampler.h"
#include "Core/GEngine.h"
#include "Core/RuntimeAssets.h"
#include "Core/GLContextThread.h"
#include "Managers/AssetsManager.h"
#include "Material/Material.h"
#include <cmath>
#include <new>
#include <print>

static int denyNothrowAfter = -1;
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    if (denyNothrowAfter == 0) { denyNothrowAfter = -1; return nullptr; }
    if (denyNothrowAfter > 0) --denyNothrowAfter;
    return std::malloc(size ? size : 1);
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }

namespace
{
    using namespace GEngine;
    using namespace ::GEngine::Asset;
    using ::GEngine::Manager::AssetsManager;
    int checks = 0;
    template<class T> void Check(const T& condition, const char* message)
    {
        ++checks;
        if (!static_cast<bool>(condition)) { std::println(stderr, "[FAIL] {}", message); std::exit(1); }
    }
    struct Observer
    {
        inline static PFNGLGENSAMPLERSPROC generate;
        inline static PFNGLDELETESAMPLERSPROC remove;
        inline static PFNGLSAMPLERPARAMETERIPROC parameter;
        inline static PFNGLBINDSAMPLERPROC bind;
        inline static std::unordered_set<GLuint> live;
        inline static std::vector<GLuint> units;
        inline static bool failName = false, failParameter = false, correct = true;
        inline static SDL_GLContext context;
        inline static std::thread::id thread;
        inline static unsigned created = 0, deleted = 0;
        static void APIENTRY Generate(GLsizei count, GLuint* names)
        {
            if (std::exchange(failName, false)) { std::fill_n(names, count, 0); return; }
            generate(count, names);
            for (int i = 0; i < count; ++i) { correct &= live.insert(names[i]).second && names[i]; ++created; }
        }
        static void APIENTRY Remove(GLsizei count, const GLuint* names)
        {
            correct &= SDL_GL_GetCurrentContext() == context && std::this_thread::get_id() == thread;
            for (int i = 0; i < count; ++i) { correct &= names[i] && live.erase(names[i]) == 1; ++deleted; }
            remove(count, names);
        }
        static void APIENTRY Parameter(GLuint name, GLenum key, GLint value)
        { if (!std::exchange(failParameter, false)) parameter(name, key, value); }
        static void APIENTRY Bind(GLuint unit, GLuint name) { units.push_back(unit); bind(unit, name); }
        Observer()
        {
            live.clear(); units.clear(); created = deleted = 0; correct = true;
            context = SDL_GL_GetCurrentContext(); thread = std::this_thread::get_id();
            generate = glad_glGenSamplers; glad_glGenSamplers = Generate;
            remove = glad_glDeleteSamplers; glad_glDeleteSamplers = Remove;
            parameter = glad_glSamplerParameteri; glad_glSamplerParameteri = Parameter;
            bind = glad_glBindSampler; glad_glBindSampler = Bind;
        }
        ~Observer()
        {
            glad_glGenSamplers = generate; glad_glDeleteSamplers = remove;
            glad_glSamplerParameteri = parameter; glad_glBindSampler = bind;
        }
    };
    GLuint Bound(unsigned unit)
    { GLint name = 0; glGetIntegeri_v(GL_SAMPLER_BINDING, unit, &name); return static_cast<GLuint>(name); }
    void OwnershipAndFailures(AssetPublication& publication)
    {
        static_assert(!std::is_copy_constructible_v<GpuSampler> && std::is_nothrow_move_constructible_v<GpuSampler>
            && std::is_nothrow_move_assignable_v<GpuSampler> && std::is_nothrow_destructible_v<GpuSampler>);
        SamplerDesc d;
        {
            auto a = GpuSampler::Create(d).value(); Check(a.Bind(0), "Initial sampler bind");
            const auto first = Bound(0);
            auto b = GpuSampler::Create(d).value(); Check(b.Bind(1), "Occupied move target bind");
            const auto second = Bound(1);
            b = std::move(a);
            Check(!a && b && !glIsSampler(second) && glIsSampler(first), "Occupied move ownership");
            Check(b.Bind(1) && Bound(1) == first, "Move lost sampler state");
            b = std::move(b); Check(b && glIsSampler(first), "Self move");
            std::vector<GpuSampler> moved;
            moved.push_back(std::move(b)); moved.reserve(40);
            Check(!b && moved[0].Bind(2) && Bound(2) == first, "Relocation ownership");
            Check(!b.Bind(0) && !moved[0].Bind(UINT32_MAX), "Invalid sampler/unit accepted");
        }
        Check(Observer::live.empty(), "Standalone sampler leaked");
        SamplerCache cache(publication, {2});
        const auto a = cache.Get(d).value();
        Check(cache.Get(d).value() == a && cache.Size() == 1, "Equivalent description cache miss");
        auto ignored = d; ignored.borderColor = {1, 2, 3, 4}; ignored.maxAnisotropy = 7;
        Check(cache.Get(ignored).value() == a, "Inactive policy did not canonicalize");
        for (int which = 0; which < 10; ++which)
        {
            auto invalid = d;
            if (which == 0) invalid.minFilter = static_cast<SamplerFilter>(-1);
            if (which == 1) invalid.magFilter = static_cast<SamplerFilter>(2);
            if (which == 2) invalid.mipFilter = static_cast<SamplerMipFilter>(3);
            if (which == 3) invalid.wrapU = static_cast<SamplerWrap>(4);
            if (which == 4) invalid.wrapV = static_cast<SamplerWrap>(-1);
            if (which == 5) invalid.wrapW = static_cast<SamplerWrap>(5);
            if (which == 6) invalid.compare = static_cast<SamplerCompare>(9);
            if (which == 7) invalid.anisotropy = static_cast<SamplerAnisotropy>(3);
            if (which == 8) invalid.maxAnisotropy = std::numeric_limits<float>::quiet_NaN();
            if (which == 9) invalid.borderColor[0] = std::numeric_limits<float>::infinity();
            auto result = cache.Get(invalid);
            Check(!result && result.error().code == SamplerErrorCode::InvalidDescription && cache.Size() == 1,
                "Invalid description partially published");
        }
        auto different = d; different.minFilter = SamplerFilter::Nearest;
        for (int allocation = 0; allocation < 2; ++allocation)
        {
            denyNothrowAfter = allocation;
            auto failed = cache.Get(different);
            Check(!failed && failed.error().code == SamplerErrorCode::Allocation && cache.Size() == 1,
                "Owner/cache allocation failure published");
        }
        Observer::failName = true;
        Check(cache.Get(different).error().code == SamplerErrorCode::Allocation && cache.Size() == 1, "Name failure published");
        const auto before = Observer::deleted;
        Observer::failParameter = true;
        Check(cache.Get(different).error().code == SamplerErrorCode::Driver && cache.Size() == 1
            && Observer::deleted == before + 1, "Driver rejection did not roll back");
        auto b = cache.Get(different).value();
        Check(a != b && cache.Size() == 2, "Different sampler collapsed");
        different.wrapU = SamplerWrap::ClampToEdge;
        const auto beforeFull = Observer::deleted;
        auto full = cache.Get(different);
        Check(!full && full.error().registry == RegistryError::SlotsExhausted && cache.Size() == 2
            && Observer::deleted == beforeFull + 1, "Full registry partially published/leaked");
        Check(!cache.Resolve({}), "Null sampler resolved");
        SamplerCache foreign(publication);
        Check(!foreign.Resolve(a), "Foreign sampler resolved");
        auto lease = cache.Resolve(a).value(); Check(lease->Bind(0), "Sampler lease bind");
        const auto name = Bound(0);
        std::thread worker([copy = std::move(lease)]() mutable { copy = {}; }); worker.join();
        Check(glIsSampler(name), "Worker release deleted sampler");
    }
    void Anisotropy(AssetPublication& publication)
    {
        SamplerCache cache(publication);
        float limit = 1; if (GLAD_GL_EXT_texture_filter_anisotropic) glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &limit);
        SamplerDesc d; d.anisotropy = SamplerAnisotropy::ClampToLimit; d.maxAnisotropy = limit * 2;
        const auto clamped = cache.Get(d).value();
        auto lease = cache.Resolve(clamped).value();
        Check(lease->Description().maxAnisotropy == limit && lease->Bind(0), "Anisotropy clamp");
        float actual = 1; if (GLAD_GL_EXT_texture_filter_anisotropic) glGetSamplerParameterfv(Bound(0), GL_TEXTURE_MAX_ANISOTROPY_EXT, &actual);
        Check(actual == limit, "Driver anisotropy differs");
        d.anisotropy = SamplerAnisotropy::RequireExact;
        Check(cache.Get(d).error().code == SamplerErrorCode::Unsupported && cache.Size() == 1, "Exact limit not rejected");
        d.maxAnisotropy = limit;
        Check(cache.Get(d).value() == clamped, "Effective anisotropy not shared");
        const auto supported = GLAD_GL_EXT_texture_filter_anisotropic;
        GLAD_GL_EXT_texture_filter_anisotropic = 0;
        d.maxAnisotropy = 2;
        Check(cache.Get(d).error().code == SamplerErrorCode::Unsupported, "Unsupported anisotropy silently accepted");
        d.anisotropy = SamplerAnisotropy::ClampToLimit;
        auto fallback = GpuSampler::Create(d).value();
        Check(fallback.Description().maxAnisotropy == 1, "Explicit unsupported clamp policy");
        GLAD_GL_EXT_texture_filter_anisotropic = supported;
        std::println("[PASS] anisotropy limit={}", limit);
    }
    struct TestMaterial : Material
    {
        explicit TestMaterial(Shader& shader) { m_Shader = &shader; }
        std::vector<std::uint32_t> Units() const
        { std::vector<std::uint32_t> result; for (const auto& [binding, unit] : m_ImageBindings) result.push_back(unit); return result; }
    };
    void SamplingAndMaterial(AssetPublication& publication)
    {
        TextureRegistry images(publication);
        TextureDesc color; color.width = 2; color.height = 1; color.colorSpace = TextureColorSpace::Linear; color.mips = TextureMipIntent::None;
        std::array<unsigned char, 8> pixels{0,0,0,255, 255,0,0,255};
        TextureDesc depth = color; depth.width = 1; depth.format = TextureFormat::Depth32Float;
        std::array<float, 1> depths{0.5f};
        TextureHandle imageHandle, depthHandle;
        { auto p = publication.BeginPublication();
          imageHandle = images.Create(p, TextureResource::Create(color, {std::as_bytes(std::span(pixels))}).value()).value();
          depthHandle = images.Create(p, TextureResource::Create(depth, {std::as_bytes(std::span(depths))}).value()).value(); }
        {
            TextureView image, shadow;
            { auto f = publication.BeginFrame(); image = TextureView(images.Acquire(f, imageHandle).value()); shadow = TextureView(images.Acquire(f, depthHandle).value()); }
            SamplerDesc nearest; nearest.minFilter = nearest.magFilter = SamplerFilter::Nearest;
            SamplerDesc linear; linear.wrapU = linear.wrapV = linear.wrapW = SamplerWrap::ClampToEdge;
            SamplerDesc compare = nearest; compare.compare = SamplerCompare::LessEqual;
            auto a = AssetsManager::SampleTexture(image, nearest).value();
            auto b = AssetsManager::SampleTexture(image, linear).value();
            auto c = AssetsManager::SampleTexture(shadow, compare).value();
            Check(a.TextureIdentity() == b.TextureIdentity() && a.SamplerIdentity() != b.SamplerIdentity(), "Image/sampler identities conflated");
            auto defaults = AssetsManager::SampleTexture(image).value();
            Check(defaults.sampler->Description().mipFilter == SamplerMipFilter::None, "Legacy mip default changed");
            const ShaderSource sources[]{
                {VERTEX, "#version 460 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2-1,0,1);}", "sampler-vertex"},
                {FRAGMENT, "#version 460 core\nuniform sampler2D a;uniform sampler2D b;uniform sampler2DShadow c;uniform vec2 uv;uniform float ref;out vec4 result;void main(){result=vec4(texture(a,uv).r,texture(b,uv).r,texture(c,vec3(.5,.5,ref)),1);}", "sampler-fragment"}};
            auto program = CreateShaderProgram(sources); Check(program.has_value(), "Sampler fixture shader creation");
            auto& shader = *program; shader.Bind();
            TestMaterial material(shader);
            Check(material.SetSampledTextureBinding("c", c, 5), "Shadow material binding");
            Check(material.SetSampledTextureBinding("b", b, 3), "Linear material binding");
            Check(material.SetSampledTextureBinding("a", a, 1), "Nearest material binding");
            Check(material.SetSampledTextureBinding("b", b, 3) && material.Units() == std::vector<std::uint32_t>{1,3,5}, "Binding replacement/order");
            Observer::units.clear(); material.BindTextureUniforms();
            Check(Observer::units == std::vector<GLuint>{1,1,3,3,5,5}, "Submission order depends on cache insertion");
            const auto bound = Bound(1);
            Check(!SampledTextureBinding{image, {}}.Bind(1) && Bound(1) == bound, "Invalid binding changed sampler state");
            Check(!SampledTextureBinding{{}, a.sampler}.Bind(1) && Bound(1) == bound, "Invalid image changed sampler state");
            GLuint vao = 0; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
            glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0,0,1,1);
            glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_FRAMEBUFFER_SRGB);
            auto draw = [&](float u, float reference)
            {
                glUniform2f(glGetUniformLocation(::GEngine::Asset::ShaderBackendAccess::Program(shader), "uv"), u, .5f);
                glUniform1f(glGetUniformLocation(::GEngine::Asset::ShaderBackendAccess::Program(shader), "ref"), reference);
                glDrawArrays(GL_TRIANGLES,0,3);
                std::array<unsigned char,4> result{}; glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,result.data()); return result;
            };
            auto wrap = draw(1.25f, .25f);
            Check(wrap[0] < 3 && wrap[1] > 252 && wrap[2] > 252, "Same-image wrap/shadow sampling pixels incorrect");
            auto filter = draw(.5f, .75f);
            Check(filter[0] > 252 && std::abs(int(filter[1])-128) <= 2 && filter[2] < 3, "Filter/shadow comparison pixels incorrect");
            auto border = linear; border.wrapU = SamplerWrap::ClampToBorder; border.borderColor = {.25f,0,0,1};
            auto bordered = AssetsManager::SampleTexture(image,border).value();
            Check(material.SetSampledTextureBinding("b", bordered, 3), "Border binding");
            Check(std::abs(int(draw(3.f,.25f)[1])-64) <= 2, "Border color sampling");
            Check(image.Bind(1) && Bound(1) == 0, "Texture-only consumer inherited a previous material sampler");
            glDeleteVertexArrays(1,&vao);
            GLint imageName = 0; image.Bind(7).value(); glGetIntegerv(GL_TEXTURE_BINDING_2D,&imageName);
            material.GetTextureList().emplace(GL_TEXTURE_2D,std::pair{static_cast<unsigned>(imageName),3u});
            material.BindTextureUniforms(); Check(Bound(3) == 0, "Legacy target retained unrelated sampler");
        }
        { auto p = publication.BeginPublication(); Check(images.Close(p), "Image leases escaped material"); }
        Check(glGetError() == GL_NO_ERROR, "Sampler operations emitted GL errors");
        std::println("[PASS] same-image wrap/filter/shadow/border pixels and deterministic material bindings");
    }
}
int main(int argc, char** argv)
{
    using namespace GEngine;
    RuntimeAssets::Initialize("GEngineEditor");
    for (int cycle=0; cycle<2; ++cycle)
    {
        auto root = std::make_unique<EngineContext>();
        WindowProperties properties;
        properties.m_Title = "Sampler validation"; properties.m_Width = properties.m_Height = 64;
        properties.m_MinWidth = properties.m_MinHeight = 32; properties.m_IsVsync = false;
        properties.flag = BitFlags<WindowFlags,uint8_t>{WindowFlags::INVISIBLE};
        Check(root->Initialize({properties}).has_value(), "Platform initialization failed");
        int startup = 0; while(glGetError()!=GL_NO_ERROR) ++startup;
        std::println("[GL] {} renderer={} legacy-startup-errors={}", reinterpret_cast<const char*>(glGetString(GL_VERSION)), reinterpret_cast<const char*>(glGetString(GL_RENDERER)), startup);
        Observer observer;
        if (argc > 1)
        {
            std::set_terminate([] { std::println(stderr,"[EXPECTED] sampler ownership invariant"); std::_Exit(86); });
            const std::string_view mode = argv[1];
            if (mode == "--reject-worker")
            {
                auto sampler = GpuSampler::Create({}).value();
                std::thread worker([owner=std::move(sampler)]() mutable {
                    std::set_terminate([] { std::println(stderr,"[EXPECTED] sampler ownership invariant"); std::_Exit(86); });
                    owner = {};
                });
                worker.join();
            }
            else if (mode == "--reject-live-lease")
            { auto cache = std::make_unique<SamplerCache>(root->AssetPublications()); auto lease = cache->Resolve(cache->Get({}).value()).value(); cache.reset(); }
            return 89;
        }
        OwnershipAndFailures(root->AssetPublications());
        Anisotropy(root->AssetPublications());
        SamplingAndMaterial(root->AssetPublications());
        root.reset();
        Check(Observer::correct && Observer::live.empty() && Observer::created == Observer::deleted && !SDL_GL_GetCurrentContext(), "Sampler exact-once/context retirement");
        std::println("[PASS] sampler created={} deleted={}", Observer::created, Observer::deleted);
    }
    std::println("[PASS] samplers cycles=2 checks={}", checks);
}
