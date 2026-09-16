// Production-library lifecycle checks; all GL and destruction stay on this thread.
#include "gepch.h"
#include "Core/BaseApp.h"
#include "Core/RuntimeAssets.h"
#include "Windows/SDLWindow.h"
#include "Windows/ImGuiWindow.h"
#include "Managers/AssetsManager.h"
#include "Managers/ShapeManager.h"
#include "Managers/ShaderManager.h"
#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <sdl2/SDL_ttf.h>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <array>
#include <type_traits>

namespace
{
    using namespace ::GEngine;
    using namespace ::GEngine::Manager;
    int checks = 0, observedFailures = 0, debugErrors = 0;
    bool teardown = false;
    void Check(bool value, const char* message)
    {
        ++checks;
        if (!value) throw std::runtime_error(message);
    }
    void Observe(bool value, const char* message)
    {
        ++checks;
        if (!value) { ++observedFailures; std::cerr << "[OBSERVED FAIL] " << message << '\n'; }
    }
    void APIENTRY DebugMessage(GLenum, GLenum type, GLuint, GLenum severity, GLsizei length,
        const GLchar* message, const void*)
    {
        if (teardown && (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH))
        {
            ++debugErrors;
            std::cerr << "[TEARDOWN GL ERROR] " << std::string(message, length) << '\n';
        }
    }
    void StartTeardownDiagnostics()
    {
        int startupErrors = 0;
        while (glGetError() != GL_NO_ERROR) ++startupErrors;
        std::cout << "[INFO] startup GL errors separated from teardown: " << startupErrors << '\n';
        Check(glDebugMessageCallback != nullptr, "KHR_debug is required by this fixture");
        glEnable(GL_DEBUG_OUTPUT); glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(DebugMessage, nullptr);
        teardown = true;
    }
    WindowProperties Properties()
    {
        WindowProperties p;
        p.m_Title = "Phase 18 lifecycle";
        p.m_Width = p.m_Height = 64;
        p.m_MinWidth = p.m_MinHeight = 32;
        p.m_IsVsync = false;
        p.flag = BitFlags<WindowFlags, uint8_t>{WindowFlags::INVISIBLE};
        return p;
    }
    void PlatformGone()
    {
        Check(SDL_GL_GetCurrentContext() == nullptr, "GL context survived platform release");
        Check(ImGui::GetCurrentContext() == nullptr, "ImGui context survived platform release");
        Check(SDL_WasInit(0) == 0 && TTF_WasInit() == 0, "SDL/TTF survived platform release");
        Check(!BaseApp::GetWindowManager() && !BaseApp::GetInputManager() && !BaseApp::GetEventManager(),
            "Engine kept a freed platform manager");
    }

    enum class Kind { Texture, Framebuffer, Buffer };
    struct Watched { Kind kind; GLuint name; int phase; int deletes = 0; const char* label; };
    std::vector<Watched> watched;
    SDL_GLContext owner = nullptr;
    std::thread::id ownerThread;
    int lastPhase = 0;
    PFNGLDELETETEXTURESPROC deleteTextures;
    PFNGLDELETEFRAMEBUFFERSPROC deleteFramebuffers;
    PFNGLDELETEBUFFERSPROC deleteBuffers;
    void Deleted(Kind kind, GLsizei count, const GLuint* names)
    {
        Observe(SDL_GL_GetCurrentContext() != nullptr && std::this_thread::get_id() == ownerThread,
            "Resource deletion has no context or ran on another thread");
        for (GLsizei i = 0; i < count; ++i) for (auto& item : watched)
            if (item.kind == kind && item.name == names[i])
            {
                Observe(SDL_GL_GetCurrentContext() == owner, "Resource deleted on a foreign context");
                Observe(++item.deletes == 1, "Watched GL resource deleted more than once");
                Observe(item.phase >= lastPhase, "Scene/renderer/asset shutdown order regressed");
                lastPhase = item.phase;
                Observe(ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData
                    && ImGui::GetIO().BackendPlatformUserData, "ImGui died before application GPU resources");
                Observe(SDL_WasInit(SDL_INIT_VIDEO) && TTF_WasInit(), "Platform died before application GPU resources");
            }
    }
    void APIENTRY DeleteTextures(GLsizei n, const GLuint* ids) { Deleted(Kind::Texture, n, ids); deleteTextures(n, ids); }
    void APIENTRY DeleteFramebuffers(GLsizei n, const GLuint* ids) { Deleted(Kind::Framebuffer, n, ids); deleteFramebuffers(n, ids); }
    void APIENTRY DeleteBuffers(GLsizei n, const GLuint* ids) { Deleted(Kind::Buffer, n, ids); deleteBuffers(n, ids); }
    struct Hooks
    {
        Hooks()
        {
            watched.clear(); lastPhase = 0; owner = SDL_GL_GetCurrentContext(); ownerThread = std::this_thread::get_id();
            deleteTextures = glad_glDeleteTextures; glad_glDeleteTextures = DeleteTextures;
            deleteFramebuffers = glad_glDeleteFramebuffers; glad_glDeleteFramebuffers = DeleteFramebuffers;
            deleteBuffers = glad_glDeleteBuffers; glad_glDeleteBuffers = DeleteBuffers;
        }
        ~Hooks()
        {
            glad_glDeleteTextures = deleteTextures; glad_glDeleteFramebuffers = deleteFramebuffers;
            glad_glDeleteBuffers = deleteBuffers;
        }
    };
    void Watch(Kind kind, GLuint name, int phase, const char* label = "fixture resource")
    {
        Check(name != 0, "Fixture observed an unallocated GL name");
        watched.push_back({kind, name, phase, 0, label});
    }
    struct TextureOwner
    {
        GLuint id = 0;
        explicit TextureOwner(int phase)
        {
            glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            Watch(Kind::Texture, id, phase);
        }
        ~TextureOwner() { glDeleteTextures(1, &id); }
    };
    struct CachedGeometry final : Geometry { TextureOwner texture{3}; };
    struct SceneActor final : Actor
    {
        TextureOwner texture{1};
        ~SceneActor() override
        {
            Observe(ShapeManager::GetShape("phase18") != nullptr, "Shared geometry died before scene borrower");
        }
    };
    class ProbeApp final : public BaseApp
    {
    public:
        std::unique_ptr<TextureOwner> applicationTexture;
        int updates = 0;
        void Prepare()
        {
            applicationTexture = std::make_unique<TextureOwner>(1);
            m_Scene->Add(new SceneActor);
            Watch(Kind::Buffer, m_UniformBufferObject->GetUBO(), 2, "BaseApp uniform buffer");
            Watch(Kind::Framebuffer, m_FinalFrameBuffer->GetFBO(), 2, "FinalFrameBuffer framebuffer");
            Watch(Kind::Texture, m_FinalFrameBuffer->GetColorMap(), 2, "FinalFrameBuffer color texture");
            Watch(Kind::Texture, m_FinalFrameBuffer->GetMousePickMap(), 2, "FinalFrameBuffer picking texture");
            GLint depth = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, m_FinalFrameBuffer->GetFBO());
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depth);
            Watch(Kind::Texture, static_cast<GLuint>(depth), 2, "FinalFrameBuffer depth texture");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            Watch(Kind::Framebuffer, m_MousePickFrameBuffer->GetLightFBO(), 2);
            Watch(Kind::Framebuffer, m_PointShadowFrameBuffer->GetDepthMapFBO(), 2);
            Watch(Kind::Framebuffer, m_CascadeShadowFrameBuffer->GetLightFBO(), 2);
            Watch(Kind::Framebuffer, m_RenderTarget->GetFrameBufferID(), 2);
            ShapeManager::Register("phase18", new CachedGeometry);
            auto* text = AssetsManager::GetTextTexture("Lifecycle", RuntimeAssets::File("Fonts/OpenSans-Regular.ttf"));
            Watch(Kind::Texture, text->GetTextureID(), 3, "AssetsManager cached text texture");
            AssetsManager::GetCascadedFrameBufferTexture(*m_CascadeShadowFrameBuffer);
            AssetsManager::GetPointShadowFrameBufferTexture(*m_PointShadowFrameBuffer);
            auto* gui = GetSDLWindow()->GetImGuiWindow();
            gui->BeginRender(GetSDLWindow());
            ImGui::TextUnformatted("Phase 18");
            gui->EndRender(GetSDLWindow());
            Check(ImGui::GetIO().Fonts->TexID != nullptr, "ImGui font GPU resource was not exercised");
        }
        void ProcessInput(Timestep) override {}
        void Update(Timestep) override { ++updates; }
        void Render() override { ShutDown(); }
    };
    void Lifetimes(bool minimized, bool applicationFailure)
    {
        RuntimeAssets::Initialize("GEngineEditor");
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            auto app = std::make_unique<ProbeApp>();
            app->Initialize(Properties());
            Hooks hooks;
            app->Prepare();
            const auto windowID = app->GetSDLWindow()->GetWindowID();
            StartTeardownDiagnostics();
            if (minimized)
            {
                SDL_Event event{}; while (SDL_PollEvent(&event)) {}
                SDL_MinimizeWindow(app->GetSDLWindow()->GetSDLWindow());
                event.type = SDL_WINDOWEVENT; event.window.windowID = windowID;
                event.window.event = SDL_WINDOWEVENT_MINIMIZED;
                Check(SDL_PushEvent(&event) == 1, "Could not queue minimize");
                std::atomic<bool> queued{false};
                // Worker only queues an event; all GL and destruction stay on this thread.
                std::jthread close([windowID, &queued] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(120));
                    SDL_Event e{}; e.type = SDL_WINDOWEVENT; e.window.windowID = windowID;
                    e.window.event = SDL_WINDOWEVENT_CLOSE; queued = SDL_PushEvent(&e) == 1;
                });
                app->Run(); close.join();
                Check(queued && app->updates == 0 && app->IsRenderingSuspended(), "Close while minimized ran application work");
            }
            else if (applicationFailure)
            {
                try
                {
                    auto unwindingApp = std::move(app);
                    throw std::runtime_error("injected derived initialization failure");
                }
                catch (const std::runtime_error&) {}
            }
            else
            {
                SDL_Event event{}; event.type = SDL_QUIT;
                Check(SDL_PushEvent(&event) == 1, "Could not queue quit");
                app->Run();
            }
            app.reset();
            for (const auto& item : watched)
                if (item.deletes != 1) std::cerr << "[RESOURCE] " << item.label << " name=" << item.name
                    << " phase=" << item.phase << " deletes=" << item.deletes << '\n';
            for (const auto& item : watched) Check(item.deletes == 1, "A watched resource survived application destruction");
            Check(lastPhase == 3 && observedFailures == 0, "Resource ownership/order checks failed");
            Check(ShapeManager::GetShape("phase18") == nullptr, "Freed geometry container was not cleared");
            Check(SDL_GL_GetCurrentContext() == owner && SDL_GetWindowFromID(windowID), "Main platform died before explicit release");
            Check(ImGui::GetIO().BackendRendererUserData && ImGui::GetIO().BackendPlatformUserData,
                "Application cleanup destroyed ImGui prematurely");
            // Empty-cache cleanup must remain idempotent, including framebuffer borrowers.
            AssetsManager::FreeAllResources(); ShaderManager::FreeShader(); ShapeManager::FreeShape();
            Check(glGetError() == GL_NO_ERROR && debugErrors == 0, "GL teardown emitted errors");
            BaseApp::GetEngine().ReleasePlatform();
            PlatformGone();
            BaseApp::GetEngine().ReleasePlatform(); PlatformGone();
            Check(debugErrors == 0, "ImGui/context teardown emitted GL errors");
            teardown = false;
            std::cout << "[PASS] lifecycle cycle=" << cycle << " watched=" << watched.size()
                      << " scene -> renderer -> assets -> ImGui -> GL -> SDL/TTF\n";
        }
    }

    struct FaultAllocator
    {
        ImGuiMemAllocFunc alloc; ImGuiMemFreeFunc free; void* data;
        bool injected = false;
        static void* Allocate(size_t size, void* user)
        {
            auto& self = *static_cast<FaultAllocator*>(user);
            if (!self.injected && ImGui::GetCurrentContext() && ImGui::GetIO().BackendPlatformUserData
                && !ImGui::GetIO().BackendRendererUserData)
            {
                self.injected = true;
                throw std::bad_alloc();
            }
            return self.alloc(size, self.data);
        }
        static void Free(void* ptr, void* user)
        {
            auto& self = *static_cast<FaultAllocator*>(user); self.free(ptr, self.data);
        }
        FaultAllocator()
        {
            ImGui::GetAllocatorFunctions(&alloc, &free, &data);
            ImGui::SetAllocatorFunctions(Allocate, Free, this);
        }
        ~FaultAllocator() { ImGui::SetAllocatorFunctions(alloc, free, data); }
    };
    void Partial(bool platformBackend)
    {
        Log::Initialize();
        if (platformBackend) RuntimeAssets::Initialize("GEngineEditor");
        Check(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
        auto manager = WindowManager::GetScopedInstance();
        bool caught = false;
        if (platformBackend)
        {
            FaultAllocator fault;
            try { manager->AddWindows(Properties()); }
            catch (const std::bad_alloc&) { caught = true; }
            Check(fault.injected, "Partial SDL-backend allocation failure was not reached");
        }
        else
        {
            // RuntimeAssets is deliberately uninitialized: font path resolution throws
            // after SDL/GL/ImGui context creation and before either ImGui backend.
            try { manager->AddWindows(Properties()); }
            catch (const std::runtime_error&) { caught = true; }
        }
        Check(caught, "Expected partial ImGui initialization failure");
        Check(manager->GetWindows().empty() && manager->GetNumOfWindows() == 0,
            "Failed window construction entered its manager");
        Check(!SDL_GL_GetCurrentContext() && !ImGui::GetCurrentContext(), "Partial window initialization leaked a context");
        manager.reset(); SDL_Quit();
        ImGuiWindow_ neverInitialized; neverInitialized.ShutDown(); neverInitialized.ShutDown();
        SDLWindow noWindow; noWindow.ShutDown(); noWindow.ShutDown();
    }
    void PlatformFailure(bool noWindows)
    {
        bool caught = false;
        {
            BaseApp app;
            try
            {
                if (noWindows) app.Initialize(std::initializer_list<WindowProperties>{});
                else app.Initialize(Properties());
            }
            catch (const std::exception& error) { caught = true; std::cout << "[EXPECTED] " << error.what() << '\n'; }
        }
        Check(caught, "Expected platform initialization failure");
        BaseApp::GetEngine().ReleasePlatform(); PlatformGone();
        BaseApp::GetEngine().ReleasePlatform(); PlatformGone();
    }
    void WindowOwners()
    {
        Log::Initialize(); RuntimeAssets::Initialize("GEngineEditor");
        Check(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
        auto manager = WindowManager::GetScopedInstance();
        manager->AddWindows(Properties());
        auto* first = static_cast<SDLWindow*>(manager->GetWindows().begin()->second.get());
        const auto firstID = first->GetWindowID(); const auto firstContext = first->GetContext();
        const auto firstGui = first->GetImGuiWindow()->GetContext();
        manager->AddWindows(Properties());
        auto second = std::find_if(manager->GetWindows().begin(), manager->GetWindows().end(),
            [firstID](const auto& entry) { return entry.first != firstID; });
        const auto secondID = second->first;
        first->BeginRender();
        StartTeardownDiagnostics();
        manager->RemoveWindow(secondID);
        Check(SDL_GL_GetCurrentContext() == firstContext && ImGui::GetCurrentContext() == firstGui,
            "Secondary window destruction failed to restore the live owning contexts");
        manager->RemoveWindow(secondID);
        Check(manager->GetNumOfWindows() == 1 && manager->GetWindows().size() == 1, "Repeated removal corrupted window count");
        auto* gui = first->GetImGuiWindow();
        gui->BeginRender(first); ImGui::TextUnformatted("font upload"); gui->EndRender(first);
        const auto font = static_cast<GLuint>(reinterpret_cast<uintptr_t>(ImGui::GetIO().Fonts->TexID));
        Check(glIsTexture(font), "ImGui font texture missing");
        gui->ShutDown(); gui->ShutDown();
        Check(!glIsTexture(font) && !ImGui::GetCurrentContext() && SDL_GL_GetCurrentContext() == firstContext,
            "ImGui cleanup did not retire GL resources before the context");
        Check(glGetError() == GL_NO_ERROR && debugErrors == 0, "Standalone window teardown GL error");
        first->ShutDown(); first->ShutDown();
        manager->RemoveWindow(firstID);
        Check(manager->GetWindows().empty() && manager->GetNumOfWindows() == 0, "Window manager kept a freed owner");
        manager.reset(); SDL_Quit(); teardown = false;
    }

    template<typename T> constexpr bool UniqueMovable = !std::is_copy_constructible_v<T>
        && !std::is_copy_assignable_v<T> && std::is_nothrow_move_constructible_v<T>
        && std::is_nothrow_move_assignable_v<T>;
    static_assert(UniqueMovable<FinalFrameBuffer>);
    template<UniformType Type> void BufferMoves()
    {
        using Buffer = UniformBufferObject<Type>;
        static_assert(UniqueMovable<Buffer>);
        watched.clear(); lastPhase = 0;
        GLuint sourceName = 0, replacedName = 0;
        {
            Buffer source(3, 0), target(7, 1);
            sourceName = source.GetUBO(); replacedName = target.GetUBO();
            Watch(Kind::Buffer, sourceName, 2); Watch(Kind::Buffer, replacedName, 2);
            const auto elementSize = source.GetUniformTypeSize();
            const std::array<GLuint, 4> data{0x01234567, 0x89abcdef, 42, 13};
            glBindBuffer(GL_UNIFORM_BUFFER, sourceName);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), data.data());
            Buffer moved(std::move(source));
            Check(source.GetUBO() == 0 && moved.GetUBO() == sourceName, "UBO move construction copied ownership");
            Check(moved.GetUniformTypeSize() == elementSize && source.GetUniformTypeSize() == 0,
                "UBO move did not transfer/reset metadata");
            target = std::move(moved);
            Check(moved.GetUBO() == 0 && target.GetUBO() == sourceName && !glIsBuffer(replacedName),
                "UBO move assignment did not retire the replaced buffer");
            glBindBuffer(GL_UNIFORM_BUFFER, target.GetUBO());
            std::array<GLuint, 4> readback{};
            glGetBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(readback), readback.data());
            GLint bytes = 0, indexed = 0;
            glGetBufferParameteriv(GL_UNIFORM_BUFFER, GL_BUFFER_SIZE, &bytes);
            glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &indexed);
            Check(readback == data && bytes == static_cast<GLint>(3 * elementSize)
                && static_cast<GLuint>(indexed) == sourceName, "UBO move changed storage/data/indexed binding");
            auto* same = &target; target = std::move(*same);
            Check(target.GetUBO() == sourceName && glIsBuffer(sourceName), "UBO self-move lost ownership");
            source = std::move(target);
            Check(!target.GetUBO() && source.GetUBO() == sourceName, "Moved-from UBO could not receive ownership");
            source = std::move(moved); // Empty source must also retire a live destination.
            Check(!source.GetUBO() && !glIsBuffer(sourceName), "Moving an empty UBO leaked its destination");
        }
        for (const auto& item : watched) Check(item.deletes == 1, "UBO move lifetime deleted a resource incorrectly");
        Check(!glIsBuffer(sourceName) && !glIsBuffer(replacedName), "UBO move lifetime left a live GL buffer");
    }
    std::array<GLuint, 4> FramebufferNames(const FinalFrameBuffer& buffer)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, buffer.GetFBO());
        GLint depth = 0;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depth);
        return {buffer.GetFBO(), buffer.GetColorMap(), buffer.GetMousePickMap(), static_cast<GLuint>(depth)};
    }
    void CheckRetired(const std::array<GLuint, 4>& names)
    {
        Check(!glIsFramebuffer(names[0]), "Replaced framebuffer survived move/destruction");
        for (size_t i = 1; i < names.size(); ++i) Check(!glIsTexture(names[i]), "An owned framebuffer texture survived move/destruction");
    }
    void FramebufferMoves()
    {
        watched.clear(); lastPhase = 0;
        std::array<GLuint, 4> sourceNames{}, replacedNames{};
        {
            FinalFrameBuffer source(16, 16), target(32, 32);
            sourceNames = FramebufferNames(source); replacedNames = FramebufferNames(target);
            for (const auto& names : {sourceNames, replacedNames})
                for (size_t i = 0; i < names.size(); ++i) Watch(i ? Kind::Texture : Kind::Framebuffer, names[i], 2);
            glBindFramebuffer(GL_FRAMEBUFFER, source.GetFBO());
            const GLfloat green[]{0.f, 1.f, 0.f, 1.f};
            glClearBufferfv(GL_COLOR, 0, green);
            FinalFrameBuffer moved(std::move(source));
            Check(!source.GetFBO() && !source.GetColorMap() && !source.GetMousePickMap()
                && source.GetResolution() == Vec2f(0), "Framebuffer move constructor left an owning source");
            Check(FramebufferNames(moved) == sourceNames && moved.GetResolution() == Vec2f(16),
                "Framebuffer move constructor changed attachments or size");
            target = std::move(moved);
            Check(!moved.GetFBO() && !moved.GetColorMap() && !moved.GetMousePickMap(),
                "Framebuffer move assignment left an owning source");
            CheckRetired(replacedNames);
            Check(FramebufferNames(target) == sourceNames && target.GetResolution() == Vec2f(16),
                "Framebuffer move assignment changed attachments or size");
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            std::array<GLubyte, 4> pixel{};
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
            Check(pixel == std::array<GLubyte, 4>{0, 255, 0, 255}, "Framebuffer contents did not survive transfer");
            auto* same = &target; target = std::move(*same);
            Check(FramebufferNames(target) == sourceNames, "Framebuffer self-move lost ownership");
            source = std::move(target);
            Check(!target.GetFBO() && FramebufferNames(source) == sourceNames, "Moved-from framebuffer could not receive ownership");
            source = std::move(moved);
            Check(!source.GetFBO() && !source.GetColorMap() && !source.GetMousePickMap(),
                "Moving an empty framebuffer retained destination ownership");
            CheckRetired(sourceNames);
        }
        for (const auto& item : watched) Check(item.deletes == 1, "Framebuffer move lifetime deleted a resource incorrectly");
        CheckRetired(sourceNames); CheckRetired(replacedNames);
    }
    void CacheOwnership()
    {
        Log::Initialize(); RuntimeAssets::Initialize("GEngineEditor");
        Check(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
        Check(TTF_Init() == 0, TTF_GetError());
        {
            SDLWindow window; window.Initialize(Properties());
            StartTeardownDiagnostics();
            // A second cycle also proves the owning map was cleared before reuse.
            for (int cycle = 0; cycle < 2; ++cycle)
            {
                Hooks hooks;
                std::array<GLuint, 2> owned{}, borrowed{};
                {
                    CascadeShadowFrameBuffer cascade(16, 16, 3);
                    PointShadowFrameBuffer point(16, 16);
                    auto* text = AssetsManager::GetTextTexture("Cache ownership",
                        RuntimeAssets::File("Fonts/OpenSans-Regular.ttf"));
                    const auto imagePath = std::filesystem::path(RuntimeAssets::File("Images/white.png"))
                        .lexically_normal().string();
                    auto* image = AssetsManager::GetTexture(imagePath);
                    Check(AssetsManager::GetTexture(imagePath) == image, "Image cache did not reuse its owner");
                    owned = {text->GetTextureID(), image->GetTextureID()};
                    borrowed = {cascade.GetLightDepthMaps(), point.GetDepthCubeMaps()};
                    Check(AssetsManager::GetCascadedFrameBufferTexture(cascade)->GetTextureID() == borrowed[0]
                        && AssetsManager::GetPointShadowFrameBufferTexture(point)->GetTextureID() == borrowed[1],
                        "Framebuffer cache did not borrow the owners' texture names");
                    for (auto name : owned) { Check(glIsTexture(name), "Owned cache texture is not live"); Watch(Kind::Texture, name, 3); }
                    for (auto name : borrowed) { Check(glIsTexture(name), "Borrowed texture is not live"); Watch(Kind::Texture, name, 3); }
                    AssetsManager::FreeTextureResource();
                    AssetsManager::FreeTextureResource();
                    for (size_t i = 0; i < owned.size(); ++i)
                        Check(!glIsTexture(owned[i]) && watched[i].deletes == 1, "Owned cache texture was not deleted exactly once");
                    for (size_t i = 0; i < borrowed.size(); ++i)
                        Check(glIsTexture(borrowed[i]) && watched[owned.size() + i].deletes == 0,
                            "Cache cleanup deleted a borrowed framebuffer texture");
                    // Recreate borrowers while the same framebuffer owners remain live.
                    Check(AssetsManager::GetCascadedFrameBufferTexture(cascade)->GetTextureID() == borrowed[0]
                        && AssetsManager::GetPointShadowFrameBufferTexture(point)->GetTextureID() == borrowed[1],
                        "Repopulated framebuffer cache changed borrowed names");
                    AssetsManager::FreeAllResources(); AssetsManager::FreeAllResources();
                    for (size_t i = 0; i < borrowed.size(); ++i)
                        Check(glIsTexture(borrowed[i]) && watched[owned.size() + i].deletes == 0,
                            "Repeated cleanup retired a live framebuffer owner's texture");
                }
                for (const auto& item : watched)
                    Check(item.deletes == 1 && !glIsTexture(item.name), "Texture outlived its actual owner or was deleted twice");
                AssetsManager::FreeAllResources();
                Check(glGetError() == GL_NO_ERROR && observedFailures == 0 && debugErrors == 0,
                    "Cache ownership cleanup emitted a GL or lifetime error");
            }
        }
        Check(!SDL_GL_GetCurrentContext() && !ImGui::GetCurrentContext(), "Cache fixture leaked its contexts");
        TTF_Quit(); SDL_Quit(); teardown = false;
    }
    void ResourceMoves()
    {
        Log::Initialize(); RuntimeAssets::Initialize("GEngineEditor");
        Check(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
        Check(TTF_Init() == 0, TTF_GetError());
        {
            SDLWindow window; window.Initialize(Properties());
            Hooks hooks;
            StartTeardownDiagnostics();
            BufferMoves<UniformType::VEC2F>(); BufferMoves<UniformType::VEC3F>();
            BufferMoves<UniformType::VEC4F>(); BufferMoves<UniformType::MATRIX_2_2>();
            BufferMoves<UniformType::MATRIX_3_3>(); BufferMoves<UniformType::MATRIX_4_4>();
            FramebufferMoves();
            Check(glGetError() == GL_NO_ERROR && observedFailures == 0 && debugErrors == 0,
                "Resource move/lifetime checks failed");
        }
        Check(!SDL_GL_GetCurrentContext() && !ImGui::GetCurrentContext(), "Resource move fixture leaked its contexts");
        TTF_Quit(); SDL_Quit(); teardown = false;
    }
}

int main(int argc, char** argv)
{
    SDL_SetMainReady();
    try
    {
        const std::string_view mode = argc == 2 ? argv[1] : "";
        if (mode == "--lifetimes") Lifetimes(false, false);
        else if (mode == "--minimized") Lifetimes(true, false);
        else if (mode == "--application-failure") Lifetimes(false, true);
        else if (mode == "--imgui-context-failure") Partial(false);
        else if (mode == "--imgui-platform-failure") Partial(true);
        else if (mode == "--sdl-failure" || mode == "--window-failure") PlatformFailure(false);
        else if (mode == "--empty-windows") PlatformFailure(true);
        else if (mode == "--window-owners") WindowOwners();
        else if (mode == "--resource-moves") ResourceMoves();
        else if (mode == "--cache-ownership") CacheOwnership();
        else throw std::invalid_argument("Unknown shutdown probe mode");
        Check(observedFailures == 0 && debugErrors == 0, "Observed lifecycle failures");
        std::cout << "[PASS] shutdown " << mode << " checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] shutdown: " << error.what() << '\n';
        BaseApp::GetEngine().ReleasePlatform();
        return 1;
    }
}
