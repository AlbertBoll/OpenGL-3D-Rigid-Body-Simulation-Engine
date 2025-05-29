#pragma once
#include "Math/Math.h"

namespace GEngine
{
	using namespace Math;
	class _Camera
	{
	public:
		_Camera() = default;
		_Camera(const Mat4& projection, const Mat4& unReversedProjection);
		_Camera(const float degFov, const float width, const float height, const float nearP, const float farP);
		_Camera(const Mat4& projection)
			: m_Projection(projection) {}

		virtual ~_Camera() = default;
		float GetExposure() const { return m_Exposure; }
		float& GetExposure() { return m_Exposure; }
		const Mat4& GetProjection() const { return m_Projection; }
		const Mat4& GetUnReversedProjectionMatrix() const { return m_UnReversedProjection; }


		void SetProjectionMatrix(const Mat4 projection, const Mat4 unReversedProjection)
		{
			m_Projection = projection;
			m_UnReversedProjection = unReversedProjection;
		}

		void SetOrthoProjectionMatrix(const float width, const float height, const float nearP, const float farP)
		{
			//TODO(Karim): Make sure this is correct.
			m_Projection = glm::ortho(-width * 0.5f, width * 0.5f, -height * 0.5f, height * 0.5f, nearP, farP);
			m_UnReversedProjection = glm::ortho(-width * 0.5f, width * 0.5f, -height * 0.5f, height * 0.5f, nearP, farP);
		}

		void SetPerspectiveProjectionMatrix(const float radFov, const float width, const float height, const float nearP, const float farP)
		{
			m_Projection = glm::perspectiveFov(radFov, width, height, nearP, farP);
			m_UnReversedProjection = glm::perspectiveFov(radFov, width, height, nearP, farP);
		}

	protected:
		float m_Exposure = 0.8f;
		Mat4 m_Projection = Mat4(1.0f);
		//Currently only needed for shadow maps and ImGuizmo
		Mat4 m_UnReversedProjection = Mat4(1.0f);
	};

}
