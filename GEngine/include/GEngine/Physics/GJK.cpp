#include"gepch.h"
#include "GJK.h"
#include "PhysicsProfile.h"
#include "Math/Math.h"
#include "PhysicsBody.h"
#include "Shape.h"
#include "glm/glm.hpp"

#include <array>

namespace GEngine
{
	


	Vec2f SignedVolume1D(const Vec3f& s1, const Vec3f& s2) {
		Vec3f ab = s2 - s1;	// Ray from a to b
		Vec3f ap = Vec3f(0.0f) - s1;	// Ray from a to origin
		const float abLengthSquared = glm::length2(ab);
		if (!Math::IsFinite(abLengthSquared) || abLengthSquared <= Math::NumericalEpsilonSquared)
		{
			return Vec2f(1.0f, 0.0f);
		}
		Vec3f p0 = s1 + ab * glm::dot(ab, ap) / abLengthSquared;	// projection of the origin onto the line

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
		if (!Math::IsFinite(mu_max) || std::fabs(mu_max) <= Math::NumericalEpsilon)
		{
			return Vec2f(1.0f, 0.0f);
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
		const float normalLengthSquared = glm::length2(normal);
		Vec3f p0(0.0f);
		if (Math::IsFinite(normalLengthSquared) && normalLengthSquared > Math::NumericalEpsilonSquared)
		{
			p0 = normal * glm::dot(s1, normal) / normalLengthSquared;
		}

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
		if (Math::IsFinite(area_max) && std::fabs(area_max) > Math::NumericalEpsilon &&
			CompareSigns(area_max, areas[0]) > 0 && CompareSigns(area_max, areas[1]) > 0 && CompareSigns(area_max, areas[2]) > 0) {
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
		if (Math::IsFinite(detM) && std::fabs(detM) > Math::NumericalEpsilon &&
			CompareSigns(detM, C4[0]) > 0 && CompareSigns(detM, C4[1]) > 0 && CompareSigns(detM, C4[2]) > 0 && CompareSigns(detM, C4[3]) > 0) {
			Vec4f lambdas = C4 * (1.0f / detM);
			return lambdas;
		}

		// If we get here, then we need to project the origin onto the faces and determine the closest one
		Vec4f lambdas(1.0f, 0.0f, 0.0f, 0.0f);
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

	bool EPA_Expand(const RigidBody3D* bodyA, const RigidBody3D* bodyB, float bias,
		const point_t simplexPoints[4], Vec3f& ptOnA, Vec3f& ptOnB);

	bool IsValidSupportPoint(const point_t& point)
	{
		return Math::IsFinite(point.xyz) && Math::IsFinite(point.ptA) && Math::IsFinite(point.ptB);
	}

	bool HasValidCollisionShapes(const RigidBody3D* bodyA, const RigidBody3D* bodyB)
	{
		return bodyA && bodyB && bodyA->m_Shape && bodyB->m_Shape &&
			bodyA->m_Shape->IsValid() && bodyB->m_Shape->IsValid() &&
			Math::IsFinite(bodyA->m_Position) && Math::IsFinite(bodyB->m_Position) &&
			Math::IsFinite(bodyA->m_Orientation) && Math::IsFinite(bodyB->m_Orientation);
	}

	/*
	================================
	Support
	================================
	*/
	point_t Support(const RigidBody3D* bodyA, const RigidBody3D* bodyB, Vec3f dir, const float bias) 
	{
		GE_PHYSICS_PROFILE_SCOPE(supportTimeNs);
		GE_PHYSICS_PROFILE_ADD(supportCallCount, 1);
		dir = Math::NormalizeOr(dir);

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
	bool HasPoint(const point_t simplexPoints[4], int numPts, const point_t& newPt) {
		const float precision = 1e-6f;

		if (numPts < 0 || numPts > 4 || !IsValidSupportPoint(newPt)) {
			return false;
		}

		for (int i = 0; i < numPts; i++) {
			Vec3f delta = simplexPoints[i].xyz - newPt.xyz;
			const float distanceSquared = glm::length2(delta);
			if (Math::IsFinite(distanceSquared) && distanceSquared < precision * precision)
			{
				return true;
			}
		}
		return false;
	}

	bool IncreasesSimplexDimension(const point_t simplexPoints[4], int numPts, const point_t& candidate)
	{
		if (!IsValidSupportPoint(candidate) || numPts < 1 || numPts > 3) {
			return false;
		}

		const Vec3f fromA = candidate.xyz - simplexPoints[0].xyz;
		const float fromALengthSquared = glm::length2(fromA);
		if (!Math::IsFinite(fromALengthSquared) || fromALengthSquared <= Math::NumericalEpsilonSquared) {
			return false;
		}
		if (numPts == 1) {
			return true;
		}

		const Vec3f ab = simplexPoints[1].xyz - simplexPoints[0].xyz;
		const float abLengthSquared = glm::length2(ab);
		const Vec3f normal = glm::cross(ab, fromA);
		const float normalLengthSquared = glm::length2(normal);
		if (!Math::IsFinite(abLengthSquared) || !Math::IsFinite(normalLengthSquared) ||
			normalLengthSquared <= Math::NumericalEpsilonSquared * abLengthSquared * fromALengthSquared) {
			return false;
		}
		if (numPts == 2) {
			return true;
		}

		const Vec3f ac = simplexPoints[2].xyz - simplexPoints[0].xyz;
		const Vec3f baseNormal = glm::cross(ab, ac);
		const float baseNormalLengthSquared = glm::length2(baseNormal);
		const float signedVolume = glm::dot(baseNormal, fromA);
		return Math::IsFinite(baseNormalLengthSquared) && Math::IsFinite(signedVolume) &&
			signedVolume * signedVolume >
			Math::NumericalEpsilonSquared * baseNormalLengthSquared * fromALengthSquared;
	}

	bool ExpandSimplexToTetrahedron(const RigidBody3D* bodyA, const RigidBody3D* bodyB,
		point_t simplexPoints[4], int& numPts)
	{
		while (numPts < 4) {
			std::array<Vec3f, 8> directions{};
			int directionCount = 0;
			if (numPts == 1) {
				directions[directionCount++] = -simplexPoints[0].xyz;
			}
			else if (numPts == 2) {
				const Vec3f ab = simplexPoints[1].xyz - simplexPoints[0].xyz;
				Vec3f u;
				Vec3f v;
				GetOrtho(ab, u, v);
				directions[directionCount++] = u;
				directions[directionCount++] = -u;
				directions[directionCount++] = v;
				directions[directionCount++] = -v;
			}
			else {
				const Vec3f ab = simplexPoints[1].xyz - simplexPoints[0].xyz;
				const Vec3f ac = simplexPoints[2].xyz - simplexPoints[0].xyz;
				const Vec3f normal = glm::cross(ab, ac);
				directions[directionCount++] = normal;
				directions[directionCount++] = -normal;
			}

			directions[directionCount++] = Vec3f(1.0f, 0.0f, 0.0f);
			directions[directionCount++] = Vec3f(0.0f, 1.0f, 0.0f);
			directions[directionCount++] = Vec3f(0.0f, 0.0f, 1.0f);

			bool addedPoint = false;
			for (int directionIndex = 0; directionIndex < directionCount; ++directionIndex) {
				const Vec3f& direction = directions[directionIndex];
				const float directionLengthSquared = glm::length2(direction);
				if (!Math::IsFinite(directionLengthSquared) ||
					directionLengthSquared <= Math::NumericalEpsilonSquared) {
					continue;
				}

				const point_t candidate = Support(bodyA, bodyB, direction, 0.0f);
				if (!HasPoint(simplexPoints, numPts, candidate) &&
					IncreasesSimplexDimension(simplexPoints, numPts, candidate)) {
					simplexPoints[numPts++] = candidate;
					addedPoint = true;
					break;
				}
			}

			if (!addedPoint) {
				return false;
			}
		}

		return true;
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
		GE_PHYSICS_PROFILE_GJK_CALL();
		if (!HasValidCollisionShapes(bodyA, bodyB)) {
			return false;
		}
		const Vec3f origin(0.0f);

		int numPts = 1;
		point_t simplexPoints[4];
		simplexPoints[0] = Support(bodyA, bodyB, Vec3f(1, 1, 1), 0.0f);
		if (!IsValidSupportPoint(simplexPoints[0])) {
			return false;
		}

		float closestDist = 1e10f;
		bool doesContainOrigin = false;
		Vec3f newDir = simplexPoints[0].xyz * -1.0f;
		do {
			GE_PHYSICS_PROFILE_GJK_ITERATION();
			// Get the new point to check on
			point_t newPt = Support(bodyA, bodyB, newDir, 0.0f);
			if (!IsValidSupportPoint(newPt)) {
				return false;
			}

			// If the new point is the same as a previous point, then we can't expand any further
			if (HasPoint(simplexPoints, numPts, newPt)) {
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



	GjkContactStatus GJK_GetContact(const RigidBody3D* bodyA, const RigidBody3D* bodyB,
		float bias, Vec3f& ptOnA, Vec3f& ptOnB)
	{
		GE_PHYSICS_PROFILE_GJK_CALL();
		ptOnA = Vec3f(0.0f);
		ptOnB = Vec3f(0.0f);
		if (!HasValidCollisionShapes(bodyA, bodyB) || !Math::IsFinite(bias) || bias < 0.0f) {
			return GjkContactStatus::Failed;
		}
		
		const Vec3f origin(0.0f);

		int numPts = 1;
		point_t simplexPoints[4];
		simplexPoints[0] = Support(bodyA, bodyB, Vec3f(1, 1, 1), 0.0f);
		if (!IsValidSupportPoint(simplexPoints[0])) {
			return GjkContactStatus::Failed;
		}

		float closestDist = 1e10f;
		bool doesContainOrigin = false;
		Vec3f newDir = simplexPoints[0].xyz * -1.0f;
		do {
			GE_PHYSICS_PROFILE_GJK_ITERATION();
			// Get the new point to check on
			point_t newPt = Support(bodyA, bodyB, newDir, 0.0f);
			if (!IsValidSupportPoint(newPt)) {
				return GjkContactStatus::Failed;
			}

			// If the new point is the same as a previous point, then we can't expand any further
			if (HasPoint(simplexPoints, numPts, newPt)) {
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
			return GjkContactStatus::Separated;
		}

		// EPA requires four finite, affinely independent support points. Exact
		// face contacts often reach the origin with only a line or triangle.
		if (!ExpandSimplexToTetrahedron(bodyA, bodyB, simplexPoints, numPts)) {
			return GjkContactStatus::Failed;
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
		if (!Math::IsFinite(avg)) {
			return GjkContactStatus::Failed;
		}

		// Now expand the simplex by the bias amount
		for (int i = 0; i < numPts; i++) {
			point_t& pt = simplexPoints[i];

			Vec3f dir = pt.xyz - avg;	// ray from "center" to witness point
			dir = Math::NormalizeOr(dir, simplexPoints[0].xyz);
			
			pt.ptA += dir * bias;
			pt.ptB -= dir * bias;
			pt.xyz = pt.ptA - pt.ptB;
		}

		//
		// Perform EPA expansion of the simplex to find the closest face on the CSO
		//
		if (!EPA_Expand(bodyA, bodyB, bias, simplexPoints, ptOnA, ptOnB)) {
			ptOnA = Vec3f(0.0f);
			ptOnB = Vec3f(0.0f);
			return GjkContactStatus::Failed;
		}

		return GjkContactStatus::Contact;

	}

	bool GJK_DoesIntersect(const RigidBody3D* bodyA, const RigidBody3D* bodyB,
		const float bias, Vec3f& ptOnA, Vec3f& ptOnB)
	{
		return GJK_GetContact(bodyA, bodyB, bias, ptOnA, ptOnB) == GjkContactStatus::Contact;
	}




	void GJK_ClosestPoints(const RigidBody3D* bodyA, const RigidBody3D* bodyB, Vec3f& ptOnA, Vec3f& ptOnB)
	{
		GE_PHYSICS_PROFILE_GJK_CALL();
		ptOnA = Vec3f(0.0f);
		ptOnB = Vec3f(0.0f);
		if (!HasValidCollisionShapes(bodyA, bodyB)) {
			return;
		}
		
		const Vec3f origin(0.0f);

		float closestDist = 1e10f;
		const float bias = 0.0f;

		int numPts = 1;
		point_t simplexPoints[4];
		simplexPoints[0] = Support(bodyA, bodyB, Vec3f(1, 1, 1), bias);
		if (!IsValidSupportPoint(simplexPoints[0])) {
			return;
		}

		Vec4f lambdas = Vec4f(1, 0, 0, 0);
		Vec3f newDir = simplexPoints[0].xyz * -1.0f;
		do {
			GE_PHYSICS_PROFILE_GJK_ITERATION();
			// Get the new point to check on
			point_t newPt = Support(bodyA, bodyB, newDir, bias);
			if (!IsValidSupportPoint(newPt)) {
				return;
			}

			// If the new point is the same as a previous point, then we can't expand any further
			if (HasPoint(simplexPoints, numPts, newPt)) {
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


		Vec3f candidatePtOnA(0.0f);
		Vec3f candidatePtOnB(0.0f);
		for (int i = 0; i < 4; i++) {
			candidatePtOnA += simplexPoints[i].ptA * lambdas[i];
			candidatePtOnB += simplexPoints[i].ptB * lambdas[i];
		}
		if (Math::IsFinite(candidatePtOnA) && Math::IsFinite(candidatePtOnB)) {
			ptOnA = candidatePtOnA;
			ptOnB = candidatePtOnB;
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
		const float normalLengthSquared = glm::length2(normal);
		if (!Math::IsFinite(normalLengthSquared) || normalLengthSquared <= Math::NumericalEpsilonSquared)
		{
			return Vec3f(1.0f, 0.0f, 0.0f);
		}
		Vec3f p0 = normal * glm::dot(s1, normal) / normalLengthSquared;

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

		if (!Math::IsFinite(area_max) || std::fabs(area_max) <= Math::NumericalEpsilon)
		{
			return Vec3f(1.0f, 0.0f, 0.0f);
		}

		Vec3f lambdas = areas / area_max;

		if(!IsValid(lambdas))
		{
			lambdas = Vec3f(1, 0, 0);
		}
		return lambdas;
	}


	namespace
	{
		constexpr int EpaMaxIterations = 64;
		constexpr float EpaRelativeConvergenceTolerance = 1.0e-4f;

		bool IsValidPointIndex(int index, const std::vector<point_t>& points)
		{
			return index >= 0 && static_cast<std::size_t>(index) < points.size();
		}

		bool TryTriangleNormal(const tri_t& tri, const std::vector<point_t>& points, Vec3f& normal)
		{
			if (!IsValidPointIndex(tri.a, points) || !IsValidPointIndex(tri.b, points) ||
				!IsValidPointIndex(tri.c, points) || tri.a == tri.b || tri.b == tri.c || tri.c == tri.a) {
				return false;
			}

			const Vec3f& a = points[tri.a].xyz;
			const Vec3f& b = points[tri.b].xyz;
			const Vec3f& c = points[tri.c].xyz;
			if (!Math::IsFinite(a) || !Math::IsFinite(b) || !Math::IsFinite(c)) {
				return false;
			}

			const Vec3f ab = b - a;
			const Vec3f ac = c - a;
			const float abLengthSquared = glm::length2(ab);
			const float acLengthSquared = glm::length2(ac);
			const Vec3f unnormalizedNormal = glm::cross(ab, ac);
			const float normalLengthSquared = glm::length2(unnormalizedNormal);
			const float areaToleranceSquared = Math::NumericalEpsilonSquared *
				abLengthSquared * acLengthSquared;
			if (!Math::IsFinite(abLengthSquared) || !Math::IsFinite(acLengthSquared) ||
				!Math::IsFinite(normalLengthSquared) ||
				abLengthSquared <= Math::NumericalEpsilonSquared ||
				acLengthSquared <= Math::NumericalEpsilonSquared ||
				normalLengthSquared <= areaToleranceSquared) {
				return false;
			}

			normal = unnormalizedNormal * glm::inversesqrt(normalLengthSquared);
			return Math::IsFinite(normal);
		}

		bool TrySignedDistanceToTriangle(const tri_t& tri, const Vec3f& point,
			const std::vector<point_t>& points, float& distance, Vec3f* normalOut = nullptr)
		{
			Vec3f normal;
			if (!Math::IsFinite(point) || !TryTriangleNormal(tri, points, normal)) {
				return false;
			}

			distance = glm::dot(normal, point - points[tri.a].xyz);
			if (!Math::IsFinite(distance)) {
				return false;
			}
			if (normalOut) {
				*normalOut = normal;
			}
			return true;
		}

		bool IsNonDegenerateTetrahedron(const std::vector<point_t>& points)
		{
			if (points.size() < 4) {
				return false;
			}
			for (int index = 0; index < 4; ++index) {
				if (!IsValidSupportPoint(points[index])) {
					return false;
				}
			}

			const Vec3f ab = points[1].xyz - points[0].xyz;
			const Vec3f ac = points[2].xyz - points[0].xyz;
			const Vec3f ad = points[3].xyz - points[0].xyz;
			const Vec3f normal = glm::cross(ab, ac);
			const float normalLengthSquared = glm::length2(normal);
			const float adLengthSquared = glm::length2(ad);
			const float signedVolume = glm::dot(normal, ad);
			return Math::IsFinite(normalLengthSquared) && Math::IsFinite(adLengthSquared) &&
				Math::IsFinite(signedVolume) && signedVolume * signedVolume >
				Math::NumericalEpsilonSquared * normalLengthSquared * adLengthSquared;
		}

		int CountEdgeUses(const edge_t& edge, const std::vector<tri_t>& triangles)
		{
			int count = 0;
			for (const tri_t& tri : triangles) {
				const edge_t edges[3] = { { tri.a, tri.b }, { tri.b, tri.c }, { tri.c, tri.a } };
				for (const edge_t& candidate : edges) {
					if (edge == candidate) {
						++count;
					}
				}
			}
			return count;
		}

		bool IsClosedValidPolytope(const std::vector<tri_t>& triangles,
			const std::vector<point_t>& points)
		{
			if (triangles.size() < 4 || points.size() < 4) {
				return false;
			}
			for (const tri_t& tri : triangles) {
				Vec3f normal;
				if (!TryTriangleNormal(tri, points, normal)) {
					return false;
				}
				const edge_t edges[3] = { { tri.a, tri.b }, { tri.b, tri.c }, { tri.c, tri.a } };
				for (const edge_t& edge : edges) {
					if (CountEdgeUses(edge, triangles) != 2) {
						return false;
					}
				}
			}
			return true;
		}
	}



	/*
	================================
	ClosestTriangle
	================================
	*/

	int ClosestTriangle(const std::vector< tri_t >& triangles, const std::vector< point_t >& points) {
		float minDistSqr = std::numeric_limits<float>::infinity();

		int idx = -1;
		for (std::size_t i = 0; i < triangles.size(); i++) {
			const tri_t& tri = triangles[i];

			float dist = 0.0f;
			if (!TrySignedDistanceToTriangle(tri, Vec3f(0.0f), points, dist)) {
				continue;
			}
			const float distSqr = dist * dist;
			if (Math::IsFinite(distSqr) && distSqr < minDistSqr) {
				idx = static_cast<int>(i);
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
	bool HasPoint(const Vec3f& w, const std::vector<point_t>& points) {
		const float epsilons = 0.001f * 0.001f;
		if (!Math::IsFinite(w)) {
			return false;
		}
		for (const point_t& point : points) {
			const float distanceSquared = glm::length2(w - point.xyz);
			if (Math::IsFinite(distanceSquared) && distanceSquared < epsilons) {
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
	bool RemoveTrianglesFacingPoint(const Vec3f& pt, std::vector<tri_t>& triangles,
		const std::vector<point_t>& points, int& numRemoved) {
		numRemoved = 0;
		std::vector<tri_t> retained;
		retained.reserve(triangles.size());
		for (const tri_t& tri : triangles) {
			float dist = 0.0f;
			if (!TrySignedDistanceToTriangle(tri, pt, points, dist)) {
				return false;
			}
			if (dist > 0.0f) {
				++numRemoved;
			}
			else {
				retained.push_back(tri);
			}
		}
		triangles.swap(retained);
		return true;
	}



	/*
	================================
	FindDanglingEdges
	================================
	*/
	bool FindDanglingEdges(std::vector< edge_t >& danglingEdges,
		const std::vector< tri_t >& triangles, const std::vector<point_t>& points) {
		danglingEdges.clear();

		for (std::size_t i = 0; i < triangles.size(); i++) {
			const tri_t& tri = triangles[i];
			Vec3f normal;
			if (!TryTriangleNormal(tri, points, normal)) {
				return false;
			}

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

			for (std::size_t j = 0; j < triangles.size(); j++) {
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
		return true;
	}



	bool EPA_Expand(const RigidBody3D* bodyA, const RigidBody3D* bodyB, float bias,
		const point_t simplexPoints[4], Vec3f& ptOnA, Vec3f& ptOnB) {
		GE_PHYSICS_PROFILE_SCOPE(epaTimeNs);
		GE_PHYSICS_PROFILE_ADD(epaCallCount, 1);
		if (!HasValidCollisionShapes(bodyA, bodyB) || !Math::IsFinite(bias) || bias < 0.0f) {
			return false;
		}
		std::vector< point_t > points;
		std::vector< tri_t > triangles;
		std::vector< edge_t > danglingEdges;

		Vec3f center(0.0f);
		for (int i = 0; i < 4; i++) {
			if (!IsValidSupportPoint(simplexPoints[i])) {
				return false;
			}
			points.push_back(simplexPoints[i]);
			center += simplexPoints[i].xyz;
		}
		center *= 0.25f;
		if (!Math::IsFinite(center) || !IsNonDegenerateTetrahedron(points)) {
			return false;
		}

		// Build the triangles
		for (int i = 0; i < 4; i++) {
			int j = (i + 1) % 4;
			int k = (i + 2) % 4;
			tri_t tri;
			tri.a = i;
			tri.b = j;
			tri.c = k;

			int unusedPt = (i + 3) % 4;
			float dist = 0.0f;
			if (!TrySignedDistanceToTriangle(tri, points[unusedPt].xyz, points, dist)) {
				return false;
			}

			// The unused point is always on the negative/inside of the triangle.. make sure the normal points away
			if (dist > 0.0f) {
				std::swap(tri.a, tri.b);
			}

			triangles.push_back(tri);
		}
		if (!IsClosedValidPolytope(triangles, points)) {
			return false;
		}

		//
		//	Expand the simplex to find the closest face of the CSO to the origin
		//
		bool converged = false;
		for (int iteration = 0; iteration < EpaMaxIterations; ++iteration) {
			if (!IsClosedValidPolytope(triangles, points)) {
				return false;
			}
			const int idx = ClosestTriangle(triangles, points);
			if (idx < 0 || static_cast<std::size_t>(idx) >= triangles.size()) {
				return false;
			}

			Vec3f normal;
			float originDistance = 0.0f;
			if (!TrySignedDistanceToTriangle(triangles[idx], Vec3f(0.0f), points,
				originDistance, &normal)) {
				return false;
			}

			const point_t newPt = Support(bodyA, bodyB, normal, bias);
			if (!IsValidSupportPoint(newPt)) {
				return false;
			}

			float expansionDistance = 0.0f;
			if (!TrySignedDistanceToTriangle(triangles[idx], newPt.xyz, points,
				expansionDistance)) {
				return false;
			}
			const float convergenceTolerance = std::max(Math::NumericalEpsilon,
				EpaRelativeConvergenceTolerance * std::max(std::fabs(originDistance), bias));

			// if w already exists, then just stop
			// because it means we can't expand any further
			if (HasPoint(newPt.xyz, points)) {
				converged = true;
				break;
			}

			if (expansionDistance <= convergenceTolerance) {
				converged = true;
				break;
			}

			// Build the expansion transactionally. The last known-valid polytope is
			// untouched unless every replacement face is valid and closes the hull.
			std::vector<point_t> candidatePoints = points;
			std::vector<tri_t> candidateTriangles = triangles;
			const int newIdx = static_cast<int>(candidatePoints.size());
			candidatePoints.push_back(newPt);

			// Remove Triangles that face this point
			int numRemoved = 0;
			if (!RemoveTrianglesFacingPoint(newPt.xyz, candidateTriangles,
				candidatePoints, numRemoved) || numRemoved == 0 || candidateTriangles.empty()) {
				return false;
			}

			// Find Dangling Edges
			danglingEdges.clear();
			if (!FindDanglingEdges(danglingEdges, candidateTriangles, candidatePoints) ||
				danglingEdges.empty()) {
				return false;
			}

			// In theory the edges should be a proper CCW order
			// So we only need to add the new point as 'a' in order
			// to create new triangles that face away from origin
			for (const edge_t& edge : danglingEdges) {
				tri_t triangle;
				triangle.a = newIdx;
				triangle.b = edge.b;
				triangle.c = edge.a;

				// Make sure it's oriented properly
				float dist = 0.0f;
				if (!TrySignedDistanceToTriangle(triangle, center, candidatePoints, dist)) {
					return false;
				}
				if (dist > 0.0f) {
					std::swap(triangle.b, triangle.c);
				}

				Vec3f triangleNormal;
				if (!TryTriangleNormal(triangle, candidatePoints, triangleNormal)) {
					return false;
				}
				candidateTriangles.push_back(triangle);
			}

			if (!IsClosedValidPolytope(candidateTriangles, candidatePoints)) {
				return false;
			}
			points.swap(candidatePoints);
			triangles.swap(candidateTriangles);
		}
		if (!converged || !IsClosedValidPolytope(triangles, points)) {
			return false;
		}

		// Get the projection of the origin on the closest triangle
		const int idx = ClosestTriangle(triangles, points);
		if (idx < 0 || static_cast<std::size_t>(idx) >= triangles.size()) {
			return false;
		}
		const tri_t& tri = triangles[idx];
		Vec3f finalNormal;
		float finalOriginDistance = 0.0f;
		if (!TrySignedDistanceToTriangle(tri, Vec3f(0.0f), points,
			finalOriginDistance, &finalNormal)) {
			return false;
		}
		Vec3f ptA_w = points[tri.a].xyz;
		Vec3f ptB_w = points[tri.b].xyz;
		Vec3f ptC_w = points[tri.c].xyz;
		Vec3f lambdas = BarycentricCoordinates(ptA_w, ptB_w, ptC_w, Vec3f(0.0f));
		const float lambdaSum = lambdas.x + lambdas.y + lambdas.z;
		const Vec3f projectedPoint = ptA_w * lambdas.x + ptB_w * lambdas.y + ptC_w * lambdas.z;
		const Vec3f expectedProjection = -finalOriginDistance * finalNormal;
		const float projectionErrorSquared = glm::length2(projectedPoint - expectedProjection);
		const float projectionScaleSquared = std::max(1.0f, glm::length2(expectedProjection));
		if (!Math::IsFinite(lambdas) || !Math::IsFinite(lambdaSum) ||
			std::fabs(lambdaSum - 1.0f) > 1.0e-3f ||
			lambdas.x < -1.0e-3f || lambdas.y < -1.0e-3f || lambdas.z < -1.0e-3f ||
			!Math::IsFinite(projectionErrorSquared) ||
			projectionErrorSquared > EpaRelativeConvergenceTolerance *
				EpaRelativeConvergenceTolerance * projectionScaleSquared) {
			return false;
		}

		// Get the point on shape A
		Vec3f ptA_a = points[tri.a].ptA;
		Vec3f ptB_a = points[tri.b].ptA;
		Vec3f ptC_a = points[tri.c].ptA;
		const Vec3f candidatePtOnA = ptA_a * lambdas[0] + ptB_a * lambdas[1] + ptC_a * lambdas[2];

		// Get the point on shape B
		Vec3f ptA_b = points[tri.a].ptB;
		Vec3f ptB_b = points[tri.b].ptB;
		Vec3f ptC_b = points[tri.c].ptB;
		const Vec3f candidatePtOnB = ptA_b * lambdas[0] + ptB_b * lambdas[1] + ptC_b * lambdas[2];

		const float penetrationDistance = glm::length(candidatePtOnB - candidatePtOnA);
		if (!Math::IsFinite(candidatePtOnA) || !Math::IsFinite(candidatePtOnB) ||
			!Math::IsFinite(penetrationDistance)) {
			return false;
		}

		ptOnA = candidatePtOnA;
		ptOnB = candidatePtOnB;
		return true;
	}



}
