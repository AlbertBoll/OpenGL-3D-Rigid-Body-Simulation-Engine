#include "Material/BasicMaterial.h"
#include "Material/TextureMaterial.h"
#include "../GEngine/src/Assets/ShaderBackend.h"
#include "../GEngine/src/Core/FramebufferBackend.h"
// Production-library lifecycle checks; all GL and destruction stay on this thread.
#include "gepch.h"
#include "Core/BaseApp.h"
#include "Core/Renderer.h"
#include "Core/Scene.h"
#include "Core/RuntimeAssets.h"
#include "Windows/SDLWindow.h"
#include "Windows/ImGuiWindow.h"
#include "Managers/AssetsManager.h"
#include "../GEngine/src/Assets/TextureBackend.h"
#include "Managers/ShapeManager.h"
#include "Managers/ShaderManager.h"
#include "Camera/PerspectiveCamera.h"
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
#include <fstream>

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
    GLuint TextureName(const Asset::Texture* texture)
    { return Asset::AssetDetail::TextureBackend::Name(texture->View()).value(); }
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
        int rejected = 0;
        if (!AssetsManager::GetFont("unavailable.ttf")) ++rejected;
        if (!ShaderManager::GetShaderProgram({})) ++rejected;
        try { (void)ShapeManager::GetShape("phase18"); } catch (const std::logic_error&) { ++rejected; }
        Check(rejected == 3, "A manager remained accessible outside ready root lifetime");
    }

    void RootContract()
    {
        static_assert(!std::is_copy_constructible_v<EngineContext> && !std::is_move_constructible_v<EngineContext>);
        static_assert(!std::is_default_constructible_v<AssetsManager> && !std::is_copy_constructible_v<AssetsManager>);
        static_assert(!std::is_default_constructible_v<ShaderManager> && !std::is_move_constructible_v<ShaderManager>);
        static_assert(!std::is_default_constructible_v<ShapeManager> && !std::is_copy_constructible_v<ShapeManager>);
        Check(EngineContext::TryGet() == nullptr, "A root exists before application construction");
        bool rejected = false;
        try { (void)BaseApp::GetEngine(); } catch (const std::logic_error&) { rejected = true; }
        Check(rejected, "Legacy access outside an application lifetime was accepted");
        {
            BaseApp app;
            auto& root = app.GetEngineContext();
            Check(EngineContext::TryGet() == &root && !root.IsReady() && !root.MainWindow(),
                "Uninitialized root published rendering services");
            Check(&BaseApp::GetEngine() == &root.LegacyEngine() && &::GEngine::GEngine::Get() == &root.LegacyEngine(),
                "Compatibility access constructed a second engine");
            PlatformGone();
            rejected = false;
            rejected = !root.MakeCurrent();
            Check(rejected, "Rendering before initialization was accepted");
            rejected = false;
            try { BaseApp second; } catch (const std::logic_error&) { rejected = true; }
            Check(rejected && EngineContext::TryGet() == &root, "Second application replaced the active root");
        }
        Check(!EngineContext::TryGet(), "Uninitialized destruction retained the root");
        PlatformGone();
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
            auto& root = GetEngineContext();
            Check(root.IsReady() && root.MainWindow() == static_cast<SDLWindow*>(GetWindow())
                && GetWindowManager() && GetInputManager() && GetEventManager() && TTF_WasInit(),
                "Application resources initialized before rendering/platform services");
            Check(SDL_GL_GetCurrentContext() == static_cast<SDLWindow*>(root.MainWindow())->GetContext(), "Main context was not published current");
            bool wrongThreadRejected = false;
            int managerThreadRejections = 0;
            std::jthread worker([&] {
                // Actual owner-thread violations now terminate under invariant policy;
                // test_viewport.py runs those calls in isolated child processes.
                wrongThreadRejected = !GLContextThread::IsCurrentOwner();
                // Texture access now uses invariant rejection, covered in an isolated child process.
                if (!ShaderManager::GetShaderProgram({})) ++managerThreadRejections;
                try { (void)ShapeManager::GetShape("Box"); } catch (const std::logic_error&) { ++managerThreadRejections; }
            });
            worker.join();
            Check(wrongThreadRejected && SDL_GL_GetCurrentContext() == static_cast<SDLWindow*>(root.MainWindow())->GetContext(),
                "Worker reached ready rendering services or changed the owning context");
            Check(managerThreadRejections == 2, "Worker accessed a rendering manager");
            bool rejected = false;
            rejected = !root.Initialize({ Properties() });
            Check(rejected && root.IsReady(), "Repeated initialization replaced live services");
            // Exercise the root's submission facade with an empty production scene,
            // including real target binding and pixel output.
            Camera::PerspectiveCamera camera;
            RenderParam parameters;
            parameters.ClearColor = { 0.25f, 0.5f, 0.75f, 1.f };
            Check(root.RenderScene(m_Scene.get(), &camera, m_RenderTarget.get(), parameters).has_value(), "Root submission failed");
            Check(m_RenderTarget->BindAndBlitToScreen().has_value(), "Target resolve failed");
            glBindFramebuffer(GL_READ_FRAMEBUFFER, ::GEngine::FramebufferDetail::Backend::Name(m_RenderTarget->Buffer(::GEngine::RenderTargetSurface::Resolved)));
            std::array<unsigned char, 4> pixel{};
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
            Check(pixel[0] >= 63 && pixel[0] <= 64 && pixel[1] >= 127 && pixel[1] <= 128
                && pixel[2] >= 191 && pixel[2] <= 192, "Root renderer facade did not submit/clear its target");
            m_RenderTarget->UnBind();
            applicationTexture = std::make_unique<TextureOwner>(1);
            m_Scene->Add(new SceneActor);
            Watch(Kind::Buffer, m_UniformBufferObject->GetUBO(), 2, "BaseApp uniform buffer");
            Watch(Kind::Framebuffer, ::GEngine::FramebufferDetail::Backend::Name(m_FinalFrameBuffer->Buffer()), 2, "FinalFrameBuffer framebuffer");
            Watch(Kind::Texture, ::GEngine::FramebufferDetail::Backend::Color(m_FinalFrameBuffer->Buffer()), 2, "FinalFrameBuffer color texture");
            Watch(Kind::Texture, ::GEngine::FramebufferDetail::Backend::Color(m_FinalFrameBuffer->Buffer(), 1), 2, "FinalFrameBuffer picking texture");
            GLint depth = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, ::GEngine::FramebufferDetail::Backend::Name(m_FinalFrameBuffer->Buffer()));
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depth);
            Watch(Kind::Texture, static_cast<GLuint>(depth), 2, "FinalFrameBuffer depth texture");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            Watch(Kind::Framebuffer, ::GEngine::FramebufferDetail::Backend::Name(m_MousePickFrameBuffer->Buffer()), 2);
            Watch(Kind::Framebuffer, ::GEngine::FramebufferDetail::Backend::Name(m_PointShadowFrameBuffer->Buffer()), 2);
            Watch(Kind::Framebuffer, ::GEngine::FramebufferDetail::Backend::Name(m_CascadeShadowFrameBuffer->Buffer()), 2);
            Watch(Kind::Framebuffer, ::GEngine::FramebufferDetail::Backend::Name(m_RenderTarget->Buffer()), 2);
            ShapeManager::Register("phase18", new CachedGeometry);
            auto* text = AssetsManager::GetTextTexture("Lifecycle", RuntimeAssets::File("Fonts/OpenSans-Regular.ttf")).value();
            Watch(Kind::Texture, TextureName(text), 3, "AssetsManager cached text texture");
            AssetsManager::GetCascadedFrameBufferTexture(*m_CascadeShadowFrameBuffer).value();
            AssetsManager::GetPointShadowFrameBufferTexture(*m_PointShadowFrameBuffer).value();
            auto* gui = static_cast<SDLWindow*>(GetWindow())->GetImGuiWindow();
            gui->BeginRender(static_cast<SDLWindow*>(GetWindow()));
            ImGui::TextUnformatted("Phase 18");
            gui->EndRender(static_cast<SDLWindow*>(GetWindow()));
            Check(ImGui::GetIO().Fonts->TexID != nullptr, "ImGui font GPU resource was not exercised");
        }
        void ProcessInput(Timestep) override {}
        void Update(Timestep) override { ++updates; }
        void Render() override { ShutDown(); }
    };
    void Lifetimes(bool minimized, bool applicationFailure)
    {
        RootContract();
        RuntimeAssets::Initialize("GEngineEditor");
        if (applicationFailure)
        {
            struct FailingConstructor final : BaseApp
            {
                FailingConstructor()
                {
                    Check(Initialize(Properties()).has_value(), "Application initialization failed");
                    throw std::runtime_error("injected derived constructor failure");
                }
            };
            bool caught = false;
            try { FailingConstructor app; }
            catch (const std::runtime_error& error)
            {
                caught = std::string_view(error.what()) == "injected derived constructor failure";
            }
            Check(caught && !EngineContext::TryGet(), "Derived constructor failure retained its rendering root");
            PlatformGone();
        }
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            auto app = std::make_unique<ProbeApp>();
            Check(app->Initialize(Properties()).has_value(), "Application initialization failed");
            Hooks hooks;
            app->Prepare();
            const auto windowID = static_cast<SDLWindow*>(app->GetWindow())->GetWindowID();
            StartTeardownDiagnostics();
            if (minimized)
            {
                SDL_Event event{}; while (SDL_PollEvent(&event)) {}
                SDL_MinimizeWindow(static_cast<SDLWindow*>(app->GetWindow())->GetSDLWindow());
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
            Check(!EngineContext::TryGet() && !SDL_GetWindowFromID(windowID), "Application-owned root/platform survived destruction");
            PlatformGone();
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
        SDL_Window* firstWindow = nullptr;
        static void* Allocate(size_t size, void* user)
        {
            auto& self = *static_cast<FaultAllocator*>(user);
            auto* currentWindow = SDL_GL_GetCurrentWindow();
            if (!self.firstWindow) self.firstWindow = currentWindow;
            if (!self.injected && ImGui::GetCurrentContext() && ImGui::GetIO().BackendPlatformUserData
                && !ImGui::GetIO().BackendRendererUserData
                && currentWindow && currentWindow != self.firstWindow)
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
        BaseApp app;
        bool caught = false;
        if (platformBackend)
        {
            FaultAllocator fault;
            try { Check(app.Initialize({ Properties(), Properties() }).has_value(), "Application initialization failed"); }
            catch (const std::bad_alloc&) { caught = true; }
            Check(fault.injected, "Second-window SDL-backend allocation failure was not reached");
        }
        else
        {
            // The platform path reports asset-root failure without exception transport.
            const auto result = app.Initialize(Properties());
            caught = !result && std::holds_alternative<PlatformError>(result.error());
        }
        Check(caught, "Expected partial ImGui initialization failure");
        Check(!app.GetEngineContext().IsReady() && !app.GetEngineContext().MainWindow(),
            "Failed window construction published rendering services");
        Check(!SDL_GL_GetCurrentContext() && !ImGui::GetCurrentContext(), "Partial window initialization leaked a context");
        PlatformGone();
        ImGuiWindow_ neverInitialized; neverInitialized.ShutDown(); neverInitialized.ShutDown();
        SDLWindow noWindow; noWindow.ShutDown(); noWindow.ShutDown();
    }
    void PlatformFailure(bool noWindows)
    {
        bool caught = false;
        {
            BaseApp app;
            auto result = noWindows ? app.Initialize(std::initializer_list<WindowProperties>{}) : app.Initialize(Properties());
            caught = !result && std::holds_alternative<PlatformError>(result.error());
            PlatformGone(); // Rollback is immediate, even while the failed root lives.
            Check(!app.GetEngineContext().IsReady(), "Failed platform published services");
            Check(app.GetEngineContext().GetState() == EngineContext::State::Failed,
                "Failed root did not expose its terminal initialization state");
            bool retryRejected = false;
            retryRejected = !app.GetEngineContext().Initialize({ Properties() });
            Check(retryRejected, "Failed root accepted a second initialization attempt");
        }
        Check(caught, "Expected platform initialization failure");
        Check(!EngineContext::TryGet(), "Failed application retained the compatibility root");
        PlatformGone();
    }
    void WindowOwners()
    {
        Log::Initialize(); RuntimeAssets::Initialize("GEngineEditor");
        Check(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
        auto manager = WindowManager::GetScopedInstance();
        Check(manager->AddWindows(Properties()).has_value(), "Window addition failed");
        auto* first = static_cast<SDLWindow*>(manager->GetWindows().begin()->second.get());
        const auto firstID = first->GetWindowID(); const auto firstContext = first->GetContext();
        const auto firstGui = first->GetImGuiWindow()->GetContext();
        Check(manager->AddWindows(Properties()).has_value(), "Window addition failed");
        auto second = std::find_if(manager->GetWindows().begin(), manager->GetWindows().end(),
            [firstID](const auto& entry) { return entry.first != firstID; });
        const auto secondID = second->first;
        Check(first->BeginRender().has_value(), "Context transition failed");
        StartTeardownDiagnostics();
        manager->RemoveWindow(secondID);
        Check(SDL_GL_GetCurrentContext() == firstContext && ImGui::GetCurrentContext() == firstGui,
            "Secondary window destruction failed to restore the live owning contexts");
        manager->RemoveWindow(secondID);
        Check(manager->GetNumOfWindows() == 1 && manager->GetWindows().size() == 1, "Repeated removal corrupted window count");
        auto* gui = first->GetImGuiWindow();
        gui->BeginRender(first); ImGui::TextUnformatted("font upload"); Check(gui->EndRender(first).has_value(), "UI submission failed");
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
        glBindFramebuffer(GL_FRAMEBUFFER, ::GEngine::FramebufferDetail::Backend::Name(buffer.Buffer()));
        GLint depth = 0;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depth);
        return {::GEngine::FramebufferDetail::Backend::Name(buffer.Buffer()), ::GEngine::FramebufferDetail::Backend::Color(buffer.Buffer()), ::GEngine::FramebufferDetail::Backend::Color(buffer.Buffer(), 1), static_cast<GLuint>(depth)};
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
            auto source = FinalFrameBuffer::Create(16, 16).value(); auto target = FinalFrameBuffer::Create(32, 32).value();
            sourceNames = FramebufferNames(source); replacedNames = FramebufferNames(target);
            for (const auto& names : {sourceNames, replacedNames})
                for (size_t i = 0; i < names.size(); ++i) Watch(i ? Kind::Texture : Kind::Framebuffer, names[i], 2);
            glBindFramebuffer(GL_FRAMEBUFFER, ::GEngine::FramebufferDetail::Backend::Name(source.Buffer()));
            const GLfloat green[]{0.f, 1.f, 0.f, 1.f};
            glClearBufferfv(GL_COLOR, 0, green);
            FinalFrameBuffer moved(std::move(source));
            Check(!::GEngine::FramebufferDetail::Backend::Name(source.Buffer()) && !::GEngine::FramebufferDetail::Backend::Color(source.Buffer()) && !::GEngine::FramebufferDetail::Backend::Color(source.Buffer(), 1)
                && source.GetResolution() == Vec2f(0), "Framebuffer move constructor left an owning source");
            Check(FramebufferNames(moved) == sourceNames && moved.GetResolution() == Vec2f(16),
                "Framebuffer move constructor changed attachments or size");
            target = std::move(moved);
            Check(!::GEngine::FramebufferDetail::Backend::Name(moved.Buffer()) && !::GEngine::FramebufferDetail::Backend::Color(moved.Buffer()) && !::GEngine::FramebufferDetail::Backend::Color(moved.Buffer(), 1),
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
            Check(!::GEngine::FramebufferDetail::Backend::Name(target.Buffer()) && FramebufferNames(source) == sourceNames, "Moved-from framebuffer could not receive ownership");
            source = std::move(moved);
            Check(!::GEngine::FramebufferDetail::Backend::Name(source.Buffer()) && !::GEngine::FramebufferDetail::Backend::Color(source.Buffer()) && !::GEngine::FramebufferDetail::Backend::Color(source.Buffer(), 1),
                "Moving an empty framebuffer retained destination ownership");
            CheckRetired(sourceNames);
        }
        for (const auto& item : watched) Check(item.deletes == 1, "Framebuffer move lifetime deleted a resource incorrectly");
        CheckRetired(sourceNames); CheckRetired(replacedNames);
    }
    void CacheOwnership()
    {
        RuntimeAssets::Initialize("GEngineEditor");
        for (int cycle = 0; cycle < 2; ++cycle)
        {
            auto root = std::make_unique<EngineContext>();
            Check(root->Initialize({Properties(), Properties()}).has_value(), "Platform initialization failed");
            Check(ShapeManager::GetShape("phase20-retired") == nullptr, "New manager retained a previous map");
            Check(&root->Assets() == &root->Assets() && &root->Shapes() == &root->Shapes()
                && root->Shaders().has_value() && *root->Shaders() == *root->Shaders(), "Root manager instances changed during their lifetime");
            Hooks hooks;
            StartTeardownDiagnostics();
            std::vector<GLuint> owned;
            {
                auto cascade = CascadeShadowFrameBuffer::Create(16, 16, 3).value(); auto otherCascade = CascadeShadowFrameBuffer::Create(16, 16, 3).value();
                auto point = PointShadowFrameBuffer::Create(16, 16).value(); auto otherPoint = PointShadowFrameBuffer::Create(16, 16).value();
                auto* text = AssetsManager::GetTextTexture("Cache ownership",
                    RuntimeAssets::File("Fonts/OpenSans-Regular.ttf")).value();
                Check(AssetsManager::GetTextTexture("Cache ownership",
                    RuntimeAssets::File("Fonts/OpenSans-Regular.ttf")).value() == text, "Identical text was not cached");
                auto* otherText = AssetsManager::GetTextTexture("Different text",
                    RuntimeAssets::File("Fonts/OpenSans-Regular.ttf")).value();
                Check(otherText != text && TextureName(otherText) != TextureName(text),
                    "Different text replaced an existing borrower");
                const auto imagePath = std::filesystem::path(RuntimeAssets::File("Images/white.png"))
                    .lexically_normal().string();
                auto& windows = root->LegacyEngine().GetWindowManager()->GetWindows();
                auto otherWindow = std::find_if(windows.begin(), windows.end(), [&](const auto& entry) {
                    return entry.second.get() != root->MainWindow();
                });
                Check(otherWindow->second->BeginRender().has_value(), "Context transition failed");
                Check(SDL_GL_GetCurrentContext() != static_cast<SDLWindow*>(root->MainWindow())->GetContext(), "Secondary context was not activated");
                const auto uniform = "cycle-" + std::to_string(cycle);
                auto* image = AssetsManager::GetTexture("white", uniform).value();
                Check(SDL_GL_GetCurrentContext() == static_cast<SDLWindow*>(root->MainWindow())->GetContext()
                    && image->GetUniformName() == uniform, "Manager used a foreign context or retained a prior root cache");
                Check(AssetsManager::GetTexture("white").value()->View().Identity() == image->View().Identity()
                    && AssetsManager::GetTexture(imagePath).value()->View().Identity() == image->View().Identity(),
                    "Short/absolute image names did not reuse their owner");
                auto* fallback = AssetsManager::GetTextureOrFallback("phase20-missing-image").value();
                Check(fallback == AssetsManager::GetTextureOrFallback("phase20-missing-image").value() && glIsTexture(TextureName(fallback)),
                    "Missing ordinary image lost its checkerboard fallback/cache owner");
                owned = {TextureName(text), TextureName(otherText), TextureName(image), TextureName(fallback)};
                auto* cascadeBorrower = AssetsManager::GetCascadedFrameBufferTexture(cascade).value();
                auto* pointBorrower = AssetsManager::GetPointShadowFrameBufferTexture(point).value();
                Check(TextureName(AssetsManager::GetCascadedFrameBufferTexture(otherCascade).value()) == ::GEngine::FramebufferDetail::Backend::Depth(otherCascade.Buffer())
                    && TextureName(AssetsManager::GetPointShadowFrameBufferTexture(otherPoint).value()) == ::GEngine::FramebufferDetail::Backend::Depth(otherPoint.Buffer()),
                    "Second framebuffer request returned stale cached names");
                Check(TextureName(cascadeBorrower) == ::GEngine::FramebufferDetail::Backend::Depth(cascade.Buffer())
                    && TextureName(pointBorrower) == ::GEngine::FramebufferDetail::Backend::Depth(point.Buffer()), "New framebuffer request mutated existing borrowers");
                for (auto name : owned) Watch(Kind::Texture, name, 3);
                for (auto name : {::GEngine::FramebufferDetail::Backend::Depth(cascade.Buffer()), ::GEngine::FramebufferDetail::Backend::Depth(point.Buffer()),
                                 ::GEngine::FramebufferDetail::Backend::Depth(otherCascade.Buffer()), ::GEngine::FramebufferDetail::Backend::Depth(otherPoint.Buffer())})
                {
                    Check(glIsTexture(name), "A cache wrapper deleted a borrowed texture");
                    Watch(Kind::Texture, name, 1);
                }
            }
            for (auto name : owned) Check(glIsTexture(name), "Framebuffer cleanup retired an owned cache texture");
            root.reset();
            for (const auto& item : watched) Check(item.deletes == 1, "Cache resource was leaked or deleted more than once");
            PlatformGone();
            Check(observedFailures == 0 && debugErrors == 0, "Root-owned cache teardown failed");
            teardown = false;
        }
    }
    struct ShaderObjects
    {
        struct Object { GLuint name; bool program; int deletes = 0; };
        std::vector<Object> objects;
        inline static ShaderObjects* active = nullptr;
        PFNGLCREATEPROGRAMPROC createProgram = glad_glCreateProgram;
        PFNGLCREATESHADERPROC createShader = glad_glCreateShader;
        PFNGLDELETEPROGRAMPROC deleteProgram = glad_glDeleteProgram;
        PFNGLDELETESHADERPROC deleteShader = glad_glDeleteShader;
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        std::thread::id thread = std::this_thread::get_id();
        bool failProgram = false, failShader = false;
        static GLuint APIENTRY CreateProgram()
        {
            auto& self = *active;
            if (self.failProgram) { self.failProgram = false; return 0; }
            auto name = self.createProgram();
            if (name) self.objects.push_back({name, true});
            return name;
        }
        static GLuint APIENTRY CreateShader(GLenum type)
        {
            auto& self = *active;
            if (self.failShader) { self.failShader = false; return 0; }
            auto name = self.createShader(type);
            if (name) self.objects.push_back({name, false});
            return name;
        }
        void Deleted(GLuint name, bool program)
        {
            Observe(SDL_GL_GetCurrentContext() == context && std::this_thread::get_id() == thread,
                "Shader resource retired outside its owning context/thread");
            auto it = std::find_if(objects.rbegin(), objects.rend(), [=](const auto& object) {
                return object.name == name && object.program == program && object.deletes == 0;
            });
            Observe(it != objects.rend(), "Shader resource was deleted twice or never observed");
            if (it != objects.rend()) ++it->deletes;
        }
        static void APIENTRY DeleteProgram(GLuint name) { active->Deleted(name, true); active->deleteProgram(name); }
        static void APIENTRY DeleteShader(GLuint name) { active->Deleted(name, false); active->deleteShader(name); }
        ShaderObjects()
        {
            active = this;
            glad_glCreateProgram = CreateProgram; glad_glCreateShader = CreateShader;
            glad_glDeleteProgram = DeleteProgram; glad_glDeleteShader = DeleteShader;
        }
        ~ShaderObjects()
        {
            glad_glCreateProgram = createProgram; glad_glCreateShader = createShader;
            glad_glDeleteProgram = deleteProgram; glad_glDeleteShader = deleteShader;
            active = nullptr;
        }
        void RetiredSince(size_t first)
        {
            for (size_t i = first; i < objects.size(); ++i)
                Check(objects[i].deletes == 1, "Failed shader initialization leaked a GL object");
        }
    };
    void WriteFixture(const char* path, const char* source)
    {
        std::ofstream file(path, std::ios::trunc);
        file << source;
        Check(bool(file), "Failed to write shader fixture");
    }
    template<class F> void ExpectFailure(F&& operation, const char* message)
    {
        bool caught = false;
        try { operation(); } catch (const std::exception&) { caught = true; }
        Check(caught, message);
    }
    void ManagerFailures()
    {
        RuntimeAssets::Initialize("GEngineEditor");
        constexpr auto vertex = "#version 460 core\nvoid main(){gl_Position=vec4(0,0,0,1);}\n";
        constexpr auto fragment = "#version 460 core\nout vec4 color; void main(){color=vec4(1);}\n";
        for (int cycle = 0; cycle < 2; ++cycle)
        {
            auto root = std::make_unique<EngineContext>();
            Check(root->Initialize({Properties()}).has_value(), "Platform initialization failed");
            Check(root->GetState() == EngineContext::State::Ready, "Initialized manager root is not ready");
            Check(ShapeManager::GetShape("phase20-retired") == nullptr, "New shape manager contains stale state");
            int oldDestroyed = 0, duplicateDestroyed = 0, replacementDestroyed = 0;
            struct Counted final : Geometry
            {
                int& destroyed;
                explicit Counted(int& counter) : destroyed(counter) {}
                ~Counted() override { ++destroyed; }
            };
            auto* old = new Counted(oldDestroyed);
            ShapeManager::Register("phase20-retired", old);
            ShapeManager::Register("phase20-retired", old);
            ShapeManager::Register("phase20-retired", new Counted(duplicateDestroyed));
            Check(duplicateDestroyed == 1 && oldDestroyed == 0 && ShapeManager::GetShape("phase20-retired") == old,
                "Duplicate registration leaked its candidate or invalidated the original borrower");
            ExpectFailure([&] { ShapeManager::Register("alias", old); }, "Same geometry acquired two owners");
            ShapeManager::UnRegister("phase20-retired");
            ShapeManager::UnRegister("phase20-retired");
            Check(!ShapeManager::GetShape("phase20-retired") && oldDestroyed == 0,
                "Unregister invalidated an existing borrower");
            auto* replacement = new Counted(replacementDestroyed);
            ShapeManager::Register("phase20-retired", replacement);
            Check(ShapeManager::GetShape("phase20-retired") == replacement && oldDestroyed == 0,
                "Explicit replacement destroyed a retired borrower");
            ExpectFailure([&] { ShapeManager::Register("alias", old); }, "Retired geometry acquired a second owner");
            ExpectFailure([] { ShapeManager::GetModel("phase20-missing-model"); }, "Missing model was published");

            const auto fontPath = std::filesystem::absolute("phase20-font.ttf");
            std::filesystem::remove(fontPath); // Fixture-owned file in the isolated runtime directory.
            Check(!AssetsManager::GetFont(fontPath.string()), "Missing font did not fail recoverably");
            Check(!AssetsManager::GetFont(fontPath.string()), "Failed font was cached");
            std::filesystem::copy_file(RuntimeAssets::File("Fonts/OpenSans-Regular.ttf"), fontPath);
            auto* font = AssetsManager::GetFont(fontPath.string()).value();
            Check(font && AssetsManager::GetFont(fontPath.string()).value() == font,
                "Retry after missing font failed or created stale font state");
            static_assert(!std::is_copy_constructible_v<Asset::Font>); // Loaded fonts expose no replacement/native map API.
            Check(!AssetsManager::GetTextTexture("text", fontPath.string(), 13),
                "Unsupported text size did not fail recoverably");
            Check(!AssetsManager::GetTextTexture("", fontPath.string()),
                "Failed SDL text surface was published");
            auto* text = AssetsManager::GetTextTexture("valid text", fontPath.string()).value();
            Check(glIsTexture(TextureName(text)), "Valid text failed after a prior text load failure");
            Asset::TextureDesc hdr; hdr.format = Asset::TextureFormat::RGB16Float;
            hdr.colorSpace = Asset::TextureColorSpace::Linear; hdr.mips = Asset::TextureMipIntent::None;
            Check(!AssetsManager::GetTexture("phase20-missing.hdr", "", "", hdr),
                "Failed HDR load published an empty resource");
            auto* fallback = AssetsManager::GetTextureOrFallback("phase20-missing.hdr").value();
            Check(glIsTexture(TextureName(fallback)), "Failed image cache entry prevented successful fallback retry");

            ShaderObjects shaderObjects;
            // Expected compiler/linker diagnostics are checked as failure results;
            // they are not counted as teardown GL errors.
            teardown = false;
            auto failsWithoutLeaks = [&](const ShaderManager::Files& files) {
                const auto first = shaderObjects.objects.size();
                auto failed = ShaderManager::GetShaderProgram(files);
                Check(!failed && !failed.error().log.empty(), "Invalid shader request was cached or lost diagnostics");
                shaderObjects.RetiredSince(first);
            };
            WriteFixture("phase20.vert", vertex);
            std::filesystem::remove("phase20.frag");
            failsWithoutLeaks({"phase20.vert", "phase20.frag"});
            failsWithoutLeaks({});
            failsWithoutLeaks({"phase20.bad-extension"});
            WriteFixture("phase20.frag", "#version 460 core\nthis is not valid GLSL\n");
            failsWithoutLeaks({"phase20.vert", "phase20.frag"});
            WriteFixture("phase20.vert", "#version 460 core\nout vec3 mismatch; void main(){mismatch=vec3(1); gl_Position=vec4(0,0,0,1);}\n");
            WriteFixture("phase20.frag", "#version 460 core\nin vec4 mismatch; out vec4 color; void main(){color=mismatch;}\n");
            failsWithoutLeaks({"phase20.vert", "phase20.frag"});
            WriteFixture("phase20.vert", vertex); WriteFixture("phase20.frag", fragment);
            shaderObjects.failProgram = true;
            failsWithoutLeaks({"phase20.vert", "phase20.frag"});
            shaderObjects.failShader = true;
            failsWithoutLeaks({"phase20.vert", "phase20.frag"});
            auto createdShader = ShaderManager::GetShaderProgram({"phase20.vert", "phase20.frag"});
            Check(createdShader.has_value(), "Valid shader retry failed");
            auto* shader = *createdShader;
            Check(shader->IsLinked() && glIsProgram(::GEngine::Asset::ShaderBackendAccess::Program(*shader)), "Valid shader retry after failed initialization failed");
            Check(ShaderManager::GetShaderProgram({"phase20.vert", "phase20.frag"}) == shader,
                "Successful shader lookup replaced a live borrower");
            auto failedMaterial = Material::Create<BasicMaterial>("phase20.vert", "missing.frag");
            Check(!failedMaterial && failedMaterial.error().code == Asset::ShaderErrorCode::FileRead
                && failedMaterial.error().source == "missing.frag", "Material factory lost shader cause");
            auto failedTextured = Material::Create<TextureMaterial>(*fallback, "phase20.vert", "missing.frag");
            Check(!failedTextured && failedTextured.error().shaderType == Asset::FRAGMENT,
                "Derived material construction did not propagate shader failure");
            auto validMaterial = Material::Create<BasicMaterial>("phase20.vert", "phase20.frag");
            Check(validMaterial.has_value(), "Material retry failed after shader error");
            validMaterial->reset();
            WriteFixture("other.vert", vertex);
            failsWithoutLeaks({"other.vert", "missing.frag"});
            Check(shader->IsLinked() && glIsProgram(::GEngine::Asset::ShaderBackendAccess::Program(*shader)), "Unrelated load failure retired a valid shader borrower");
            StartTeardownDiagnostics();
            root.reset();
            shaderObjects.RetiredSince(0);
            Check(oldDestroyed == 1 && replacementDestroyed == 1 && duplicateDestroyed == 1,
                "Manager shutdown did not retire current/old/duplicate geometry exactly once");
            PlatformGone();
            Check(observedFailures == 0 && debugErrors == 0, "Manager failure or shutdown emitted unexpected diagnostics");
            teardown = false;
        }
        for (bool shaderFailure : {false})
        {
            struct FailingApp final : BaseApp
            {
                explicit FailingApp(bool shaderFailure)
                {
                    Check(Initialize(Properties()).has_value(), "Application initialization failed");
                    AssetsManager::GetTexture("white").value();
                    (void)shaderFailure;
                    AssetsManager::GetFont("phase20-missing-font.ttf").value();
                }
            };
            ExpectFailure([&] { FailingApp app(shaderFailure); }, "Asset/shader application initialization unexpectedly succeeded");
            Check(!EngineContext::TryGet(), "Asset/shader constructor failure retained the owning root");
            PlatformGone();
        }
        {
            struct ShaderFailingApp final : BaseApp
            {
                ApplicationInitializationResult Initialize(const std::initializer_list<WindowProperties>& properties) override
                {
                    if (auto initialized = BaseApp::Initialize(properties); !initialized) return initialized;
                    auto material = Material::Create<TextureMaterial>(*AssetsManager::GetTexture("white").value(), "phase20.vert", "missing.frag");
                    if (!material) return std::unexpected(material.error());
                    return {};
                }
            };
            auto app = std::make_unique<ShaderFailingApp>();
            auto initialized = app->Initialize({Properties()});
            Check(!initialized, "Shader failure did not reach typed application startup");
            const auto* error = std::get_if<Asset::ShaderError>(&initialized.error());
            Check(error && error->code == Asset::ShaderErrorCode::FileRead && error->shaderType == Asset::FRAGMENT
                && error->source == "missing.frag" && !error->log.empty(), "Application error lost shader fields");
            app.reset();
            Check(!EngineContext::TryGet(), "Typed shader startup failure retained its root");
            PlatformGone();
        }

    }
    void ResourceMoves()
    {
        Log::Initialize(); RuntimeAssets::Initialize("GEngineEditor");
        Check(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
        Check(TTF_Init() == 0, TTF_GetError());
        {
            SDLWindow window; Check(window.Initialize(Properties()).has_value(), "Window initialization failed");
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

    void ContextThread()
    {
        RuntimeAssets::Initialize("GEngineEditor");
        Check(!GLContextThread::IsCurrentOwner(), "Context permission existed before initialization");
        for (int cycle = 0; cycle != 2; ++cycle)
        {
            {
                auto properties = Properties();
                properties.ImGuiWindowProperties.bViewPortEnabled = true;
                BaseApp app; Check(app.Initialize(properties).has_value(), "Application initialization failed");
                auto* window = static_cast<SDLWindow*>(app.GetWindow());
                Check(GLContextThread::IsCurrentOwner(), "Owner context was not registered");
                bool workerRejected = false;
                std::jthread worker([&] { workerRejected = !GLContextThread::IsCurrentOwner(); });
                worker.join();
                Check(workerRejected, "Validation-only query allowed a worker without issuing GL");
                Check(window->NullRender().has_value(), "Context transition failed");
                Check(!GLContextThread::IsCurrentOwner(), "Detached context retained permission");
                Check(window->BeginRender().has_value(), "Context transition failed");
                Check(GLContextThread::IsCurrentOwner(), "Owner could not restore its context");
                while (glGetError() != GL_NO_ERROR) {} // Existing target initialization diagnostics.
                {
                    const std::array<float, 4> initial{1.f, 2.f, 3.f, 4.f};
                    UniformBufferObject<UniformType::VEC4F> buffer(1);
                    glBindBuffer(GL_UNIFORM_BUFFER, buffer.GetUBO());
                    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(initial), initial.data());
                    const std::array<float, 2> update{8.f, 9.f};
                    glBufferSubData(GL_UNIFORM_BUFFER, sizeof(float), sizeof(update), update.data());
                    std::array<float, 4> result{};
                    glGetBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(result), result.data());
                    Check(result == std::array<float, 4>{1.f, 8.f, 9.f, 4.f}, "Owner upload/readback changed payload");
                }
                auto* gui = window->GetImGuiWindow();
                for (int frame = 0; frame != 3; ++frame)
                {
                    gui->BeginRender(window);
                    const auto* main = ImGui::GetMainViewport();
                    ImGui::SetNextWindowPos(ImVec2(main->Pos.x + main->Size.x + 40.f, main->Pos.y));
                    ImGui::SetNextWindowSize(ImVec2(128.f, 96.f));
                    ImGui::Begin("Context owner viewport"); ImGui::TextUnformatted("Owner thread"); ImGui::End();
                    Check(gui->EndRender(window).has_value(), "UI submission failed");
                }
                Check(ImGui::GetPlatformIO().Viewports.Size > 1, "ImGui secondary context was not exercised");
                Check(GLContextThread::IsCurrentOwner(), "ImGui failed to restore the owner context");
                Check(glGetError() == GL_NO_ERROR, "Correct-thread operations emitted a GL error");
            }
            Check(!GLContextThread::IsCurrentOwner(), "Teardown retained context permission");
            PlatformGone();
        }
    }

    // Each death case runs in a disposable process. The expected termination
    // handler is installed only after valid initialization; driver sentinels exit
    // with a different code if rejection ever forwards an illegal call to GL.
    void APIENTRY ForbiddenGen(GLsizei, GLuint*) { std::_Exit(87); }
    void APIENTRY ForbiddenUpload(GLenum, GLintptr, GLsizeiptr, const void*) { std::_Exit(87); }
    void APIENTRY ForbiddenDelete(GLsizei, const GLuint*) { std::_Exit(87); }
    void APIENTRY ForbiddenDraw(GLenum, GLint, GLsizei) { std::_Exit(87); }
    void APIENTRY ForbiddenRead(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) { std::_Exit(87); }

    void WrongThread(std::string_view mode)
    {
        RuntimeAssets::Initialize("GEngineEditor");
        auto app = std::make_unique<BaseApp>(); Check(app->Initialize(Properties()).has_value(), "Application initialization failed");
        auto* window = static_cast<SDLWindow*>(app->GetWindow());
        glad_glGenBuffers = ForbiddenGen;
        glad_glBufferSubData = ForbiddenUpload;
        glad_glDeleteBuffers = ForbiddenDelete;
        glad_glDrawArrays = ForbiddenDraw;
        glad_glReadPixels = ForbiddenRead;
        const auto attempt = [&] {
            // Install on the thread under test as well as the detached-owner case.
            std::set_terminate([] { std::_Exit(86); });
            GLuint id = 0;
            std::array<unsigned char, 4> pixel{};
            if (mode == "--reject-create") { UniformBufferObject<UniformType::VEC4F> buffer(1); }
            else if (mode == "--reject-upload") glBufferSubData(GL_ARRAY_BUFFER, 0, 1, pixel.data());
            else if (mode == "--reject-delete") glDeleteBuffers(1, &id);
            else if (mode == "--reject-submit") glDrawArrays(GL_TRIANGLES, 0, 3);
            else if (mode == "--reject-readback" || mode == "--reject-detached")
                glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
            else if (mode == "--reject-imgui") window->GetImGuiWindow()->BeginRender(window);
            else if (mode == "--reject-context-create") SDL_GL_CreateContext(window->GetSDLWindow());
            else if (mode == "--reject-context-switch") SDL_GL_MakeCurrent(window->GetSDLWindow(), window->GetContext());
            else if (mode == "--reject-context-delete") SDL_GL_DeleteContext(window->GetContext());
            else if (mode == "--reject-window-teardown") window->ShutDown();
            else if (mode == "--reject-root-teardown") app.reset();
            else std::_Exit(88);
            std::_Exit(89); // A rejected operation returned instead of terminating.
        };
        if (mode == "--reject-detached") { Check(window->NullRender().has_value(), "Detach failed"); attempt(); }
        std::jthread worker(attempt);
        worker.join();
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
        else if (mode == "--manager-failures") ManagerFailures();
        else if (mode == "--context-thread") ContextThread();
        else if (mode.starts_with("--reject-")) WrongThread(mode);
        else throw std::invalid_argument("Unknown shutdown probe mode");
        Check(observedFailures == 0 && debugErrors == 0, "Observed lifecycle failures");
        std::cout << "[PASS] shutdown " << mode << " checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] shutdown: " << error.what() << '\n';
        return 1;
    }
}
