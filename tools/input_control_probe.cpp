// Focused production-library tests; run through test_input_control.py.
#include "gepch.h"
#include "Core/BaseApp.h"
#include "Core/RuntimeAssets.h"
#include "Core/Timer.h"
#include "Scene/_Scene.h"
#include "Windows/SDLWindow.h"
#include <imgui/imgui.h>
#include <array>
#include <new>
#include <stdexcept>
#include <string_view>
#include <windows.h>

// Compile the actual application implementation into a dedicated viewport fixture.
// Its entry point is retained under another name; production sources need no test hooks.
#if defined(GENGINE_PROBE_SIMULATION) || defined(GENGINE_PROBE_RAY)
#define main ApplicationEntryPointForProbe
#if defined(GENGINE_PROBE_SIMULATION)
#include "../RigidBodySimulation/src/RigidBodySimulation.cpp"
#else
#include "../RayTracing/src/RayTracing.cpp"
#endif
#undef main
WindowProperties winProp;
#endif

namespace
{
    using namespace GEngine;
    using namespace ::GEngine::Manager;

    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    void FrameClockChecks()
    {
        using namespace std::chrono_literals;
        static_assert(FrameClock::Clock::is_steady && Timer::Clock::is_steady);
        static_assert(_Scene::PhysicsStep == Seconds(1.0 / 60.0));
        static_assert(_Scene::PhysicsStepSeconds == 1.0 / 60.0);
        const auto origin = FrameClock::TimePoint{};
        FrameClock clock(origin);
        auto frame = clock.Tick(origin + 1250us);
        Require(std::abs(frame.rawDelta.count() - .00125) < 1e-15
            && frame.rawDelta == frame.clampedDelta && frame.clampedDelta == frame.renderDelta,
            "Short frame was rounded, fabricated or scaled incorrectly");
        const Timestep shortStep(frame.rawDelta);
        Require(shortStep.GetDuration() == frame.rawDelta && shortStep.GetMilliseconds() == 1.25f
            && shortStep.GetSeconds() == .00125f && static_cast<float>(shortStep) == .00125f,
            "Typed timestep conversions do not match seconds/milliseconds");
        Require(Timestep(250ms).GetSecondsPrecise() == .25 && Timestep(1.25).GetMilliseconds() == 1250.f,
            "Chrono/legacy duration conversion differs");
        Require(Timestep(Seconds::zero()).GetSeconds() == 0
            && Timestep(-.125).GetSecondsPrecise() == -.125
            && std::isinf(Timestep(std::numeric_limits<double>::max()).GetSeconds())
            && std::isnan(Timestep(std::numeric_limits<double>::quiet_NaN()).GetSeconds()),
            "Legacy invalid/large timestep values changed before the scheduler boundary");

        frame = clock.Tick(origin + 601250us);
        Require(std::abs(frame.rawDelta.count() - .6) < 1e-15
            && frame.clampedDelta == 250ms && frame.renderDelta == 250ms,
            "Long frame lost raw time or failed to bound presentation time");
        const auto anchor = clock.LastSample();
        Require(clock.Tick(anchor).rawDelta == Seconds::zero()
            && clock.Tick(origin).rawDelta == Seconds::zero() && clock.LastSample() == anchor,
            "Equal/backwards sample moved the monotonic anchor");
        Require(clock.Tick(anchor + 5ms).rawDelta == 5ms, "Backwards sample double-counted elapsed time");
        clock.Reset(origin + 10s);
        Require(clock.Tick(origin + 10s + 2ms).rawDelta == 2ms, "Reset leaked suspended time");
        clock.Reset(origin);
        Seconds total{};
        for (int i = 1; i <= 1000; ++i) total += clock.Tick(origin + i * 1ms).rawDelta;
        Require(std::abs(total.count() - 1.0) < 1e-12, "Measured frame durations do not conserve elapsed time");

        Timer timer(origin);
        Require(timer.ElapsedDuration(origin + 1250ms) == 1250ms
            && timer.ElapsedSeconds(origin + 1250ms) == 1.25f
            && timer.ElapsedMilliSeconds(origin + 1250ms) == 1250.f
            && timer.Elapsed(origin + 1250ms) == 1.25f, "Timer seconds/milliseconds disagree");
        timer.Reset(origin + 3s);
        Require(timer.ElapsedDuration(origin + 3s) == Seconds::zero()
            && timer.ElapsedDuration(origin + 3250ms) == 250ms, "Timer reset/conversion differs");
        timer.Reset(); // Keep the legacy destructor diagnostic meaningful.
        FrameClock real;
        auto previous = real.LastSample();
        for (int i = 0; i < 1000; ++i)
        {
            Require(real.Tick().rawDelta >= Seconds::zero() && real.LastSample() >= previous,
                "Steady-clock samples regressed");
            previous = real.LastSample();
        }
        std::cout << "[PASS] clock-conversions/short-long/equal-backwards/reset/monotonic/conservation\n";
    }

    void Neutral(const InputState& state)
    {
        for (unsigned key = 0; key < GENGINE_MAX_KEYCODES; ++key)
            Require(state.m_Keyboard.GetKeyState(static_cast<GEngineKeyCode>(key)) == ButtonState::None,
                "Initial keyboard state is not neutral");
        for (unsigned button = 0; button < GENGINE_CONTROLLER_BUTTON_MAX; ++button)
        {
            const auto code = static_cast<GEngineControllerCode>(button);
            Require(!state.m_Controller.GetButtonValue(code) && state.m_Controller.GetButtonState(code) == ButtonState::None,
                "Initial controller buttons are not neutral");
        }
        const auto& mouse = state.m_Mouse;
        Require(!mouse.IsRelative() && mouse.GetDX() == 0 && mouse.GetDY() == 0 && mouse.GetDWheel() == 0
            && mouse.GetPosition().x == 0 && mouse.GetPosition().y == 0 && mouse.GetScrollWheel().x == 0
            && mouse.m_CurrentButtons == 0 && mouse.m_PreviousButtons == 0, "Initial mouse state is not neutral");
        const auto& controller = state.m_Controller;
        Require(!controller.IsControllerConnected() && controller.GetLeftTrigger() == 0 && controller.GetRightTrigger() == 0
            && controller.GetLeftStick().x == 0 && controller.GetLeftStick().y == 0
            && controller.GetRightStick().x == 0 && controller.GetRightStick().y == 0,
            "Disconnected controller axes are not neutral");
    }

    void InitialStateAndButtons()
    {
        // Default-initialize on deliberately nonzero storage: value initialization
        // or a zero-filled allocation must not hide missing member initializers.
        alignas(InputState) std::array<unsigned char, sizeof(InputState)> storage;
        storage.fill(0xa5);
        auto* state = ::new (static_cast<void*>(storage.data())) InputState;
        Neutral(*state);
        std::destroy_at(state);
        std::cout << "[PASS] deterministic-default-state/nonzero-storage\n";

        MouseState mouse;
        const std::array codes{ GENGINE_BUTTON_LEFT, GENGINE_BUTTON_MIDDLE, GENGINE_BUTTON_RIGHT,
            GENGINE_BUTTON_X1, GENGINE_BUTTON_X2 };
        const std::array<Uint32, 5> masks{ SDL_BUTTON_LMASK, SDL_BUTTON_MMASK, SDL_BUTTON_RMASK,
            SDL_BUTTON_X1MASK, SDL_BUTTON_X2MASK };
        for (Uint32 previous = 0; previous < 32; ++previous)
            for (Uint32 current = 0; current < 32; ++current)
                for (std::size_t i = 0; i < codes.size(); ++i)
                {
                    mouse.m_PreviousButtons = previous; mouse.m_CurrentButtons = current;
                    const bool was = (previous & masks[i]) != 0, down = (current & masks[i]) != 0;
                    const auto expected = down ? (was ? ButtonState::Held : ButtonState::Pressed)
                        : (was ? ButtonState::Released : ButtonState::None);
                    Require(mouse.GetButtonValue(codes[i]) == down && mouse.GetButtonState(codes[i]) == expected,
                        "Mouse mask/transition differs");
                    Require(mouse.isButtonPressed(codes[i]) == (expected == ButtonState::Pressed)
                        && mouse.isButtonHeld(codes[i]) == (expected == ButtonState::Held)
                        && mouse.isButtonReleased(codes[i]) == (expected == ButtonState::Released), "Mouse state helper differs");
                }
        for (const unsigned invalid : { 0u, 6u, 255u })
        {
            const auto code = static_cast<GEngineMouseCode>(invalid);
            Require(!mouse.GetButtonValue(code) && mouse.GetButtonState(code) == ButtonState::None,
                "Invalid mouse button was not neutral");
        }
        KeyboardState keyboard;
        Require(keyboard.GetKeyState(static_cast<GEngineKeyCode>(512)) == ButtonState::None, "Key sentinel not rejected");
        ControllerState controller;
        for (const auto code : { GENGINE_BUTTON_INVALID, GENGINE_BUTTON_MAX })
            Require(!controller.GetButtonValue(code) && controller.GetButtonState(code) == ButtonState::None,
                "Controller sentinel not rejected");
        std::cout << "[PASS] five-buttons/all-5120-mask-transitions/invalid-codes\n";
    }

    void UntouchedFrames()
    {
        SDL_SetMainReady();
        Require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0, SDL_GetError());
        struct Cleanup { ~Cleanup() { SDL_Quit(); } } cleanup;
        auto input = InputManager::GetScopedInstance();
        input->PrepareForUpdate(); // Also safe before the SDL keyboard pointer is installed.
        input->Initialize();
        Neutral(input->GetInputState());
        auto& mouse = input->GetMouseState();
        for (int frame = 0; frame < 4; ++frame)
        {
            mouse.m_XRel = 19; mouse.m_YRel = -23;
            mouse.SetScrollWheel(Math::Vector2(2, -3));
            mouse.m_MousePos = Math::Vector2(27, 31);
            mouse.m_CurrentButtons = SDL_BUTTON_RMASK;
            input->PrepareForUpdate();
            Require(mouse.GetDX() == 0 && mouse.GetDY() == 0 && mouse.GetScrollWheel().x == 0 && mouse.GetDWheel() == 0,
                "Untouched frame retained transient input");
            Require(mouse.GetPosition().x == 27 && mouse.GetPosition().y == 31
                && mouse.m_PreviousButtons == SDL_BUTTON_RMASK, "Frame reset lost persistent position/button history");
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {}
            input->Update(); // No SDLWindow has been assigned; polling must be safe.
            Require(mouse.GetDX() == 0 && mouse.GetDY() == 0 && mouse.GetDWheel() == 0,
                "Absolute polling resurrected an old transient");
        }
        mouse.SetMouseRelative(true);
        input->PrepareForUpdate();
        input->Update(); // Relative polling without a window and without new motion.
        Require(mouse.GetDX() == 0 && mouse.GetDY() == 0, "Untouched relative frame is not neutral");
        input->ShutDown();
        input->Initialize();
        Neutral(input->GetInputState());
        input->ShutDown();
        std::cout << "[PASS] untouched-frames/reset-history/relative-and-absolute/no-window/reinitialize\n";
    }

    void PushWindowEvent(Uint32 id, int width)
    {
        SDL_Event event{};
        event.type = SDL_WINDOWEVENT;
        event.window.windowID = id;
        event.window.event = SDL_WINDOWEVENT_RESIZED;
        event.window.data1 = event.window.data2 = width;
        Require(SDL_PushEvent(&event) == 1, "Could not queue fixture window event");
    }

    void PushState(Uint32 id, Uint8 state, int width = 64, int height = 64)
    {
        SDL_Event event{};
        event.type = SDL_WINDOWEVENT;
        event.window.windowID = id;
        event.window.event = state;
        event.window.data1 = width;
        event.window.data2 = height;
        Require(SDL_PushEvent(&event) == 1, "Could not queue SDL window state");
    }

    double ProcessCpuSeconds()
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        Require(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user), "GetProcessTimes failed");
        ULARGE_INTEGER k{}, u{};
        k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
        return (k.QuadPart + u.QuadPart) * 1e-7;
    }

    class LoopApp : public BaseApp
    {
    public:
        int controls = 0, updates = 0, renders = 0, resizeEvents = 0;
        double firstSeconds = 0;
        int motionFrames = 0;
        bool measureClock = false;
        double inputSeconds = 0, stalledSeconds = 0;
        void Suspend(bool suspended) { m_Minimized = suspended; }
        bool Suspended() const { return m_Minimized; }
        void ProcessInput(Timestep ts) override
        {
            ++controls;
            inputSeconds = ts.GetSecondsPrecise();
            Require(ts.GetDuration() == GetFrameTime().renderDelta,
                "Input did not receive the measured presentation delta in seconds");
            // Intentionally omit BaseApp::ProcessInput: event pumping belongs to Run.
            const auto& mouse = GetInputManager()->GetMouseState();
            if (mouse.GetDX() == 7 && mouse.GetDY() == -9)
            {
                ++motionFrames;
                int x = 0, y = 0;
                SDL_GetMouseState(&x, &y);
                Require(mouse.GetPosition().x == x && mouse.GetPosition().y == y,
                    "SDL state was sampled before event polling");
            }
        }
        void Update(Timestep ts) override
        {
            if (++updates == 1) firstSeconds = ts.GetSecondsPrecise();
            Require(ts.GetDuration() == GetFrameTime().rawDelta,
                "Update did not receive all raw elapsed time");
            if (measureClock && updates == 2)
            {
                stalledSeconds = ts.GetSecondsPrecise();
                Require(stalledSeconds >= .350 && inputSeconds == .25,
                    "Actual stall was fabricated/clamped for simulation or unbounded for input");
            }
        }
        void Render() override
        {
            ++renders;
            if (measureClock && renders == 1)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(350));
                return;
            }
            // Exercise the real SDL/ImGui event return and capture paths.
            auto* gui = GetSDLWindow()->GetImGuiWindow();
            SDL_Event handled{}; handled.type = SDL_MOUSEMOTION;
            Require(gui->HandleSDLEvent(handled), "ImGui handled-event return is false");
            SDL_Event ignored{}; ignored.type = SDL_USEREVENT;
            Require(!gui->HandleSDLEvent(ignored), "ImGui unhandled-event return is true");
            auto& io = ImGui::GetIO();
            for (const bool value : { false, true })
            {
                io.WantCaptureMouse = io.WantCaptureKeyboard = value;
                Require(gui->WantCaptureMouse() == value && gui->WantCaptureKeyBoard() == value,
                    "ImGui capture getter return differs");
            }
            SDL_Event quit{}; quit.type = SDL_QUIT;
            Require(SDL_PushEvent(&quit) == 1, "Could not queue quit after resumed frame");
        }
    };

    void ApplicationLoop(std::string_view mode)
    {
        SDL_SetMainReady();
        RuntimeAssets::Initialize("GEngineEditor");
        {
            LoopApp app;
            WindowProperties properties;
            properties.m_Title = "Input and frame clock probe";
            properties.m_Width = properties.m_Height = 64;
            properties.m_MinWidth = properties.m_MinHeight = 32;
            properties.m_IsVsync = false;
            properties.flag = BitFlags<WindowFlags, uint8_t>{ WindowFlags::INVISIBLE };
            app.Initialize(properties);
            auto* window = app.GetSDLWindow();
            const auto id = window->GetWindowID();
            Require(window->GetSDLWindow() && window->GetContext() && window->GetTitle() == properties.m_Title,
                "Active window getter return differs");
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {}
            // Logical visibility events exercise the production state machine while
            // keeping the fixture's native GL window off the user's desktop.
            PushState(id, SDL_WINDOWEVENT_SHOWN);
            app.OnEvent(event);
            auto* resize = new Events<void(WindowResizeParam)>("WindowResize");
            resize->Subscribe([&](WindowResizeParam param) {
                ++app.resizeEvents;
                app.Suspend(param.Width == 0 || param.Height == 0);
            });
            BaseApp::GetEventManager()->GetEventDispatcher().RegisterEvent(resize);
            const bool resume = mode == "--loop-resume", minimize = mode == "--loop-minimize";
            app.measureClock = mode == "--loop-clock";
            const bool activeInput = mode == "--loop-input" || app.measureClock;
            app.Suspend(!minimize && !activeInput);
            if (minimize) PushWindowEvent(id, 0);
            if (activeInput)
            {
                SDL_Event motion{}; motion.type = SDL_MOUSEMOTION;
                motion.motion.windowID = id; motion.motion.x = 10001; motion.motion.y = 10003;
                motion.motion.xrel = 7; motion.motion.yrel = -9;
                Require(SDL_PushEvent(&motion) == 1, "Could not queue active-frame motion");
            }
            std::atomic<bool> queued{false};
            // This worker only enqueues SDL events. All input/GL/UI/loop work stays on main.
            std::jthread events([&] {
                if (activeInput) { queued = true; return; }
                bool motionQueued = true;
                if (resume)
                {
                    // Queue motion well before restore so it belongs to suspended input,
                    // not the first active event batch after the restore notification.
                    std::this_thread::sleep_for(std::chrono::milliseconds(450));
                    SDL_Event motion{}; motion.type = SDL_MOUSEMOTION;
                    motion.motion.windowID = id; motion.motion.x = 10001; motion.motion.y = 10003;
                    motion.motion.xrel = 7; motion.motion.yrel = -9;
                    motionQueued = SDL_PushEvent(&motion) == 1;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(resume ? 450 : 150));
                SDL_Event signal{};
                signal.type = resume ? SDL_WINDOWEVENT : SDL_QUIT;
                if (resume)
                {
                    signal.window.windowID = id; signal.window.event = SDL_WINDOWEVENT_RESIZED;
                    signal.window.data1 = signal.window.data2 = 64;
                }
                queued = SDL_PushEvent(&signal) == 1 && motionQueued;
            });
            app.Run();
            events.join();
            Require(queued, "Worker failed to enqueue control event");
            if (resume)
            {
                Require(app.resizeEvents == 1 && app.updates == 1 && app.renders == 1 && app.controls == 1,
                    "Suspended loop ran controls/update/render or failed to resume");
                Require(app.firstSeconds >= 0 && app.firstSeconds < 0.5, "Suspended time leaked into simulation timestep");
                // Motion drained while suspended must not leak into the next untouched active frame.
                Require(app.motionFrames == 0, "Suspended motion leaked into a later active frame");
            }
            else if (app.measureClock)
                Require(app.controls == 2 && app.updates == 2 && app.renders == 2
                    && app.firstSeconds >= .016 && app.stalledSeconds >= .350,
                    "Paced/stalled production frame sequence differs");
            else if (activeInput)
                Require(app.controls == 1 && app.updates == 1 && app.renders == 1 && app.motionFrames == 1,
                    "Active input was stale, repeated, or not observed");
            else
                Require(app.updates == 0 && app.renders == 0 && app.controls == 0,
                    "Suspended close or immediate minimize ran application work");
            app.Suspend(false);
            std::cout << "[PASS] " << mode << " controls=" << app.controls << " updates=" << app.updates
                << " renders=" << app.renders << " first-seconds=" << app.firstSeconds
                << " stalled-seconds=" << app.stalledSeconds << '\n';
        }
        BaseApp::GetEngine().ReleasePlatform();
    }

    void NativeWindowLoop(std::string_view mode)
    {
        SDL_SetMainReady();
        RuntimeAssets::Initialize("GEngineEditor");
        {
            LoopApp app;
            WindowProperties properties;
            properties.m_Title = "Suspended event loop probe";
            properties.m_Width = properties.m_Height = 64;
            properties.m_MinWidth = properties.m_MinHeight = 32;
            properties.m_IsVsync = false;
            properties.flag = BitFlags<WindowFlags, uint8_t>{WindowFlags::INVISIBLE};
            app.Initialize(properties);
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {}
            const auto id = app.GetSDLWindow()->GetWindowID();
            Require(app.IsRenderingSuspended(), "Initially hidden window was treated as renderable");
            const bool restore = mode == "--native-restore";
            const bool show = mode == "--hidden-show";
            const bool hidden = mode == "--hidden-quit" || show;
            const bool resize = mode == "--native-resize";
            if (show)
            {
                PushState(id, SDL_WINDOWEVENT_SHOWN);
                PushState(id, SDL_WINDOWEVENT_HIDDEN);
                PushState(id, SDL_WINDOWEVENT_RESTORED);
                app.OnEvent(event);
                Require(app.IsRenderingSuspended(), "Restore overrode hidden window state");
            }
            if (!hidden)
            {
                PushState(id, SDL_WINDOWEVENT_SHOWN);
                PushState(id, SDL_WINDOWEVENT_MINIMIZED);
                // Neither a foreign window nor a resize can restore a minimized main window.
                PushState(id + 99, SDL_WINDOWEVENT_RESTORED);
                PushState(id + 99, SDL_WINDOWEVENT_CLOSE);
                PushState(id, SDL_WINDOWEVENT_SIZE_CHANGED, 96, 64);
                app.OnEvent(event);
                Require(app.IsRenderingSuspended(), "Resize/foreign-window event overrode minimize");
                if (resize)
                {
                    PushState(id, SDL_WINDOWEVENT_SIZE_CHANGED, 0, 64);
                    PushState(id, SDL_WINDOWEVENT_RESTORED);
                    app.OnEvent(event);
                    Require(app.IsRenderingSuspended(), "Restore overrode zero drawable extent");
                }
            }
            std::atomic<bool> queued{false};
            const double cpuStart = ProcessCpuSeconds();
            const auto wallStart = FrameClock::Clock::now();
            std::jthread events([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(900));
                SDL_Event signal{};
                if (hidden && !show) signal.type = SDL_QUIT;
                else
                {
                    signal.type = SDL_WINDOWEVENT; signal.window.windowID = id;
                    signal.window.event = show ? SDL_WINDOWEVENT_SHOWN : resize ? SDL_WINDOWEVENT_SIZE_CHANGED
                        : restore ? SDL_WINDOWEVENT_RESTORED : SDL_WINDOWEVENT_CLOSE;
                    signal.window.data1 = 96; signal.window.data2 = 64;
                }
                queued = SDL_PushEvent(&signal) == 1;
            });
            app.Run();
            events.join();
            const double wall = Seconds(FrameClock::Clock::now() - wallStart).count();
            const double cpu = ProcessCpuSeconds() - cpuStart;
            Require(queued && wall >= .9 && wall < 3.0, "Suspended control event was lost or starved");
            const int work = restore || resize || show ? 1 : 0;
            Require(app.controls == work && app.updates == work && app.renders == work,
                "Suspended window performed application work or failed to restore");
            Require(app.firstSeconds < .5, "Suspended wall time leaked into first resumed update");
            // Observation excludes context/resource startup and counts all process threads.
            Require(cpu / wall < .25, "Idle loop consumed more than 25% of one logical CPU");
            std::cout << "[PASS] suspended-cpu mode=" << mode << " wall-seconds=" << wall
                << " process-cpu-seconds=" << cpu << " one-core-percent=" << cpu / wall * 100
                << " controls=" << app.controls << " updates=" << app.updates << " renders=" << app.renders << '\n';
        }
        BaseApp::GetEngine().ReleasePlatform();
    }

    void CadenceChecks()
    {
        // Exercise the production accumulator at additional presentation rates.
        for (int hz : {30, 60, 75, 120, 144, 240})
        {
            _Scene scene;
            scene.OnRuntimeStart();
            for (int frame = 0; frame < hz * 10; ++frame)
            {
                scene.Update(Timestep(1.0 / hz));
                const auto& timing = scene.GetPhysicsTiming();
                Require(timing.stepsLastUpdate <= 2 && timing.totalDiscardedSeconds == 0,
                    "Refresh-rate sweep exceeded fixed work budget or lost time");
            }
            const auto& timing = scene.GetPhysicsTiming();
            Require(timing.totalSteps == 600 && timing.pendingSeconds < 1e-10,
                "Ten seconds of presentation samples did not produce 600 fixed ticks");
            std::cout << "[PASS] cadence render-hz=" << hz << " fixed-ticks=" << timing.totalSteps
                << " pending-seconds=" << timing.pendingSeconds << '\n';
        }
    }

    class PacingApp final : public BaseApp
    {
    public:
        _Scene scene;
        int updates = 0, renders = 0, swaps = 0;
        bool stallResume = false, switchPacing = false;
        double supplied = 0, firstSeconds = 0, stalledSeconds = 0, resumedSeconds = 0;
        double retainedBeforeSuspend = 0;
        std::uint64_t stepsBeforeSuspend = 0;
        std::atomic<bool> restored{false};
        std::jthread events;
        std::vector<double> deltas;

        void ProcessInput(Timestep ts) override
        {
            Require(ts.GetDuration() == GetFrameTime().renderDelta, "Pacing changed input time units");
        }
        void Update(Timestep ts) override
        {
            ++updates;
            const double elapsed = ts.GetSecondsPrecise();
            if (updates == 1) firstSeconds = elapsed;
            deltas.push_back(elapsed);
            Require(ts.GetDuration() == GetFrameTime().rawDelta, "Pacing hid elapsed simulation time");
            scene.Update(ts);
            supplied += elapsed;
            const auto& timing = scene.GetPhysicsTiming();
            Require(timing.stepsLastUpdate <= 2 && timing.pendingSeconds <= .25
                && std::abs(supplied - (timing.totalSteps * _Scene::PhysicsStepSeconds
                    + timing.pendingSeconds + timing.totalDiscardedSeconds)) < 1e-10,
                "Live loop violated work bound or elapsed-time conservation");
            if (stallResume && updates == 2)
            {
                stalledSeconds = elapsed;
                Require(elapsed >= .350 && timing.stepsLastUpdate == 2 && timing.discardedSeconds >= .1,
                    "Stall did not retain bounded backlog and report catastrophic overflow");
                retainedBeforeSuspend = timing.pendingSeconds;
                stepsBeforeSuspend = timing.totalSteps;
            }
            if (stallResume && updates == 3)
            {
                resumedSeconds = elapsed;
                Require(elapsed < .25 && timing.stepsLastUpdate == 2 && timing.discardedSeconds == 0
                    && timing.totalSteps == stepsBeforeSuspend + 2
                    && std::abs(timing.pendingSeconds - (retainedBeforeSuspend + elapsed
                        - 2 * _Scene::PhysicsStepSeconds)) < 1e-10,
                    "Native suspension added time or lost the pre-existing physics backlog");
            }
        }
        void Render() override
        {
            ++renders;
            glClear(GL_COLOR_BUFFER_BIT);
            GetSDLWindow()->SwapBuffer();
            ++swaps;
            if (stallResume && renders == 1)
                std::this_thread::sleep_for(std::chrono::milliseconds(350));
            if (stallResume && renders == 2)
            {
                const auto id = GetSDLWindow()->GetWindowID();
                PushState(id, SDL_WINDOWEVENT_MINIMIZED);
                events = std::jthread([this, id] {
                    // Only enqueue events here; all context and physics work stays on main.
                    std::this_thread::sleep_for(std::chrono::milliseconds(900));
                    SDL_Event event{};
                    event.type = SDL_WINDOWEVENT;
                    event.window.windowID = id; event.window.event = SDL_WINDOWEVENT_RESTORED;
                    restored = SDL_PushEvent(&event) == 1;
                });
            }
            if (switchPacing)
            {
                if (renders == 1) SetManualFrameRateLimit(0);
                if (renders == 2)
                {
                    SetManualFrameRateLimit(2);
                    Require(SDL_GL_SetSwapInterval(1) == 0 && SDL_GL_GetSwapInterval() == 1,
                        "Could not enable VSYNC during pacing transition");
                }
                if (renders == 3)
                    Require(SDL_GL_SetSwapInterval(0) == 0 && SDL_GL_GetSwapInterval() == 0,
                        "Could not disable VSYNC during pacing transition");
            }
            if (renders == (stallResume ? 3 : 4))
            {
                SDL_Event quit{}; quit.type = SDL_QUIT;
                Require(SDL_PushEvent(&quit) == 1, "Could not queue pacing probe quit");
            }
        }
    };

    void PacingLoop(std::string_view mode)
    {
        SDL_SetMainReady();
        RuntimeAssets::Initialize("GEngineEditor");
        {
            PacingApp app;
            WindowProperties properties;
            properties.m_Title = "Frame pacing probe";
            properties.m_Width = properties.m_Height = 64;
            properties.m_MinWidth = properties.m_MinHeight = 32;
            properties.m_IsVsync = mode == "--pacing-on-cap" || mode == "--pacing-on-unlimited";
            properties.flag = BitFlags<WindowFlags, uint8_t>{WindowFlags::INVISIBLE};
            app.Initialize(properties);
            const int interval = SDL_GL_GetSwapInterval();
            Require(interval == (properties.m_IsVsync ? 1 : 0), "Requested swap interval is unavailable");
            Require(app.GetManualFrameRateLimit() == 60, "Default fallback cap is not 60 FPS");
            app.stallResume = mode == "--pacing-stall-resume";
            app.switchPacing = mode == "--pacing-switch";
            const bool capped = mode == "--pacing-off-cap" || mode == "--pacing-on-cap" || app.switchPacing;
            if (!app.stallResume) app.SetManualFrameRateLimit(capped ? 2 : 0);
            app.scene.OnRuntimeStart();
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {}
            PushState(app.GetSDLWindow()->GetWindowID(), SDL_WINDOWEVENT_SHOWN);
            app.OnEvent(event);
            app.Run();
            if (app.events.joinable()) app.events.join();
            Require(app.updates == app.renders && app.renders == app.swaps
                && app.renders == (app.stallResume ? 3 : 4),
                "Catch-up or suspension repeated/skipped a visible-frame render or swap");
            if (app.stallResume)
                Require(app.restored && app.firstSeconds >= 1.0 / 60.0,
                    "Default manual cap or restore event failed");
            else if (app.switchPacing)
                Require(app.deltas[0] >= .5 && app.deltas[1] < .4
                    && app.deltas[2] < .4 && app.deltas[3] >= .5,
                    "Runtime cap/VSYNC changes did not select the next frame's pacing");
            else if (capped && interval == 0)
                for (double delta : app.deltas) Require(delta >= .5, "Manual cap did not limit frame starts");
            else
                for (double delta : app.deltas) Require(delta < .4,
                    "VSYNC or uncapped mode still used the slow manual cap");
            std::cout << "[PASS] pacing mode=" << mode << " actual-swap-interval=" << interval
                << " updates=" << app.updates << " renders=" << app.renders << " swaps=" << app.swaps
                << " fixed-ticks=" << app.scene.GetPhysicsTiming().totalSteps
                << " pending=" << app.scene.GetPhysicsTiming().pendingSeconds
                << " discarded=" << app.scene.GetPhysicsTiming().totalDiscardedSeconds
                << " first-seconds=" << app.firstSeconds << " stall=" << app.stalledSeconds
                << " resumed=" << app.resumedSeconds << " deltas=";
            for (double delta : app.deltas) std::cout << delta << ',';
            std::cout << '\n';
        }
        BaseApp::GetEngine().ReleasePlatform();
    }

#if defined(GENGINE_PROBE_SIMULATION) || defined(GENGINE_PROBE_RAY)
    unsigned imageUploads = 0;
    PFNGLTEXIMAGE2DPROC originalImage = nullptr;
    PFNGLTEXSUBIMAGE2DPROC originalSubImage = nullptr;
    void APIENTRY ImageUpload(GLenum target, GLint level, GLint format, GLsizei w, GLsizei h,
        GLint border, GLenum externalFormat, GLenum type, const void* pixels)
    {
        if (pixels) ++imageUploads;
        originalImage(target, level, format, w, h, border, externalFormat, type, pixels);
    }
    void APIENTRY SubImageUpload(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
        GLenum format, GLenum type, const void* pixels)
    {
        if (pixels) ++imageUploads;
        originalSubImage(target, level, x, y, w, h, format, type, pixels);
    }

    void ApplicationViewport()
    {
        SDL_SetMainReady();
#if defined(GENGINE_PROBE_SIMULATION)
        RuntimeAssets::Initialize("RigidBodySimulation");
        using App = RigidBodySimulationApp;
#else
        RuntimeAssets::Initialize("RayTracing");
        using App = RayTracingAPP;
#endif
        {
            App app;
            WindowProperties properties;
            properties.m_Width = 640; properties.m_Height = 480;
            properties.m_MinWidth = properties.m_MinHeight = 32;
            properties.m_IsVsync = false;
            properties.flag = BitFlags<WindowFlags, uint8_t>{WindowFlags::INVISIBLE};
            app.Initialize(properties);
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {}
            PushState(app.GetSDLWindow()->GetWindowID(), SDL_WINDOWEVENT_SHOWN);
            app.OnEvent(event);
            originalImage = glad_glTexImage2D; originalSubImage = glad_glTexSubImage2D;
            glad_glTexImage2D = ImageUpload; glad_glTexSubImage2D = SubImageUpload;
            struct Restore { ~Restore() { glad_glTexImage2D = originalImage; glad_glTexSubImage2D = originalSubImage; } } restore;
            auto frame = [&] {
                imageUploads = 0;
                app.Update(Timestep(0.0));
                app.Render();
                Require(!app.IsRenderingSuspended(), "Empty docked viewport suspended its own UI");
            };
            frame(); // Create the real application UI.
            ImGui::SetWindowSize("Viewport", ImVec2(160, 160), ImGuiCond_Always);
            ImGui::SetWindowCollapsed("Viewport", false, ImGuiCond_Always);
            frame(); frame();
#if defined(GENGINE_PROBE_SIMULATION)
            Require(RenderSystem::GetRenderStats().m_ArrayDrawCall + RenderSystem::GetRenderStats().m_ElementsDrawCall > 0,
                "Visible simulation viewport did not render");
#else
            Require(imageUploads > 0, "Visible ray viewport did not upload an image");
#endif
            ImGui::SetWindowCollapsed("Viewport", true, ImGuiCond_Always);
            frame(); // The simulation consumes visibility on the following frame.
            for (int i = 0; i < 3; ++i)
            {
                frame();
#if defined(GENGINE_PROBE_SIMULATION)
                Require(!app.HasVisibleViewport() && RenderSystem::GetRenderStats().m_ArrayDrawCall == 0
                    && RenderSystem::GetRenderStats().m_ElementsDrawCall == 0, "Collapsed simulation viewport submitted scene draws");
#else
                Require(imageUploads == 0, "Collapsed ray viewport still generated/uploaded an image");
#endif
            }
            ImGui::SetWindowCollapsed("Viewport", false, ImGuiCond_Always);
            frame(); frame();
#if defined(GENGINE_PROBE_SIMULATION)
            Require(app.HasVisibleViewport() && RenderSystem::GetRenderStats().m_ArrayDrawCall
                + RenderSystem::GetRenderStats().m_ElementsDrawCall > 0, "Reopened simulation viewport did not resume scene rendering");
#else
            Require(imageUploads > 0, "Reopened ray viewport did not resume image generation");
#endif
            std::cout << "[PASS] application-viewport visible/collapsed/reopened UI remains live\n";
        }
        BaseApp::GetEngine().ReleasePlatform();
    }
#endif
}

int main(int argc, char** argv)
{
    try
    {
        const std::string_view mode = argc == 2 ? argv[1] : "";
        if (mode == "--state") { InitialStateAndButtons(); UntouchedFrames(); }
        else if (mode == "--clock") FrameClockChecks();
        else if (mode == "--cadence") CadenceChecks();
        else if (mode == "--pacing-off-cap" || mode == "--pacing-off-unlimited"
            || mode == "--pacing-on-cap" || mode == "--pacing-on-unlimited"
            || mode == "--pacing-stall-resume" || mode == "--pacing-switch") PacingLoop(mode);
        else if (mode == "--native-restore" || mode == "--native-close" || mode == "--native-resize"
            || mode == "--hidden-quit" || mode == "--hidden-show") NativeWindowLoop(mode);
#if defined(GENGINE_PROBE_SIMULATION) || defined(GENGINE_PROBE_RAY)
        else if (mode == "--viewport") ApplicationViewport();
#endif
        else if (mode == "--loop-resume" || mode == "--loop-close" || mode == "--loop-minimize"
            || mode == "--loop-input" || mode == "--loop-clock") ApplicationLoop(mode);
        else throw std::runtime_error("Expected --state, --clock, --loop-resume, --loop-close, --loop-minimize, --loop-input or --loop-clock");
        std::cout << "[PASS] input-control-safety " << mode << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        BaseApp::GetEngine().ReleasePlatform();
        return 1;
    }
}
