#pragma once

// Native injection and private provider observation are confined to this diagnostic TU.
#include <glad/glad.h>
#include <sdl2/SDL.h>
#include <sdl2/SDL_syswm.h>
#include <imgui/imgui_internal.h>
#include "Managers/InputManager.h"
#include "Managers/EventManager.h"
#include "Core/BaseApp.h"
#include "Core/Window.h"
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <thread>

namespace PreEditorInput
{
    using namespace ::GEngine;
    using namespace ::GEngine::Manager;
    using Clock = std::chrono::steady_clock;

    // Explicit-instantiation access is test-only; no production header or layout change.
    struct DispatcherProbe
    {
        using Member = TypedSubscriptions<void(const RoutedEvent&)> EventDispatcher::*;
        friend Member Provider(DispatcherProbe);
    };
    template<class Tag, typename Tag::Member member> struct PrivateObserver
    {
        friend typename Tag::Member Provider(Tag) { return member; }
    };
    template struct PrivateObserver<DispatcherProbe, &EventDispatcher::source>;

#ifndef GENGINE_INPUT_CONTROL
    struct NativeForegroundGuard
    {
        bool held = false;
        HWND window = nullptr;
        bool wasTopmost = false;
        bool Expose(HWND handle)
        {
            if (!window)
            {
                window = handle;
                wasTopmost = (GetWindowLongPtr(handle, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
            }
            return SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0;
        }
        bool Acquire()
        {
            if (!held)
                held = LockSetForegroundWindow(LSFW_LOCK) != 0;
            return held;
        }
        bool Release()
        {
            if (window)
            {
                if (IsWindow(window) &&
                    !SetWindowPos(window, wasTopmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                                  0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
                    return false;
                window = nullptr;
            }
            if (!held)
                return true;
            if (!LockSetForegroundWindow(LSFW_UNLOCK))
                return false;
            held = false;
            return true;
        }
        ~NativeForegroundGuard() { (void)Release(); }
    };
    inline NativeForegroundGuard nativeForeground;
#endif

    inline PlatformResult Failure(const char* message)
    {
#ifndef GENGINE_INPUT_CONTROL
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_FOREGROUND_RELEASE success={}",
                                   nativeForeground.Release());
#endif
        Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_15_FAIL {}", message);
        return std::unexpected(PlatformError{PlatformErrorCode::Initialization,
                                             "Phase 15 input checks", message});
    }

    inline SDL_Event Native(unsigned kind, unsigned window, unsigned ordinal = 0)
    {
        SDL_Event event{};
        switch (kind)
        {
        case 0:
        case 1:
            event.type = kind == 0 ? SDL_KEYDOWN : SDL_KEYUP;
            event.key.windowID = window;
            event.key.state = kind == 0 ? SDL_PRESSED : SDL_RELEASED;
            event.key.keysym.scancode = SDL_SCANCODE_P;
            event.key.keysym.sym = SDLK_p;
            break;
        case 2:
            event.type = SDL_MOUSEMOTION;
            event.motion.windowID = window;
            event.motion.x = 640 + static_cast<int>(ordinal);
            event.motion.y = 360 - static_cast<int>(ordinal);
            event.motion.xrel = 1;
            event.motion.yrel = -1;
            break;
        case 3:
            event.type = SDL_MOUSEWHEEL;
            event.wheel.windowID = window;
            event.wheel.y = 1;
            event.wheel.preciseY = 1;
            break;
        }
        return event;
    }

    inline double Milliseconds(Clock::duration elapsed)
    {
        return std::chrono::duration<double, std::milli>(elapsed).count();
    }

    inline PlatformResult Measure(BaseApp& app)
    {
        auto& events = *app.GetEventManager();
        auto& input = *app.GetInputManager();
        auto& dispatcher = events.GetEventDispatcher();
        auto& provider = dispatcher.*Provider(DispatcherProbe{});
        SubscriptionObservations observations{};
        std::array<Clock::time_point, 64> injected{};
        unsigned deliveries = 0;
        bool observed = false;
        bool unexpectedEvent = false;
        double maxLatency = 0;
        const auto delivered = [&](unsigned index)
        {
            ++deliveries;
            if (index >= injected.size())
            {
                unexpectedEvent = true;
                return;
            }
            if (observed)
                maxLatency = std::max(maxLatency, Milliseconds(Clock::now() - injected[index]));
        };
#ifdef GENGINE_INPUT_CONTROL
        std::array<unsigned, 3> counts{};
        auto key = events.Subscribe(Manager::Event::AppPause, [&]() { delivered(4 * counts[0]++); });
        auto motion = events.Subscribe(Manager::Event::MouseMove,
            [&](const MouseMoveParam&) { delivered(4 * counts[1]++ + 2); });
        auto wheel = events.Subscribe(Manager::Event::MouseScrollWheel,
            [&](const MouseScrollWheelParam&) { delivered(4 * counts[2]++ + 3); });
        if (!key || !motion || !wheel)
            return Failure("control observer subscription");
        constexpr unsigned expectedDeliveries = 48;
#else
        unsigned sequence = 0;
        auto trace = input.Observe([&](const InputEvent&) { delivered(sequence++); });
        if (!trace)
            return Failure("candidate observer subscription");
        constexpr unsigned expectedDeliveries = 64;
#endif
        // Drain incidental startup events outside every recorded boundary.
        input.PrepareForUpdate();
        events.PollEvents();
        input.Update();
        auto& io = ImGui::GetIO();
        const bool mouseCapture = io.WantCaptureMouse, keyboardCapture = io.WantCaptureKeyboard;
        struct ObserverExit
        {
            TypedSubscriptions<void(const RoutedEvent&)>& source;
            InputManager& input;
            ImGuiIO& io;
            bool mouse, keyboard;
            ~ObserverExit()
            {
                (void)source.Observe(nullptr);
#ifndef GENGINE_INPUT_CONTROL
                (void)input.ObserveCosts(nullptr);
#endif
                ImGui::GetCurrentContext()->InputEventsQueue.clear();
                io.WantCaptureMouse = mouse;
                io.WantCaptureKeyboard = keyboard;
            }
        } observerExit{provider, input, io, mouseCapture, keyboardCapture};
        io.WantCaptureMouse = io.WantCaptureKeyboard = false;
#ifndef GENGINE_INPUT_CONTROL
        const auto priorRouting = input.Routing();
        input.PublishRouting({app.GetWindow()->GetWindowID(), 0, 0, 1280, 720, true, true, false, false});
#endif
        std::ofstream output("input-cost.csv");
        output << "pair,on,sample,batch_ms,max_latency_ms,deliveries,allocations,requested_bytes,lookups,callbacks,logical_value_copies,logical_copy_bytes,engine_backlog,native_remaining,drain_mutex_acquisitions\n";
        output << std::setprecision(12);
        for (unsigned pair = 0; pair < 3; ++pair)
        {
            for (unsigned lane = 0; lane < 2; ++lane)
            {
                observed = lane == 1;
                if (!provider.Observe(observed ? &observations : nullptr))
                    return Failure("provider observer attachment");
#ifndef GENGINE_INPUT_CONTROL
                if (!input.ObserveCosts(observed ? &observations : nullptr))
                    return Failure("input observer attachment");
#endif
                for (unsigned iteration = 0; iteration < 360; ++iteration)
                {
                    // UI is inactive in this input-only fixture; retire its copied native
                    // events outside the measured interval, without advancing a UI frame.
                    ImGui::GetCurrentContext()->InputEventsQueue.clear();
                    deliveries = 0;
                    unexpectedEvent = false;
                    maxLatency = 0;
                    observations = {};
#ifdef GENGINE_INPUT_CONTROL
                    counts = {};
#else
                    sequence = 0;
#endif
                    SDL_PumpEvents();
                    // Incidental OS traffic is not admitted as part of the synthetic batch.
                    if (SDL_HasEvents(SDL_FIRSTEVENT, SDL_LASTEVENT))
                        return Failure("external native traffic before measured batch");
                    for (unsigned i = 0; i < 64; ++i)
                    {
                        auto event = Native(i % 4, app.GetWindow()->GetWindowID(), i / 4);
                        if (observed)
                            injected[i] = Clock::now();
                        if (SDL_PushEvent(&event) != 1)
                            return Failure("native injection did not accept an event");
                    }
                    const auto start = Clock::now();
                    input.PrepareForUpdate();
                    events.PollEvents();
                    input.Update();
                    const double cost = Milliseconds(Clock::now() - start);
                    const int pending = SDL_PeepEvents(nullptr, 0, SDL_PEEKEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
                    if (unexpectedEvent || deliveries != expectedDeliveries || pending != 0)
                        return Failure("incomplete ordered native batch or external traffic");
                    if (iteration >= 120)
                    {
                        // Contract-level trivial value copies, not a claim of physical memcpy.
                        // The old deliver lambda and EventDispatcher's tuple each copy mouse values.
#ifdef GENGINE_INPUT_CONTROL
                        constexpr unsigned copies = 64;
                        constexpr unsigned copyBytes = 32 * (sizeof(MouseMoveParam) + sizeof(MouseScrollWheelParam));
#else
                        constexpr unsigned copies = 0, copyBytes = 0; // Input routes borrow one owned envelope.
#endif
                        output << pair << ',' << lane << ',' << iteration - 120 << ',' << cost << ','
                               << maxLatency << ',' << deliveries << ',' << observations.allocations << ','
                               << observations.allocatedBytes << ',' << observations.lookups << ','
                               << observations.callbacks << ',' << copies << ',' << copyBytes << ",0,"
                               << pending << ",1\n";
                    }
                }
            }
        }
        if (!provider.Observe(nullptr))
            return Failure("provider observer detach");
#ifndef GENGINE_INPUT_CONTROL
        if (!input.ObserveCosts(nullptr))
            return Failure("input observer detach");
        input.PublishRouting(priorRouting);
#endif
        io.WantCaptureMouse = mouseCapture;
        io.WantCaptureKeyboard = keyboardCapture;
        if (!output.good())
            return Failure("cost evidence write");
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_COST_COMPLETE pairs=3 warmup=120 samples=240 events=64 allocations_boundary=provider_storage copies=logical_value_transfers native_allocations=unmeasured");
        return {};
    }

    inline void Describe(BaseApp& app)
    {
        const auto logical = app.GetWindow()->GetLogicalSize();
        const auto pixels = app.GetWindow()->GetFramebufferPixelSize();
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_ENV logical={}x{} pixels={}x{} vsync={} cap={} renderer={} driver={}",
            logical.Width, logical.Height, pixels.Width, pixels.Height, app.GetWindow()->GetSwapInterval(),
            app.GetManualFrameRateLimit(), reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
            reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    }

    inline bool RequestNativeClose()
    {
#ifndef GENGINE_INPUT_CONTROL
        if (!nativeForeground.Release())
            return false;
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_FOREGROUND_RELEASE success=true");
#endif
        SDL_Event close{};
        close.type = SDL_QUIT;
        return SDL_PushEvent(&close) == 1;
    }

#ifndef GENGINE_INPUT_CONTROL
    struct UIProbe
    {
        std::array<ImVec2, 8> points{};
        std::array<bool, 8> seen{};
        char text[128]{};
        std::string routedText;
        bool button = false, popupItem = false, shadowsOpen = false, fileOpen = false;
        bool watch = false, leak = false;
        bool placeInFront = false, textActive = false;
        bool traceReady = false;
        unsigned pickingAttempts = 0;
        unsigned setupFrames = 0;
        unsigned textDrainFrames = 0;
        unsigned step = 0, uiEvents = 0, viewEvents = 0, gameEvents = 0;
        Subscription trace;
    };
    inline UIProbe uiProbe;

    inline void DescribeNativeFocus()
    {
        const HWND foreground = GetForegroundWindow();
        DWORD process = 0;
        const DWORD thread = GetWindowThreadProcessId(foreground, &process);
        char type[256]{};
        GetClassNameA(foreground, type, sizeof(type));
        auto* keyboard = SDL_GetKeyboardFocus();
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_15_FOCUS_OWNER process={} thread={} class={} ownProcess={} sdlKeyboard={}",
            process, thread, type, GetCurrentProcessId(), keyboard ? SDL_GetWindowID(keyboard) : 0);
    }

    void RecordInputPicking(bool enabled)
    {
        if (uiProbe.watch && enabled)
        {
            ++uiProbe.pickingAttempts;
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_UI_PICK step={} attempts={}",
                                       uiProbe.step, uiProbe.pickingAttempts);
            uiProbe.leak = true;
        }
    }

    // These hooks are emitted only by the isolated diagnostic executable.
    void NoteUIItem(unsigned index)
    {
        const auto low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
        const auto origin = ImGui::GetMainViewport()->Pos;
        uiProbe.points[index] = {(low.x + high.x) * .5f - origin.x,
                                 (low.y + high.y) * .5f - origin.y};
        uiProbe.seen[index] = high.x > low.x && high.y > low.y;
    }
    void NoteUIMenu(unsigned index, bool open, float x, float y, float width, float height)
    {
        const auto origin = ImGui::GetMainViewport()->Pos;
        uiProbe.points[index] = {x + width * .5f - origin.x, y + height * .5f - origin.y};
        uiProbe.seen[index] = true;
        if (index == 0)
            uiProbe.shadowsOpen = open;
        if (index == 3)
            uiProbe.fileOpen = open;
    }
    void DrawInputFixture()
    {
        if (uiProbe.placeInFront)
        {
            ImGui::SetNextWindowFocus();
            uiProbe.placeInFront = false;
        }
        ImGui::SetNextWindowPos({940, 70}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({280, 240}, ImGuiCond_Always);
        ImGui::Begin("Input regression fixture", nullptr,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
        const auto low = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        const auto origin = ImGui::GetMainViewport()->Pos;
        (void)BaseApp::GetInputManager()->AddUIRegion({low.x - origin.x, low.y - origin.y,
                                                       low.x + size.x - origin.x,
                                                       low.y + size.y - origin.y});
        ImGui::InputText("Text", uiProbe.text, sizeof(uiProbe.text));
        uiProbe.textActive = ImGui::IsItemActive();
        NoteUIItem(4);
        if (ImGui::Button("Ordinary widget"))
            uiProbe.button = !uiProbe.button;
        NoteUIItem(5);
        if (ImGui::Button("Open popup"))
            ImGui::OpenPopup("Input popup");
        NoteUIItem(6);
        if (ImGui::BeginPopup("Input popup"))
        {
            if (ImGui::MenuItem("Popup action"))
                uiProbe.popupItem = true;
            NoteUIItem(7);
            ImGui::EndPopup();
        }
        ImGui::End();
    }
    inline bool PushPointer(BaseApp& app, unsigned index, bool down)
    {
        if (!uiProbe.seen[index])
            return false;
        const auto p = uiProbe.points[index];
        SDL_WarpMouseInWindow(SDL_GetWindowFromID(app.GetWindow()->GetWindowID()), int(p.x),
                              int(p.y));
        SDL_PumpEvents(); // Queue native warp motion before the button (backend ignores button x/y).
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_UI_POINTER item={} down={} target={},{}",
                                   index, down, p.x, p.y);
        SDL_Event event{};
        event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.windowID = app.GetWindow()->GetWindowID();
        event.button.button = SDL_BUTTON_LEFT;
        event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
        event.button.x = int(p.x);
        event.button.y = int(p.y);
        event.button.clicks = 1;
        return SDL_PushEvent(&event) == 1;
    }
    inline bool PushKeys(BaseApp& app)
    {
        for (auto scan : {SDL_SCANCODE_P, SDL_SCANCODE_R, SDL_SCANCODE_SPACE})
            for (auto type : {SDL_KEYDOWN, SDL_KEYUP})
            {
                SDL_Event e{};
                e.type = type;
                e.key.windowID = app.GetWindow()->GetWindowID();
                e.key.keysym.scancode = scan;
                e.key.keysym.sym = SDL_GetKeyFromScancode(scan);
                e.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
                if (SDL_PushEvent(&e) != 1)
                    return false;
            }
        return true;
    }
    inline PlatformResult StepUI(BaseApp& app)
    {
        auto& p = uiProbe;
        if (!p.traceReady)
        {
            auto token = app.GetInputManager()->Observe(
                [&](const InputEvent& e)
                {
                    if (e.kind == InputKind::FocusLost)
                        DescribeNativeFocus();
                    if (uiProbe.watch)
                        Log::GetCoreLogger()->info(
                            "PRE_EDITOR_PHASE_15_UI_TRACE step={} kind={} window={} key={} button={} position={},{} destination={} handled={} cleanup={}",
                            uiProbe.step, int(e.kind), e.window, int(e.key), int(e.button),
                            e.x, e.y, int(e.destination), e.handled, e.cleanup);
                    if (!uiProbe.watch || e.cleanup || e.kind == InputKind::Cancel ||
                        e.kind == InputKind::FocusLost || e.kind == InputKind::FocusGained)
                        return;
                    if (e.destination == InputLayer::UI)
                    {
                        ++uiProbe.uiEvents;
                        if (e.kind == InputKind::Text)
                            uiProbe.routedText += e.Text();
                    }
                    if (e.destination == InputLayer::View)
                        ++uiProbe.viewEvents;
                    if (e.destination == InputLayer::Game)
                        ++uiProbe.gameEvents;
                    uiProbe.leak |= e.destination != InputLayer::UI;
                });
            if (!token)
                return Failure("UI trace subscription");
            p.trace = std::move(*token);
            p.traceReady = true;
        }
        if (!p.step)
        {
            const auto pending = ImGui::GetCurrentContext()->InputEventsQueue.Size;
            if (pending)
            {
                Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_UI_SETUP frame={} pending={} focused={}",
                                           p.setupFrames, pending, app.GetInputManager()->Focused());
                if (++p.setupFrames > 64)
                    return Failure("preceding native fixture UI events did not drain");
                return {};
            }
            DescribeNativeFocus();
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_UI_SETUP_COMPLETE focused={} flags={}",
                app.GetInputManager()->Focused(),
                SDL_GetWindowFlags(SDL_GetWindowFromID(app.GetWindow()->GetWindowID())));
            // Start after the deterministic state/cost fixtures have retired their events.
            p.placeInFront = true;
        }
        if (p.leak)
        {
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_15_UI_FAILURE step={} ui={} view={} game={} picking={}",
                p.step, p.uiEvents, p.viewEvents, p.gameEvents, p.pickingAttempts);
            return Failure("UI input delivered to View or Game");
        }
        if (p.step == 25 && ImGui::GetCurrentContext()->InputEventsQueue.Size)
        {
            const auto& context = *ImGui::GetCurrentContext();
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_15_UI_TEXT_DRAIN wait={} frame={} queued={} head={} trail={}",
                p.textDrainFrames, context.FrameCount, context.InputEventsQueue.Size,
                int(context.InputEventsQueue[0].Type), context.InputEventsTrail.Size);
            if (++p.textDrainFrames > 64)
                return Failure("native text fixture UI events did not drain");
            return {};
        }
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_15_UI_STEP step={} shadows={} file={} text={} widget={} popup={} ui={} view={} game={}",
            p.step, p.shadowsOpen, p.fileOpen, p.text, p.button, p.popupItem, p.uiEvents,
            p.viewEvents, p.gameEvents);
        const auto& context = *ImGui::GetCurrentContext();
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_15_UI_OBSERVER mouse={},{} left={} right={} queued={} active={} hovered={}",
            context.IO.MousePos.x, context.IO.MousePos.y, context.IO.MouseDown[0],
            context.IO.MouseDown[1], context.InputEventsQueue.Size, context.ActiveId,
            context.HoveredWindow ? context.HoveredWindow->Name : "none");
        bool injected = true;
        switch (p.step)
        {
        case 1:
            p.watch = true;
            injected = PushPointer(app, 0, true);
            break;
        case 2:
            injected = PushPointer(app, 0, false);
            break;
        case 5:
            if (!p.shadowsOpen || !p.seen[2])
                return Failure("Shadows menu fixture did not open");
            injected = PushPointer(app, 2, true);
            break;
        case 6:
            injected = PushPointer(app, 2, false);
            break;
        case 9:
            injected = PushKeys(app);
            break;
        case 12:
            injected = PushPointer(app, 4, true);
            break; // Dismiss the menu first.
        case 13:
            injected = PushPointer(app, 4, false);
            break;
        case 16:
            injected = PushPointer(app, 4, true);
            break;
        case 17:
            injected = PushPointer(app, 4, false);
            break;
        case 20:
        {
            if (!p.textActive || !app.GetInputManager()->Routing().uiKeyboard)
                return Failure("text fixture must be active before shortcut injection");
            injected = PushKeys(app);
            SDL_Event text{};
            text.type = SDL_TEXTINPUT;
            text.text.windowID = app.GetWindow()->GetWindowID();
            std::strcpy(text.text.text, "A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x99\x82");
            injected = SDL_PushEvent(&text) == 1 && injected;
            break;
        }
        case 25:
            if (p.routedText != "A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x99\x82")
                return Failure("native UI route must preserve complete Unicode bytes");
#if IM_UNICODE_CODEPOINT_MAX == 0xFFFF
            if (std::string_view(p.text) != "A\xC3\xA9\xE4\xB8\xAD\xEF\xBF\xBD")
#else
            if (std::string_view(p.text) != p.routedText)
#endif
                return Failure("native text widget buffer versus pinned ImGui encoding");
            injected = PushPointer(app, 5, true);
            break;
        case 26:
            injected = PushPointer(app, 5, false);
            break;
        case 29:
            if (!p.button)
                return Failure("ordinary active widget fixture");
            injected = PushPointer(app, 6, true);
            break;
        case 30:
            injected = PushPointer(app, 6, false);
            break;
        case 33:
            injected = PushPointer(app, 7, true);
            break;
        case 34:
            injected = PushPointer(app, 7, false);
            break;
        case 37:
            if (!p.popupItem)
                return Failure("popup interaction fixture");
            injected = PushPointer(app, 3, true);
            break;
        case 38:
            injected = PushPointer(app, 3, false);
            break;
        case 41:
            if (!p.fileOpen || p.uiEvents == 0 || p.viewEvents || p.gameEvents)
                return Failure("general menu consumption");
            if (!p.trace.Reset())
                return Failure("UI trace release");
            p.watch = false;
            if (glGetError() != GL_NO_ERROR)
                return Failure("GL error during input integration");
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_15_UI_PASS shadows=true widget=true popup=true text=true menu=true view=0 game=0");
            break;
        }
        if (!injected)
            return Failure("native UI fixture event injection");
        ++p.step;
        return {};
    }

    inline PlatformResult CheckState(BaseApp& app)
    {
        unsigned checks = 0;
#define INPUT_REQUIRE(condition, message)                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
            return Failure(message);                                                               \
        ++checks;                                                                                  \
    } while (false)
        auto owned = InputManager::GetScopedInstance();
        auto& input = *owned;
        INPUT_REQUIRE(input.Initialize(), "isolated input fixture initialization");
        input.SetWindow(app.GetWindow());
        const auto window = app.GetWindow()->GetWindowID();
        INPUT_REQUIRE(input.PublishRouting({window, 0, 0, 1280, 720, true, true, false, false}),
                      "view descriptor");
        std::vector<InputLayer> order;
        InputLayer consume = InputLayer::Game;
        unsigned cleanup = 0;
        auto ui = input.Subscribe(InputLayer::UI,
                                  [&](InputDelivery& d)
                                  {
                                      if (d.event.cleanup)
                                      {
                                          ++cleanup;
                                          return;
                                      }
                                      order.push_back(InputLayer::UI);
                                      if (consume == InputLayer::UI)
                                          d.Handle();
                                  });
        auto view = input.Subscribe(InputLayer::View,
                                    [&](InputDelivery& d)
                                    {
                                        if (d.event.cleanup)
                                        {
                                            ++cleanup;
                                            return;
                                        }
                                        order.push_back(InputLayer::View);
                                        if (consume == InputLayer::View)
                                            d.Handle();
                                    });
        auto game = input.Subscribe(InputLayer::Game,
                                    [&](InputDelivery& d)
                                    {
                                        if (d.event.cleanup)
                                        {
                                            ++cleanup;
                                            return;
                                        }
                                        order.push_back(InputLayer::Game);
                                        d.Handle();
                                    });
        INPUT_REQUIRE(ui && view && game, "scoped route fixture subscriptions");
        const auto key = [&](InputKind kind, GEngineKeyCode code, bool repeat = false)
        {
            InputEvent e;
            e.kind = kind;
            e.key = code;
            e.window = window;
            e.repeated = repeat;
            return input.Route(e);
        };
        input.PrepareForUpdate();
        INPUT_REQUIRE(key(InputKind::KeyDown, GENGINE_KEY_P), "key press");
        INPUT_REQUIRE((order == std::vector{InputLayer::UI, InputLayer::View, InputLayer::Game}),
                      "UI View Game order");
        INPUT_REQUIRE(input.GetKeyboardState().IsKeyHeld(GENGINE_KEY_P) &&
                          input.GetKeyboardState().IsKeyPressed(GENGINE_KEY_P),
                      "initial held and pressed");
        INPUT_REQUIRE(key(InputKind::KeyUp, GENGINE_KEY_P), "same-frame key release");
        INPUT_REQUIRE(!input.GetKeyboardState().IsKeyHeld(GENGINE_KEY_P) &&
                          input.GetKeyboardState().IsKeyPressed(GENGINE_KEY_P) &&
                          input.GetKeyboardState().IsKeyReleased(GENGINE_KEY_P),
                      "short transition preserves both independent edges");
        INPUT_REQUIRE(key(InputKind::KeyDown, GENGINE_KEY_P) &&
                          key(InputKind::KeyUp, GENGINE_KEY_P),
                      "multiple cycles remain deliverable");
        input.PrepareForUpdate();
        INPUT_REQUIRE(!input.GetKeyboardState().IsKeyPressed(GENGINE_KEY_P) &&
                          !input.GetKeyboardState().IsKeyReleased(GENGINE_KEY_P),
                      "edges retire once");
        INPUT_REQUIRE(key(InputKind::KeyDown, GENGINE_KEY_W), "held start");
        input.PrepareForUpdate();
        INPUT_REQUIRE(key(InputKind::KeyDown, GENGINE_KEY_W, true), "repeat delivery");
        INPUT_REQUIRE(input.GetKeyboardState().IsKeyHeld(GENGINE_KEY_W) &&
                          !input.GetKeyboardState().IsKeyPressed(GENGINE_KEY_W),
                      "repeat does not create edge");
        INPUT_REQUIRE(key(InputKind::KeyUp, GENGINE_KEY_W), "held release");
        INPUT_REQUIRE(key(InputKind::KeyDown, GENGINE_KEY_LSHIFT) &&
                          key(InputKind::KeyDown, GENGINE_KEY_RCTRL),
                      "modifier presses");
        INPUT_REQUIRE((input.GetInputState().modifiers & (LeftShift | RightControl)) ==
                          (LeftShift | RightControl),
                      "left/right modifiers");
        INPUT_REQUIRE(key(InputKind::KeyUp, GENGINE_KEY_LSHIFT) &&
                          key(InputKind::KeyUp, GENGINE_KEY_RCTRL) &&
                          input.GetInputState().modifiers == 0,
                      "modifier releases");
        order.clear();
        consume = InputLayer::UI;
        INPUT_REQUIRE(key(InputKind::KeyDown, GENGINE_KEY_A), "UI consumed key");
        INPUT_REQUIRE(order == std::vector{InputLayer::UI} &&
                          !input.GetKeyboardState().IsKeyHeld(GENGINE_KEY_A) &&
                          !input.GetViewState().m_Keyboard.IsKeyHeld(GENGINE_KEY_A),
                      "UI prevents state bypass");
        INPUT_REQUIRE(key(InputKind::KeyUp, GENGINE_KEY_A), "UI matching cleanup");
        consume = InputLayer::View;
        input.PrepareForUpdate();
        InputEvent move;
        move.window = window;
        move.kind = InputKind::Motion;
        move.x = 100;
        move.y = 100;
        move.dx = 2;
        move.dy = -3;
        INPUT_REQUIRE(input.Route(move), "first motion");
        move.x = 110;
        move.y = 115;
        move.dx = 4;
        move.dy = 7;
        INPUT_REQUIRE(input.Route(move), "second motion");
        input.Update();
        INPUT_REQUIRE(input.GetViewState().m_Mouse.GetDX() == 6 &&
                          input.GetViewState().m_Mouse.GetDY() == 4 &&
                          input.GetViewState().m_Mouse.GetPosition().x == 110,
                      "motion sum/latest survives Update");
        InputEvent wheel;
        wheel.window = window;
        wheel.kind = InputKind::Wheel;
        wheel.wheelX = .5f;
        wheel.wheelY = 2;
        INPUT_REQUIRE(input.Route(wheel) && input.Route(wheel), "two-axis wheel delivery");
        INPUT_REQUIRE(input.GetViewState().m_Mouse.GetScrollWheel().x == 1 &&
                          input.GetViewState().m_Mouse.GetDWheel() == 4,
                      "wheel accumulation");
        input.PrepareForUpdate();
        INPUT_REQUIRE(input.GetViewState().m_Mouse.GetDX() == 0 &&
                          input.GetViewState().m_Mouse.GetDWheel() == 0,
                      "deltas retire once");
        InputEvent button;
        button.window = window;
        button.kind = InputKind::ButtonDown;
        button.button = GENGINE_BUTTON_LEFT;
        button.x = 120;
        button.y = 120;
        INPUT_REQUIRE(input.Route(button), "button down");
        button.kind = InputKind::ButtonUp;
        INPUT_REQUIRE(input.Route(button), "button up");
        INPUT_REQUIRE(input.GetViewState().m_Mouse.isButtonPressed(GENGINE_BUTTON_LEFT) &&
                          input.GetViewState().m_Mouse.isButtonReleased(GENGINE_BUTTON_LEFT) &&
                          !input.GetViewState().m_Mouse.isButtonHeld(GENGINE_BUTTON_LEFT),
                      "short button preserves both edges");
        std::string collected;
        auto textObserver = input.Observe(
            [&](const InputEvent& e)
            {
                if (e.kind == InputKind::Text)
                    collected += e.Text();
            });
        INPUT_REQUIRE(textObserver, "text observer");
        InputEvent text;
        text.window = window;
        text.kind = InputKind::Text;
        constexpr std::string_view unicode = "A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x99\x82";
        INPUT_REQUIRE(text.SetText(unicode) && input.Route(text), "Unicode text route");
        auto retained = text.Clone();
        INPUT_REQUIRE(retained && text.SetText("changed") && retained->Text() == unicode &&
                          collected == unicode,
                      "retained text owns original bytes");
        INPUT_REQUIRE(!text.SetText("\xC0\xAF") && text.Text() == "changed",
                      "invalid UTF8 transactional preservation");
        text.kind = InputKind::Composition;
        text.compositionStart = 0;
        text.compositionLength = 1;
        INPUT_REQUIRE(input.Route(text) && input.GetViewState().compositionActive,
                      "composition update");
        text.kind = InputKind::Text;
        INPUT_REQUIRE(input.Route(text) && !input.GetViewState().compositionActive,
                      "composition commit");
        INPUT_REQUIRE(key(InputKind::KeyDown, GENGINE_KEY_W), "focus fixture key held");
        button.kind = InputKind::ButtonDown;
        button.button = GENGINE_BUTTON_RIGHT;
        INPUT_REQUIRE(input.Route(button), "capture fixture button held");
        INPUT_REQUIRE(input.CapturePointer(InputLayer::View, true) &&
                          SDL_GetRelativeMouseMode() == SDL_TRUE,
                      "native View capture");
        InputEvent focus;
        focus.window = window;
        focus.kind = InputKind::FocusLost;
        INPUT_REQUIRE(input.Route(focus), "focus loss");
        INPUT_REQUIRE(!input.Focused() &&
                          !input.GetViewState().m_Keyboard.IsKeyHeld(GENGINE_KEY_W) &&
                          input.GetViewState().m_Keyboard.IsKeyReleased(GENGINE_KEY_W) &&
                          input.PointerCapture() == InputLayer::None &&
                          SDL_GetRelativeMouseMode() == SDL_FALSE,
                      "focus cleanup releases held/native capture");
        focus.kind = InputKind::FocusGained;
        INPUT_REQUIRE(input.Route(focus) && key(InputKind::KeyDown, GENGINE_KEY_W, true),
                      "focus regain and held repeat");
        INPUT_REQUIRE(!input.GetViewState().m_Keyboard.IsKeyHeld(GENGINE_KEY_W),
                      "regain remains neutral until new press");
        INPUT_REQUIRE(key(InputKind::KeyDown, GENGINE_KEY_W), "fresh press after regain");
        button.kind = InputKind::ButtonDown;
        INPUT_REQUIRE(input.Route(button) && input.CapturePointer(InputLayer::View, true),
                      "capture transfer setup");
        INPUT_REQUIRE(input.CapturePointer(InputLayer::UI, false) &&
                          input.PointerCapture() == InputLayer::UI &&
                          !input.GetViewState().m_Mouse.isButtonHeld(GENGINE_BUTTON_RIGHT),
                      "transfer cancels old gesture");
        move.x = 1400;
        move.y = 800;
        auto capturedMotion = input.Route(move);
        INPUT_REQUIRE(capturedMotion && *capturedMotion == InputLayer::UI,
                      "captured pointer stays with new owner outside view");
        INPUT_REQUIRE(input.ReleasePointer(InputLayer::UI) &&
                          input.CancelInput(InputCancelReason::Explicit),
                      "explicit cancel");
        INPUT_REQUIRE(cleanup > 0 && !input.GetPhysicalState().m_Keyboard.IsKeyHeld(GENGINE_KEY_W),
                      "cleanup delivered and physical reset");
        InputEvent invalid;
        invalid.window = window + 999;
        invalid.kind = InputKind::KeyDown;
        invalid.key = GENGINE_KEY_P;
        INPUT_REQUIRE(!input.Route(invalid), "foreign window rejection");
        invalid.window = window;
        invalid.key = static_cast<GEngineKeyCode>(65535);
        INPUT_REQUIRE(!input.Route(invalid), "invalid key rejection");
        invalid.kind = InputKind::ButtonDown;
        invalid.button = static_cast<GEngineMouseCode>(255);
        INPUT_REQUIRE(!input.Route(invalid), "invalid button rejection");
        bool wrongThread = false;
        std::thread worker(
            [&]
            {
                auto result = key(InputKind::KeyDown, GENGINE_KEY_P);
                wrongThread = !result && result.error() == InputError::WrongThread;
            });
        worker.join();
        INPUT_REQUIRE(wrongThread, "input owner thread rejection");
        consume = InputLayer::Game;
        Subscription self, other;
        unsigned selfCalls = 0, otherCalls = 0;
        bool nested = false;
        auto first = input.Subscribe(InputLayer::UI,
                                     [&](InputDelivery& d)
                                     {
                                         if (d.event.cleanup)
                                             return;
                                         ++selfCalls;
                                         (void)self.Reset();
                                         (void)other.Reset();
                                         nested = bool(key(InputKind::KeyDown, GENGINE_KEY_B));
                                     });
        INPUT_REQUIRE(first, "self-release fixture subscribe");
        self = std::move(*first);
        auto second = input.Subscribe(InputLayer::UI,
                                      [&](InputDelivery& d)
                                      {
                                          if (!d.event.cleanup)
                                              ++otherCalls;
                                      });
        INPUT_REQUIRE(second, "cross-release fixture subscribe");
        other = std::move(*second);
        INPUT_REQUIRE(key(InputKind::KeyDown, GENGINE_KEY_A) && nested && selfCalls == 1 &&
                          otherCalls == 0,
                      "self/cross release and nested dispatch");
        INPUT_REQUIRE(input.CancelInput(InputCancelReason::Shutdown), "fixture teardown cleanup");
        INPUT_REQUIRE(textObserver->Reset() && ui->Reset() && view->Reset() && game->Reset(),
                      "scoped cleanup before borrowed state");
        owned.reset();
        SDL_StartTextInput(); // The fixture's platform cleanup must not disable the live RBS adapter.
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_STATE_PASS checks={}", checks);
#undef INPUT_REQUIRE
        return {};
    }

    inline PlatformResult PrepareNativeFixture(BaseApp& app)
    {
        auto* window = SDL_GetWindowFromID(app.GetWindow()->GetWindowID());
        SDL_SysWMinfo native{};
        SDL_VERSION(&native.version);
        if (!SDL_GetWindowWMInfo(window, &native) || native.subsystem != SDL_SYSWM_WINDOWS)
            return Failure("native Windows fixture handle");
        const HWND handle = native.info.win.window;
        if (!nativeForeground.Expose(handle))
            return Failure("native fixture window exposure");
        const DWORD current = GetCurrentThreadId();
        const DWORD foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const bool attach = foreground && foreground != current;
        if (!IsWindowVisible(handle))
            ShowWindow(handle, SW_SHOW);
        if (attach && !AttachThreadInput(current, foreground, TRUE))
            return Failure("native fixture foreground attachment");
        const BOOL activated = GetForegroundWindow() == handle || SetForegroundWindow(handle);
        SetFocus(handle);
        const BOOL detached = !attach || AttachThreadInput(current, foreground, FALSE);
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_15_FIXTURE_ACTIVATE visible={} foreground={} activated={} detached={}",
            bool(IsWindowVisible(handle)), foreground, bool(activated), bool(detached));
        if (!activated || !detached)
            return Failure("native fixture foreground activation/detachment");
        SDL_PumpEvents();
        app.GetInputManager()->PrepareForUpdate();
        app.GetEventManager()->PollEvents();
        app.GetInputManager()->Update();
        const auto flags = SDL_GetWindowFlags(window);
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_FIXTURE_FOCUS nativeFlags={} focused={}",
                                   flags, app.GetInputManager()->Focused());
        if (!(flags & SDL_WINDOW_INPUT_FOCUS) || !app.GetInputManager()->Focused())
            return Failure("native fixture did not acquire input focus");
        if (!nativeForeground.Acquire())
            return Failure("native fixture foreground lease");
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_FOREGROUND_ACQUIRE success=true");
        return {};
    }

    inline bool NativeTap(BaseApp& app, SDL_Scancode scan)
    {
        auto& input = *app.GetInputManager();
        auto trace = input.Observe([&input, scan](const InputEvent& event)
        {
            if (event.kind == InputKind::FocusLost)
                DescribeNativeFocus();
            Log::GetCoreLogger()->info(
                "PRE_EDITOR_PHASE_15_NATIVE_TRACE tap={} kind={} key={} destination={} handled={} cleanup={} focused={}",
                int(scan), int(event.kind), int(event.key), int(event.destination),
                event.handled, event.cleanup, input.Focused());
        });
        if (!trace)
            return false;
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PHASE_15_NATIVE_BEFORE tap={} focused={} uiKeyboard={} nativeFlags={}",
            int(scan), input.Focused(), input.Routing().uiKeyboard,
            SDL_GetWindowFlags(SDL_GetWindowFromID(app.GetWindow()->GetWindowID())));
        for (unsigned kind = 0; kind < 2; ++kind)
        {
            auto event = Native(kind, app.GetWindow()->GetWindowID());
            event.key.keysym.scancode = scan;
            event.key.keysym.sym = SDL_GetKeyFromScancode(scan);
            if (SDL_PushEvent(&event) != 1)
                return false;
        }
        app.GetInputManager()->PrepareForUpdate();
        app.GetEventManager()->PollEvents();
        app.GetInputManager()->Update();
        std::array<SDL_Event, 128> pending{};
        const int count = SDL_PeepEvents(pending.data(), int(pending.size()), SDL_PEEKEVENT,
                                         SDL_KEYDOWN, SDL_KEYUP);
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_NATIVE_AFTER tap={} pendingKeys={} focused={}",
                                   int(scan), count, input.Focused());
        for (int i = 0; i < count; ++i)
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_15_NATIVE_PENDING type={} key={} window={}",
                                       pending[i].type, int(pending[i].key.keysym.scancode),
                                       pending[i].key.windowID);
        return true;
    }
#endif
}
