#pragma once

#include "GEngine.h"
#include "Timestep.h"
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
	public:
		BaseApp();

		NONCOPYMOVABLE(BaseApp);

		virtual ~BaseApp();

	    static GEngine& GetEngine(){ return m_GEngine;};

	    static Manager::WindowManager* GetWindowManager() { return m_GEngine.GetWindowManager(); };
		static Manager::EventManager* GetEventManager()   { return m_GEngine.GetEventManager(); };
		static Manager::InputManager* GetInputManager()   { return m_GEngine.GetInputManager(); };
		SDLWindow* GetSDLWindow();

		//CameraBase* GetCamera(){ return m_EditorCamera; }

	    virtual void Initialize(const std::initializer_list<WindowProperties>& WindowsPropertyList);
		virtual void Initialize(const WindowProperties& WindowsPropertyList);
		//void OnResize(int new_width, int new_height) const;
		//void OnScroll(float new_zoom) const;

		virtual void Update(Timestep ts){};
		virtual void ProcessInput(Timestep ts);
		virtual void Run();
		virtual void Render();

		void ShutDown();


		void OnEvent(SDL_Event& e)const;
		virtual void ImGuiRender(){};
		virtual void OnUIRender() {};
		
	protected:
		inline static GEngine m_GEngine;
		CameraBase* m_EditorCamera{};
		//CameraBase* m_GameOrthoCamera{};
		//CameraBase* m_OrthoCamera{};
		//RefPtr<_Scene> m_ActiveScene;
		//RefPtr<_Scene> m_EditorScene;
		//Camera::_EditorCamera m_EditorCamera_;
		ScopedPtr<Scene> m_Scene;
		ScopedPtr<RenderTarget> m_RenderTarget{};
		ScopedPtr<CascadeShadowFrameBuffer> m_CascadeShadowFrameBuffer{};
		ScopedPtr<MousePickFrameBuffer> m_MousePickFrameBuffer{};
		ScopedPtr<FinalFrameBuffer> m_FinalFrameBuffer{};
		ScopedPtr<UniformBufferObject<UniformType::MATRIX_4_4>> m_UniformBufferObject{};
		SDLWindow* m_SDLWindow{};

		Vec4f m_PlaneColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		Vec2f m_ViewportSize{ 1280, 720 };

		Vec2f m_ViewportBounds[2];

		uint64_t m_LastFrameTime{0};
		bool m_Running = true;
		inline static bool m_Initialize = false;
		inline static bool m_Minimized = false;
		//Signal<void(Manager::WindowResizeParam)> m_WindowResizeSignal;
		
		int m_LastXRel{};
		int m_LastYRel{};
		int m_GizmoType = 0;
		bool m_ProjectionType[2]{ false, true };
		bool m_ViewportForcused = false;
		bool m_ViewportHovered = false;
	};




}