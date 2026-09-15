#pragma once
#include"entt/entt.hpp"
#include <Core/Timestep.h>
#include <cstdint>
#include <map>
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
		// Handles/relationships are non-owning. Explicit destruction includes descendants
		// unless excludeChildren detaches them; legacy first is retained for source compatibility.
		void DestroyEntity(_Entity entity, bool excludeChildren = false, bool first = true);
		void DestroyEntity(UUID entityID, bool excludeChildren = false, bool first = true);

		// Scene owns the application physics clock; PhysicsSystem remains one tick.
		static constexpr Seconds PhysicsStep{1.0 / 60.0};
		static constexpr double PhysicsStepSeconds = PhysicsStep.count(); // Legacy scheduler boundary.
		static constexpr std::uint32_t MaxPhysicsStepsPerUpdate = 2;
		static constexpr double MaxPendingPhysicsSeconds = 0.25;
		struct PhysicsTiming
		{
			double pendingSeconds{};
			double discardedSeconds{}; // Latest update's backlog overflow.
			double totalDiscardedSeconds{};
			std::uint64_t totalSteps{};
			std::uint32_t stepsLastUpdate{};
		};
		const PhysicsTiming& GetPhysicsTiming() const { return m_PhysicsTiming; }
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

		// A program name is an associative key, never an engine array index.
		const std::vector<_Entity>& GetLightEntitiesWithRenderID(unsigned int id) const;

		PhysicsSystem* GetPhysicsSystem()
		{
			return m_PhysicsSystem;
		}

	private:
		void RemoveFromRenderLists(const _Entity& entity);

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
		PhysicsTiming m_PhysicsTiming{};
		PhysicsWorld* m_TimingWorld{};
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		// Transitional grouping: ascending program order and insertion order within a group.
		using ProgramGroups = std::map<unsigned int, std::vector<_Entity>>;
		ProgramGroups m_GroupEntities;
		ProgramGroups m_LightEntities;
	};


}
