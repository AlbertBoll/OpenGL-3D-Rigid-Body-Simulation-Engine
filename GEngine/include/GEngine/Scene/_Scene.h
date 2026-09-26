#pragma once
#include"entt/entt.hpp"
#include "Scene/RenderEcs.h"
#include "Scene/SceneError.h"
#include "Scene/RenderState.h"
#include "Physics/PhysicsShapeError.h"
#include <Core/Timestep.h>
#include <cstdint>
#include <expected>
#include <concepts>
#include <utility>
#include <string_view>
#include <map>
#include "Core/UUID.h"
#include <Camera/EditorCamera.h>

namespace GEngine
{
    struct RenderTransformQueryError
    {
        TransformErrorCode code = TransformErrorCode::InvalidEntity;
        std::string_view operation = "_Scene::GetRenderTransform";
        std::string_view message = "Render transform requires a live entity in this scene";
    };
	struct WorldTransform
	{
		EntityRenderId entity;
		Mat4 matrix{1.0f};
		std::uint64_t revision{};
	};
	struct WorldTransformUpdate
	{
		// Value snapshot in deterministic parent-before-child order. No registry borrows.
		std::vector<WorldTransform> transforms;
		std::size_t recomputed{};
	};
	class _Entity;
    namespace SceneDetail { struct BackendAccess; }
	class PhysicsWorld;
	class PhysicsSystem;
	//using namespace Camera;

	class _Scene
	{
		
		friend class _Entity;
        friend class RenderSystem;
        friend struct SceneDetail::BackendAccess;

	public:

		_Scene();
		~_Scene();
		[[nodiscard]] static std::expected<RefPtr<_Scene>, SceneError> Copy(RefPtr<_Scene> other);

		[[nodiscard]] std::expected<_Entity, SceneError> CreateEntity(const std::string& name = std::string());
		[[nodiscard]] std::expected<_Entity, SceneError> CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());
		// Handles/relationships are non-owning. Explicit destruction includes descendants
		// unless excludeChildren detaches them; legacy first is retained for source compatibility.
		[[nodiscard]] std::expected<void, SceneError> DestroyEntity(_Entity entity, bool excludeChildren = false, bool first = true);
		[[nodiscard]] std::expected<void, SceneError> DestroyEntity(UUID entityID, bool excludeChildren = false, bool first = true);

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

		// Call after authoring/physics and before serial render extraction. Ordinary
		// Transform3DComponent TRS is local; entities with RigidBody3DComponent are
		// explicit world-space anchors, preserving the existing physics bridge.
		// Reparent/detach retains authored TRS. Failure publishes no partial snapshot.
		std::expected<WorldTransformUpdate, TransformError> UpdateWorldTransforms();

		// After resource publication and authoring, before BeginExtraction. Resolves
		// ready versions, samples presentation, and reports effective invalidation.
		// Invalid/missing bounds stay visible with a diagnostic; malformed hierarchy
		// or presentation fails transactionally without publishing render revisions.
		std::expected<SceneRenderState, TransformError> UpdateRenderState(const RenderStateResources&);

		// Legacy submission/interpolation adapter (application adoption 42, renderer 46).
		// Its authored matrices retain the pre-hierarchy world-space interpretation.
		struct RenderTransform
		{
			Mat4 matrix{1.0f};
			std::uint64_t revision{}; // Changes with the sampled matrix, even without a tick.
			std::uint64_t simulationRevision{}; // Current scene fixed-update count.
		};
		double GetRenderInterpolationAlpha() const;
		[[nodiscard]] std::expected<RenderTransform, RenderTransformQueryError> GetRenderTransform(const _Entity& entity);
		std::expected<void, TransformError> ResetRenderInterpolation(const _Entity& entity);
		void SetRenderInterpolationEnabled(bool enabled) { m_RenderData.RequireMutable(); m_RenderInterpolationEnabled = enabled; }
		bool IsRenderInterpolationEnabled() const { return m_RenderInterpolationEnabled; }

		// Legacy mutable access must finish before the serial extraction boundary.
		entt::registry& Reg() { m_RenderData.RequireMutable(); return m_Registry; }
		RenderEcs& RenderData() { return m_RenderData; }
		const RenderEcs& RenderData() const { return m_RenderData; }

        // Successful startup retains the existing caller-owned shape handoff.
        // Failure retires pending bodies/shapes and disconnects their scale callbacks.
        [[nodiscard]] std::expected<void, PhysicsShapeError> OnRuntimeStart();
		void OnRuntimeStop();

		void OnSimulationStart();
		void OnSimulationStop();
		void OnUpdateEditor(Timestep ts, Camera::_EditorCamera& camera);
		void OnUpdateRuntime(Timestep ts);

		void OnViewportResize(uint32_t width, uint32_t height);

		[[nodiscard]] std::expected<_Entity, SceneError> DuplicateEntity(_Entity entity);
	

		_Entity FindEntityByName(std::string_view name);
		_Entity GetEntityByUUID(UUID uuid);

		_Entity GetPrimaryCameraEntity();

		bool IsRunning() const { return m_IsRunning; }
		bool IsPaused() const { return m_IsPaused; }

		void SetPaused(bool paused);

		void Step(int frames = 1);

		[[nodiscard]] std::expected<void, SceneError> PushToRenderList(_Entity entity);

        template<typename... Entities>
            requires (sizeof...(Entities) != 1 && (std::convertible_to<Entities, _Entity> && ...))
        [[nodiscard]] std::expected<void, SceneError> PushToRenderList(Entities&&... entities)
        {
            std::expected<void, SceneError> result;
            auto push = [&](auto&& entity) {
                if (result) result = PushToRenderList(std::forward<decltype(entity)>(entity));
            };
            (push(std::forward<Entities>(entities)), ...);
            return result;
        }

		template<typename... Components>
		auto GetAllEntitiesWith()
		{
			m_RenderData.RequireMutable();
			return m_Registry.view<Components...>();
		}

		template<typename IncludeComponent, typename ... ExcludeComponents>
		auto GetAllEntitiesWithExclude()
		{
			m_RenderData.RequireMutable();
			return m_Registry.view<IncludeComponent>(entt::exclude<ExcludeComponents...>);
		}

		
		template<typename...Components>
		auto& View()
		{
			m_RenderData.RequireMutable();
			return m_Registry.view<Components...>();
		}


		PhysicsSystem* GetPhysicsSystem()
		{
			m_RenderData.RequireMutable();
			return m_PhysicsSystem;
		}

	private:
		auto& GetGroupEntities() { m_RenderData.RequireMutable(); return m_GroupEntities; }
		auto& GetLightEntities() { m_RenderData.RequireMutable(); return m_LightEntities; }
		const std::vector<_Entity>& GetLightEntitiesWithRenderID(unsigned int id) const;

		void RemoveFromRenderLists(const _Entity& entity);
        RenderPresentationInput ObserveRenderPresentation(entt::entity entity) const;
        static Mat4 EvaluateRenderPresentation(const RenderPresentationInput&);
		Mat4 SampleRenderMatrix(entt::entity entity) const;

		template<typename T>
		void OnComponentAdded(_Entity entity, T& component);

        [[nodiscard]] std::expected<void, PhysicsShapeError> OnPhysics3DStart();
		void OnPhysics3DStop();

	private:
		bool m_RenderInterpolationEnabled = true;
		std::uint64_t m_RenderTransformRevision{};
		entt::registry m_Registry;
		RenderEcs m_RenderData{m_Registry};
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
