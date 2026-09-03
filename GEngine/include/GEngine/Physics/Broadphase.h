#pragma once

#include "Bounds.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace GEngine
{
	struct collisionPair_t
	{
		int a;
		int b;

		bool operator == (const collisionPair_t& rhs) const
		{
			return (((a == rhs.a) && (b == rhs.b)) || ((a == rhs.b) && (b == rhs.a)));
		}
		bool operator != (const collisionPair_t& rhs) const
		{
			return !(*this == rhs);
		}
	};

	class RigidBody3D;

	struct BroadphaseStats
	{
		std::uint64_t axisOverlapCount{};
		std::uint64_t aabbRejectedCount{};
		std::uint64_t staticPairRejectedCount{};
		std::uint64_t maskRejectedCount{};
		std::uint64_t insertionSortSwapCount{};
		std::uint64_t fullSortCount{};
	};

	class SweepAndPruneBroadphase
	{
	public:
		void FindPairs(const std::vector<RigidBody3D*>& bodies,
			std::vector<collisionPair_t>& finalPairs, float dtSeconds);
		void Clear();

		const BroadphaseStats& GetLastStats() const { return m_LastStats; }
		std::size_t GetEndpointCapacity() const { return m_Endpoints.capacity(); }
		std::size_t GetPairScratchCapacity() const { return m_ActiveBodies.capacity(); }

	private:
		struct Endpoint
		{
			int bodyIndex{};
			float value{};
			bool isMinimum{};
		};

		static bool EndpointLess(const Endpoint& lhs, const Endpoint& rhs);
		bool HasSameBodies(const std::vector<RigidBody3D*>& bodies) const;
		void Rebuild(const std::vector<RigidBody3D*>& bodies);
		void UpdateSweptBounds(const std::vector<RigidBody3D*>& bodies, float dtSeconds);
		void UpdateEndpointValues();
		void IncrementalSort();
		void BuildPairs(const std::vector<RigidBody3D*>& bodies,
			std::vector<collisionPair_t>& finalPairs);

		std::vector<const RigidBody3D*> m_Bodies;
		std::vector<Endpoint> m_Endpoints;
		std::vector<Bounds> m_SweptBounds;
		std::vector<int> m_ActiveBodies;
		BroadphaseStats m_LastStats;
	};

	// Compatibility entry point for callers that do not own persistent broadphase state.
	void BroadPhase(const std::vector<RigidBody3D*>& bodies,
		std::vector<collisionPair_t>& finalPairs, float dtSeconds);
}
