#pragma once
#include "Core/Entity.h"
//

namespace GEngine
{

	struct CharacterParams
	{
		float RunSpeed = 20.f;
		float TurnSpeed = 10.f;
		float Gravity = -50.f;
		float JumpPower = 30;

	};

	class Terrain;
	class CameraRig;

	class Character :public Entity
	{



	public:
		Character(Geometry* geometry, const RefPtr<Material>& material);

		void Update(Timestep ts)override;
		//void Update(Timestep ts);

		void CheckInput();

		void Jump();

		void SetTerrain(Terrain* terrain) { m_Terrain = terrain; }
		void SetCameraRig(CameraRig* rig) { m_Rig = rig; }

		//void Render(CameraBase* camera)override;

	private:

		CharacterParams m_Params;
		float m_CurrentSpeed = 0.f;
		float m_CurrentTurnSpeed = 0.f;
		float m_UpwardSpeed = 0.f;
		float m_TerrainHeight = 0.f;
		bool m_IsInAir = false;
		Terrain* m_Terrain{};
		CameraRig* m_Rig{};


	};


}

