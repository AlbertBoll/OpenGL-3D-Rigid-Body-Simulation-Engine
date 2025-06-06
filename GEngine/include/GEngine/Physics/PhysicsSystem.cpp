#include"gepch.h"

#include "PhysicsSystem.h"
//#include"PhysicsBody.h"
#include "ShapeSphere.h"
#include "PhysicsWorld.h"
#include "Broadphase.h"
#include "GJK.h"
#include <Core/Timer.h>

namespace GEngine
{
	static int CompareContacts(const void* p1, const void* p2) {
		contact_t a = *(contact_t*)p1;
		contact_t b = *(contact_t*)p2;

		if (a.timeOfImpact < b.timeOfImpact) {
			return -1;
		}

		if (a.timeOfImpact == b.timeOfImpact) {
			return 0;
		}

		return 1;
	}

	
	//static int CompareContacts(const contact_t& p1, const contact_t& p2)
	//{
	//	if (p1.timeOfImpact <= p2.timeOfImpact) {
	//		return 0;
	//	}

	//	/*if (p1.timeOfImpact == p2.timeOfImpact) {
	//		return 0;
	//	}*/

	//	return 1;
	//}



	static bool RaySphere(const Vec3f& rayStart, const Vec3f& rayDir, const Vec3f& sphereCenter, const float sphereRadius, float& t1, float& t2) {
		const Vec3f m = sphereCenter - rayStart;
		const float a = glm::length2(rayDir);//rayDir.Dot(rayDir);
		const float b = glm::dot(m, rayDir); // m.Dot(rayDir);
		const float c = glm::length2(m) - sphereRadius * sphereRadius;

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
		Vec3f norm = ab;
		norm = glm::normalize(norm);

		ptOnA = posA + norm * sphereA->m_Radius;
		ptOnB = posB - norm * sphereB->m_Radius;

		const float radiusAB = sphereA->m_Radius + sphereB->m_Radius;
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
			float radius = shapeA->m_Radius + shapeB->m_Radius + 0.001f;
			if (glm::length2(ab) > radius * radius) {
				return false;
			}
		}
		else if (!RaySphere(posA, rayDir, posB, shapeA->m_Radius + shapeB->m_Radius, t0, t1)) {
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
		float inv = 1.0f / glm::length(ab);
		//if (0.0f * inv == 0.0f * inv)
		//{
			ab = glm::normalize(ab);
		//}

		ptOnA = newPosA + ab * shapeA->m_Radius;
		ptOnB = newPosB - ab * shapeB->m_Radius;
		return true;
	}


	PhysicsSystem::~PhysicsSystem()
	{
		if (m_PhysicsWorld)delete m_PhysicsWorld;
		m_PhysicsWorld = nullptr;
	}

	void PhysicsSystem::Initialize()
	{

	}

	void PhysicsSystem::Update(Timestep ts)
	{

		using namespace Collision;
		{
			//Timeit("	m_Manifolds-RemoveExpired")
			m_Manifolds.RemoveExpired();
		}

		if (m_PhysicsWorld)
		{
			auto& PhysicsBodies = m_PhysicsWorld->GetPhysicsBodies();
			size_t size = PhysicsBodies.size();
			auto gravity = m_PhysicsWorld->GetGravity();

			// Gravity impulse
			{
				//Timeit("	apply linear impluse to dynamic entities")
				for (size_t i = 0; i < size; i++)
				{
					RigidBody3D* body = PhysicsBodies[i];
					float mass = 1.0f / body->m_InvMass;
					Vec3f impulseGravity = gravity * mass * (float)ts;
					body->ApplyImpulseLinear(impulseGravity);
				}
			}

			//
			// Broadphase
			//
			std::vector<collisionPair_t> collisionPairs;
			{
				//Timeit("	BroadPhase")
				BroadPhase(PhysicsBodies, collisionPairs, (float)ts);
			}

			//
			//	NarrowPhase (perform actual collision detection)
			//
			int numContacts = 0;
			const size_t maxContacts = size * size;
			static std::vector<contact_t> contacts;
			
			contacts.reserve(maxContacts);

			//contact_t* contacts = (contact_t*)alloca(sizeof(contact_t) * maxContacts);
			for (int i = 0; i < collisionPairs.size(); i++) {
				const collisionPair_t& pair = collisionPairs[i];
				RigidBody3D* bodyA = PhysicsBodies[pair.a];
				RigidBody3D* bodyB = PhysicsBodies[pair.b];

				// Skip body pairs with infinite mass
				if (bodyA->Type == BodyType::Static && bodyB->Type == BodyType::Static)
				{
					continue;
				}

				contact_t contact;

				if(Intersect(bodyA, bodyB, (float)ts, contact))
				{
					if (0.0f == contact.timeOfImpact)
					{
						//std::cout << "0.0f occurred" << std::endl;
						
						m_Manifolds.AddContact(contact);
						
					}
					else
					{
						//std::cout << "Collision occurred" << std::endl;
						GENGINE_INFO("Collision occurred");
						contacts.push_back(contact);
						//contacts[numContacts] = contact;
						numContacts++;
						//
					}
				}
			}

			// Sort the times of impact from first to last
			if (numContacts > 1) {
				{
					//Timeit("	qsort")
					qsort(contacts.data(), numContacts, sizeof(contact_t), CompareContacts);
				}
				//std::sort(contacts.begin(), contacts.end(), CompareContacts);
			}

			{
				//Timeit("	m_Manifolds PreSolve")
				////Solve the Constraints
				m_Manifolds.PreSolve(ts);
			}

			{
				//Timeit("	m_Manifolds Solve")
				const int maxIters = 1;
				for (int iters = 0; iters < maxIters; iters++)
				{
					m_Manifolds.Solve();
				}
			}

			{
				//Timeit("	m_Manifolds PostSolve")
				//m_Manifolds.PostSolve();
			}


			//
			// Apply ballistic impulses
			//
			float accumulatedTime = 0.0f;
			for (int i = 0; i < numContacts; i++) {
				contact_t& contact = contacts[i];
				const float dt = contact.timeOfImpact - accumulatedTime;

				// Position update
				{
					//Timeit("	all entities update positions and rotation")
					for (int j = 0; j < size; j++) {
						PhysicsBodies[j]->Update(dt);
					}
				}

				{
					//Timeit("	Resolve contact")
					ResolveContact(contact);
				}
				accumulatedTime += dt;
			}

			// Update the positions for the rest of this frame's time
			const float timeRemaining = (float)ts - accumulatedTime;
			if (timeRemaining > 0.0f) {
				{
					{
						//Timeit("	update entities rest")
						for (int i = 0; i < size; i++) 
						{
							PhysicsBodies[i]->Update(timeRemaining);
						}
					}
				}
			}

			contacts.clear();

		}

	}

	void PhysicsSystem::OnExit()
	{
		delete m_PhysicsWorld;
		m_PhysicsWorld = nullptr;
	}

	void PhysicsSystem::SetPhysicsWorld(PhysicsWorld* physics_world)
	{
		m_PhysicsWorld = physics_world;
	}



	bool Collision::SphereSphereIntersect(RigidBody3D* bodyA, RigidBody3D* bodyB, const float dt, contact_t& contact)
	{
		contact.m_BodyA = bodyA;
		contact.m_BodyB = bodyB;

		//if (bodyA->m_shape->GetType() == Shape::SHAPE_SPHERE && bodyB->m_shape->GetType() == Shape::SHAPE_SPHERE) {
		const ShapeSphere* sphereA = (const ShapeSphere*)bodyA->m_Shape;
		const ShapeSphere* sphereB = (const ShapeSphere*)bodyB->m_Shape;

		Vec3f posA = bodyA->m_Position;
		Vec3f posB = bodyB->m_Position;

		Vec3f velA = bodyA->m_LinearVelocity;
		Vec3f velB = bodyB->m_LinearVelocity;

		if (SphereSphereDynamic(sphereA, sphereB, posA, posB, velA, velB, dt, contact.ptOnA_WorldSpace, contact.ptOnB_WorldSpace, contact.timeOfImpact)) {
			// Step bodies forward to get local space collision points
			bodyA->Update(contact.timeOfImpact);
			bodyB->Update(contact.timeOfImpact);

			// Convert world space contacts to local space
			contact.ptOnA_LocalSpace = bodyA->WorldSpaceToBodySpace(contact.ptOnA_WorldSpace);
			contact.ptOnB_LocalSpace = bodyB->WorldSpaceToBodySpace(contact.ptOnB_WorldSpace);

			contact.normal = bodyA->m_Position - bodyB->m_Position;
			contact.normal = glm::normalize(contact.normal);

			// Unwind time step
			bodyA->Update(-contact.timeOfImpact);
			bodyB->Update(-contact.timeOfImpact);

			// Calculate the separation distance
			Vec3f ab = bodyB->m_Position - bodyA->m_Position;
			float r = glm::length(ab) - (sphereA->m_Radius + sphereB->m_Radius);
			contact.separationDistance = r;
			return true;
		}
		//}
		return false;
	}

	bool Collision::Intersect(RigidBody3D* bodyA, RigidBody3D* bodyB, contact_t& contact)
	{
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
				contact.normal = glm::normalize(posA - posB);

				contact.ptOnA_LocalSpace = bodyA->WorldSpaceToBodySpace(contact.ptOnA_WorldSpace);
				contact.ptOnB_LocalSpace = bodyB->WorldSpaceToBodySpace(contact.ptOnB_WorldSpace);

				Vec3f ab = bodyB->m_Position - bodyA->m_Position;
				float r = glm::length(ab) - (sphereA->m_Radius + sphereB->m_Radius);
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
			if (GJK_DoesIntersect(bodyA, bodyB, bias, ptOnA, ptOnB)) 
			{
				//std::cout << "GJK_DoesIntersect" << std::endl;
				// There was an intersection, so get the contact data
				//std::cout << "normal: length" << glm::length(ptOnB - ptOnA) << std::endl;
				Vec3f normal = glm::normalize(ptOnB - ptOnA);
				

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


	void Collision::ResolveContact(contact_t& contact)
	{
		RigidBody3D* bodyA = contact.m_BodyA;
		RigidBody3D* bodyB = contact.m_BodyB;

		const Vec3f ptOnA = bodyA->BodySpaceToWorldSpace(contact.ptOnA_LocalSpace);
		const Vec3f ptOnB = bodyB->BodySpaceToWorldSpace(contact.ptOnB_LocalSpace);

		const float elasticityA = bodyA->m_Elasticity;
		const float elasticityB = bodyB->m_Elasticity;
		const float elasticity = elasticityA * elasticityB;

		const float invMassA = bodyA->m_InvMass;
		const float invMassB = bodyB->m_InvMass;

		const Mat3 invWorldInertiaA = bodyA->GetInverseInertiaTensorWorldSpace();
		const Mat3 invWorldInertiaB = bodyB->GetInverseInertiaTensorWorldSpace();

		const Vec3f n = contact.normal;

		const Vec3f ra = ptOnA - bodyA->GetCenterOfMassWorldSpace();
		const Vec3f rb = ptOnB - bodyB->GetCenterOfMassWorldSpace();

		const Vec3f angularJA = glm::cross(invWorldInertiaA * glm::cross(ra, n), ra);
		const Vec3f angularJB = glm::cross(invWorldInertiaB * glm::cross(rb, n), rb);//(invWorldInertiaB * rb.Cross(n)).Cross(rb);
		const float angularFactor = glm::dot(angularJA + angularJB, n);

		// Get the world space velocity of the motion and rotation
		const Vec3f velA = bodyA->m_LinearVelocity + glm::cross(bodyA->m_AngularVelocity, ra);
		const Vec3f velB = bodyB->m_LinearVelocity + glm::cross(bodyB->m_AngularVelocity, rb);//bodyB->m_AngularVelocity.Cross(rb);

		// Calculate the collision impulse
		const Vec3f vab = velA - velB;
		const float ImpulseJ = (1.0f + elasticity) * glm::dot(vab, n) / (invMassA + invMassB + angularFactor);
		const Vec3f vectorImpulseJ = n * ImpulseJ;

		bodyA->ApplyImpulse(ptOnA, vectorImpulseJ * -1.0f);
		bodyB->ApplyImpulse(ptOnB, vectorImpulseJ * 1.0f);

		//
		// Calculate the impulse caused by friction
		//

		const float frictionA = bodyA->m_Friction;
		const float frictionB = bodyB->m_Friction;
		const float friction = frictionA * frictionB;

		// Find the normal direction of the velocity with respect to the normal of the collision
		const Vec3f velNorm = n * glm::dot(n, vab);

		// Find the tangent direction of the velocity with respect to the normal of the collision
		const Vec3f velTang = vab - velNorm;

		// Get the tangential velocities relative to the other body
		Vec3f relativeVelTang = velTang;
		if(glm::length(relativeVelTang) >= 0.0000001f)
			relativeVelTang = glm::normalize(relativeVelTang);

		const Vec3f inertiaA = glm::cross(invWorldInertiaA * glm::cross(ra, relativeVelTang), ra);
		const Vec3f inertiaB = glm::cross(invWorldInertiaB * glm::cross(rb, relativeVelTang), rb);//(invWorldInertiaB * rb.Cross(relativeVelTang)).Cross(rb);
		const float invInertia = glm::dot(inertiaA + inertiaB, relativeVelTang);

		// Calculate the tangential impulse for friction
		const float reducedMass = 1.0f / (bodyA->m_InvMass + bodyB->m_InvMass + invInertia);
		const Vec3f impulseFriction = velTang * reducedMass * friction;

		// Apply kinetic friction
		bodyA->ApplyImpulse(ptOnA, impulseFriction * -1.0f);
		bodyB->ApplyImpulse(ptOnB, impulseFriction * 1.0f);

		//
		// Let's also move our colliding objects to just outside of each other (projection method)
		//
		if (contact.timeOfImpact == 0.0f) {
			const Vec3f ds = ptOnB - ptOnA;

			const float tA = invMassA / (invMassA + invMassB);
			const float tB = invMassB / (invMassA + invMassB);

			bodyA->m_Position += ds * tA;
			bodyB->m_Position -= ds * tB;
		}




	}

	bool Collision::ConservativeAdvance(RigidBody3D* bodyA, RigidBody3D* bodyB, float dt, contact_t& contact)
	{
		contact.m_BodyA = bodyA;
		contact.m_BodyB = bodyB;

		float toi = 0.0f;

		int numIters = 0;

		// Advance the positions of the bodies until they touch or there's not time left
		while (dt > 0.0f) {
			
			// Check for intersection
			bool didIntersect = Intersect(bodyA, bodyB, contact);
			if (didIntersect) {
				//std::cout << "Intersection" << std::endl;
				contact.timeOfImpact = toi;
				bodyA->Update(-toi);
				bodyB->Update(-toi);
				return true;
			}
			//std::cout << "No intersection" << std::endl;
			++numIters;
			if (numIters > 10) {
				break;
			}
			
			// Get the vector from the closest point on A to the closest point on B
			Vec3f ab = glm::normalize(contact.ptOnB_WorldSpace - contact.ptOnA_WorldSpace);
			//std::cout << "ab: " << ab.x << ", " << ab.y << ", " << ab.z << std::endl;

			// project the relative velocity onto the ray of shortest distance
			Vec3f relativeVelocity = bodyA->m_LinearVelocity - bodyB->m_LinearVelocity;
			float orthoSpeed = glm::dot(relativeVelocity, ab);

			// Add to the orthoSpeed the maximum angular speeds of the relative shapes
			float angularSpeedA = bodyA->m_Shape->FastestLinearSpeed(bodyA->m_AngularVelocity, ab);
			float angularSpeedB = bodyB->m_Shape->FastestLinearSpeed(bodyB->m_AngularVelocity, ab * -1.0f);
			orthoSpeed += angularSpeedA + angularSpeedB;
			if (orthoSpeed <= 0.0f) {
				break;
			}

			float timeToGo = contact.separationDistance / orthoSpeed;
			if (timeToGo > dt) {
				break;
			}

			dt -= timeToGo;
			toi += timeToGo;
			bodyA->Update(timeToGo);
			bodyB->Update(timeToGo);
		}

		// unwind the clock
		bodyA->Update(-toi);
		bodyB->Update(-toi);
		return false;
	}


	bool Collision::Intersect(RigidBody3D* bodyA, RigidBody3D* bodyB, const float dt, contact_t& contact)
	{
		contact.m_BodyA = bodyA;
		contact.m_BodyB = bodyB;
		//if (dynamic_cast<ShapeSphere*>(bodyA->m_Shape) && dynamic_cast<ShapeSphere*>(bodyB->m_Shape)) {
		if (bodyA->m_Shape->GetShapeType() == ShapeType::Sphere && bodyB->m_Shape->GetShapeType() == ShapeType::Sphere)
		{
			const ShapeSphere* sphereA = (const ShapeSphere*)bodyA->m_Shape;
			const ShapeSphere* sphereB = (const ShapeSphere*)bodyB->m_Shape;

			Vec3f posA = bodyA->m_Position;
			Vec3f posB = bodyB->m_Position;

			Vec3f velA = bodyA->m_LinearVelocity;
			Vec3f velB = bodyB->m_LinearVelocity;

			if (SphereSphereDynamic(sphereA, sphereB, posA, posB, velA, velB, dt, contact.ptOnA_WorldSpace, contact.ptOnB_WorldSpace, contact.timeOfImpact)) {
				// Step bodies forward to get local space collision points
				bodyA->Update(contact.timeOfImpact);
				bodyB->Update(contact.timeOfImpact);

				// Convert world space contacts to local space
				contact.ptOnA_LocalSpace = bodyA->WorldSpaceToBodySpace(contact.ptOnA_WorldSpace);
				contact.ptOnB_LocalSpace = bodyB->WorldSpaceToBodySpace(contact.ptOnB_WorldSpace);

				contact.normal = glm::normalize(bodyA->m_Position - bodyB->m_Position);

				// Unwind time step
				bodyA->Update(-contact.timeOfImpact);
				bodyB->Update(-contact.timeOfImpact);

				// Calculate the separation distance
				Vec3f ab = bodyB->m_Position - bodyA->m_Position;
				float r = glm::length(ab) - (sphereA->m_Radius + sphereB->m_Radius);
				contact.separationDistance = r;
				return true;

			}
			
		}
		else
		{
			//Use GJK to perform conservative advancement
			//std::cout << "GJK conservative advance" << std::endl;
			bool result = ConservativeAdvance(bodyA, bodyB, dt, contact);
			return result;
			
		}
		return false;
	}

}
