#pragma once

#include "GEngine.h"
#include "Timestep.h"
#include "FrameClock.h"
#include "Camera/Camera.h"
#include "Managers/EventManager.h"
#include "Core/Renderer.h"
#include <Core/Scene.h>
#include "Core/RenderTarget.h"
#include <Camera/EditorCamera.h>



namespace GEngine
{
	//using namespace  Manager;
	class _Scene;

	class BaseApp
	{
		             
		friend class SceneHierarchyPanel;
	private:
		// First member, destroyed last: all derived/base GPU owners borrow its context.
		EngineContext m_EngineContext;
	public:
		BaseApp();

		NONCOPYMOVABLE(BaseApp);

		virtual ~BaseApp();

	    static GEngine& GetEngine(){ return EngineContext::Current().LegacyEngine(); };
		EngineContext& GetEngineContext() { return m_EngineContext; }

	    static Manager::WindowManager* GetWindowManager() { return EngineContext::TryGet() ? GetEngine().GetWindowManager() : nullptr; };
		static Manager::EventManager* GetEventManager()   { return EngineContext::TryGet() ? GetEngine().GetEventManager() : nullptr; };
		static Manager::InputManager* GetInputManager()   { return EngineContext::TryGet() ? GetEngine().GetInputManager() : nullptr; };
		SDLWindow* GetSDLWindow();

		//CameraBase* GetCamera(){ return m_EditorCamera; }

	    virtual void Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList);
		virtual void Initialize(const WindowProperties& WindowsPropertyList);
		//void OnResize(int new_width, int new_height) const;
		//void OnScroll(float new_zoom) const;

		virtual void Update(Timestep ts){};
		virtual void ProcessInput(Timestep ts);
		virtual void Run();
		const FrameTime& GetFrameTime() const { return m_FrameTime; }
		// Used only when the main context's actual swap interval is zero; 0 is uncapped.
		void SetManualFrameRateLimit(uint32_t framesPerSecond) { m_ManualFrameRateLimit = framesPerSecond; }
		uint32_t GetManualFrameRateLimit() const { return m_ManualFrameRateLimit; }
		bool IsRenderingSuspended() const { return m_Minimized || m_WindowHidden || m_WindowZeroSize; }
		bool HasVisibleViewport() const { return m_ViewportSize.x >= 1.f && m_ViewportSize.y >= 1.f; }
		virtual void Render();

		void ShutDown();


		void OnEvent(SDL_Event& e)const;
		virtual void ImGuiRender(){};
		virtual void OnUIRender() {};
		
	protected:
		CameraBase* m_EditorCamera{};
		//CameraBase* m_GameOrthoCamera{};
		//CameraBase* m_OrthoCamera{};
		//RefPtr<_Scene> m_ActiveScene;
		//RefPtr<_Scene> m_EditorScene;
		//Camera::_EditorCamera m_EditorCamera_;
		ScopedPtr<Scene> m_Scene;
		ScopedPtr<RenderTarget> m_RenderTarget{};
		ScopedPtr<PointShadowFrameBuffer> m_PointShadowFrameBuffer{};
		ScopedPtr<CascadeShadowFrameBuffer> m_CascadeShadowFrameBuffer{};
		ScopedPtr<MousePickFrameBuffer> m_MousePickFrameBuffer{};
		ScopedPtr<FinalFrameBuffer> m_FinalFrameBuffer{};
		ScopedPtr<UniformBufferObject<UniformType::MATRIX_4_4>> m_UniformBufferObject{};
		SDLWindow* m_SDLWindow{};

		Vec4f m_PlaneColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		Vec2f m_ViewportSize{ 1280, 720 };

		Vec2f m_ViewportBounds[2];

		FrameTime m_FrameTime{};
		uint32_t m_ManualFrameRateLimit = 60;
		bool m_Running = true;
		bool m_Initialize = false;
		bool m_Minimized = false;
		bool m_WindowHidden = false;
		bool m_WindowZeroSize = false;
		//Signal<void(Manager::WindowResizeParam)> m_WindowResizeSignal;
		
		int m_LastXRel{};
		int m_LastYRel{};
		int m_GizmoType = 0;
		bool m_ProjectionType[2]{ false, true };
		bool m_ViewportForcused = false;
		bool m_ViewportHovered = false;
	};




}
