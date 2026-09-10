#include"gepch.h"

#include "PhysicsSystem.h"
//#include"PhysicsBody.h"
#include "ShapeSphere.h"
#include "BoxContact.h"
#include "PhysicsWorld.h"
#include "Broadphase.h"
#include "GJK.h"
#include "PhysicsProfile.h"
#include <Core/Timer.h>
#include <cmath>
#include <limits>
#include <utility>
#include <span>

namespace GEngine
{
	// A small phase-local timer keeps attribution inside the sleep integration boundary.
	class SleepPathTimer
	{
	public:
#ifdef GE_ENABLE_PHYSICS_PROFILING
		explicit SleepPathTimer(std::uint64_t& output)
			: m_Output(output), m_Start(std::chrono::steady_clock::now()) {}
		~SleepPathTimer() { Stop(); }
		void Stop()
		{
			if (!m_Running) return;
			m_Output += std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - m_Start).count();
			m_Running = false;
		}
	private:
		std::uint64_t& m_Output;
		std::chrono::steady_clock::time_point m_Start;
		bool m_Running{ true };
#else
		explicit SleepPathTimer(std::uint64_t&) {}
		void Stop() {}
#endif
	};

	namespace ContactIslandDetail
	{
		template<class T> void Reserve(std::vector<T>& values, std::size_t size)
		{
			if (size <= values.capacity()) return;
			GE_PHYSICS_PROFILE_ADD(contactIslandCapacityGrowthCount, 1);
			values.reserve(size);
		}

		template<class T> void Resize(std::vector<T>& values, std::size_t size)
		{
			if (size == values.size()) return;
			if (size > values.capacity()) GE_PHYSICS_PROFILE_ADD(contactIslandCapacityGrowthCount, 1);
			values.resize(size);
		}

		// Overwrite existing elements instead of invalidating/rebuilding every nested
		// vector. Contents are never used to decide this tick's graph membership.
		template<class T> void Write(std::vector<T>& values, std::size_t index, const T& value)
		{
			if (index < values.size()) values[index] = value;
			else
			{
				if (values.size() == values.capacity()) GE_PHYSICS_PROFILE_ADD(contactIslandCapacityGrowthCount, 1);
				values.push_back(value);
			}
		}
	}

	void BuildContactIslands(const std::vector<RigidBody3D*>& bodies,
		const std::vector<Manifold>& manifolds, std::vector<ContactIsland>& islands,
		ContactIslandScratch& scratch)
	{
		using namespace ContactIslandDetail;
		using Node = ContactIslandScratch::Node;
		GE_PHYSICS_PROFILE_SCOPE(contactIslandBuildTimeNs);
		GE_PHYSICS_PROFILE_SCOPE_NAMED(nodeTimer, contactIslandNodesTimeNs);
		const auto identityLess = [](RigidBodyIdentity a, RigidBodyIdentity b)
		{
			return a.GetSlot() < b.GetSlot() ||
				(a.GetSlot() == b.GetSlot() && a.GetGeneration() < b.GetGeneration());
		};
		auto& nodes = scratch.nodes;
		auto& edges = scratch.edges;
		auto& counts = scratch.counts;
		nodes.clear();
		edges.clear();
		counts.clear();
		Reserve(nodes, bodies.size());
		bool ordered = true;
		for (const RigidBody3D* body : bodies)
		{
			if (!body || !body->GetIdentity().IsValid()) continue;
			const auto identity = body->GetIdentity();
			if (!nodes.empty() && identityLess(identity, nodes.back().identity)) ordered = false;
			nodes.push_back({identity, body->GetInverseMass() > 0.0f});
		}
		if (!ordered)
		{
			GE_PHYSICS_PROFILE_ADD(contactIslandNodeSortCount, 1);
			std::sort(nodes.data(), nodes.data() + nodes.size(), [&](const Node& a, const Node& b)
				{ return identityLess(a.identity, b.identity); });
		}
		bool denseSlots = !nodes.empty();
		for (std::size_t i = 0; i < nodes.size(); ++i)
		{
			nodes[i].parent = i;
			if (i && nodes[i].identity.GetSlot() != nodes[i-1].identity.GetSlot() + 1) denseSlots = false;
		}
		GE_PHYSICS_PROFILE_STOP(nodeTimer);
		GE_PHYSICS_PROFILE_SCOPE_NAMED(edgeTimer, contactIslandEdgesTimeNs);
		const auto findNode = [&](RigidBodyIdentity identity)
		{
			if (nodes.empty()) return nodes.size();
			if (denseSlots)
			{
				// Dense-slot indexing is proven from this tick's sorted live identities.
				// Check the full identity even here: a reused slot is not the same body.
				const auto offset = identity.GetSlot() - nodes.front().identity.GetSlot();
				return offset < nodes.size() && nodes[static_cast<std::size_t>(offset)].identity == identity
					? static_cast<std::size_t>(offset) : nodes.size();
			}
			const auto* it = std::lower_bound(nodes.data(), nodes.data() + nodes.size(), identity,
				[&](const Node& node, RigidBodyIdentity key) { return identityLess(node.identity, key); });
			return it != nodes.data() + nodes.size() && it->identity == identity
				? static_cast<std::size_t>(it - nodes.data()) : nodes.size();
		};
		const auto edgeLess = [](const auto& a, const auto& b)
		{
			return a[0] < b[0] || (a[0] == b[0] && a[1] < b[1]);
		};
		const auto edgeEqual = [](const auto& a, const auto& b)
		{
			return a[0] == b[0] && a[1] == b[1];
		};
		Reserve(edges, manifolds.size());
		ordered = true;
		for (const Manifold& manifold : manifolds)
		{
			if (manifold.GetNumContacts() == 0) continue;
			// Read cached identity values only; never follow manifold body pointers.
			const std::size_t a = findNode(manifold.GetBodyAIdentity());
			const std::size_t b = findNode(manifold.GetBodyBIdentity());
			if (a == nodes.size() || b == nodes.size() || a == b ||
				(!nodes[a].dynamic && !nodes[b].dynamic)) continue;
			const std::array<std::size_t, 2> edge{std::min(a, b), std::max(a, b)};
			if (!edges.empty() && edgeLess(edge, edges.back())) ordered = false;
			edges.push_back(edge);
		}
		if (edges.size() > 1)
		{
			if (!ordered)
			{
				GE_PHYSICS_PROFILE_ADD(contactIslandEdgeSortCount, 1);
				std::sort(edges.data(), edges.data() + edges.size(), edgeLess);
			}
			Resize(edges, static_cast<std::size_t>(std::unique(edges.data(), edges.data() + edges.size(), edgeEqual) - edges.data()));
		}
		GE_PHYSICS_PROFILE_STOP(edgeTimer);
		GE_PHYSICS_PROFILE_SCOPE(contactIslandOutputTimeNs);
		if (edges.empty())
		{
			// Separated worlds need no DSU traversal or per-island output cursors.
			// Still overwrite every identity and retire all previous contacts/boundaries.
			std::size_t size = 0;
			for (const Node& node : nodes) if (node.dynamic) ++size;
			Resize(islands, size);
			std::size_t index = 0;
			for (const Node& node : nodes)
			{
				if (!node.dynamic) continue;
				auto& island = islands[index++];
				Resize(island.dynamicBodies, 1);
				island.dynamicBodies[0] = node.identity;
				Resize(island.boundaryBodies, 0);
				Resize(island.contactPairs, 0);
			}
			return;
		}
		const auto root = [&](std::size_t i)
		{
			while (nodes[i].parent != i)
			{
				nodes[i].parent = nodes[nodes[i].parent].parent;
				i = nodes[i].parent;
			}
			return i;
		};
		for (const auto& edge : edges)
		{
			if (!nodes[edge[0]].dynamic || !nodes[edge[1]].dynamic) continue;
			const std::size_t a = root(edge[0]), b = root(edge[1]);
			nodes[std::max(a, b)].parent = std::min(a, b);
		}
		std::size_t islandCount = 0;
		for (std::size_t i = 0; i < nodes.size(); ++i)
		{
			if (!nodes[i].dynamic) continue;
			const std::size_t representative = root(i);
			if (representative == i) nodes[i].island = islandCount++;
			nodes[i].island = nodes[representative].island;
		}
		// Size once: growing the outer vector one root at a time repeatedly moves
		// nested vectors (and their checked-iterator bookkeeping in the current CRT).
		Resize(islands, islandCount);
		Resize(counts, islandCount);
		for (const Node& node : nodes)
		{
			if (!node.dynamic) continue;
			Write(islands[node.island].dynamicBodies, counts[node.island].dynamics++, node.identity);
		}
		for (const auto& edge : edges)
		{
			const Node& a = nodes[edge[0]], &b = nodes[edge[1]];
			const auto index = a.dynamic ? a.island : b.island;
			auto& island = islands[index];
			auto& count = counts[index];
			Write(island.contactPairs, count.pairs++, ContactIslandPair{a.identity, b.identity});
			if (!a.dynamic) Write(island.boundaryBodies, count.boundaries++, a.identity);
			if (!b.dynamic) Write(island.boundaryBodies, count.boundaries++, b.identity);
		}
		for (std::size_t i = 0; i < islands.size(); ++i)
		{
			auto& island = islands[i];
			Resize(island.dynamicBodies, counts[i].dynamics);
			Resize(island.contactPairs, counts[i].pairs);
			auto& boundaries = island.boundaryBodies;
			Resize(boundaries, counts[i].boundaries);
			if (boundaries.size() > 1)
			{
				std::sort(boundaries.data(), boundaries.data() + boundaries.size(), identityLess);
				Resize(boundaries, static_cast<std::size_t>(std::unique(boundaries.data(), boundaries.data() + boundaries.size()) - boundaries.data()));
			}
		}
	}

	std::vector<ContactIsland> BuildContactIslands(
		const std::vector<RigidBody3D*>& bodies, const std::vector<Manifold>& manifolds,
		std::vector<ContactIsland> islands)
	{
		ContactIslandScratch scratch;
		BuildContactIslands(bodies, manifolds, islands, scratch);
		return islands;
	}
	namespace
	{
		bool IsFiniteContact(const contact_t& contact)
		{
			return Math::IsFinite(contact.ptOnA_WorldSpace) && Math::IsFinite(contact.ptOnB_WorldSpace) &&
				Math::IsFinite(contact.ptOnA_LocalSpace) && Math::IsFinite(contact.ptOnB_LocalSpace) &&
				Math::IsFinite(contact.normal) && Math::IsFinite(contact.separationDistance) &&
				Math::IsFinite(contact.timeOfImpact);
		}

		void RemoveManifoldsForBody(ManifoldCollector& manifoldCollector, const RigidBody3D* body)
		{
			if (!body)
			{
				return;
			}

			auto& manifolds = manifoldCollector.m_Manifolds;
			manifolds.erase(std::remove_if(manifolds.begin(), manifolds.end(),
				[body](Manifold& manifold)
				{
					if (manifold.GetNumContacts() == 0)
					{
						return false;
					}

					const contact_t contact = manifold.GetContact(0);
					return contact.m_BodyA == body || contact.m_BodyB == body;
				}), manifolds.end());
		}

		void RemoveContactsForBody(std::vector<contact_t>& contacts, const RigidBody3D* body)
		{
			contacts.erase(std::remove_if(contacts.begin(), contacts.end(),
				[body](const contact_t& contact)
				{
					return contact.m_BodyA == body || contact.m_BodyB == body;
				}), contacts.end());
		}
	}

	// Ballistic revalidation can produce a zero-relative-time hit. Keep its impulse
	// path separate from the public resolver's legacy zero-TOI position projection.
	static void ResolveContactAtCurrentState(contact_t& contact, bool projectPosition);

	static bool QueryCurrentToiContact(RigidBody3D* bodyA, RigidBody3D* bodyB, contact_t& contact)
	{
		if (bodyA->m_Shape->GetShapeType() == ShapeType::Sphere && bodyB->m_Shape->GetShapeType() == ShapeType::Sphere)
		{
			// Reuse the existing short-ray contact tolerance at the actual impact pose.
			return Collision::SphereSphereIntersect(bodyA, bodyB, 0.0f, contact);
		}
		return Collision::Intersect(bodyA, bodyB, contact);
	}

	static bool RaySphere(const Vec3f& rayStart, const Vec3f& rayDir, const Vec3f& sphereCenter, const float sphereRadius, float& t1, float& t2) {
		const Vec3f m = sphereCenter - rayStart;
		const float a = glm::length2(rayDir);//rayDir.Dot(rayDir);
		const float b = glm::dot(m, rayDir); // m.Dot(rayDir);
		const float c = glm::length2(m) - sphereRadius * sphereRadius;

		if (!Math::IsFinite(a) || a <= Math::NumericalEpsilonSquared)
		{
			return false;
		}

		const float delta = b * b - a * c;
		const float invA = 1.0f / a;

		if (delta < 0) {
			// no real solutions exist
			return false;
		}

		const float deltaRoot = sqrtf(delta);
		t1 = invA * (b - deltaRoot);
		t2 = invA * (b + deltaRoot);

		return true;
	}

	bool SphereSphereStatic(const ShapeSphere* sphereA, const ShapeSphere* sphereB, const Vec3f& posA, const Vec3f& posB, Vec3f& ptOnA, Vec3f& ptOnB) {
		const Vec3f ab = posB - posA;
		const Vec3f norm = Math::NormalizeOr(ab);

		ptOnA = posA + norm * sphereA->GetRadius();
		ptOnB = posB - norm * sphereB->GetRadius();

		const float radiusAB = sphereA->GetRadius() + sphereB->GetRadius();
		const float lengthSquare = glm::length2(ab);
		if (lengthSquare <= (radiusAB * radiusAB)) {
			return true;
		}

		return false;
	}


	static bool SphereSphereDynamic(const ShapeSphere* shapeA, const ShapeSphere* shapeB, const Vec3f& posA, const Vec3f& posB, const Vec3f& velA, const Vec3f& velB, const float dt, Vec3f& ptOnA, Vec3f& ptOnB, float& toi) {
		const Vec3f relativeVelocity = velA - velB;

		const Vec3f startPtA = posA;
		const Vec3f endPtA = posA + relativeVelocity * dt;
		const Vec3f rayDir = endPtA - startPtA;

		float t0 = 0;
		float t1 = 0;
		if (glm::length2(rayDir) < 0.001f * 0.001f) {
			// Ray is too short, just check if already intersecting
			Vec3f ab = posB - posA;
			float radius = shapeA->GetRadius() + shapeB->GetRadius() + 0.001f;
			if (glm::length2(ab) > radius * radius) {
				return false;
			}
		}
		else if (!RaySphere(posA, rayDir, posB, shapeA->GetRadius() + shapeB->GetRadius(), t0, t1)) {
			return false;
		}

		// Change from [0,1] range to [0,dt] range
		t0 *= dt;
		t1 *= dt;

		// If the collision is only in the past, then there's not future collision this frame
		if (t1 < 0.0f) {
			return false;
		}

		// Get the earliest positive time of impact
		toi = (t0 < 0.0f) ? 0.0f : t0;

		// If the earliest collision is too far in the future, then there's no collision this frame
		if (toi > dt) {
			return false;
		}

		// Get the points on the respective points of collision and return true
		Vec3f newPosA = posA + velA * toi;
		Vec3f newPosB = posB + velB * toi;
		Vec3f ab = newPosB - newPosA;
		ab = Math::NormalizeOr(ab, relativeVelocity);

		ptOnA = newPosA + ab * shapeA->GetRadius();
		ptOnB = newPosB - ab * shapeB->GetRadius();
		return true;
	}


	PhysicsSystem::~PhysicsSystem()
	{
		OnExit();
	}

	void PhysicsSystem::Initialize()
	{

	}

	RigidBody3D* PhysicsSystem::ResolveBody(RigidBodyIdentity identity) const
	{
		if (!m_PhysicsWorld || !m_PhysicsWorld->IsBodyIdentityValid(identity)) return nullptr;
		auto* body = m_PhysicsWorld->m_BodyIdentitySlots[identity.GetSlot() - 1].body;
		return body->GetIdentity() == identity ? body : nullptr;
	}

	void PhysicsSystem::WakeConnectedTo(RigidBody3D* body)
	{
		if (!m_PhysicsWorld || !body) return;
		const auto& bodies = m_PhysicsWorld->GetPhysicsBodies();
		if (std::find(bodies.begin(), bodies.end(), body) == bodies.end()) return;
		// Rebuild from still-live retained contacts before a mutation/removal retires
		// support edges. This conservative wake graph is never the public solve graph.
		BuildContactIslands(bodies, m_Manifolds.m_Manifolds, m_ActivationIslands, m_ContactIslandScratch);
		const auto identity = body->GetIdentity();
		for (const auto& island : m_ActivationIslands) {
			if (std::find(island.dynamicBodies.begin(), island.dynamicBodies.end(), identity) == island.dynamicBodies.end() &&
				std::find(island.boundaryBodies.begin(), island.boundaryBodies.end(), identity) == island.boundaryBodies.end()) continue;
			for (auto member : island.dynamicBodies) if (auto* live = ResolveBody(member)) live->WakeUp();
		}
		body->WakeUp();
	}

	void PhysicsSystem::Update(Timestep ts)
	{
		m_SleepTimings = {};
		// Invalidate the public snapshot while retaining its allocation capacity locally.
		std::vector<ContactIsland> previousIslands;
		previousIslands.swap(m_ContactIslands);
		const float dtSeconds = static_cast<float>(ts);
		GENGINE_CORE_ASSERT(Math::IsFinite(dtSeconds), "Physics timestep must be finite");
		if (!Math::IsFinite(dtSeconds))
		{
			return;
		}

		GE_PHYSICS_PROFILE_SCOPE(physicsWorldTimeNs);
		GE_PHYSICS_PROFILE_ADD(stepCount, 1);

		using namespace Collision;

		if (m_PhysicsWorld && dtSeconds > 0.0f) {
			const auto gravity = m_PhysicsWorld->GetGravity();
			if (!Math::IsFinite(gravity)) return;
			const bool gravityChanged = m_HasSleepGravity && gravity != m_LastSleepGravity;
			bool hasSleeping = false;
			const auto& bodies = m_PhysicsWorld->GetPhysicsBodies();
			SleepPathTimer sourceCheckTimer(m_SleepTimings.sourceCheckNs);
			for (auto* body : std::span(bodies.data(), bodies.size())) {
				const auto& previous = body->m_SleepSource;
				const auto revision = body->m_Shape ? body->m_Shape->GetRevision() : 0;
				if (!body->m_SleepSourceValid || body->m_Shape != previous.shape || revision != previous.shapeRevision)
					body->m_SleepShapeValid = body->m_Shape && body->m_Shape->IsValid();
				// Compare source fields in place; materialize a snapshot only on a change.
				const bool sourceChanged = !body->m_SleepSourceValid ||
					body->m_Position != previous.position || body->m_LinearVelocity != previous.linearVelocity ||
					body->m_AngularVelocity != previous.angularVelocity || body->m_Orientation != previous.orientation ||
					body->m_InvMass != previous.inverseMass || body->m_Elasticity != previous.elasticity ||
					body->m_Friction != previous.friction || body->m_CollisionLayer != previous.layer ||
					body->m_CollisionMask != previous.mask || body->m_Shape != previous.shape ||
					revision != previous.shapeRevision || body->Type != previous.type;
				body->m_ExternalMutation = body->m_WakeRequested || gravityChanged ||
					(body->m_SleepSourceValid ? sourceChanged : body->IsSleeping());
				hasSleeping |= body->IsSleeping();
				body->m_ActivationIsland = static_cast<std::size_t>(-1);
				if (body->m_ExternalMutation || !m_SleepingEnabled) body->WakeUp();
				// An unchanged final snapshot already is this tick's start snapshot.
				if (sourceChanged) body->m_SleepSource = { body->m_Position, body->m_LinearVelocity,
					body->m_AngularVelocity, body->m_Orientation, body->m_InvMass, body->m_Elasticity,
					body->m_Friction, body->m_CollisionLayer, body->m_CollisionMask, body->m_Shape, revision, body->Type };
			}
			sourceCheckTimer.Stop();
			SleepPathTimer wakeTimer(m_SleepTimings.wakeNs);
			if (hasSleeping && m_SleepingEnabled) {
				// Wake conservatively using retained live contact identities before expiry can
				// erase a changed/removed support. No connectivity decision is cached across ticks.
				BuildContactIslands(bodies, m_Manifolds.m_Manifolds, m_ActivationIslands, m_ContactIslandScratch);
				for (std::size_t i = 0; i < m_ActivationIslands.size(); ++i) {
					const auto& island = m_ActivationIslands[i];
					bool sleeping = false, awake = false, changed = false;
					for (auto identity : std::span(island.dynamicBodies.data(), island.dynamicBodies.size())) if (auto* body = ResolveBody(identity)) {
						body->m_ActivationIsland = i;
						sleeping |= body->IsSleeping(); awake |= !body->IsSleeping();
						changed |= body->m_ExternalMutation;
					}
					for (auto identity : std::span(island.boundaryBodies.data(), island.boundaryBodies.size())) if (auto* body = ResolveBody(identity)) {
						changed |= body->m_ExternalMutation || (body->Type == BodyType::Kinematic &&
							(body->GetLinearVelocity() != Vec3f(0) || body->GetAngularVelocity() != Vec3f(0)));
					}
					if (changed || (sleeping && awake))
						for (auto identity : std::span(island.dynamicBodies.data(), island.dynamicBodies.size())) if (auto* body = ResolveBody(identity)) body->WakeUp();
				}
			}
			m_LastSleepGravity = gravity;
			m_HasSleepGravity = true;
		}
		{
			GE_PHYSICS_PROFILE_SCOPE(manifoldTimeNs);
			//Timeit("	m_Manifolds-RemoveExpired")
			m_Manifolds.RemoveExpired();
		}

		if (m_PhysicsWorld)
		{
			auto& PhysicsBodies = m_PhysicsWorld->GetPhysicsBodies();
			size_t size = PhysicsBodies.size();
			auto gravity = m_PhysicsWorld->GetGravity();
			GENGINE_CORE_ASSERT(Math::IsFinite(gravity), "Physics gravity must be finite");
			if (!Math::IsFinite(gravity))
			{
				return;
			}
			SleepPathTimer flagsTimer(m_SleepTimings.stepFlagsNs);
			for (RigidBody3D* body : PhysicsBodies)
			{
				body->m_InPhysicsStep = true;
				GENGINE_CORE_ASSERT(body != nullptr, "Physics world must not contain null bodies");
				if (body)
				{
					body->AssertFiniteState();
				}
			}


			flagsTimer.Stop();
			// Gravity impulse
			{
				GE_PHYSICS_PROFILE_SCOPE(gravityTimeNs);
				//Timeit("	apply linear impluse to dynamic entities")
				for (size_t i = 0; i < size; i++)
				{
					RigidBody3D* body = PhysicsBodies[i];
					if (body->Type == BodyType::Dynamic && body->GetInverseMass() > 0.0f && !body->IsSleeping())
					{
						body->m_LinearVelocity += gravity * dtSeconds;
						body->AssertFiniteState();
					}
				}
			}

			//
			// Broadphase
			//
			{
				GE_PHYSICS_PROFILE_SCOPE(broadphaseTimeNs);
				//Timeit("	BroadPhase")
				m_Broadphase.FindPairs(PhysicsBodies, m_CollisionPairs, dtSeconds);
			}
			const BroadphaseStats& broadphaseStats = m_Broadphase.GetLastStats();
			GE_PHYSICS_PROFILE_SET(candidatePairCount, m_CollisionPairs.size());
			GE_PHYSICS_PROFILE_ADD(broadphaseAxisOverlapCount, broadphaseStats.axisOverlapCount);
			GE_PHYSICS_PROFILE_ADD(broadphaseAabbRejectedCount, broadphaseStats.aabbRejectedCount);
			GE_PHYSICS_PROFILE_ADD(broadphaseStaticPairRejectedCount, broadphaseStats.staticPairRejectedCount);
			GE_PHYSICS_PROFILE_ADD(broadphaseMaskRejectedCount, broadphaseStats.maskRejectedCount);
			GE_PHYSICS_PROFILE_ADD(broadphaseInsertionSortSwapCount, broadphaseStats.insertionSortSwapCount);
			GE_PHYSICS_PROFILE_ADD(broadphaseFullSortCount, broadphaseStats.fullSortCount);

			//
			//	NarrowPhase (perform actual collision detection)
			//
			m_Contacts.clear();
			const auto wakeSleeping = [&](RigidBody3D* seed) {
				if (!seed->IsSleeping()) return false;
				const auto wake = [&](RigidBody3D* body) {
					if (!body || !body->IsSleeping()) return;
					body->WakeUp();
					// Gravity was skipped earlier. Apply it exactly once before this tick's solve.
					if (body->GetInverseMass() > 0.0f) body->m_LinearVelocity += gravity * dtSeconds;
				};
				if (seed->m_ActivationIsland < m_ActivationIslands.size()) {
					for (auto identity : m_ActivationIslands[seed->m_ActivationIsland].dynamicBodies) wake(ResolveBody(identity));
				}
				else wake(seed);
				return true;
			};
			const auto inactiveEndpoint = [](const RigidBody3D* body) {
				return !body->m_ExternalMutation && (body->IsSleeping() ||
					(body->GetInverseMass() == 0.0f && body->GetLinearVelocity() == Vec3f(0) && body->GetAngularVelocity() == Vec3f(0)));
			};
			for (std::size_t i = 0; i < m_CollisionPairs.size(); ++i) {
				const collisionPair_t& pair = m_CollisionPairs[i];
				const bool validPair = pair.a >= 0 && pair.b >= 0 && pair.a != pair.b &&
					static_cast<std::size_t>(pair.a) < PhysicsBodies.size() &&
					static_cast<std::size_t>(pair.b) < PhysicsBodies.size();
				GENGINE_CORE_ASSERT(validPair, "Broadphase returned invalid body indices");
				if (!validPair)
				{
					continue;
				}
				RigidBody3D* bodyA = PhysicsBodies[pair.a];
				RigidBody3D* bodyB = PhysicsBodies[pair.b];
				if (!bodyA || !bodyB || !bodyA->m_Shape || !bodyB->m_Shape ||
					!bodyA->m_Shape->IsValid() || !bodyB->m_Shape->IsValid())
				{
					continue;
				}

				GE_PHYSICS_PROFILE_ADD(pairFilterCheckCount, 1);
				GE_PHYSICS_PROFILE_SCOPE_NAMED(pairFilterTimer, pairFilterTimeNs);
				const bool skipStaticPair =
					bodyA->Type == BodyType::Static && bodyB->Type == BodyType::Static;
				const bool skipMaskedPair =
					(bodyA->m_CollisionMask & bodyB->m_CollisionLayer) == 0u ||
					(bodyB->m_CollisionMask & bodyA->m_CollisionLayer) == 0u;
				GE_PHYSICS_PROFILE_STOP(pairFilterTimer);

				// Retain a defensive filter at the narrowphase boundary.
				if (skipStaticPair || skipMaskedPair)
				{
					GE_PHYSICS_PROFILE_ADD(pairFilterRejectedCount, 1);
					continue;
				}

				// Keep every proxy in SAP so active/edited bodies can still discover sleepers.
				if (m_SleepingEnabled && dtSeconds > 0.0f && inactiveEndpoint(bodyA) && inactiveEndpoint(bodyB) &&
					(bodyA->IsSleeping() || bodyB->IsSleeping())) continue;
				contact_t contact{};

				GE_PHYSICS_PROFILE_ADD(narrowphaseCallCount, 1);
				GE_PHYSICS_PROFILE_SCOPE_NAMED(narrowphaseTimer, narrowphaseTimeNs);
				bool didIntersect = Intersect(bodyA, bodyB, (float)ts, contact);
				if (didIntersect && IsFiniteContact(contact) && dtSeconds > 0.0f) {
					const bool wokeA = wakeSleeping(bodyA), wokeB = wakeSleeping(bodyB);
					if (wokeA || wokeB) {
						GE_PHYSICS_PROFILE_ADD(narrowphaseCallCount, 1);
						didIntersect = Intersect(bodyA, bodyB, dtSeconds, contact);
					}
				}
				GE_PHYSICS_PROFILE_STOP(narrowphaseTimer);
				if(didIntersect)
				{
					GENGINE_CORE_ASSERT(IsFiniteContact(contact), "Generated physics contact must be finite");
					if (!IsFiniteContact(contact))
					{
						continue;
					}
					GE_PHYSICS_PROFILE_ADD(generatedContactCount, 1);
					if (0.0f == contact.timeOfImpact)
					{
						//std::cout << "0.0f occurred" << std::endl;
						
						{
							GE_PHYSICS_PROFILE_SCOPE(manifoldTimeNs);
							std::array<contact_t, 4> faceContacts{};
							const int faceContactCount = BuildBoxFaceContacts(contact, faceContacts);
							if (faceContactCount > 0) {
								m_Manifolds.AddContacts(faceContacts.data(), faceContactCount);
								GE_PHYSICS_PROFILE_ADD(generatedContactCount, faceContactCount - 1);
							}
							else {
								m_Manifolds.AddContact(contact);
							}
						}
						
					}
					else
					{
						//std::cout << "Collision occurred" << std::endl;
						GENGINE_INFO("Collision occurred");
						m_Contacts.push_back(contact);
						//
					}
				}
			}

#ifdef GE_ENABLE_PHYSICS_PROFILING
			int manifoldContactCount = 0;
			for (auto& manifold : m_Manifolds.m_Manifolds) {
				if (manifold.GetNumContacts() == 0) continue;
				const auto contact = manifold.GetContact(0);
				if ((contact.m_BodyA->GetInverseMass() > 0.0f && !contact.m_BodyA->IsSleeping()) ||
					(contact.m_BodyB->GetInverseMass() > 0.0f && !contact.m_BodyB->IsSleeping())) manifoldContactCount += manifold.GetNumContacts();
			}
			GE_PHYSICS_PROFILE_SET(manifoldCount, m_Manifolds.m_Manifolds.size());
			GE_PHYSICS_PROFILE_SET(manifoldContactCount, m_Manifolds.GetContactCount());
			GE_PHYSICS_PROFILE_SET(solverConstraintCount, manifoldContactCount);
#endif

			{
				GE_PHYSICS_PROFILE_SCOPE(solverTimeNs);
				//Timeit("	m_Manifolds PreSolve")
				////Solve the Constraints
				m_Manifolds.PreSolve(ts);
			}

			// PreSolve may retire invalid manifolds. Describe exactly the retained graph
			// without changing contact order or CCD. Sleep decisions use this graph below.
			BuildContactIslands(PhysicsBodies, m_Manifolds.m_Manifolds, previousIslands, m_ContactIslandScratch);
			m_ContactIslands.swap(previousIslands);

			{
				GE_PHYSICS_PROFILE_SCOPE(solverTimeNs);
				//Timeit("	m_Manifolds Solve")
				// Warm start once above; repeat only the existing ordered constraint traversal.
				const int maxIters = m_SolverIterations;
#ifdef GE_ENABLE_PHYSICS_PROFILING
				if (manifoldContactCount > 0)
				{
					GE_PHYSICS_PROFILE_ADD(solverIterationCount, maxIters);
				}
#endif
				for (int iters = 0; iters < maxIters; iters++)
				{
					m_Manifolds.Solve();
				}
			}



			// Revalidate the initially detected positive-TOI pairs after the resting solve.
			// A missed prediction stays pending: another impulse on either body may revive it.
			float accumulatedTime = 0.0f;
			const auto refreshPrediction = [&](contact_t& pending)
			{
				contact_t current{};
				const float remaining = dtSeconds - accumulatedTime;
				GE_PHYSICS_PROFILE_ADD(narrowphaseCallCount, 1);
				GE_PHYSICS_PROFILE_SCOPE(narrowphaseTimeNs);
				const bool hit = remaining > 0.0f
					? Intersect(pending.m_BodyA, pending.m_BodyB, remaining, current)
					: QueryCurrentToiContact(pending.m_BodyA, pending.m_BodyB, current);
				if (hit && IsFiniteContact(current) && current.timeOfImpact >= 0.0f && current.timeOfImpact <= remaining)
				{
					pending = current;
					pending.timeOfImpact = std::min(dtSeconds, accumulatedTime + current.timeOfImpact);
				}
				else
				{
					pending.timeOfImpact = std::numeric_limits<float>::max();
				}
			};
			for (contact_t& pending : m_Contacts)
			{
				refreshPrediction(pending);
			}

			while (!m_Contacts.empty())
			{
				// Keep initial pair order for ties. Each iteration consumes one initial event,
				// so simultaneous/overlapping contacts cannot create an unbounded zero-time loop.
				const auto next = std::min_element(m_Contacts.begin(), m_Contacts.end(),
					[](const contact_t& a, const contact_t& b) { return a.timeOfImpact < b.timeOfImpact; });
				if (next->timeOfImpact > dtSeconds)
				{
					break;
				}
				const contact_t event = *next;
				m_Contacts.erase(next);
				const float dt = std::max(0.0f, event.timeOfImpact - accumulatedTime);
				if (dt > 0.0f)
				{
					GE_PHYSICS_PROFILE_SCOPE(integrationTimeNs);
					for (RigidBody3D* body : PhysicsBodies)
					{
						if (!body->IsSleeping()) { body->Update(dt); GE_PHYSICS_PROFILE_ADD(integratedBodyCount, 1); }
					}
				}
				accumulatedTime = event.timeOfImpact;

				// Predictions supply scheduling only. Rebuild geometry at the actual pose;
				// failed/non-finite queries cannot supply an impulse. The resolver rejects
				// non-closing contact velocity using these current anchors and normal.
				contact_t current{};
				bool hit = false;
				{
					GE_PHYSICS_PROFILE_ADD(narrowphaseCallCount, 1);
					GE_PHYSICS_PROFILE_SCOPE(narrowphaseTimeNs);
					hit = QueryCurrentToiContact(event.m_BodyA, event.m_BodyB, current);
				}
				if (hit && IsFiniteContact(current))
				{
					current.timeOfImpact = accumulatedTime;
					GE_PHYSICS_PROFILE_SCOPE(contactResolutionTimeNs);
					ResolveContactAtCurrentState(current, false);
				}

				// Only these bodies can have received ballistic impulses. Refresh every
				// remaining event involving either, including previously missed predictions,
				// before choosing the next time. Unrelated events keep their absolute time.
				for (contact_t& pending : m_Contacts)
				{
					if (pending.m_BodyA == event.m_BodyA || pending.m_BodyA == event.m_BodyB ||
						pending.m_BodyB == event.m_BodyA || pending.m_BodyB == event.m_BodyB)
					{
						refreshPrediction(pending);
					}
				}
			}

			// Update the positions for the rest of this frame's time
			const float timeRemaining = (float)ts - accumulatedTime;
			if (timeRemaining > 0.0f) {
				{
					{
						GE_PHYSICS_PROFILE_SCOPE(integrationTimeNs);
						//Timeit("	update entities rest")
						for (int i = 0; i < size; i++) 
						{
							if (!PhysicsBodies[i]->IsSleeping()) { PhysicsBodies[i]->Update(timeRemaining); GE_PHYSICS_PROFILE_ADD(integratedBodyCount, 1); }
						}
					}
				}
			}

			// Correct current resting penetration once, after all physical/TOI integration.
			{
				GE_PHYSICS_PROFILE_SCOPE(solverTimeNs);
				m_Manifolds.PostSolve();
			}

			if (dtSeconds > 0.0f && m_SleepingEnabled) {
				SleepPathTimer depthTimer(m_SleepTimings.depthNs);
				// Do not freeze residual penetration outside the existing position-stability
				// gate: solver slop 0.02 plus its tested 0.00001 floating-point tolerance.
				for (auto& manifold : m_Manifolds.m_Manifolds) for (int i = 0; i < manifold.GetNumContacts(); ++i) {
					const auto contact = manifold.GetContact(i);
					const Vec3f a = contact.m_BodyA->BodySpaceToWorldSpace(contact.ptOnA_LocalSpace);
					const Vec3f b = contact.m_BodyB->BodySpaceToWorldSpace(contact.ptOnB_LocalSpace);
					const float depth = -glm::dot(a - b, contact.normal);
					if (!Math::IsFinite(depth) || depth > 0.02001f) {
						if (contact.m_BodyA->GetInverseMass() > 0.0f) contact.m_BodyA->WakeUp();
						if (contact.m_BodyB->GetInverseMass() > 0.0f) contact.m_BodyB->WakeUp();
					}
				}
				depthTimer.Stop();
				SleepPathTimer eligibilityTimer(m_SleepTimings.eligibilityNs);
				bool hasSleepCandidate = false;
				for (auto* body : std::span(PhysicsBodies.data(), PhysicsBodies.size())) {
					if (body->IsSleeping()) { hasSleepCandidate = true; continue; }
					if (body->GetInverseMass() <= 0.0f) { body->WakeUp(); continue; }
					const bool unchangedPose = body->m_Position == body->m_SleepSource.position &&
						body->m_Orientation == body->m_SleepSource.orientation;
					bool moved = body->m_WakeRequested || !body->m_SleepShapeValid;
					// Equal poses have zero displacement: avoid widening and squaring them.
					// Changed poses retain the exact correction-motion equations and limits.
					if (!moved && !unchangedPose) {
						const auto& settings = body->GetSleepSettings();
						const glm::dvec3 displacement = glm::dvec3(body->m_Position) - glm::dvec3(body->m_SleepSource.position);
						const glm::dquat q(body->m_Orientation), old(body->m_SleepSource.orientation);
						const auto dq = q - old, opposite = q + old;
						const double linearLimit = double(settings.linearSpeedThreshold) * dtSeconds;
						const double angularLimit = double(settings.angularSpeedThreshold) * dtSeconds * 0.5;
						moved = glm::dot(displacement, displacement) > linearLimit * linearLimit ||
							std::min(glm::dot(dq,dq), glm::dot(opposite,opposite)) > angularLimit * angularLimit;
					}
					if (moved) body->WakeUp();
					else if (body->m_SleepSourceValid && !body->m_ExternalMutation && unchangedPose &&
						body->m_InactiveSeconds > 0.0 &&
						body->m_LinearVelocity == body->m_SleepSource.linearVelocity &&
						body->m_AngularVelocity == body->m_SleepSource.angularVelocity) {
						// Positive inactivity proves prior finite/speed eligibility. The complete
						// source is unchanged, as are this step's motion and the sleep policy
						// (policy setters wake/reset). Reuse that proof, not a connectivity decision.
						body->AdvanceSleepTimer(dtSeconds);
					}
					else body->UpdateSleepTimer(dtSeconds);
					hasSleepCandidate |= body->m_InactiveSeconds >= body->m_SleepSettings.inactivitySeconds;
				}
				eligibilityTimer.Stop();
				SleepPathTimer qualificationTimer(m_SleepTimings.qualificationNs);
				// With no retained contacts, every current island is a singleton without a boundary.
				// If no timer has reached its dwell, there is no qualification or boundary wake work.
				if (hasSleepCandidate || !m_Manifolds.m_Manifolds.empty())
				for (const auto& island : std::span(m_ContactIslands.data(), m_ContactIslands.size())) {
					bool ready = true;
					for (auto identity : std::span(island.dynamicBodies.data(), island.dynamicBodies.size())) {
						auto* body = ResolveBody(identity);
						ready &= body && body->CanSleep();
					}
					for (auto identity : std::span(island.boundaryBodies.data(), island.boundaryBodies.size())) if (auto* body = ResolveBody(identity)) {
						if (body->m_ExternalMutation || (body->Type == BodyType::Kinematic &&
							(body->GetLinearVelocity() != Vec3f(0) || body->GetAngularVelocity() != Vec3f(0)))) {
							ready = false;
							for (auto member : std::span(island.dynamicBodies.data(), island.dynamicBodies.size())) if (auto* dynamic = ResolveBody(member)) dynamic->WakeUp();
						}
					}
					if (ready) for (auto identity : std::span(island.dynamicBodies.data(), island.dynamicBodies.size())) if (auto* body = ResolveBody(identity)) {
						body->TrySleep();
						body->m_LinearVelocity = body->m_AngularVelocity = Vec3f(0);
					}
				}
			}
#ifdef GE_ENABLE_PHYSICS_PROFILING
			std::uint64_t dynamicBodyCount = 0;
			std::uint64_t activeBodyCount = 0, sleepingBodyCount = 0;
			for (const RigidBody3D* body : PhysicsBodies)
			{
				if (body->Type == BodyType::Dynamic)
				{
					++dynamicBodyCount;
				}
				if (body->IsSleeping()) ++sleepingBodyCount;
				if (body->Type != BodyType::Static && !body->IsSleeping())
				{
					++activeBodyCount;
				}
			}
			GE_PHYSICS_PROFILE_SET(bodyCount, size);
			GE_PHYSICS_PROFILE_SET(dynamicBodyCount, dynamicBodyCount);
			GE_PHYSICS_PROFILE_SET(activeBodyCount, activeBodyCount);
			GE_PHYSICS_PROFILE_SET(sleepingBodyCount, sleepingBodyCount);
#endif
			m_Contacts.clear();
			SleepPathTimer storeTimer(m_SleepTimings.sourceStoreNs);
			for (RigidBody3D* body : std::span(PhysicsBodies.data(), PhysicsBodies.size()))
			{
				if (body)
				{
					body->AssertFiniteState();
					body->m_InPhysicsStep = false;
					if (dtSeconds > 0.0f) {
						// Configuration cannot change during this single-threaded step. Only
						// integrated/solved motion needs to replace the start snapshot.
						if (body->Type != BodyType::Static) {
							body->m_SleepSource.position = body->m_Position;
							body->m_SleepSource.orientation = body->m_Orientation;
							body->m_SleepSource.linearVelocity = body->m_LinearVelocity;
							body->m_SleepSource.angularVelocity = body->m_AngularVelocity;
						}
						body->m_SleepSourceValid = true;
						body->m_WakeRequested = body->m_ExternalMutation = false;
					}
				}
			}

		}

	}

	bool PhysicsSystem::SetBodyPose(RigidBody3D* body, const Vec3f& position, const Quat& orientation)
	{
		if (!m_PhysicsWorld || !body || !Math::IsFinite(position) || !Math::IsFinite(orientation))
			return false;

		// Check ownership before dereferencing the caller's pointer.
		const auto& bodies = m_PhysicsWorld->GetPhysicsBodies();
		if (std::find(bodies.begin(), bodies.end(), body) == bodies.end()) return false;
		const float lengthSquared = glm::dot(orientation, orientation);
		if (!Math::IsFinite(lengthSquared) || lengthSquared <= Math::NumericalEpsilonSquared) return false;
		// Re-normalization can change a unit quaternion's last bit. Exact resubmission
		// (including the equivalent sign) must preserve contacts at float unit tolerance.
		if (body->m_Position == position &&
			std::abs(lengthSquared - 1.0f) <= 4.0f * std::numeric_limits<float>::epsilon() &&
			(body->m_Orientation == orientation || body->m_Orientation == -orientation)) return true;
		const Quat normalized = orientation / std::sqrt(lengthSquared);
		if (body->m_Position == position &&
			(body->m_Orientation == normalized || body->m_Orientation == -normalized)) return true;

		WakeConnectedTo(body);
		m_ContactIslands.clear();
		RemoveManifoldsForBody(m_Manifolds, body);
		RemoveContactsForBody(m_Contacts, body);
		m_CollisionPairs.clear();
		m_Broadphase.Clear();
		body->m_Position = position;
		body->m_Orientation = normalized;
		// Derived body data already observes source pose changes lazily.
		return true;
	}

	void PhysicsSystem::OnExit()
	{
		m_ActivationIslands.clear();
		m_HasSleepGravity = false;
		m_ContactIslands.clear();
		m_Manifolds.Clear();
		m_Broadphase.Clear();
		m_CollisionPairs.clear();
		m_Contacts.clear();

		PhysicsWorld* physicsWorld = m_PhysicsWorld;
		m_PhysicsWorld = nullptr;
		delete physicsWorld;
	}

	void PhysicsSystem::SetPhysicsWorld(PhysicsWorld* physics_world)
	{
		if (m_PhysicsWorld == physics_world)
		{
			return;
		}

		OnExit();
		m_PhysicsWorld = physics_world;
		if (m_PhysicsWorld)
		{
			m_PhysicsWorld->SetBodyRemovalCallback([this](RigidBody3D* body)
			{
				WakeConnectedTo(body);
				m_ContactIslands.clear();
				RemoveManifoldsForBody(m_Manifolds, body);
				RemoveContactsForBody(m_Contacts, body);
			});
		}
	}



	bool Collision::SphereSphereIntersect(RigidBody3D* bodyA, RigidBody3D* bodyB, const float dt, contact_t& contact)
	{
		contact.featureA = contact.featureB = 0; // This query emits an unfeatured witness.
		contact.m_BodyA = bodyA;
		contact.m_BodyB = bodyB;

		//if (bodyA->m_shape->GetType() == Shape::SHAPE_SPHERE && bodyB->m_shape->GetType() == Shape::SHAPE_SPHERE) {
		const ShapeSphere* sphereA = (const ShapeSphere*)bodyA->m_Shape;
		const ShapeSphere* sphereB = (const ShapeSphere*)bodyB->m_Shape;

		Vec3f posA = bodyA->m_Position;
		Vec3f posB = bodyB->m_Position;

		Vec3f velA = bodyA->GetLinearVelocity();
		Vec3f velB = bodyB->GetLinearVelocity();

		if (SphereSphereDynamic(sphereA, sphereB, posA, posB, velA, velB, dt, contact.ptOnA_WorldSpace, contact.ptOnB_WorldSpace, contact.timeOfImpact)) {
			// Predict on local value copies: integration and lazy caches must never touch live bodies.
			RigidBody3D predictedA = *bodyA;
			RigidBody3D predictedB = *bodyB;
			predictedA.Update(contact.timeOfImpact);
			predictedB.Update(contact.timeOfImpact);

			// Convert world space contacts to local space
			contact.ptOnA_LocalSpace = predictedA.WorldSpaceToBodySpace(contact.ptOnA_WorldSpace);
			contact.ptOnB_LocalSpace = predictedB.WorldSpaceToBodySpace(contact.ptOnB_WorldSpace);

			contact.normal = Math::NormalizeOr(predictedA.m_Position - predictedB.m_Position, Vec3f(-1.0f, 0.0f, 0.0f));

			// Calculate the separation distance
			Vec3f ab = bodyB->m_Position - bodyA->m_Position;
			float r = glm::length(ab) - (sphereA->GetRadius() + sphereB->GetRadius());
			contact.separationDistance = r;
			return true;
		}
		//}
		return false;
	}

	// Internal narrow-phase evaluation accepts only query-local bodies.
	static bool IntersectAtQueryState(RigidBody3D* bodyA, RigidBody3D* bodyB, contact_t& contact)
	{
		contact.featureA = contact.featureB = 0; // This query emits an unfeatured witness.
		contact.m_BodyA = bodyA;
		contact.m_BodyB = bodyB;
		contact.timeOfImpact = 0.0f;

		if (bodyA->m_Shape->GetShapeType() == ShapeType::Sphere && bodyB->m_Shape->GetShapeType() == ShapeType::Sphere)
		{
			const ShapeSphere* sphereA = (const ShapeSphere*)bodyA->m_Shape;
			const ShapeSphere* sphereB = (const ShapeSphere*)bodyB->m_Shape;

			Vec3f posA = bodyA->m_Position;
			Vec3f posB = bodyB->m_Position;

			if (SphereSphereStatic(sphereA, sphereB, posA, posB, contact.ptOnA_WorldSpace, contact.ptOnB_WorldSpace)) {
				contact.normal = Math::NormalizeOr(posA - posB, Vec3f(-1.0f, 0.0f, 0.0f));

				contact.ptOnA_LocalSpace = bodyA->WorldSpaceToBodySpace(contact.ptOnA_WorldSpace);
				contact.ptOnB_LocalSpace = bodyB->WorldSpaceToBodySpace(contact.ptOnB_WorldSpace);

				Vec3f ab = bodyB->m_Position - bodyA->m_Position;
				float r = glm::length(ab) - (sphereA->GetRadius() + sphereB->GetRadius());
				contact.separationDistance = r;
				return true;
			}
		}
		else 
		{
			//std::cout << "GJK_DoesIntersect" << std::endl;
			Vec3f ptOnA;
			Vec3f ptOnB;
			const float bias = 0.001f;
			const GjkContactStatus gjkStatus = GJK_GetContact(bodyA, bodyB, bias, ptOnA, ptOnB);
			if (gjkStatus == GjkContactStatus::Contact)
			{
				//std::cout << "GJK_DoesIntersect" << std::endl;
				// There was an intersection, so get the contact data
				//std::cout << "normal: length" << glm::length(ptOnB - ptOnA) << std::endl;
				Vec3f normal = Math::NormalizeOr(ptOnB - ptOnA, bodyB->m_Position - bodyA->m_Position);
				

				ptOnA -= normal * bias;
				ptOnB += normal * bias;

				contact.normal = normal;

				contact.ptOnA_WorldSpace = ptOnA;
				contact.ptOnB_WorldSpace = ptOnB;

				contact.ptOnA_LocalSpace = bodyA->WorldSpaceToBodySpace(contact.ptOnA_WorldSpace);
				contact.ptOnB_LocalSpace = bodyB->WorldSpaceToBodySpace(contact.ptOnB_WorldSpace);

				Vec3f ab = bodyB->m_Position - bodyA->m_Position;
				float r = glm::length(ptOnA - ptOnB);
				contact.separationDistance = -r;
				return true;
			}
			if (gjkStatus == GjkContactStatus::Failed)
			{
				return false;
			}

			// There was no collision, but we still want the contact data, so get it
			GJK_ClosestPoints(bodyA, bodyB, ptOnA, ptOnB);
			contact.ptOnA_WorldSpace = ptOnA;
			contact.ptOnB_WorldSpace = ptOnB;

			contact.ptOnA_LocalSpace = bodyA->WorldSpaceToBodySpace(contact.ptOnA_WorldSpace);
			contact.ptOnB_LocalSpace = bodyB->WorldSpaceToBodySpace(contact.ptOnB_WorldSpace);

			Vec3f ab = bodyB->m_Position - bodyA->m_Position;
			float r = glm::length(ptOnA - ptOnB);
			contact.separationDistance = r;
		}

		return false;

	}


	bool Collision::Intersect(RigidBody3D* bodyA, RigidBody3D* bodyB, contact_t& contact)
	{
		// Even a zero-time query can populate lazy caches; keep those writes local too.
		RigidBody3D predictedA = *bodyA;
		RigidBody3D predictedB = *bodyB;
		const bool hit = IntersectAtQueryState(&predictedA, &predictedB, contact);
		contact.m_BodyA = bodyA;
		contact.m_BodyB = bodyB;
		return hit;
	}

	static void ResolveContactAtCurrentState(contact_t& contact, bool projectPosition)
	{
		RigidBody3D* bodyA = contact.m_BodyA;
		RigidBody3D* bodyB = contact.m_BodyB;

		const Vec3f ptOnA = bodyA->BodySpaceToWorldSpace(contact.ptOnA_LocalSpace);
		const Vec3f ptOnB = bodyB->BodySpaceToWorldSpace(contact.ptOnB_LocalSpace);

		const float elasticityA = bodyA->m_Elasticity;
		const float elasticityB = bodyB->m_Elasticity;
		// Material restitution combines by multiplication (ordinary coefficients are in [0, 1]).
		const float elasticity = elasticityA * elasticityB;
		// World units/second at the contact, including spin. At or below this speed,
		// use an inelastic normal impulse to avoid repeated small ballistic bounces.
		constexpr float restitutionVelocityThreshold = 1.0f;

		const float invMassA = bodyA->GetInverseMass();
		const float invMassB = bodyB->GetInverseMass();

		const Mat3 invWorldInertiaA = bodyA->GetInverseInertiaTensorWorldSpace();
		const Mat3 invWorldInertiaB = bodyB->GetInverseInertiaTensorWorldSpace();

		const Vec3f n = Math::NormalizeOr(contact.normal, bodyA->m_Position - bodyB->m_Position);

		const Vec3f ra = ptOnA - bodyA->GetCenterOfMassWorldSpace();
		const Vec3f rb = ptOnB - bodyB->GetCenterOfMassWorldSpace();

		const Vec3f angularJA = glm::cross(invWorldInertiaA * glm::cross(ra, n), ra);
		const Vec3f angularJB = glm::cross(invWorldInertiaB * glm::cross(rb, n), rb);//(invWorldInertiaB * rb.Cross(n)).Cross(rb);
		const float angularFactor = glm::dot(angularJA + angularJB, n);

		// Get the world space velocity of the motion and rotation
		const Vec3f velA = bodyA->GetLinearVelocity() + glm::cross(bodyA->GetAngularVelocity(), ra);
		const Vec3f velB = bodyB->GetLinearVelocity() + glm::cross(bodyB->GetAngularVelocity(), rb);//bodyB->GetAngularVelocity().Cross(rb);

		// A B -> A normal has negative relative normal speed only while closing.
		const Vec3f vab = velA - velB;
		const float normalSpeed = glm::dot(vab, n);
		const float normalDenominator = invMassA + invMassB + angularFactor;
		float normalImpulse = 0.0f;
		if (Math::IsFinite(normalSpeed) && normalSpeed < 0.0f &&
			Math::IsFinite(normalDenominator) && normalDenominator > Math::NumericalEpsilon)
		{
			const float restitution = -normalSpeed > restitutionVelocityThreshold ? elasticity : 0.0f;
			const float impulseJ = (1.0f + restitution) * normalSpeed / normalDenominator;
			if (Math::IsFinite(impulseJ) && impulseJ < 0.0f)
			{
				const Vec3f vectorImpulseJ = n * impulseJ;
				bodyA->ApplyImpulse(ptOnA, vectorImpulseJ * -1.0f);
				bodyB->ApplyImpulse(ptOnB, vectorImpulseJ);
				normalImpulse = -impulseJ;
			}
		}

		const float frictionA = bodyA->m_Friction;
		const float frictionB = bodyB->m_Friction;
		// Preserve material multiplication across the full range of finite positive float coefficients.
		const double friction = frictionA > 0.0f && frictionB > 0.0f &&
			Math::IsFinite(frictionA) && Math::IsFinite(frictionB)
			? static_cast<double>(frictionA) * static_cast<double>(frictionB) : 0.0;
		if (normalImpulse > 0.0f && friction > 0.0)
		{
			// Off-center normal impulses can change slip: friction must oppose the updated contact velocity.
			const Vec3f relativeVelocity = bodyA->GetLinearVelocity() + glm::cross(bodyA->GetAngularVelocity(), ra) -
				bodyB->GetLinearVelocity() - glm::cross(bodyB->GetAngularVelocity(), rb);
			const Vec3f velTang = relativeVelocity - n * glm::dot(n, relativeVelocity);
			const float tangentialSpeedSquared = glm::length2(velTang);
			if (Math::IsFinite(tangentialSpeedSquared) && tangentialSpeedSquared > Math::NumericalEpsilonSquared)
			{
				const Vec3f tangent = Math::NormalizeOr(velTang);
				const Vec3f inertiaA = glm::cross(invWorldInertiaA * glm::cross(ra, tangent), ra);
				const Vec3f inertiaB = glm::cross(invWorldInertiaB * glm::cross(rb, tangent), rb);
				const float frictionDenominator = invMassA + invMassB + glm::dot(inertiaA + inertiaB, tangent);
				if (Math::IsFinite(frictionDenominator) && frictionDenominator > Math::NumericalEpsilon)
				{
					// Stop slip when possible; otherwise saturate at the applied normal impulse's Coulomb limit.
					const double candidateImpulse = std::sqrt(static_cast<double>(tangentialSpeedSquared)) / frictionDenominator;
					const double frictionLimit = friction * static_cast<double>(normalImpulse);
					const Vec3f impulseFriction = tangent * static_cast<float>(std::min(candidateImpulse, frictionLimit));
					if (Math::IsFinite(impulseFriction))
					{
						bodyA->ApplyImpulse(ptOnA, -impulseFriction);
						bodyB->ApplyImpulse(ptOnB, impulseFriction);
					}
				}
			}
		}

		//
		// Let's also move our colliding objects to just outside of each other (projection method)
		//
		if (projectPosition) {
			const Vec3f ds = ptOnB - ptOnA;

			const float inverseMassSum = invMassA + invMassB;
			if (Math::IsFinite(inverseMassSum) && inverseMassSum > Math::NumericalEpsilon)
			{
				const float tA = invMassA / inverseMassSum;
				const float tB = invMassB / inverseMassSum;

				bodyA->m_Position += ds * tA;
				bodyB->m_Position -= ds * tB;
			}
		}
		bodyA->AssertFiniteState();
		bodyB->AssertFiniteState();
	}

	void Collision::ResolveContact(contact_t& contact)
	{
		ResolveContactAtCurrentState(contact, contact.timeOfImpact == 0.0f);
	}

	bool Collision::ConservativeAdvance(RigidBody3D* bodyA, RigidBody3D* bodyB, float dt, contact_t& contact)
	{
		contact.featureA = contact.featureB = 0; // This query emits an unfeatured witness.
		contact.m_BodyA = bodyA;
		contact.m_BodyB = bodyB;

		// Value copies own independent derived caches and are never registered in a world.
		// Advance them with the existing integrator; shapes are read-only during prediction.
		RigidBody3D predictedA = *bodyA;
		RigidBody3D predictedB = *bodyB;
		float toi = 0.0f;

		int numIters = 0;

		// Advance the positions of the bodies until they touch or there's not time left
		while (dt > 0.0f) {
			
			// Check for intersection
			bool didIntersect = IntersectAtQueryState(&predictedA, &predictedB, contact);
			// Never let query-local pointers escape, on either success or failure.
			contact.m_BodyA = bodyA;
			contact.m_BodyB = bodyB;
			if (didIntersect) {
				//std::cout << "Intersection" << std::endl;
				contact.timeOfImpact = toi;
				return true;
			}
			//std::cout << "No intersection" << std::endl;
			++numIters;
			if (numIters > 10) {
				break;
			}
			
			// Get the vector from the closest point on A to the closest point on B
			Vec3f ab = Math::NormalizeOr(contact.ptOnB_WorldSpace - contact.ptOnA_WorldSpace,
				predictedB.m_Position - predictedA.m_Position);
			//std::cout << "ab: " << ab.x << ", " << ab.y << ", " << ab.z << std::endl;

			// project the relative velocity onto the ray of shortest distance
			Vec3f relativeVelocity = predictedA.GetLinearVelocity() - predictedB.GetLinearVelocity();
			float orthoSpeed = glm::dot(relativeVelocity, ab);

			// Add to the orthoSpeed the maximum angular speeds of the relative shapes
			// Shapes evaluate omega x (localPoint - localCOM). Both inputs must use
			// the current predicted body's local frame; each body has its own rotation.
			const Mat3& worldToA = predictedA.GetWorldToBodyRotation();
			const Mat3& worldToB = predictedB.GetWorldToBodyRotation();
			float angularSpeedA = predictedA.m_Shape->FastestLinearSpeed(worldToA * predictedA.GetAngularVelocity(), worldToA * ab);
			float angularSpeedB = predictedB.m_Shape->FastestLinearSpeed(worldToB * predictedB.GetAngularVelocity(), worldToB * -ab);
			orthoSpeed += angularSpeedA + angularSpeedB;
			if (!Math::IsFinite(orthoSpeed) || orthoSpeed <= Math::NumericalEpsilon) {
				break;
			}

			float timeToGo = contact.separationDistance / orthoSpeed;
			if (!Math::IsFinite(timeToGo) || timeToGo < 0.0f)
			{
				break;
			}
			if (timeToGo > dt) {
				break;
			}

			dt -= timeToGo;
			toi += timeToGo;
			predictedA.Update(timeToGo);
			predictedB.Update(timeToGo);
		}

		return false;
	}


	bool Collision::Intersect(RigidBody3D* bodyA, RigidBody3D* bodyB, const float dt, contact_t& contact)
	{
		if (bodyA->m_Shape->GetShapeType() == ShapeType::Sphere && bodyB->m_Shape->GetShapeType() == ShapeType::Sphere)
		{
			return SphereSphereIntersect(bodyA, bodyB, dt, contact);
		}
		return ConservativeAdvance(bodyA, bodyB, dt, contact);
	}

}
