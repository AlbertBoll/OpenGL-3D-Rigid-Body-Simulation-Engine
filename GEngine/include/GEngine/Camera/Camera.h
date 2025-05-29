#pragma once
#include "Core/Actor.h"
#include "Math/Math.h"

namespace GEngine
{


	enum class CameraType
	{
		Perspective,
		Orthographic
	};

	struct PerspectiveSetting
	{
		//float m_ZoomLevel{ 1.0f };
		float m_FieldOfView{ 45.f };
		float m_AspectRatio{ 1.0f };
		float m_Near{ 0.01f };
		float m_Far{ 1000.f };
	};

	struct OrthographicSetting
	{
		float m_Left{ -1.0f };
		float m_Right{ 1.0f };
		float m_Bottom{ -1.0f };
		float m_Top{ 1.0f };
		//float m_Size{ 10.0f };
		//float m_AspectRatio{ 1.0f };
		float m_Near{ -1.0f };
		float m_Far{ 1.0f };

	};
	
	union CameraSetting
	{
		PerspectiveSetting m_PerspectiveSetting;
		OrthographicSetting m_OrthographicSetting;

		CameraSetting(){}


	};


	//class _Camera
	//{
	//public:
	//	_Camera() = default;
	//	_Camera(const Mat4& projection)
	//		: m_Projection(projection) {}

	//	virtual ~_Camera() = default;

	//	const Mat4& GetProjection() const { return m_Projection; }
	//protected:
	//	Mat4 m_Projection = Mat4(1.0f);
	//};
	

	class CameraBase: public Actor
	{

		friend class Entity;
		friend class SpriteEntity;
		friend class Renderer2D;

	public:
		CameraBase(): Actor(){}
		CameraBase(const CameraSetting& camera): m_CameraSetting{camera}{}
		void SetCameraSetting(const CameraSetting& camera_setting) { m_CameraSetting = camera_setting; }
		virtual void OnResize(int new_width, int new_height) = 0;
		virtual void OnResize(float ratio) = 0;
		virtual void OnScroll(float new_zoom_level) = 0;

		virtual void OnUpdateView()
		{
			m_View = glm::inverse(GetWorldTransform());
			//RecalculateViewProjection();
		}

		//void RecalculateViewProjection() { m_ViewProjection = m_Projection * m_View; }

		/*Mat4 GetViewProjection() const
		{
			return m_ViewProjection;
		}*/

		Mat4 GetView() const { return m_View; }
		CameraSetting& GetCameraSetting() { return m_CameraSetting; }

		Mat4 GetProjection() const { return m_Projection; }
		Vec2f& GetInitialMousePosition() { return m_InitialMousePosition; }
		//float GetZoomLevel() const { return m_CameraSetting.m_PerspectiveSetting.m_ZoomLevel; }

		bool GetActive()const { return m_IsActive; }
		void SetActive(bool isActive){ m_IsActive = isActive; }

		bool& GetIsActive() { return m_IsActive; }
		bool& GetIsFixAspectRatio() { return m_IsFixAspectRatio; }
		virtual void MouseRotate(const Vec2f& delta) {};
		virtual void MousePan(const Vec2f& delta) {};
	

	private:
		

	protected:
		CameraSetting m_CameraSetting;

		Mat4 m_Projection{ 1.0f };
		Mat4 m_View{ 1.0f };
		Vec2f m_InitialMousePosition{ 0.f, 0.f };
		//Mat4 m_ViewProjection{ 1.0f };
		bool m_IsActive{ true };
		bool m_IsFixAspectRatio{ false };
		float m_ViewportWidth = 1600, m_ViewportHeight = 900;
	};
}


