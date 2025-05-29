#include "gepch.h"
#include "Extras/CameraRig.h"
//#include <Managers/InputManager.h>
#include <Core/BaseApp.h>
//#include <imguizmo/ImGuizmo.h>

namespace GEngine
{
	static constexpr float m_MaxPitch = Pi / 2.f;

	CameraRig::CameraRig(bool bAttachToControlledActor, float units_per_seconds, float degree_per_seconds): Actor()
	{
		m_RigParams.m_UnitsPerSecond = units_per_seconds;
		m_RigParams.m_DegreesPerSecond = degree_per_seconds;

		if (bAttachToControlledActor)
		{
			m_RigAttachment = new Actor;
			//m_RigAttachment->SetParent(this);
			m_RigAttachment->SetTag("Rig Attachment");
			Add(m_RigAttachment);
		}
	
		
	}

	void CameraRig::Attach(Actor* child)
	{
		if (m_RigAttachment)
			m_RigAttachment->Add(child);

		else Add(child);

	}

	void CameraRig::SetYawSpeed(float YawSpeed)
	{
		m_RigParams.m_YawSpeed = YawSpeed;
	}

	void CameraRig::SetPitchSpeed(float PitchSpeed)
	{
		m_RigParams.m_PitchSpeed = PitchSpeed;
	}

	void CameraRig::Reset()
	{
		m_RigParams.m_YawSpeed = 0.f;
		m_RigParams.m_PitchSpeed = 0.f;
		//m_RigParams.m_CurrentPitch = 0.f;
		//m_RigParams.m_CurrentYaw = 0.f;

	}

	void CameraRig::Update(Timestep ts)
	{

		auto& engine = BaseApp::GetEngine();
		auto* input = engine.GetInputManager();

		const float moveAmount = m_RigParams.m_UnitsPerSecond * ts;
		const float rotateAmount = m_RigParams.m_DegreesPerSecond * 3.14f / 180.f * ts;

		const auto keyboard = input->GetKeyboardState();
		auto& mouse = input->GetMouseState();

		//if (!ImGuizmo::IsUsing())

		if (keyboard.IsKeyHeld(GENGINE_KEY_W)) TranslateZ(-moveAmount);//Translate(0.f, 0.f, -moveAmount);
		if (keyboard.IsKeyHeld(GENGINE_KEY_S)) TranslateZ(moveAmount);//Translate(0.f, 0.f, moveAmount);
		if (keyboard.IsKeyHeld(GENGINE_KEY_A)) TranslateX(-moveAmount);//Translate(-moveAmount, 0.f, 0.f);
		if (keyboard.IsKeyHeld(GENGINE_KEY_D)) TranslateX(moveAmount);//Translate(moveAmount, 0.f, 0.f);
		if (keyboard.IsKeyHeld(GENGINE_KEY_R)) TranslateY(moveAmount);//Translate(0.f, moveAmount, 0.f);
		if (keyboard.IsKeyHeld(GENGINE_KEY_F)) TranslateY(-moveAmount);//Translate(0.f, -moveAmount, 0.f);

		/*float run_speed;
		float strafe_speed;
		auto& engine = BaseApp::GetEngine();
		auto* input = engine.GetInputManager();
		float runsignZ = std::cos(GetYaw());
		float runsignX = std::sin(GetYaw());;

		float strafesignZ = std::sin(GetYaw());
		float strafesignX = std::cos(GetYaw());;

		const auto keyboard = input->GetKeyboardState();
		auto& mouse = input->GetMouseState();*/

		//if (!ImGuizmo::IsUsing())
		
		//if (keyboard.IsKeyHeld(GENGINE_KEY_W)) Translate(0.f, 0.f, moveAmount_Z);
		//if (keyboard.IsKeyHeld(GENGINE_KEY_S)) Translate(0.f, 0.f, -moveAmount_Z);
		//if (keyboard.IsKeyHeld(GENGINE_KEY_A)) Translate(moveAmount_X, 0.f, 0.f);
		//if (keyboard.IsKeyHeld(GENGINE_KEY_D)) Translate(-moveAmount_X , 0.f, 0.f);
		//if (keyboard.IsKeyHeld(GENGINE_KEY_W)) run_speed = 20.f;
		//else if (keyboard.IsKeyHeld(GENGINE_KEY_S)) run_speed = -20.f;
		//else run_speed = 0;

		//const float RunDelta_Z = run_speed * ts * runsignZ;
		//const float RunDelta_X = run_speed * ts * runsignX;
		//Translate(RunDelta_X, 0.f, RunDelta_Z);

	
		//if (keyboard.IsKeyHeld(GENGINE_KEY_A)) strafe_speed = 10.f;
		//else if (keyboard.IsKeyHeld(GENGINE_KEY_D)) strafe_speed = -10.f;
		//else strafe_speed = 0;

		//const float StrafeDelta_Z = strafe_speed * ts * strafesignZ;
		//const float StrafeDelta_X = strafe_speed * ts * strafesignX;
		//Translate(StrafeDelta_X, 0.f, -StrafeDelta_Z);
	
		//GENGINE_CORE_INFO("x:{}, z:{}", GetPosition().x, GetPosition().z);
		//const float moveAmount = m_RigParams.m_UnitsPerSecond * ts;
	
		////const float moveAmount_Z = run_speed * ts * signZ;
		////const float moveAmount_X = strafe_speed * ts * signX;
		////const float moveAmount_X = m_RigParams.m_UnitsPerSecond * ts * signX;
		//const float rotateAmount = m_RigParams.m_DegreesPerSecond * 3.14f / 180.f * ts;

		

		//if (keyboard.IsKeyHeld(GENGINE_KEY_R)) Translate(0.f, moveAmount, 0.f);
		//if (keyboard.IsKeyHeld(GENGINE_KEY_F)) Translate(0.f, -moveAmount, 0.f);

	    if (keyboard.IsKeyPressed(GENGINE_KEY_SPACE)) input->SetRelativeMouseMode(false);
		
		auto& mouseState = BaseApp::GetEngine().GetInputManager()->GetMouseState();
		
		if (mouse.IsRelative())
		{

			int32_t x = mouseState.m_XRel;
			int32_t y = mouseState.m_YRel;

			//GENGINE_CORE_INFO("relative mode Mouse motion - xPos: {}   yPos: {}", x, y);
			

			const int maxMouseSpeed = 750;
			// Rotation/sec at maximum speed
			const float maxYawSpeed = Math::Pi * 8;
			if (x != 0)
			{
				// Convert to ~[-1.0, 1.0]
				m_RigParams.m_YawSpeed = static_cast<float>(-x) / maxMouseSpeed * maxYawSpeed;
			}


			// Compute pitch
			constexpr float maxPitchSpeed = Pi * 5.f;

			//float pitchSpeed = 0.0f;
			if (y != 0)
			{
				// Convert to ~[-1.0, 1.0]
				m_RigParams.m_PitchSpeed = static_cast<float>(-y) / maxMouseSpeed * maxPitchSpeed;
			}

			if (!Math::NearZero(m_RigParams.m_YawSpeed))
			{
				m_RigParams.m_CurrentYaw += ts * m_RigParams.m_YawSpeed;
			
				const float angle = ts * m_RigParams.m_YawSpeed;
				RotateY(angle);
				//GENGINE_CORE_INFO("Yaw: {}", m_RigParams.m_CurrentYaw);
			}

			if (!Math::NearZero(m_RigParams.m_PitchSpeed))
			{

				m_RigParams.m_CurrentPitch += m_RigParams.m_PitchSpeed * ts;

				m_RigParams.m_CurrentPitch = glm::clamp(m_RigParams.m_CurrentPitch, -m_MaxPitch, m_MaxPitch);
				//GENGINE_CORE_INFO("Pitch: {}", m_RigParams.m_CurrentPitch);
				if (m_RigParams.m_CurrentPitch > -m_MaxPitch && m_RigParams.m_CurrentPitch < m_MaxPitch)
				{
					if (m_RigAttachment) m_RigAttachment->RotateX(m_RigParams.m_PitchSpeed * ts);
					else RotateX(m_RigParams.m_PitchSpeed * ts);
				}


				//m_RigParams.m_CurrentPitch += m_RigParams.m_PitchSpeed * ts;

				//m_RigParams.m_CurrentPitch = glm::clamp(m_RigParams.m_CurrentPitch, -m_MaxPitch, m_MaxPitch);
				////GENGINE_CORE_INFO("Pitch: {}", m_RigParams.m_CurrentPitch);
				//if (m_RigParams.m_CurrentPitch > -m_MaxPitch && m_RigParams.m_CurrentPitch < m_MaxPitch)
				//{
				//	if (m_RigAttachment) {

				//		m_RigAttachment->RotateX(m_RigParams.m_PitchSpeed * ts);
				//		if (!m_RigAttachment->GetChildrenRef().empty())
				//		{
				//			m_RigAttachment->GetChildrenRef()[0]->RotateX(-m_RigParams.m_PitchSpeed * ts);
				//			if (!m_RigAttachment->GetChildrenRef()[0]->GetChildrenRef().empty())
				//			{
				//				m_RigAttachment->GetChildrenRef()[0]->GetChildrenRef()[0]->RotateX(m_RigParams.m_PitchSpeed * ts);
				//			}
				//		}
				//		//m_RigAttachment->GetChildrenRef()[0]->RotateX(-m_RigParams.m_PitchSpeed * ts);
				//		//m_RigAttachment->GetChildrenRef()[0]->GetChildrenRef()[0]->RotateX(m_RigParams.m_PitchSpeed * ts);

				//	}
				//	else RotateX(m_RigParams.m_PitchSpeed * ts);
				


			}

			Reset();
		}
		
		else
		{
			if (keyboard.IsKeyHeld(GENGINE_KEY_Q))
			{
				RotateY(rotateAmount);
			}

			if (keyboard.IsKeyHeld(GENGINE_KEY_E))
			{
				RotateY(-rotateAmount);
			}


			if (keyboard.IsKeyHeld(GENGINE_KEY_T))
			{
				if (m_RigAttachment) m_RigAttachment->RotateX(rotateAmount);
				else RotateX(rotateAmount);
			}

			if (keyboard.IsKeyHeld(GENGINE_KEY_G))
			{
				if (m_RigAttachment) m_RigAttachment->RotateX(-rotateAmount);
				else RotateX(-rotateAmount);
			}
		}

		
	
	}
}