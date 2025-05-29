#include "gepch.h"
#include "Camera/EditorCamera.h"
#include "Math/Matrix.h"
#include "Core/BaseApp.h"
#include "UI/UICore.h"

namespace GEngine::Camera
{
	EditorCamera::EditorCamera(float fov, float aspect_ratio, float near_field, float far_field): CameraBase()
	{
		m_CameraSetting.m_PerspectiveSetting.m_Far = far_field;
		m_CameraSetting.m_PerspectiveSetting.m_Near = near_field;
		m_CameraSetting.m_PerspectiveSetting.m_FieldOfView = fov;
		m_CameraSetting.m_PerspectiveSetting.m_AspectRatio = aspect_ratio;
	
		SetPerspective(fov, aspect_ratio, near_field, far_field);
	}

	void EditorCamera::SetPerspective(float field_of_view, float aspect_ratio, float near_field, float far_field)
	{
		m_Projection = Matrix::MakePerspective(field_of_view, aspect_ratio, near_field, far_field);
	}

	void EditorCamera::SetPerspective(const CameraSetting& setting)
	{
		SetPerspective(setting.m_PerspectiveSetting.m_FieldOfView,
			setting.m_PerspectiveSetting.m_AspectRatio,
			setting.m_PerspectiveSetting.m_Near,
			setting.m_PerspectiveSetting.m_Far);
	}

	void EditorCamera::Update(Timestep ts)
	{
		using namespace GEngine;
		auto input = BaseApp::GetInputManager();
		auto& keyboardState = input->GetKeyboardState();
		auto& mouseState = input->GetMouseState();
		if(keyboardState.IsKeyPressed(GENGINE_KEY_LCTRL))
		//if (Input::IsKeyPressed(Key::LeftAlt))
		{
			const Vec2f mouse = { mouseState.GetPosition().x, mouseState.GetPosition().y };
			Vec2f delta = (mouse - m_InitialMousePosition) * 0.003f;
			m_InitialMousePosition = mouse;

			if (mouseState.isButtonPressed(GENGINE_BUTTON_MIDDLE))
				MousePan(delta);
			else if (mouseState.isButtonPressed(GENGINE_BUTTON_LEFT))
				MouseRotate(delta);
			else if (mouseState.isButtonPressed(GENGINE_BUTTON_RIGHT))
				MouseZoom(delta.y);
		}

		OnUpdateView();
	}

	void EditorCamera::OnUpdateView()
	{
		auto pos = GetPosition();
		auto orientation = GetOrientation();

		m_View = glm::translate(glm::mat4(1.0f), pos) * glm::toMat4(orientation);
		m_View = glm::inverse(m_View);

	}

	void EditorCamera::OnResize(int new_width, int new_height)
	{
		m_ViewportWidth = (float)new_width;
		m_ViewportHeight = (float)new_height;

		if (!m_IsFixAspectRatio)
		{
			m_CameraSetting.m_PerspectiveSetting.m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
			SetPerspective(m_CameraSetting);
		}
	}

	void EditorCamera::OnResize(float ratio)
	{
		if (!m_IsFixAspectRatio)
		{
			m_CameraSetting.m_PerspectiveSetting.m_AspectRatio = ratio;
			SetPerspective(m_CameraSetting);
		}

	}


	void EditorCamera::OnScroll(float new_zoom_level)
	{
		float delta = new_zoom_level * 0.1f;
		MouseZoom(delta);
		OnUpdateView();
	}

	Vec3f EditorCamera::CalculatePosition() const
	{
		return m_FocalPoint - GetForwardDirection() * m_Distance;
	}

	void EditorCamera::MousePan(const Vec2f& delta)
	{
		auto [xSpeed, ySpeed] = PanSpeed();
		m_FocalPoint += -GetRightDirection() * delta.x * xSpeed * m_Distance;
		m_FocalPoint += GetUpDirection() * delta.y * ySpeed * m_Distance;
	}

	void EditorCamera::MouseRotate(const Vec2f& delta)
	{
		float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;

		auto& transform = GetLocalTransformation();
		auto& euler = transform.Rotation;
		auto m_Yaw = ToRadians(euler.y);
		auto m_Pitch = ToRadians(euler.x);
		m_Yaw += yawSign * delta.x * RotationSpeed();
		m_Pitch += delta.y * RotationSpeed();

		euler.y = ToDegrees(m_Yaw);
		euler.x = ToDegrees(m_Pitch);
	}

	void EditorCamera::MouseZoom(float delta)
	{
		m_Distance -= delta * ZoomSpeed();
		if (m_Distance < 1.0f)
		{
			m_FocalPoint += GetForwardDirection();
			m_Distance = 1.0f;
		}
	}

	std::pair<float, float> EditorCamera::PanSpeed() const
	{
		float x = std::min(m_ViewportWidth / 1000.0f, 2.4f); // max = 2.4f
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		float y = std::min(m_ViewportHeight / 1000.0f, 2.4f); // max = 2.4f
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

		return { xFactor, yFactor };
	}

	float EditorCamera::RotationSpeed() const
	{
		return 0.8f;
	}

	float EditorCamera::ZoomSpeed() const
	{
		float distance = m_Distance * 0.2f;
		distance = std::max(distance, 0.0f);
		float speed = distance * distance;
		speed = std::min(speed, 100.0f); // max speed = 100
		return speed;
	}

	_EditorCamera::_EditorCamera(float fov, float aspectRatio, float nearClip, float farClip) : m_FOV(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip), _Camera(glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip))
	{
		UpdateView();

	}

	_EditorCamera::_EditorCamera(const float degFov, const float width, const float height, const float nearP, const float farP) :
		_Camera(glm::perspectiveFov(glm::radians(degFov), width, height, nearP, farP), glm::perspectiveFov(glm::radians(degFov), width, height, farP, nearP)), m_FocalPoint(0.0f), m_FOV(glm::radians(degFov)), m_NearClip(nearP), m_FarClip(farP)
	{
	
		Initialize();
	}


	void _EditorCamera::Initialize()
	{
		constexpr Vec3f position = { -40.f, 40.f, 40.f };
		m_Distance = glm::distance(position, m_FocalPoint);

		//m_Yaw = 3.0f * glm::pi<float>() / 4.0f;
		//m_Pitch = glm::pi<float>() / 4.0f;

		m_Yaw = 0.f;
		m_Pitch = glm::pi<float>() / 4.0f;

		m_Position = CalculatePosition();
		const Quat orientation = GetOrientation();
		m_Direction = glm::eulerAngles(orientation) * (180.0f / glm::pi<float>());
		m_ViewMatrix = glm::translate(Mat4(1.0f), m_Position) * glm::toMat4(orientation);
		m_ViewMatrix = glm::inverse(m_ViewMatrix);
	}

	static void DisableMouse()
	{
		
		auto& input = BaseApp::GetEngine().GetInputManager()->GetMouseState();
		input.SetCursorMode(CursorMode::LOCKED);
		//UI::SetInputEnabled(false);
	}


	static void EnableMouse()
	{
		auto& input = BaseApp::GetEngine().GetInputManager()->GetMouseState();
		input.SetCursorMode(CursorMode::NORMAL);
		//UI::SetInputEnabled(true);
	}

	void _EditorCamera::OnUpdate(Timestep ts)
	{
		auto& keyboard_input = BaseApp::GetInputManager()->GetKeyboardState();
		auto& mouse_input = BaseApp::GetInputManager()->GetMouseState();
		Vec2f mouse = Vec2f{ mouse_input.GetPosition().x, mouse_input.GetPosition().y };
		//GENGINE_CORE_INFO("Mouse X: {}, Y: {}", mouse.x, mouse.y);
		const Vec2f delta = (mouse - m_InitialMousePosition) * 0.002f;
		//GENGINE_CORE_INFO("Mouse delX: {}, delY: {}", delta.x, delta.y);
		/*if (!m_IsActive)
		{
			if (!UI::IsInputEnabled())
				UI::SetInputEnabled(true);

			return;
		}*/

		if (mouse_input.isButtonHeld(GENGINE_BUTTON_RIGHT) && !keyboard_input.IsKeyHeld(GENGINE_KEY_LALT))
		{
			//GENGINE_CORE_INFO("Mouse Right Button is Held");
			m_CameraMode = CameraMode::FLYCAM;
			DisableMouse();
			const float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;

			const float speed = GetCameraSpeed();

			if (keyboard_input.IsKeyHeld(GENGINE_KEY_Q))
				m_PositionDelta += ts.GetMilliseconds() * speed * Vec3f{ 0.f, yawSign, 0.f };
			if (keyboard_input.IsKeyHeld(GENGINE_KEY_E))
				m_PositionDelta -= ts.GetMilliseconds() * speed * Vec3f{ 0.f, yawSign, 0.f };
			if (keyboard_input.IsKeyHeld(GENGINE_KEY_S))
				m_PositionDelta -= ts.GetMilliseconds() * speed * m_Direction;
			if (keyboard_input.IsKeyHeld(GENGINE_KEY_W))
				m_PositionDelta += ts.GetMilliseconds() * speed * m_Direction;
			if (keyboard_input.IsKeyHeld(GENGINE_KEY_A))
				m_PositionDelta -= ts.GetMilliseconds() * speed * m_RightDirection;
			if (keyboard_input.IsKeyHeld(GENGINE_KEY_D))
				m_PositionDelta += ts.GetMilliseconds() * speed * m_RightDirection;

			constexpr float maxRate{ 0.12f };
			m_YawDelta += glm::clamp(yawSign * delta.x * RotationSpeed(), -maxRate, maxRate);
			m_PitchDelta += glm::clamp(delta.y * RotationSpeed(), -maxRate, maxRate);

			m_RightDirection = glm::cross(m_Direction, glm::vec3{ 0.f, yawSign, 0.f });

			m_Direction = glm::rotate(glm::normalize(glm::cross(glm::angleAxis(-m_PitchDelta, m_RightDirection),
				glm::angleAxis(-m_YawDelta, glm::vec3{ 0.f, yawSign, 0.f }))), m_Direction);

			const float distance = glm::distance(m_FocalPoint, m_Position);
			m_FocalPoint = m_Position + GetForwardDirection() * distance;
			m_Distance = distance;

		}		
		else if (keyboard_input.IsKeyHeld(GENGINE_KEY_LALT))
		{
			m_CameraMode = CameraMode::ARCBALL;

			if (mouse_input.isButtonHeld(GENGINE_BUTTON_MIDDLE))
			{
				DisableMouse();
				MousePan(delta);						
			}
			else if (mouse_input.isButtonHeld(GENGINE_BUTTON_LEFT))
			{
				DisableMouse();
				MouseRotate(delta);				
			}
			else if(mouse_input.isButtonHeld(GENGINE_BUTTON_RIGHT))
			{
				DisableMouse();
				MouseZoom((delta.x + delta.y) * 0.1f);
			}
			else
			{
				EnableMouse();
			}			

		}
		else
		{
			EnableMouse();
		}

		m_InitialMousePosition = mouse;
		m_Position += m_PositionDelta;
		m_Yaw += m_YawDelta;
		m_Pitch += m_PitchDelta;

		if (m_CameraMode == CameraMode::ARCBALL)
			m_Position = CalculatePosition();

	}

	void _EditorCamera::SetViewportSize(float width, float height)
	{
		SetPerspectiveProjectionMatrix(m_FOV, (float)width, (float)height, m_NearClip, m_FarClip);
		m_ViewportWidth = (uint32_t)width;
		m_ViewportHeight = (uint32_t)height;
		m_AspectRatio = (float)m_ViewportWidth / m_ViewportHeight;
	}

	Vec3f _EditorCamera::GetUpDirection() const
	{
		return glm::rotate(GetOrientation(), Vec3f(0.0f, 1.0f, 0.0f));;
	}

	Vec3f _EditorCamera::GetRightDirection() const
	{
		return glm::rotate(GetOrientation(), Vec3f(1.0f, 0.0f, 0.0f));
	}

	Vec3f _EditorCamera::GetForwardDirection() const
	{
		return glm::rotate(GetOrientation(), Vec3f(0.0f, 0.0f, -1.0f));
	}

	Quat _EditorCamera::GetOrientation() const
	{
		return Quat(Vec3f(-m_Pitch - m_PitchDelta, -m_Yaw - m_YawDelta, 0.0f));
	}

	float _EditorCamera::GetCameraSpeed() const
	{
		auto& keyboard_input = BaseApp::GetInputManager()->GetKeyboardState();
		float speed = m_NormalSpeed;
		if (keyboard_input.IsKeyPressed(GENGINE_KEY_LCTRL))
			speed /= 2 - glm::log(m_NormalSpeed);
		if (keyboard_input.IsKeyPressed(GENGINE_KEY_LSHIFT))
			speed *= 2 - glm::log(m_NormalSpeed);

		return glm::clamp(speed, MIN_SPEED, MAX_SPEED);
	}

	void _EditorCamera::UpdateProjection()
	{
		m_AspectRatio = (float)m_ViewportWidth / m_ViewportHeight;
		m_Projection = glm::perspective(glm::radians(m_FOV), m_AspectRatio, m_NearClip, m_FarClip);
	}

	void _EditorCamera::UpdateView()
	{
		const float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;

		// Extra step to handle the problem when the camera direction is the same as the up vector
		const float cosAngle = glm::dot(GetForwardDirection(), GetUpDirection());
		if (cosAngle * yawSign > 0.99f)
			m_PitchDelta = 0.f;

		const Vec3f lookAt = m_Position + GetForwardDirection();
		m_Direction = glm::normalize(lookAt - m_Position);
		m_Distance = glm::distance(m_Position, m_FocalPoint);
		m_ViewMatrix = glm::lookAt(m_Position, lookAt, Vec3f{ 0.f, yawSign, 0.f });

		//damping for smooth camera
		m_YawDelta *= 0.6f;
		m_PitchDelta *= 0.6f;
		m_PositionDelta *= 0.8f;
	}

	void _EditorCamera::Focus(const Vec3f& focusPoint)
	{
		m_FocalPoint = focusPoint;
		m_CameraMode = CameraMode::FLYCAM;
		if (m_Distance > m_MinFocusDistance)
		{
			m_Distance -= m_Distance - m_MinFocusDistance;
			m_Position = m_FocalPoint - GetForwardDirection() * m_Distance;
		}
		m_Position = m_FocalPoint - GetForwardDirection() * m_Distance;
		UpdateView();
	}

	bool _EditorCamera::OnMouseScroll(float new_zoom_level)
	{
		auto& mouse_input = BaseApp::GetInputManager()->GetMouseState();
		if (mouse_input.isButtonHeld(GENGINE_BUTTON_RIGHT))
		{
			m_NormalSpeed += new_zoom_level * 0.3f * m_NormalSpeed;
			m_NormalSpeed = std::clamp(m_NormalSpeed, MIN_SPEED, MAX_SPEED);
		}
		else
		{
			float delta = new_zoom_level * 0.1f;
			MouseZoom(delta);
			UpdateView();
		}
		
		return true;
	}

	void _EditorCamera::MousePan(const Vec2f& delta)
	{
		auto [xSpeed, ySpeed] = PanSpeed();
		m_FocalPoint -= GetRightDirection() * delta.x * xSpeed * m_Distance;
		m_FocalPoint += GetUpDirection() * delta.y * ySpeed * m_Distance;
	}

	void _EditorCamera::MouseRotate(const Vec2f& delta)
	{
		const float yawSign = GetUpDirection().y < 0.0f ? -1.0f : 1.0f;
		m_YawDelta += yawSign * delta.x * RotationSpeed();
		m_PitchDelta += delta.y * RotationSpeed();
	}

	void _EditorCamera::MouseZoom(float delta)
	{
		m_Distance -= delta * ZoomSpeed();
		const glm::vec3 forwardDir = GetForwardDirection();
		m_Position = m_FocalPoint - forwardDir * m_Distance;
		if (m_Distance < 1.0f)
		{
			m_FocalPoint += forwardDir * m_Distance;
			m_Distance = 1.0f;
		}
		m_PositionDelta += delta * ZoomSpeed() * forwardDir;
	}


	Vec3f _EditorCamera::CalculatePosition() const
	{
		return m_FocalPoint - GetForwardDirection() * m_Distance + m_PositionDelta;
	}

	std::pair<float, float> _EditorCamera::PanSpeed() const
	{
		float x = std::min(m_ViewportWidth / 1000.0f, 2.4f); // max = 2.4f
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		float y = std::min(m_ViewportHeight / 1000.0f, 2.4f); // max = 2.4f
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

		return { xFactor, yFactor };
	}

	float _EditorCamera::RotationSpeed() const
	{
		return 0.3f;
	}

	float _EditorCamera::ZoomSpeed() const
	{
		float distance = m_Distance * 0.2f;
		distance = std::max(distance, 0.0f);
		float speed = distance * distance;
		speed = std::min(speed, 50.0f); // max speed = 100
		return speed;
	}
}
