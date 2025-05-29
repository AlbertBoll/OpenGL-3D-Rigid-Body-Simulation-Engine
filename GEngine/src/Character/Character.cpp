#include "gepch.h"
#include "Character/Character.h"
#include "Core/BaseApp.h"
#include "Shapes/Terrain.h"
#include "Extras/CameraRig.h"

namespace GEngine
{
	Character::Character(Geometry* geometry, const RefPtr<Material>& material): Entity(geometry, material)
	{

	}

	void Character::Update(Timestep ts)
	{
		CheckInput();
		//RotateY(m_CurrentTurnSpeed * ts);
		//float distance = m_CurrentSpeed * ts;
		//float dx = distance * std::sin(Math::ToRadians(m_LocalTransformComponent.Rotation.y));
		//float dz = distance * std::cos(Math::ToRadians(m_LocalTransformComponent.Rotation.y));
		//Translate(dx, 0, dz);
		m_UpwardSpeed += m_Params.Gravity * ts;
		Translate(0, m_UpwardSpeed * ts, 0);
		//GENGINE_CORE_INFO("yaw: {}", m_Rig->Yaw());
		float terrainHeight = m_Terrain->GetTerrainHeight(m_Rig->GetPosition().x, m_Rig->GetPosition().z);
		GENGINE_CORE_INFO("({}, {}), terrain height: {}", m_Rig->GetPosition().x, m_Rig->GetPosition().z, terrainHeight);
		if (m_LocalTransformComponent.Translation.y < terrainHeight)
		{
			m_UpwardSpeed = 0;
			SetPositionY(terrainHeight);
			m_IsInAir = false;
		}
	}

	void Character::CheckInput()
	{
		auto& engine = BaseApp::GetEngine();
		auto* input = engine.GetInputManager();

		const auto keyboard = input->GetKeyboardState();

		/*if (keyboard.IsKeyHeld(GENGINE_KEY_W))
		{
			m_CurrentSpeed = m_Params.RunSpeed;
		}

		else if (keyboard.IsKeyHeld(GENGINE_KEY_S))
		{
			m_CurrentSpeed = -m_Params.RunSpeed;
		}

		else m_CurrentSpeed = 0.f;


		if (keyboard.IsKeyHeld(GENGINE_KEY_A))
		{
			m_CurrentTurnSpeed = m_Params.TurnSpeed;
		}

		else if (keyboard.IsKeyHeld(GENGINE_KEY_D))
		{
			m_CurrentTurnSpeed = -m_Params.TurnSpeed;
		}*/

		//else m_CurrentTurnSpeed = 0.f;

		if (keyboard.IsKeyHeld(GENGINE_KEY_SPACE))
		{
			Jump();
		}
	}

	void Character::Jump()
	{
		if (!m_IsInAir)
		{
			m_UpwardSpeed = m_Params.JumpPower;
			m_IsInAir = true;
		}
	}

	/*void Character::Render(CameraBase* camera)
	{

	}*/
		

}
