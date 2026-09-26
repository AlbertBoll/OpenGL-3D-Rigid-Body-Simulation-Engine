#if defined(SHAPE_STARTUP_SCHEMA_ONLY) || defined(SHAPE_STARTUP_ENTRY_PROBE)
#include "Core/GEngine.h"
#include "Core/RenderTarget.h"
#include <type_traits>
static_assert(std::is_constructible_v<::GEngine::ApplicationInitializationError, ::GEngine::ShapeRegistrationError>);
static_assert(std::same_as<decltype(std::declval<::GEngine::EngineContext&>().Initialize({})), ::GEngine::EngineInitializationResult>);
#ifndef SHAPE_STARTUP_SCHEMA_ONLY
#include "gepch.h"
#include "Core/BaseApp.h"
#include "Core/Scene.h"
#include <sdl2/SDL_ttf.h>
#include <print>
#define main ProductionEntryPoint
#include "EntryPoint.h"
#undef main
namespace Fixture
{
    bool fail=false,valid=true;
    unsigned runs=0,destructors=0;
    void Check(bool ok,const char* reason){if(!ok){valid=false;std::println("[FAIL] {}",reason);}}
    class App final : public ::GEngine::BaseApp
    {
    public:
        ::GEngine::ApplicationInitializationResult Initialize(const std::initializer_list<::GEngine::WindowProperties>& props) override
        {
            auto initialized=BaseApp::Initialize(props);if(!initialized)return initialized;
            if(fail)
            {
                ::GEngine::ShapeRegistrationError error{::GEngine::ShapeRegistrationErrorCode::AlreadyOwned,
                    "ShapeRegistrationFixture::Register","fixture-shape","Geometry already belongs to this manager; detail=retired owner 37"};
                ::GEngine::EngineInitializationResult rootResult = std::unexpected(error);
                auto result = std::visit([](const auto& failure) -> ::GEngine::ApplicationInitializationResult
                    { return std::unexpected(failure); }, rootResult.error());
                auto transport = std::move(result.error());
                const auto* observed=std::get_if<::GEngine::ShapeRegistrationError>(&transport);
                Check(observed && observed->code==error.code && observed->operation==error.operation
                    && observed->name==error.name && observed->message==error.message,"Startup variant lost registration diagnostics");
                return std::unexpected(std::move(transport));
            }
            return {};
        }
        ::GEngine::ApplicationRunResult Run() override {++runs;return {};}
        ~App() override {++destructors;Check(SDL_GL_GetCurrentContext()!=nullptr,"Application retired after its context");}
    };
}
::GEngine::WindowProperties winProp=[] {
    ::GEngine::WindowProperties p;p.m_Title="Shape startup error validation";p.m_Width=p.m_Height=64;
    p.m_MinWidth=p.m_MinHeight=32;p.m_IsVsync=false;
    p.flag=::GEngine::BitFlags<::GEngine::WindowFlags,uint8_t>{::GEngine::WindowFlags::INVISIBLE};return p;
}();
::GEngine::BaseApp* CreateApp(){return new Fixture::App;}
int main(int argc,char* argv[])
{
    if(argc!=2)return 2;
    const std::string_view mode=argv[1];if(mode!="success"&&mode!="failure")return 2;
    Fixture::fail=mode=="failure";SDL_SetMainReady();
    const int status=ProductionEntryPoint(argc,argv);
    Fixture::Check(status==(Fixture::fail?1:0),"Actual EntryPoint exit status changed");
    Fixture::Check(Fixture::runs==(Fixture::fail?0u:1u)&&Fixture::destructors==1,"Post-failure Run or missing application retirement");
    Fixture::Check(!::GEngine::EngineContext::TryGet() && SDL_WasInit(0)==0 && TTF_WasInit()==0,"Root/platform survived registration failure");
    if(!Fixture::valid)return 97;
    std::println("[PASS] shape startup {} exit={} runs={} teardown=1",mode,status,Fixture::runs);return status;
}
#endif

#else
#include "gepch.h"
#include "Core/BaseApp.h"
#include "Core/RuntimeAssets.h"
#include "Camera/PerspectiveCamera.h"
#include "Windows/SDLWindow.h" // Test-only native event injection, outside the consumer boundary.
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <print>
#include <new>

static int denyPlatformAllocation = -1;
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    if (denyPlatformAllocation == 0) { denyPlatformAllocation = -1; return nullptr; }
    if (denyPlatformAllocation > 0) --denyPlatformAllocation;
    return std::malloc(size ? size : 1);
}
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace
{
    using namespace GEngine;
    unsigned checks = 0;
    template<class T> requires requires(const T& value) { static_cast<bool>(value); }
    void Check(const T& value, const char* message)
    {
        ++checks;
        if (!static_cast<bool>(value)) { std::println(stderr, "[FAIL] {}", message); std::exit(1); }
    }
    WindowProperties Properties()
    {
        WindowProperties p;
        p.m_Title = "Phase 30 viewport probe";
        p.m_Width = 320; p.m_Height = 240; p.m_MinWidth = 1; p.m_MinHeight = 1;
        p.m_IsVsync = false;
        p.flag = {WindowFlags::INVISIBLE, WindowFlags::RESIZABLE};
        p.ImGuiWindowProperties.bDockingEnabled = true;
        p.ImGuiWindowProperties.bViewPortEnabled = true;
        return p;
    }
    struct App : BaseApp
    {
        Camera::PerspectiveCamera Camera{45.0f, 1.0f, 0.1f, 100.0f};
        App() { m_EditorCamera = &Camera; }
        RenderTarget& Target() { return *m_RenderTarget; }
        MousePickFrameBuffer& Picking() { return *m_MousePickFrameBuffer; }
        void Resize(EditorViewportLogicalSize logical, FramebufferScale scale)
        {
            Check(SetEditorViewport(logical, scale), "viewport conversion");
            Check(ResizeViewportTargets(), "target resize");
        }
    };
    SDLWindow* Native(BaseApp& app) { return static_cast<SDLWindow*>(app.GetWindow()); }

    void PushState(App& app, Uint8 change, int w = 0, int h = 0)
    {
        SDL_Event e{}; e.type = SDL_WINDOWEVENT; e.window.windowID = app.GetWindow()->GetWindowID();
        e.window.event = change; e.window.data1 = w; e.window.data2 = h;
        Check(SDL_PushEvent(&e) == 1, "event injection"); app.PollEvents();
    }

    void DockCycle(App& app)
    {
        const auto original = app.GetWindow()->GetLogicalSize();
        bool sawDocked = false, sawDetached = false, sawRedocked = false;
        ImGui::GetIO().ConfigViewportsNoAutoMerge = true;
        for (int frame = 0; frame != 12; ++frame)
        {
            app.PollEvents();
            Check(app.GetWindow()->BeginUI(), "begin UI");
            auto* main = ImGui::GetMainViewport();
            const auto dock = ImGui::DockSpaceOverViewport(main);
            const bool detached = frame >= 4 && frame < 8;
            ImGui::SetNextWindowDockID(detached ? 0 : dock, ImGuiCond_Always);
            if (detached)
            {
                ImGui::SetNextWindowPos({main->Pos.x + main->Size.x + 40, main->Pos.y + 40}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({180, 160}, ImGuiCond_Always);
            }
            ImGui::Begin("Phase 30 panel", nullptr, ImGuiWindowFlags_NoSavedSettings);
            auto* viewport = ImGui::GetWindowViewport();
            const bool onMain = viewport->ID == main->ID;
            if (frame == 3) sawDocked = onMain && ImGui::IsWindowDocked();
            if (frame == 7) sawDetached = !onMain && !ImGui::IsWindowDocked();
            if (frame == 11) sawRedocked = onMain && ImGui::IsWindowDocked();
            if (viewport->PlatformHandle)
            {
                auto scale = UI::CurrentViewportFramebufferScale();
                Check(scale, "backing-window DPI query");
                int lw, lh, pw, ph;
                auto* backing = static_cast<SDL_Window*>(viewport->PlatformHandle);
                SDL_GetWindowSize(backing, &lw, &lh); SDL_GL_GetDrawableSize(backing, &pw, &ph);
                Check(std::abs(scale->X - float(pw) / lw) < 0.0001f
                    && std::abs(scale->Y - float(ph) / lh) < 0.0001f, "DPI comes from panel backing window");
                const auto available = ImGui::GetContentRegionAvail();
                Check(app.SetEditorViewport({std::max(0.0f, available.x), std::max(0.0f, available.y)}, *scale), "publish panel dimensions");
            }
            ImGui::TextUnformatted("Independent panel dimensions");
            ImGui::End();
            Check(app.GetWindow()->EndUI(), "end UI and restore owning context");
            Check(app.GetWindow()->IsCurrent(), "main context restored after detached submission");
            Check(app.ResizeViewportTargets(), "coalesced dock resize");
            Check(app.GetWindow()->GetLogicalSize() == original, "docking never changes native dimensions");
        }
        Check(sawDocked && sawDetached && sawRedocked, "actual dock / detached native viewport / redock transition");
    }

    void Viewports()
    {
        Check(!ToFramebufferPixels({-1, 2}, {1, 1}), "negative panel rejected");
        Check(!ToFramebufferPixels({1, 2}, {0, 1}), "zero scale rejected");
        Check(!ToFramebufferPixels({std::numeric_limits<float>::infinity(), 2}, {1, 1}), "infinite panel rejected");
        Check(!ToFramebufferPixels({8192, 2}, {2, 1}), "scaled overflow rejected before allocation");
        Check(ToFramebufferPixels({0, 20}, {2, 2})->Width == 0, "zero panel defers storage");
        for (int cycle = 0; cycle != 2; ++cycle)
        {
            App app; Check(app.Initialize(Properties()), "application initialization");
            Check(app.GetWindow()->IsCurrent(), "main window current");
            const auto native = app.GetWindow()->GetLogicalSize();
            app.Resize({80, 50}, {2, 2});
            Check(app.GetEditorViewportLogicalSize() == EditorViewportLogicalSize{80, 50}, "logical state retained");
            Check(app.GetEditorViewportPixelSize() == EditorViewportPixelSize{160, 100}, "high DPI pixel state");
            Check(app.Target().GetWidth() == 160 && app.Target().GetHeight() == 100, "real high DPI storage");
            Check(app.GetWindow()->GetLogicalSize() == native, "editor dimensions independent");
            Check(app.Target().Description().SizeSource == TargetSizeSource::EditorViewport, "explicit editor size source");
            const auto count = app.Target().ReallocationCount();
            for (int n = 0; n != 100; ++n) app.Resize({80.01f, 50.01f}, {2, 2});
            Check(app.Target().ReallocationCount() == count, "fractional jitter does not reallocate");
            for (int n = 0; n != 25; ++n)
                Check(app.SetEditorViewport({float(81 + n), 60}, {1.25f, 1.5f}), "drag publishes size");
            Check(app.ResizeViewportTargets(), "apply latest drag sample");
            Check(app.Target().ReallocationCount() == count + 1, "drag coalesces to one allocation transaction");
            Check(app.Target().GetWidth() == 131 && app.Target().GetHeight() == 90, "fractional DPI rounds once");
            SDL_SetWindowSize(Native(app)->GetSDLWindow(), 640, 360);
            app.PollEvents();
            app.Resize({100, 160}, {1, 1});
            Check(app.GetWindow()->GetLogicalSize() == NativeWindowLogicalSize{640, 360}, "native dimensions refreshed independently");
            Check(std::abs(app.Camera.GetCameraSetting().m_PerspectiveSetting.m_AspectRatio - 0.625f) < 0.0001f,
                "camera uses actual target aspect, not native aspect");
            auto invalid = app.SetEditorViewport({9000, 1}, {1, 1});
            Check(!invalid && invalid.error().code == PlatformErrorCode::InvalidSize, "typed conversion failure");
            Check(app.Target().GetWidth() == 100, "conversion failure preserves previous target");
            Check(app.Picking().ClearAttachment(0, 73), "integer target clear");
            const auto position = ViewportPixelAt(99.99f, 0, {100, 160}, {100, 160});
            Check(position && position->X == 99 && position->Y == 159, "top-right maps inside target");
            Check(app.Picking().ReadPixel(position->X, position->Y).value_or(-1) == 73, "actual integer pixel readback");
            Check(!ViewportPixelAt(100, 0, {100, 160}, {100, 160}), "exclusive right edge");
            Check(!ViewportPixelAt(0, 160, {100, 160}, {100, 160}), "exclusive bottom edge");
            app.Resize({0, 0}, {2, 2});
            const auto zeroCount = app.Target().ReallocationCount();
            Check(!app.Target() && !app.HasVisibleViewport(), "zero panel releases storage and suspends scene passes");
            for (int n = 0; n != 20; ++n) app.Resize({0, 0}, {2, 2});
            Check(app.Target().ReallocationCount() == zeroCount, "zero panel has no allocation storm");
            app.Resize({70, 40}, {2, 2});
            Check(app.HasVisibleViewport() && app.Target().ReallocationCount() == zeroCount + 1, "panel reopen restores storage once");
            PushState(app, SDL_WINDOWEVENT_SHOWN);
            PushState(app, SDL_WINDOWEVENT_MINIMIZED);
            Check(app.IsRenderingSuspended(), "native minimize independent of panel");
            PushState(app, SDL_WINDOWEVENT_RESTORED);
            PushState(app, SDL_WINDOWEVENT_SIZE_CHANGED, 0, 0);
            Check(app.IsRenderingSuspended(), "native zero-size suspension");
            PushState(app, SDL_WINDOWEVENT_SIZE_CHANGED, 640, 360);
            Check(!app.IsRenderingSuspended(), "native restore resumes");
            Check(app.Target().GetWidth() == 140, "native events do not overwrite editor storage");
            auto fixed = RenderTarget::Create(8, 6, 1); Check(fixed, "fixed target factory");
            Check(fixed->ResizeFrom({640, 360}, {140, 80}), "fixed size source ignores ambient dimensions");
            Check(fixed->GetWidth() == 8 && fixed->GetHeight() == 6, "fixed target preserved");
            auto desc = fixed->Description(); desc.SizeSource = TargetSizeSource::NativeFramebuffer;
            Check(fixed->Reconfigure(desc), "native size source");
            Check(fixed->ResizeFrom({32, 18}, {10, 16}), "apply native size source");
            Check(fixed->GetWidth() == 32 && fixed->GetHeight() == 18, "native source selects framebuffer pixels");
            DockCycle(app);
            PushState(app, SDL_WINDOWEVENT_CLOSE);
        }
        Check(SDL_WasInit(0) == 0 && SDL_GL_GetCurrentContext() == nullptr, "two lifetimes drain platform and context");
        std::println("[PASS] viewport checks={} cycles=2 dpi=1.25,1.5,2 dock-undock-redock=real", checks);
    }

    void StartupErrors(bool initializeAssets)
    {
        if (!initializeAssets)
        {
            App app; auto result = app.Initialize(Properties());
            Check(!result && std::holds_alternative<PlatformError>(result.error()), "asset error uses typed platform transport");
            Check(std::get<PlatformError>(result.error()).code == PlatformErrorCode::ResourcePath, "asset path classification");
            Check(!app.GetEngineContext().IsReady() && SDL_WasInit(0) == 0, "failed UI initialization rolls back immediately");
            std::println("[PASS] asset-root-error"); return;
        }
        for (int allocation = 0; allocation < 3; ++allocation)
        {
            App app; denyPlatformAllocation = allocation;
            auto result = app.Initialize(Properties());
            Check(!result && std::holds_alternative<PlatformError>(result.error()), "typed platform allocation failure");
            Check(std::get<PlatformError>(result.error()).code == PlatformErrorCode::Allocation
                && denyPlatformAllocation == -1, "platform allocation injection reached requested owner");
            Check(!app.GetEngineContext().IsReady() && SDL_WasInit(0) == 0, "platform allocation failure rolls back immediately");
        }
        {
            App app;
            Check(!app.GetEngineContext().MakeCurrent(), "uninitialized activation returns typed error");
            auto resize = app.ResizeViewportTargets();
            Check(!resize && resize.error().code == FramebufferErrorCode::InvalidOperation, "uninitialized resize returns typed error");
            auto result = app.Initialize(std::initializer_list<WindowProperties>{});
            Check(!result && std::holds_alternative<PlatformError>(result.error()), "empty window list typed failure");
            Check(!app.GetEngineContext().IsReady() && SDL_WasInit(0) == 0, "empty startup leaves no platform");
        }
        {
            App app; auto invalid = Properties(); invalid.m_Width = 0;
            auto result = app.Initialize({Properties(), invalid});
            Check(!result && std::get<PlatformError>(result.error()).code == PlatformErrorCode::InvalidSize, "second window typed failure");
            Check(SDL_WasInit(0) == 0 && !app.GetEngineContext().MainWindow(), "partial multiwindow startup cleans all owners");
        }
        {
            App app; Check(app.Initialize(Properties()), "retry with new root succeeds");
            auto* windows = BaseApp::GetWindowManager();
            auto found = windows->GetInternalWindow(app.GetWindow()->GetWindowID());
            Check(found && *found == app.GetWindow(), "window lookup preserves semantic owner");
            auto missing = windows->GetInternalWindow(0);
            Check(!missing && missing.error().code == PlatformErrorCode::WindowNotFound, "unknown window lookup returns typed error");
            auto invalid = Properties(); invalid.m_Width = 0;
            auto result = BaseApp::GetWindowManager()->AddWindows({Properties(), invalid});
            Check(!result, "additional window batch failure");
            Check(BaseApp::GetWindowManager()->GetWindows().size() == 1 && app.GetWindow()->IsCurrent(), "batch rollback preserves existing owner and context");
            auto repeated = app.GetEngineContext().Initialize({Properties()});
            const auto* platform = repeated ? nullptr : std::get_if<PlatformError>(&repeated.error());
            Check(platform && platform->code == PlatformErrorCode::InvalidState, "repeated initialization typed failure");
            Check(app.GetEngineContext().IsReady(), "rejected repeat does not tear down live root");
        }
        Check(SDL_WasInit(0) == 0, "failure/success lifetimes drain platform");
        std::println("[PASS] startup-errors checks={}", checks);
    }
}

int main(int argc, char** argv)
{
    const std::string_view mode = argc > 1 ? argv[1] : "--viewport";
    if (mode != "--asset-root-error") RuntimeAssets::Initialize("GEngineEditor");
    if (mode == "--viewport") Viewports();
    else if (mode == "--startup-errors") StartupErrors(true);
    else if (mode == "--asset-root-error") StartupErrors(false);
    else if (mode == "--reject-worker" || mode == "--reject-root-worker")
    {
        App app; Check(app.Initialize(Properties()), "worker guard setup");
        std::set_terminate([] { std::println(stderr, "[PASS] reject-worker ownership invariant"); std::fflush(stderr); std::_Exit(86); });
        std::thread worker([&] {
            std::set_terminate([] { std::println(stderr, "[PASS] reject-worker ownership invariant"); std::fflush(stderr); std::_Exit(86); });
            if (mode == "--reject-root-worker") (void)app.GetEngineContext().MakeCurrent();
            else app.GetWindow()->RefreshDimensions();
        }); worker.join();
        Check(false, "foreign thread reached backend");
    }
    else return 2;
    return 0;
}

#endif
