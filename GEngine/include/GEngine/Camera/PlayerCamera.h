#pragma once
#include "Camera.h"
#include <Shapes/Terrain.h>
//#include <Character/Character.h>

namespace GEngine
{
	class Character;
}

struct PlayerCameraParams
{
	float DistanceFromPlayer = 50.f;
	float AngleAroundPlayer = 0.f;

};

namespace GEngine::Camera
{


	class PlayerCamera :public CameraBase
	{
	public:
		PlayerCamera(Character* player, const CameraSetting& camera);

		void SetPerspective();

		Vec3f &GetPosition()
		{
			return GetLocalTransformation().Translation;
		}

		float& GetPitch()
		{
			return GetLocalTransformation().Rotation.x;
		}

		void Update(Timestep ts)override;

		float& GetYaw()
		{
			return GetLocalTransformation().Rotation.y;
		}
		

		float& GetRoll()
		{
			return GetLocalTransformation().Rotation.z;
		}


		void SetPlayerCameraParam(const PlayerCameraParams& param)
		{
			m_PlayerCamParam = param;
		}

		PlayerCameraParams GetPlayerCameraParam()const { return m_PlayerCamParam; }

		void OnResize(int new_width, int new_height)override{};
		void OnResize(float ratio)override{}
		void OnScroll(float new_zoom_level)override{}
	
	private:

		void CalculateZoom();
		void CalculatePitch(Timestep ts);
		void CalculateAngleAroundPlayer(Timestep ts);

		void CalculateCameraPosition(float hor, float ver);

		float GetHorizontalDistance();
		float GetVerticalDistance();

	private:
		Character* m_Player;
		PlayerCameraParams m_PlayerCamParam;

	};
}


