#include"gepch.h"
#include "GJK.h"
#include "PhysicsProfile.h"
#include "Math/Math.h"
#include "PhysicsBody.h"
#include "Shape.h"
#include "glm/glm.hpp"

#include <array>
#include <bit>
#include <limits>

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
		const point_t simplexPoints[4], Vec3f& ptOnA, Vec3f& ptOnB,
		EpaDiagnostics* diagnostics, unsigned maxIterations);

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







	// Each public search owns its diagnostics; no shared state or profiling dependency.
	struct GjkSearchControl
	{
		GjkDiagnostics* output;
		unsigned limit;
		unsigned iterations{};
		GjkTermination reason{ GjkTermination::InvalidInput };

		GjkSearchControl(GjkDiagnostics* diagnostics, unsigned requestedLimit)
			: output(diagnostics), limit(std::min(requestedLimit, GjkMaxIterations))
		{
			if (output) { *output = GjkDiagnostics{}; }
		}

		void Finish(GjkTermination termination)
		{
			reason = termination;
			if (output) { output->termination = reason; output->iterations = iterations; }
		}

		bool Next()
		{
			if (iterations == limit) {
				Finish(GjkTermination::IterationLimit);
				return false;
			}
			++iterations;
			return true;
		}
	};

	static double GjkDistanceSquared(const Vec3f& direction)
	{
		const float distance = glm::length2(direction);
		// Preserve established float projections, using double only when the square
		// overflows despite a finite direction.
		return Math::IsFinite(distance) ? distance :
			glm::dot(glm::dvec3(direction), glm::dvec3(direction));
	}

	static bool GjkMadeProgress(double previous, double current)
	{
		// Both terms have units of length squared. Scaling a scene scales both sides
		// equally. A quarter epsilon is below one ULP of a normal float distance:
		// do not discard representable progress, which can change witness selection.
		// The cap handles paths that keep making tiny progress. Infinity denotes the
		// first projection, not a fixed world-distance cutoff.
		constexpr double relativeTolerance = 0.25 * std::numeric_limits<float>::epsilon();
		return current < previous && (!std::isfinite(previous) ||
			previous - current > relativeTolerance * previous);
	}

	static bool IsValidGjkProjection(const Vec3f& direction, const Vec4f& lambdas)
	{
		return Math::IsFinite(direction) && Math::IsFinite(lambdas) &&
			lambdas.x >= 0 && lambdas.y >= 0 && lambdas.z >= 0 && lambdas.w >= 0 &&
			lambdas.x + lambdas.y + lambdas.z + lambdas.w > Math::NumericalEpsilon;
	}

	bool GJK_DoesIntersect(const RigidBody3D* bodyA, const RigidBody3D* bodyB,
		GjkDiagnostics* diagnostics, unsigned maxIterations)
	{
		GE_PHYSICS_PROFILE_GJK_CALL();
		GjkSearchControl search(diagnostics, maxIterations);
		if (!HasValidCollisionShapes(bodyA, bodyB)) {
			return false;
		}
		const Vec3f origin(0.0f);

		int numPts = 1;
		point_t simplexPoints[4];
		simplexPoints[0] = Support(bodyA, bodyB, Vec3f(1, 1, 1), 0.0f);
		if (!IsValidSupportPoint(simplexPoints[0])) {
			search.Finish(GjkTermination::InvalidSupport);
			return false;
		}

		double closestDist = std::numeric_limits<double>::infinity();
		bool doesContainOrigin = false;
		Vec3f newDir = simplexPoints[0].xyz * -1.0f;
		do {
			if (!search.Next()) { return false; }
			GE_PHYSICS_PROFILE_GJK_ITERATION();
			// Get the new point to check on
			point_t newPt = Support(bodyA, bodyB, newDir, 0.0f);
			if (!IsValidSupportPoint(newPt)) {
				search.Finish(GjkTermination::InvalidSupport);
				return false;
			}

			// If the new point is the same as a previous point, then we can't expand any further
			if (HasPoint(simplexPoints, numPts, newPt)) {
				search.Finish(GjkTermination::DuplicateSupport);
				break;
			}

			simplexPoints[numPts] = newPt;
			numPts++;

			// If this new point hasn't moved passed the origin, then the
			// origin cannot be in the set. And therefore there is no collision.
			const float supportProjection = glm::dot(newDir, newPt.xyz - origin);
			const double dotdot = Math::IsFinite(supportProjection) ? supportProjection :
				glm::dot(glm::dvec3(newDir), glm::dvec3(newPt.xyz - origin));
			if (dotdot < 0.0f) {
				search.Finish(GjkTermination::SeparatingAxis);
				break;
			}

			Vec4f lambdas;
			doesContainOrigin = SimplexSignedVolumes(simplexPoints, numPts, newDir, lambdas);
			if (!IsValidGjkProjection(newDir, lambdas)) {
				search.Finish(GjkTermination::InvalidSimplex);
				return false;
			}
			if (doesContainOrigin) {
				break;
			}

			// Check that the new projection of the origin onto the simplex is closer than the previous
			const double dist = GjkDistanceSquared(newDir);
			if (!GjkMadeProgress(closestDist, dist)) {
				search.Finish(GjkTermination::NoProgress);
				break;
			}
			closestDist = dist;

			// Use the lambdas that support the new search direction, and invalidate any points that don't support it
			SortValids(simplexPoints, lambdas);
			numPts = NumValids(lambdas);
			doesContainOrigin = (4 == numPts);
		} while (!doesContainOrigin);
		if (doesContainOrigin) { search.Finish(GjkTermination::OriginReached); }

		return doesContainOrigin;

	}



	GjkContactStatus GJK_GetContact(const RigidBody3D* bodyA, const RigidBody3D* bodyB,
		float bias, Vec3f& ptOnA, Vec3f& ptOnB,
		GjkDiagnostics* diagnostics, unsigned maxIterations, unsigned maxEpaIterations)
	{
		GE_PHYSICS_PROFILE_GJK_CALL();
		GjkSearchControl search(diagnostics, maxIterations);
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
			search.Finish(GjkTermination::InvalidSupport);
			return GjkContactStatus::Failed;
		}

		double closestDist = std::numeric_limits<double>::infinity();
		bool doesContainOrigin = false;
		Vec3f newDir = simplexPoints[0].xyz * -1.0f;
		do {
			if (!search.Next()) { return GjkContactStatus::Failed; }
			GE_PHYSICS_PROFILE_GJK_ITERATION();
			// Get the new point to check on
			point_t newPt = Support(bodyA, bodyB, newDir, 0.0f);
			if (!IsValidSupportPoint(newPt)) {
				search.Finish(GjkTermination::InvalidSupport);
				return GjkContactStatus::Failed;
			}

			// If the new point is the same as a previous point, then we can't expand any further
			if (HasPoint(simplexPoints, numPts, newPt)) {
				search.Finish(GjkTermination::DuplicateSupport);
				break;
			}

			simplexPoints[numPts] = newPt;
			numPts++;

			// If this new point hasn't moved passed the origin, then the
			// origin cannot be in the set. And therefore there is no collision.
			const float supportProjection = glm::dot(newDir, newPt.xyz - origin);
			const double dotdot = Math::IsFinite(supportProjection) ? supportProjection :
				glm::dot(glm::dvec3(newDir), glm::dvec3(newPt.xyz - origin));
			if (dotdot < 0.0f) {
				search.Finish(GjkTermination::SeparatingAxis);
				break;
			}

			Vec4f lambdas;
			doesContainOrigin = SimplexSignedVolumes(simplexPoints, numPts, newDir, lambdas);
			if (!IsValidGjkProjection(newDir, lambdas)) {
				search.Finish(GjkTermination::InvalidSimplex);
				return GjkContactStatus::Failed;
			}
			if (doesContainOrigin) {
				break;
			}

			// Check that the new projection of the origin onto the simplex is closer than the previous
			const double dist = GjkDistanceSquared(newDir);
			if (!GjkMadeProgress(closestDist, dist)) {
				search.Finish(GjkTermination::NoProgress);
				break;
			}
			closestDist = dist;

			// Use the lambdas that support the new search direction, and invalidate any points that don't support it
			SortValids(simplexPoints, lambdas);
			numPts = NumValids(lambdas);
			doesContainOrigin = (4 == numPts);
		} while (!doesContainOrigin);
		if (doesContainOrigin) { search.Finish(GjkTermination::OriginReached); }

		if (!doesContainOrigin) {
			return GjkContactStatus::Separated;
		}

		// EPA requires four finite, affinely independent support points. Exact
		// face contacts often reach the origin with only a line or triangle.
		if (!ExpandSimplexToTetrahedron(bodyA, bodyB, simplexPoints, numPts)) {
			search.Finish(GjkTermination::ContactExpansionFailed);
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
			search.Finish(GjkTermination::InvalidSimplex);
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
		if (!EPA_Expand(bodyA, bodyB, bias, simplexPoints, ptOnA, ptOnB,
			diagnostics ? &diagnostics->epa : nullptr, maxEpaIterations)) {
			search.Finish(GjkTermination::ContactExpansionFailed);
			ptOnA = Vec3f(0.0f);
			ptOnB = Vec3f(0.0f);
			return GjkContactStatus::Failed;
		}

		return GjkContactStatus::Contact;

	}

	bool GJK_DoesIntersect(const RigidBody3D* bodyA, const RigidBody3D* bodyB,
		const float bias, Vec3f& ptOnA, Vec3f& ptOnB,
		GjkDiagnostics* diagnostics, unsigned maxIterations, unsigned maxEpaIterations)
	{
		return GJK_GetContact(bodyA, bodyB, bias, ptOnA, ptOnB, diagnostics, maxIterations, maxEpaIterations) == GjkContactStatus::Contact;
	}




	void GJK_ClosestPoints(const RigidBody3D* bodyA, const RigidBody3D* bodyB, Vec3f& ptOnA, Vec3f& ptOnB,
		GjkDiagnostics* diagnostics, unsigned maxIterations)
	{
		GE_PHYSICS_PROFILE_GJK_CALL();
		GjkSearchControl search(diagnostics, maxIterations);
		ptOnA = Vec3f(0.0f);
		ptOnB = Vec3f(0.0f);
		if (!HasValidCollisionShapes(bodyA, bodyB)) {
			return;
		}
		
		const Vec3f origin(0.0f);

		double closestDist = std::numeric_limits<double>::infinity();
		const float bias = 0.0f;

		int numPts = 1;
		point_t simplexPoints[4];
		simplexPoints[0] = Support(bodyA, bodyB, Vec3f(1, 1, 1), bias);
		if (!IsValidSupportPoint(simplexPoints[0])) {
			search.Finish(GjkTermination::InvalidSupport);
			return;
		}

		Vec4f lambdas = Vec4f(1, 0, 0, 0);
		Vec3f newDir = simplexPoints[0].xyz * -1.0f;
		do {
			if (!search.Next()) { return; }
			GE_PHYSICS_PROFILE_GJK_ITERATION();
			// Get the new point to check on
			point_t newPt = Support(bodyA, bodyB, newDir, bias);
			if (!IsValidSupportPoint(newPt)) {
				search.Finish(GjkTermination::InvalidSupport);
				return;
			}

			// If the new point is the same as a previous point, then we can't expand any further
			if (HasPoint(simplexPoints, numPts, newPt)) {
				search.Finish(GjkTermination::DuplicateSupport);
				break;
			}

			// Add point and get new search direction
			simplexPoints[numPts] = newPt;
			numPts++;

			SimplexSignedVolumes(simplexPoints, numPts, newDir, lambdas);
			if (!IsValidGjkProjection(newDir, lambdas)) {
				search.Finish(GjkTermination::InvalidSimplex);
				return;
			}
			SortValids(simplexPoints, lambdas);
			numPts = NumValids(lambdas);

			// Check that the new projection of the origin onto the simplex is closer than the previous
			const double dist = GjkDistanceSquared(newDir);
			if (!GjkMadeProgress(closestDist, dist)) {
				search.Finish(GjkTermination::NoProgress);
				break;
			}
			closestDist = dist;
		} while (numPts < 4);
		if (numPts == 4) { search.Finish(GjkTermination::OriginReached); }


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
		else { search.Finish(GjkTermination::InvalidSimplex); }

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
			// P20_EPA_OBSERVATION_BEGIN
			PhysicsDiagnostic::BarycentricRecord([&](auto& value) {
				value.exit = PhysicsDiagnostic::BarycentricExit::NormalFallback;
				value.validFields = 1;
				value.normalLengthSquared = normalLengthSquared;
				value.returnedWeights = Vec3f(1.0f, 0.0f, 0.0f);
			});
			// P20_EPA_OBSERVATION_END
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
			// P20_EPA_OBSERVATION_BEGIN
			PhysicsDiagnostic::BarycentricRecord([&](auto& value) {
				value.exit = PhysicsDiagnostic::BarycentricExit::AreaFallback;
				value.validFields = 15;
				value.normalLengthSquared = normalLengthSquared;
				value.projectedOrigin = p0;
				value.dominantArea = area_max;
				value.dominantAxis = idx;
				value.subareas = areas;
				value.returnedWeights = Vec3f(1.0f, 0.0f, 0.0f);
			});
			// P20_EPA_OBSERVATION_END
			return Vec3f(1.0f, 0.0f, 0.0f);
		}

		Vec3f lambdas = areas / area_max;

		// P20_EPA_OBSERVATION_BEGIN
		const Vec3f observationRawWeights = lambdas;
		// P20_EPA_OBSERVATION_END
		if(!IsValid(lambdas))
		{
			lambdas = Vec3f(1, 0, 0);
		}
		// P20_EPA_OBSERVATION_BEGIN
		PhysicsDiagnostic::BarycentricRecord([&](auto& value) {
			value.exit = IsValid(observationRawWeights) ? PhysicsDiagnostic::BarycentricExit::Weights :
				PhysicsDiagnostic::BarycentricExit::NonfiniteWeightsFallback;
			value.validFields = 31;
			value.normalLengthSquared = normalLengthSquared;
			value.projectedOrigin = p0;
			value.dominantArea = area_max;
			value.dominantAxis = idx;
			value.subareas = areas;
			value.rawWeights = observationRawWeights;
			value.returnedWeights = lambdas;
		});
		// P20_EPA_OBSERVATION_END
		return lambdas;
	}


	namespace
	{
		struct EpaQueryControl
		{
			EpaDiagnostics* output;
			EpaDiagnostics result{ EpaTermination::InvalidInput, 0 };

			explicit EpaQueryControl(EpaDiagnostics* diagnostics) : output(diagnostics) {}
			~EpaQueryControl()
			{
				if (output) { *output = result; }
				GE_PHYSICS_PROFILE_ADD(epaIterationCount, result.iterations);
				GE_PHYSICS_PROFILE_MAX(epaMaxIterations, result.iterations);
				if (result.termination != EpaTermination::Converged &&
					result.termination != EpaTermination::DuplicateSupport) {
					GE_PHYSICS_PROFILE_ADD(epaFailureCount, 1);
				}
			}

			bool Finish(EpaTermination reason)
			{
				result.termination = reason;
				return reason == EpaTermination::Converged || reason == EpaTermination::DuplicateSupport;
			}
		};
		constexpr float EpaRelativeConvergenceTolerance = 1.0e-4f;

		struct EpaFinalProjection
		{
			Vec3f lambdas{}, projectedPoint{}, expectedProjection{};
			float lambdaSum{}, projectionErrorSquared{}, projectionScaleSquared{};
		};

		EpaFinalProjection EvaluateFinalProjection(const point_t& a, const point_t& b,
			const point_t& c, const Vec3f& lambdas, const Vec3f& finalNormal,
			float finalOriginDistance)
		{
			Vec3f ptA_w = a.xyz;
			Vec3f ptB_w = b.xyz;
			Vec3f ptC_w = c.xyz;
			const float lambdaSum = lambdas.x + lambdas.y + lambdas.z;
			const Vec3f projectedPoint = ptA_w * lambdas.x + ptB_w * lambdas.y + ptC_w * lambdas.z;
			const Vec3f expectedProjection = -finalOriginDistance * finalNormal;
			const float projectionErrorSquared = glm::length2(projectedPoint - expectedProjection);
			const float projectionScaleSquared = std::max(1.0f, glm::length2(expectedProjection));
			return {lambdas, projectedPoint, expectedProjection, lambdaSum,
				projectionErrorSquared, projectionScaleSquared};
		}

		EpaTermination ReconstructFinalProjection(const point_t& a, const point_t& b,
			const point_t& c, const EpaFinalProjection& projection, Vec3f& ptOnA, Vec3f& ptOnB)
		{
			const Vec3f& lambdas = projection.lambdas;
			const float lambdaSum = projection.lambdaSum;
			const float projectionErrorSquared = projection.projectionErrorSquared;
			const float projectionScaleSquared = projection.projectionScaleSquared;
			if (!Math::IsFinite(lambdas) || !Math::IsFinite(lambdaSum) ||
				std::fabs(lambdaSum - 1.0f) > 1.0e-3f ||
				lambdas.x < -1.0e-3f || lambdas.y < -1.0e-3f || lambdas.z < -1.0e-3f ||
				!Math::IsFinite(projectionErrorSquared) ||
				projectionErrorSquared > EpaRelativeConvergenceTolerance *
					EpaRelativeConvergenceTolerance * projectionScaleSquared) {
				return EpaTermination::InvalidProjection;
			}

			// Get the point on shape A
			Vec3f ptA_a = a.ptA;
			Vec3f ptB_a = b.ptA;
			Vec3f ptC_a = c.ptA;
			const Vec3f candidatePtOnA = ptA_a * lambdas[0] + ptB_a * lambdas[1] + ptC_a * lambdas[2];

			// Get the point on shape B
			Vec3f ptA_b = a.ptB;
			Vec3f ptB_b = b.ptB;
			Vec3f ptC_b = c.ptB;
			const Vec3f candidatePtOnB = ptA_b * lambdas[0] + ptB_b * lambdas[1] + ptC_b * lambdas[2];

			const float penetrationDistance = glm::length(candidatePtOnB - candidatePtOnA);
			if (!Math::IsFinite(candidatePtOnA) || !Math::IsFinite(candidatePtOnB) ||
				!Math::IsFinite(penetrationDistance)) {
				return EpaTermination::InvalidWitness;
			}

			ptOnA = candidatePtOnA;
			ptOnB = candidatePtOnB;
			// The caller retains its original EPA convergence reason.
			return EpaTermination::Converged;
		}

		std::uint32_t FinalProjectionFailedPredicates(const EpaFinalProjection& projection)
		{
			const Vec3f& lambdas = projection.lambdas;
			const float lambdaSum = projection.lambdaSum;
			const float projectionErrorSquared = projection.projectionErrorSquared;
			const float projectionScaleSquared = projection.projectionScaleSquared;
			return (!Math::IsFinite(lambdas) ? 1u : 0u) |
				(!Math::IsFinite(lambdaSum) ? 2u : 0u) |
				(std::fabs(lambdaSum - 1.0f) > 1.0e-3f ? 4u : 0u) |
				(lambdas.x < -1.0e-3f ? 8u : 0u) |
				(lambdas.y < -1.0e-3f ? 16u : 0u) |
				(lambdas.z < -1.0e-3f ? 32u : 0u) |
				(!Math::IsFinite(projectionErrorSquared) ? 64u : 0u) |
				(projectionErrorSquared > EpaRelativeConvergenceTolerance *
					EpaRelativeConvergenceTolerance * projectionScaleSquared ? 128u : 0u);
		}

		// Exact predicates for the admitted final-plane recovery only. A binary32
		// value on the common 2^-149 grid needs at most 277 magnitude bits.
		// The degree-four expressions below need fewer than 1120; capacity is
		// checked at every arithmetic boundary and never grows dynamically.
		constexpr unsigned EpaProjectionWordCount = 40;
		struct EpaProjectionInteger
		{
			std::uint32_t words[EpaProjectionWordCount]{};
			unsigned used{};
			bool negative{};
		};
		struct EpaProjectionWorkspace
		{
			// 0..2: selected normal; 3..9: reusable arithmetic;
			// 10: denominator/plane value; 11..13: barycentric numerators.
			EpaProjectionInteger value[14]{};
			unsigned gridShift = 277; // Minimum exact low-bit position; 277 means no nonzero operand yet.
		};
		static_assert(sizeof(EpaProjectionWorkspace) <= 2360);

		bool IncludeProjectionGrid(const Vec3f& input, unsigned& gridShift)
		{
			for (unsigned axis = 0; axis < 3; ++axis) {
				const std::uint32_t bits = std::bit_cast<std::uint32_t>(input[axis]);
				const unsigned exponent = (bits >> 23) & 255u;
				if (exponent == 255u) return false;
				const std::uint32_t mantissa = (bits & 0x7fffffu) | (exponent ? 0x800000u : 0u);
				if (mantissa) {
					const unsigned low = (exponent ? exponent - 1 : 0) + std::countr_zero(mantissa);
					if (low < gridShift) gridShift = low;
				}
			}
			return true;
		}

		void NormalizeProjectionInteger(EpaProjectionInteger& value)
		{
			while (value.used && value.words[value.used - 1] == 0) --value.used;
			if (!value.used) value.negative = false;
		}

		__declspec(noinline) bool InitializeProjectionGrid(const std::span<const point_t>& points,
			const Vec3f& finalNormal, unsigned& gridShift)
		{
			if (!IncludeProjectionGrid(finalNormal, gridShift)) return false;
			for (const point_t& point : points) {
				if (!IncludeProjectionGrid(point.xyz, gridShift)) return false;
			}
			return true;
		}

		bool LoadProjectionInteger(float input, EpaProjectionInteger& value, unsigned gridShift)
		{
			const std::uint32_t bits = std::bit_cast<std::uint32_t>(input);
			const unsigned exponent = (bits >> 23) & 255u;
			if (exponent == 255u) return false;
			std::uint32_t mantissa = (bits & 0x7fffffu) | (exponent ? 0x800000u : 0u);
			memset(value.words, 0, sizeof(value.words));
			value.used = 0;
			value.negative = false;
			if (!mantissa) return true;
			unsigned shift = exponent ? exponent - 1 : 0;
			// Remove only the common exact power of two. No floating operation,
			// discarded significant bit or new tolerance participates in a predicate.
			if (gridShift > shift) {
				const unsigned removed = gridShift - shift;
				if (removed > 23 || (mantissa & ((1u << removed) - 1u))) return false;
				mantissa >>= removed;
				shift = 0;
			} else shift -= gridShift;
			const unsigned word = shift / 32;
			const std::uint64_t shifted = static_cast<std::uint64_t>(mantissa) << (shift % 32);
			if (word + 1 >= EpaProjectionWordCount) return false;
			value.words[word] = static_cast<std::uint32_t>(shifted);
			value.words[word + 1] = static_cast<std::uint32_t>(shifted >> 32);
			value.used = word + (value.words[word + 1] ? 2 : 1);
			value.negative = (bits >> 31) != 0;
			return true;
		}

		int CompareProjectionMagnitude(const EpaProjectionInteger& a, const EpaProjectionInteger& b)
		{
			if (a.used != b.used) return a.used < b.used ? -1 : 1;
			for (unsigned i = a.used; i != 0; --i) {
				if (a.words[i - 1] != b.words[i - 1])
					return a.words[i - 1] < b.words[i - 1] ? -1 : 1;
			}
			return 0;
		}

		bool AddProjectionInteger(EpaProjectionInteger& a, const EpaProjectionInteger& b,
			bool subtract = false)
		{
			const unsigned aUsed = a.used;
			const unsigned count = a.used < b.used ? b.used : a.used;
			const bool bNegative = b.used && (b.negative != subtract);
			if (!b.used) return true;
			if (!a.used) {
				a = b;
				a.negative = bNegative;
				return true;
			}
			if (a.negative == bNegative) {
				std::uint64_t carry = 0;
				for (unsigned i = 0; i < count; ++i) {
					const std::uint64_t sum = static_cast<std::uint64_t>(i < aUsed ? a.words[i] : 0) +
						(i < b.used ? b.words[i] : 0) + carry;
					a.words[i] = static_cast<std::uint32_t>(sum);
					carry = sum >> 32;
				}
				a.used = count;
				if (carry) {
					if (a.used == EpaProjectionWordCount) return false;
					a.words[a.used++] = static_cast<std::uint32_t>(carry);
				}
			} else {
				// The same magnitude comparison stays in this frame, so its
				// temporary call frame does not overlap the subtraction state.
				bool aLarger = a.used >= b.used;
				if (a.used == b.used) {
					for (unsigned i = a.used; i != 0; --i) {
						if (a.words[i - 1] != b.words[i - 1]) {
							aLarger = a.words[i - 1] > b.words[i - 1];
							break;
						}
					}
				}
				std::uint64_t borrow = 0;
				for (unsigned i = 0; i < count; ++i) {
					const std::uint64_t av = i < aUsed ? a.words[i] : 0;
					const std::uint64_t bv = i < b.used ? b.words[i] : 0;
					const std::uint64_t larger = aLarger ? av : bv;
					const std::uint64_t smaller = (aLarger ? bv : av) + borrow;
					a.words[i] = static_cast<std::uint32_t>(larger - smaller);
					borrow = larger < smaller ? 1 : 0;
				}
				if (borrow) return false;
				a.used = count;
				if (!aLarger) a.negative = bNegative;
			}
			while (a.used && a.words[a.used - 1] == 0) --a.used;
			if (!a.used) a.negative = false;
			return true;
		}

		bool MultiplyProjectionInteger(const EpaProjectionInteger& a, const EpaProjectionInteger& b,
			EpaProjectionInteger& output)
		{
			if (&output == &a || &output == &b || a.used + b.used > EpaProjectionWordCount) return false;
			memset(output.words, 0, sizeof(output.words));
			output.used = 0;
			output.negative = false;
			if (!a.used || !b.used) return true;
			for (unsigned i = 0; i < a.used; ++i) {
				std::uint64_t carry = 0;
				for (unsigned j = 0; j < b.used; ++j) {
					// Product + existing word + carry is at most UINT64_MAX.
					const std::uint64_t product = static_cast<std::uint64_t>(a.words[i]) * b.words[j] +
						output.words[i + j] + carry;
					output.words[i + j] = static_cast<std::uint32_t>(product);
					carry = product >> 32;
				}
				output.words[i + b.used] = static_cast<std::uint32_t>(carry);
			}
			output.used = a.used + b.used;
			output.negative = a.negative != b.negative;
			NormalizeProjectionInteger(output);
			return true;
		}

		bool ProjectionDifference(float a, float b, EpaProjectionInteger& output,
			EpaProjectionInteger& scratch, unsigned gridShift)
		{
			return LoadProjectionInteger(a, output, gridShift) && LoadProjectionInteger(b, scratch, gridShift) &&
				AddProjectionInteger(output, scratch, true);
		}

		bool BuildProjectionNormal(const Vec3f& a, const Vec3f& b, const Vec3f& c,
			EpaProjectionWorkspace& workspace)
		{
			auto& w = workspace.value;
			for (unsigned i = 0; i < 3; ++i) {
				const unsigned j = (i + 1) % 3, k = (i + 2) % 3;
				if (!ProjectionDifference(b[j], a[j], w[3], w[9], workspace.gridShift) ||
					!ProjectionDifference(c[k], a[k], w[4], w[9], workspace.gridShift) ||
					!ProjectionDifference(b[k], a[k], w[5], w[9], workspace.gridShift) ||
					!ProjectionDifference(c[j], a[j], w[6], w[9], workspace.gridShift) ||
					!MultiplyProjectionInteger(w[3], w[4], w[7]) ||
					!MultiplyProjectionInteger(w[5], w[6], w[8]) ||
					!AddProjectionInteger(w[7], w[8], true)) return false;
				w[i] = w[7];
			}
			return w[0].used || w[1].used || w[2].used;
		}

		bool ProjectionPlaneValue(const Vec3f& point, const Vec3f& base,
			EpaProjectionWorkspace& workspace)
		{
			auto& w = workspace.value;
			w[10].used = 0;
			w[10].negative = false;
			for (unsigned i = 0; i < 3; ++i) {
				if (!ProjectionDifference(point[i], base[i], w[3], w[4], workspace.gridShift) ||
					!MultiplyProjectionInteger(w[i], w[3], w[5]) ||
					!AddProjectionInteger(w[10], w[5])) return false;
			}
			return true;
		}

		bool ProjectionDotCross(const Vec3f& u, const Vec3f& v, const Vec3f& base,
			EpaProjectionInteger& output, EpaProjectionWorkspace& workspace)
		{
			auto& w = workspace.value;
			output.used = 0;
			output.negative = false;
			for (unsigned i = 0; i < 3; ++i) {
				const unsigned j = (i + 1) % 3, k = (i + 2) % 3;
				if (!LoadProjectionInteger(u[j], w[3], workspace.gridShift) || !LoadProjectionInteger(base[j], w[9], workspace.gridShift) ||
					!AddProjectionInteger(w[3], w[9], true) ||
					!LoadProjectionInteger(v[k], w[4], workspace.gridShift) || !LoadProjectionInteger(base[k], w[9], workspace.gridShift) ||
					!AddProjectionInteger(w[4], w[9], true) ||
					!LoadProjectionInteger(u[k], w[5], workspace.gridShift) || !LoadProjectionInteger(base[k], w[9], workspace.gridShift) ||
					!AddProjectionInteger(w[5], w[9], true) ||
					!LoadProjectionInteger(v[j], w[6], workspace.gridShift) || !LoadProjectionInteger(base[j], w[9], workspace.gridShift) ||
					!AddProjectionInteger(w[6], w[9], true) ||
					!MultiplyProjectionInteger(w[3], w[4], w[7]) ||
					!MultiplyProjectionInteger(w[5], w[6], w[8]) ||
					!AddProjectionInteger(w[7], w[8], true) ||
					!MultiplyProjectionInteger(w[i], w[7], w[9]) ||
					!AddProjectionInteger(output, w[9])) return false;
			}
			return true;
		}

		bool ProjectionFaceCoordinates(const point_t& a, const point_t& b, const point_t& c,
			EpaProjectionWorkspace& workspace)
		{
			auto& w = workspace.value;
			if (!ProjectionDotCross(b.xyz, c.xyz, a.xyz, w[10], workspace) ||
				!ProjectionDotCross(b.xyz, c.xyz, Vec3f(0.0f), w[11], workspace) ||
				!ProjectionDotCross(c.xyz, a.xyz, Vec3f(0.0f), w[12], workspace) ||
				!ProjectionDotCross(a.xyz, b.xyz, Vec3f(0.0f), w[13], workspace)) return false;
			// The exact area identity also guards the private workspace contract.
			w[3] = w[11];
			return AddProjectionInteger(w[3], w[12]) && AddProjectionInteger(w[3], w[13]) &&
				w[3].negative == w[10].negative && CompareProjectionMagnitude(w[3], w[10]) == 0;
		}

		bool ShiftProjectionInteger(EpaProjectionInteger& value)
		{
			std::uint64_t carry = 0;
			for (unsigned i = 0; i < value.used; ++i) {
				const std::uint64_t shifted = (static_cast<std::uint64_t>(value.words[i]) << 1) | carry;
				value.words[i] = static_cast<std::uint32_t>(shifted);
				carry = shifted >> 32;
			}
			if (carry) {
				if (value.used == EpaProjectionWordCount) return false;
				value.words[value.used++] = static_cast<std::uint32_t>(carry);
			}
			return true;
		}

		bool ProjectionRatioToFloat(const EpaProjectionInteger& numerator,
			const EpaProjectionInteger& denominator, EpaProjectionInteger& remainder, float& output)
		{
			if (!denominator.used || denominator.negative || numerator.negative) return false;
			const int order = CompareProjectionMagnitude(numerator, denominator);
			if (order > 0) return false;
			if (!numerator.used || order == 0) {
				output = numerator.used ? 1.0f : 0.0f;
				return true;
			}
			remainder = numerator;
			unsigned leading = 0, roundPosition = 150;
			std::uint32_t significand = 0;
			// Binary32 has 24 significand bits, minimum normal exponent -126,
			// and minimum subnormal exponent -149. Position150 is its last
			// possible rounding bit, not a geometric tolerance or solver cap.
			for (unsigned position = 1; position <= roundPosition; ++position) {
				if (!ShiftProjectionInteger(remainder)) return false;
				const bool bit = CompareProjectionMagnitude(remainder, denominator) >= 0;
				if (bit && !AddProjectionInteger(remainder, denominator, true)) return false;
				if (!leading && bit) {
					leading = position;
					if (leading <= 126) roundPosition = leading + 24;
				}
				if (position < roundPosition) {
					if (leading) significand = (significand << 1) | (bit ? 1u : 0u);
				} else {
					// Round the exact rational to nearest, ties to even.
					if (bit && (remainder.used || (significand & 1u))) ++significand;
					std::uint32_t bits = significand;
					if (leading && leading <= 126) {
						unsigned exponent = 127 - leading;
						if (significand == 0x1000000u) { significand >>= 1; ++exponent; }
						bits = (exponent << 23) | (significand & 0x7fffffu);
					}
					output = std::bit_cast<float>(bits);
					return true;
				}
			}
			return false;
		}

		bool ConvertCertifiedProjectionWeights(EpaProjectionWorkspace& workspace, Vec3f& lambdas)
		{
			auto& w = workspace.value;
			if (!w[10].used || w[10].negative || w[11].negative || w[12].negative || w[13].negative)
				return false;
			for (unsigned i = 0; i < 3; ++i) {
				if (!ProjectionRatioToFloat(w[11 + i], w[10], w[3], lambdas[i])) return false;
			}
			return true;
		}

		bool ReconstructCertifiedFace(const point_t& a, const point_t& b, const point_t& c,
			const Vec3f& finalNormal, float finalOriginDistance, const Vec3f& lambdas,
			EpaFinalProjection& projection, Vec3f& ptOnA, Vec3f& ptOnB)
		{
			projection = EvaluateFinalProjection(a, b, c, lambdas, finalNormal, finalOriginDistance);
			return ReconstructFinalProjection(a, b, c, projection, ptOnA, ptOnB) ==
				EpaTermination::Converged;
		}

		struct EpaProjectionCertificate
		{
			Vec3f lambdas{};
			int face = -1;
		};

		// Only this call owns the integer workspace. Its frame must return before
		// the caller evaluates the floating projection and reconstructs witnesses.
		__declspec(noinline) bool FindFinalProjectionCertificate(std::span<const point_t> points,
			std::span<const tri_t> triangles, int selected, const Vec3f& finalNormal,
			unsigned gridShift, EpaProjectionCertificate& certificate)
		{
			const auto validFace = [points](const tri_t& tri) {
				return tri.a >= 0 && tri.b >= 0 && tri.c >= 0 &&
					static_cast<std::size_t>(tri.a) < points.size() &&
					static_cast<std::size_t>(tri.b) < points.size() &&
					static_cast<std::size_t>(tri.c) < points.size() &&
					tri.a != tri.b && tri.b != tri.c && tri.c != tri.a;
			};
			const tri_t& source = triangles[selected];
			if (!validFace(source)) return false;
			EpaProjectionWorkspace workspace{};
			workspace.gridShift = gridShift;
			auto& w = workspace.value;
			if (!BuildProjectionNormal(points[source.a].xyz, points[source.b].xyz,
				points[source.c].xyz, workspace) ||
				!ProjectionPlaneValue(finalNormal, Vec3f(0.0f), workspace) ||
				!w[10].used || w[10].negative ||
				!ProjectionPlaneValue(Vec3f(0.0f), points[source.a].xyz, workspace) ||
				(w[10].used && !w[10].negative)) return false;
			// The recovery certifies its supporting plane. It does not replace
			// the existing EPA topology checks with a full convex-hull validator.
			for (const point_t& point : points) {
				if (!ProjectionPlaneValue(point.xyz, points[source.a].xyz, workspace) ||
					(w[10].used && !w[10].negative)) return false;
			}
			if (!ProjectionFaceCoordinates(points[source.a], points[source.b], points[source.c], workspace) ||
				!w[10].used || w[10].negative ||
				(!w[11].negative && !w[12].negative && !w[13].negative)) return false;
			for (std::size_t i = 0; i < triangles.size(); ++i) {
				if (i == static_cast<std::size_t>(selected)) continue;
				const tri_t& tri = triangles[i];
				if (!validFace(tri)) return false;
				bool coplanar = true;
				const int indices[3] = {tri.a, tri.b, tri.c};
				for (const int index : indices) {
					if (!ProjectionPlaneValue(points[index].xyz, points[source.a].xyz, workspace))
						return false;
					if (w[10].used) { coplanar = false; break; }
				}
				if (!coplanar) continue;
				if (!ProjectionFaceCoordinates(points[tri.a], points[tri.b], points[tri.c], workspace))
					return false;
				if (!w[10].used || w[10].negative ||
					w[11].negative || w[12].negative || w[13].negative) continue;
				if (!w[10].used || w[10].negative || w[11].negative || w[12].negative || w[13].negative)
					return false;
				for (unsigned axis = 0; axis < 3; ++axis) {
					if (!ProjectionRatioToFloat(w[11 + axis], w[10], w[3], certificate.lambdas[axis]))
						return false;
				}
				certificate.face = static_cast<int>(i);
				return true;
			}
			return false;
		}

		__declspec(noinline) bool TryRecoverFinalProjection(std::span<const point_t> points, std::span<const tri_t> triangles,
			int selected, const Vec3f& finalNormal, float finalOriginDistance,
			const EpaFinalProjection& original, Vec3f& ptOnA, Vec3f& ptOnB,
			EpaFinalProjection* recoveredProjection = nullptr, int* recoveredFace = nullptr)
		{
			const std::uint32_t predicates = FinalProjectionFailedPredicates(original);
			if (!predicates || (predicates & ~56u) || selected < 0 ||
				static_cast<std::size_t>(selected) >= triangles.size()) return false;
			// Finish the complete grid scan before entering the integer workspace.
			unsigned gridShift = 277;
			if (!InitializeProjectionGrid(points, finalNormal, gridShift)) return false;
			EpaProjectionCertificate certificate;
			if (!FindFinalProjectionCertificate(points, triangles, selected, finalNormal, gridShift, certificate))
				return false;
			const tri_t& tri = triangles[certificate.face];
			EpaFinalProjection projection{};
			if (!ReconstructCertifiedFace(points[tri.a], points[tri.b], points[tri.c],
				finalNormal, finalOriginDistance, certificate.lambdas, projection, ptOnA, ptOnB)) return false;
			if (recoveredProjection) *recoveredProjection = projection;
			if (recoveredFace) *recoveredFace = certificate.face;
			return true;
		}

		// Diagnostic certificate uses the same arithmetic as production B, with
		// the same non-overlapping workspace and floating-reconstruction lifetimes.
		__declspec(noinline) bool CertifyPrimitiveProjection(const point_t& a, const point_t& b,
			const point_t& c, Vec3f& lambdas)
		{
			EpaProjectionWorkspace workspace{};
			return IncludeProjectionGrid(a.xyz, workspace.gridShift) &&
				IncludeProjectionGrid(b.xyz, workspace.gridShift) &&
				IncludeProjectionGrid(c.xyz, workspace.gridShift) &&
				BuildProjectionNormal(a.xyz, b.xyz, c.xyz, workspace) &&
				ProjectionFaceCoordinates(a, b, c, workspace) &&
				ConvertCertifiedProjectionWeights(workspace, lambdas);
		}

		__declspec(noinline) bool ReconstructCertifiedPrimitiveFace(const point_t& a, const point_t& b,
			const point_t& c, const Vec3f& normal, float originDistance, EpaFinalProjection& projection,
			Vec3f& ptOnA, Vec3f& ptOnB)
		{
			Vec3f lambdas;
			return CertifyPrimitiveProjection(a, b, c, lambdas) &&
				ReconstructCertifiedFace(a, b, c, normal, originDistance, lambdas, projection, ptOnA, ptOnB);
		}

		// P20_EPA_OBSERVATION_BEGIN
		void RecordEpaProjection(const std::vector<point_t>& points,
			const std::vector<tri_t>& triangles, PhysicsDiagnostic::EpaProjectionEvent& value) noexcept
		{
			auto* sink = PhysicsDiagnostic::queryObserver;
			if (!sink || !sink->epaProjection) return;
			const auto start = PhysicsDiagnostic::Nanoseconds();
			value.iteration = sink->iteration;
			value.pointCount = points.size();
			value.faceCount = triangles.size();
			value.overflow = points.size() > PhysicsDiagnostic::EpaCaptureMaxPoints ||
				triangles.size() > PhysicsDiagnostic::EpaCaptureMaxFaces;
			std::array<PhysicsDiagnostic::EpaPoint, PhysicsDiagnostic::EpaCaptureMaxPoints> pointCopy{};
			std::array<PhysicsDiagnostic::EpaFace, PhysicsDiagnostic::EpaCaptureMaxFaces> faceCopy{};
			if (!value.overflow) {
				for (std::size_t i = 0; i < points.size(); ++i) {
					pointCopy[i] = {points[i].xyz, points[i].ptA, points[i].ptB};
				}
				for (std::size_t i = 0; i < triangles.size(); ++i) {
					faceCopy[i] = {triangles[i].a, triangles[i].b, triangles[i].c};
				}
				value.points = {pointCopy.data(), points.size()};
				value.faces = {faceCopy.data(), triangles.size()};
			}
			sink->epaProjection(sink->context, value);
			value.points = {};
			value.faces = {};
			sink->observationNs += PhysicsDiagnostic::Nanoseconds() - start;
		}
		// P20_EPA_OBSERVATION_END

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
		const point_t simplexPoints[4], Vec3f& ptOnA, Vec3f& ptOnB,
		EpaDiagnostics* diagnostics, unsigned maxIterations) {
		GE_PHYSICS_PROFILE_SCOPE(epaTimeNs);
		GE_PHYSICS_PROFILE_ADD(epaCallCount, 1);
		EpaQueryControl query(diagnostics);
		ptOnA = ptOnB = Vec3f(0.0f);
		if (!HasValidCollisionShapes(bodyA, bodyB) || !Math::IsFinite(bias) || bias < 0.0f) {
			return query.Finish(EpaTermination::InvalidInput);
		}
		std::vector< point_t > points;
		std::vector< tri_t > triangles;
		std::vector< edge_t > danglingEdges;

		Vec3f center(0.0f);
		for (int i = 0; i < 4; i++) {
			if (!IsValidSupportPoint(simplexPoints[i])) {
				return query.Finish(EpaTermination::InvalidSimplex);
			}
			points.push_back(simplexPoints[i]);
			center += simplexPoints[i].xyz;
		}
		center *= 0.25f;
		if (!Math::IsFinite(center) || !IsNonDegenerateTetrahedron(points)) {
			return query.Finish(EpaTermination::InvalidSimplex);
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
				return query.Finish(EpaTermination::InvalidFace);
			}

			// The unused point is always on the negative/inside of the triangle.. make sure the normal points away
			if (dist > 0.0f) {
				std::swap(tri.a, tri.b);
			}

			triangles.push_back(tri);
		}
		if (!IsClosedValidPolytope(triangles, points)) {
			return query.Finish(EpaTermination::InvalidTopology);
		}

		//
		//	Expand the simplex to find the closest face of the CSO to the origin
		//
		bool converged = false;
		EpaTermination convergenceReason = EpaTermination::Converged;
		// P20_EPA_OBSERVATION_BEGIN
		PhysicsDiagnostic::EpaProjectionEvent projectionObservation{};
		// P20_EPA_OBSERVATION_END
		const unsigned limit = std::min(maxIterations, EpaMaxIterations);
		for (unsigned iteration = 0; iteration < limit; ++iteration) {
			++query.result.iterations;
			if (!IsClosedValidPolytope(triangles, points)) {
				return query.Finish(EpaTermination::InvalidTopology);
			}
			const int idx = ClosestTriangle(triangles, points);
			if (idx < 0 || static_cast<std::size_t>(idx) >= triangles.size()) {
				return query.Finish(EpaTermination::InvalidFace);
			}

			Vec3f normal;
			float originDistance = 0.0f;
			if (!TrySignedDistanceToTriangle(triangles[idx], Vec3f(0.0f), points,
				originDistance, &normal)) {
				return query.Finish(EpaTermination::InvalidFace);
			}

			const point_t newPt = Support(bodyA, bodyB, normal, bias);
			if (!IsValidSupportPoint(newPt)) {
				return query.Finish(EpaTermination::InvalidSupport);
			}

			float expansionDistance = 0.0f;
			if (!TrySignedDistanceToTriangle(triangles[idx], newPt.xyz, points,
				expansionDistance)) {
				return query.Finish(EpaTermination::InvalidFace);
			}
			const float convergenceTolerance = std::max(Math::NumericalEpsilon,
				EpaRelativeConvergenceTolerance * std::max(std::fabs(originDistance), bias));
			// P20_EPA_OBSERVATION_BEGIN
			if (PhysicsDiagnostic::queryObserver && PhysicsDiagnostic::queryObserver->epaProjection) {
				projectionObservation.lastSupport = {newPt.xyz, newPt.ptA, newPt.ptB};
				projectionObservation.lastSupportNormal = normal;
				projectionObservation.lastOriginDistance = originDistance;
				projectionObservation.lastExpansionDistance = expansionDistance;
				projectionObservation.lastConvergenceTolerance = convergenceTolerance;
				projectionObservation.lastSelectedFace = idx;
			}
			// P20_EPA_OBSERVATION_END

			// if w already exists, then just stop
			// because it means we can't expand any further
			if (HasPoint(newPt.xyz, points)) {
				convergenceReason = EpaTermination::DuplicateSupport;
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
				return query.Finish(EpaTermination::InvalidHorizon);
			}

			// Find Dangling Edges
			danglingEdges.clear();
			if (!FindDanglingEdges(danglingEdges, candidateTriangles, candidatePoints) ||
				danglingEdges.empty()) {
				return query.Finish(EpaTermination::InvalidHorizon);
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
					return query.Finish(EpaTermination::InvalidFace);
				}
				if (dist > 0.0f) {
					std::swap(triangle.b, triangle.c);
				}

				Vec3f triangleNormal;
				if (!TryTriangleNormal(triangle, candidatePoints, triangleNormal)) {
					return query.Finish(EpaTermination::InvalidFace);
				}
				candidateTriangles.push_back(triangle);
			}

			if (!IsClosedValidPolytope(candidateTriangles, candidatePoints)) {
				return query.Finish(EpaTermination::InvalidTopology);
			}
			points.swap(candidatePoints);
			triangles.swap(candidateTriangles);
		}
		if (!converged || !IsClosedValidPolytope(triangles, points)) {
			return query.Finish(converged ? EpaTermination::InvalidTopology : EpaTermination::IterationLimit);
		}

		// Get the projection of the origin on the closest triangle
		const int idx = ClosestTriangle(triangles, points);
		if (idx < 0 || static_cast<std::size_t>(idx) >= triangles.size()) {
			return query.Finish(EpaTermination::InvalidFace);
		}
		const tri_t& tri = triangles[idx];
		Vec3f finalNormal;
		float finalOriginDistance = 0.0f;
		if (!TrySignedDistanceToTriangle(tri, Vec3f(0.0f), points,
			finalOriginDistance, &finalNormal)) {
			return query.Finish(EpaTermination::InvalidFace);
		}
		Vec3f ptA_w = points[tri.a].xyz;
		Vec3f ptB_w = points[tri.b].xyz;
		Vec3f ptC_w = points[tri.c].xyz;
		Vec3f lambdas = BarycentricCoordinates(ptA_w, ptB_w, ptC_w, Vec3f(0.0f));
		const EpaFinalProjection projection = EvaluateFinalProjection(points[tri.a], points[tri.b],
			points[tri.c], lambdas, finalNormal, finalOriginDistance);
		// P20_EPA_OBSERVATION_BEGIN
		if (PhysicsDiagnostic::queryObserver && PhysicsDiagnostic::queryObserver->epaProjection) {
			projectionObservation.epaIterations = query.result.iterations;
			projectionObservation.selectedFace = idx;
			projectionObservation.convergenceReason = convergenceReason;
			projectionObservation.bias = bias;
			projectionObservation.normal = finalNormal;
			projectionObservation.originDistance = finalOriginDistance;
			projectionObservation.lambdas = lambdas;
			projectionObservation.lambdaSum = projection.lambdaSum;
			projectionObservation.projectedPoint = projection.projectedPoint;
			projectionObservation.expectedProjection = projection.expectedProjection;
			projectionObservation.projectionErrorSquared = projection.projectionErrorSquared;
			projectionObservation.projectionScaleSquared = projection.projectionScaleSquared;
			projectionObservation.failedPredicates = FinalProjectionFailedPredicates(projection);
			RecordEpaProjection(points, triangles, projectionObservation);
		}
		// P20_EPA_OBSERVATION_END
		const EpaTermination reconstruction = ReconstructFinalProjection(points[tri.a], points[tri.b],
			points[tri.c], projection, ptOnA, ptOnB);
		if (reconstruction == EpaTermination::InvalidProjection &&
			TryRecoverFinalProjection(points, triangles, idx, finalNormal, finalOriginDistance,
				projection, ptOnA, ptOnB)) {
			return query.Finish(convergenceReason);
		}
		if (reconstruction != EpaTermination::Converged) {
			return query.Finish(reconstruction);
		}
		return query.Finish(convergenceReason);
	}

	namespace PhysicsDiagnostic
	{
		ProjectionPrimitiveObservation EvaluateProjectionPrimitive(unsigned scenario)
		{
			ProjectionPrimitiveObservation result{};
			if (scenario > 1) return result;
			std::array<point_t, 4> points{};
			std::array<tri_t, 2> triangles{};
			if (scenario == 0) {
				points[0].xyz = Vec3f(-1.0f, 0.125f, -2.0f);
				points[1].xyz = Vec3f(3.0f, 0.125f, 2.0f);
				points[2].xyz = Vec3f(3.0f, 0.125f, -2.0f);
				points[3].xyz = Vec3f(-1.0f, 0.125f, 2.0f);
				triangles[0] = {0, 1, 2};
				triangles[1] = {0, 3, 1};
			} else {
				constexpr float h = 1.0f / 2048.0f;
				points[0].xyz = Vec3f(-h, 0.125f, -h);
				points[1].xyz = Vec3f(0.0f, 0.125f, h);
				points[2].xyz = Vec3f(h, 0.125f, -h);
				triangles[0] = {0, 1, 2};
			}
			for (point_t& point : points) point.ptA = point.xyz;
			// Both prescribed faces have this exact oriented plane. These controls
			// exercise final reconstruction, not public-query hull reachability.
			result.selectedFace = 0;
			result.normal = Vec3f(0.0f, 1.0f, 0.0f);
			result.originDistance = -0.125f;
			const tri_t& tri = triangles[0];
			const Vec3f lambdas = BarycentricCoordinates(points[tri.a].xyz, points[tri.b].xyz,
				points[tri.c].xyz, Vec3f(0.0f));
			EpaFinalProjection projection = EvaluateFinalProjection(points[tri.a], points[tri.b],
				points[tri.c], lambdas, result.normal, result.originDistance);
			result.termination = ReconstructFinalProjection(points[tri.a], points[tri.b], points[tri.c],
				projection, result.witnessA, result.witnessB);
			if (scenario == 0 && result.termination == EpaTermination::InvalidProjection) {
				if (TryRecoverFinalProjection(points, triangles, 0, result.normal, result.originDistance,
					projection, result.witnessA, result.witnessB, &projection, &result.selectedFace))
					result.termination = EpaTermination::Converged;
			} else if (scenario == 1) {
				// Exercise the same certified-face component used by production B.
				// This does not enable selected-inside production recovery.
				if (ReconstructCertifiedPrimitiveFace(points[tri.a], points[tri.b], points[tri.c],
					result.normal, result.originDistance, projection, result.witnessA, result.witnessB))
					result.termination = EpaTermination::Converged;
			}
			result.lambdas = projection.lambdas;
			result.lambdaSum = projection.lambdaSum;
			result.projectedPoint = projection.projectedPoint;
			result.expectedProjection = projection.expectedProjection;
			result.projectionErrorSquared = projection.projectionErrorSquared;
			result.projectionScaleSquared = projection.projectionScaleSquared;
			result.failedPredicates = FinalProjectionFailedPredicates(projection);
			if (result.termination == EpaTermination::Converged) {
				result.penetrationDistance = glm::length(result.witnessB - result.witnessA);
			}
			return result;
		}
	}



}
