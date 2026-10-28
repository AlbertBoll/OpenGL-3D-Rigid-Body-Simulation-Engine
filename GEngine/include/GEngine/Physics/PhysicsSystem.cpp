#include"gepch.h"

#include "PhysicsSystem.h"
//#include"PhysicsBody.h"
#include "ShapeSphere.h"
#include "BoxContact.h"
#include "PhysicsWorld.h"
#include "Broadphase.h"
#include "GJK.h"
#include "PhysicsProfile.h"
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
#include "../../../../RigidBodySimulation/tests/CoupledCollisionObservation.h"
#endif
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

	// Private classification remains available to the Physics caller even when the
	// optional diagnostic observer is disconnected. Public Collision APIs stay bool.
	enum class QueryOutcome { Contact, SeparatedValid, Failed };
	struct QueryResult
	{
		QueryOutcome outcome{ QueryOutcome::Failed };
		PhysicsDiagnostic::QueryExit reason{ PhysicsDiagnostic::QueryExit::None };
		GjkDiagnostics diagnostics{};
		bool HasContact() const { return outcome == QueryOutcome::Contact; }
	};
	static QueryResult QueryInstantContact(RigidBody3D* bodyA, RigidBody3D* bodyB, contact_t& contact);
	static QueryResult QueryPredictedContact(RigidBody3D* bodyA, RigidBody3D* bodyB, float dt, contact_t& contact);

	// Keep patch scratch on the admission branch, outside every query call lifetime.
	static __declspec(noinline) void AdmitRestingContact(ManifoldCollector& manifolds,
		RigidBody3D* bodyA, RigidBody3D* bodyB, const contact_t& contact, bool refreshCandidates
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
		, const PhysicsDiagnostic::Coupled::Event& coupledPairResult
#endif
	)
	{
		std::array<contact_t, 4> faceContacts{};
		const int faceContactCount = BuildBoxFaceContacts(contact, faceContacts);
		if (faceContactCount > 0) {
			if (refreshCandidates && faceContactCount == 1)
				manifolds.RemoveExpiredForPair(bodyA, bodyB);
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
            PhysicsDiagnostic::Coupled::Admit(coupledPairResult,contact,faceContacts.data(),
                static_cast<unsigned>(faceContactCount),PhysicsDiagnostic::Coupled::AdmissionKind::RestingPatch);
#endif
			if (refreshCandidates && faceContactCount > 1)
				manifolds.RefreshContactPatch(bodyA, bodyB, faceContacts.data(), faceContactCount);
			else manifolds.AddContacts(faceContacts.data(), faceContactCount);
			GE_PHYSICS_PROFILE_ADD(generatedContactCount, faceContactCount - 1);
		}
		else {
			if (refreshCandidates) manifolds.RemoveExpiredForPair(bodyA, bodyB);
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
            PhysicsDiagnostic::Coupled::Admit(coupledPairResult,contact,&contact,1,
                PhysicsDiagnostic::Coupled::AdmissionKind::RestingSingle);
#endif
			manifolds.AddContact(contact);
		}
	}

	static QueryResult QueryCurrentToiContact(RigidBody3D* bodyA, RigidBody3D* bodyB, contact_t& contact)
	{
		if (bodyA->m_Shape->GetShapeType() == ShapeType::Sphere && bodyB->m_Shape->GetShapeType() == ShapeType::Sphere)
		{
			// Reuse the existing short-ray contact tolerance at the actual impact pose.
			return { Collision::SphereSphereIntersect(bodyA, bodyB, 0.0f, contact)
				? QueryOutcome::Contact : QueryOutcome::SeparatedValid };
		}
		return QueryInstantContact(bodyA, bodyB, contact);
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
        const auto observe = [&](PhysicsDiagnostic::Stage stage) {
            auto* sink=PhysicsDiagnostic::observer;
            if (!sink || !m_PhysicsWorld) return;
            // Only named vector capacity is counted, not process/heap allocation.
            const auto bytes=m_Contacts.capacity()*sizeof(contact_t) +
                m_CollisionPairs.capacity()*sizeof(collisionPair_t) +
                m_Manifolds.m_Manifolds.capacity()*sizeof(Manifold) +
                m_ContactIslandScratch.nodes.capacity()*sizeof(ContactIslandScratch::Node) +
                m_ContactIslandScratch.edges.capacity()*sizeof(std::array<std::size_t,2>) +
                m_ContactIslandScratch.counts.capacity()*sizeof(ContactIslandScratch::Counts);
            sink->boundary(sink->context,stage,*m_PhysicsWorld,m_Manifolds,
                           m_ContactIslands,bytes,m_SolverIterations);
        };
        observe(PhysicsDiagnostic::Stage::Begin);
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
				if (body->m_ExternalMutation || !m_SleepingEnabled) { PhysicsDiagnostic::Observation sleepDiagnostic(PhysicsDiagnostic::Operation::SleepDecision,body,body,true); if (sleepDiagnostic) sleepDiagnostic.value.reason=1; body->WakeUp(); }
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
						for (auto identity : std::span(island.dynamicBodies.data(), island.dynamicBodies.size())) if (auto* body = ResolveBody(identity)) { PhysicsDiagnostic::Observation sleepDiagnostic(PhysicsDiagnostic::Operation::SleepDecision,body,body,true); if (sleepDiagnostic) sleepDiagnostic.value.reason=2; body->WakeUp(); }
				}
			}
			m_LastSleepGravity = gravity;
			m_HasSleepGravity = true;
		}
		// The finite-step/first-gravity guards above run before any candidate
		// lifetime begins. Paused, reverse and null-world paths remain strict.
		bool refreshCandidates = false;
		{
			GE_PHYSICS_PROFILE_SCOPE(manifoldTimeNs);
			//Timeit("	m_Manifolds-RemoveExpired")
			if (m_PhysicsWorld && dtSeconds > 0.0f) refreshCandidates = m_Manifolds.PrepareContactRefresh();
			else m_Manifolds.RemoveExpired();
		}

		if (m_PhysicsWorld)
		{
			auto& PhysicsBodies = m_PhysicsWorld->GetPhysicsBodies();
			size_t size = PhysicsBodies.size();
			auto gravity = m_PhysicsWorld->GetGravity();
			GENGINE_CORE_ASSERT(Math::IsFinite(gravity), "Physics gravity must be finite");
			if (!Math::IsFinite(gravity))
			{
				if (refreshCandidates) {
					GE_PHYSICS_PROFILE_SCOPE(manifoldTimeNs);
					m_Manifolds.RemoveExpired();
				}
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
            observe(PhysicsDiagnostic::Stage::Wake);
			// Gravity impulse
			{
				GE_PHYSICS_PROFILE_SCOPE(gravityTimeNs);
				//Timeit("	apply linear impluse to dynamic entities")
				for (size_t i = 0; i < size; i++)
				{
					RigidBody3D* body = PhysicsBodies[i];
					if (body->Type == BodyType::Dynamic && body->GetInverseMass() > 0.0f && !body->IsSleeping())
					{
                        PhysicsDiagnostic::Observation gravityDiagnostic(PhysicsDiagnostic::Operation::GravityImpulse,body,body,true);
                        if (gravityDiagnostic) gravityDiagnostic.value.duration=dtSeconds;
						body->m_LinearVelocity += gravity * dtSeconds;
						body->AssertFiniteState();
					}
				}
			}

			//
            observe(PhysicsDiagnostic::Stage::Gravity);
			// Broadphase
			//
			{
				GE_PHYSICS_PROFILE_SCOPE(broadphaseTimeNs);
				//Timeit("	BroadPhase")
				m_Broadphase.FindPairs(PhysicsBodies, m_CollisionPairs, dtSeconds);
			}
            observe(PhysicsDiagnostic::Stage::Broadphase);
			const BroadphaseStats& broadphaseStats = m_Broadphase.GetLastStats();
			GE_PHYSICS_PROFILE_SET(candidatePairCount, m_CollisionPairs.size());
			GE_PHYSICS_PROFILE_ADD(broadphaseAxisOverlapCount, broadphaseStats.axisOverlapCount);
			GE_PHYSICS_PROFILE_ADD(broadphaseAabbRejectedCount, broadphaseStats.aabbRejectedCount);
			GE_PHYSICS_PROFILE_ADD(broadphaseStaticPairRejectedCount, broadphaseStats.staticPairRejectedCount);
			GE_PHYSICS_PROFILE_ADD(broadphaseMaskRejectedCount, broadphaseStats.maskRejectedCount);
			GE_PHYSICS_PROFILE_ADD(broadphaseInsertionSortSwapCount, broadphaseStats.insertionSortSwapCount);
			GE_PHYSICS_PROFILE_ADD(broadphaseFullSortCount, broadphaseStats.fullSortCount);
			if (refreshCandidates) {
				GE_PHYSICS_PROFILE_SCOPE(manifoldTimeNs);
				m_Manifolds.RemoveExpiredOutsidePairs(m_CollisionPairs, PhysicsBodies);
			}
			const auto expirePair = [&](const RigidBody3D* a, const RigidBody3D* b) {
				if (refreshCandidates) {
					GE_PHYSICS_PROFILE_SCOPE(manifoldTimeNs);
					m_Manifolds.RemoveExpiredForPair(a, b);
				}
			};

			//
			//	NarrowPhase (perform actual collision detection)
			//
			m_Contacts.clear();
			const auto wakeSleeping = [&](RigidBody3D* seed) {
				if (!seed->IsSleeping()) return false;
				const auto wake = [&](RigidBody3D* body) {
					if (!body || !body->IsSleeping()) return;
                    PhysicsDiagnostic::Observation sleepDiagnostic(PhysicsDiagnostic::Operation::SleepDecision,body,body,true);
                    if (sleepDiagnostic) sleepDiagnostic.value.reason=3;
					body->WakeUp();
                    sleepDiagnostic.Finish();
					// Gravity was skipped earlier. Apply it exactly once before this tick's solve.
					if (body->GetInverseMass() > 0.0f) {
                        PhysicsDiagnostic::Observation gravityDiagnostic(PhysicsDiagnostic::Operation::GravityImpulse,body,body,true);
                        if (gravityDiagnostic) { gravityDiagnostic.value.duration=dtSeconds; gravityDiagnostic.value.reason=1; }
                        body->m_LinearVelocity += gravity * dtSeconds;
                    }
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
					expirePair(bodyA, bodyB);
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
					expirePair(bodyA, bodyB);
					continue;
				}

				// Keep every proxy in SAP so active/edited bodies can still discover sleepers.
				if (m_SleepingEnabled && dtSeconds > 0.0f && inactiveEndpoint(bodyA) && inactiveEndpoint(bodyB) &&
					(bodyA->IsSleeping() || bodyB->IsSleeping())) {
					expirePair(bodyA, bodyB);
					continue;
				}
				contact_t contact{};

				GE_PHYSICS_PROFILE_ADD(narrowphaseCallCount, 1);
				GE_PHYSICS_PROFILE_SCOPE_NAMED(narrowphaseTimer, narrowphaseTimeNs);
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
                PhysicsDiagnostic::Coupled::QueryScope coupledInitialQuery(
                    PhysicsDiagnostic::Coupled::Role::InitialPrediction,bodyA,bodyB,(float)ts,0.0f);
#endif
				QueryResult pairQuery = QueryPredictedContact(bodyA, bodyB, (float)ts, contact);
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
                auto coupledPairResult=coupledInitialQuery.Finish(static_cast<int>(pairQuery.outcome),
                    pairQuery.reason,pairQuery.diagnostics,contact);
#endif
				bool didIntersect = pairQuery.HasContact();
				if (didIntersect && IsFiniteContact(contact) && dtSeconds > 0.0f) {
					const bool wokeA = wakeSleeping(bodyA), wokeB = wakeSleeping(bodyB);
					if (wokeA || wokeB) {
						GE_PHYSICS_PROFILE_ADD(narrowphaseCallCount, 1);
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
                        PhysicsDiagnostic::Coupled::QueryScope coupledWakeQuery(
                            PhysicsDiagnostic::Coupled::Role::WakeRequery,bodyA,bodyB,dtSeconds,0.0f);
#endif
						pairQuery = QueryPredictedContact(bodyA, bodyB, dtSeconds, contact);
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
                        coupledPairResult=coupledWakeQuery.Finish(static_cast<int>(pairQuery.outcome),
                            pairQuery.reason,pairQuery.diagnostics,contact);
#endif
						didIntersect = pairQuery.HasContact();
					}
				}
				GE_PHYSICS_PROFILE_STOP(narrowphaseTimer);
				if (!didIntersect) expirePair(bodyA, bodyB);
				if(didIntersect)
				{
					GENGINE_CORE_ASSERT(IsFiniteContact(contact), "Generated physics contact must be finite");
					if (!IsFiniteContact(contact))
					{
						expirePair(bodyA, bodyB);
						continue;
					}
					GE_PHYSICS_PROFILE_ADD(generatedContactCount, 1);
					if (0.0f == contact.timeOfImpact)
					{
						//std::cout << "0.0f occurred" << std::endl;
						
						{
							GE_PHYSICS_PROFILE_SCOPE(manifoldTimeNs);
							AdmitRestingContact(m_Manifolds, bodyA, bodyB, contact, refreshCandidates
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
								, coupledPairResult
#endif
							);
						}
						
					}
					else
					{
						expirePair(bodyA, bodyB);
						//std::cout << "Collision occurred" << std::endl;
						GENGINE_INFO("Collision occurred");
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
                        PhysicsDiagnostic::Coupled::Admit(coupledPairResult,contact,&contact,1,
                            PhysicsDiagnostic::Coupled::AdmissionKind::ToiQueued);
#endif
						m_Contacts.push_back(contact);
						//
					}
				}
			}

            observe(PhysicsDiagnostic::Stage::Narrowphase);
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

            observe(PhysicsDiagnostic::Stage::WarmStart);
			// PreSolve may retire invalid manifolds. Describe exactly the retained graph
			// without changing contact order or CCD. Sleep decisions use this graph below.
			BuildContactIslands(PhysicsBodies, m_Manifolds.m_Manifolds, previousIslands, m_ContactIslandScratch);
			m_ContactIslands.swap(previousIslands);
            observe(PhysicsDiagnostic::Stage::Islands);

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



            observe(PhysicsDiagnostic::Stage::Solve);
			// Revalidate the initially detected positive-TOI pairs after the resting solve.
			// A missed prediction stays pending: another impulse on either body may revive it.
			float accumulatedTime = 0.0f;
			const auto refreshPrediction = [&](contact_t& pending)
			{
				contact_t current{};
				const float remaining = dtSeconds - accumulatedTime;
				GE_PHYSICS_PROFILE_ADD(narrowphaseCallCount, 1);
				GE_PHYSICS_PROFILE_SCOPE(narrowphaseTimeNs);
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
                PhysicsDiagnostic::Coupled::QueryScope coupledPendingQuery(
                    PhysicsDiagnostic::Coupled::Role::PendingRefresh,pending.m_BodyA,pending.m_BodyB,
                    remaining,accumulatedTime);
#endif
				const QueryResult pendingQuery = remaining > 0.0f
					? QueryPredictedContact(pending.m_BodyA, pending.m_BodyB, remaining, current)
					: QueryCurrentToiContact(pending.m_BodyA, pending.m_BodyB, current);
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
                coupledPendingQuery.Finish(static_cast<int>(pendingQuery.outcome),
                    pendingQuery.reason,pendingQuery.diagnostics,current);
#endif
				const bool hit = pendingQuery.HasContact();
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
						if (!body->IsSleeping()) {
                            PhysicsDiagnostic::Observation integrationDiagnostic(PhysicsDiagnostic::Operation::Integrate,body,body,true);
                            if (integrationDiagnostic) { integrationDiagnostic.value.time=accumulatedTime; integrationDiagnostic.value.duration=dt; }
                            body->Update(dt); GE_PHYSICS_PROFILE_ADD(integratedBodyCount, 1);
                        }
					}
				}
				accumulatedTime = event.timeOfImpact;

				// Predictions supply scheduling only. Rebuild geometry at the actual pose;
				// failed/non-finite queries cannot supply an impulse. The resolver rejects
				// non-closing contact velocity using these current anchors and normal.
				contact_t current{};
				QueryResult currentQuery;
				{
					GE_PHYSICS_PROFILE_ADD(narrowphaseCallCount, 1);
					GE_PHYSICS_PROFILE_SCOPE(narrowphaseTimeNs);
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
                    PhysicsDiagnostic::Coupled::QueryScope coupledCurrentQuery(
                        PhysicsDiagnostic::Coupled::Role::CurrentToi,event.m_BodyA,event.m_BodyB,
                        0.0f,accumulatedTime);
#endif
					currentQuery = QueryCurrentToiContact(event.m_BodyA, event.m_BodyB, current);
#ifdef GE_ENABLE_COUPLED_COLLISION_OBSERVATION
                    coupledCurrentQuery.Finish(static_cast<int>(currentQuery.outcome),
                        currentQuery.reason,currentQuery.diagnostics,current);
#endif
				}
				if (currentQuery.HasContact() && IsFiniteContact(current))
				{
					current.timeOfImpact = accumulatedTime;
					GE_PHYSICS_PROFILE_SCOPE(contactResolutionTimeNs);
                    PhysicsDiagnostic::Observation toiDiagnostic(PhysicsDiagnostic::Operation::ToiQuery,current.m_BodyA,current.m_BodyB,true);
                    if (toiDiagnostic) {
                        toiDiagnostic.value.time=accumulatedTime; toiDiagnostic.value.normal=-current.normal;
                        toiDiagnostic.value.anchorA=current.ptOnA_WorldSpace; toiDiagnostic.value.anchorB=current.ptOnB_WorldSpace;
                        toiDiagnostic.value.localA=current.ptOnA_LocalSpace; toiDiagnostic.value.localB=current.ptOnB_LocalSpace;
                        toiDiagnostic.value.featureA=current.featureA; toiDiagnostic.value.featureB=current.featureB;
                        toiDiagnostic.value.penetration=-current.separationDistance;
                    }
                    toiDiagnostic.Finish();
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
							if (!PhysicsBodies[i]->IsSleeping()) {
                                PhysicsDiagnostic::Observation integrationDiagnostic(PhysicsDiagnostic::Operation::Integrate,PhysicsBodies[i],PhysicsBodies[i],true);
                                if (integrationDiagnostic) { integrationDiagnostic.value.time=accumulatedTime; integrationDiagnostic.value.duration=timeRemaining; }
                                PhysicsBodies[i]->Update(timeRemaining); GE_PHYSICS_PROFILE_ADD(integratedBodyCount, 1);
                            }
						}
					}
				}
			}

            observe(PhysicsDiagnostic::Stage::Integration);
			// Correct current resting penetration once, after all physical/TOI integration.
			{
				GE_PHYSICS_PROFILE_SCOPE(solverTimeNs);
				m_Manifolds.PostSolve();
			}

            observe(PhysicsDiagnostic::Stage::Correction);
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
						if (contact.m_BodyA->GetInverseMass() > 0.0f) { PhysicsDiagnostic::Observation sleepDiagnostic(PhysicsDiagnostic::Operation::SleepDecision,contact.m_BodyA,contact.m_BodyA,true); if (sleepDiagnostic) sleepDiagnostic.value.reason=4; contact.m_BodyA->WakeUp(); }
						if (contact.m_BodyB->GetInverseMass() > 0.0f) { PhysicsDiagnostic::Observation sleepDiagnostic(PhysicsDiagnostic::Operation::SleepDecision,contact.m_BodyB,contact.m_BodyB,true); if (sleepDiagnostic) sleepDiagnostic.value.reason=4; contact.m_BodyB->WakeUp(); }
					}
				}
				depthTimer.Stop();
				SleepPathTimer eligibilityTimer(m_SleepTimings.eligibilityNs);
				bool hasSleepCandidate = false;
				for (auto* body : std::span(PhysicsBodies.data(), PhysicsBodies.size())) {
					if (body->IsSleeping()) { hasSleepCandidate = true; continue; }
					if (body->GetInverseMass() <= 0.0f) { { PhysicsDiagnostic::Observation sleepDiagnostic(PhysicsDiagnostic::Operation::SleepDecision,body,body,true); if (sleepDiagnostic) sleepDiagnostic.value.reason=5; body->WakeUp(); } continue; }
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
					if (moved) { PhysicsDiagnostic::Observation sleepDiagnostic(PhysicsDiagnostic::Operation::SleepDecision,body,body,true); if (sleepDiagnostic) sleepDiagnostic.value.reason=6; body->WakeUp(); }
					else if (body->m_SleepSourceValid && !body->m_ExternalMutation && unchangedPose &&
						body->m_InactiveSeconds > 0.0 &&
						body->m_LinearVelocity == body->m_SleepSource.linearVelocity &&
						body->m_AngularVelocity == body->m_SleepSource.angularVelocity) {
						// Positive inactivity proves prior finite/speed eligibility. The complete
						// source is unchanged, as are this step's motion and the sleep policy
						// (policy setters wake/reset). Reuse that proof, not a connectivity decision.
						{ PhysicsDiagnostic::Observation sleepDiagnostic(PhysicsDiagnostic::Operation::SleepDecision,body,body,true); if (sleepDiagnostic) sleepDiagnostic.value.reason=7; body->AdvanceSleepTimer(dtSeconds); }
					}
					else { PhysicsDiagnostic::Observation sleepDiagnostic(PhysicsDiagnostic::Operation::SleepDecision,body,body,true); if (sleepDiagnostic) sleepDiagnostic.value.reason=8; body->UpdateSleepTimer(dtSeconds); }
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
							for (auto member : std::span(island.dynamicBodies.data(), island.dynamicBodies.size())) if (auto* dynamic = ResolveBody(member)) { PhysicsDiagnostic::Observation sleepDiagnostic(PhysicsDiagnostic::Operation::SleepDecision,dynamic,dynamic,true); if (sleepDiagnostic) sleepDiagnostic.value.reason=9; dynamic->WakeUp(); }
						}
					}
					if (ready) for (auto identity : std::span(island.dynamicBodies.data(), island.dynamicBodies.size())) if (auto* body = ResolveBody(identity)) {
                        PhysicsDiagnostic::Observation sleepDiagnostic(PhysicsDiagnostic::Operation::SleepDecision,body,body,true);
                        if (sleepDiagnostic) sleepDiagnostic.value.reason=10;
						body->TrySleep();
						body->m_LinearVelocity = body->m_AngularVelocity = Vec3f(0);
					}
				}
			}
            observe(PhysicsDiagnostic::Stage::Sleep);
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
        observe(PhysicsDiagnostic::Stage::End);
	}

    DebugGeometryResult PhysicsSystem::AppendColliderGeometry(DebugGeometry& out, const PhysicalShape& shape,
        const Vec3f& position, const Quat& orientation, const glm::vec4& color)
    {
        DebugGeometryResult collider;
                if (shape.GetShapeType()==ShapeType::Sphere)
                    collider=out.Sphere(position,static_cast<const ShapeSphere&>(shape).GetRadius(),color);
                else if (shape.GetShapeType()==ShapeType::Capsule) {
                    const auto& capsule=static_cast<const ShapeCapsule&>(shape);
                    const float radius=capsule.GetRadius(), half=capsule.GetHalfLength();
                    // Two meridian outlines and both cap seams, using the same
                    // dimensions as Support(). Append once: capacity is atomic.
                    std::array<FrameDebugLine,100> lines; std::size_t count=0;
                    const auto append=[&](Vec3f a,Vec3f b) {
                        lines[count++]={position+orientation*a,
                            position+orientation*b,color,{},DebugDepth::Tested};
                    };
                    for (int axis : {0,2}) {
                        for (float sign : {-1.f,1.f}) {
                            for (unsigned i=0;i<16;++i) {
                                const auto point=[&](unsigned j) {
                                    const float angle=float(j)*Math::Pi/16.f;
                                    Vec3f p(0); p[axis]=radius*std::cos(angle);
                                    p.y=sign*(half+radius*std::sin(angle)); return p;
                                };
                                append(point(i),point(i+1));
                            }
                            Vec3f a(0),b(0); a[axis]=b[axis]=sign*radius;
                            a.y=-half; b.y=half; append(a,b);
                        }
                    }
                    for (float sign : {-1.f,1.f}) for (unsigned i=0;i<16;++i) {
                        const auto point=[&](unsigned j) {
                            const float angle=float(j%16)*Math::TwoPi/16.f;
                            return Vec3f(radius*std::cos(angle),sign*half,radius*std::sin(angle));
                        };
                        append(point(i),point(i+1));
                    }
                    collider=out.Append(lines);
                }
                else if (shape.GetShapeType()==ShapeType::Convex) {
                    const auto& convex=static_cast<const ShapeConvex&>(shape);
                    collider=out.IndexedLines(convex.GetPoints(),convex.GetBoundaryEdges(),
                        position,orientation,color);
                }
                else {
                    const auto bounds=shape.GetBounds();
                    collider=out.Box(bounds.mins,bounds.maxs,position,orientation,
                        shape.GetShapeType()==ShapeType::Box?color:glm::vec4(1,0,1,1));
                }
        return collider;
    }

    namespace
    {
        auto TransformBodyFailure(PhysicsBodyErrorCode code, PhysicsBodyHandle handle, std::string_view op)
        { return std::unexpected(PhysicsBodyError{code, handle, op}); }

        bool AcceptBodyShape(const RigidBody3D& body, PhysicalShape& shape)
        {
            // All derived-data work is on a disposable probe: rejection cannot even
            // refresh a live body's lazy caches, let alone wake or invalidate it.
            RigidBody3D probe;
            probe.Type = body.Type; probe.m_InvMass = body.m_InvMass;
            probe.m_Position = body.m_Position; probe.m_Orientation = body.m_Orientation;
            probe.m_Shape = &shape;
            const auto matrixFinite = [](const Mat3& m) {
                return Math::IsFinite(m[0]) && Math::IsFinite(m[1]) && Math::IsFinite(m[2]);
            };
            const auto bounds = probe.GetWorldBounds();
            return shape.IsValid() && Math::IsFinite(bounds.mins) && Math::IsFinite(bounds.maxs) &&
                Math::IsFinite(probe.GetCenterOfMassWorldSpace()) && matrixFinite(shape.InertiaTensor()) &&
                matrixFinite(probe.GetInverseInertiaTensorBodySpace()) &&
                matrixFinite(probe.GetInverseInertiaTensorWorldSpace());
        }
    }

    PhysicsBodyResult<PhysicsBodyPose> PhysicsSystem::ReadBodyPose(PhysicsBodyHandle handle) const
    {
        if (!m_PhysicsWorld) return TransformBodyFailure(PhysicsBodyErrorCode::Closed, handle, "ReadBodyPose");
        auto resolved = m_PhysicsWorld->Resolve(handle, "ReadBodyPose");
        if (!resolved) return std::unexpected(resolved.error());
        const auto& b = **resolved;
        return PhysicsBodyPose{b.Type, b.m_Position, b.m_Orientation};
    }

    PhysicsBodyResult<bool> PhysicsSystem::SetBodyPose(PhysicsBodyHandle handle, const Vec3f& p, const Quat& q)
    {
        if (!m_PhysicsWorld) return TransformBodyFailure(PhysicsBodyErrorCode::Closed, handle, "SetBodyPose");
        auto resolved = m_PhysicsWorld->Resolve(handle, "SetBodyPose", true);
        if (!resolved) return std::unexpected(resolved.error());
        const auto position = (*resolved)->m_Position;
        const auto rotation = (*resolved)->m_Orientation;
        if (!SetBodyPose(*resolved, p, q))
            return TransformBodyFailure(PhysicsBodyErrorCode::InvalidPose, handle, "SetBodyPose");
        return position != (*resolved)->m_Position || rotation != (*resolved)->m_Orientation;
    }

    void PhysicsSystem::InvalidateBodyContacts(RigidBody3D* body)
    {
        WakeConnectedTo(body);
        m_ContactIslands.clear();
        RemoveManifoldsForBody(m_Manifolds, body);
        RemoveContactsForBody(m_Contacts, body);
        m_CollisionPairs.clear();
        m_Broadphase.Clear();
    }

    PhysicsBodyResult<bool> PhysicsSystem::SetBodyScale(PhysicsBodyHandle handle, const Vec3f& scale)
    {
        constexpr auto op = "SetBodyScale";
        if (!m_PhysicsWorld) return TransformBodyFailure(PhysicsBodyErrorCode::Closed, handle, op);
        auto resolved = m_PhysicsWorld->Resolve(handle, op, true);
        if (!resolved) return std::unexpected(resolved.error());
        auto* body = *resolved;
        if (!body->m_Shape) return TransformBodyFailure(PhysicsBodyErrorCode::InvalidShape, handle, op);
        auto staged = body->m_Shape->PrepareScale(scale);
        if (!staged)
            return TransformBodyFailure(staged.error().code == PhysicsShapeErrorCode::Allocation ?
                PhysicsBodyErrorCode::Allocation : PhysicsBodyErrorCode::InvalidShape, handle, op);
        if ((*staged)->GetRevision() < body->m_Shape->GetRevision() || !AcceptBodyShape(*body, **staged))
            return TransformBodyFailure(PhysicsBodyErrorCode::NonFiniteResult, handle, op);
        if ((*staged)->GetRevision() == body->m_Shape->GetRevision()) return false;
        InvalidateBodyContacts(body);
        body->m_Shape->CommitScale(**staged);
        return true;
    }

    PhysicsBodyResult<> PhysicsSystem::ReplaceBodyCollider(PhysicsBodyHandle handle, const CollisionShapeDesc& desc)
    {
        constexpr auto op = "ReplaceBodyCollider";
        if (!m_PhysicsWorld) return TransformBodyFailure(PhysicsBodyErrorCode::Closed, handle, op);
        auto resolved = m_PhysicsWorld->Resolve(handle, op, true);
        if (!resolved) return std::unexpected(resolved.error());
        auto& owned = m_PhysicsWorld->m_OwnedResources[handle.index].shape;
        if (!owned || owned.get() != (*resolved)->m_Shape)
            return TransformBodyFailure(PhysicsBodyErrorCode::InvalidShape, handle, op);
        auto staged = CreateCollisionShape(desc);
        if (!staged)
            return TransformBodyFailure(staged.error().code == PhysicsShapeErrorCode::Allocation ?
                PhysicsBodyErrorCode::Allocation : PhysicsBodyErrorCode::InvalidShape, handle, op);
        if (!AcceptBodyShape(**resolved, **staged))
            return TransformBodyFailure(PhysicsBodyErrorCode::NonFiniteResult, handle, op);
        InvalidateBodyContacts(*resolved);
        (*resolved)->m_Shape = staged->get();
        owned.swap(*staged); // Old owner retires here; handle, body and binding are unchanged.
        // A replacement may reuse an earlier shape address and local revision.
        // Retire all derived data at commit, even if its lazy source keys match.
        (*resolved)->m_DerivedData = {};
        return {};
    }

    void PhysicsSystem::ApplyLegacyBodyScale(PhysicsBodyHandle handle, const Vec3f& scale)
    {
        if (!m_PhysicsWorld) return;
        auto resolved = m_PhysicsWorld->Resolve(handle, "Component scale", true);
        if (!resolved || !(*resolved)->m_Shape) return;
        auto* body = *resolved;
        const auto revision = body->m_Shape->GetRevision();
        body->m_Shape->HandleScaleChanged(scale);
        if (body->m_Shape->GetRevision() != revision) InvalidateBodyContacts(body);
    }

    DebugGeometryResult PhysicsSystem::AppendPresentationGeometry(DebugGeometry& out, PhysicsBodyHandle handle,
        const Vec3f& position, const Quat& orientation) const
    {
        if (!m_PhysicsWorld) return std::unexpected(DebugGeometryCode::InvalidInput);
        auto resolved = m_PhysicsWorld->Resolve(handle, "AppendPresentationGeometry");
        if (!resolved || !(*resolved)->m_Shape || !(*resolved)->m_Shape->IsValid() ||
            !Math::IsFinite(position) || !Math::IsFinite(orientation) ||
            std::abs(glm::dot(orientation, orientation) - 1.f) > 1e-4f)
            return std::unexpected(DebugGeometryCode::InvalidInput);
        return AppendColliderGeometry(out, *(*resolved)->m_Shape, position, orientation, {1, .2f, 1, 1});
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

		InvalidateBodyContacts(body);
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

	static QueryResult ClassifyContactQuery(GjkContactStatus status, const GjkDiagnostics& diagnostics)
	{
		if (status == GjkContactStatus::Contact)
			return { QueryOutcome::Contact, PhysicsDiagnostic::QueryExit::None, diagnostics };
		if (status == GjkContactStatus::Failed)
			return { QueryOutcome::Failed, PhysicsDiagnostic::QueryExit::ContactSearchFailed, diagnostics };
		// Provisional separation is completed by the fresh closest-distance query
		// before it can leave IntersectAtQueryState.
		return { QueryOutcome::SeparatedValid, PhysicsDiagnostic::QueryExit::None, diagnostics };
	}

	static QueryResult ClassifyClosestQuery(const GjkDiagnostics& diagnostics,
		const Vec3f& ptOnA, const Vec3f& ptOnB, float separation)
	{
		if (diagnostics.termination == GjkTermination::OriginReached)
			return { QueryOutcome::Failed, PhysicsDiagnostic::QueryExit::NoProgress, diagnostics };
		if (diagnostics.termination != GjkTermination::DuplicateSupport &&
			diagnostics.termination != GjkTermination::NoProgress)
			return { QueryOutcome::Failed, PhysicsDiagnostic::QueryExit::ClosestDistanceFailed, diagnostics };
		if (!Math::IsFinite(ptOnA) || !Math::IsFinite(ptOnB) || !Math::IsFinite(separation) || separation < 0.0f)
			return { QueryOutcome::Failed, PhysicsDiagnostic::QueryExit::InvalidDistance, diagnostics };
		if (separation == 0.0f)
			return { QueryOutcome::Failed, PhysicsDiagnostic::QueryExit::NoProgress, diagnostics };
		return { QueryOutcome::SeparatedValid, PhysicsDiagnostic::QueryExit::None, diagnostics };
	}

	// A non-contact result never exposes an earlier query's contact payload.
	// Preserve the caller's labelled bodies while clearing every value field.
	static void ClearQueryContact(contact_t& contact)
	{
		contact.ptOnA_WorldSpace = contact.ptOnB_WorldSpace = Vec3f(0.0f);
		contact.ptOnA_LocalSpace = contact.ptOnB_LocalSpace = Vec3f(0.0f);
		contact.normal = Vec3f(0.0f);
		contact.separationDistance = contact.timeOfImpact = 0.0f;
		contact.featureA = contact.featureB = 0;
	}

	static PhysicsDiagnostic::QueryExit CaQueryDecision(const QueryResult& query, contact_t& contact)
	{
		if (query.HasContact()) return PhysicsDiagnostic::QueryExit::Contact;
		if (query.outcome == QueryOutcome::Failed) {
			ClearQueryContact(contact);
			return query.reason == PhysicsDiagnostic::QueryExit::None
				? PhysicsDiagnostic::QueryExit::InvalidDistance : query.reason;
		}
		return PhysicsDiagnostic::QueryExit::None;
	}
	static bool HasCaInterval(float dt) { return dt > 0.0f; }

	static PhysicsDiagnostic::QueryExit CaIntervalDecision(float dt, contact_t& contact)
	{
		if (!Math::IsFinite(dt) || dt < 0.0f) {
			ClearQueryContact(contact);
			return PhysicsDiagnostic::QueryExit::InvalidTime;
		}
		if (!HasCaInterval(dt)) {
			ClearQueryContact(contact);
			return PhysicsDiagnostic::QueryExit::IntervalExhausted;
		}
		return PhysicsDiagnostic::QueryExit::None;
	}

	static QueryResult CompleteClosestQuery(RigidBody3D* bodyA, RigidBody3D* bodyB,
		const GjkDiagnostics& diagnostics, const Vec3f& ptOnA, const Vec3f& ptOnB, contact_t& contact)
	{
		const float r = glm::length(ptOnA - ptOnB);
		const QueryResult query = ClassifyClosestQuery(diagnostics, ptOnA, ptOnB, r);
		if (query.outcome == QueryOutcome::Failed) {
			ClearQueryContact(contact);
			return query;
		}
		contact.ptOnA_WorldSpace = ptOnA;
		contact.ptOnB_WorldSpace = ptOnB;
		contact.ptOnA_LocalSpace = bodyA->WorldSpaceToBodySpace(contact.ptOnA_WorldSpace);
		contact.ptOnB_LocalSpace = bodyB->WorldSpaceToBodySpace(contact.ptOnB_WorldSpace);
		contact.separationDistance = r;
		return query;
	}

	// Internal narrow-phase evaluation accepts only query-local bodies.
	static QueryResult IntersectAtQueryState(RigidBody3D* bodyA, RigidBody3D* bodyB, contact_t& contact)
	{
		ClearQueryContact(contact);
		contact.m_BodyA = bodyA;
		contact.m_BodyB = bodyB;

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
				return { QueryOutcome::Contact };
			}
		}
		else 
		{
			//std::cout << "GJK_DoesIntersect" << std::endl;
			Vec3f ptOnA;
			Vec3f ptOnB;
			const float bias = 0.001f;
			GjkDiagnostics queryContactDiagnostics;
			const GjkContactStatus gjkStatus = GJK_GetContact(bodyA, bodyB, bias, ptOnA, ptOnB,
				&queryContactDiagnostics);
			const QueryResult contactQuery = ClassifyContactQuery(gjkStatus, queryContactDiagnostics);
			// P20_QUERY_OBSERVATION_BEGIN
			PhysicsDiagnostic::QueryRecord([&](auto& q) {
				q.kind=PhysicsDiagnostic::QueryKind::ContactSearch;
				q.contactStatus=static_cast<int>(gjkStatus); q.diagnostics=queryContactDiagnostics;
				q.a=PhysicsDiagnostic::Capture(*bodyA); q.b=PhysicsDiagnostic::Capture(*bodyB);
				q.witnessA=ptOnA; q.witnessB=ptOnB; q.contact=contact;
			});
			// P20_QUERY_OBSERVATION_END
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
				return contactQuery;
			}
			if (gjkStatus == GjkContactStatus::Failed)
			{
				return contactQuery;
			}

			// There was no collision, but we still want the contact data, so get it
			GjkDiagnostics queryClosestDiagnostics;
			GJK_ClosestPoints(bodyA, bodyB, ptOnA, ptOnB,
				&queryClosestDiagnostics);
			// P20_QUERY_OBSERVATION_BEGIN
			PhysicsDiagnostic::QueryRecord([&](auto& q) {
				q.kind=PhysicsDiagnostic::QueryKind::ClosestPoints; q.diagnostics=queryClosestDiagnostics;
				q.a=PhysicsDiagnostic::Capture(*bodyA); q.b=PhysicsDiagnostic::Capture(*bodyB);
				q.witnessA=ptOnA; q.witnessB=ptOnB;
			});
			// P20_QUERY_OBSERVATION_END
			// Producer diagnostics certify this fresh distance independently of observation.
			return CompleteClosestQuery(bodyA, bodyB, queryClosestDiagnostics, ptOnA, ptOnB, contact);
		}

		// The existing static sphere miss supplies no fresh distance to CA.
		return { QueryOutcome::Failed, PhysicsDiagnostic::QueryExit::InvalidDistance };

	}


	static QueryResult QueryInstantContact(RigidBody3D* bodyA, RigidBody3D* bodyB, contact_t& contact)
	{
		// Even a zero-time query can populate lazy caches; keep those writes local too.
		RigidBody3D predictedA = *bodyA;
		RigidBody3D predictedB = *bodyB;
		const QueryResult result = IntersectAtQueryState(&predictedA, &predictedB, contact);
		contact.m_BodyA = bodyA;
		contact.m_BodyB = bodyB;
		return result;
	}

	bool Collision::Intersect(RigidBody3D* bodyA, RigidBody3D* bodyB, contact_t& contact)
	{
		return QueryInstantContact(bodyA, bodyB, contact).HasContact();
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
                PhysicsDiagnostic::Observation diagnostic(PhysicsDiagnostic::Operation::ToiNormal,bodyA,bodyB);
                if (diagnostic) {
                    diagnostic.value.normal=-n; diagnostic.value.anchorA=ptOnA; diagnostic.value.anchorB=ptOnB;
                    diagnostic.value.linearImpulseB=vectorImpulseJ;
                    diagnostic.value.linearImpulseA=-vectorImpulseJ;
                    diagnostic.value.angularImpulseA=glm::cross(ra,-vectorImpulseJ);
                    diagnostic.value.angularImpulseB=glm::cross(rb,vectorImpulseJ);
                    diagnostic.value.appliedImpulsesKnown=true; diagnostic.value.time=contact.timeOfImpact;
                    diagnostic.value.localA=contact.ptOnA_LocalSpace; diagnostic.value.localB=contact.ptOnB_LocalSpace;
                    diagnostic.value.restitution=restitution; diagnostic.value.closingSpeed=normalSpeed;
                    diagnostic.value.penetration=-glm::dot(ptOnA-ptOnB,n);
                }
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
                        PhysicsDiagnostic::Observation diagnostic(PhysicsDiagnostic::Operation::ToiFriction,bodyA,bodyB);
                        if (diagnostic) {
                            diagnostic.value.normal=-n; diagnostic.value.anchorA=ptOnA; diagnostic.value.anchorB=ptOnB;
                            diagnostic.value.linearImpulseB=impulseFriction;
                            diagnostic.value.linearImpulseA=-impulseFriction;
                            diagnostic.value.angularImpulseA=glm::cross(ra,-impulseFriction);
                            diagnostic.value.angularImpulseB=glm::cross(rb,impulseFriction);
                            diagnostic.value.appliedImpulsesKnown=true; diagnostic.value.time=contact.timeOfImpact;
                            diagnostic.value.tangentU=tangent;
                            diagnostic.value.localA=contact.ptOnA_LocalSpace; diagnostic.value.localB=contact.ptOnB_LocalSpace;
                        }
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
            PhysicsDiagnostic::Observation diagnostic(PhysicsDiagnostic::Operation::ToiPosition,bodyA,bodyB);
            if (diagnostic) { diagnostic.value.normal=-n; diagnostic.value.anchorA=ptOnA; diagnostic.value.anchorB=ptOnB;
                diagnostic.value.time=contact.timeOfImpact; diagnostic.value.localA=contact.ptOnA_LocalSpace; diagnostic.value.localB=contact.ptOnB_LocalSpace; }
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

	static PhysicsDiagnostic::QueryExit CaAdvanceExit(PhysicsDiagnostic::QueryExit exit,
		contact_t& contact, float dt, float toi, float speed, float timeToGo = 0.0f)
	{
		ClearQueryContact(contact);
		PhysicsDiagnostic::QueryRecord([&](auto& q) {
			q.kind=PhysicsDiagnostic::QueryKind::Exit; q.exit=exit; q.contact=contact;
			q.remaining=dt; q.toi=toi; q.speed=speed; q.timeToGo=timeToGo;
		});
		return exit;
	}

	// The production advance and bounded regressions share these guards and updates.
	static PhysicsDiagnostic::QueryExit AdvanceCaQuery(contact_t& contact, float orthoSpeed,
		float& dt, float& toi, RigidBody3D& predictedA, RigidBody3D& predictedB)
	{
		if (!Math::IsFinite(contact.separationDistance) || contact.separationDistance < 0.0f)
			return CaAdvanceExit(PhysicsDiagnostic::QueryExit::InvalidDistance, contact, dt, toi, orthoSpeed);
		if (contact.separationDistance == 0.0f)
			return CaAdvanceExit(PhysicsDiagnostic::QueryExit::NoProgress, contact, dt, toi, orthoSpeed);
		if (!Math::IsFinite(orthoSpeed) || orthoSpeed <= Math::NumericalEpsilon)
			return CaAdvanceExit(PhysicsDiagnostic::QueryExit::InvalidOrNonclosingSpeed, contact, dt, toi, orthoSpeed);

		const float timeToGo = contact.separationDistance / orthoSpeed;
		PhysicsDiagnostic::QueryRecord([&](auto& q) {
			q.kind=PhysicsDiagnostic::QueryKind::Time; q.contact=contact; q.speed=orthoSpeed;
			q.remaining=dt; q.toi=toi; q.timeToGo=timeToGo;
		});
		if (!Math::IsFinite(timeToGo) || timeToGo < 0.0f)
			return CaAdvanceExit(PhysicsDiagnostic::QueryExit::InvalidTime, contact, dt, toi, orthoSpeed, timeToGo);
		if (timeToGo == 0.0f)
			return CaAdvanceExit(PhysicsDiagnostic::QueryExit::NoProgress, contact, dt, toi, orthoSpeed, timeToGo);
		if (timeToGo > dt)
			return CaAdvanceExit(PhysicsDiagnostic::QueryExit::TimeBeyondRemaining, contact, dt, toi, orthoSpeed, timeToGo);

		const float nextRemaining = dt - timeToGo;
		const float nextToi = toi + timeToGo;
		if (!Math::IsFinite(nextRemaining) || !Math::IsFinite(nextToi))
			return CaAdvanceExit(PhysicsDiagnostic::QueryExit::InvalidTime, contact, dt, toi, orthoSpeed, timeToGo);
		if (!(nextRemaining < dt) || !(nextToi > toi))
			return CaAdvanceExit(PhysicsDiagnostic::QueryExit::NoProgress, contact, dt, toi, orthoSpeed, timeToGo);

		dt = nextRemaining;
		toi = nextToi;
		predictedA.Update(timeToGo);
		predictedB.Update(timeToGo);
		return PhysicsDiagnostic::QueryExit::None;
	}

	static QueryResult ConservativeAdvanceQuery(RigidBody3D* bodyA, RigidBody3D* bodyB, float dt, contact_t& contact)
	{
		contact.featureA = contact.featureB = 0; // This query emits an unfeatured witness.
		contact.m_BodyA = bodyA;
		contact.m_BodyB = bodyB;
		PhysicsDiagnostic::QueryIteration(0);
		const auto intervalExit = CaIntervalDecision(dt, contact);
		if (intervalExit != PhysicsDiagnostic::QueryExit::None) {
			PhysicsDiagnostic::QueryRecord([&](auto& q) {
				q.kind=PhysicsDiagnostic::QueryKind::Exit; q.exit=intervalExit;
				q.contact=contact; q.remaining=dt; q.toi=0.0f;
			});
			return { QueryOutcome::Failed, intervalExit };
		}

		// Value copies own independent derived caches and are never registered in a world.
		// Advance them with the existing integrator; shapes are read-only during prediction.
		RigidBody3D predictedA = *bodyA;
		RigidBody3D predictedB = *bodyB;
		float toi = 0.0f;

		int numIters = 0;
		QueryResult query{ QueryOutcome::Failed, PhysicsDiagnostic::QueryExit::IntervalExhausted };

		// Advance the positions of the bodies until they touch or there's no time left.
		while (HasCaInterval(dt)) {

			// P20_QUERY_OBSERVATION_BEGIN
			PhysicsDiagnostic::QueryIteration(static_cast<unsigned>(numIters+1));
			PhysicsDiagnostic::QueryRecord([&](auto& q) {
				q.kind=PhysicsDiagnostic::QueryKind::Begin; q.remaining=dt; q.toi=toi;
				q.a=PhysicsDiagnostic::Capture(predictedA); q.b=PhysicsDiagnostic::Capture(predictedB);
				q.contact=contact;
			});
			// P20_QUERY_OBSERVATION_END
			query = IntersectAtQueryState(&predictedA, &predictedB, contact);
			// Never let query-local pointers escape, on either success or failure.
			contact.m_BodyA = bodyA;
			contact.m_BodyB = bodyB;
			const auto queryExit = CaQueryDecision(query, contact);
			// This Result is the instantaneous query; Exit describes the whole CA result.
			PhysicsDiagnostic::QueryRecord([&](auto& q) {
				q.kind=PhysicsDiagnostic::QueryKind::Result; q.hit=query.HasContact();
				q.exit=query.reason; q.diagnostics=query.diagnostics;
				q.contact=contact; q.remaining=dt; q.toi=toi;
			});
			if (queryExit == PhysicsDiagnostic::QueryExit::Contact) {
				contact.timeOfImpact = toi;
				PhysicsDiagnostic::QueryRecord([&](auto& q) {
					q.kind=PhysicsDiagnostic::QueryKind::Exit; q.exit=PhysicsDiagnostic::QueryExit::Contact;
					q.hit=true; q.diagnostics=query.diagnostics; q.contact=contact; q.remaining=dt; q.toi=toi;
				});
				return query;
			}
			if (queryExit != PhysicsDiagnostic::QueryExit::None) {
				PhysicsDiagnostic::QueryRecord([&](auto& q) {
					q.kind=PhysicsDiagnostic::QueryKind::Exit; q.exit=queryExit; q.diagnostics=query.diagnostics;
					q.contact=contact; q.remaining=dt; q.toi=toi;
				});
				return { QueryOutcome::Failed, queryExit, query.diagnostics };
			}
			++numIters;
			if (numIters > 10) {
				ClearQueryContact(contact);
				PhysicsDiagnostic::QueryRecord([&](auto& q) {
					q.kind=PhysicsDiagnostic::QueryKind::Exit; q.exit=PhysicsDiagnostic::QueryExit::IterationLimit;
					q.diagnostics=query.diagnostics; q.contact=contact; q.remaining=dt; q.toi=toi;
				});
				return { QueryOutcome::Failed, PhysicsDiagnostic::QueryExit::IterationLimit, query.diagnostics };
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
			// P20_QUERY_OBSERVATION_BEGIN
			PhysicsDiagnostic::QueryRecord([&](auto& q) {
				q.kind=PhysicsDiagnostic::QueryKind::SpeedTerms; q.direction=ab; q.relativeVelocity=relativeVelocity;
				q.projectedSpeed=orthoSpeed; q.angularA=angularSpeedA; q.angularB=angularSpeedB;
			});
			// P20_QUERY_OBSERVATION_END
			orthoSpeed += angularSpeedA + angularSpeedB;
			// P20_QUERY_OBSERVATION_BEGIN
			PhysicsDiagnostic::QueryRecord([&](auto& q) {
				q.kind=PhysicsDiagnostic::QueryKind::Speed; q.speed=orthoSpeed;
			});
			// P20_QUERY_OBSERVATION_END
			const auto advanceExit = AdvanceCaQuery(contact, orthoSpeed, dt, toi, predictedA, predictedB);
			if (advanceExit != PhysicsDiagnostic::QueryExit::None) {
				const bool validSeparation = advanceExit == PhysicsDiagnostic::QueryExit::TimeBeyondRemaining ||
					(advanceExit == PhysicsDiagnostic::QueryExit::InvalidOrNonclosingSpeed && Math::IsFinite(orthoSpeed));
				return { validSeparation ? QueryOutcome::SeparatedValid : QueryOutcome::Failed,
					advanceExit, query.diagnostics };
			}
		}
		// The last witness precedes the final advance; no endpoint query certified it.
		ClearQueryContact(contact);
		PhysicsDiagnostic::QueryRecord([&](auto& q) {
			q.kind=PhysicsDiagnostic::QueryKind::Exit; q.exit=PhysicsDiagnostic::QueryExit::IntervalExhausted;
			q.diagnostics=query.diagnostics; q.contact=contact; q.remaining=dt; q.toi=toi;
		});
		return { QueryOutcome::Failed, PhysicsDiagnostic::QueryExit::IntervalExhausted, query.diagnostics };
	}

	bool Collision::ConservativeAdvance(RigidBody3D* bodyA, RigidBody3D* bodyB, float dt, contact_t& contact)
	{
		return ConservativeAdvanceQuery(bodyA, bodyB, dt, contact).HasContact();
	}

	static QueryResult QueryPredictedContact(RigidBody3D* bodyA, RigidBody3D* bodyB, float dt, contact_t& contact)
	{
		if (bodyA->m_Shape->GetShapeType() == ShapeType::Sphere && bodyB->m_Shape->GetShapeType() == ShapeType::Sphere)
		{
			return { Collision::SphereSphereIntersect(bodyA, bodyB, dt, contact)
				? QueryOutcome::Contact : QueryOutcome::SeparatedValid };
		}
		return ConservativeAdvanceQuery(bodyA, bodyB, dt, contact);
	}

	bool Collision::Intersect(RigidBody3D* bodyA, RigidBody3D* bodyB, const float dt, contact_t& contact)
	{
		return QueryPredictedContact(bodyA, bodyB, dt, contact).HasContact();
	}

	PhysicsDiagnostic::CaContractObservation PhysicsDiagnostic::EvaluateCaContract(
		unsigned scenario, RigidBody3D* seedA, RigidBody3D* seedB)
	{
		CaContractObservation observation;
		RigidBody3D predictedA = *seedA;
		RigidBody3D predictedB = *seedB;
		contact_t contact{};
		contact.m_BodyA = seedA;
		contact.m_BodyB = seedB;
		contact.ptOnA_WorldSpace = Vec3f(1.0f, 2.0f, 3.0f);
		contact.ptOnB_WorldSpace = Vec3f(2.0f, 2.0f, 3.0f);
		contact.ptOnA_LocalSpace = Vec3f(4.0f, 5.0f, 6.0f);
		contact.ptOnB_LocalSpace = Vec3f(7.0f, 8.0f, 9.0f);
		contact.normal = Vec3f(1.0f, 0.0f, 0.0f);
		contact.separationDistance = 0.25f;
		float remaining = scenario == 5 ? 0.0f : 1.0f;
		float toi = scenario == 4 ? 1.0f : 0.0f;
		const auto consume = [&](const QueryResult& query) {
			++observation.queries;
			QueryIteration(observation.queries);
			observation.queryOutcome = static_cast<int>(query.outcome);
			observation.queryReason = query.reason;
			observation.diagnostics = query.diagnostics;
			observation.exit = CaQueryDecision(query, contact);
			QueryRecord([&](auto& q) {
				q.kind=QueryKind::Result; q.hit=query.HasContact(); q.exit=query.reason;
				q.diagnostics=query.diagnostics; q.contact=contact; q.remaining=remaining; q.toi=toi;
			});
			if (observation.exit != QueryExit::None) {
				QueryRecord([&](auto& q) {
					q.kind=QueryKind::Exit; q.exit=observation.exit; q.diagnostics=query.diagnostics;
					q.contact=contact; q.remaining=remaining; q.toi=toi;
				});
				return false;
			}
			observation.exit = AdvanceCaQuery(contact, 1.0f, remaining, toi, predictedA, predictedB);
			if (observation.exit == QueryExit::None) ++observation.advances;
			return observation.exit == QueryExit::None;
		};
		const auto failedContact = [&]() {
			GjkDiagnostics diagnostics;
			diagnostics.termination = GjkTermination::ContactExpansionFailed;
			diagnostics.epa.termination = EpaTermination::InvalidProjection;
			return ClassifyContactQuery(GjkContactStatus::Failed, diagnostics);
		};
		const auto closest = [&](GjkTermination termination, float distance) {
			GjkDiagnostics contactDiagnostics;
			contactDiagnostics.termination = GjkTermination::SeparatingAxis;
			const QueryResult contactQuery = ClassifyContactQuery(GjkContactStatus::Separated, contactDiagnostics);
			if (contactQuery.outcome != QueryOutcome::SeparatedValid) return contactQuery;
			GjkDiagnostics distanceDiagnostics;
			distanceDiagnostics.termination = termination;
			return CompleteClosestQuery(&predictedA, &predictedB, distanceDiagnostics,
				Vec3f(0.0f), Vec3f(distance, 0.0f, 0.0f), contact);
		};
		QueryIteration(0);
		observation.exit = CaIntervalDecision(remaining, contact);
		if (observation.exit != QueryExit::None) {
			QueryRecord([&](auto& q) {
				q.kind=QueryKind::Exit; q.exit=observation.exit;
				q.contact=contact; q.remaining=remaining; q.toi=toi;
			});
		}
		else if (scenario == 0) consume(failedContact());
		else if (scenario == 1) {
			if (consume(closest(GjkTermination::DuplicateSupport, 0.25f))) consume(failedContact());
		}
		else if (scenario == 2) consume(closest(GjkTermination::InvalidSupport, 0.0f));
		else if (scenario == 3) consume(closest(GjkTermination::DuplicateSupport, 0.0f));
		else if (scenario == 4) consume(closest(GjkTermination::DuplicateSupport, 0x1p-30f));
		else observation.exit = QueryExit::InvalidDistance;
		observation.remaining = remaining;
		observation.toi = toi;
		observation.contact = contact;
		observation.predictedA = Capture(predictedA);
		observation.predictedB = Capture(predictedB);
		return observation;
	}

}
