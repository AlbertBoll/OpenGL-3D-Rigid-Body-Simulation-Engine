#include"gepch.h"
#include "GJK.h"
#include "Math/Math.h"
#include "PhysicsBody.h"
#include "Shape.h"
#include "glm/glm.hpp"

namespace GEngine
{
	


	Vec2f SignedVolume1D(const Vec3f& s1, const Vec3f& s2) {
		Vec3f ab = s2 - s1;	// Ray from a to b
		Vec3f ap = Vec3f(0.0f) - s1;	// Ray from a to origin
		Vec3f p0 = s1 + ab * glm::dot(ab, ap) / glm::length2(ab);	// projection of the origin onto the line

		// Choose the axis with the greatest difference/length
		int idx = 0;
		float mu_max = 0;
		for (int i = 0; i < 3; i++) {
			float mu = s2[i] - s1[i];
			if (mu * mu > mu_max * mu_max) {
				mu_max = mu;
				idx = i;
			}
		}

		// Project the simplex points and projected origin onto the axis with greatest length
		const float a = s1[idx];
		const float b = s2[idx];
		const float p = p0[idx];

		// Get the signed distance from a to p and from p to b
		const float C1 = p - a;
		const float C2 = b - p;

		// if p is between [a,b]
		if ((p > a && p < b) || (p > b && p < a)) {
			Vec2f lambdas;
			lambdas[0] = C2 / mu_max;
			lambdas[1] = C1 / mu_max;
			return lambdas;
		}

		// if p is on the far side of a
		if ((a <= b && p <= a) || (a >= b && p >= a)) {
			return Vec2f(1.0f, 0.0f);
		}

		// p must be on the far side of b
		return Vec2f(0.0f, 1.0f);
	}


	/*
	================================
	CompareSigns
	================================
	*/
	int CompareSigns(float a, float b) {
		if (a > 0.0f && b > 0.0f) {
			return 1;
		}
		if (a < 0.0f && b < 0.0f) {
			return 1;
		}
		return 0;
	}

	/*
	================================
	SignedVolume2D
	================================
	*/
	Vec3f SignedVolume2D(const Vec3f& s1, const Vec3f& s2, const Vec3f& s3) {
		Vec3f normal = glm::cross(s2 - s1, s3 - s1);
		Vec3f p0 = normal * glm::dot(s1, normal) / glm::length2(normal);

		// Find the axis with the greatest projected area
		int idx = 0;
		float area_max = 0;
		for (int i = 0; i < 3; i++) 
		{
			int j = (i + 1) % 3;
			int k = (i + 2) % 3;

			Vec2f a = Vec2f(s1[j], s1[k]);
			Vec2f b = Vec2f(s2[j], s2[k]);
			Vec2f c = Vec2f(s3[j], s3[k]);
			Vec2f ab = b - a;
			Vec2f ac = c - a;

			float area = ab.x * ac.y - ab.y * ac.x;
			if (area * area > area_max * area_max) {
				idx = i;
				area_max = area;
			}
		}

		// Project onto the appropriate axis
		int x = (idx + 1) % 3;
		int y = (idx + 2) % 3;
		Vec2f s[3];
		s[0] = Vec2f(s1[x], s1[y]);
		s[1] = Vec2f(s2[x], s2[y]);
		s[2] = Vec2f(s3[x], s3[y]);
		Vec2f p = Vec2f(p0[x], p0[y]);

		// Get the sub-areas of the triangles formed from the projected origin and the edges
		Vec3f areas;
		for (int i = 0; i < 3; i++) {
			int j = (i + 1) % 3;
			int k = (i + 2) % 3;

			Vec2f a = p;
			Vec2f b = s[j];
			Vec2f c = s[k];
			Vec2f ab = b - a;
			Vec2f ac = c - a;

			areas[i] = ab.x * ac.y - ab.y * ac.x;
		}

		// If the projected origin is inside the triangle, then return the barycentric points
		if (CompareSigns(area_max, areas[0]) > 0 && CompareSigns(area_max, areas[1]) > 0 && CompareSigns(area_max, areas[2]) > 0) {
			Vec3f lambdas = areas / area_max;
			return lambdas;
		}

		// If we make it here, then we need to project onto the edges and determine the closest point
		float dist = 1e10;
		Vec3f lambdas = Vec3f(1, 0, 0);
		for (int i = 0; i < 3; i++) {
			int k = (i + 1) % 3;
			int l = (i + 2) % 3;

			Vec3f edgesPts[3];
			edgesPts[0] = s1;
			edgesPts[1] = s2;
			edgesPts[2] = s3;

			Vec2f lambdaEdge = SignedVolume1D(edgesPts[k], edgesPts[l]);
			Vec3f pt = edgesPts[k] * lambdaEdge[0] + edgesPts[l] * lambdaEdge[1];
			if (glm::length2(pt) < dist) {
				dist = glm::length2(pt);
				lambdas[i] = 0;
				lambdas[k] = lambdaEdge[0];
				lambdas[l] = lambdaEdge[1];
			}
		}

		return lambdas;
	}


	Vec4f SignedVolume3D(const Vec3f& s1, const Vec3f& s2, const Vec3f& s3, const Vec3f& s4)
	{

		Mat4 M;

		M[0] = Vec4f(s1.x, s1.y, s1.z, 1.f);
		M[1] = Vec4f(s2.x, s2.y, s2.z, 1.f);
		M[2] = Vec4f(s3.x, s3.y, s3.z, 1.f);
		M[3] = Vec4f(s4.x, s4.y, s4.z, 1.f);

		using namespace Math;
		Vec4f C4;
		C4[0] = Cofactor(M, 3, 0);
		C4[1] = Cofactor(M, 3, 1);
		C4[2] = Cofactor(M, 3, 2);
		C4[3] = Cofactor(M, 3, 3);

		const float detM = C4[0] + C4[1] + C4[2] + C4[3];
		// If the barycentric coordinates put the origin inside the simplex, then return them
		if (CompareSigns(detM, C4[0]) > 0 && CompareSigns(detM, C4[1]) > 0 && CompareSigns(detM, C4[2]) > 0 && CompareSigns(detM, C4[3]) > 0) {
			Vec4f lambdas = C4 * (1.0f / detM);
			return lambdas;
		}

		// If we get here, then we need to project the origin onto the faces and determine the closest one
		Vec4f lambdas;
		float dist = 1e10;
		for (int i = 0; i < 4; i++) {
			int j = (i + 1) % 4;
			int k = (i + 2) % 4;

			Vec3f facePts[4];
			facePts[0] = s1;
			facePts[1] = s2;
			facePts[2] = s3;
			facePts[3] = s4;

			Vec3f lambdasFace = SignedVolume2D(facePts[i], facePts[j], facePts[k]);
			Vec3f pt = facePts[i] * lambdasFace[0] + facePts[j] * lambdasFace[1] + facePts[k] * lambdasFace[2];
			if (glm::length2(pt) < dist) {
				dist = glm::length2(pt);
				lambdas = Vec4f{ 0.f };
				lambdas[i] = lambdasFace[0];
				lambdas[j] = lambdasFace[1];
				lambdas[k] = lambdasFace[2];
			}
		}

		return lambdas;

	}




	/*
	================================
	TestSignedVolumeProjection
	================================
	*/
	void TestSignedVolumeProjection() {
		const Vec3f orgPts[4] = {
			Vec3f(0, 0, 0),
			Vec3f(1, 0, 0),
			Vec3f(0, 1, 0),
			Vec3f(0, 0, 1),
		};
		Vec3f pts[4];
		Vec4f lambdas;
		Vec3f v;

		for (int i = 0; i < 4; i++) {
			pts[i] = orgPts[i] + Vec3f(1, 1, 1);
		}
		lambdas = SignedVolume3D(pts[0], pts[1], pts[2], pts[3]);
		v = Vec3f{ 0.f };
		for (int i = 0; i < 4; i++) {
			v += pts[i] * lambdas[i];
		}
		printf("lambdas: %.3f %.3f %.3f %.3f        v: %.3f %.3f %.3f\n",
			lambdas.x, lambdas.y, lambdas.z, lambdas.w,
			v.x, v.y, v.z
		);

		for (int i = 0; i < 4; i++) {
			pts[i] = orgPts[i] + Vec3f(-1, -1, -1) * 0.25f;
		}
		lambdas = SignedVolume3D(pts[0], pts[1], pts[2], pts[3]);
		v = Vec3f{ 0.f };
		for (int i = 0; i < 4; i++) {
			v += pts[i] * lambdas[i];
		}
		printf("lambdas: %.3f %.3f %.3f %.3f        v: %.3f %.3f %.3f\n",
			lambdas.x, lambdas.y, lambdas.z, lambdas.w,
			v.x, v.y, v.z
		);

		for (int i = 0; i < 4; i++) {
			pts[i] = orgPts[i] + Vec3f(-1, -1, -1);
		}
		lambdas = SignedVolume3D(pts[0], pts[1], pts[2], pts[3]);
		v = Vec3f{ 0.f };
		for (int i = 0; i < 4; i++) {
			v += pts[i] * lambdas[i];
		}
		printf("lambdas: %.3f %.3f %.3f %.3f        v: %.3f %.3f %.3f\n",
			lambdas.x, lambdas.y, lambdas.z, lambdas.w,
			v.x, v.y, v.z
		);

		for (int i = 0; i < 4; i++) {
			pts[i] = orgPts[i] + Vec3f(1, 1, -0.5f);
		}
		lambdas = SignedVolume3D(pts[0], pts[1], pts[2], pts[3]);
		v = Vec3f{ 0.f };
		for (int i = 0; i < 4; i++) {
			v += pts[i] * lambdas[i];
		}
		printf("lambdas: %.3f %.3f %.3f %.3f        v: %.3f %.3f %.3f\n",
			lambdas.x, lambdas.y, lambdas.z, lambdas.w,
			v.x, v.y, v.z
		);

		pts[0] = Vec3f(51.1996613f, 26.1989613f, 1.91339576f);
		pts[1] = Vec3f(-51.0567360f, -26.0565681f, -0.436143428f);
		pts[2] = Vec3f(50.8978920f, -24.1035538f, -1.04042661f);
		pts[3] = Vec3f(-49.1021080f, 25.8964462f, -1.04042661f);
		lambdas = SignedVolume3D(pts[0], pts[1], pts[2], pts[3]);
		v = Vec3f{ 0.f };
		for (int i = 0; i < 4; i++) {
			v += pts[i] * lambdas[i];
		}
		printf("lambdas: %.3f %.3f %.3f %.3f        v: %.3f %.3f %.3f\n",
			lambdas.x, lambdas.y, lambdas.z, lambdas.w,
			v.x, v.y, v.z
		);
	}


	/*
	================================================================================================
	
	Gilbert Johnson Keerthi
	
	================================================================================================
	*/

	struct point_t 
	{

		Vec3f xyz;	// The point on the minkowski sum
		Vec3f ptA;	// The point on bodyA
		Vec3f ptB;	// The point on bodyB

		point_t() : xyz(0.0f), ptA(0.0f), ptB(0.0f) {}

		const point_t& operator = (const point_t& rhs) {
			xyz = rhs.xyz;
			ptA = rhs.ptA;
			ptB = rhs.ptB;
			return *this;
		}

		bool operator == (const point_t& rhs) const {
			return ((ptA == rhs.ptA) && (ptB == rhs.ptB) && (xyz == rhs.xyz));
		}

	};

	struct tri_t 
	{
		int a;
		int b;
		int c;
	};

	struct edge_t 
	{
		int a;
		int b;

		bool operator == (const edge_t& rhs) const
		{
			return ((a == rhs.a && b == rhs.b) || (a == rhs.b && b == rhs.a));
		}
	};

	float EPA_Expand(const RigidBody3D* bodyA, const RigidBody3D* bodyB, const float bias, const point_t simplexPoints[4], Vec3f& ptOnA, Vec3f& ptOnB);

	/*
	================================
	Support
	================================
	*/
	point_t Support(const RigidBody3D* bodyA, const RigidBody3D* bodyB, Vec3f dir, const float bias) 
	{
		dir = glm::normalize(dir);

		point_t point;

		// Find the point in A furthest in direction
		point.ptA = bodyA->m_Shape->Support(dir, bodyA->m_Position, bodyA->m_Orientation, bias);

		dir *= -1.0f;

		// Find the point in B furthest in the opposite direction
		point.ptB = bodyB->m_Shape->Support(dir, bodyB->m_Position, bodyB->m_Orientation, bias);

		// Return the point, in the minkowski sum, furthest in the direction
		point.xyz = point.ptA - point.ptB;
		return point;
	}


	/*
	================================
	SimplexSignedVolumes
	
	Projects the origin onto the simplex to acquire the new search direction,
	also checks if the origin is "inside" the simplex.
	================================
	*/
	bool SimplexSignedVolumes(point_t* pts, const int num, Vec3f& newDir, Vec4f& lambdasOut) {
		const float epsilonf = 0.0001f * 0.0001f;

		lambdasOut = Vec4f{ 0.f };

		bool doesIntersect = false;
		switch (num) {
		default:
		case 2: {
			Vec2f lambdas = SignedVolume1D(pts[0].xyz, pts[1].xyz);
			Vec3f v(0.0f);
			for (int i = 0; i < 2; i++) {
				v += pts[i].xyz * lambdas[i];
			}
			newDir = v * -1.0f;
			doesIntersect = (glm::length2(v) < epsilonf);
			lambdasOut[0] = lambdas[0];
			lambdasOut[1] = lambdas[1];
		} break;
		case 3: {
			Vec3f lambdas = SignedVolume2D(pts[0].xyz, pts[1].xyz, pts[2].xyz);
			Vec3f v(0.0f);
			for (int i = 0; i < 3; i++) {
				v += pts[i].xyz * lambdas[i];
			}
			newDir = v * -1.0f;
			doesIntersect = (glm::length2(v) < epsilonf);
			lambdasOut[0] = lambdas[0];
			lambdasOut[1] = lambdas[1];
			lambdasOut[2] = lambdas[2];
		} break;
		case 4: {
			Vec4f lambdas = SignedVolume3D(pts[0].xyz, pts[1].xyz, pts[2].xyz, pts[3].xyz);
			Vec3f v(0.0f);
			for (int i = 0; i < 4; i++) {
				v += pts[i].xyz * lambdas[i];
			}
			newDir = v * -1.0f;
			doesIntersect = (glm::length2(v) < epsilonf);
			lambdasOut[0] = lambdas[0];
			lambdasOut[1] = lambdas[1];
			lambdasOut[2] = lambdas[2];
			lambdasOut[3] = lambdas[3];
		} break;
		};

		return doesIntersect;
	}


	/*
	================================
	HasPoint
	
	Checks whether the new point already exists in the simplex
	================================
	*/
	bool HasPoint(const point_t simplexPoints[4], const point_t& newPt) {
		const float precision = 1e-6f;

		for (int i = 0; i < 4; i++) {
			Vec3f delta = simplexPoints[i].xyz - newPt.xyz;
			if (glm::length2(delta) < precision * precision) 
			{
				return true;
			}
		}
		return false;
	}

	/*
	================================
	SortValids
	
	Sorts the valid support points to the beginning of the array
	================================
	*/
	void SortValids(point_t simplexPoints[4], Vec4f& lambdas) {
		bool valids[4];
		for (int i = 0; i < 4; i++) {
			valids[i] = true;
			//if (lambdas[i] == 0.0f) {
			if (glm::abs(lambdas[i]) < 0.00001f) {
				valids[i] = false;
			}
		}

		Vec4f validLambdas(0.0f);
		int validCount = 0;
		point_t validPts[4];
		memset(validPts, 0, sizeof(point_t) * 4);
		for (int i = 0; i < 4; i++) {
			if (valids[i]) {
				validPts[validCount] = simplexPoints[i];
				validLambdas[validCount] = lambdas[i];
				validCount++;
			}
		}

		// Copy the valids back into simplexPoints
		for (int i = 0; i < 4; i++) {
			simplexPoints[i] = validPts[i];
			lambdas[i] = validLambdas[i];
		}
	}


	/*
	================================
	NumValids
	================================
	*/
	static int NumValids(const Vec4f& lambdas) {
		int num = 0;
		for (int i = 0; i < 4; i++) {
			//if (0.0f != lambdas[i]) {
			if (glm::abs(lambdas[i]) > 0.00001f) {
				num++;
			}
		}
		return num;
	}






	bool GJK_DoesIntersect(const RigidBody3D* bodyA, const RigidBody3D* bodyB)
	{
		const Vec3f origin(0.0f);

		int numPts = 1;
		point_t simplexPoints[4];
		simplexPoints[0] = Support(bodyA, bodyB, Vec3f(1, 1, 1), 0.0f);

		float closestDist = 1e10f;
		bool doesContainOrigin = false;
		Vec3f newDir = simplexPoints[0].xyz * -1.0f;
		do {
			// Get the new point to check on
			point_t newPt = Support(bodyA, bodyB, newDir, 0.0f);

			// If the new point is the same as a previous point, then we can't expand any further
			if (HasPoint(simplexPoints, newPt)) {
				break;
			}

			simplexPoints[numPts] = newPt;
			numPts++;

			// If this new point hasn't moved passed the origin, then the
			// origin cannot be in the set. And therefore there is no collision.
			float dotdot = glm::dot(newDir, newPt.xyz - origin);
			if (dotdot < 0.0f) {
				break;
			}

			Vec4f lambdas;
			doesContainOrigin = SimplexSignedVolumes(simplexPoints, numPts, newDir, lambdas);
			if (doesContainOrigin) {
				break;
			}

			// Check that the new projection of the origin onto the simplex is closer than the previous
			float dist = glm::length2(newDir);
			if (dist >= closestDist) {
				break;
			}
			closestDist = dist;

			// Use the lambdas that support the new search direction, and invalidate any points that don't support it
			SortValids(simplexPoints, lambdas);
			numPts = NumValids(lambdas);
			doesContainOrigin = (4 == numPts);
		} while (!doesContainOrigin);

		return doesContainOrigin;

	}



	bool GJK_DoesIntersect(const RigidBody3D* bodyA, const RigidBody3D* bodyB, const float bias, Vec3f& ptOnA, Vec3f& ptOnB)
	{
		
		const Vec3f origin(0.0f);

		int numPts = 1;
		point_t simplexPoints[4];
		simplexPoints[0] = Support(bodyA, bodyB, Vec3f(1, 1, 1), 0.0f);

		float closestDist = 1e10f;
		bool doesContainOrigin = false;
		Vec3f newDir = simplexPoints[0].xyz * -1.0f;
		do {
			// Get the new point to check on
			point_t newPt = Support(bodyA, bodyB, newDir, 0.0f);

			// If the new point is the same as a previous point, then we can't expand any further
			if (HasPoint(simplexPoints, newPt)) {
				break;
			}

			simplexPoints[numPts] = newPt;
			numPts++;

			// If this new point hasn't moved passed the origin, then the
			// origin cannot be in the set. And therefore there is no collision.
			float dotdot = glm::dot(newDir, newPt.xyz - origin);
			if (dotdot < 0.0f) {
				break;
			}

			Vec4f lambdas;
			doesContainOrigin = SimplexSignedVolumes(simplexPoints, numPts, newDir, lambdas);
			if (doesContainOrigin) {
				break;
			}

			// Check that the new projection of the origin onto the simplex is closer than the previous
			float dist = glm::length2(newDir); 
			if (dist >= closestDist) {
				break;
			}
			closestDist = dist;

			// Use the lambdas that support the new search direction, and invalidate any points that don't support it
			SortValids(simplexPoints, lambdas);
			numPts = NumValids(lambdas);
			doesContainOrigin = (4 == numPts);
		} while (!doesContainOrigin);

		if (!doesContainOrigin) {
			return false;
		}

		//
		//	Check that we have a 3-simplex (EPA expects a tetrahedron)
		//
		if (1 == numPts) {
			Vec3f searchDir = simplexPoints[0].xyz * -1.0f;
			point_t newPt = Support(bodyA, bodyB, searchDir, 0.0f);
			simplexPoints[numPts] = newPt;
			numPts++;
		}
		if (2 == numPts) {
			Vec3f ab = simplexPoints[1].xyz - simplexPoints[0].xyz;
			Vec3f u, v;
			GetOrtho(ab, u, v);

			Vec3f newDir = u;
			point_t newPt = Support(bodyA, bodyB, newDir, 0.0f);
			simplexPoints[numPts] = newPt;
			numPts++;
		}
		if (3 == numPts) {
			Vec3f ab = simplexPoints[1].xyz - simplexPoints[0].xyz;
			Vec3f ac = simplexPoints[2].xyz - simplexPoints[0].xyz;
			Vec3f norm = glm::cross(ab, ac);

			Vec3f newDir = norm;
			point_t newPt = Support(bodyA, bodyB, newDir, 0.0f);
			simplexPoints[numPts] = newPt;
			numPts++;
		}

		//
		// Expand the simplex by the bias amount
		//

		// Get the center point of the simplex
		Vec3f avg = Vec3f(0, 0, 0);
		for (int i = 0; i < 4; i++) {
			avg += simplexPoints[i].xyz;
		}
		avg *= 0.25f;

		// Now expand the simplex by the bias amount
		for (int i = 0; i < numPts; i++) {
			point_t& pt = simplexPoints[i];

			Vec3f dir = pt.xyz - avg;	// ray from "center" to witness point
			dir = glm::normalize(dir);
			
			pt.ptA += dir * bias;
			pt.ptB -= dir * bias;
			pt.xyz = pt.ptA - pt.ptB;
		}

		//
		// Perform EPA expansion of the simplex to find the closest face on the CSO
		//
		EPA_Expand(bodyA, bodyB, bias, simplexPoints, ptOnA, ptOnB);

		return true;

	}




	void GJK_ClosestPoints(const RigidBody3D* bodyA, const RigidBody3D* bodyB, Vec3f& ptOnA, Vec3f& ptOnB)
	{
		
		const Vec3f origin(0.0f);

		float closestDist = 1e10f;
		const float bias = 0.0f;

		int numPts = 1;
		point_t simplexPoints[4];
		simplexPoints[0] = Support(bodyA, bodyB, Vec3f(1, 1, 1), bias);

		Vec4f lambdas = Vec4f(1, 0, 0, 0);
		Vec3f newDir = simplexPoints[0].xyz * -1.0f;
		do {
			// Get the new point to check on
			point_t newPt = Support(bodyA, bodyB, newDir, bias);

			// If the new point is the same as a previous point, then we can't expand any further
			if (HasPoint(simplexPoints, newPt)) {
				break;
			}

			// Add point and get new search direction
			simplexPoints[numPts] = newPt;
			numPts++;

			SimplexSignedVolumes(simplexPoints, numPts, newDir, lambdas);
			SortValids(simplexPoints, lambdas);
			numPts = NumValids(lambdas);

			// Check that the new projection of the origin onto the simplex is closer than the previous
			float dist = glm::length2(newDir); 
			if (dist >= closestDist) {
				break;
			}
			closestDist = dist;
		} while (numPts < 4);


		ptOnA = Vec3f{0.f};
		ptOnB = Vec3f{0.f};

		for (int i = 0; i < 4; i++) {
			ptOnA += simplexPoints[i].ptA * lambdas[i];
			ptOnB += simplexPoints[i].ptB * lambdas[i];
		}

	}



	/*
	================================================================================================
	
	Expanding Polytope Algorithm
	
	================================================================================================
	*/
	
	/*
	================================
	BarycentricCoordinates
	
	This borrows our signed volume code to perform the barycentric coordinates.
	================================
	*/
	Vec3f BarycentricCoordinates(Vec3f s1, Vec3f s2, Vec3f s3, const Vec3f& pt) {
		s1 = s1 - pt;
		s2 = s2 - pt;
		s3 = s3 - pt;

		Vec3f normal = glm::cross(s2 - s1, s3 - s1);
		Vec3f p0 = normal * glm::dot(s1, normal) / glm::length2(normal);

		// Find the axis with the greatest projected area
		int idx = 0;
		float area_max = 0;
		for (int i = 0; i < 3; i++) {
			int j = (i + 1) % 3;
			int k = (i + 2) % 3;

			Vec2f a = Vec2f(s1[j], s1[k]);
			Vec2f b = Vec2f(s2[j], s2[k]);
			Vec2f c = Vec2f(s3[j], s3[k]);
			Vec2f ab = b - a;
			Vec2f ac = c - a;

			float area = ab.x * ac.y - ab.y * ac.x;
			if (area * area > area_max * area_max) {
				idx = i;
				area_max = area;
			}
		}

		// Project onto the appropriate axis
		int x = (idx + 1) % 3;
		int y = (idx + 2) % 3;
		Vec2f s[3];
		s[0] = Vec2f(s1[x], s1[y]);
		s[1] = Vec2f(s2[x], s2[y]);
		s[2] = Vec2f(s3[x], s3[y]);
		Vec2f p = Vec2f(p0[x], p0[y]);

		// Get the sub-areas of the triangles formed from the projected origin and the edges
		Vec3f areas;
		for (int i = 0; i < 3; i++) {
			int j = (i + 1) % 3;
			int k = (i + 2) % 3;

			Vec2f a = p;
			Vec2f b = s[j];
			Vec2f c = s[k];
			Vec2f ab = b - a;
			Vec2f ac = c - a;

			areas[i] = ab.x * ac.y - ab.y * ac.x;
		}

		Vec3f lambdas = areas / area_max;

		if(!IsValid(lambdas))
		{
			lambdas = Vec3f(1, 0, 0);
		}
		return lambdas;
	}


	/*
	================================
	NormalDirection
	================================
	*/

	Vec3f NormalDirection(const tri_t& tri, const std::vector< point_t >& points) {
		const Vec3f& a = points[tri.a].xyz;
		const Vec3f& b = points[tri.b].xyz;
		const Vec3f& c = points[tri.c].xyz;

		Vec3f ab = b - a;
		Vec3f ac = c - a;
		Vec3f normal = glm::normalize(glm::cross(ab, ac));
		return normal;
	}



	/*
	================================
	SignedDistanceToTriangle
	================================
	*/

	float SignedDistanceToTriangle(const tri_t& tri, const Vec3f& pt, const std::vector< point_t >& points) {
		const Vec3f normal = NormalDirection(tri, points);
		const Vec3f& a = points[tri.a].xyz;
		const Vec3f a2pt = pt - a;
		const float dist = glm::dot(normal, a2pt);
		return dist;
	}



	/*
	================================
	ClosestTriangle
	================================
	*/

	int ClosestTriangle(const std::vector< tri_t >& triangles, const std::vector< point_t >& points) {
		float minDistSqr = 1e10;

		int idx = -1;
		for (int i = 0; i < triangles.size(); i++) {
			const tri_t& tri = triangles[i];

			float dist = SignedDistanceToTriangle(tri, Vec3f(0.0f), points);
			float distSqr = dist * dist;
			if (distSqr < minDistSqr) {
				idx = i;
				minDistSqr = distSqr;
			}
		}

		return idx;
	}


	/*
	================================
	HasPoint
	================================
	*/
	bool HasPoint(const Vec3f& w, const std::vector< tri_t > triangles, const std::vector< point_t >& points) {
		const float epsilons = 0.001f * 0.001f;
		Vec3f delta;

		for (int i = 0; i < triangles.size(); i++) {
			const tri_t& tri = triangles[i];

			delta = w - points[tri.a].xyz;
			if (glm::length2(delta) < epsilons) {
				return true;
			}
			delta = w - points[tri.b].xyz;
			if (glm::length2(delta) < epsilons) {
				return true;
			}
			delta = w - points[tri.c].xyz;
			if (glm::length2(delta) < epsilons) {
				return true;
			}
		}
		return false;
	}



	/*
	================================
	RemoveTrianglesFacingPoint
	================================
	*/
	int RemoveTrianglesFacingPoint(const Vec3f& pt, std::vector< tri_t >& triangles, const std::vector< point_t >& points) {
		int numRemoved = 0;
		for (int i = 0; i < triangles.size(); i++) {
			const tri_t& tri = triangles[i];

			float dist = SignedDistanceToTriangle(tri, pt, points);
			if (dist > 0.0f) {
				// This triangle faces the point.  Remove it.
				triangles.erase(triangles.begin() + i);
				i--;
				numRemoved++;
			}
		}
		return numRemoved;
	}



	/*
	================================
	FindDanglingEdges
	================================
	*/
	void FindDanglingEdges(std::vector< edge_t >& danglingEdges, const std::vector< tri_t >& triangles) {
		danglingEdges.clear();

		for (int i = 0; i < triangles.size(); i++) {
			const tri_t& tri = triangles[i];

			edge_t edges[3];
			edges[0].a = tri.a;
			edges[0].b = tri.b;

			edges[1].a = tri.b;
			edges[1].b = tri.c;

			edges[2].a = tri.c;
			edges[2].b = tri.a;

			int counts[3];
			counts[0] = 0;
			counts[1] = 0;
			counts[2] = 0;

			for (int j = 0; j < triangles.size(); j++) {
				if (j == i) {
					continue;
				}

				const tri_t& tri2 = triangles[j];

				edge_t edges2[3];
				edges2[0].a = tri2.a;
				edges2[0].b = tri2.b;

				edges2[1].a = tri2.b;
				edges2[1].b = tri2.c;

				edges2[2].a = tri2.c;
				edges2[2].b = tri2.a;

				for (int k = 0; k < 3; k++) {
					if (edges[k] == edges2[0]) {
						counts[k]++;
					}
					if (edges[k] == edges2[1]) {
						counts[k]++;
					}
					if (edges[k] == edges2[2]) {
						counts[k]++;
					}
				}
			}

			// An edge that isn't shared, is dangling 
			for (int k = 0; k < 3; k++) {
				if (0 == counts[k]) {
					danglingEdges.push_back(edges[k]);
				}
			}
		}
	}



	float EPA_Expand(const RigidBody3D* bodyA, const RigidBody3D* bodyB, const float bias, const point_t simplexPoints[4], Vec3f& ptOnA, Vec3f& ptOnB) {
		std::vector< point_t > points;
		std::vector< tri_t > triangles;
		std::vector< edge_t > danglingEdges;

		Vec3f center(0.0f);
		for (int i = 0; i < 4; i++) {
			points.push_back(simplexPoints[i]);
			center += simplexPoints[i].xyz;
		}
		center *= 0.25f;

		// Build the triangles
		for (int i = 0; i < 4; i++) {
			int j = (i + 1) % 4;
			int k = (i + 2) % 4;
			tri_t tri;
			tri.a = i;
			tri.b = j;
			tri.c = k;

			int unusedPt = (i + 3) % 4;
			float dist = SignedDistanceToTriangle(tri, points[unusedPt].xyz, points);

			// The unused point is always on the negative/inside of the triangle.. make sure the normal points away
			if (dist > 0.0f) {
				std::swap(tri.a, tri.b);
			}

			triangles.push_back(tri);
		}

		//
		//	Expand the simplex to find the closest face of the CSO to the origin
		//
		while (1) {
			const int idx = ClosestTriangle(triangles, points);
			Vec3f normal = NormalDirection(triangles[idx], points);

			const point_t newPt = Support(bodyA, bodyB, normal, bias);

			// if w already exists, then just stop
			// because it means we can't expand any further
			if (HasPoint(newPt.xyz, triangles, points)) {
				break;
			}

			float dist = SignedDistanceToTriangle(triangles[idx], newPt.xyz, points);
			if (dist <= 0.0f) {
				break;	// can't expand
			}

			const int newIdx = (int)points.size();
			points.push_back(newPt);

			// Remove Triangles that face this point
			int numRemoved = RemoveTrianglesFacingPoint(newPt.xyz, triangles, points);
			if (0 == numRemoved) {
				break;
			}

			// Find Dangling Edges
			danglingEdges.clear();
			FindDanglingEdges(danglingEdges, triangles);
			if (0 == danglingEdges.size()) {
				break;
			}

			// In theory the edges should be a proper CCW order
			// So we only need to add the new point as 'a' in order
			// to create new triangles that face away from origin
			for (int i = 0; i < danglingEdges.size(); i++) {
				const edge_t& edge = danglingEdges[i];

				tri_t triangle;
				triangle.a = newIdx;
				triangle.b = edge.b;
				triangle.c = edge.a;

				// Make sure it's oriented properly
				float dist = SignedDistanceToTriangle(triangle, center, points);
				if (dist > 0.0f) {
					std::swap(triangle.b, triangle.c);
				}

				triangles.push_back(triangle);
			}
		}

		// Get the projection of the origin on the closest triangle
		const int idx = ClosestTriangle(triangles, points);
		const tri_t& tri = triangles[idx];
		Vec3f ptA_w = points[tri.a].xyz;
		Vec3f ptB_w = points[tri.b].xyz;
		Vec3f ptC_w = points[tri.c].xyz;
		Vec3f lambdas = BarycentricCoordinates(ptA_w, ptB_w, ptC_w, Vec3f(0.0f));

		// Get the point on shape A
		Vec3f ptA_a = points[tri.a].ptA;
		Vec3f ptB_a = points[tri.b].ptA;
		Vec3f ptC_a = points[tri.c].ptA;
		ptOnA = ptA_a * lambdas[0] + ptB_a * lambdas[1] + ptC_a * lambdas[2];

		// Get the point on shape B
		Vec3f ptA_b = points[tri.a].ptB;
		Vec3f ptB_b = points[tri.b].ptB;
		Vec3f ptC_b = points[tri.c].ptB;
		ptOnB = ptA_b * lambdas[0] + ptB_b * lambdas[1] + ptC_b * lambdas[2];

		// Return the penetration distance
		Vec3f delta = ptOnB - ptOnA;
		return glm::length(delta);
	}



}