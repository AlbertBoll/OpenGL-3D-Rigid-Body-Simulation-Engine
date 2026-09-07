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

namespace GEngine
{
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

	void PhysicsSystem::Update(Timestep ts)
	{
		const float dtSeconds = static_cast<float>(ts);
		GENGINE_CORE_ASSERT(Math::IsFinite(dtSeconds), "Physics timestep must be finite");
		if (!Math::IsFinite(dtSeconds))
		{
			return;
		}

		GE_PHYSICS_PROFILE_SCOPE(physicsWorldTimeNs);
		GE_PHYSICS_PROFILE_ADD(stepCount, 1);

		using namespace Collision;
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
			for (const RigidBody3D* body : PhysicsBodies)
			{
				GENGINE_CORE_ASSERT(body != nullptr, "Physics world must not contain null bodies");
				if (body)
				{
					body->AssertFiniteState();
				}
			}
#ifdef GE_ENABLE_PHYSICS_PROFILING
			std::uint64_t dynamicBodyCount = 0;
			std::uint64_t activeBodyCount = 0;
			for (const RigidBody3D* body : PhysicsBodies)
			{
				if (body->Type == BodyType::Dynamic)
				{
					++dynamicBodyCount;
				}
				if (body->Type != BodyType::Static)
				{
					++activeBodyCount;
				}
			}
			GE_PHYSICS_PROFILE_SET(bodyCount, size);
			GE_PHYSICS_PROFILE_SET(dynamicBodyCount, dynamicBodyCount);
			GE_PHYSICS_PROFILE_SET(activeBodyCount, activeBodyCount);
			GE_PHYSICS_PROFILE_SET(sleepingBodyCount, 0);
#endif

			// Gravity impulse
			{
				GE_PHYSICS_PROFILE_SCOPE(gravityTimeNs);
				//Timeit("	apply linear impluse to dynamic entities")
				for (size_t i = 0; i < size; i++)
				{
					RigidBody3D* body = PhysicsBodies[i];
					if (body->Type == BodyType::Dynamic && body->GetInverseMass() > 0.0f)
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

				contact_t contact{};

				GE_PHYSICS_PROFILE_ADD(narrowphaseCallCount, 1);
				GE_PHYSICS_PROFILE_SCOPE_NAMED(narrowphaseTimer, narrowphaseTimeNs);
				const bool didIntersect = Intersect(bodyA, bodyB, (float)ts, contact);
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
			const int manifoldContactCount = m_Manifolds.GetContactCount();
			GE_PHYSICS_PROFILE_SET(manifoldCount, m_Manifolds.m_Manifolds.size());
			GE_PHYSICS_PROFILE_SET(manifoldContactCount, manifoldContactCount);
			GE_PHYSICS_PROFILE_SET(solverConstraintCount, manifoldContactCount);
#endif

			{
				GE_PHYSICS_PROFILE_SCOPE(solverTimeNs);
				//Timeit("	m_Manifolds PreSolve")
				////Solve the Constraints
				m_Manifolds.PreSolve(ts);
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
						body->Update(dt);
					}
					GE_PHYSICS_PROFILE_ADD(integratedBodyCount, size);
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
							PhysicsBodies[i]->Update(timeRemaining);
						}
						GE_PHYSICS_PROFILE_ADD(integratedBodyCount, size);
					}
				}
			}

			// Correct current resting penetration once, after all physical/TOI integration.
			{
				GE_PHYSICS_PROFILE_SCOPE(solverTimeNs);
				m_Manifolds.PostSolve();
			}

			m_Contacts.clear();
			for (const RigidBody3D* body : PhysicsBodies)
			{
				if (body)
				{
					body->AssertFiniteState();
				}
			}

		}

	}

	void PhysicsSystem::OnExit()
	{
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
