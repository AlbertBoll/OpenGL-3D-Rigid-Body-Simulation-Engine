#pragma once
#include"entt/entt.hpp"
#include <Core/Timestep.h>
#include "Core/UUID.h"
#include <Camera/EditorCamera.h>

namespace GEngine
{
	class _Entity;
	class PhysicsWorld;
	class PhysicsSystem;
	//using namespace Camera;

	class _Scene
	{
		
		friend class _Entity;

	public:

		_Scene();
		~_Scene();
		static RefPtr<_Scene> Copy(RefPtr<_Scene> other);

		_Entity CreateEntity(const std::string& name = std::string());
		_Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());
		void DestroyEntity(_Entity& entity);
		void DestroyEntity(_Entity entity, bool excludeChildren = false, bool first = true);
		void DestroyEntity(UUID entityID, bool excludeChildren = false, bool first = true);

		void Update(Timestep ts);

		entt::registry& Reg() { return m_Registry; }

		void OnRuntimeStart();
		void OnRuntimeStop();

		void OnSimulationStart();
		void OnSimulationStop();
		void OnUpdateEditor(Timestep ts, Camera::_EditorCamera& camera);
		void OnUpdateRuntime(Timestep ts);

		void OnViewportResize(uint32_t width, uint32_t height);

		_Entity DuplicateEntity(_Entity entity);
	

		_Entity FindEntityByName(std::string_view name);
		_Entity GetEntityByUUID(UUID uuid);

		_Entity GetPrimaryCameraEntity();

		bool IsRunning() const { return m_IsRunning; }
		bool IsPaused() const { return m_IsPaused; }

		void SetPaused(bool paused) { m_IsPaused = paused; }

		void Step(int frames = 1);

		void PushToRenderList(_Entity entity);

		template<typename...Entities>
		void PushToRenderList(Entities&& ... entities)
		{
			(PushToRenderList(entities), ...);
		}

		template<typename... Components>
		auto GetAllEntitiesWith()
		{
			return m_Registry.view<Components...>();
		}

		template<typename IncludeComponent, typename ... ExcludeComponents>
		auto GetAllEntitiesWithExclude()
		{
			return m_Registry.view<IncludeComponent>(entt::exclude<ExcludeComponents...>);
		}

		auto& GetGroupEntities() { return m_GroupEntities; }
		auto& GetLightEntities() { return m_LightEntities; }
		
		template<typename...Components>
		auto& View()
		{
			return m_Registry.view<Components...>();
		}

		auto& GetLightEntitiesWithRenderID(int id)
		{
			return m_LightEntities[id - 1];
		}

		PhysicsSystem* GetPhysicsSystem()
		{
			return m_PhysicsSystem;
		}

	private:
		template<typename T>
		void OnComponentAdded(_Entity entity, T& component);

		void OnPhysics3DStart();//)PhysicsSystem* physics_system);
		void OnPhysics3DStop();

	private:
		entt::registry m_Registry;
		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
		bool m_IsRunning = false;
		bool m_IsPaused = false;
		int m_StepFrames = 0;
		//PhysicsWorld* m_PhysicsWorld{};
		PhysicsSystem* m_PhysicsSystem{};
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		std::vector<std::vector<_Entity>> m_GroupEntities;
		std::vector<std::vector<_Entity>> m_LightEntities;
	};


}