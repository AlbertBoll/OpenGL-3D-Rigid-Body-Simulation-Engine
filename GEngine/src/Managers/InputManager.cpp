#include "gepch.h"
#include "Managers/InputManager.h"
#include "Core/BaseApp.h"
#include "Core/Window.h"
#include <new>
#include <limits>
#ifdef GENGINE_PLATFORM_WINDOWS
#include <sdl2/SDL_syswm.h>
#endif

namespace GEngine::Manager
{
    struct InputManager::NativePointerTransition
    {
        bool relative = false, cancelling = false;
        int grab = -1; // -1 leaves grab unchanged; 0/1 releases/acquires it.
        InputLayer owner = InputLayer::None;
        std::uint64_t sequence = 0;
        int originX = 0, originY = 0, pixelX = 0, pixelY = 0;
        bool deferred = false;
    };
    struct InputManager::Backend
    {
        SDL_GameController* Controller = nullptr;
        std::array<NativePointerTransition, 256> transitions{};
        std::size_t transitionCount = 0;
        unsigned pollDepth = 0;
        std::optional<PlatformError> pollFailure;
        bool failing = false;
        std::uint32_t relativeWindow = 0;
        int relativeOriginX = 0, relativeOriginY = 0;
        int relativePixelX = 0, relativePixelY = 0;
    };
    InputManager::InputManager() = default;
    InputManager::~InputManager() { ShutDown(); }

    namespace
    {
        bool ValidText(std::string_view text)
        {
            for (std::size_t i = 0; i < text.size();)
            {
                const auto first = static_cast<unsigned char>(text[i++]);
                if (first < 0x80)
                    continue;
                unsigned count = first >= 0xF0 && first <= 0xF4   ? 3
                                 : first >= 0xE0 && first <= 0xEF ? 2
                                 : first >= 0xC2 && first <= 0xDF ? 1
                                                                  : 0;
                if (!count || i + count > text.size())
                    return false;
                std::uint32_t value = first & ((1u << (6 - count)) - 1);
                const unsigned original = count;
                while (count--)
                {
                    const auto next = static_cast<unsigned char>(text[i++]);
                    if ((next & 0xC0) != 0x80)
                        return false;
                    value = (value << 6) | (next & 0x3F);
                }
                if ((original == 1 && value < 0x80) || (original == 2 && value < 0x800) ||
                    (original == 3 && value < 0x10000) || value > 0x10FFFF ||
                    (value >= 0xD800 && value <= 0xDFFF))
                    return false;
            }
            return true;
        }
        bool ValidButton(GEngineMouseCode button)
        {
            return button >= GENGINE_BUTTON_LEFT && button <= GENGINE_BUTTON_X2;
        }
        bool KeyEvent(InputKind kind)
        {
            return kind == InputKind::KeyDown || kind == InputKind::KeyUp;
        }
        bool ButtonEvent(InputKind kind)
        {
            return kind == InputKind::ButtonDown || kind == InputKind::ButtonUp;
        }
        bool PointerEvent(InputKind kind)
        {
            return ButtonEvent(kind) || kind == InputKind::Motion || kind == InputKind::Wheel;
        }
    }

    InputResult InputEvent::SetText(std::string_view value)
    {
        if (!ValidText(value) || value.size() == std::numeric_limits<std::size_t>::max())
            return std::unexpected(InputError::InvalidText);
        std::unique_ptr<char[]> owned(new (std::nothrow) char[value.size() + 1]);
        if (!owned)
            return std::unexpected(InputError::Allocation);
        if (!value.empty())
            std::memcpy(owned.get(), value.data(), value.size());
        owned[value.size()] = 0;
        text = std::move(owned);
        textBytes = value.size();
        return {};
    }

    std::expected<InputEvent, InputError> InputEvent::Clone() const
    {
        InputEvent copy;
        copy.kind = kind;
        copy.window = window;
        copy.key = key;
        copy.button = button;
        copy.repeated = repeated;
        copy.modifiers = modifiers;
        copy.x = x;
        copy.y = y;
        copy.wheelX = wheelX;
        copy.wheelY = wheelY;
        copy.dx = dx;
        copy.dy = dy;
        copy.compositionStart = compositionStart;
        copy.compositionLength = compositionLength;
        copy.sequence = sequence;
        copy.destination = destination;
        copy.cancellation = cancellation;
        copy.cleanup = cleanup;
        copy.handled = handled;
        if (text)
            if (auto result = copy.SetText(Text()); !result)
                return std::unexpected(result.error());
        return copy;
    }

    ButtonState KeyboardState::GetKeyState(GEngineKeyCode key) const
    {
        if (IsKeyPressed(key))
            return ButtonState::Pressed;
        if (IsKeyReleased(key))
            return ButtonState::Released;
        return IsKeyHeld(key) ? ButtonState::Held : ButtonState::None;
    }
    bool MouseState::GetButtonValue(GEngineMouseCode button) const
    {
        return ValidButton(button) && (m_CurrentButtons & GENGINE_BUTTON(button)) != 0;
    }
    bool MouseState::isButtonPressed(GEngineMouseCode button) const
    {
        return ValidButton(button) && (m_PressedButtons & GENGINE_BUTTON(button)) != 0;
    }
    bool MouseState::isButtonReleased(GEngineMouseCode button) const
    {
        return ValidButton(button) && (m_ReleasedButtons & GENGINE_BUTTON(button)) != 0;
    }
    ButtonState MouseState::GetButtonState(GEngineMouseCode button) const
    {
        if (isButtonPressed(button))
            return ButtonState::Pressed;
        if (isButtonReleased(button))
            return ButtonState::Released;
        return GetButtonValue(button) ? ButtonState::Held : ButtonState::None;
    }
    void MouseState::SetCursorMode(CursorMode mode)
    {
        auto* input = BaseApp::GetInputManager();
        if (auto result = input->SetRelativeMouseMode(mode != CursorMode::NORMAL); !result)
            ReportPlatformError(result.error());
    }

    bool ControllerState::GetButtonValue(GEngineControllerCode button) const
    {
        if (static_cast<unsigned>(button) >= GENGINE_CONTROLLER_BUTTON_MAX) return false;
        return m_CurrentButtons[button] == 1;
    }


    ButtonState ControllerState::GetButtonState(GEngineControllerCode button) const
    {
        if (static_cast<unsigned>(button) >= GENGINE_CONTROLLER_BUTTON_MAX) return ButtonState::None;
        if (m_PreviousButtons[button] == 0)
        {
            if (m_CurrentButtons[button] == 0)
            {
                return ButtonState::None;
            }

            else
                return ButtonState::Pressed;
        }

        else
            if (m_CurrentButtons[button] == 0)
            {
                return ButtonState::Released;
            }

            else
                return ButtonState::Held;

    }

    ScopedPtr<InputManager> InputManager::GetScopedInstance()
    {
        struct Enable : InputManager
        {
        };
        return CreateScopedPtr<Enable>();
    }
    PlatformResult InputManager::Initialize()
    {
        ShutDown();
        m_Routes = {};
        m_Trace = {};
        m_Backend.reset(new (std::nothrow) Backend);
        if (!m_Backend)
            return std::unexpected(PlatformError{PlatformErrorCode::Allocation, "input backend",
                                                 "Input backend allocation failed"});
        m_Backend->Controller = SDL_GameControllerOpen(0);
        GetControllerState().m_IsConnected = m_Backend->Controller != nullptr;
        SDL_StartTextInput();
        return {};
    }
    void InputManager::ShutDown()
    {
        if (m_Backend)
        {
            // Teardown is outside delivery: never leave deferred native capture alive.
            m_Backend->pollDepth = 0;
            m_Backend->transitionCount = 0;
            if (auto result = CancelInput(InputCancelReason::Shutdown); !result)
                GENGINE_CORE_ERROR("Input shutdown cancellation failed: {}", int(result.error()));
            SDL_StopTextInput();
            if (m_Backend->Controller)
                SDL_GameControllerClose(m_Backend->Controller);
        }
        for (auto& route : m_Routes)
            route.Close();
        m_Trace.Close();
        m_Backend.reset();
        m_Window = nullptr;
        m_Physical = {};
        m_States = {};
        m_KeyOwners = {};
        m_ButtonOwners = {};
        m_Routing = {};
        m_UIRegions.reset();
        m_UIBuilding.reset();
        m_UICount = m_UIBuildingCount = m_UICapacity = m_UIBuildingCapacity = 0;
        m_Capture = InputLayer::None;
        m_ViewCancelled = false;
    }
    void InputManager::SetWindow(Window* window)
    {
        m_Window = window;
    }
    void InputManager::PrepareForUpdate()
    {
        const auto clear = [](InputState& state)
        {
            state.m_Keyboard.pressed.reset();
            state.m_Keyboard.released.reset();
            auto& mouse = state.m_Mouse;
            mouse.m_PreviousButtons = mouse.m_CurrentButtons;
            mouse.m_PressedButtons = mouse.m_ReleasedButtons = 0;
            mouse.m_XRel = mouse.m_YRel = 0;
            mouse.m_ScrollWheel = {};
            state.textCommits = state.compositionChanges = 0;
        };
        clear(m_Physical);
        for (auto& state : m_States)
            clear(state);
        auto& controller = GetControllerState();
        std::memcpy(controller.m_PreviousButtons, controller.m_CurrentButtons,
                    GENGINE_CONTROLLER_BUTTON_MAX);
    }
    void InputManager::Update()
    {
        // Keyboard/pointer state is exclusively event-driven. Preserve controller polling.
        auto& state = GetControllerState();
        if (!m_Backend || !m_Backend->Controller)
            return;
        for (int i = 0; i < GENGINE_CONTROLLER_BUTTON_MAX; ++i)
            state.m_CurrentButtons[i] = SDL_GameControllerGetButton(
                m_Backend->Controller, static_cast<SDL_GameControllerButton>(i));
        state.m_LeftTrigger = Filter1D(
            SDL_GameControllerGetAxis(m_Backend->Controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
        state.m_RightTrigger = Filter1D(
            SDL_GameControllerGetAxis(m_Backend->Controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
        state.m_LeftStick =
            Filter2D(SDL_GameControllerGetAxis(m_Backend->Controller, SDL_CONTROLLER_AXIS_LEFTX),
                     -SDL_GameControllerGetAxis(m_Backend->Controller, SDL_CONTROLLER_AXIS_LEFTY));
        state.m_RightStick =
            Filter2D(SDL_GameControllerGetAxis(m_Backend->Controller, SDL_CONTROLLER_AXIS_RIGHTX),
                     -SDL_GameControllerGetAxis(m_Backend->Controller, SDL_CONTROLLER_AXIS_RIGHTY));
    }
    void InputManager::RefreshModifiers(InputState& state, std::uint16_t locks)
    {
        state.modifiers = locks & (CapsLock | NumLock);
        constexpr std::array keys{GENGINE_KEY_LSHIFT, GENGINE_KEY_RSHIFT, GENGINE_KEY_LCTRL,
                                  GENGINE_KEY_RCTRL,  GENGINE_KEY_LALT,   GENGINE_KEY_RALT,
                                  GENGINE_KEY_LGUI,   GENGINE_KEY_RGUI};
        for (unsigned i = 0; i < keys.size(); ++i)
            if (state.m_Keyboard.IsKeyHeld(keys[i]))
                state.modifiers |= std::uint16_t(1u << i);
    }
    void InputManager::Apply(InputState& state, const InputEvent& event)
    {
        auto& keyboard = state.m_Keyboard;
        auto& mouse = state.m_Mouse;
        if (KeyEvent(event.kind))
        {
            const unsigned key = static_cast<unsigned>(event.key);
            if (event.sequence < keyboard.versions[key])
                return;
            keyboard.versions[key] = event.sequence;
            if (event.kind == InputKind::KeyDown)
            {
                if (!keyboard.held[key] && !event.repeated)
                    keyboard.pressed[key] = true;
                keyboard.held[key] = true;
            }
            else
            {
                if (keyboard.held[key])
                    keyboard.released[key] = true;
                keyboard.held[key] = false;
            }
            RefreshModifiers(state, event.modifiers);
        }
        if (ButtonEvent(event.kind))
        {
            const unsigned button = static_cast<unsigned>(event.button);
            if (event.sequence < mouse.versions[button])
                return;
            mouse.versions[button] = event.sequence;
            const auto mask = GENGINE_BUTTON(button);
            if (event.kind == InputKind::ButtonDown)
            {
                if (!(mouse.m_CurrentButtons & mask))
                    mouse.m_PressedButtons |= mask;
                mouse.m_CurrentButtons |= mask;
            }
            else
            {
                if (mouse.m_CurrentButtons & mask)
                    mouse.m_ReleasedButtons |= mask;
                mouse.m_CurrentButtons &= ~mask;
            }
        }
        if ((ButtonEvent(event.kind) || event.kind == InputKind::Motion) &&
            event.sequence >= mouse.positionVersion)
        {
            mouse.m_MousePos = Vector2{event.x, event.y};
            mouse.positionVersion = event.sequence;
        }
        if (event.kind == InputKind::Motion)
        {
            // Saturate only the legacy 32-bit snapshot; each original delta is delivered losslessly.
            const auto accumulate = [](std::int32_t a, std::int32_t b)
            {
                return static_cast<std::int32_t>(std::clamp(
                    std::int64_t(a) + b, std::int64_t(INT32_MIN), std::int64_t(INT32_MAX)));
            };
            mouse.m_XRel = accumulate(mouse.m_XRel, event.dx);
            mouse.m_YRel = accumulate(mouse.m_YRel, event.dy);
        }
        if (event.kind == InputKind::Wheel)
        {
            mouse.m_ScrollWheel.x += event.wheelX;
            mouse.m_ScrollWheel.y += event.wheelY;
        }
        if (event.kind == InputKind::Text)
        {
            ++state.textCommits;
            state.compositionActive = false;
        }
        if (event.kind == InputKind::Composition)
        {
            ++state.compositionChanges;
            state.compositionActive = !event.Text().empty();
        }
    }
    void InputManager::Commit(InputLayer layer, const InputEvent& event)
    {
        // A nested native input may supersede an outer event before it is handled.
        // Do not revive that older key/button state when the outer callback resumes.
        if (KeyEvent(event.kind) && m_Physical.m_Keyboard.versions[event.key] != event.sequence &&
            !event.cleanup)
            return;
        if (ButtonEvent(event.kind) &&
            m_Physical.m_Mouse.versions[event.button] != event.sequence && !event.cleanup)
            return;
        Apply(m_States[static_cast<unsigned>(layer) - 1], event);
        if (event.kind == InputKind::KeyDown)
            m_KeyOwners[event.key] = layer;
        if (event.kind == InputKind::KeyUp)
            m_KeyOwners[event.key] = InputLayer::None;
        if (event.kind == InputKind::ButtonDown)
            m_ButtonOwners[event.button] = layer;
        if (event.kind == InputKind::ButtonUp)
            m_ButtonOwners[event.button] = InputLayer::None;
    }
    InputResult InputManager::Deliver(InputLayer layer, InputEvent& event, bool forced)
    {
        struct Context
        {
            InputManager* input;
            InputLayer layer;
        } context{this, layer};
        InputDelivery delivery(event);
        delivery.context = &context;
        delivery.commit = [](void* opaque, const InputEvent& value)
        {
            auto& current = *static_cast<Context*>(opaque);
            current.input->Commit(current.layer, value);
        };
        event.destination = layer;
        if (event.cleanup)
            delivery.Handle();
        // Cleanup is notification to its original layer, not another action.
        if (auto result = m_Routes[static_cast<unsigned>(layer) - 1].Dispatch(delivery); !result)
            return std::unexpected(InputError::Delivery);
        event.handled = delivery.Handled();
        if (forced)
            delivery.Handle();
        if (!delivery.Handled())
            event.destination = InputLayer::None;
        return {};
    }
    bool InputManager::PointerInside(const InputEvent& event) const
    {
        const auto point = event.kind == InputKind::Wheel ? m_Physical.m_Mouse.GetPosition()
                                                          : Vector2{event.x, event.y};
        return m_Routing.visible && point.x >= m_Routing.left && point.x < m_Routing.right &&
               point.y >= m_Routing.top && point.y < m_Routing.bottom;
    }
    std::expected<InputLayer, InputError> InputManager::Route(InputEvent& event)
    {
        if (m_Owner != std::this_thread::get_id())
            return std::unexpected(InputError::WrongThread);
        if (m_Cancelling)
            return std::unexpected(InputError::Reentrant);
        if (!m_Backend)
            return std::unexpected(InputError::Closed);
        if (m_Backend->pollFailure)
            return std::unexpected(InputError::Capture);
        if (!m_Window || event.window != m_Window->GetWindowID())
            return std::unexpected(InputError::InvalidWindow);
        if (KeyEvent(event.kind) && (event.key == GENGINE_KEY_UNKNOWN ||
                                     static_cast<unsigned>(event.key) >= GENGINE_MAX_KEYCODES))
            return std::unexpected(InputError::InvalidKey);
        if (ButtonEvent(event.kind) && !ValidButton(event.button))
            return std::unexpected(InputError::InvalidButton);
        if ((event.kind == InputKind::Text || event.kind == InputKind::Composition) &&
            (!ValidText(event.Text()) || event.compositionStart < 0 || event.compositionLength < 0))
            return std::unexpected(InputError::InvalidText);
        if (m_Sequence == UINT64_MAX)
            return std::unexpected(InputError::IdentityExhausted);
        event.sequence = ++m_Sequence;
        event.destination = InputLayer::None;
        event.handled = false;
        event.cleanup = false;
        if (event.kind == InputKind::FocusLost || event.kind == InputKind::Cancel)
        {
            const auto cancelled =
                CancelInput(event.kind == InputKind::FocusLost ? InputCancelReason::FocusLoss
                                                               : event.cancellation);
            if (event.kind == InputKind::FocusLost)
            {
                m_Physical.focused = false;
                for (auto& state : m_States)
                    state.focused = false;
            }
            if (event.kind == InputKind::Cancel && event.cancellation == InputCancelReason::Hidden)
            {
                m_Routing.visible = m_Routing.focused = false;
                m_Physical.focused = false;
                for (auto& state : m_States)
                    state.focused = false;
            }
            if (!cancelled) return std::unexpected(cancelled.error());
            event.handled = true;
        }
        else if (event.kind == InputKind::FocusGained)
        {
            m_Physical.focused = true;
            for (auto& state : m_States)
                state.focused = true;
        }
        else if (!Focused() || (event.kind == InputKind::KeyDown && event.repeated &&
                                !m_Physical.m_Keyboard.IsKeyHeld(event.key)))
        {
            event.handled = true; // Neutral regain: a repeated held key is not a new press.
        }
        else
        {
            const bool key = KeyEvent(event.kind), button = ButtonEvent(event.kind);
            const bool release =
                event.kind == InputKind::KeyUp || event.kind == InputKind::ButtonUp;
            InputLayer owner = key      ? m_KeyOwners[event.key]
                               : button ? m_ButtonOwners[event.button]
                                        : InputLayer::None;
            if (event.kind == InputKind::KeyDown && m_Physical.m_Keyboard.IsKeyHeld(event.key))
                event.repeated = true;
            const bool buttonWasHeld = button && m_Physical.m_Mouse.GetButtonValue(event.button);
            Apply(m_Physical, event);
            event.modifiers = m_Physical.modifiers;
            if (release)
            {
                if (owner != InputLayer::None)
                {
                    event.cleanup = true;
                    if (auto result = Deliver(owner, event, true); !result)
                        return std::unexpected(result.error());
                }
                event.handled = true;
            }
            else if ((key || button) && owner == InputLayer::None &&
                     (event.repeated || (button && buttonWasHeld)))
            {
                event.handled = true; // A cancelled hold cannot transfer as a fresh gesture.
            }
            else if ((event.repeated || (button && buttonWasHeld)) && owner != InputLayer::None)
            {
                if (auto result = Deliver(owner, event, true); !result)
                    return std::unexpected(result.error());
            }
            else
            {
                const bool pointer = PointerEvent(event.kind);
                const auto point = event.kind == InputKind::Wheel ? m_Physical.m_Mouse.GetPosition()
                                                                  : Vector2{event.x, event.y};
                bool overUI = false;
                if (pointer)
                    for (std::size_t i = 0; i < m_UICount; ++i)
                    {
                        const auto& rect = m_UIRegions[i];
                        overUI |= point.x >= rect.left && point.x < rect.right &&
                                  point.y >= rect.top && point.y < rect.bottom;
                    }
                const bool ui =
                    pointer ? (m_Routing.uiMouse || (overUI && m_Capture == InputLayer::None) ||
                               (m_Routing.visible && !PointerInside(event) &&
                                m_Capture == InputLayer::None))
                            : m_Routing.uiKeyboard;
                if (pointer && m_Capture != InputLayer::None)
                {
                    if (auto result = Deliver(m_Capture, event, true); !result)
                        return std::unexpected(result.error());
                    event.handled = true;
                }
                else if (ui)
                {
                    if (auto result = Deliver(InputLayer::UI, event, true); !result)
                        return std::unexpected(result.error());
                    event.handled = true;
                }
                else
                {
                    if (auto result = Deliver(InputLayer::UI, event, false); !result)
                        return std::unexpected(result.error());
                    if (event.destination == InputLayer::None &&
                        (m_Capture == InputLayer::View ||
                         (m_Routing.visible &&
                          (pointer ? PointerInside(event) : m_Routing.focused))))
                    {
                        if (auto result = Deliver(InputLayer::View, event, false); !result)
                            return std::unexpected(result.error());
                    }
                    if (event.destination == InputLayer::None)
                        if (auto result = Deliver(InputLayer::Game, event, true); !result)
                            return std::unexpected(result.error());
                }
            }
        }
        if (auto traced = m_Trace.Dispatch(event); !traced)
            return std::unexpected(InputError::Delivery);
        return event.destination;
    }
    InputResult InputManager::PublishRouting(const InputRouting& routing)
    {
        if (m_Owner != std::this_thread::get_id())
            return std::unexpected(InputError::WrongThread);
        if (!m_Window || routing.window != m_Window->GetWindowID())
            return std::unexpected(InputError::InvalidWindow);
        if ((!routing.visible && m_Routing.visible) || (routing.uiMouse && !m_Routing.uiMouse) ||
            (routing.uiKeyboard && !m_Routing.uiKeyboard))
        {
            if (routing.uiMouse && m_Capture == InputLayer::Game)
                if (auto result = CancelInput(InputCancelReason::Transfer, InputLayer::Game); !result)
                    return result;
            if (auto result = CancelInput(routing.visible ? InputCancelReason::Transfer
                                                          : InputCancelReason::Hidden,
                                          InputLayer::View);
                !result)
                return result;
            if (routing.uiKeyboard)
                if (auto result = CancelInput(InputCancelReason::Transfer, InputLayer::Game);
                    !result)
                    return result;
        }
        m_Routing = routing;
        std::swap(m_UIRegions, m_UIBuilding);
        std::swap(m_UICapacity, m_UIBuildingCapacity);
        m_UICount = m_UIBuildingCount;
        m_UIBuildingCount = 0;
        return {};
    }
    InputResult InputManager::AddUIRegion(InputRect region)
    {
        if (m_Owner != std::this_thread::get_id())
            return std::unexpected(InputError::WrongThread);
        if (region.right <= region.left || region.bottom <= region.top)
            return {};
        if (m_UIBuildingCount == m_UIBuildingCapacity)
        {
            if (m_UIBuildingCapacity > SIZE_MAX / (2 * sizeof(InputRect)))
                return std::unexpected(InputError::Allocation);
            const auto capacity = m_UIBuildingCapacity ? m_UIBuildingCapacity * 2 : 4;
            std::unique_ptr<InputRect[]> next(new (std::nothrow) InputRect[capacity]);
            if (!next)
                return std::unexpected(InputError::Allocation);
            for (std::size_t i = 0; i < m_UIBuildingCount; ++i)
                next[i] = m_UIBuilding[i];
            m_UIBuilding = std::move(next);
            m_UIBuildingCapacity = capacity;
        }
        m_UIBuilding[m_UIBuildingCount++] = region;
        return {};
    }
    InputResult InputManager::CancelInput(InputCancelReason reason, InputLayer layer)
    {
        if (m_Owner != std::this_thread::get_id())
            return std::unexpected(InputError::WrongThread);
        if (m_Cancelling)
            return std::unexpected(InputError::Reentrant);
        const auto nativeBegin = m_Backend ? m_Backend->transitionCount : 0;
        m_Cancelling = true;
        struct Exit
        {
            bool& flag;
            ~Exit()
            {
                flag = false;
            }
        } exit{m_Cancelling};
        bool modeFailed = false;
        if (layer == InputLayer::None || layer == m_Capture)
        {
            // Cancellation invalidates logical ownership even if native release fails.
            modeFailed = !TransitionPointer(false, 0);
            m_Capture = InputLayer::None;
        }
        for (unsigned i = 0; i < m_States.size(); ++i)
        {
            const auto current = static_cast<InputLayer>(i + 1);
            if (layer != InputLayer::None && current != layer)
                continue;
            auto& state = m_States[i];
            for (unsigned key = 0; key < m_KeyOwners.size(); ++key)
            {
                if (m_KeyOwners[key] != current)
                    continue;
                InputEvent event;
                event.window = m_Window ? m_Window->GetWindowID() : 0;
                event.kind = InputKind::KeyUp;
                event.key = static_cast<GEngineKeyCode>(key);
                event.cleanup = true;
                event.cancellation = reason;
                event.sequence = ++m_Sequence;
                if (auto result = Deliver(current, event, true); !result)
                    return result;
                if (!m_Trace.Dispatch(event))
                    return std::unexpected(InputError::Delivery);
            }
            for (unsigned button = 1; button < m_ButtonOwners.size(); ++button)
            {
                if (m_ButtonOwners[button] != current)
                    continue;
                InputEvent event;
                event.window = m_Window ? m_Window->GetWindowID() : 0;
                event.kind = InputKind::ButtonUp;
                event.button = static_cast<GEngineMouseCode>(button);
                event.x = state.m_Mouse.m_MousePos.x;
                event.y = state.m_Mouse.m_MousePos.y;
                event.cleanup = true;
                event.cancellation = reason;
                event.sequence = ++m_Sequence;
                if (auto result = Deliver(current, event, true); !result)
                    return result;
                if (!m_Trace.Dispatch(event))
                    return std::unexpected(InputError::Delivery);
            }
            state.m_Keyboard.pressed.reset();
            state.m_Mouse.m_PressedButtons = 0;
            state.m_Mouse.m_XRel = state.m_Mouse.m_YRel = 0;
            state.m_Mouse.m_ScrollWheel = {};
            state.modifiers = 0;
            state.compositionActive = false;
            state.textCommits = 0;
            ++state.compositionChanges;
            if (current == InputLayer::View)
                m_ViewCancelled = true;
            InputEvent cancelled;
            cancelled.kind = InputKind::Cancel;
            cancelled.window = m_Window ? m_Window->GetWindowID() : 0;
            cancelled.cleanup = true;
            cancelled.cancellation = reason;
            cancelled.sequence = ++m_Sequence;
            if (auto result = Deliver(current, cancelled, true); !result)
                return result;
            if (!m_Trace.Dispatch(cancelled))
                return std::unexpected(InputError::Delivery);
        }
        if (layer == InputLayer::None)
        {
            m_Physical.m_Keyboard.released |= m_Physical.m_Keyboard.held;
            m_Physical.m_Mouse.m_ReleasedButtons |= m_Physical.m_Mouse.m_CurrentButtons;
            m_Physical.m_Keyboard.held.reset();
            m_Physical.m_Keyboard.pressed.reset();
            m_Physical.m_Mouse.m_CurrentButtons = m_Physical.m_Mouse.m_PressedButtons = 0;
            m_Physical.m_Mouse.m_XRel = m_Physical.m_Mouse.m_YRel = 0;
            m_Physical.m_Mouse.m_ScrollWheel = {};
            m_Physical.modifiers = 0;
            m_Physical.compositionActive = false;
            m_Physical.textCommits = 0;
            ++m_Physical.compositionChanges;
        }
        if (m_Backend && m_Backend->pollDepth)
            for (std::size_t i = nativeBegin; i < m_Backend->transitionCount; ++i)
                if (auto& transition = m_Backend->transitions[i]; transition.cancelling && !transition.relative)
                    transition.sequence = m_Sequence; // Synthetic cleanup is part of this cancellation.
        return modeFailed ? InputResult(std::unexpected(InputError::Capture)) : InputResult{};
    }
    PlatformResult InputManager::CapturePointer(InputLayer layer, bool relative)
    {
        if (m_Owner != std::this_thread::get_id() || m_Cancelling || !Focused() || layer < InputLayer::UI ||
            layer > InputLayer::Game ||
            (m_Capture == InputLayer::None && !m_Physical.m_Mouse.m_CurrentButtons) ||
            (layer == InputLayer::View &&
             (!m_Routing.visible || m_Routing.uiMouse ||
              (m_Capture == InputLayer::None && !m_States[1].m_Mouse.m_CurrentButtons))))
            return std::unexpected(PlatformError{PlatformErrorCode::InputMode, "pointer capture",
                                                 "Ineligible input owner"});
        if (m_Capture != InputLayer::None && m_Capture != layer)
            if (!CancelInput(InputCancelReason::Transfer, m_Capture))
                return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                                                     "pointer capture",
                                                     "Old owner cancellation failed"});
        if (auto mode = TransitionPointer(relative, 1); !mode)
            return mode;
        m_Capture = layer;
        return {};
    }
    PlatformResult InputManager::ReleasePointer(InputLayer layer)
    {
        if (m_Owner != std::this_thread::get_id())
            return std::unexpected(
                PlatformError{PlatformErrorCode::InputMode, "pointer release", "Wrong thread"});
        if (m_Capture != layer)
            return {};
        const auto mode = TransitionPointer(false, 0);
        m_Capture = InputLayer::None;
        return mode;
    }
    const PlatformError* InputManager::NativePollFailure() const
    {
        return m_Backend && m_Backend->pollFailure ? &*m_Backend->pollFailure : nullptr;
    }
    PlatformResult InputManager::BeginNativePoll()
    {
        if (m_Owner != std::this_thread::get_id() || !m_Backend)
            return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                "begin native input poll", "Unavailable input owner"});
        if (m_Backend->pollFailure) return std::unexpected(*m_Backend->pollFailure);
        if (m_Backend->pollDepth == UINT_MAX)
            return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                "begin native input poll", "Poll nesting exhausted"});
        ++m_Backend->pollDepth;
        return {};
    }
    void InputManager::CancelFailedPointerTransition(const PlatformError& error)
    {
        if (!m_Backend) return;
        if (!m_Backend->pollFailure) m_Backend->pollFailure = error;
        m_Backend->transitionCount = 0;
        m_Backend->failing = true;
        if (!m_Cancelling)
            if (auto cancelled = CancelInput(InputCancelReason::Explicit); !cancelled)
                GENGINE_CORE_ERROR("Native input failure cancellation: {}", int(cancelled.error()));
        m_Capture = InputLayer::None;
        m_KeyOwners = {}; m_ButtonOwners = {};
        const auto neutral = [](InputState& state) {
            state.m_Keyboard.held.reset(); state.m_Keyboard.pressed.reset();
            state.m_Mouse.m_CurrentButtons = state.m_Mouse.m_PressedButtons = 0;
            state.m_Mouse.m_XRel = state.m_Mouse.m_YRel = 0;
            state.m_Mouse.m_ScrollWheel = {}; state.modifiers = 0;
        };
        neutral(m_Physical);
        for (auto& state : m_States) neutral(state);
        m_ViewCancelled = true;
        m_Backend->failing = false;
    }
    PlatformResult InputManager::EndNativePoll()
    {
        if (m_Owner != std::this_thread::get_id() || !m_Backend || !m_Backend->pollDepth)
            return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                "end native input poll", "Unbalanced input poll"});
        if (--m_Backend->pollDepth) return {};
        const auto count = std::exchange(m_Backend->transitionCount, 0);
        if (!m_Backend->pollFailure)
        {
            std::size_t lastExit = 0, lastGrab = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                if (!m_Backend->transitions[i].relative) lastExit = i + 1;
                if (m_Backend->transitions[i].grab >= 0) lastGrab = i + 1;
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                auto transition = m_Backend->transitions[i];
                // A completed/cancelled gesture needs no transient native entry.
                // Keep actual exits (and their restore reason) before any fresh held entry.
                if (transition.relative && i + 1 < lastExit)
                    transition.relative = m_Physical.m_Mouse.m_IsRelative;
                if (transition.grab >= 0 && i + 1 < lastGrab) transition.grab = -1;
                if (auto result = ApplyPointerTransition(transition); !result)
                {
                    CancelFailedPointerTransition(result.error());
                    break;
                }
            }
        }
        if (m_Backend->pollFailure)
        {
            // A failed transaction is terminal for this input backend. Best-effort release
            // does not revive ownership or discard the first failure.
            NativePointerTransition release;
            release.grab = 0; release.cancelling = true;
            release.owner = InputLayer::View; release.sequence = m_Sequence; release.deferred = true;
            if (auto result = ApplyPointerTransition(release); !result)
                ReportPlatformError(result.error());
            const bool relative = SDL_GetRelativeMouseMode() == SDL_TRUE;
            m_Physical.m_Mouse.m_IsRelative = relative;
            for (auto& state : m_States) state.m_Mouse.m_IsRelative = relative;
            return std::unexpected(*m_Backend->pollFailure);
        }
        return {};
    }
    PlatformResult InputManager::SetRelativeMouseMode(bool value)
    {
        return TransitionPointer(value, -1);
    }
    PlatformResult InputManager::ReadPointerOrigin(NativePointerTransition& transition)
    {
        auto* window = m_Window ? SDL_GetWindowFromID(m_Window->GetWindowID()) : nullptr;
        SDL_GetMouseState(&transition.originX, &transition.originY);
        if (window)
        {
            // Focus/hide cancellation must not warp. SDL can therefore
            // retain its old absolute cache until the next real motion.
            // The actual cursor, not that cache, defines a new gesture.
            int windowX = 0, windowY = 0;
            SDL_GetGlobalMouseState(&transition.originX, &transition.originY);
            SDL_GetWindowPosition(window, &windowX, &windowY);
            transition.originX -= windowX;
            transition.originY -= windowY;
        }
        transition.pixelX = transition.originX;
        transition.pixelY = transition.originY;
#ifdef GENGINE_PLATFORM_WINDOWS
        if (window)
        {
            SDL_SysWMinfo native{};
            SDL_VERSION(&native.version);
            if (!SDL_GetWindowWMInfo(window, &native) ||
                native.subsystem != SDL_SYSWM_WINDOWS)
                return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                    "relative mouse entry", "Native window unavailable"});
            const auto previous = SetThreadDpiAwarenessContext(
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            if (!previous)
                return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                    "relative mouse entry", "Physical cursor context unavailable"});
            POINT point{}, client{};
            const bool positioned = GetPhysicalCursorPos(&point) &&
                ClientToScreen(native.info.win.window, &client);
            SetThreadDpiAwarenessContext(previous);
            if (!positioned)
                return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                    "relative mouse entry", "Physical cursor coordinates unavailable"});
            transition.pixelX = point.x - client.x;
            transition.pixelY = point.y - client.y;
        }
#endif
        return {};
    }
    PlatformResult InputManager::TransitionPointer(bool relative, int grab)
    {
        if (m_Owner != std::this_thread::get_id() || !m_Backend)
            return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                "pointer transition", "Unavailable input owner"});
        if (m_Backend->failing) return {};
        if (m_Backend->pollFailure) return std::unexpected(*m_Backend->pollFailure);
        NativePointerTransition transition;
        transition.relative = relative; transition.grab = grab;
        transition.owner = m_Capture; transition.cancelling = m_Cancelling;
        transition.sequence = m_Sequence; transition.deferred = m_Backend->pollDepth != 0;
        const bool planned = m_Backend->transitionCount
            ? m_Backend->transitions[m_Backend->transitionCount - 1].relative
            : m_Physical.m_Mouse.m_IsRelative;
        if (relative && !planned)
            if (auto result = ReadPointerOrigin(transition); !result)
            {
                CancelFailedPointerTransition(result.error());
                return result;
            }
        if (transition.deferred)
        {
            if (m_Cancelling)
                for (std::size_t i = 0; i < m_Backend->transitionCount; ++i)
                    m_Backend->transitions[i].cancelling = true;
            if (m_Backend->transitionCount == m_Backend->transitions.size())
            {
                const PlatformError error{PlatformErrorCode::InputMode, "pointer transition",
                    "Native transition capacity exhausted"};
                CancelFailedPointerTransition(error);
                return std::unexpected(error);
            }
            m_Backend->transitions[m_Backend->transitionCount++] = transition;
            return {};
        }
        auto result = ApplyPointerTransition(transition);
        if (!result)
        {
            CancelFailedPointerTransition(result.error());
            NativePointerTransition release;
            release.grab = 0; release.cancelling = true; release.owner = transition.owner;
            release.sequence = m_Sequence;
            if (auto cleanup = ApplyPointerTransition(release); !cleanup)
                ReportPlatformError(cleanup.error());
        }
        return result;
    }
    PlatformResult InputManager::ApplyPointerTransition(const NativePointerTransition& transition)
    {
        const bool value = transition.relative;
        auto* window = m_Window ? SDL_GetWindowFromID(m_Window->GetWindowID()) : nullptr;
        if (m_Physical.m_Mouse.m_IsRelative != value)
        {
            const int originX = transition.originX, originY = transition.originY;
            const int originPixelX = transition.pixelX, originPixelY = transition.pixelY;
            int width = 0, height = 0;
            if (window)
                SDL_GetWindowSize(window, &width, &height);
            const bool viewExit = !value && m_Backend && window &&
                transition.owner == InputLayer::View;
            const bool focusedExit = viewExit && SDL_GetKeyboardFocus() == window &&
                (SDL_GetWindowFlags(window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) == 0;
            const bool restore = focusedExit && !transition.cancelling && Focused() &&
                m_Routing.visible && !m_Routing.uiMouse &&
                m_Backend->relativeWindow == SDL_GetWindowID(window) &&
                m_Backend->relativeOriginX >= 0 && m_Backend->relativeOriginX < width &&
                m_Backend->relativeOriginY >= 0 && m_Backend->relativeOriginY < height;
            int exitX = 0, exitY = 0;
            struct MouseRectGuard
            {
                SDL_Window* window = nullptr;
                SDL_Rect previous{};
                bool hadPrevious = false;
#ifdef GENGINE_PLATFORM_WINDOWS
                DPI_AWARENESS_CONTEXT previousDpi = nullptr;
#endif
                ~MouseRectGuard()
                {
#ifdef GENGINE_PLATFORM_WINDOWS
                    if (previousDpi)
                        SetThreadDpiAwarenessContext(previousDpi);
#endif
                    if (window)
                        SDL_SetWindowMouseRect(window, hadPrevious ? &previous : nullptr);
                }
            } mouseRect;
            if (focusedExit)
            {
                if (restore)
                {
                    exitX = m_Backend->relativeOriginX;
                    exitY = m_Backend->relativeOriginY;
                }
                else
                {
                    int windowX = 0, windowY = 0;
                    SDL_GetGlobalMouseState(&exitX, &exitY);
                    SDL_GetWindowPosition(window, &windowX, &windowY);
                    exitX -= windowX;
                    exitY -= windowY;
                }
                mouseRect.window = window;
                if (const auto* previous = SDL_GetWindowMouseRect(window))
                {
                    mouseRect.previous = *previous;
                    mouseRect.hadPrevious = true;
                }
                SDL_Rect stationary{exitX, exitY, 1, 1};
#ifdef GENGINE_PLATFORM_WINDOWS
                SDL_SysWMinfo native{};
                SDL_VERSION(&native.version);
                if (!SDL_GetWindowWMInfo(window, &native) ||
                    native.subsystem != SDL_SYSWM_WINDOWS)
                    return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                        "relative mouse exit", "Native window unavailable"});
                mouseRect.previousDpi = SetThreadDpiAwarenessContext(
                    DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
                if (!mouseRect.previousDpi)
                    return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                        "relative mouse exit", "Physical cursor context unavailable"});
                POINT point{}, client{};
                if (!GetPhysicalCursorPos(&point) ||
                    !ClientToScreen(native.info.win.window, &client))
                    return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                        "relative mouse exit", "Physical cursor coordinates unavailable"});
                // SDL 2.0.20 subtracts one from the right/bottom clip edges.
                // Use one PHYSICAL pixel, not a DPI-virtualized logical pixel.
                // Cancellation holds the current point; only normal release
                // may restore the recorded point in the originating window.
                stationary = {restore ? m_Backend->relativePixelX : point.x - client.x,
                              restore ? m_Backend->relativePixelY : point.y - client.y, 2, 2};
#endif
                if (SDL_SetWindowMouseRect(window, &stationary) != 0)
                    return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                        "relative mouse exit", SDL_GetError()});
            }
            if (SDL_SetRelativeMouseMode(value ? SDL_TRUE : SDL_FALSE) != 0)
                return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                    "relative mouse mode", SDL_GetError()});
            if (focusedExit)
            {
                // Update SDL absolute state AFTER leaving warp-relative mode:
                // that mode ignores an exact center sample. Native confinement
                // suppresses SDL's implicit exit warp and one-pixel jitter.
                // On cancellation this performs no native cursor displacement.
                SDL_WarpMouseInWindow(window, exitX, exitY);
                SDL_FlushEvent(SDL_MOUSEMOTION);
            }
            if (viewExit)
            {
                int exitX = 0, exitY = 0;
                SDL_GetMouseState(&exitX, &exitY);
                const Vector2 position{float(exitX), float(exitY)};
                const auto restoreSnapshot = [&](MouseState& mouse)
                {
                    // A fresh later press/motion belongs to a different logical gesture.
                    // Committing an older native release must not overwrite its position.
                    if (!transition.deferred || mouse.positionVersion <= transition.sequence)
                    {
                        mouse.m_MousePos = position;
                        mouse.m_XRel = mouse.m_YRel = 0;
                    }
                };
                restoreSnapshot(m_Physical.m_Mouse);
                restoreSnapshot(m_States[1].m_Mouse);
            }
            if (m_Backend)
            {
                m_Backend->relativeWindow = value && window ? SDL_GetWindowID(window) : 0;
                m_Backend->relativeOriginX = originX;
                m_Backend->relativeOriginY = originY;
                m_Backend->relativePixelX = originPixelX;
                m_Backend->relativePixelY = originPixelY;
            }
        }
        m_Physical.m_Mouse.m_IsRelative = value;
        for (auto& state : m_States)
            state.m_Mouse.m_IsRelative = value;
        if (transition.grab >= 0 && m_Window)
        {
            m_Window->SetMouseGrab(transition.grab != 0);
            if (!window || (SDL_GetWindowGrab(window) == SDL_TRUE) != (transition.grab != 0))
                return std::unexpected(PlatformError{PlatformErrorCode::InputMode,
                    "pointer grab", "Native grab did not match requested state"});
        }
        return {};
    }
    SubscriptionResult InputManager::ObserveCosts(SubscriptionObservations* observer)
    {
        for (auto& route : m_Routes)
            if (auto result = route.Observe(observer); !result)
                return result;
        return m_Trace.Observe(observer);
    }
    float InputManager::Filter1D(int input)
    {
        // A value < dead zone is interpreted as 0%
        const int deadZone = 250;
        // A value > max value is interpreted as 100%
        const int maxValue = 30000;

        float retVal = 0.0f;

        // Take absolute value of input
        const int absValue = input > 0 ? input : -input;
        // Ignore input within dead zone
        if (absValue > deadZone)
        {
            // Compute fractional value between dead zone and max value
            retVal = static_cast<float>(absValue - deadZone) /
                (maxValue - deadZone);
            // Make sure sign matches original value
            retVal = input > 0 ? retVal : -1.0f * retVal;
            // Clamp between -1.0f and 1.0f
            retVal = Math::Clamp(retVal, -1.0f, 1.0f);
        }

        return retVal;
    }


    Vector2 InputManager::Filter2D(int inputX, int inputY)
    {
        const float deadZone = 8000.0f;
        const float maxValue = 30000.0f;

        // Make into 2D vector
        Vector2 dir;
        dir.x = static_cast<float>(inputX);
        dir.y = static_cast<float>(inputY);

        const float length = dir.Length();

        // If length < deadZone, should be no input
        if (length < deadZone)
        {
            //dir = Vector2::Zero;
            dir = Vector2{ 0.f, 0.f };
        }
        else
        {
            // Calculate fractional value between
            // dead zone and max value circles
            float f = (length - deadZone) / (maxValue - deadZone);
            // Clamp f between 0.0f and 1.0f
            f = Math::Clamp(f, 0.0f, 1.0f);
            // Normalize the vector, and then scale it to the
            // fractional value
            dir *= f / length;
        }

        return dir;
    }


}
