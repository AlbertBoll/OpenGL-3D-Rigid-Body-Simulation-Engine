#include "gepch.h"
#include "ShapeBox.h"
#include "Geometry/Geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>


namespace GEngine
{

	bool ShapeBox::IsValidPointSet(const std::vector<Vec3f>& pts)
	{
		if (pts.empty()) {
			return false;
		}

		Bounds bounds;
		for (const Vec3f& point : pts) {
			if (!Math::IsFinite(point)) {
				return false;
			}
			bounds.Expand(point);
		}

		return Math::IsFinite(bounds.mins) && Math::IsFinite(bounds.maxs) &&
			bounds.WidthX() > Math::NumericalEpsilon &&
			bounds.WidthY() > Math::NumericalEpsilon &&
			bounds.WidthZ() > Math::NumericalEpsilon;
	}

	bool ShapeBox::IsValid() const
	{
		if (m_points.size() != 8 || !Math::IsFinite(m_bounds.mins) ||
			!Math::IsFinite(m_bounds.maxs) || !Math::IsFinite(m_CenterOfMass) ||
			m_bounds.WidthX() <= Math::NumericalEpsilon ||
			m_bounds.WidthY() <= Math::NumericalEpsilon ||
			m_bounds.WidthZ() <= Math::NumericalEpsilon) {
			return false;
		}

		return std::all_of(m_points.begin(), m_points.end(),
			[](const Vec3f& point) { return Math::IsFinite(point); });
	}

	void ShapeBox::Build(const std::vector<Vec3f>& pts)
	{
		if (!IsValidPointSet(pts)) {
			return;
		}

		Bounds bounds;
		for (const Vec3f& point : pts) {
			bounds.Expand(point);
		}

		std::vector<Vec3f> points;
		points.reserve(8);
		points.push_back(Vec3f(bounds.mins.x, bounds.mins.y, bounds.mins.z));
		points.push_back(Vec3f(bounds.maxs.x, bounds.mins.y, bounds.mins.z));
		points.push_back(Vec3f(bounds.mins.x, bounds.maxs.y, bounds.mins.z));
		points.push_back(Vec3f(bounds.mins.x, bounds.mins.y, bounds.maxs.z));

		points.push_back(Vec3f(bounds.maxs.x, bounds.maxs.y, bounds.maxs.z));
		points.push_back(Vec3f(bounds.mins.x, bounds.maxs.y, bounds.maxs.z));
		points.push_back(Vec3f(bounds.maxs.x, bounds.mins.y, bounds.maxs.z));
		points.push_back(Vec3f(bounds.maxs.x, bounds.maxs.y, bounds.mins.z));

		m_bounds = bounds;
		m_points.swap(points);
		m_CenterOfMass = (bounds.maxs + bounds.mins) * 0.5f;
		MarkGeometryChanged();
		GENGINE_INFO("Center of mass: x: {}, y: {}, z: {}", m_CenterOfMass.x, m_CenterOfMass.y, m_CenterOfMass.z);

	}


	bool ShapeBox::GetContactFace(const Vec3f& direction, const Vec3f& position,
		const Quat& orientation, BoxFaceFeature& output) const
	{
		if (!IsValid() || !Math::IsFinite(direction) || !Math::IsFinite(position) ||
			!Math::IsFinite(orientation)) {
			return false;
		}

		// Rescale before normalization so finite tiny/large directions neither underflow nor overflow.
		const float magnitude = std::max({ std::abs(direction.x), std::abs(direction.y), std::abs(direction.z) });
		if (magnitude == 0.0f) {
			return false;
		}
		const Vec3f scaledDirection = direction / magnitude;
		const Vec3f unitDirection = scaledDirection / glm::length(scaledDirection);
		const double orientationLength2 = double(orientation.w) * orientation.w +
			double(orientation.x) * orientation.x + double(orientation.y) * orientation.y +
			double(orientation.z) * orientation.z;
		if (std::abs(orientationLength2 - 1.0) > 1.0e-4) {
			return false;
		}
		const Mat3 rotation = glm::toMat3(orientation * float(1.0 / std::sqrt(orientationLength2)));
		const Vec3f localDirection = glm::transpose(rotation) * unitDirection;
		const float bestAlignment = std::max({ std::abs(localDirection.x),
			std::abs(localDirection.y), std::abs(localDirection.z) });
		int axis = 0;
		while (axis < 2 && std::abs(localDirection[axis]) < bestAlignment - BoxFaceAlignmentTolerance) {
			++axis;
		}
		const bool positive = localDirection[axis] > 0.0f;
		const int u = (axis + 1) % 3;
		const int v = (axis + 2) % 3;

		BoxFaceFeature face;
		face.id = static_cast<BoxFaceId>(2 * axis + (positive ? 1 : 0));
		face.shapeRevision = GetRevision();
		face.normal = rotation[axis] * (positive ? 1.0f : -1.0f);
		face.alignment = glm::dot(face.normal, unitDirection);
		// Cycling the tangent axes makes u cross v point along the positive face axis.
		for (int corner = 0; corner < 4; ++corner) {
			Vec3f point = m_bounds.mins;
			point[axis] = positive ? m_bounds.maxs[axis] : m_bounds.mins[axis];
			point[u] = (corner == 1 || corner == 2) ? m_bounds.maxs[u] : m_bounds.mins[u];
			point[v] = (corner >= 2) ? m_bounds.maxs[v] : m_bounds.mins[v];
			face.vertices[corner] = rotation * point + position;
			if (!Math::IsFinite(face.vertices[corner])) {
				return false;
			}
		}
		if (!positive) {
			std::swap(face.vertices[1], face.vertices[3]);
		}
		// Reject faces collapsed by world-coordinate rounding. Double products also handle
		// the area of finite very small/large faces without float underflow/overflow.
		for (int corner = 0; corner < 4; ++corner) {
			const glm::dvec3 edge0 = glm::dvec3(face.vertices[(corner + 1) % 4]) - glm::dvec3(face.vertices[corner]);
			const glm::dvec3 edge1 = glm::dvec3(face.vertices[(corner + 2) % 4]) - glm::dvec3(face.vertices[(corner + 1) % 4]);
			if (glm::dot(glm::cross(edge0, edge1), glm::dvec3(face.normal)) <= 0.0) {
				return false;
			}
		}
		output = face;
		return true;
	}

	Vec3f ShapeBox::Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const
	{
		if (!IsValid()) {
			GENGINE_CORE_ASSERT(false, "ShapeBox::Support called without valid box geometry");
			return Vec3f(std::numeric_limits<float>::quiet_NaN());
		}

		// Find the point in furthest in direction
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
		Vec3f norm = Math::NormalizeOr(dir) * bias;
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
