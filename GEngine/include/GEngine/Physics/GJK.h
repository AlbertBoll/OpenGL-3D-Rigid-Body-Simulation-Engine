#pragma once
#include <Math/Math.h>


namespace GEngine
{
	class RigidBody3D;
	enum class GjkContactStatus
	{
		Separated,
		Contact,
		Failed
	};


	// Per-query diagnostics are available even when profiling is disabled. The iteration
	// count covers simplex-search steps only, excluding bounded tetrahedron seeding/EPA.
	enum class GjkTermination
	{
		InvalidInput,
		InvalidSupport,
		InvalidSimplex,
		SeparatingAxis,
		DuplicateSupport,
		NoProgress,
		OriginReached,
		IterationLimit,
		ContactExpansionFailed
	};

	// NotRun distinguishes GJK separation/seeding failure from an attempted EPA query.
	// Converged and DuplicateSupport are successful only after output validation.
	// DuplicateSupport retains the existing approximate 0.001-world-unit criterion;
	// it does not assert that the relative expansion-gap convergence test passed.
	enum class EpaTermination
	{
		NotRun,
		Converged,
		DuplicateSupport,
		InvalidInput,
		InvalidSimplex,
		InvalidFace,
		InvalidTopology,
		InvalidSupport,
		InvalidHorizon,
		InvalidProjection,
		InvalidWitness,
		IterationLimit
	};

	struct EpaDiagnostics
	{
		EpaTermination termination{ EpaTermination::NotRun };
		unsigned iterations{};
	};

	inline constexpr unsigned EpaMaxIterations = 64;

	struct GjkDiagnostics
	{
		GjkTermination termination{ GjkTermination::InvalidInput };
		unsigned iterations{};
		EpaDiagnostics epa{};
	};

	inline constexpr unsigned GjkMaxIterations = 64;
	// A caller may reduce the search budget (including zero), but cannot raise the cap.
	// Exhaustion returns false/Failed, or zero closest-point outputs. Check diagnostics
	// to distinguish a failed distance query from valid coincident witnesses.
	// DuplicateSupport/NoProgress retain the existing approximate separation semantics.
	// Progress uses a relative squared-distance tolerance; contact/duplicate geometric
	// thresholds remain unchanged. Validated shape scales: 0.1 through 100 units.

	using namespace Math;

	Vec2f SignedVolume1D(const Vec3f& s1, const Vec3f& s2);
	Vec3f SignedVolume2D(const Vec3f& s1, const Vec3f& s2, const Vec3f& s3);
	Vec4f SignedVolume3D(const Vec3f& s1, const Vec3f& s2, const Vec3f& s3, const Vec3f& s4);
	Vec3f BarycentricCoordinates(Vec3f s1, Vec3f s2, Vec3f s3, const Vec3f& point);

	bool GJK_DoesIntersect(const RigidBody3D* bodyA, const RigidBody3D* bodyB,
		GjkDiagnostics* diagnostics = nullptr, unsigned maxIterations = GjkMaxIterations);
	bool GJK_DoesIntersect(const RigidBody3D* bodyA, const RigidBody3D* bodyB, const float bias, Vec3f& ptOnA, Vec3f& ptOnB,
		GjkDiagnostics* diagnostics = nullptr, unsigned maxIterations = GjkMaxIterations,
		unsigned maxEpaIterations = EpaMaxIterations);
	// EPA budgets may only reduce its 64-step cap. Failure clears both witnesses.
	// EPA iterations count expansion attempts; initial tetrahedron setup is excluded.
	GjkContactStatus GJK_GetContact(const RigidBody3D* bodyA, const RigidBody3D* bodyB,
		float bias, Vec3f& ptOnA, Vec3f& ptOnB,
		GjkDiagnostics* diagnostics = nullptr, unsigned maxIterations = GjkMaxIterations,
		unsigned maxEpaIterations = EpaMaxIterations);
	void GJK_ClosestPoints(const RigidBody3D* bodyA, const RigidBody3D* bodyB, Vec3f& ptOnA, Vec3f& ptOnB,
		GjkDiagnostics* diagnostics = nullptr, unsigned maxIterations = GjkMaxIterations);
	void TestSignedVolumeProjection();
}
