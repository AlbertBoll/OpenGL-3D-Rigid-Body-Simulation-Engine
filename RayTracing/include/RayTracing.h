#pragma once

#include "Core\BaseApp.h"
#include "Core/Image.h"
#include "Core/SimpleRenderer.h"
#include "Renderer/FrameScheduler.h"
#include "Camera/RaytracingCamera.h"
#include "Core/RayTracingScene.h"

namespace GEngine
{
	class RayTracingAPP : public BaseApp
	{

	public:
		RayTracingAPP();
		virtual ~RayTracingAPP() override;

		void Update(Timestep ts) override;
		void Render()override;
		ApplicationInitializationResult Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList)override;
		ApplicationInitializationResult Initialize(const WindowProperties& prop = WindowProperties{}) override;
		void ProcessInput(Timestep ts) override;
		void OnUIRender()override;

	private:
		[[nodiscard]] ImageResult GenerateImage();

	private:
        std::optional<ScheduleError> m_UIFailure;
		uint32_t m_Width{};
		uint32_t m_Height{};
		SimpleRenderer m_Renderer;
		RayTracingCamera m_Camera;
		RayTracingScene m_RayScene;
		float m_LastRenderTime{};
	
	};
}


using namespace GEngine;