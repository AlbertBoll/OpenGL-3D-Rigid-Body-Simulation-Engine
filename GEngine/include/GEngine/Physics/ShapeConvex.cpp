#include "gepch.h"
#include "ShapeConvex.h"


#include <algorithm>
#include <cmath>
#include <limits>

namespace GEngine
{
	namespace
	{
		float HullLengthTolerance(const Bounds& bounds)
		{
			const float scale = std::max({ bounds.WidthX(), bounds.WidthY(), bounds.WidthZ() });
			return std::max(Math::NumericalEpsilon, scale * Math::NumericalEpsilon);
		}

		bool HasFinitePointSet(const std::vector<Vec3f>& points, Bounds& bounds)
		{
			if (points.size() < 4) {
				return false;
			}

			for (const Vec3f& point : points) {
				if (!Math::IsFinite(point)) {
					return false;
				}
				bounds.Expand(point);
			}

			const float tolerance = HullLengthTolerance(bounds);
			return Math::IsFinite(bounds.mins) && Math::IsFinite(bounds.maxs) &&
				Math::IsFinite(tolerance) &&
				bounds.WidthX() > tolerance && bounds.WidthY() > tolerance &&
				bounds.WidthZ() > tolerance;
		}
	}

	/*
	====================================================
	FindPointFurthestInDir
	====================================================
	*/
	static int FindPointFurthestInDir(const std::vector<Vec3f>& pts, const Vec3f& dir) {
		int maxIdx = 0;
		int size = pts.size();
		float maxDist = glm::dot(dir, pts[0]);
		for (int i = 1; i < size; i++) {
			float dist = glm::dot(dir, pts[i]);
			if (dist > maxDist) {
				maxDist = dist;
				maxIdx = i;
			}
		}
		return maxIdx;
	}

	/*
	====================================================
	DistanceFromTriangle
	====================================================
	*/
	static float DistanceFromTriangle(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& pt) {
		Vec3f ab = b - a;
		Vec3f ac = c - a;
		Vec3f normal = Math::NormalizeOr(glm::cross(ab, ac));
		//normal.Normalize();

		Vec3f ray = pt - a;
		float dist = glm::dot(ray, normal);
		return dist;
	}

	/*
	====================================================
	BuildTetrahedron
	====================================================
	*/
	static bool BuildTetrahedron(const std::vector<Vec3f>& verts, const float tolerance,
		std::vector<Vec3f>& hullPts, std::vector<tri_t>& hullTris) {
		hullPts.clear();
		hullTris.clear();

		Vec3f points[4];
		float maxDistanceSquared = 0.0f;
		for (std::size_t a = 0; a < verts.size(); ++a) {
			for (std::size_t b = a + 1; b < verts.size(); ++b) {
				const float distanceSquared = glm::length2(verts[b] - verts[a]);
				if (distanceSquared > maxDistanceSquared) {
					maxDistanceSquared = distanceSquared;
					points[0] = verts[a];
					points[1] = verts[b];
				}
			}
		}
		if (!Math::IsFinite(maxDistanceSquared) || maxDistanceSquared <= tolerance * tolerance) {
			return false;
		}

		const Vec3f line = points[1] - points[0];
		const float lineLengthSquared = glm::length2(line);
		float maxLineDistanceSquared = 0.0f;
		for (const Vec3f& vertex : verts) {
			const float distanceSquared =
				glm::length2(glm::cross(line, vertex - points[0])) / lineLengthSquared;
			if (distanceSquared > maxLineDistanceSquared) {
				maxLineDistanceSquared = distanceSquared;
				points[2] = vertex;
			}
		}
		if (!Math::IsFinite(maxLineDistanceSquared) || maxLineDistanceSquared <= tolerance * tolerance) {
			return false;
		}

		const Vec3f planeNormal = Math::NormalizeOr(glm::cross(
			points[1] - points[0], points[2] - points[0]));
		float maxPlaneDistance = 0.0f;
		for (const Vec3f& vertex : verts) {
			const float distance = std::fabs(glm::dot(vertex - points[0], planeNormal));
			if (distance > maxPlaneDistance) {
				maxPlaneDistance = distance;
				points[3] = vertex;
			}
		}
		if (!Math::IsFinite(maxPlaneDistance) || maxPlaneDistance <= tolerance) {
			return false;
		}

		// This is important for making sure the ordering is CCW for all faces.
		float dist = DistanceFromTriangle(points[0], points[1], points[2], points[3]);
		if (!Math::IsFinite(dist) || std::fabs(dist) <= tolerance) {
			return false;
		}
		if (dist > 0.0f) {
			std::swap(points[0], points[1]);
		}

		// Build the tetrahedron
		hullPts.push_back(points[0]);
		hullPts.push_back(points[1]);
		hullPts.push_back(points[2]);
		hullPts.push_back(points[3]);

		tri_t tri;
		tri.a = 0;
		tri.b = 1;
		tri.c = 2;
		hullTris.push_back(tri);

		tri.a = 0;
		tri.b = 2;
		tri.c = 3;
		hullTris.push_back(tri);

		tri.a = 2;
		tri.b = 1;
		tri.c = 3;
		hullTris.push_back(tri);

		tri.a = 1;
		tri.b = 0;
		tri.c = 3;
		hullTris.push_back(tri);
		return true;
	}


	/*
	====================================================
	RemoveInternalPoints
	====================================================
	*/
	static void RemoveInternalPoints(const std::vector<Vec3f>& hullPoints, const std::vector< tri_t >& hullTris, std::vector<Vec3f>& checkPts) {
		for (int i = 0; i < checkPts.size(); i++) {
			const Vec3f& pt = checkPts[i];

			bool isExternal = false;
			for (int t = 0; t < hullTris.size(); t++) {
				const tri_t& tri = hullTris[t];
				const Vec3f& a = hullPoints[tri.a];
				const Vec3f& b = hullPoints[tri.b];
				const Vec3f& c = hullPoints[tri.c];

				// If the point is in front of any triangle then it's external
				float dist = DistanceFromTriangle(a, b, c, pt);
				if (dist > 0.0f) {
					isExternal = true;
					break;
				}
			}

			// if it's not external, then it's inside the polyhedron and should be removed
			if (!isExternal) {
				checkPts.erase(checkPts.begin() + i);
				i--;
			}
		}

		// Also remove any points that are just a little too close to the hull points
		for (int i = 0; i < checkPts.size(); i++) {
			const Vec3f& pt = checkPts[i];

			bool isTooClose = false;
			for (int j = 0; j < hullPoints.size(); j++) {
				Vec3f hullPt = hullPoints[j];
				Vec3f ray = hullPt - pt;
				if (glm::length2(ray) < 0.01f * 0.01f) 
				{	// 1cm is too close
					isTooClose = true;
					break;
				}
			}

			if (isTooClose) {
				checkPts.erase(checkPts.begin() + i);
				i--;
			}
		}
	}


	/*
	====================================================
	IsEdgeUnique
	This will compare the incoming edge with all the edges in the facing tris and then return true if it's unique
	====================================================
	*/
	static bool IsEdgeUnique(const std::vector<tri_t>& tris, const std::vector<int>& facingTris, const int ignoreTri, const edge_t& edge) {
		for (int i = 0; i < facingTris.size(); i++) {
			const int triIdx = facingTris[i];
			if (ignoreTri == triIdx) {
				continue;
			}

			const tri_t& tri = tris[triIdx];

			edge_t edges[3];
			edges[0].a = tri.a;
			edges[0].b = tri.b;

			edges[1].a = tri.b;
			edges[1].b = tri.c;

			edges[2].a = tri.c;
			edges[2].b = tri.a;

			for (int e = 0; e < 3; e++) {
				if (edge == edges[e]) {
					return false;
				}
			}
		}
		return true;
	}

	/*
	====================================================
	AddPoint
	====================================================
	*/
	static void AddPoint(std::vector< Vec3f >& hullPoints, std::vector<tri_t>& hullTris, const Vec3f& pt) {
		// This point is outside
		// Now we need to remove old triangles and build new ones

		// Find all the triangles that face this point
		std::vector<int> facingTris;
		for (int i = (int)hullTris.size() - 1; i >= 0; i--) {
			const tri_t& tri = hullTris[i];

			const Vec3f& a = hullPoints[tri.a];
			const Vec3f& b = hullPoints[tri.b];
			const Vec3f& c = hullPoints[tri.c];

			const float dist = DistanceFromTriangle(a, b, c, pt);
			if (dist > 0.0f) {
				facingTris.push_back(i);
			}
		}

		// Now find all edges that are unique to the tris, these will be the edges that form the new triangles
		std::vector<edge_t> uniqueEdges;
		for (int i = 0; i < facingTris.size(); i++) {
			const int triIdx = facingTris[i];
			const tri_t& tri = hullTris[triIdx];

			edge_t edges[3];
			edges[0].a = tri.a;
			edges[0].b = tri.b;

			edges[1].a = tri.b;
			edges[1].b = tri.c;

			edges[2].a = tri.c;
			edges[2].b = tri.a;

			for (int e = 0; e < 3; e++) {
				if (IsEdgeUnique(hullTris, facingTris, triIdx, edges[e])) {
					uniqueEdges.push_back(edges[e]);
				}
			}
		}

		// now remove the old facing tris
		for (int i = 0; i < facingTris.size(); i++) {
			hullTris.erase(hullTris.begin() + facingTris[i]);
		}

		// Now add the new point
		hullPoints.push_back(pt);
		const int newPtIdx = (int)hullPoints.size() - 1;

		// Now add triangles for each unique edge
		for (int i = 0; i < uniqueEdges.size(); i++) {
			const edge_t& edge = uniqueEdges[i];

			tri_t tri;
			tri.a = edge.a;
			tri.b = edge.b;
			tri.c = newPtIdx;
			hullTris.push_back(tri);
		}
	}

	/*
	====================================================
	RemoveUnreferencedVerts
	====================================================
	*/
	static void RemoveUnreferencedVerts(std::vector<Vec3f>& hullPoints, std::vector<tri_t>& hullTris) {
		for (int i = 0; i < hullPoints.size(); i++) {

			bool isUsed = false;
			for (int j = 0; j < hullTris.size(); j++) {
				const tri_t& tri = hullTris[j];

				if (tri.a == i || tri.b == i || tri.c == i) {
					isUsed = true;
					break;
				}
			}

			if (isUsed) {
				continue;
			}

			for (int j = 0; j < hullTris.size(); j++) {
				tri_t& tri = hullTris[j];
				if (tri.a > i) {
					tri.a--;
				}
				if (tri.b > i) {
					tri.b--;
				}
				if (tri.c > i) {
					tri.c--;
				}
			}

			hullPoints.erase(hullPoints.begin() + i);
			i--;
		}
	}


	/*
	====================================================
	ExpandConvexHull
	====================================================
	*/
	static void ExpandConvexHull(std::vector<Vec3f>& hullPoints, std::vector<tri_t>& hullTris, const std::vector<Vec3f>& verts) {
		std::vector<Vec3f> externalVerts = verts;
		RemoveInternalPoints(hullPoints, hullTris, externalVerts);

		while (externalVerts.size() > 0) {
			int ptIdx = FindPointFurthestInDir(externalVerts, externalVerts[0]);

			Vec3f pt = externalVerts[ptIdx];

			// remove this element
			externalVerts.erase(externalVerts.begin() + ptIdx);

			AddPoint(hullPoints, hullTris, pt);

			RemoveInternalPoints(hullPoints, hullTris, externalVerts);
		}

		RemoveUnreferencedVerts(hullPoints, hullTris);
	}


	/*
	====================================================
	IsExternal
	====================================================
	*/
	static bool IsExternal(const std::vector<Vec3f>& pts, const std::vector< tri_t >& tris, const Vec3f& pt) {
		bool isExternal = false;
		for (int t = 0; t < tris.size(); t++) {
			const tri_t& tri = tris[t];
			const Vec3f& a = pts[tri.a];
			const Vec3f& b = pts[tri.b];
			const Vec3f& c = pts[tri.c];

			// If the point is in front of any triangle then it's external
			float dist = DistanceFromTriangle(a, b, c, pt);
			if (dist > 0.0f) {
				isExternal = true;
				break;
			}
		}

		return isExternal;
	}

	/*
	====================================================
	CalculateCenterOfMass
	====================================================
	*/
	static bool CalculateCenterOfMass(const std::vector<Vec3f>& pts,
		const std::vector< tri_t >& tris, Vec3f& centerOfMass) {
		const int numSamples = 100;

		Bounds bounds;
		bounds.Expand(pts);

		const float dx = bounds.WidthX() / (float)numSamples;
		const float dy = bounds.WidthY() / (float)numSamples;
		const float dz = bounds.WidthZ() / (float)numSamples;
		if (!Math::IsFinite(dx) || !Math::IsFinite(dy) || !Math::IsFinite(dz) ||
			dx <= 0.0f || dy <= 0.0f || dz <= 0.0f) {
			return false;
		}

		Vec3f cm(0.0f);
		int sampleCount = 0;
		for (int xIndex = 0; xIndex < numSamples; ++xIndex) {
			const float x = bounds.mins.x + dx * static_cast<float>(xIndex);
			for (int yIndex = 0; yIndex < numSamples; ++yIndex) {
				const float y = bounds.mins.y + dy * static_cast<float>(yIndex);
				for (int zIndex = 0; zIndex < numSamples; ++zIndex) {
					const float z = bounds.mins.z + dz * static_cast<float>(zIndex);
					Vec3f pt(x, y, z);

					if (IsExternal(pts, tris, pt)) {
						continue;
					}

					cm += pt;
					sampleCount++;
				}
			}
		}

		if (sampleCount == 0) {
			return false;
		}

		cm /= static_cast<float>(sampleCount);
		if (!Math::IsFinite(cm)) {
			return false;
		}

		centerOfMass = cm;
		return true;
	}


	/*
	====================================================
	CalculateInertiaTensor
	====================================================
	*/
	static bool CalculateInertiaTensor(const std::vector<Vec3f>& pts,
		const std::vector<tri_t>& tris, const Vec3f& cm, Mat3& inertiaTensor) {
		const int numSamples = 100;

		Bounds bounds;
		bounds.Expand(pts);

		Mat3 tensor{0.f};


		const float dx = bounds.WidthX() / (float)numSamples;
		const float dy = bounds.WidthY() / (float)numSamples;
		const float dz = bounds.WidthZ() / (float)numSamples;
		if (!Math::IsFinite(dx) || !Math::IsFinite(dy) || !Math::IsFinite(dz) ||
			dx <= 0.0f || dy <= 0.0f || dz <= 0.0f) {
			return false;
		}

		int sampleCount = 0;
		for (int xIndex = 0; xIndex < numSamples; ++xIndex) {
			const float x = bounds.mins.x + dx * static_cast<float>(xIndex);
			for (int yIndex = 0; yIndex < numSamples; ++yIndex) {
				const float y = bounds.mins.y + dy * static_cast<float>(yIndex);
				for (int zIndex = 0; zIndex < numSamples; ++zIndex) {
					const float z = bounds.mins.z + dz * static_cast<float>(zIndex);
					Vec3f pt(x, y, z);

					if (IsExternal(pts, tris, pt)) {
						continue;
					}

					// Get the point relative to the center of mass
					pt -= cm;

					tensor[0][0] += pt.y * pt.y + pt.z * pt.z;
					tensor[1][1] += pt.z * pt.z + pt.x * pt.x;
					tensor[2][2] += pt.x * pt.x + pt.y * pt.y;

					tensor[1][0] += -1.0f * pt.x * pt.y;
					tensor[2][0] += -1.0f * pt.x * pt.z;
					tensor[2][1] += -1.0f * pt.y * pt.z;

					tensor[0][1] += -1.0f * pt.x * pt.y;
					tensor[0][2] += -1.0f * pt.x * pt.z;
					tensor[1][2] += -1.0f * pt.y * pt.z;

					sampleCount++;
				}
			}
		}

		if (sampleCount == 0) {
			return false;
		}

		tensor *= 1.0f / static_cast<float>(sampleCount);
		for (int column = 0; column < 3; ++column) {
			if (!Math::IsFinite(tensor[column])) {
				return false;
			}
		}

		inertiaTensor = tensor;
		return true;
	}

	static bool IsValidHull(const std::vector<Vec3f>& hullPoints,
		const std::vector<tri_t>& hullTriangles, const float tolerance) {
		if (hullPoints.size() < 4 || hullTriangles.size() < 4) {
			return false;
		}

		double signedVolumeTimesSix = 0.0;
		const Vec3f reference = hullPoints[0];
		const float areaToleranceSquared =
			tolerance * tolerance * tolerance * tolerance;
		for (const tri_t& triangle : hullTriangles) {
			if (triangle.a < 0 || triangle.b < 0 || triangle.c < 0 ||
				triangle.a >= static_cast<int>(hullPoints.size()) ||
				triangle.b >= static_cast<int>(hullPoints.size()) ||
				triangle.c >= static_cast<int>(hullPoints.size()) ||
				triangle.a == triangle.b || triangle.b == triangle.c || triangle.c == triangle.a) {
				return false;
			}

			const Vec3f a = hullPoints[triangle.a] - reference;
			const Vec3f b = hullPoints[triangle.b] - reference;
			const Vec3f c = hullPoints[triangle.c] - reference;
			const Vec3f faceCross = glm::cross(b - a, c - a);
			if (!Math::IsFinite(a) || !Math::IsFinite(faceCross) ||
				glm::length2(faceCross) <= areaToleranceSquared) {
				return false;
			}
			signedVolumeTimesSix += static_cast<double>(glm::dot(a, glm::cross(b, c)));
		}

		const double volumeTolerance = static_cast<double>(tolerance) * tolerance * tolerance;
		return std::isfinite(signedVolumeTimesSix) &&
			std::fabs(signedVolumeTimesSix) > volumeTolerance;
	}

	static bool BuildConvexHull(const std::vector<Vec3f>& verts,
		std::vector<Vec3f>& hullPts, std::vector<tri_t>& hullTris) {
		Bounds inputBounds;
		if (!HasFinitePointSet(verts, inputBounds)) {
			return false;
		}
		const float tolerance = HullLengthTolerance(inputBounds);

		// Build a tetrahedron
		if (!BuildTetrahedron(verts, tolerance, hullPts, hullTris)) {
			return false;
		}

		ExpandConvexHull(hullPts, hullTris, verts);
		return IsValidHull(hullPts, hullTris, tolerance);
	}


	void ShapeConvex::Build(const std::vector<Vec3f>& pts)
	{
		std::vector< Vec3f > hullPoints;
		std::vector< tri_t > hullTriangles;
		if (!BuildConvexHull(pts, hullPoints, hullTriangles)) {
			return;
		}

		Bounds bounds;
		bounds.Expand(hullPoints);

		Vec3f centerOfMass(0.0f);
		Mat3 inertiaTensor(0.0f);
		if (!CalculateCenterOfMass(hullPoints, hullTriangles, centerOfMass) ||
			!CalculateInertiaTensor(hullPoints, hullTriangles, centerOfMass, inertiaTensor)) {
			return;
		}

		m_MeshPoints = pts;
		m_Points.swap(hullPoints);
		m_Bounds = bounds;
		m_CenterOfMass = centerOfMass;
		m_InertiaTensor = inertiaTensor;
		m_IsValid = true;
		MarkGeometryChanged();
	}

	Vec3f ShapeConvex::Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const
	{
		if (!IsValid() || !Math::IsFinite(dir) || !Math::IsFinite(pos) ||
			!Math::IsFinite(orient) || !Math::IsFinite(bias)) {
			return Vec3f(std::numeric_limits<float>::quiet_NaN());
		}

		// Find the point in furthest in direction
		//Vec3f maxPt = glm::transpose(glm::toMat3(orient)) * m_Points[0] + pos;
		Vec3f maxPt = glm::toMat3(orient) * m_Points[0] + pos;
		float maxDist = glm::dot(dir, maxPt);
		for (int i = 1; i < m_Points.size(); i++) {
			//const Vec3f pt = glm::transpose(glm::toMat3(orient)) * m_Points[i] + pos;
			const Vec3f pt = glm::toMat3(orient) * m_Points[i] + pos;
			const float dist = glm::dot(dir, pt);

			if (dist > maxDist) {
				maxDist = dist;
				maxPt = pt;
			}
		}

		Vec3f norm = Math::NormalizeOr(dir);
		
		norm *= bias;

		return maxPt + norm;
	}

	Bounds ShapeConvex::GetBounds(const Vec3f& pos, const Quat& orient) const
	{
		if (!IsValid() || !Math::IsFinite(pos) || !Math::IsFinite(orient)) {
			return Bounds();
		}

		Vec3f corners[8];
		corners[0] = Vec3f(m_Bounds.mins.x, m_Bounds.mins.y, m_Bounds.mins.z);
		corners[1] = Vec3f(m_Bounds.mins.x, m_Bounds.mins.y, m_Bounds.maxs.z);
		corners[2] = Vec3f(m_Bounds.mins.x, m_Bounds.maxs.y, m_Bounds.mins.z);
		corners[3] = Vec3f(m_Bounds.maxs.x, m_Bounds.mins.y, m_Bounds.mins.z);

		corners[4] = Vec3f(m_Bounds.maxs.x, m_Bounds.maxs.y, m_Bounds.maxs.z);
		corners[5] = Vec3f(m_Bounds.maxs.x, m_Bounds.maxs.y, m_Bounds.mins.z);
		corners[6] = Vec3f(m_Bounds.maxs.x, m_Bounds.mins.y, m_Bounds.maxs.z);
		corners[7] = Vec3f(m_Bounds.mins.x, m_Bounds.maxs.y, m_Bounds.maxs.z);

		Bounds bounds;
		for (int i = 0; i < 8; i++) {
			corners[i] = glm::toMat3(orient) * corners[i] + pos;
			//corners[i] = glm::transpose(glm::toMat3(orient)) * corners[i] + pos;
			bounds.Expand(corners[i]);
		}

		return bounds;
	}

	float ShapeConvex::FastestLinearSpeed(const Vec3f& angularVelocity, const Vec3f& dir) const
	{
		if (!IsValid() || !Math::IsFinite(angularVelocity) || !Math::IsFinite(dir)) {
			return 0.0f;
		}

		float maxSpeed = 0.0f;
		for (int i = 0; i < m_Points.size(); i++) {
			Vec3f r = m_Points[i] - m_CenterOfMass;
			Vec3f linearVelocity = glm::cross(angularVelocity, r);
			float speed = glm::dot(dir, linearVelocity);
			if (speed > maxSpeed) {
				maxSpeed = speed;
			}
		}
		return maxSpeed;
	}


	


}

