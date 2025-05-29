#include "gepch.h"
#include "Broadphase.h"
#include <Math/Math.h>
#include "Bounds.h"
#include "PhysicsBody.h"
#include"Shape.h"

namespace GEngine
{
	using namespace Math;

	struct psuedoBody_t {
		int id;
		float value;
		bool ismin;
		psuedoBody_t() = default;
		psuedoBody_t(int _id, float value, bool is_min): id(_id), value(value), ismin(is_min){}

	};


	/*
	====================================================
	CompareSAP
	====================================================
	*/
	int CompareSAP(const void* a, const void* b) {
		const psuedoBody_t* ea = (const psuedoBody_t*)a;
		const psuedoBody_t* eb = (const psuedoBody_t*)b;

		if (ea->value < eb->value) {
			return -1;
		}
		return 1;
	}

	/*bool CompareSAP(const psuedoBody_t& a, const psuedoBody_t& b) 
	{	
		return a.value < b.value;
	}*/


	/*
	====================================================
	SortBodiesBounds
	====================================================
	*/
	void SortBodiesBounds(const std::vector<RigidBody3D*>& bodies, psuedoBody_t* sortedArray, const float dt_sec) {
		Vec3f axis = glm::normalize(Vec3f(1, 1, 1));

		size_t num = bodies.size();

		for (int i = 0; i < num; i++) {
			const RigidBody3D& body = *bodies[i];
			
			Bounds bounds = body.m_Shape->GetBounds(body.m_Position, body.m_Orientation);
		
			// Expand the bounds by the linear velocity
			bounds.Expand(bounds.mins + body.m_LinearVelocity * dt_sec);
			bounds.Expand(bounds.maxs + body.m_LinearVelocity * dt_sec);

			const float epsilon = 0.01f;
			bounds.Expand(bounds.mins + Vec3f(-1, -1, -1) * epsilon);
			bounds.Expand(bounds.maxs + Vec3f(1, 1, 1) * epsilon);

			sortedArray[i * 2 + 0].id = i;
			sortedArray[i * 2 + 0].value = glm::dot(axis, bounds.mins);
			sortedArray[i * 2 + 0].ismin = true;

			sortedArray[i * 2 + 1].id = i;
			sortedArray[i * 2 + 1].value = glm::dot(axis, bounds.maxs);
			sortedArray[i * 2 + 1].ismin = false;
		}

		qsort(sortedArray, num * 2, sizeof(psuedoBody_t), CompareSAP);
	}

	void SortBodiesBounds(const std::vector<RigidBody3D*>& bodies, std::vector<psuedoBody_t>& sortedArray, const float dt_sec) {
		Vec3f axis = glm::normalize(Vec3f(1, 1, 1));

		size_t num = bodies.size();
		const float epsilon = 0.01f;
		Vec3f a = Vec3f(-1, -1, -1) * epsilon;
		Vec3f b = Vec3f(1, 1, 1) * epsilon;

		for (int i = 0; i < num; i++) {
			const RigidBody3D& body = *bodies[i];
			Vec3f extend = body.m_LinearVelocity * dt_sec;

			Bounds bounds = body.m_Shape->GetBounds(body.m_Position, body.m_Orientation);

			// Expand the bounds by the linear velocity
			bounds.Expand(bounds.mins + extend);
			bounds.Expand(bounds.maxs + extend);

			
			bounds.Expand(bounds.mins + a);
			bounds.Expand(bounds.maxs + b);

			/*sortedArray[i * 2 + 0].id = i;
			sortedArray[i * 2 + 0].value = glm::dot(axis, bounds.mins);
			sortedArray[i * 2 + 0].ismin = true;*/
			sortedArray.emplace_back(i, glm::dot(axis, bounds.mins), true);
			/*sortedArray[i * 2 + 1].id = i;
			sortedArray[i * 2 + 1].value = glm::dot(axis, bounds.maxs);
			sortedArray[i * 2 + 1].ismin = false;*/
			sortedArray.emplace_back(i, glm::dot(axis, bounds.maxs), false);
		}

		//std::sort(sortedArray.begin(), sortedArray.end(), CompareSAP);
	}



	/*
	====================================================
	BuildPairs
	====================================================
	*/
	void BuildPairs(std::vector< collisionPair_t >& collisionPairs, const psuedoBody_t* sortedBodies, const int num) {
		collisionPairs.clear();

		// Now that the bodies are sorted, build the collision pairs
		for (int i = 0; i < num * 2; i++) {
			const psuedoBody_t& a = sortedBodies[i];
			if (!a.ismin) {
				continue;
			}

			collisionPair_t pair;
			pair.a = a.id;

			for (int j = i + 1; j < num * 2; j++) {
				const psuedoBody_t& b = sortedBodies[j];
				// if we've hit the end of the a element, then we're done creating pairs with a
				if (b.id == a.id) {
					break;
				}

				if (!b.ismin) {
					continue;
				}

				pair.b = b.id;
				collisionPairs.push_back(pair);
			}
		}
	}

	void BuildPairs(std::vector< collisionPair_t >& collisionPairs, const std::vector<psuedoBody_t>& sortedBodies) {
		collisionPairs.clear();
		int size = sortedBodies.size();
		// Now that the bodies are sorted, build the collision pairs
		for (int i = 0; i < size; i++) {
			const psuedoBody_t& a = sortedBodies[i];
			if (!a.ismin) {
				continue;
			}

			collisionPair_t pair;
			pair.a = a.id;

			for (int j = i + 1; j < size; j++) {
				const psuedoBody_t& b = sortedBodies[j];
				// if we've hit the end of the a element, then we're done creating pairs with a
				if (b.id == a.id) {
					break;
				}

				if (!b.ismin) {
					continue;
				}

				pair.b = b.id;
				collisionPairs.push_back(pair);
			}
		}
	}



	void SweepAndPrune1D(const std::vector<RigidBody3D*>& bodies, std::vector< collisionPair_t >& finalPairs, const float dt_sec) {
		auto num = bodies.size();
		psuedoBody_t* sortedBodies = (psuedoBody_t*)alloca(sizeof(psuedoBody_t) * num * 2);
		//static std::vector<psuedoBody_t> sortedBodies;
		//sortedBodies.resize(2 * num);
		//sortedBodies.reserve(2 * num);
		SortBodiesBounds(bodies, sortedBodies, dt_sec);
		BuildPairs(finalPairs, sortedBodies, num);
		//BuildPairs(finalPairs, sortedBodies);
		//sortedBodies.clear();
	}


	void BroadPhase(const RigidBody3D* bodies, const int num, std::vector<collisionPair_t>& finalPairs, const float dt_sec)
	{

	}

	void BroadPhase(const std::vector<RigidBody3D*>& bodies, std::vector<collisionPair_t>& finalPairs, const float dt_sec)
	{
		finalPairs.clear();

		SweepAndPrune1D(bodies, finalPairs, dt_sec);
	}

}