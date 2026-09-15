// Focused production-library tests; run through test_input_control.py.
#include "gepch.h"
#include "Core/BaseApp.h"
#include "Core/RuntimeAssets.h"
#include "Windows/SDLWindow.h"
#include <imgui/imgui.h>
#include <array>
#include <new>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace GEngine;
    using namespace ::GEngine::Manager;

    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
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

    class LoopApp : public BaseApp
    {
    public:
        int controls = 0, updates = 0, renders = 0, resizeEvents = 0;
        double firstSeconds = 0;
        int motionFrames = 0;
        void Suspend(bool suspended) { m_Minimized = suspended; }
        bool Suspended() const { return m_Minimized; }
        void ProcessInput(Timestep ts) override
        {
            ++controls;
            BaseApp::ProcessInput(ts);
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
        }
        void Render() override
        {
            ++renders;
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
            properties.m_Title = "Phase 09 input control probe";
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
            auto* resize = new Events<void(WindowResizeParam)>("WindowResize");
            resize->Subscribe([&](WindowResizeParam param) {
                ++app.resizeEvents;
                app.Suspend(param.Width == 0 || param.Height == 0);
            });
            BaseApp::GetEventManager()->GetEventDispatcher().RegisterEvent(resize);
            const bool resume = mode == "--loop-resume", minimize = mode == "--loop-minimize";
            const bool activeInput = mode == "--loop-input";
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
                Require(app.resizeEvents == 1 && app.updates == 1 && app.renders == 1 && app.controls == 2,
                    "Suspended loop ran controls/update/render or failed to resume");
                Require(app.firstSeconds >= 0 && app.firstSeconds < 0.5, "Suspended time leaked into simulation timestep");
                // Motion drained while suspended must not leak into the next untouched active frame.
                Require(app.motionFrames == 0, "Suspended motion leaked into a later active frame");
            }
            else if (activeInput)
                Require(app.controls == 2 && app.updates == 1 && app.renders == 1 && app.motionFrames == 1,
                    "Active input was stale, repeated, or not observed");
            else
                Require(app.updates == 0 && app.renders == 0 && app.controls == (minimize ? 1 : 0),
                    "Suspended close or immediate minimize ran application work");
            app.Suspend(false);
            std::cout << "[PASS] " << mode << " controls=" << app.controls << " updates=" << app.updates
                << " renders=" << app.renders << " first-seconds=" << app.firstSeconds << '\n';
        }
        BaseApp::GetEngine().ReleasePlatform();
    }
}

int main(int argc, char** argv)
{
    try
    {
        const std::string_view mode = argc == 2 ? argv[1] : "";
        if (mode == "--state") { InitialStateAndButtons(); UntouchedFrames(); }
        else if (mode == "--loop-resume" || mode == "--loop-close" || mode == "--loop-minimize"
            || mode == "--loop-input") ApplicationLoop(mode);
        else throw std::runtime_error("Expected --state, --loop-resume, --loop-close, --loop-minimize or --loop-input");
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
