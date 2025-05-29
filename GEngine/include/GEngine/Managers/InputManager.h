#pragma once

#include "Managers/ManagerBase.h"
#include "Math/Math.h"
#include "Inputs/KeyCodes.h"
#include <Inputs/MouseCodes.h>
#include "Inputs/ControllerCodes.h"



#define GENGINE_MAX_KEYCODES 512
#define GENGINE_BUTTON(x) (1 << ((x)-1))
#define GENGINE_CONTROLLER_BUTTON_MAX 21


union SDL_Event;
struct _SDL_GameController;

using namespace GEngine::Input::Key;
using namespace GEngine::Input::Mouse;
using namespace GEngine::Input::Controller;
namespace GEngine
{
	class SDLWindow;
}

enum class CursorMode
{
	NORMAL = 0,
	HIDDEN = 1,
	LOCKED = 2
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




	//Helper for keyboard input
	class KeyboardState
	{
	public:
	
		ButtonState GetKeyState(GEngineKeyCode keyCode)const;
	
		bool IsKeyHeld(GEngineKeyCode keyCode)const { return GetKeyState(keyCode) == ButtonState::Held; };
		bool IsKeyPressed(GEngineKeyCode keyCode)const { return GetKeyState(keyCode) == ButtonState::Pressed; };
		bool IsKeyReleased(GEngineKeyCode keyCode)const { return GetKeyState(keyCode) == ButtonState::Released; };

	private:
		friend class InputManager;
		const uint8_t* m_CurrentState;
		uint8_t m_PreviousState[GENGINE_MAX_KEYCODES];
	};


	//Helper for mouse input
	class MouseState
	{
		
	public:
		const Vector2& GetPosition() const { return m_MousePos; }
		const Vector2& GetScrollWheel() const { return m_ScrollWheel; }
		void SetScrollWheel(const Vector2& scrollWheel) { m_ScrollWheel = scrollWheel; }
		void SetMouseRelative(bool new_relative) { m_IsRelative = new_relative; }
		bool GetButtonValue(GEngineMouseCode button) const;
		ButtonState GetButtonState(GEngineMouseCode button) const;
		bool isButtonPressed(GEngineMouseCode button) const { return GetButtonState(button) == ButtonState::Pressed; }
		bool isButtonHeld(GEngineMouseCode button) const { return GetButtonState(button) == ButtonState::Held; }
		bool isButtonReleased(GEngineMouseCode button) const { return GetButtonState(button) == ButtonState::Released; }
		[[nodiscard]] bool IsRelative() const { return m_IsRelative; }

		int32_t GetDX()const { return m_XRel; }
		int32_t GetDY()const { return m_YRel; }
		float GetDWheel()const { return m_ScrollWheel.y; }
		void SetCursorMode(CursorMode mode);
		
		
	public:
		friend class MouseEvent;
		friend class InputManager;
		Vector2 m_MousePos;
		Vector2 m_ScrollWheel;
		uint32_t m_CurrentButtons;
		uint32_t m_PreviousButtons;

		int m_XRel{};
		int m_YRel{};

		//Are we in relative mouse mode
		bool m_IsRelative;
	};

	//helper for controller input
	class ControllerState
	{

	public:
		bool GetButtonValue(GEngineControllerCode button) const;
		ButtonState GetButtonState(GEngineControllerCode button) const;

		const Vector2& GetLeftStick() const { return m_LeftStick; }
		const Vector2& GetRightStick() const { return m_RightStick; }
		float GetLeftTrigger() const { return m_LeftTrigger; }
		float GetRightTrigger() const { return m_RightTrigger; }

		bool IsControllerConnected() const { return m_IsConnected; }

		bool isButtonPressed(GEngineControllerCode button) const { return GetButtonState(button) == ButtonState::Pressed; }
		bool isButtonHeld(GEngineControllerCode button) const { return GetButtonState(button) == ButtonState::Held; }
		bool isButtonReleased(GEngineControllerCode button) const { return GetButtonState(button) == ButtonState::Released; }


	private:

		friend class InputManager;
		uint8_t m_CurrentButtons[GENGINE_CONTROLLER_BUTTON_MAX];
		uint8_t m_PreviousButtons[GENGINE_CONTROLLER_BUTTON_MAX];

		Vector2 m_LeftStick;
		Vector2 m_RightStick;

		float m_LeftTrigger;
		float m_RightTrigger;

		//Is controller connected
		bool m_IsConnected;

	};


	struct InputState
	{
		KeyboardState m_Keyboard;
		MouseState m_Mouse;
		ControllerState m_Controller;
	};

	
	class InputManager: public ManagerBase<InputManager>
	{
		friend class ManagerBase<InputManager>;
	
		
	public:
		
		
		static ScopedPtr<InputManager> GetScopedInstance();

		void Initialize();
		void ShutDown();

		// Called right before Events loop
		void PrepareForUpdate();

		// Called after Events loop
		void Update();

		// Called to process an event 
		void ProcessEvent(SDL_Event& event);

		const InputState& GetInputState() const { return m_InputState; }
		InputState& GetInputState(){ return m_InputState; }
		KeyboardState& GetKeyboardState() { return m_InputState.m_Keyboard; }
		MouseState& GetMouseState() { return m_InputState.m_Mouse; }
		ControllerState& GetControllerState() { return m_InputState.m_Controller; }
		void SetSDLWindow(SDLWindow* window);



		void SetRelativeMouseMode(bool value);

	private:
		InputManager() = default;
		friend class GEngine;
		float Filter1D(int input);
		Vector2 Filter2D(int inputX, int inputY);


	private:
		InputState m_InputState;
		_SDL_GameController* m_GameController{};
		SDLWindow* m_SDLWindow{};

	};

}