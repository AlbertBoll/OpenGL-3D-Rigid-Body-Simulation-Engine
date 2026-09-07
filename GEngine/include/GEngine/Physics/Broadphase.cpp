#include "gepch.h"
#include "Broadphase.h"

#include "PhysicsBody.h"
#include "Shape.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace GEngine
{
	namespace
	{
		constexpr float BroadphaseMargin = 0.01f;
		// Round an enclosure outward without introducing infinite SAP endpoints.
		float OutwardBound(double value, bool upper)
		{
			const float limit = std::numeric_limits<float>::max();
			value = std::clamp(value, -static_cast<double>(limit), static_cast<double>(limit));
			float result = static_cast<float>(value);
			if ((upper && result < value) || (!upper && result > value))
			{
				result = std::nextafter(result, upper ? limit : -limit);
			}
			return result;
		}

		void ExpandForRotation(Bounds& bounds, const RigidBody3D& body, const Vec3f& displacement, float dtSeconds)
		{
			const Vec3f omega = body.GetAngularVelocity();
			if (dtSeconds == 0.0f || omega == Vec3f(0.0f) || !body.m_Shape ||
				!body.m_Shape->IsValid() || body.m_Shape->GetShapeType() == ShapeType::Sphere)
			{
				return;
			}

			// Every local AABB point is within this radius of the shape COM. Rotation
			// preserves that distance, including intermediate poses and complete turns.
			// Use the full envelope: gyroscopic integration can change angular speed and
			// axis, so an initial |omega| * dt bound alone is not conservative here.
			const Bounds local = body.m_Shape->GetBounds();
			const glm::dvec3 localCenter(body.m_Shape->GetCenterOfMass());
			const glm::dvec3 extent = glm::max(glm::abs(glm::dvec3(local.mins) - localCenter),
				glm::abs(glm::dvec3(local.maxs) - localCenter));
			const double radius = glm::length(extent);
			const Vec3f center = body.GetCenterOfMassWorldSpace();
			for (int axis = 0; axis < 3; ++axis)
			{
				const double start = center[axis];
				const double end = start + displacement[axis];
				bounds.mins[axis] = std::min(bounds.mins[axis], OutwardBound(std::min(start, end) - radius, false));
				bounds.maxs[axis] = std::max(bounds.maxs[axis], OutwardBound(std::max(start, end) + radius, true));
			}
		}

		bool CollisionMasksOverlap(const RigidBody3D& lhs, const RigidBody3D& rhs)
		{
			return (lhs.m_CollisionMask & rhs.m_CollisionLayer) != 0u &&
				(rhs.m_CollisionMask & lhs.m_CollisionLayer) != 0u;
		}
	}

	bool SweepAndPruneBroadphase::EndpointLess(const Endpoint& lhs, const Endpoint& rhs)
	{
		if (lhs.value != rhs.value)
		{
			return lhs.value < rhs.value;
		}
		if (lhs.isMinimum != rhs.isMinimum)
		{
			// A touching interval is a candidate, so minima precede maxima at equal values.
			return lhs.isMinimum;
		}
		return lhs.bodyIndex < rhs.bodyIndex;
	}

	bool SweepAndPruneBroadphase::HasSameBodies(const std::vector<RigidBody3D*>& bodies) const
	{
		if (m_Bodies.size() != bodies.size())
		{
			return false;
		}

		for (std::size_t index = 0; index < bodies.size(); ++index)
		{
			if (m_Bodies[index] != bodies[index])
			{
				return false;
			}
		}
		return true;
	}

	void SweepAndPruneBroadphase::Rebuild(const std::vector<RigidBody3D*>& bodies)
	{
		m_Bodies.assign(bodies.begin(), bodies.end());
		m_SweptBounds.resize(bodies.size());
		m_Endpoints.resize(bodies.size() * 2);
		m_ActiveBodies.reserve(bodies.size());

		for (std::size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex)
		{
			m_Endpoints[bodyIndex * 2] = {
				static_cast<int>(bodyIndex), 0.0f, true
			};
			m_Endpoints[bodyIndex * 2 + 1] = {
				static_cast<int>(bodyIndex), 0.0f, false
			};
		}
	}

	void SweepAndPruneBroadphase::UpdateSweptBounds(
		const std::vector<RigidBody3D*>& bodies, float dtSeconds)
	{
		const Vec3f margin(BroadphaseMargin);
		for (std::size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex)
		{
			const RigidBody3D& body = *bodies[bodyIndex];
			Bounds bounds = body.GetWorldBounds();
			const Vec3f initialMins = bounds.mins;
			const Vec3f initialMaxs = bounds.maxs;
			const Vec3f displacement = body.m_LinearVelocity * dtSeconds;
			bounds.Expand(initialMins + displacement);
			bounds.Expand(initialMaxs + displacement);
			ExpandForRotation(bounds, body, displacement, dtSeconds);
			bounds.Expand(bounds.mins - margin);
			bounds.Expand(bounds.maxs + margin);
			m_SweptBounds[bodyIndex] = bounds;
		}
	}

	void SweepAndPruneBroadphase::UpdateEndpointValues()
	{
		for (Endpoint& endpoint : m_Endpoints)
		{
			const Bounds& bounds = m_SweptBounds[endpoint.bodyIndex];
			endpoint.value = endpoint.isMinimum ? bounds.mins.x : bounds.maxs.x;
		}
	}

	void SweepAndPruneBroadphase::IncrementalSort()
	{
		for (std::size_t index = 1; index < m_Endpoints.size(); ++index)
		{
			std::size_t insertionIndex = index;
			while (insertionIndex > 0 &&
				EndpointLess(m_Endpoints[insertionIndex], m_Endpoints[insertionIndex - 1]))
			{
				std::swap(m_Endpoints[insertionIndex], m_Endpoints[insertionIndex - 1]);
				--insertionIndex;
				++m_LastStats.insertionSortSwapCount;
			}
		}
	}

	void SweepAndPruneBroadphase::BuildPairs(const std::vector<RigidBody3D*>& bodies,
		std::vector<collisionPair_t>& finalPairs)
	{
		m_ActiveBodies.clear();
		for (const Endpoint& endpoint : m_Endpoints)
		{
			if (!endpoint.isMinimum)
			{
				const auto active = std::find(
					m_ActiveBodies.begin(), m_ActiveBodies.end(), endpoint.bodyIndex);
				if (active != m_ActiveBodies.end())
				{
					m_ActiveBodies.erase(active);
				}
				continue;
			}

			const RigidBody3D& bodyB = *bodies[endpoint.bodyIndex];
			for (const int activeBodyIndex : m_ActiveBodies)
			{
				++m_LastStats.axisOverlapCount;
				const RigidBody3D& bodyA = *bodies[activeBodyIndex];

				if (bodyA.Type == BodyType::Static && bodyB.Type == BodyType::Static)
				{
					++m_LastStats.staticPairRejectedCount;
					continue;
				}
				if (!CollisionMasksOverlap(bodyA, bodyB))
				{
					++m_LastStats.maskRejectedCount;
					continue;
				}
				if (!m_SweptBounds[activeBodyIndex].DoesIntersect(m_SweptBounds[endpoint.bodyIndex]))
				{
					++m_LastStats.aabbRejectedCount;
					continue;
				}

				finalPairs.push_back({ activeBodyIndex, endpoint.bodyIndex });
			}
			m_ActiveBodies.push_back(endpoint.bodyIndex);
		}
	}

	void SweepAndPruneBroadphase::FindPairs(const std::vector<RigidBody3D*>& bodies,
		std::vector<collisionPair_t>& finalPairs, float dtSeconds)
	{
		finalPairs.clear();
		m_LastStats = {};

		const bool rebuild = !HasSameBodies(bodies);
		if (rebuild)
		{
			Rebuild(bodies);
		}

		UpdateSweptBounds(bodies, dtSeconds);
		UpdateEndpointValues();
		if (rebuild)
		{
			std::sort(m_Endpoints.begin(), m_Endpoints.end(), EndpointLess);
			++m_LastStats.fullSortCount;
		}
		else
		{
			IncrementalSort();
		}
		BuildPairs(bodies, finalPairs);
	}

	void SweepAndPruneBroadphase::Clear()
	{
		m_Bodies.clear();
		m_Endpoints.clear();
		m_SweptBounds.clear();
		m_ActiveBodies.clear();
		m_LastStats = {};
	}

	void BroadPhase(const std::vector<RigidBody3D*>& bodies,
		std::vector<collisionPair_t>& finalPairs, float dtSeconds)
	{
		SweepAndPruneBroadphase broadphase;
		broadphase.FindPairs(bodies, finalPairs, dtSeconds);
	}
}
