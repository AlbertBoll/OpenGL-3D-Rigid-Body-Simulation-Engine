#include "gepch.h"
#include "Camera/RayTracingCamera.h"
#include "Core/BaseApp.h"
#include "Windows/SDLWindow.h"

namespace GEngine
{
	RayTracingCamera::RayTracingCamera(float verticalFOV, float nearClip, float farClip)
		:m_VerticalFOV(verticalFOV), m_NearClip(nearClip), m_FarClip(farClip)
	{
		m_ForwardDirection = glm::vec3(0, 0, -1);
		m_Position = glm::vec3(0, 0, 6);
	}

	bool RayTracingCamera::OnUpdate(float ts)
	{
		auto input = BaseApp::GetEngine().GetInputManager();
		
		auto& mouseState = input->GetMouseState();
		auto& keyboardState = input->GetKeyboardState();
	
		if (!mouseState.isButtonHeld(GEngineMouseCode::GENGINE_BUTTON_RIGHT))
		{
			
			mouseState.SetCursorMode(CursorMode::NORMAL);
		
			m_FirstMouse = true;
			return false;
		}

		mouseState.SetCursorMode(CursorMode::LOCKED);
		
		Vec2f mousePos = { mouseState.m_MousePos.x, mouseState.m_MousePos.y };
		
		Vec2f delta = mousePos - m_LastMousePosition;
		auto abs_x = std::abs(delta.x);
		auto abs_y = std::abs(delta.y);
		if (abs_x < 140.f && abs_y < 140.f && !m_FirstMouse)
		{
			delta = delta * 0.002f;
		}
			
		else
			delta = { 0.f, 0.f };
		
		
		m_LastMousePosition = mousePos;

		
		m_FirstMouse = false;
	
		

		bool moved = false;

		constexpr Vec3f upDirection(0.0f, 1.0f, 0.0f);
		Vec3f rightDirection = glm::cross(m_ForwardDirection, upDirection);

		float speed = 5.0f;

		//Movement
		if (keyboardState.IsKeyHeld(GEngineKeyCode::GENGINE_KEY_W))
		{
			
			m_Position += m_ForwardDirection * speed * ts;
			moved = true;
		}

		if (keyboardState.IsKeyHeld(GEngineKeyCode::GENGINE_KEY_S))
		{
			m_Position -= m_ForwardDirection * speed * ts;
			moved = true;
		}

		if (keyboardState.IsKeyHeld(GEngineKeyCode::GENGINE_KEY_A))
		{
			m_Position -= rightDirection * speed * ts;
			moved = true;
		}

		if (keyboardState.IsKeyHeld(GEngineKeyCode::GENGINE_KEY_D))
		{
			m_Position += rightDirection * speed * ts;
			moved = true;
		}

		if (keyboardState.IsKeyHeld(GEngineKeyCode::GENGINE_KEY_Q))
		{
			m_Position += upDirection * speed * ts;
			moved = true;
		}

		if (keyboardState.IsKeyHeld(GEngineKeyCode::GENGINE_KEY_E))
		{
			m_Position -= upDirection * speed * ts;
			moved = true;
		}

		// Rotation
		if (delta.x != 0.0f || delta.y != 0.0f)
		{
			float pitchDelta = delta.y * GetRotationSpeed();
			float yawDelta = delta.x * GetRotationSpeed();

			Quat q = glm::normalize(glm::cross(glm::angleAxis(-pitchDelta, rightDirection),
				glm::angleAxis(-yawDelta, Vec3f(0.f, 1.0f, 0.0f))));
			m_ForwardDirection = glm::rotate(q, m_ForwardDirection);

			moved = true;
		}

		if (moved)
		{
			RecalculateView();
			RecalculateRayDirections();

		}

		return moved;


	}

	bool RayTracingCamera::OnResize(uint32_t width, uint32_t height)
	{
		if (width == m_ViewportWidth && height == m_ViewportHeight)
			return false;

		m_ViewportWidth = width;
		m_ViewportHeight = height;

		RecalculateProjection();
		RecalculateRayDirections();

		return true;
	}

	bool RayTracingCamera::IsOnResize(uint32_t width, uint32_t height)
	{
		if (width == m_ViewportWidth && height == m_ViewportHeight)
			return false;
		return true;
	}

	float RayTracingCamera::GetRotationSpeed()
	{
		return 0.35f;
	}

	void RayTracingCamera::RecalculateProjection()
	{
		m_Projection = glm::perspectiveFov(glm::radians(m_VerticalFOV), (float)m_ViewportWidth, (float)m_ViewportHeight, m_NearClip, m_FarClip);
		m_InverseProjection = glm::inverse(m_Projection);
	}

	void RayTracingCamera::RecalculateView()
	{
		m_View = glm::lookAt(m_Position, m_Position + m_ForwardDirection, glm::vec3(0, 1, 0));
		m_InverseView = glm::inverse(m_View);
	}

	void RayTracingCamera::RecalculateRayDirections()
	{
		m_RayDirections.resize(m_ViewportWidth * m_ViewportHeight);

		for (uint32_t y = 0; y < m_ViewportHeight; y++)
		{
			for (uint32_t x = 0; x < m_ViewportWidth; x++)
			{
				Vec2f coord = { (float)x / (float)m_ViewportWidth, (float)y / (float)m_ViewportHeight };
				coord = coord * 2.0f - 1.0f; // -1 -> 1

				Vec4f target = m_InverseProjection * Vec4f(coord.x, coord.y, 1, 1);
				Vec3f rayDirection = Vec3f(m_InverseView * Vec4f(glm::normalize(Vec3f(target) / target.w), 0)); // World space
				m_RayDirections[x + y * m_ViewportWidth] = rayDirection;
			}
		}
	}
}
