#include "gepch.h"
#include "_Camera.h"

GEngine::_Camera::_Camera(const Mat4& projection, const Mat4& unReversedProjection) : m_Projection(projection), m_UnReversedProjection(unReversedProjection)
{

}

GEngine::_Camera::_Camera(const float degFov, const float width, const float height, const float nearP, const float farP) :
	m_Projection(glm::perspectiveFov(glm::radians(degFov), width, height, nearP, farP)), m_UnReversedProjection(glm::perspectiveFov(glm::radians(degFov), width, height, nearP, farP))
{

}
