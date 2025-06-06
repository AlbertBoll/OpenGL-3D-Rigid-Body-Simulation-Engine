#include "gepch.h"
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

namespace GEngine
{
	using namespace Camera;

	//static std::vector<std::vector<_Entity>> GroupEntities(10);

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
		m_GroupEntities.resize(5);
		m_LightEntities.resize(5);
		m_PhysicsSystem = new PhysicsSystem();
		
	}

	_Scene::~_Scene()
	{
		//delete m_PhysicsWorld;
		delete m_PhysicsSystem;
	}

	RefPtr<_Scene> _Scene::Copy(RefPtr<_Scene> other)
	{
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
			auto renderID = dstSceneRegistry.get<RenderComponent>(e).Shader->GetHandle();
			newScene->GetGroupEntities()[renderID - 1].emplace_back(e, newScene.get());
		}

		return newScene;


	}

	void _Scene::Update(Timestep ts)
	{
		//for (int i = 0; i < 2; i++)
		//{
		//	m_PhysicsSystem->Update(ts * 0.5f);
		//}
		{
			//Timeit(m_PhysicsSystem_Update\n)
			m_PhysicsSystem->Update(ts);
		}
		

		for (auto& rigidy_body: GetAllEntitiesWith<RigidBody3DComponent, Transform3DComponent>())
		{
			
			_Entity entity = { rigidy_body, this };
			//auto& transform = entity.GetComponent<Transform3DComponent>();
			auto& rigidBody = entity.GetComponent<RigidBody3DComponent>();
			if (rigidBody.Type != BodyType::Static)
			{
				auto& tag = entity.GetComponent<TagComponent>();
				auto& transform = entity.GetComponent<Transform3DComponent>();
				transform.SetTranslation(rigidBody.RuntimeBody->m_Position);
				transform.SetRotation(rigidBody.RuntimeBody->m_Orientation);
				//std::cout <<tag.Name<< " Position: " << transform.Translation.x << " " << transform.Translation.y << " " << transform.Translation.z << std::endl;
				//std::cout << tag.Name << " Orientation: " << transform.QuatRotation.x << " " << transform.QuatRotation.y << " " << transform.QuatRotation.z <<" "<<transform.QuatRotation.w << std::endl;
			}
			

		}
	}

	void _Scene::OnRuntimeStart()
	{
		m_IsRunning = true;

		OnPhysics3DStart();
	}

	void _Scene::OnRuntimeStop()
	{

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
		// Copy name because we're going to modify component data structure
		std::string name = entity.GetName();
		_Entity newEntity = CreateEntity(name);
		CopyComponentIfExists(AllComponents{}, newEntity, entity);
		if (newEntity.HasAllComponents<RenderComponent>())
		{
			auto shader_id = newEntity.GetComponent<RenderComponent>().Shader->GetHandle();
			m_GroupEntities[shader_id - 1].emplace_back(newEntity);
			//m_GroupEntities[shader_id - 1].emplace_back((entt::entity)newEntity, this);
		}
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
		if (m_EntityMap.find(uuid) != m_EntityMap.end())
		{
			return { m_EntityMap[uuid], this };
		}

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
		_Entity entity = { m_Registry.create(), this };
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<Transform3DComponent>();
		auto& tag = entity.AddComponent<TagComponent>();
		tag.Name = name.empty() ? "Entity" : name;

		m_EntityMap[uuid] = entity;

		return entity;
	}

	void _Scene::DestroyEntity(_Entity& entity)
	{	
		//auto& _entity = entity;
		//while (!_entity.IsChildListEmpty())
		//{
		//	for (auto child : _entity.GetChildrenEntitiesList())
		//	{
		//		DestroyEntity(child);
		//	}
		//}


		//m_EntityMap.erase(_entity.GetUUID());
		////m_Registry.destroy(_entity);
		//if (_entity.HasAllComponents<RenderComponent>())
		//{
		//	auto& render_component = _entity.GetComponent<RenderComponent>();
		//	auto id = render_component.Shader->GetHandle();
		//	auto& entities_with_same_shader = m_GroupEntities[id - 1];
		//	for (auto& __entity : entities_with_same_shader)
		//	{
		//		if (__entity == _entity)
		//		{
		//			entities_with_same_shader.erase(std::remove(entities_with_same_shader.begin(),
		//				entities_with_same_shader.end(), __entity), entities_with_same_shader.end());
		//			break;
		//		}
		//	}
		//	
		//}

		//if (!_entity.IsRoot())
		//{
		//	auto& ChildrenEntitiesList = _entity.GetParent()->GetChildrenEntitiesList();
		//	auto it = std::find(ChildrenEntitiesList.begin(), ChildrenEntitiesList.end(), _entity);
		//	ASSERT(it != ChildrenEntitiesList.end());
		//	ChildrenEntitiesList.erase(it);
			///if(it!=)
			//GENGINE_INFO("{}", it != ChildrenEntitiesList.end());
			//ChildrenEntitiesList.erase(std::remove(ChildrenEntitiesList.begin(), ChildrenEntitiesList.end(), _entity), ChildrenEntitiesList.end());
			//m_Registry.destroy(*ele);
		

		//m_Registry.destroy(_entity);

		/*m_EntityMap.erase(entity.GetUUID());
		m_Registry.destroy(entity);
		if (entity.HasAllComponents<RenderComponent>())
		{;
			auto& render_component = entity.GetComponent<RenderComponent>();
			auto id = render_component.Shader->GetHandle();
			auto& entities_with_same_shader = m_GroupEntities[id-1];
			for (auto& _entity : entities_with_same_shader)
			{
				if (_entity == entity)
				{
					entities_with_same_shader.erase(std::remove(entities_with_same_shader.begin(), 
						entities_with_same_shader.end(), _entity), entities_with_same_shader.end());
					return;
				}
			}
		}*/
	}

	void _Scene::DestroyEntity(_Entity entity, bool excludeChildren, bool first)
	{
		if (!entity)
			return;

		if (!excludeChildren)
		{
			// don't make this a foreach loop because entt will move the children
			//            vector in memory as entities/components get deleted
			size_t size = entity.Children().size();
			for (size_t i = 0; i < size; i++)
			{
				auto childId = entity.Children()[i];
				_Entity child = GetEntityByUUID(childId);
				DestroyEntity(child, excludeChildren, false);
			}
		}

		if (first)
		{
			if (auto parent = entity.GetParent(); parent)
				parent.RemoveChild(entity);
		}

		//delete the entity in the group render
		if (entity.HasAllComponents<RenderComponent>())
		{
			auto& render_component = entity.GetComponent<RenderComponent>();
			auto id = render_component.Shader->GetHandle();
			auto& entities_with_same_shader = m_GroupEntities[id - 1];
			for (auto& _entity : entities_with_same_shader)
			{
				if (_entity == entity)
				{
					entities_with_same_shader.erase(std::remove(entities_with_same_shader.begin(),
						entities_with_same_shader.end(), _entity), entities_with_same_shader.end());
					break;
				}
			}
		}

		//remove its corresponding rigid body if exists
		if (entity.HasAllComponents<RigidBody3DComponent>())
		{
			auto rigid_body = entity.GetComponent<RigidBody3DComponent>().RuntimeBody;
			
			auto physics_world = m_PhysicsSystem->GetPhysicsWorld();
			if (physics_world && rigid_body)
				physics_world->RemoveRigidBody3D(rigid_body);

		}

		UUID id = entity.GetUUID();
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

	void _Scene::PushToRenderList(_Entity entity)
	{
		if (auto it = m_EntityMap.find(entity.GetUUID()); it != m_EntityMap.end())
		{
			
			if (entity.HasAllComponents<RenderComponent>() && entity.HasAnyComponents<DirectionalLightComponent, PointLightComponent, SpotLightComponent>())
			{

				auto renderId = entity.GetComponent<RenderComponent>().Shader->GetHandle();

				m_LightEntities[renderId - 1].emplace_back((entt::entity)entity, this);
			/*	if (entity.HasAllComponents<MeshComponent>())
				{
					m_GroupEntities[renderId - 1].emplace_back((entt::entity)entity, this);
				}*/
			}
			else if (entity.HasAllComponents<RenderComponent>() && !entity.HasAnyComponents<DirectionalLightComponent, PointLightComponent, SpotLightComponent>())
			{

				auto renderId = entity.GetComponent<RenderComponent>().Shader->GetHandle();
				m_GroupEntities[renderId - 1].emplace_back((entt::entity)entity, this);
			}


		}
	}



	void _Scene::Step(int frames)
	{
		m_StepFrames = frames;
	}

	void _Scene::OnPhysics3DStart()
	{
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
				body->m_Position = sphere_fixure.Property.m_Position;
				body->m_Orientation = sphere_fixure.Property.m_Orientation;
				body->Type = rigid_body.Type;
				body->m_Shape = new ShapeSphere(sphere_fixure.Radius);
				Connection(transform, OnScaleChanged, *static_cast<ShapeSphere*>(body->m_Shape), &ShapeSphere::HandleScaleChanged);
				
				
				rigid_body.RuntimeBody = body;
			}

			else if (entity.HasAllComponents<BoxFixture3DComponent>())
			{
				auto& box_fixure = entity.GetComponent<BoxFixture3DComponent>();
				RigidBody3D* body = m_PhysicsWorld->CreateRigidBody3D();

				body->m_LinearVelocity = box_fixure.Property.m_LinearVelocity;//rigid_body.Property.m_LinearVelocity;
				body->m_AngularVelocity = box_fixure.Property.m_AngularVelocity;
				body->m_InvMass = box_fixure.Property.m_InvMass;
				body->m_Elasticity = box_fixure.Property.m_Elasticity;
				body->m_Friction = box_fixure.Property.m_Friction;
				body->m_Position = box_fixure.Property.m_Position;
				body->m_Orientation = box_fixure.Property.m_Orientation;
				body->Type = rigid_body.Type;
				auto pts = entity.GetComponent<MeshComponent>().m_Geometry->GetPoints(transform.Scale);
				
				//std::cout << entity.GetComponent<TagComponent>().Name << std::endl;
				/*for (auto& pt : pts)
				{
					std::cout << pt.x << " " << pt.y << " " << pt.z << std::endl;
				}*/
				body->m_Shape = new ShapeBox(pts);
				Connection(transform, OnScaleChanged, *body->m_Shape, &PhysicalShape::HandleScaleChanged);
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
				body->m_Position = convex_fixure.Property.m_Position;
				body->m_Orientation = convex_fixure.Property.m_Orientation;
				body->m_LinearVelocity = convex_fixure.Property.m_LinearVelocity;
				body->Type = rigid_body.Type;
				auto pts = entity.GetComponent<MeshComponent>().m_Geometry->GetUniquePoints();
				
				//std::cout << entity.GetComponent<TagComponent>().Name << std::endl;
				/*for (auto& pt : pts)
				{
					std::cout << pt.x << " " << pt.y << " " << pt.z << std::endl;
				}*/
				body->m_Shape = new ShapeConvex(pts);
				Connection(transform, OnScaleChanged, *body->m_Shape, &PhysicalShape::HandleScaleChanged);
				
				/*auto bound = body->m_Shape->GetBounds();
				std::cout << "Box Bounds: " << bound.mins.x << " " << bound.mins.y << " " << bound.mins.z << std::endl;
				std::cout << "Box Bounds: " << bound.maxs.x << " " << bound.maxs.y << " " << bound.maxs.z << std::endl;*/
				rigid_body.RuntimeBody = body;
			}


		
		}



	}

	void _Scene::OnPhysics3DStop()
	{
		m_PhysicsSystem->OnExit();
	}
}