#pragma once

#include "Manifold.h"
#include <array>
#include <cstddef>
#include <vector>

namespace GEngine
{
	struct ContactIslandPair
	{
		RigidBodyIdentity bodyA;
		RigidBodyIdentity bodyB;
		friend bool operator==(const ContactIslandPair&, const ContactIslandPair&) = default;
	};

	struct ContactIsland
	{
		std::vector<RigidBodyIdentity> dynamicBodies;
		// Zero effective mass bodies may border several islands, but never join them.
		std::vector<RigidBodyIdentity> boundaryBodies;
		std::vector<ContactIslandPair> contactPairs;
		friend bool operator==(const ContactIsland&, const ContactIsland&) = default;
	};

	// Reusable capacity only. Every node, edge, parent and output cursor is replaced
	// on each build; no pointers or connectivity decisions survive into the next tick.
	struct ContactIslandScratch
	{
		struct Node
		{
			RigidBodyIdentity identity;
			bool dynamic;
			std::size_t parent{};
			std::size_t island{};
		};
		struct Counts
		{
			std::size_t dynamics{}, boundaries{}, pairs{};
		};
		std::vector<Node> nodes;
		std::vector<std::array<std::size_t, 2>> edges;
		std::vector<Counts> counts;
	};

	// Pure graph construction over live, uniquely owned bodies and validated resting
	// manifolds. Call after expiry/PreSolve; this helper does not validate geometry.
	// Positive-mass dynamics (including passive sleeping flags) are graph vertices;
	// Static, Kinematic and zero-effective-mass bodies are non-propagating boundaries.
	// Isolated dynamics form singleton islands. Positive TOIs are not resting edges.
	// All output uses (slot, generation) ordering, never pointers or container order.
	// Results own only identities: callers must rebuild after topology/type changes.
	void BuildContactIslands(const std::vector<RigidBody3D*>& bodies,
		const std::vector<Manifold>& manifolds, std::vector<ContactIsland>& islands,
		ContactIslandScratch& scratch);

	// Owning value API retained for callers that do not keep scratch between builds.
	std::vector<ContactIsland> BuildContactIslands(
		const std::vector<RigidBody3D*>& bodies, const std::vector<Manifold>& manifolds,
		std::vector<ContactIsland> islands = {});
}
