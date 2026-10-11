#pragma once
#include "Managers/ManagerBase.h"
#include "Core/Platform.h"
#include "Events/Event.h"
#include "Math/Math.h"
#include "Inputs/KeyCodes.h"
#include "Inputs/MouseCodes.h"
#include "Inputs/ControllerCodes.h"
#include <array>
#include <bitset>
#include <string_view>

#define GENGINE_MAX_KEYCODES 512
#define GENGINE_BUTTON(x) (1u << ((x) - 1))
#define GENGINE_CONTROLLER_BUTTON_MAX 21
// Retained compatibility spellings; semantic contracts live in Manager.
using namespace GEngine::Input::Key;
using namespace GEngine::Input::Mouse;
using namespace GEngine::Input::Controller;
namespace GEngine
{
    class Window;
}
enum class CursorMode
{
    NORMAL,
    HIDDEN,
    LOCKED
};

namespace GEngine::Manager
{
    using namespace Math;
    enum class ButtonState
    {
        None,
        Pressed,
        Released,
        Held
    };
    enum class InputLayer
    {
        None,
        UI,
        View,
        Game
    };
    enum class InputKind
    {
        KeyDown,
        KeyUp,
        Text,
        Composition,
        Motion,
        ButtonDown,
        ButtonUp,
        Wheel,
        FocusGained,
        FocusLost,
        Cancel
    };
    enum class InputCancelReason
    {
        Explicit,
        FocusLoss,
        Hidden,
        Transfer,
        OwnerRelease,
        Shutdown
    };
    enum class InputError
    {
        WrongThread,
        Closed,
        InvalidKey,
        InvalidButton,
        InvalidWindow,
        InvalidText,
        Allocation,
        Delivery,
        Capture,
        Reentrant,
        IdentityExhausted
    };
    using InputResult = std::expected<void, InputError>;
    enum InputModifier : std::uint16_t
    {
        LeftShift = 1,
        RightShift = 2,
        LeftControl = 4,
        RightControl = 8,
        LeftAlt = 16,
        RightAlt = 32,
        LeftSuper = 64,
        RightSuper = 128,
        CapsLock = 256,
        NumLock = 512
    };

    // Move-only ownership; Clone explicitly reports allocation failure.
    struct InputEvent
    {
        InputKind kind = InputKind::Motion;
        std::uint32_t window = 0;
        GEngineKeyCode key = GENGINE_KEY_UNKNOWN;
        GEngineMouseCode button = GENGINE_BUTTON_LEFT;
        bool repeated = false;
        std::uint16_t modifiers = 0;
        float x = 0, y = 0, wheelX = 0, wheelY = 0;
        std::int32_t dx = 0, dy = 0, compositionStart = 0, compositionLength = 0;
        std::uint64_t sequence = 0;
        InputLayer destination = InputLayer::None;
        InputCancelReason cancellation = InputCancelReason::Explicit;
        bool cleanup = false, handled = false;
        std::unique_ptr<char[]> text;
        std::size_t textBytes = 0;
        std::string_view Text() const noexcept
        {
            return {text ? text.get() : "", textBytes};
        }
        [[nodiscard]] InputResult SetText(std::string_view value);
        [[nodiscard]] std::expected<InputEvent, InputError> Clone() const;
    };

    class InputDelivery
    {
        friend class InputManager;
        bool handled = false;
        void* context = nullptr;
        void (*commit)(void*, const InputEvent&) = nullptr;

    public:
        const InputEvent& event;
        explicit InputDelivery(const InputEvent& value) : event(value) {}
        void Handle() noexcept
        {
            if (!handled)
            {
                handled = true;
                if (commit)
                    commit(context, event);
            }
        }
        bool Handled() const noexcept
        {
            return handled;
        }
    };

    struct InputRouting
    {
        std::uint32_t window = 0;
        float left = 0, top = 0, right = 0, bottom = 0;
        bool visible = false, focused = false;
        bool uiMouse = false, uiKeyboard = false;
    };
    struct InputRect
    {
        float left, top, right, bottom;
    };

    class KeyboardState
    {
        friend class InputManager;
        std::bitset<GENGINE_MAX_KEYCODES> held, pressed, released;
        std::array<std::uint64_t, GENGINE_MAX_KEYCODES> versions{};

    public:
        ButtonState GetKeyState(GEngineKeyCode key) const;
        bool IsKeyHeld(GEngineKeyCode key) const
        {
            return static_cast<unsigned>(key) < held.size() && held[key];
        }
        bool IsKeyPressed(GEngineKeyCode key) const
        {
            return static_cast<unsigned>(key) < pressed.size() && pressed[key];
        }
        bool IsKeyReleased(GEngineKeyCode key) const
        {
            return static_cast<unsigned>(key) < released.size() && released[key];
        }
    };

    class MouseState
    {
        friend class InputManager;
        std::array<std::uint64_t, 6> versions{};
        std::uint64_t positionVersion = 0;

    public:
        const Vector2& GetPosition() const
        {
            return m_MousePos;
        }
        const Vector2& GetScrollWheel() const
        {
            return m_ScrollWheel;
        }
        void SetScrollWheel(const Vector2& value)
        {
            m_ScrollWheel = value;
        }
        void SetMouseRelative(bool value)
        {
            m_IsRelative = value;
        }
        bool GetButtonValue(GEngineMouseCode button) const;
        ButtonState GetButtonState(GEngineMouseCode button) const;
        bool isButtonPressed(GEngineMouseCode button) const;
        bool isButtonHeld(GEngineMouseCode button) const
        {
            return GetButtonValue(button);
        }
        bool isButtonReleased(GEngineMouseCode button) const;
        bool IsRelative() const
        {
            return m_IsRelative;
        }
        std::int32_t GetDX() const
        {
            return m_XRel;
        }
        std::int32_t GetDY() const
        {
            return m_YRel;
        }
        float GetDWheel() const
        {
            return m_ScrollWheel.y;
        }
        void SetCursorMode(CursorMode mode);
        Vector2 m_MousePos{}, m_ScrollWheel{};
        std::uint32_t m_CurrentButtons = 0, m_PreviousButtons = 0;
        std::uint32_t m_PressedButtons = 0, m_ReleasedButtons = 0;
        std::int32_t m_XRel = 0, m_YRel = 0;
        bool m_IsRelative = false;
    };

    class ControllerState
    {
    public:
        bool GetButtonValue(GEngineControllerCode button) const;
        ButtonState GetButtonState(GEngineControllerCode button) const;
        const Vector2& GetLeftStick() const
        {
            return m_LeftStick;
        }
        const Vector2& GetRightStick() const
        {
            return m_RightStick;
        }
        float GetLeftTrigger() const
        {
            return m_LeftTrigger;
        }
        float GetRightTrigger() const
        {
            return m_RightTrigger;
        }
        bool IsControllerConnected() const
        {
            return m_IsConnected;
        }
        bool isButtonPressed(GEngineControllerCode b) const
        {
            return GetButtonState(b) == ButtonState::Pressed;
        }
        bool isButtonHeld(GEngineControllerCode b) const
        {
            return GetButtonState(b) == ButtonState::Held;
        }
        bool isButtonReleased(GEngineControllerCode b) const
        {
            return GetButtonState(b) == ButtonState::Released;
        }

    private:
        friend class InputManager;
        std::uint8_t m_CurrentButtons[GENGINE_CONTROLLER_BUTTON_MAX]{};
        std::uint8_t m_PreviousButtons[GENGINE_CONTROLLER_BUTTON_MAX]{};
        Vector2 m_LeftStick{}, m_RightStick{};
        float m_LeftTrigger = 0, m_RightTrigger = 0;
        bool m_IsConnected = false;
    };

    struct InputState
    {
        KeyboardState m_Keyboard;
        MouseState m_Mouse;
        ControllerState m_Controller;
        std::uint16_t modifiers = 0;
        std::uint32_t textCommits = 0, compositionChanges = 0;
        bool compositionActive = false, focused = true;
    };

    class InputManager : public ManagerBase<InputManager>
    {
        friend class ManagerBase<InputManager>;

    public:
        static ScopedPtr<InputManager> GetScopedInstance();
        [[nodiscard]] PlatformResult Initialize();
        ~InputManager();
        void ShutDown();
        void PrepareForUpdate();
        void Update();
        void SetWindow(Window* window);
        const InputState& GetInputState() const
        {
            return m_States[2];
        }
        InputState& GetInputState()
        {
            return m_States[2];
        }
        KeyboardState& GetKeyboardState()
        {
            return m_States[2].m_Keyboard;
        }
        MouseState& GetMouseState()
        {
            return m_States[2].m_Mouse;
        }
        ControllerState& GetControllerState()
        {
            return m_States[2].m_Controller;
        }
        const InputState& GetViewState() const
        {
            return m_States[1];
        }
        const InputState& GetPhysicalState() const
        {
            return m_Physical;
        }
        const InputRouting& Routing() const
        {
            return m_Routing;
        }
        bool Focused() const
        {
            return m_Physical.focused;
        }
        InputLayer PointerCapture() const
        {
            return m_Capture;
        }
        bool ConsumeViewCancellation()
        {
            return std::exchange(m_ViewCancelled, false);
        }
        [[nodiscard]] InputResult PublishRouting(const InputRouting& routing);
        void BeginUIRouting()
        {
            m_UIBuildingCount = 0;
        }
        [[nodiscard]] InputResult AddUIRegion(InputRect region);
        [[nodiscard]] std::expected<InputLayer, InputError> Route(InputEvent& event);
        [[nodiscard]] InputResult CancelInput(InputCancelReason reason,
                                              InputLayer layer = InputLayer::None);
        [[nodiscard]] PlatformResult CapturePointer(InputLayer layer, bool relative);
        [[nodiscard]] PlatformResult ReleasePointer(InputLayer layer);
        [[nodiscard]] PlatformResult SetRelativeMouseMode(bool value);

        template <class F>
        auto Subscribe(InputLayer layer, F callback)
            -> std::expected<Subscription, SubscriptionError>
        {
            if (layer < InputLayer::UI || layer > InputLayer::Game)
                return std::unexpected(SubscriptionError::StaleTarget);
            return m_Routes[static_cast<unsigned>(layer) - 1].Subscribe(
                [callback = std::move(callback)](InputDelivery& delivery) mutable
                {
                    if (!delivery.Handled() || delivery.event.cleanup)
                        callback(delivery);
                });
        }
        template <class F> auto Observe(F observer)
        {
            return m_Trace.Subscribe(std::move(observer));
        }
        [[nodiscard]] SubscriptionResult ObserveCosts(SubscriptionObservations* observer);

    private:
        InputManager();
        float Filter1D(int input);
        Vector2 Filter2D(int x, int y);
        void Apply(InputState& state, const InputEvent& event);
        void Commit(InputLayer layer, const InputEvent& event);
        void RefreshModifiers(InputState& state, std::uint16_t locks);
        InputResult Deliver(InputLayer layer, InputEvent& event, bool forced);
        bool PointerInside(const InputEvent& event) const;
        InputState m_Physical;
        std::array<InputState, 3> m_States;
        std::array<TypedSubscriptions<void(InputDelivery&)>, 3> m_Routes;
        TypedSubscriptions<void(const InputEvent&)> m_Trace;
        std::array<InputLayer, GENGINE_MAX_KEYCODES> m_KeyOwners{};
        std::array<InputLayer, 6> m_ButtonOwners{};
        InputRouting m_Routing;
        std::unique_ptr<InputRect[]> m_UIRegions, m_UIBuilding;
        std::size_t m_UICount = 0, m_UIBuildingCount = 0, m_UICapacity = 0,
                    m_UIBuildingCapacity = 0;
        InputLayer m_Capture = InputLayer::None;
        bool m_ViewCancelled = false, m_Cancelling = false;
        std::uint64_t m_Sequence = 0;
        const std::thread::id m_Owner = std::this_thread::get_id();
        struct Backend;
        ScopedPtr<Backend> m_Backend;
        Window* m_Window = nullptr;
    };
}
