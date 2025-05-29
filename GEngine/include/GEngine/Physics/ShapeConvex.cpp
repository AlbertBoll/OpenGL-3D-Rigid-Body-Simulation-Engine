#include "gepch.h"
#include "ShapeConvex.h"


namespace GEngine
{

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
	DistanceFromLine
	====================================================
	*/
	static float DistanceFromLine(const Vec3f& a, const Vec3f& b, const Vec3f& pt) {
		Vec3f ab = glm::normalize(b - a);

		Vec3f ray = pt - a;
		Vec3f projection = ab * glm::dot(ray, ab);	// project the ray onto ab
		Vec3f perpindicular = ray - projection;
		return glm::length(perpindicular); 
	}

	/*
	====================================================
	FindPointFurthestFromLine
	====================================================
	*/
	static Vec3f FindPointFurthestFromLine(const std::vector<Vec3f>& pts, const Vec3f& ptA, const Vec3f& ptB) {
		int maxIdx = 0;
		int num = pts.size();
		float maxDist = DistanceFromLine(ptA, ptB, pts[0]);
		for (int i = 1; i < num; i++) {
			float dist = DistanceFromLine(ptA, ptB, pts[i]);
			if (dist > maxDist) {
				maxDist = dist;
				maxIdx = i;
			}
		}
		return pts[maxIdx];
	}


	/*
	====================================================
	DistanceFromTriangle
	====================================================
	*/
	static float DistanceFromTriangle(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& pt) {
		Vec3f ab = b - a;
		Vec3f ac = c - a;
		Vec3f normal = glm::normalize(glm::cross(ab, ac));
		//normal.Normalize();

		Vec3f ray = pt - a;
		float dist = glm::dot(ray, normal);
		return dist;
	}

	/*
	====================================================
	FindPointFurthestFromTriangle
	====================================================
	*/
	static Vec3f FindPointFurthestFromTriangle(const std::vector<Vec3f>& pts, const Vec3f& ptA, const Vec3f& ptB, const Vec3f& ptC) {
		int maxIdx = 0;
		int num = pts.size();
		float maxDist = DistanceFromTriangle(ptA, ptB, ptC, pts[0]);
		for (int i = 1; i < num; i++) {
			float dist = DistanceFromTriangle(ptA, ptB, ptC, pts[i]);
			if (dist * dist > maxDist * maxDist) {
				maxDist = dist;
				maxIdx = i;
			}
		}
		return pts[maxIdx];
	}

	/*
	====================================================
	BuildTetrahedron
	====================================================
	*/
	static void BuildTetrahedron(const std::vector<Vec3f>& verts, std::vector<Vec3f>& hullPts, std::vector<tri_t>& hullTris) {
		hullPts.clear();
		hullTris.clear();

		Vec3f points[4];
		int num = verts.size();
		int idx = FindPointFurthestInDir(verts, Vec3f(1, 0, 0));
		points[0] = verts[idx];
		idx = FindPointFurthestInDir(verts, points[0] * -1.0f);
		points[1] = verts[idx];
		points[2] = FindPointFurthestFromLine(verts, points[0], points[1]);
		points[3] = FindPointFurthestFromTriangle(verts, points[0], points[1], points[2]);

		// This is important for making sure the ordering is CCW for all faces.
		float dist = DistanceFromTriangle(points[0], points[1], points[2], points[3]);
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
	static Vec3f CalculateCenterOfMass(const std::vector<Vec3f>& pts, const std::vector< tri_t >& tris) {
		const int numSamples = 100;

		Bounds bounds;
		bounds.Expand(pts.data(), pts.size());

		Vec3f cm(0.0f);
		const float dx = bounds.WidthX() / (float)numSamples;
		const float dy = bounds.WidthY() / (float)numSamples;
		const float dz = bounds.WidthZ() / (float)numSamples;

		int sampleCount = 0;
		for (float x = bounds.mins.x; x < bounds.maxs.x; x += dx) {
			for (float y = bounds.mins.y; y < bounds.maxs.y; y += dy) {
				for (float z = bounds.mins.z; z < bounds.maxs.z; z += dz) {
					Vec3f pt(x, y, z);

					if (IsExternal(pts, tris, pt)) {
						continue;
					}

					cm += pt;
					sampleCount++;
				}
			}
		}

		cm /= (float)sampleCount;
		return cm;
	}


	/*
	====================================================
	CalculateInertiaTensor
	====================================================
	*/
	Mat3 CalculateInertiaTensor(const std::vector<Vec3f>& pts, const std::vector<tri_t>& tris, const Vec3f& cm) {
		const int numSamples = 100;

		Bounds bounds;
		bounds.Expand(pts);

		Mat3 tensor{0.f};


		const float dx = bounds.WidthX() / (float)numSamples;
		const float dy = bounds.WidthY() / (float)numSamples;
		const float dz = bounds.WidthZ() / (float)numSamples;

		int sampleCount = 0;
		for (float x = bounds.mins.x; x < bounds.maxs.x; x += dx) {
			for (float y = bounds.mins.y; y < bounds.maxs.y; y += dy) {
				for (float z = bounds.mins.z; z < bounds.maxs.z; z += dz) {
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

		tensor *= 1.0f / (float)sampleCount;
		return tensor;
	}

	static void BuildConvexHull(const std::vector<Vec3f>& verts, std::vector<Vec3f>& hullPts, std::vector<tri_t>& hullTris) {
		if (verts.size() < 4) {
			return;
		}

		// Build a tetrahedron
		BuildTetrahedron(verts, hullPts, hullTris);

		ExpandConvexHull(hullPts, hullTris, verts);
	}


	void ShapeConvex::Build(const std::vector<Vec3f>& pts)
	{
		m_Points.clear();
		int num = pts.size();
		m_Points.reserve(num);
		for (int i = 0; i < num; i++) {
			m_Points.push_back(pts[i]);
		}

		// Expand into a convex hull
		std::vector< Vec3f > hullPoints;
		std::vector< tri_t > hullTriangles;
		BuildConvexHull(m_Points, hullPoints, hullTriangles);
		m_Points = hullPoints;

		// Expand the bounds
		m_Bounds.Clear();
		m_Bounds.Expand(m_Points);

		m_CenterOfMass = CalculateCenterOfMass(hullPoints, hullTriangles);

		m_InertiaTensor = CalculateInertiaTensor(hullPoints, hullTriangles, m_CenterOfMass);
	}

	Vec3f ShapeConvex::Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const
	{
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

		Vec3f norm = glm::normalize(dir);
		
		norm *= bias;

		return maxPt + norm;
	}

	Bounds ShapeConvex::GetBounds(const Vec3f& pos, const Quat& orient) const
	{
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

