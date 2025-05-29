#include "gepch.h"
#include "Camera/ThirdPersonCamera.h"


namespace GEngine::Camera
{
	ThirdPersonCamera::ThirdPersonCamera(float field_of_view, float aspect_ratio, float near_field, float far_field, float zoom_level): 
		PerspectiveCamera(field_of_view, aspect_ratio, near_field, far_field, zoom_level)
	{

	}

	void ThirdPersonCamera::SetPlayer(Character* player)
	{
		m_Player = player;
	}

	void ThirdPersonCamera::OnScroll(float new_zoom_level)
	{

	}
}