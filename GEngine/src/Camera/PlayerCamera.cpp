#include "gepch.h"
#include "Camera/PlayerCamera.h"
#include "Core/BaseApp.h"
#include "Character/Character.h"
#include "Math/Matrix.h"

namespace GEngine::Camera
{

	PlayerCamera::PlayerCamera(Character* player, const CameraSetting& camera): CameraBase(camera), m_Player(player)
	{
		GetLocalTransformation().Rotation.x = 20.f;
		SetPerspective();
	
		float Horizontal = GetHorizontalDistance();
		float Vertical = GetVerticalDistance();
		CalculateCameraPosition(Horizontal, Vertical);
		GetYaw() = 180.f - (m_Player->GetRotation().y + m_PlayerCamParam.AngleAroundPlayer);
		UpdateLocalTransform();

	}

	void PlayerCamera::SetPerspective()
	{
		m_Projection = Matrix::MakePerspective(m_CameraSetting.m_PerspectiveSetting.m_FieldOfView,
			                                    m_CameraSetting.m_PerspectiveSetting.m_AspectRatio, 
			                                         m_CameraSetting.m_PerspectiveSetting.m_Near, 
			                                    m_CameraSetting.m_PerspectiveSetting.m_Far);
	}

	void PlayerCamera::Update(Timestep ts)
	{
		//m_Player->Update(ts);
		CalculateZoom();
		CalculatePitch(ts);
		CalculateAngleAroundPlayer(ts);
		float Horizontal = GetHorizontalDistance();
		float Vertical = GetVerticalDistance();
		CalculateCameraPosition(Horizontal, Vertical);
		GetYaw() = 180.f - (m_Player->GetRotation().y + m_PlayerCamParam.AngleAroundPlayer);
		UpdateLocalTransform();
		
	}


	void PlayerCamera::CalculateZoom()
	{
		using namespace GEngine;
		auto input = BaseApp::GetInputManager();
	
		auto& mouseState = input->GetMouseState();

		float zoomLevel = mouseState.GetDWheel();

		m_PlayerCamParam.DistanceFromPlayer -= zoomLevel;





	}

	void PlayerCamera::CalculatePitch(Timestep ts)
	{
		using namespace GEngine;
		auto input = BaseApp::GetInputManager();

		auto& mouseState = input->GetMouseState();
		if (mouseState.isButtonHeld(GENGINE_BUTTON_RIGHT))
		{
			float pitchChange = mouseState.GetDY() * ts;
			GetPitch() -= pitchChange;
		}
	}

	void PlayerCamera::CalculateAngleAroundPlayer(Timestep ts)
	{
		using namespace GEngine;
		auto input = BaseApp::GetInputManager();

		auto& mouseState = input->GetMouseState();
		if (mouseState.isButtonHeld(GENGINE_BUTTON_LEFT))
		{
			float angleChange = mouseState.GetDX() * ts;
			m_PlayerCamParam.AngleAroundPlayer -= angleChange;
		}
	}

	void PlayerCamera::CalculateCameraPosition(float hor, float ver)
	{

		float theta = m_Player->GetRotation().y + m_PlayerCamParam.AngleAroundPlayer;
		float offsetX = hor * std::sin(Math::ToRadians(theta));
		float offsetZ = hor * std::cos(Math::ToRadians(theta));
		SetPositionX(GetPosition().x - offsetX);
		SetPositionZ(GetPosition().z - offsetZ);
		SetPositionY(m_Player->GetPosition().y + ver);

	}

	float PlayerCamera::GetHorizontalDistance()
	{
		return m_PlayerCamParam.DistanceFromPlayer * std::cos(Math::ToRadians(GetPitch()));
	}

	float PlayerCamera::GetVerticalDistance()
	{
		return m_PlayerCamParam.DistanceFromPlayer * std::sin(Math::ToRadians(GetPitch()));
	}


}
