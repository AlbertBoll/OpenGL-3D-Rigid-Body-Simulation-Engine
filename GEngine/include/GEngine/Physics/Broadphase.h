#pragma once

namespace GEngine
{
	struct collisionPair_t {
		int a;
		int b;

		bool operator == (const collisionPair_t& rhs) const {
			return (((a == rhs.a) && (b == rhs.b)) || ((a == rhs.b) && (b == rhs.a)));
		}
		bool operator != (const collisionPair_t& rhs) const {
			return !(*this == rhs);
		}
	};

	class RigidBody3D;

	void BroadPhase(const RigidBody3D* bodies, const int num, std::vector< collisionPair_t >& finalPairs, const float dt_sec);

	void BroadPhase(const std::vector<RigidBody3D*>& bodies, std::vector< collisionPair_t >& finalPairs, const float dt_sec);
}