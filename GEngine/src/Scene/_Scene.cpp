#include "gepch.h"
#include "../Assets/ShaderBackend.h"
#include <Scene/_Scene.h>
#include "Physics/PhysicsWorld.h"
#include <Component/Component.h>
#include <Scene/_Entity.h>
#include "Physics/PhysicsSystem.h"
#include "Physics/ShapeSphere.h"
#include "Physics/ShapeBox.h"
#include "Physics/ShapeConvex.h"
#include "Core/Utility.h"
#include "Assets/Shaders/shader.h"
#include "Geometry/Geometry.h"
#include <Core/Timer.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace GEngine
{
	using namespace Camera;

	namespace
	{
		// Runtime-only bridge state: not copied with authoring components or serialized.
		struct RuntimePhysicsPose
		{
			RigidBodyIdentity identity;
			RigidBody3D* body{};
			Vec3f translation{};
			Quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			Vec3f previousTranslation{}, currentTranslation{}, scale{1.0f};
			Quat previousRotation{1.0f, 0.0f, 0.0f, 0.0f}, currentRotation{1.0f, 0.0f, 0.0f, 0.0f};
			UUID parent{0};
		};

		bool ValidPhysicsPose(PhysicsWorld* world, const Component::RigidBody3DComponent& rigidBody, const RuntimePhysicsPose& pose)
		{
			return world && rigidBody.RuntimeBody && rigidBody.RuntimeBody == pose.body
				&& world->IsBodyIdentityValid(pose.identity);
		}

		void ResetPhysicsHistory(RuntimePhysicsPose& pose, const Component::Transform3DComponent& transform,
			const RigidBody3D& body, UUID parent)
		{
			pose.previousTranslation = pose.currentTranslation = body.m_Position;
			pose.previousRotation = pose.currentRotation = body.m_Orientation;
			pose.scale = transform.Scale;
			pose.parent = parent;
		}

		void PublishPhysicsPose(Component::Transform3DComponent& transform, RuntimePhysicsPose& pose,
			const RigidBody3D& body)
		{
			transform.SetTranslation(body.m_Position);
			transform.SetRotation(body.m_Orientation);
			pose.translation = transform.Translation;
			pose.rotation = transform.QuatRotation;
		}
	}


	template<typename... Component>
	static void CopyComponent(entt::registry& dst, entt::registry& src, const std::unordered_map<UUID, entt::entity>& enttMap)
	{
		([&]()
			{
				auto view = src.view<Component>();
				for (auto srcEntity : view)
				{
					entt::entity dstEntity = enttMap.at(src.get<IDComponent>(srcEntity).ID);

					auto& srcComponent = src.get<Component>(srcEntity);
					dst.emplace_or_replace<Component>(dstEntity, srcComponent);
				}
			}(), ...);
	}


	template<typename... Component>
	static void CopyComponent(ComponentGroup<Component...>, entt::registry& dst, entt::registry& src, const std::unordered_map<UUID, entt::entity>& enttMap)
	{
		CopyComponent<Component...>(dst, src, enttMap);
	}


	template<typename... Component>
	static void CopyComponentIfExists(_Entity dst, _Entity src)
	{
		([&]()
			{
				if (src.HasAllComponents<Component>())
					dst.AddOrReplaceComponent<Component>(src.GetComponent<Component>());
			}(), ...);
	}

	template<typename... Component>
	static void CopyComponentIfExists(ComponentGroup<Component...>, _Entity dst, _Entity src)
	{
		CopyComponentIfExists<Component...>(dst, src);
	}


	using namespace Component;
	_Scene::_Scene()
	{
	/*	auto entity = m_Registry.create();
		m_Registry.emplace<TransformComponent>(entity);

	

		if (m_Registry.any_of<TransformComponent>(entity))
		{
			auto& transform = m_Registry.get<TransformComponent>(entity);
		}*/
		m_PhysicsSystem = new PhysicsSystem();
		
	}

	_Scene::~_Scene()
	{
		m_RenderData.RequireMutable();
		OnPhysics3DStop();
		delete m_PhysicsSystem;
	}

	RefPtr<_Scene> _Scene::Copy(RefPtr<_Scene> other)
	{
		other->m_RenderData.RequireMutable();
		RefPtr<_Scene> newScene = CreateRefPtr<_Scene>();

		newScene->m_ViewportWidth = other->m_ViewportWidth;
		newScene->m_ViewportHeight = other->m_ViewportHeight;

		auto& srcSceneRegistry = other->m_Registry;
		auto& dstSceneRegistry = newScene->m_Registry;
		std::unordered_map<UUID, entt::entity> enttMap;

		// Create entities in new scene
		auto idView = srcSceneRegistry.view<IDComponent>();
		for (auto e : idView)
		{
			UUID uuid = srcSceneRegistry.get<IDComponent>(e).ID;
			const auto& name = srcSceneRegistry.get<TagComponent>(e).Name;
			_Entity newEntity = newScene->CreateEntityWithUUID(uuid, name);
			enttMap[uuid] = (entt::entity)newEntity;
		}

		// Copy components (except IDComponent and TagComponent)
		CopyComponent(AllComponents{}, dstSceneRegistry, srcSceneRegistry, enttMap);
		auto RenderView = dstSceneRegistry.view<RenderComponent>();
		for (auto e : RenderView)
		{
			newScene->PushToRenderList(_Entity(e, newScene.get()));
		}

		return newScene;


	}

	void _Scene::Update(Timestep ts)
	{
		m_RenderData.RequireMutable();
		auto* timingWorld = m_PhysicsSystem->GetPhysicsWorld();
		if (!timingWorld || timingWorld != m_TimingWorld)
		{
			m_PhysicsTiming = {};
			m_TimingWorld = timingWorld;
		}
		m_PhysicsTiming.stepsLastUpdate = 0;
		m_PhysicsTiming.discardedSeconds = 0.0;
		for (auto e : m_Registry.view<RigidBody3DComponent, Transform3DComponent, RuntimePhysicsPose>())
		{
			auto& rigidBody = m_Registry.get<RigidBody3DComponent>(e);
			auto& pose = m_Registry.get<RuntimePhysicsPose>(e);
			auto* world = m_PhysicsSystem->GetPhysicsWorld();
			if (!world || !rigidBody.RuntimeBody || rigidBody.RuntimeBody != pose.body ||
				!world->IsBodyIdentityValid(pose.identity)) continue;
			auto& transform = m_Registry.get<Transform3DComponent>(e);
			auto* body = rigidBody.RuntimeBody;
			// Static transforms are authoritative. Kinematic bodies retain prescribed-velocity
			// motion; dynamic bodies retain simulated motion. Both consume authored pose edits
			// as teleports, then publish their next physics pose without replaying that output.
			const bool authoredEdit = transform.Translation != pose.translation || transform.QuatRotation != pose.rotation;
			const bool staticPoseChanged = body->Type == BodyType::Static &&
				(transform.Translation != body->m_Position || transform.QuatRotation != body->m_Orientation);
			const auto parent = _Entity(e, this).GetParentUUID();
			const bool discontinuity = body->m_Position != pose.currentTranslation
				|| body->m_Orientation != pose.currentRotation || transform.Scale != pose.scale || parent != pose.parent;
			if (authoredEdit || staticPoseChanged)
			{
				m_PhysicsSystem->SetBodyPose(body, transform.Translation, transform.QuatRotation);
				// Rejected edits restore the last accepted body pose, including Euler display data.
				PublishPhysicsPose(transform, pose, *body);
			}
			if (authoredEdit || staticPoseChanged || discontinuity)
				ResetPhysicsHistory(pose, transform, *body, parent);
		}
		const double elapsed = ts.GetSecondsPrecise();
		if (timingWorld && !m_IsPaused && std::isfinite(elapsed) && elapsed > 0.0)
		{
			// Retain a bounded backlog. Report every positive second beyond its cap.
			const double accepted = std::min(elapsed, MaxPendingPhysicsSeconds - m_PhysicsTiming.pendingSeconds);
			m_PhysicsTiming.pendingSeconds += accepted;
			m_PhysicsTiming.discardedSeconds = elapsed - accepted;
			constexpr double maxTotal = std::numeric_limits<double>::max();
			m_PhysicsTiming.totalDiscardedSeconds += std::min(m_PhysicsTiming.discardedSeconds,
				maxTotal - m_PhysicsTiming.totalDiscardedSeconds);
			// Only absorb roundoff at a tick boundary (less than 5e-16 seconds).
			constexpr double roundoff = 8.0 * std::numeric_limits<double>::epsilon() * MaxPendingPhysicsSeconds;
			while (m_PhysicsTiming.stepsLastUpdate < MaxPhysicsStepsPerUpdate &&
				m_PhysicsTiming.pendingSeconds + roundoff >= PhysicsStepSeconds)
			{
				for (auto e : m_Registry.view<RigidBody3DComponent, RuntimePhysicsPose>())
				{
					auto& pose = m_Registry.get<RuntimePhysicsPose>(e);
					if (!ValidPhysicsPose(timingWorld, m_Registry.get<RigidBody3DComponent>(e), pose)) continue;
					pose.previousTranslation = pose.currentTranslation;
					pose.previousRotation = pose.currentRotation;
				}
				m_PhysicsSystem->Update(Timestep(PhysicsStepSeconds));
				for (auto e : m_Registry.view<RigidBody3DComponent, RuntimePhysicsPose>())
				{
					auto& pose = m_Registry.get<RuntimePhysicsPose>(e);
					if (!ValidPhysicsPose(timingWorld, m_Registry.get<RigidBody3DComponent>(e), pose)) continue;
					pose.currentTranslation = pose.body->m_Position;
					pose.currentRotation = pose.body->m_Orientation;
				}
				m_PhysicsTiming.pendingSeconds = std::max(0.0, m_PhysicsTiming.pendingSeconds - PhysicsStepSeconds);
				++m_PhysicsTiming.stepsLastUpdate;
				++m_PhysicsTiming.totalSteps;
			}
		}
		

		for (auto e : m_Registry.view<RigidBody3DComponent, Transform3DComponent, RuntimePhysicsPose>())
		{
			auto& rigidBody = m_Registry.get<RigidBody3DComponent>(e);
			auto& pose = m_Registry.get<RuntimePhysicsPose>(e);
			auto* world = m_PhysicsSystem->GetPhysicsWorld();
			if (!world || !rigidBody.RuntimeBody || rigidBody.RuntimeBody != pose.body ||
				!world->IsBodyIdentityValid(pose.identity)) continue;
			auto& transform = m_Registry.get<Transform3DComponent>(e);
			PublishPhysicsPose(transform, pose, *rigidBody.RuntimeBody);
		}
	}

	double _Scene::GetRenderInterpolationAlpha() const
	{
		// Whole retained ticks indicate overload: show current state, never extrapolate
		// or wrap the remainder back toward an older pose.
		return std::clamp(m_PhysicsTiming.pendingSeconds / PhysicsStepSeconds, 0.0, 1.0);
	}

	_Scene::RenderTransform _Scene::GetRenderTransform(const _Entity& entity)
	{
		if (entity.GetSceneContext() != this || !entity.HasAllComponents<Transform3DComponent>())
			throw std::invalid_argument("Render transform requires a live entity in this scene");
		const auto handle = static_cast<entt::entity>(entity);
		const auto& transform = entity.GetComponent<Transform3DComponent>();
		Vec3f translation = transform.Translation;
		Quat rotation = transform.QuatRotation;
		const auto* pose = m_Registry.try_get<RuntimePhysicsPose>(handle);
		const auto* rigidBody = m_Registry.try_get<RigidBody3DComponent>(handle);
		if (pose && rigidBody && ValidPhysicsPose(m_PhysicsSystem->GetPhysicsWorld(), *rigidBody, *pose))
		{
			// Edits between Update and submission must be visible immediately, but must
			// not write back into physics. Update accepts/rejects authored poses as before.
			const bool authoredEdit = transform.Translation != pose->translation
				|| transform.QuatRotation != pose->rotation;
			if (!authoredEdit)
			{
				const auto* body = rigidBody->RuntimeBody;
				translation = body->m_Position;
				rotation = body->m_Orientation;
				if (m_RenderInterpolationEnabled && !m_IsPaused && transform.Scale == pose->scale
					&& entity.GetParentUUID() == pose->parent && translation == pose->currentTranslation
					&& rotation == pose->currentRotation)
				{
					const float alpha = static_cast<float>(GetRenderInterpolationAlpha());
					translation = glm::mix(pose->previousTranslation, pose->currentTranslation, alpha);
					// Shortest-arc normalized quaternion interpolation; no Euler or matrix lerp.
					rotation = glm::normalize(glm::slerp(pose->previousRotation, pose->currentRotation, alpha));
				}
			}
		}
		// Scale is authored, not integrated. A scale edit snaps history; preserving
		// T*R*S directly supports non-uniform/negative scale without matrix decomposition.
		const Mat4 matrix = glm::translate(Mat4(1.0f), translation) * glm::toMat4(rotation)
			* glm::scale(Mat4(1.0f), transform.Scale);
		auto& sampled = m_Registry.get_or_emplace<RenderTransform>(handle);
		if (sampled.revision == 0 || sampled.matrix != matrix)
		{
			sampled.matrix = matrix;
			sampled.revision = ++m_RenderTransformRevision;
		}
		sampled.simulationRevision = m_PhysicsTiming.totalSteps;
		return sampled;
	}

	void _Scene::ResetRenderInterpolation(const _Entity& entity)
	{
		if (entity.GetSceneContext() != this || !entity)
			throw std::invalid_argument("Interpolation reset requires a live entity in this scene");
		const auto handle = static_cast<entt::entity>(entity);
		auto* pose = m_Registry.try_get<RuntimePhysicsPose>(handle);
		const auto* rigidBody = m_Registry.try_get<RigidBody3DComponent>(handle);
		const auto* transform = m_Registry.try_get<Transform3DComponent>(handle);
		if (pose && rigidBody && transform && ValidPhysicsPose(m_PhysicsSystem->GetPhysicsWorld(), *rigidBody, *pose))
			ResetPhysicsHistory(*pose, *transform, *rigidBody->RuntimeBody, entity.GetParentUUID());
	}

	void _Scene::SetPaused(bool paused)
	{
		if (m_IsPaused == paused) return;
		m_IsPaused = paused;
		// Pause/resume snaps to current state; resuming without a tick cannot rewind.
		for (auto e : m_Registry.view<RuntimePhysicsPose>())
			ResetRenderInterpolation(_Entity(e, this));
	}

	void _Scene::OnRuntimeStart()
	{
		m_IsRunning = true;

		OnPhysics3DStart();
	}

	void _Scene::OnRuntimeStop()
	{
		m_IsRunning = false;
		OnPhysics3DStop();
	}

	void _Scene::OnSimulationStart()
	{

	}

	void _Scene::OnSimulationStop()
	{

	}

	void _Scene::OnUpdateEditor(Timestep ts, _EditorCamera& camera)
	{

	}

	void _Scene::OnUpdateRuntime(Timestep ts)
	{

	}

	void _Scene::OnViewportResize(uint32_t width, uint32_t height)
	{
		if (m_ViewportWidth == width && m_ViewportHeight == height)
			return;

		m_ViewportWidth = width;
		m_ViewportHeight = height;
		// Resize our non-FixedAspectRatio cameras
		auto view = m_Registry.view<CameraComponent>();
		for (auto entity : view)
		{

			auto& cameraComponent = view.get<CameraComponent>(entity);
			if (!cameraComponent.FixedAspectRatio)
				cameraComponent.Camera.SetViewportSize(width, height);

		}


	}

	_Entity _Scene::DuplicateEntity(_Entity entity)
	{
		m_RenderData.RequireMutable();
		if (entity.GetSceneContext() != this || !entity.HasAllComponents<IDComponent>())
			throw std::invalid_argument("Duplicate requires a live entity in this scene");
		// Copy name because we're going to modify component data structure
		std::string name = entity.GetName();
		_Entity newEntity = CreateEntity(name);
		CopyComponentIfExists(AllComponents{}, newEntity, entity);
		// A single-entity duplicate is a sibling; its source's children are not copied.
		if (newEntity.HasAllComponents<RelationshipComponent>())
			newEntity.GetComponent<RelationshipComponent>() = RelationshipComponent{};
		newEntity.SetParent(entity.GetParent());
		PushToRenderList(newEntity);
		return newEntity;
	}

	_Entity _Scene::FindEntityByName(std::string_view name)
	{
		auto view = m_Registry.view<TagComponent>();
		for (auto entity : view)
		{
			const TagComponent& tc = view.get<TagComponent>(entity);
			if (tc.Name == name) return { entity, this };
		}
		return {};
	}

	_Entity _Scene::GetEntityByUUID(UUID uuid)
	{
		const auto it = m_EntityMap.find(uuid);
		if (it != m_EntityMap.end() && m_Registry.valid(it->second)
			&& m_Registry.all_of<IDComponent>(it->second)
			&& m_Registry.get<IDComponent>(it->second).ID == uuid)
			return { it->second, this };

		return {};
	}

	_Entity _Scene::GetPrimaryCameraEntity()
	{
		auto view = m_Registry.view<CameraComponent>();
		for (auto entity : view)
		{
			const auto& camera = view.get<CameraComponent>(entity);
			if (camera.Primary)
				return { entity, this };
		}
		return {};
	}

	_Entity _Scene::CreateEntity(const std::string& tag)
	{
		return CreateEntityWithUUID(UUID(), tag);
	}

	_Entity _Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		m_RenderData.RequireMutable();
		if (uuid == 0 || GetEntityByUUID(uuid))
			throw std::invalid_argument("Entity UUID must be nonzero and unique in its scene");
		_Entity entity = { m_Registry.create(), this };
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<Transform3DComponent>();
		auto& tag = entity.AddComponent<TagComponent>();
		tag.Name = name.empty() ? "Entity" : name;

		m_EntityMap[uuid] = entity;

		return entity;
	}

	void _Scene::DestroyEntity(_Entity entity, bool excludeChildren, bool /*first*/)
	{
		m_RenderData.RequireMutable();
		if ((entt::entity)entity == entt::null)
			return;
		if (entity.GetSceneContext() != this)
			throw std::invalid_argument("Destroy entity does not belong to this scene");
		if (!entity)
			return;
		if (!entity.HasAllComponents<IDComponent>())
			throw std::invalid_argument("Destroy requires a scene entity with an ID");

		const auto id = entity.GetUUID();
		// Copy UUIDs before registry removals move components. Links do not own entities;
		// only this explicit scene operation requests recursive destruction.
		const auto children = std::as_const(entity).Children();
		entity.SetParent({});
		for (const auto childId : children)
		{
			auto child = GetEntityByUUID(childId);
			if (child && child.GetParentUUID() == id)
			{
				if (excludeChildren)
					child.SetParent({});
				else
					DestroyEntity(child, false, false);
			}
		}

		// Remove recorded memberships even if the shader was replaced or removed.
		RemoveFromRenderLists(entity);

		//remove its corresponding rigid body if exists
		if (entity.HasAllComponents<RigidBody3DComponent>())
		{
			auto rigid_body = entity.GetComponent<RigidBody3DComponent>().RuntimeBody;
			
			auto physics_world = m_PhysicsSystem->GetPhysicsWorld();
			if (physics_world && rigid_body)
				physics_world->RemoveRigidBody3D(rigid_body);

		}

		m_Registry.destroy((entt::entity)entity);
		m_EntityMap.erase(id);

	}

	void _Scene::DestroyEntity(UUID entityID, bool excludeChildren, bool first)
	{
		auto it = m_EntityMap.find(entityID);
		if (it == m_EntityMap.end())
			return;
		DestroyEntity({ it->second, this }, excludeChildren, first);
	}

	const std::vector<_Entity>& _Scene::GetLightEntitiesWithRenderID(unsigned int id) const
	{
		const auto it = m_LightEntities.find(id);
		static const std::vector<_Entity> empty;
		return it == m_LightEntities.end() ? empty : it->second;
	}

	void _Scene::RemoveFromRenderLists(const _Entity& entity)
	{
		for (auto* groups : { &m_GroupEntities, &m_LightEntities })
		{
			for (auto it = groups->begin(); it != groups->end();)
			{
				std::erase(it->second, entity);
				if (it->second.empty())
					it = groups->erase(it);
				else
					++it;
			}
		}
	}

	void _Scene::PushToRenderList(_Entity entity)
	{
		if (entity.GetSceneContext() != this || !m_Registry.valid(entity))
			throw std::invalid_argument("Render-list entity does not belong to this scene");
		if (!entity.HasAllComponents<RenderComponent>())
			return;

		const auto* shader = entity.GetComponent<RenderComponent>().Shader;
		if (!shader || Asset::ShaderBackendAccess::Program(*shader) == 0)
			throw std::invalid_argument("Render-list entity requires a nonzero shader program");
		const auto program = static_cast<unsigned int>(Asset::ShaderBackendAccess::Program(*shader));
		auto& groups = entity.HasAnyComponents<DirectionalLightComponent, PointLightComponent, SpotLightComponent>()
			? m_LightEntities : m_GroupEntities;
		const auto found = groups.find(program);
		if (found != groups.end() && std::find(found->second.begin(), found->second.end(), entity) != found->second.end())
			return;

		// Re-publishing after a shader change moves the entity from its old group.
		RemoveFromRenderLists(entity);
		groups[program].push_back(entity);
	}


	void _Scene::Step(int frames)
	{
		m_StepFrames = frames;
	}

	void _Scene::OnPhysics3DStart()
	{
		OnPhysics3DStop();
		auto m_PhysicsWorld = new PhysicsWorld{};
		m_PhysicsSystem->SetPhysicsWorld(m_PhysicsWorld);
		//auto view = m_Registry.view<RigidBody3DComponent>();
		for (auto& e : m_Registry.view<RigidBody3DComponent>())
		{
			_Entity entity{ e, this };
			auto& transform = entity.GetComponent<Transform3DComponent>();
			auto& rigid_body = entity.GetComponent<RigidBody3DComponent>();

			if (entity.HasAllComponents<SphereFixture3DComponent>())
			{
				auto& sphere_fixure = entity.GetComponent<SphereFixture3DComponent>();
				RigidBody3D* body = m_PhysicsWorld->CreateRigidBody3D();

				body->m_LinearVelocity = sphere_fixure.Property.m_LinearVelocity;//rigid_body.Property.m_LinearVelocity;
				body->m_AngularVelocity = sphere_fixure.Property.m_AngularVelocity;
				body->m_InvMass = sphere_fixure.Property.m_InvMass;
				body->m_Elasticity = sphere_fixure.Property.m_Elasticity;
				body->m_Friction = sphere_fixure.Property.m_Friction;
				body->Type = rigid_body.Type;
				body->m_CollisionLayer = rigid_body.CollisionLayer;
				body->m_CollisionMask = rigid_body.CollisionMask;
				body->m_Shape = new ShapeSphere(sphere_fixure.Radius);

				
				
				rigid_body.RuntimeBody = body;
			}

			else if (entity.HasAllComponents<BoxFixture3DComponent>())
			{
				auto& box_fixure = entity.GetComponent<BoxFixture3DComponent>();
				if (!entity.HasAllComponents<MeshComponent>())
				{
					GENGINE_CORE_ERROR("Cannot create box rigid body without a mesh component");
					continue;
				}
				auto* geometry = entity.GetComponent<MeshComponent>().m_Geometry;
				if (!geometry)
				{
					GENGINE_CORE_ERROR("Cannot create box rigid body without mesh geometry");
					continue;
				}
				auto pts = geometry->GetPoints(transform.Scale);
				if (!ShapeBox::IsValidPointSet(pts))
				{
					GENGINE_CORE_ERROR("Cannot create box rigid body from empty, non-finite, or degenerate geometry");
					continue;
				}
				auto* shape = new ShapeBox(pts);
				RigidBody3D* body = m_PhysicsWorld->CreateRigidBody3D();

				body->m_LinearVelocity = box_fixure.Property.m_LinearVelocity;//rigid_body.Property.m_LinearVelocity;
				body->m_AngularVelocity = box_fixure.Property.m_AngularVelocity;
				body->m_InvMass = box_fixure.Property.m_InvMass;
				body->m_Elasticity = box_fixure.Property.m_Elasticity;
				body->m_Friction = box_fixure.Property.m_Friction;
				body->Type = rigid_body.Type;
				body->m_CollisionLayer = rigid_body.CollisionLayer;
				body->m_CollisionMask = rigid_body.CollisionMask;
				//std::cout << entity.GetComponent<TagComponent>().Name << std::endl;
				/*for (auto& pt : pts)
				{
					std::cout << pt.x << " " << pt.y << " " << pt.z << std::endl;
				}*/
				body->m_Shape = shape;


				//transform.SetScale(2);
				/*auto bound = body->m_Shape->GetBounds();
				std::cout << "Box Bounds: " << bound.mins.x << " " << bound.mins.y << " " << bound.mins.z << std::endl;
				std::cout << "Box Bounds: " << bound.maxs.x << " " << bound.maxs.y << " " << bound.maxs.z << std::endl;*/
				rigid_body.RuntimeBody = body;
			}
			else if(entity.HasAllComponents<ConvexFixture3DComponent>())
			{
				auto& convex_fixure = entity.GetComponent<ConvexFixture3DComponent>();
				RigidBody3D* body = m_PhysicsWorld->CreateRigidBody3D();

				body->m_LinearVelocity = convex_fixure.Property.m_LinearVelocity;//rigid_body.Property.m_LinearVelocity;
				body->m_AngularVelocity = convex_fixure.Property.m_AngularVelocity;
				body->m_InvMass = convex_fixure.Property.m_InvMass;
				body->m_Elasticity = convex_fixure.Property.m_Elasticity;
				body->m_Friction = convex_fixure.Property.m_Friction;
				body->m_LinearVelocity = convex_fixure.Property.m_LinearVelocity;
				body->Type = rigid_body.Type;
				body->m_CollisionLayer = rigid_body.CollisionLayer;
				body->m_CollisionMask = rigid_body.CollisionMask;
				auto pts = entity.GetComponent<MeshComponent>().m_Geometry->GetUniquePoints();
				
				//std::cout << entity.GetComponent<TagComponent>().Name << std::endl;
				/*for (auto& pt : pts)
				{
					std::cout << pt.x << " " << pt.y << " " << pt.z << std::endl;
				}*/
				body->m_Shape = new ShapeConvex(pts);

				
				/*auto bound = body->m_Shape->GetBounds();
				std::cout << "Box Bounds: " << bound.mins.x << " " << bound.mins.y << " " << bound.mins.z << std::endl;
				std::cout << "Box Bounds: " << bound.maxs.x << " " << bound.maxs.y << " " << bound.maxs.z << std::endl;*/
				rigid_body.RuntimeBody = body;
			}


			if (auto* body = rigid_body.RuntimeBody)
			{
				if (!m_PhysicsSystem->SetBodyPose(body, transform.Translation, transform.QuatRotation))
				{
					GENGINE_CORE_ERROR("Cannot create rigid body from an invalid entity pose");
					delete body->m_Shape;
					m_PhysicsWorld->RemoveRigidBody3D(body);
					rigid_body.RuntimeBody = nullptr;
					continue;
				}
				auto& pose = m_Registry.emplace_or_replace<RuntimePhysicsPose>(e);
				pose.identity = body->GetIdentity();
				pose.body = body;
				PublishPhysicsPose(transform, pose, *body);
				ResetPhysicsHistory(pose, transform, *body, entity.GetParentUUID());
				Connection(transform, OnScaleChanged, *body->m_Shape, &PhysicalShape::HandleScaleChanged);
			}
		}
	}

	void _Scene::OnPhysics3DStop()
	{
		m_PhysicsTiming = {};
		m_TimingWorld = nullptr;
		m_Registry.clear<RuntimePhysicsPose>();
		for (auto& e : m_Registry.view<RigidBody3DComponent>())
		{
			m_Registry.get<RigidBody3DComponent>(e).RuntimeBody = nullptr;
		}

		m_PhysicsSystem->OnExit();
	}
}
