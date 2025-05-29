#pragma once
#include "Camera/Camera.h"

namespace GEngine
{
	namespace Camera
	{
		enum class CameraMode
		{
			NONE, FLYCAM, ARCBALL
		};

		class _EditorCamera : public _Camera
		{
		public:
			_EditorCamera() = default;
			_EditorCamera(float fov, float aspectRatio, float nearClip, float farClip);
			_EditorCamera(const float degFov, const float width, const float height, const float nearP, const float farP);
			void Initialize();
			void OnUpdate(Timestep ts);
			//void OnEvent(Event& e);

			inline float GetDistance() const { return m_Distance; }
			inline void SetDistance(float distance) { m_Distance = distance; }

			void SetViewportSize(float width, float height);

			const Mat4& GetViewMatrix() const { return m_ViewMatrix; }
			Mat4 GetViewProjection() const { return m_Projection * m_ViewMatrix; }
			Mat4 GetUnReversedViewProjection() const { return GetUnReversedProjectionMatrix() * m_ViewMatrix; }

			Vec3f GetUpDirection() const;
			Vec3f GetRightDirection() const;
			Vec3f GetForwardDirection() const;
			const Vec3f& GetPosition() const { return m_Position; }
			Quat GetOrientation() const;
			float GetCameraSpeed() const;
			float GetPitch() const { return m_Pitch; }
			float GetYaw() const { return m_Yaw; }
			bool OnMouseScroll(float new_zoom_level);
			[[nodiscard]] float GetFOV() const { return m_FOV; }
			[[nodiscard]] float GetAspectRatio() const { return m_AspectRatio; }
			[[nodiscard]] float GetNearClip() const { return m_NearClip; }
			[[nodiscard]] float GetFarClip() const { return m_FarClip; }
			void UpdateView();

		private:
			void UpdateProjection();
			
			void Focus(const Vec3f& focusPoint);

		

			void MousePan(const Vec2f& delta);
			void MouseRotate(const Vec2f& delta);
			void MouseZoom(float delta);

			Vec3f CalculatePosition() const;

			std::pair<float, float> PanSpeed() const;
			float RotationSpeed() const;
			float ZoomSpeed() const;
		public:
			float m_FOV = 45.0f, m_AspectRatio = 1.778f, m_NearClip = 0.1f, m_FarClip = 1000.0f;
			bool m_IsActive = false;
			bool m_Panning, m_Rotating;

			Mat4 m_ViewMatrix;

			Vec3f m_Position;
			Vec3f m_Direction;
			Vec3f m_FocalPoint;			

			Vec3f m_InitialFocalPoint;
			Vec3f m_InitialRotation;
			Vec3f m_PositionDelta{};
			Vec3f m_RightDirection{};

			Vec2f m_InitialMousePosition{};


			float m_NormalSpeed{ 0.002f };
			float m_Distance;
			float m_Pitch, m_Yaw;
			float m_PitchDelta{}, m_YawDelta{};
			float m_MinFocusDistance{ 100.0f };
			constexpr static float MIN_SPEED{ 0.0005f }, MAX_SPEED{ 2.0f };
			CameraMode m_CameraMode{ CameraMode::ARCBALL };

			uint32_t m_ViewportWidth = 1280, m_ViewportHeight = 720;
		};


		class EditorCamera : public CameraBase
		{
		public:
			EditorCamera(float fov = 45.f, float aspect_ratio = 1.0f, float near_field = 0.01f, float far_field = 1000.f);
			void SetPerspective(float field_of_view = 45.f, float aspect_ratio = 1.0f, float near_field = 0.01f, float far_field = 1000.f);
			void SetPerspective(const CameraSetting& setting);

			void Update(Timestep ts)override;
			void OnUpdateView()override;
			void OnResize(int new_width, int new_height) override;
			void OnResize(float ratio) override;
			inline float GetDistance() const { return m_Distance; }
			inline void SetDistance(float distance) { m_Distance = distance; }


			//float GetPitch() const { return GetLocalTransformation().; }
			//float GetYaw() const { return m_Yaw; }

			// Inherited via CameraBase
			virtual void OnScroll(float new_zoom_level) override;


		private:

			Vec3f CalculatePosition() const;
			void MousePan(const Vec2f& delta)override;
			void MouseRotate(const Vec2f& delta)override;
			void MouseZoom(float delta);

			std::pair<float, float> PanSpeed() const;
			float RotationSpeed() const;
			float ZoomSpeed() const;

		private:
			//float m_FOV = 45.0f, m_AspectRatio = 1.778f, m_NearClip = 0.1f, m_FarClip = 1000.0f;
			//Mat4 m_ViewMatrix;
			//Vec3f m_Position = { 0.0f, 0.0f, 0.0f };
			Vec3f m_FocalPoint = { 0.0f, 0.0f, 0.0f };

			glm::vec2 m_InitialMousePosition = { 0.0f, 0.0f };

			float m_Distance = 10.0f;
			//float m_Pitch = 0.0f, m_Yaw = 0.0f;

			//float m_ViewportWidth = 1280, m_ViewportHeight = 720;

		};
	}
}


