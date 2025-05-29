#include "gepch.h"
#include "ShapeBox.h"
#include "Geometry/Geometry.h"


namespace GEngine
{


	void ShapeBox::Build(const std::vector<Vec3f>& pts)
	{
		int num = pts.size();

		for (int i = 0; i < num; i++) {
			m_bounds.Expand(pts[i]);
		}

		m_points.clear();
		m_points.push_back(Vec3f(m_bounds.mins.x, m_bounds.mins.y, m_bounds.mins.z));
		m_points.push_back(Vec3f(m_bounds.maxs.x, m_bounds.mins.y, m_bounds.mins.z));
		m_points.push_back(Vec3f(m_bounds.mins.x, m_bounds.maxs.y, m_bounds.mins.z));
		m_points.push_back(Vec3f(m_bounds.mins.x, m_bounds.mins.y, m_bounds.maxs.z));

		m_points.push_back(Vec3f(m_bounds.maxs.x, m_bounds.maxs.y, m_bounds.maxs.z));
		m_points.push_back(Vec3f(m_bounds.mins.x, m_bounds.maxs.y, m_bounds.maxs.z));
		m_points.push_back(Vec3f(m_bounds.maxs.x, m_bounds.mins.y, m_bounds.maxs.z));
		m_points.push_back(Vec3f(m_bounds.maxs.x, m_bounds.maxs.y, m_bounds.mins.z));

		m_CenterOfMass = (m_bounds.maxs + m_bounds.mins) * 0.5f;
		GENGINE_INFO("Center of mass: x: {}, y: {}, z: {}", m_CenterOfMass.x, m_CenterOfMass.y, m_CenterOfMass.z);

	}


	Vec3f ShapeBox::Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const
	{
		// Find the point in furthest in direction
		Vec3f dir_ = glm::normalize(dir);
		//Vec3f maxPt = glm::transpose(glm::toMat3(orient)) * m_points[0] + pos;
		Vec3f maxPt = glm::toMat3(orient) * m_points[0] + pos;
		float maxDist = glm::dot(dir, maxPt);
		for (int i = 1; i < m_points.size(); i++) {
			const Vec3f pt = glm::toMat3(orient) * m_points[i] + pos;
			//const Vec3f pt = glm::toMat3(orient) * m_points[i] + pos;
			const float dist = glm::dot(dir, pt);

			if (dist > maxDist) {
				maxDist = dist;
				maxPt = pt;
			}
		}

		//Vec3f norm = dir;
		Vec3f norm = glm::normalize(dir) * bias;
		//norm *= bias;

		return maxPt + norm;
	}

	/*void ShapeBox::HandleScaleChanged(const Vec3f& new_scale)
	{
		auto pts = m_MeshPoints;
		for (auto& pt : pts)
		{
			pt *= new_scale;
		}
		Build(pts);
	}*/


	Mat3 ShapeBox::InertiaTensor() const
	{
		// Inertia tensor for box centered around zero
		const float dx = m_bounds.maxs.x - m_bounds.mins.x;
		const float dy = m_bounds.maxs.y - m_bounds.mins.y;
		const float dz = m_bounds.maxs.z - m_bounds.mins.z;

		Mat3 tensor{ 0.f };
		tensor[0][0] = (dy * dy + dz * dz) / 12.0f;
		tensor[1][1] = (dx * dx + dz * dz) / 12.0f;
		tensor[2][2] = (dx * dx + dy * dy) / 12.0f;

		// Now we need to use the parallel axis theorem to get the inertia tensor for a box
		// that is not centered around the origin

		Vec3f cm;
		cm.x = (m_bounds.maxs.x + m_bounds.mins.x) * 0.5f;
		cm.y = (m_bounds.maxs.y + m_bounds.mins.y) * 0.5f;
		cm.z = (m_bounds.maxs.z + m_bounds.mins.z) * 0.5f;

		const Vec3f R = Vec3f(0, 0, 0) - cm;	// the displacement from center of mass to the origin
		const float R2 = glm::length2(R);
		Mat3 patTensor;
		patTensor[0] = Vec3f(R2 - R.x * R.x, R.x * R.y, R.x * R.z);
		patTensor[1] = Vec3f(R.y * R.x, R2 - R.y * R.y, R.y * R.z);
		patTensor[2] = Vec3f(R.z * R.x, R.z * R.y, R2 - R.z * R.z);
		patTensor = glm::transpose(patTensor);

		// Now we need to add the center of mass tensor and the parallel axis theorem tensor together;
		tensor += patTensor;
		return tensor;
	}

	Bounds ShapeBox::GetBounds(const Vec3f& pos, const Quat& orient) const
	{
		Vec3f corners[8];
		corners[0] = Vec3f(m_bounds.mins.x, m_bounds.mins.y, m_bounds.mins.z);
		corners[1] = Vec3f(m_bounds.mins.x, m_bounds.mins.y, m_bounds.maxs.z);
		corners[2] = Vec3f(m_bounds.mins.x, m_bounds.maxs.y, m_bounds.mins.z);
		corners[3] = Vec3f(m_bounds.maxs.x, m_bounds.mins.y, m_bounds.mins.z);

		corners[4] = Vec3f(m_bounds.maxs.x, m_bounds.maxs.y, m_bounds.maxs.z);
		corners[5] = Vec3f(m_bounds.maxs.x, m_bounds.maxs.y, m_bounds.mins.z);
		corners[6] = Vec3f(m_bounds.maxs.x, m_bounds.mins.y, m_bounds.maxs.z);
		corners[7] = Vec3f(m_bounds.mins.x, m_bounds.maxs.y, m_bounds.maxs.z);

		Bounds bounds;
		for (int i = 0; i < 8; i++) {
			//corners[i] = glm::transpose(glm::toMat3(orient)) * corners[i] + pos;
			corners[i] = glm::toMat3(orient) * corners[i] + pos;
			bounds.Expand(corners[i]);
		}

		return bounds;
	}


	float ShapeBox::FastestLinearSpeed(const Vec3f& angularVelocity, const Vec3f& dir) const
	{
		float maxSpeed = 0.0f;
		for (int i = 0; i < m_points.size(); i++) {
			Vec3f r = m_points[i] - m_CenterOfMass;
			Vec3f linearVelocity = glm::cross(angularVelocity, r);
			float speed = glm::dot(dir, linearVelocity);
			if (speed > maxSpeed) {
				maxSpeed = speed;
			}
		}
		return maxSpeed;
	}

}