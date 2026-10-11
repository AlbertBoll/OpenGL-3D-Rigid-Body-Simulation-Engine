#include "gepch.h"
#include "Managers/EventManager.h"
#include "Managers/InputManager.h"
#include "Managers/WindowManager.h"
#include "Core/BaseApp.h"
#include "Windows/SDLWindow.h"

namespace GEngine::Manager
{
    ScopedPtr<EventManager> EventManager::GetScopedInstance()
    {
        struct Enable : EventManager
        {
        };
        return CreateScopedPtr<Enable>();
    }

    void EventManager::Initialize()
    {
        // InputManager owns snapshots; no notification callback may overwrite motion.
        if (auto result = m_MouseMoveConnection.Reset(); !result)
            ReportSubscriptionError(result.error());
    }

    void EventManager::PollEvents()
    {
        auto drained = m_Completions.Drain();
        if (!drained)
        {
            ReportSubscriptionError(drained.error());
            return;
        }
        const auto deliver = [this](auto key, auto... values)
        {
            if (auto result = m_EventDispatcher.Dispatch(key, values...); !result)
                ReportSubscriptionError(result.error());
        };
        auto* root = EngineContext::TryGet();
        auto* window = root ? static_cast<SDLWindow*>(root->MainWindow()) : nullptr;
        auto* input = root ? root->LegacyEngine().GetInputManager() : nullptr;
        SDL_Event native{};
        while (SDL_PollEvent(&native))
        {
            if (window && window->GetImGuiWindow())
                window->GetImGuiWindow()->HandleSDLEvent(native);
            InputEvent value;
            bool isInput = true;
            InputResult translated{};
            switch (native.type)
            {
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                value.kind = native.type == SDL_KEYDOWN ? InputKind::KeyDown : InputKind::KeyUp;
                value.window = native.key.windowID;
                value.key = static_cast<GEngineKeyCode>(native.key.keysym.scancode);
                value.repeated = native.key.repeat != 0;
                if (native.key.keysym.mod & KMOD_CAPS)
                    value.modifiers |= CapsLock;
                if (native.key.keysym.mod & KMOD_NUM)
                    value.modifiers |= NumLock;
                break;
            case SDL_TEXTINPUT:
                value.kind = InputKind::Text;
                value.window = native.text.windowID;
                translated = value.SetText(native.text.text);
                break;
            case SDL_TEXTEDITING:
                value.kind = InputKind::Composition;
                value.window = native.edit.windowID;
                value.compositionStart = native.edit.start;
                value.compositionLength = native.edit.length;
                translated = value.SetText(native.edit.text);
                break;
#if SDL_VERSION_ATLEAST(2, 0, 22)
            case SDL_TEXTEDITING_EXT:
                value.kind = InputKind::Composition;
                value.window = native.editExt.windowID;
                value.compositionStart = native.editExt.start;
                value.compositionLength = native.editExt.length;
                translated = value.SetText(native.editExt.text ? native.editExt.text : "");
                SDL_free(native.editExt.text);
                break;
#endif
            case SDL_MOUSEMOTION:
                value.kind = InputKind::Motion;
                value.window = native.motion.windowID;
                value.x = static_cast<float>(native.motion.x);
                value.y = static_cast<float>(native.motion.y);
                value.dx = native.motion.xrel;
                value.dy = native.motion.yrel;
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                value.kind = native.type == SDL_MOUSEBUTTONDOWN ? InputKind::ButtonDown
                                                                : InputKind::ButtonUp;
                value.window = native.button.windowID;
                value.button = static_cast<GEngineMouseCode>(native.button.button);
                value.x = static_cast<float>(native.button.x);
                value.y = static_cast<float>(native.button.y);
                break;
            case SDL_MOUSEWHEEL:
            {
                value.kind = InputKind::Wheel;
                value.window = native.wheel.windowID;
                const float direction =
                    native.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f;
                value.wheelX = native.wheel.preciseX * direction;
                value.wheelY = native.wheel.preciseY * direction;
                break;
            }
            case SDL_WINDOWEVENT:
                value.window = native.window.windowID;
                if (native.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                    value.kind = InputKind::FocusLost;
                else if (native.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                    value.kind = InputKind::FocusGained;
                else if (native.window.event == SDL_WINDOWEVENT_HIDDEN ||
                         native.window.event == SDL_WINDOWEVENT_MINIMIZED ||
                         native.window.event == SDL_WINDOWEVENT_CLOSE)
                {
                    value.kind = InputKind::Cancel;
                    value.cancellation = InputCancelReason::Hidden;
                }
                else
                    isInput = false;
                break;
            default:
                isInput = false;
                break;
            }
            if (isInput && input && window)
            {
                if (!input->Routing().window)
                {
                    // Compatibility applications without an authored View descriptor.
                    const InputRouting routing{
                        window->GetWindowID(),  0, 0, 0, 0, false, false, window->WantsMouse(),
                        window->WantsKeyboard()};
                    if (auto result = input->PublishRouting(routing); !result)
                        GENGINE_CORE_ERROR("Input routing rejected: {}", int(result.error()));
                }
                auto routed = translated ? input->Route(value)
                                         : std::expected<InputLayer, InputError>(
                                               std::unexpected(translated.error()));
                if (!routed)
                {
                    GENGINE_CORE_ERROR("Input translation/routing rejected: {}",
                                       int(routed.error()));
                }
                else if (!value.handled && !value.cleanup)
                {
                    // Compatibility notifications follow consumption; migrated RBS handlers
                    // consume their typed route and therefore never receive duplicate actions.
                    switch (value.kind)
                    {
                    case InputKind::KeyDown:
                        if (value.repeated)
                            break;
                        if (value.key == GENGINE_KEY_P)
                            deliver(Event::AppPause);
                        else if (value.key == GENGINE_KEY_R)
                            deliver(Event::AppResume);
                        else if (value.key == GENGINE_KEY_SPACE)
                            deliver(Event::DebugShow);
                        else
                            deliver(Event::ViewportChange);
                        break;
                    case InputKind::Motion:
                        deliver(Event::MouseMove, MouseMoveParam{value.window, int(value.x),
                                                                 int(value.y), value.dx, value.dy});
                        break;
                    case InputKind::ButtonDown:
                        deliver(Event::MouseButtonPress,
                                MouseButtonParam{value.window, int(value.x), int(value.y),
                                                 std::uint8_t(value.button), native.button.clicks});
                        break;
                    case InputKind::Wheel:
                        deliver(Event::MouseScrollWheel,
                                MouseScrollWheelParam{value.window, value.wheelX, value.wheelY});
                        break;
                    default:
                        break;
                    }
                }
            }
            const auto& e = native;
            switch (native.type)
            {
            case SDL_QUIT:
                if (input)
                    if (auto result = input->CancelInput(InputCancelReason::Shutdown); !result)
                        GENGINE_CORE_ERROR("Input close cancellation rejected: {}",
                                           int(result.error()));
                deliver(Event::AppClose);
                break;
            case SDL_WINDOWEVENT:
			{
				// Preserve the actual SDL state event; resizing and visibility are independent.
				auto& windows = BaseApp::GetWindowManager()->GetWindows();
                auto found = windows.find(e.window.windowID);
                WindowStateChange change = WindowStateChange::Other;
                switch (e.window.event)
                {
                case SDL_WINDOWEVENT_MINIMIZED: change = WindowStateChange::Minimized; break;
                case SDL_WINDOWEVENT_RESTORED: change = WindowStateChange::Restored; break;
                case SDL_WINDOWEVENT_MAXIMIZED: change = WindowStateChange::Maximized; break;
                case SDL_WINDOWEVENT_HIDDEN: change = WindowStateChange::Hidden; break;
                case SDL_WINDOWEVENT_SHOWN: change = WindowStateChange::Shown; break;
                case SDL_WINDOWEVENT_RESIZED:
                case SDL_WINDOWEVENT_SIZE_CHANGED: change = WindowStateChange::Resized; break;
                case SDL_WINDOWEVENT_DISPLAY_CHANGED: change = WindowStateChange::DisplayChanged; break;
                }
                NativeWindowLogicalSize logical{};
                NativeFramebufferPixelSize pixels{};
                if (found != windows.end())
                {
                    found->second->RefreshDimensions();
                    logical = found->second->GetLogicalSize(); pixels = found->second->GetFramebufferPixelSize();
                }
                // Preserve zero-size notifications for suspension even on platforms
                // that retain their last nonzero drawable while minimized.
                if (change == WindowStateChange::Resized && (e.window.data1 <= 0 || e.window.data2 <= 0))
                    logical = {};
                deliver(Event::WindowState, WindowStateEvent{e.window.windowID, change, logical, pixels});

				if (e.window.event == SDL_WINDOWEVENT_CLOSE)
				{
					deliver(Event::WindowClose, WindowCloseParam{ .ID = e.window.windowID });
                    GENGINE_CORE_INFO("Window close event");
                }

				else if (e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
				{
					deliver(Event::WindowResize, WindowResizeParam{ .ID = e.window.windowID,
																					   .Width = e.window.data1,
																					   .Height = e.window.data2 });
				}

				break;

			}
            default:
                break;
            }
        }
    }
}
