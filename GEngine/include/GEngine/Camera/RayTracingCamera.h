#pragma once
#include "Math/Math.h"

namespace GEngine
{
	using namespace Math;

	class RayTracingCamera
	{

	public:
		RayTracingCamera(float verticalFOV, float nearClip, float farClip);

		bool OnUpdate(float ts);
		bool OnResize(uint32_t width, uint32_t height);

		bool IsOnResize(uint32_t width, uint32_t height);

		const auto& GetProjection() const { return m_Projection; }
		const auto& GetInverseProjection() const { return m_InverseProjection; }
		const auto& GetView() const { return m_View; }
		const auto& GetInverseView() const { return m_InverseView; }

		const auto& GetPosition() const { return m_Position; }
		const auto& GetDirection() const { return m_ForwardDirection; }

		const std::vector<Vec3f>& GetRayDirections() const { return m_RayDirections; }

		float GetRotationSpeed();

	private:
		void RecalculateProjection();
		void RecalculateView();
		void RecalculateRayDirections();

	private:
		Mat4 m_Projection{ 1.0f };
		Mat4 m_View{ 1.0f };
		Mat4 m_InverseProjection{ 1.0f };
		Mat4 m_InverseView{ 1.0f };
		Vec3f m_Position{ 0.0f, 0.0f, 0.0f };
		Vec3f m_ForwardDirection{ 0.0f, 0.0f, 0.0f };
		Vec2f m_LastMousePosition{ 0.0f, 0.0f };
		float m_VerticalFOV = 45.0f;
		uint32_t m_ViewportWidth = 0;
		uint32_t m_ViewportHeight = 0;
		float m_NearClip = 0.1f;
		float m_FarClip = 100.0f;
		bool m_FirstMouse = true;
		// Cached ray directions
		std::vector<Vec3f> m_RayDirections;

	

		

	};

}
