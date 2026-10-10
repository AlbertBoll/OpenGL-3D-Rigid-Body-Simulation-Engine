#pragma once
#include"entt/entt.hpp"
#include "Scene/RenderEcs.h"
#include "Scene/SceneError.h"
#include "Scene/RenderState.h"
#include "Physics/PhysicsShapeError.h"
#include "Events/Event.h"
#include "Math/Math.h"
#include <Core/Timestep.h>
#include <cstdint>
#include <expected>
#include <concepts>
#include <utility>
#include <string_view>
#include <map>
#include <memory>
#include <type_traits>
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

    enum class TransformMutationErrorCode
    {
        InvalidEntity,
        ForeignEntity,
        MissingTransform,
        ExtractionActive,
        MutationActive,
        NonFiniteTransform,
        InvalidRotation,
        InvalidParent,
        Cycle,
        IdentityExhausted,
        PhysicsPoseRejected
    };
    struct TransformMutationError
    {
        TransformMutationErrorCode code;
        UUID entity{0};
        UUID parent{0};
    };
    enum class AuthoritativeTransformChangeKind
    {
        Pose,
        Scale,
        Parent
    };
    // Value snapshot of committed authoring state, not a registry borrow or Physics result.
    struct AuthoritativeTransformChange
    {
        EntityRenderId entity;
        UUID parent{0};
        AuthoritativeTransformChangeKind kind;
        Vec3f translation{}, eulerRotation{}, scale{1.0f};
        Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };
    struct AuthoritativeTransformMutation
    {
        // Success means authoritative Transform mutation only. The legacy void scale
        // bridge cannot report collider acceptance or Physics synchronization. A collider
        // may retain its prior shape; no Transform-plus-collider atomicity is promised.
        bool changed = false;
        // A delivery failure follows a committed change; it does not roll that change back.
        SubscriptionResult notification{};
    };

    enum class SceneAssignmentError
    {
        InvalidEntity, ForeignEntity, MissingIdentity, ExtractionActive,
        InvalidMesh, InvalidMaterial, InvalidSubmesh, IdentityExhausted, RevisionExhausted,
        AuthoringAllocation, AuthoringOwnershipExhausted
    };
    struct SceneAssignmentChange
    {
        bool changed = false;
        // Counts successful changed assignments in this scene only. This is not
        // a transaction notification or a revision of raw/legacy ECS mutations.
        std::uint64_t revision = 0;
    };

	class _Entity;
    class SceneRenderResources;
    namespace SceneDetail { struct BackendAccess; }
	class PhysicsWorld;
	class PhysicsSystem;
	//using namespace Camera;

	class _Scene
	{
		
		friend class _Entity;
        friend class SceneRenderResources;
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

        // Owner-thread authoring before extraction. Rejections/no-ops do not notify.
        // Finite signed/nonuniform/zero scales retain the independent legacy collider
        // policy. Direct fields/Component setters remain compatibility paths, not these
        // Scene-level successful-change operations. Pose changes are authoring teleports;
        // ordinary Physics publication never passes through this API.
        [[nodiscard]] std::expected<AuthoritativeTransformMutation, TransformMutationError>
        SetLocalPose(const _Entity&, const Vec3f& translation, const Quat& rotation);
        [[nodiscard]] std::expected<AuthoritativeTransformMutation, TransformMutationError>
        SetLocalTranslation(const _Entity&, const Vec3f& translation);
        [[nodiscard]] std::expected<AuthoritativeTransformMutation, TransformMutationError>
        SetLocalRotation(const _Entity&, const Vec3f& eulerRotation);
        [[nodiscard]] std::expected<AuthoritativeTransformMutation, TransformMutationError>
        SetLocalScale(const _Entity&, const Vec3f& scale);
        [[nodiscard]] std::expected<AuthoritativeTransformMutation, TransformMutationError>
        SetLocalScale(const _Entity& entity, float scale)
        {
            return SetLocalScale(entity, Vec3f(scale));
        }
        // Keeps local TRS. Rigid bodies remain world-space anchors even with a parent link.
        [[nodiscard]] std::expected<AuthoritativeTransformMutation, TransformMutationError>
        SetParent(const _Entity& entity, const _Entity& parent);

        // Scoped listeners borrow neither an entity nor this Scene through the payload.
        // Phase 13 owner-thread/lifetime/registration/nested-delivery rules apply.
        template <class F>
            requires std::is_nothrow_move_constructible_v<std::decay_t<F>> &&
                     std::invocable<F&, const AuthoritativeTransformChange&>
        [[nodiscard]] std::expected<Subscription, SubscriptionError>
        SubscribeToAuthoritativeTransformChanges(F listener)
        {
            return m_AuthoritativeTransformChanges.Subscribe(std::move(listener));
        }

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
		entt::registry& Reg()
        {
            m_RenderData.RequireMutable();
            InvalidateGeometryOwnership();
            return m_Registry;
        }
        const entt::registry& ReadRegistry() const
        {
            (void)m_RenderData.IsExtracting(); // Checks the owner thread; const reads may extract.
            return m_Registry;
        }
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

        // Single-entity sibling copy: fresh UUID/render identity, shared resources,
        // no children or live Physics bindings. The source remains unchanged.
        [[nodiscard]] std::expected<_Entity, SceneError> DuplicateEntity(_Entity entity);

        // Owner-thread authoring before extraction. Validates entity, registry
        // domains/liveness and submesh before changing intent. Leases are temporary;
        // the component retains handles only. Failure/no-op preserves the revision.
        [[nodiscard]] std::expected<SceneAssignmentChange, SceneAssignmentError>
        AssignRenderable(_Entity entity, const Component::MeshRendererComponent& value,
                         const RenderStateResources& resources);
        std::uint64_t GetRenderAssignmentRevision() const { return m_RenderAssignmentRevision; }
        std::size_t GeometryOwnershipReceiptBytes() const
        {
            (void)m_RenderData.IsExtracting();
            return m_GeometryReceiptCapacity * sizeof(GeometryOwnerReceipt);
        }
	

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
			if constexpr (((std::same_as<std::remove_const_t<Components>, Component::MeshRendererComponent>
                && !std::is_const_v<Components>) || ...)) InvalidateGeometryOwnership();
			return m_Registry.view<Components...>();
		}

		template<typename IncludeComponent, typename ... ExcludeComponents>
		auto GetAllEntitiesWithExclude()
		{
			m_RenderData.RequireMutable();
            if constexpr (std::same_as<std::remove_const_t<IncludeComponent>, Component::MeshRendererComponent>
                && !std::is_const_v<IncludeComponent>) InvalidateGeometryOwnership();
			return m_Registry.view<IncludeComponent>(entt::exclude<ExcludeComponents...>);
		}

		
		template<typename...Components>
		auto View()
		{
			m_RenderData.RequireMutable();
			if constexpr (((std::same_as<std::remove_const_t<Components>, Component::MeshRendererComponent>
                && !std::is_const_v<Components>) || ...)) InvalidateGeometryOwnership();
			return m_Registry.view<Components...>();
		}


		PhysicsSystem* GetPhysicsSystem()
		{
			m_RenderData.RequireMutable();
			return m_PhysicsSystem;
		}

	private:
        [[nodiscard]] std::expected<EntityRenderId, TransformMutationError>
        ValidateTransformMutation(const _Entity&);
        [[nodiscard]] std::expected<AuthoritativeTransformMutation, TransformMutationError>
        ApplyLocalPose(const _Entity&, Vec3f, Quat, Vec3f eulerRotation);
        AuthoritativeTransformMutation
        PublishAuthoritativeTransformChange(const _Entity&, EntityRenderId,
                                            AuthoritativeTransformChangeKind);
        // Guards the pre-store compatibility callback, not post-commit notification dispatch.
        bool m_ApplyingTransform = false;
        TypedSubscriptions<void(const AuthoritativeTransformChange&)>
            m_AuthoritativeTransformChanges;

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
		struct GeometryOwnerReceipt
        {
            entt::entity entity = entt::null;
            Asset::MeshHandle mesh;
        };
        bool PrepareGeometryReceipt(entt::entity);
        void ObserveGeometryOwner(entt::registry&, entt::entity);
        void RemoveGeometryOwner(entt::registry&, entt::entity);
        void InvalidateGeometryOwnership();
        bool HasLiveGeometryScaleBinding(entt::entity) const;
        std::unique_ptr<GeometryOwnerReceipt[]> m_GeometryReceipts;
        std::size_t m_GeometryReceiptCapacity = 0;
        bool m_GeometryCoverageUnavailable = false;

		bool m_RenderInterpolationEnabled = true;
		std::uint64_t m_RenderTransformRevision{};
        std::uint64_t m_RenderAssignmentRevision{};
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
