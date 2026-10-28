#include "gepch.h"
#include "../Assets/ShaderBackend.h"
#include <Scene/_Scene.h>
#include "Physics/PhysicsWorld.h"
#include <Component/Component.h>
#include <Scene/_Entity.h>
#include "Renderer/SceneRenderResources.h"
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
#include <memory>
#include <optional>
#include <new>
#include <unordered_set>
#include "../Renderer/RenderCpuBacking.h"

namespace GEngine
{
	using namespace Camera;

	namespace
	{
        struct TransientEditorView {};

        SceneCameraError CameraIdentityError(EntityRenderId id, RenderEcsError error)
        {
            const auto code = error == RenderEcsError::ForeignId ? SceneCameraErrorCode::ForeignId
                : error == RenderEcsError::ExtractionActive ? SceneCameraErrorCode::ExtractionActive
                : error == RenderEcsError::IdentityExhausted ? SceneCameraErrorCode::IdentityExhausted
                : SceneCameraErrorCode::StaleId;
            return {code, id, error};
        }

		struct CachedWorldTransform
		{
			Vec3f translation{}, scale{1.0f};
			Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
			Mat4 local{1.0f}, world{1.0f};
			EntityRenderId parent{};
			std::uint64_t parentRevision{}, revision{};
			bool worldAnchor{};
		};


		bool FiniteMatrix(const Mat4& matrix)
		{
			for (int column = 0; column != 4; ++column)
				for (int row = 0; row != 4; ++row)
					if (!std::isfinite(matrix[column][row])) return false;
			return true;
		}

        struct C3WorldNode
        {
            entt::entity handle;
            UUID uuid;
            EntityRenderId identity{}, parentIdentity{};
            std::size_t parent = SIZE_MAX;
        };
        struct C3TopologyInput
        {
            entt::entity handle;
            UUID uuid, parent{0};
            EntityRenderId identity{}, parentIdentity{};
            bool relationship{}, transform{}, anchor{};
            bool operator==(const C3TopologyInput&) const = default;
        };
        struct C3WorldGraph
        {
            std::vector<C3WorldNode> nodes;
            std::vector<std::size_t> order;
            std::vector<C3TopologyInput> observed;
        };

		// Runtime-only bridge state: not copied with authoring components or serialized.
		struct RuntimePhysicsPose
		{
			PhysicsBodyHandle handle;
            SceneBodyPose authored;
            Subscription scaleConnection;
			Vec3f translation{};
			Quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			Vec3f previousTranslation{}, currentTranslation{}, scale{1.0f};
			Quat previousRotation{1.0f, 0.0f, 0.0f, 0.0f}, currentRotation{1.0f, 0.0f, 0.0f, 0.0f};
			UUID parent{0};
		};

        // Copies authoring state without borrowing another entity's live physics bindings.
        void ClearCopiedRuntimeBindings(entt::registry& destination, entt::entity entity,
            const RuntimePhysicsPose* sourceRuntime)
        {
            if (auto* body = destination.try_get<Component::RigidBody3DComponent>(entity)) body->RuntimeBody = {};
            // Signal copies omit scoped runtime subscriptions; the source token
            // remains owned exclusively by the source RuntimePhysicsPose.
            (void)sourceRuntime;
        }

		bool ValidPhysicsPose(PhysicsWorld* world, const Component::RigidBody3DComponent& rigidBody, const RuntimePhysicsPose& pose)
		{
			return world && rigidBody.RuntimeBody == pose.handle && world->IsBodyValid(pose.handle);
		}

		void ResetPhysicsHistory(RuntimePhysicsPose& pose, const Component::Transform3DComponent& transform,
			const PhysicsBodyPose& body, UUID parent)
		{
			pose.previousTranslation = pose.currentTranslation = body.position;
			pose.previousRotation = pose.currentRotation = body.orientation;
			pose.scale = transform.Scale;
			pose.parent = parent;
		}

		void PublishPhysicsPose(Component::Transform3DComponent& transform, RuntimePhysicsPose& pose,
			const PhysicsBodyPose& body)
		{
			transform.SetTranslation(body.position);
			transform.SetRotation(body.orientation);
			pose.translation = transform.Translation;
			pose.rotation = transform.QuatRotation;
		}
	}


	template<typename... Component>
	static void CopyComponent(entt::registry& dst, entt::registry& src, const std::unordered_map<UUID, entt::entity>& enttMap)
	{
		([&]()
			{
				auto view = src.view<Component>(entt::exclude<TransientEditorView>);
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
        m_Registry.on_destroy<RigidBody3DComponent>().connect<&_Scene::RetirePhysicsBinding>(*this);
        m_Registry.on_destroy<Transform3DComponent>().connect<&_Scene::RetirePhysicsBinding>(*this);
        m_Registry.on_construct<MeshRendererComponent>().connect<&_Scene::ObserveGeometryOwner>(*this);
        m_Registry.on_update<MeshRendererComponent>().connect<&_Scene::ObserveGeometryOwner>(*this);
        m_Registry.on_destroy<MeshRendererComponent>().connect<&_Scene::RemoveGeometryOwner>(*this);
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
        m_Registry.on_destroy<RigidBody3DComponent>().disconnect(*this);
        m_Registry.on_destroy<Transform3DComponent>().disconnect(*this);
		delete m_PhysicsSystem;
        // Observe removals while receipts/resources are still alive, then disconnect.
        m_Registry.clear<MeshRendererComponent>();
        m_Registry.on_construct<MeshRendererComponent>().disconnect(*this);
        m_Registry.on_update<MeshRendererComponent>().disconnect(*this);
        m_Registry.on_destroy<MeshRendererComponent>().disconnect(*this);
        if (m_GeometryCoverageUnavailable) SceneRenderResources::IncompleteGeometryScene(false);
	}

    bool _Scene::PrepareGeometryReceipt(entt::entity entity)
    {
        const auto index = std::size_t(entt::to_entity(entity));
        if (index < m_GeometryReceiptCapacity) return true;
        std::size_t capacity = m_GeometryReceiptCapacity ? m_GeometryReceiptCapacity : 16;
        while (capacity <= index)
        {
            if (capacity > SIZE_MAX / 2) return false;
            capacity *= 2;
        }
        if (capacity > SIZE_MAX / sizeof(GeometryOwnerReceipt)) return false;
        std::unique_ptr<GeometryOwnerReceipt[]> next(new (std::nothrow) GeometryOwnerReceipt[capacity]);
        if (!next) return false;
        for (std::size_t i = 0; i < m_GeometryReceiptCapacity; ++i) next[i] = m_GeometryReceipts[i];
        m_GeometryReceipts = std::move(next);
        m_GeometryReceiptCapacity = capacity;
        return true;
    }

    void _Scene::InvalidateGeometryOwnership()
    {
        m_RenderData.RequireMutable();
        if (!m_GeometryCoverageUnavailable)
        {
            m_GeometryCoverageUnavailable = true;
            SceneRenderResources::IncompleteGeometryScene(true);
        }
    }

    void _Scene::ObserveGeometryOwner(entt::registry& registry, entt::entity entity)
    {
        if (!PrepareGeometryReceipt(entity))
        {
            InvalidateGeometryOwnership();
            return;
        }
        auto& receipt = m_GeometryReceipts[entt::to_entity(entity)];
        const auto mesh = registry.get<MeshRendererComponent>(entity).mesh;
        if (receipt.entity == entity && receipt.mesh == mesh) return;
        if (!SceneRenderResources::CanAddGeometryOwner(mesh))
        {
            InvalidateGeometryOwnership();
            return;
        }
        if (receipt.entity != entt::null) SceneRenderResources::RemoveGeometryOwner(receipt.mesh);
        if (!SceneRenderResources::AddGeometryOwner(mesh))
        {
            receipt = {};
            InvalidateGeometryOwnership();
            return;
        }
        receipt = {entity, mesh};
    }

    void _Scene::RemoveGeometryOwner(entt::registry&, entt::entity entity)
    {
        const auto index = std::size_t(entt::to_entity(entity));
        if (index >= m_GeometryReceiptCapacity) return;
        auto& receipt = m_GeometryReceipts[index];
        if (receipt.entity != entity) return;
        SceneRenderResources::RemoveGeometryOwner(receipt.mesh);
        receipt = {};
    }

    bool _Scene::HasLiveGeometryScaleBinding(entt::entity entity) const
    {
        const auto* pose = m_Registry.try_get<RuntimePhysicsPose>(entity);
        return pose && bool(pose->scaleConnection);
    }

    void _Scene::RetirePhysicsBinding(entt::registry&, entt::entity entity)
    {
        const auto* pose = m_Registry.try_get<RuntimePhysicsPose>(entity);
        if (!pose) return;
        const auto handle = pose->handle;
        auto* world = m_PhysicsSystem->GetPhysicsWorld();
        if (world && world->IsBodyValid(handle))
            (void)world->DestroyBody(handle); // Retirement disconnects the subscription first.
        else
            m_Registry.remove<RuntimePhysicsPose>(entity);
    }

	std::expected<RefPtr<_Scene>, SceneError> _Scene::Copy(RefPtr<_Scene> other)
	{
        if (!other) return std::unexpected(SceneError{SceneErrorCode::InvalidScene, "_Scene::Copy",
            "Scene copy requires a source scene"});
		other->m_RenderData.RequireMutable();
        for (auto entity : other->m_Registry.view<MeshRendererComponent>(entt::exclude<TransientEditorView>))
            if (!other->m_Registry.all_of<IDComponent, TagComponent>(entity))
                return std::unexpected(SceneError{SceneErrorCode::MissingIdentity, "_Scene::Copy",
                    "Renderer copy requires authored Entity identity and name"});
		RefPtr<_Scene> newScene = CreateRefPtr<_Scene>();

		newScene->m_ViewportWidth = other->m_ViewportWidth;
		newScene->m_ViewportHeight = other->m_ViewportHeight;

		auto& srcSceneRegistry = other->m_Registry;
		auto& dstSceneRegistry = newScene->m_Registry;
		std::unordered_map<UUID, entt::entity> enttMap;

		// Create entities in new scene
		auto idView = srcSceneRegistry.view<IDComponent>(entt::exclude<TransientEditorView>);
		for (auto e : idView)
		{
            if (!srcSceneRegistry.all_of<TagComponent>(e))
                return std::unexpected(SceneError{SceneErrorCode::MissingIdentity, "_Scene::Copy",
                    "Entity copy requires an authored name"});
			UUID uuid = srcSceneRegistry.get<IDComponent>(e).ID;
			const auto& name = srcSceneRegistry.get<TagComponent>(e).Name;
            auto created = newScene->CreateEntityWithUUID(uuid, name);
            if (!created) return std::unexpected(created.error());
            enttMap[uuid] = static_cast<entt::entity>(*created);
		}

		// Copy components (except IDComponent and TagComponent)
		CopyComponent(AllComponents{}, dstSceneRegistry, srcSceneRegistry, enttMap);
        if (newScene->m_GeometryCoverageUnavailable)
            return std::unexpected(SceneError{SceneErrorCode::InvalidScene, "_Scene::Copy",
                "Scene copy could not establish geometry-owner accounting"});
        for (auto source : idView)
            ClearCopiedRuntimeBindings(dstSceneRegistry, enttMap.find(srcSceneRegistry.get<IDComponent>(source).ID)->second,
                srcSceneRegistry.try_get<RuntimePhysicsPose>(source));
		// UUID authoring links copy, but runtime lifetime stamps belong to one scene.
		for (auto e : dstSceneRegistry.view<RelationshipComponent>())
		{
			auto& link = dstSceneRegistry.get<RelationshipComponent>(e);
			const auto sourceParent = other->m_RenderData.Resolve(link.ParentIdentity);
			const bool validSource = sourceParent && srcSceneRegistry.all_of<IDComponent>(*sourceParent)
				&& srcSceneRegistry.get<IDComponent>(*sourceParent).ID == link.ParentHandle;
			link.ParentIdentity = {};
			const auto parent = enttMap.find(link.ParentHandle);
			if (validSource && parent != enttMap.end())
			{
				auto identity = newScene->m_RenderData.Identify(parent->second);
				// Identity exhaustion leaves the authoring copy intact. Evaluation first
				// identifies every member and reports IdentityExhausted before any world
				// snapshot can be published; it never treats an unbound link as a root.
				if (identity) link.ParentIdentity = *identity;
			}
		}
		auto RenderView = dstSceneRegistry.view<RenderComponent>();
		for (auto e : RenderView)
		{
            if (auto published = newScene->PushToRenderList(_Entity(e, newScene.get())); !published)
                return std::unexpected(published.error());
		}

        if (other->m_ActiveSceneCamera)
        {
            auto source = other->ResolveSceneCamera(*other->m_ActiveSceneCamera);
            if (source)
            {
                const auto copied = enttMap.find(srcSceneRegistry.get<IDComponent>(*source).ID);
                if (copied != enttMap.end())
                {
                    auto id = newScene->m_RenderData.Identify(copied->second);
                    if (!id)
                        return std::unexpected(SceneError{SceneErrorCode::InvalidIdentity,
                            "_Scene::Copy", "Active camera identity allocation failed"});
                    newScene->m_ActiveSceneCamera = *id;
                }
            }
        }

		return newScene;


	}

    bool _Scene::SetPhysicsStep(Seconds step)
    {
        m_RenderData.RequireMutable();
        const auto seconds=step.count();
        if (m_PhysicsStepStarted || m_PhysicsTiming.pendingSeconds != 0.0 ||
            (seconds != PhysicsStepSeconds && seconds != 1.0/120.0)) return false;
        m_PhysicsStepSeconds=seconds;
        return true;
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
			if (!ValidPhysicsPose(world, rigidBody, pose)) continue;
			auto& transform = m_Registry.get<Transform3DComponent>(e);
			auto body = m_PhysicsSystem->ReadBodyPose(pose.handle);
            if (!body) continue;
			// Static transforms are authoritative. Kinematic bodies retain prescribed-velocity
			// motion; dynamic bodies retain simulated motion. Both consume authored pose edits
			// as teleports, then publish their next physics pose without replaying that output.
			const bool authoredEdit = transform.Translation != pose.translation || transform.QuatRotation != pose.rotation;
			const bool staticPoseChanged = body->type == BodyType::Static &&
				(transform.Translation != body->position || transform.QuatRotation != body->orientation);
			const auto parent = _Entity(e, this).GetParentUUID();
			const bool discontinuity = body->position != pose.currentTranslation
				|| body->orientation != pose.currentRotation || transform.Scale != pose.scale || parent != pose.parent;
			if (authoredEdit || staticPoseChanged)
			{
				const auto changed = m_PhysicsSystem->SetBodyPose(pose.handle, transform.Translation, transform.QuatRotation);
                body = m_PhysicsSystem->ReadBodyPose(pose.handle);
                if (changed && body) pose.authored = {body->position, body->orientation};
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
			while (!m_IsPaused && m_PhysicsTiming.stepsLastUpdate < MaxPhysicsStepsPerUpdate &&
				m_PhysicsTiming.pendingSeconds + roundoff >= m_PhysicsStepSeconds)
			{
				for (auto e : m_Registry.view<RigidBody3DComponent, RuntimePhysicsPose>())
				{
					auto& pose = m_Registry.get<RuntimePhysicsPose>(e);
					if (!ValidPhysicsPose(timingWorld, m_Registry.get<RigidBody3DComponent>(e), pose)) continue;
					pose.previousTranslation = pose.currentTranslation;
					pose.previousRotation = pose.currentRotation;
				}
                m_PhysicsStepStarted=true;
				m_PhysicsSystem->Update(Timestep(m_PhysicsStepSeconds));
				for (auto e : m_Registry.view<RigidBody3DComponent, RuntimePhysicsPose>())
				{
					auto& pose = m_Registry.get<RuntimePhysicsPose>(e);
					if (!ValidPhysicsPose(timingWorld, m_Registry.get<RigidBody3DComponent>(e), pose)) continue;
                    if (auto body = m_PhysicsSystem->ReadBodyPose(pose.handle))
                    {
                        pose.currentTranslation = body->position;
                        pose.currentRotation = body->orientation;
                    }
				}
				m_PhysicsTiming.pendingSeconds = std::max(0.0, m_PhysicsTiming.pendingSeconds - m_PhysicsStepSeconds);
				++m_PhysicsTiming.stepsLastUpdate;
				++m_PhysicsTiming.totalSteps;
			}
		}
		

		for (auto e : m_Registry.view<RigidBody3DComponent, Transform3DComponent, RuntimePhysicsPose>())
		{
			auto& rigidBody = m_Registry.get<RigidBody3DComponent>(e);
			auto& pose = m_Registry.get<RuntimePhysicsPose>(e);
			auto* world = m_PhysicsSystem->GetPhysicsWorld();
			if (!ValidPhysicsPose(world, rigidBody, pose)) continue;
			auto& transform = m_Registry.get<Transform3DComponent>(e);
			if (auto body = m_PhysicsSystem->ReadBodyPose(pose.handle))
                PublishPhysicsPose(transform, pose, *body);
		}
	}

	double _Scene::GetRenderInterpolationAlpha() const
	{
		// Whole retained ticks indicate overload: show current state, never extrapolate
		// or wrap the remainder back toward an older pose.
		return std::clamp(m_PhysicsTiming.pendingSeconds / m_PhysicsStepSeconds, 0.0, 1.0);
	}

	std::expected<_Scene::RenderTransform, RenderTransformQueryError> _Scene::GetRenderTransform(const _Entity& entity)
	{
		m_RenderData.RequireMutable();
		if (entity.GetSceneContext() != this || !entity.HasAllComponents<Transform3DComponent>())
			return std::unexpected(RenderTransformQueryError{});
		const auto handle = static_cast<entt::entity>(entity);
		const auto matrix = SampleRenderMatrix(handle);
		auto& sampled = m_Registry.get_or_emplace<RenderTransform>(handle);
		if (sampled.revision == 0 || sampled.matrix != matrix)
		{
			sampled.matrix = matrix;
			sampled.revision = ++m_RenderTransformRevision;
		}
		sampled.simulationRevision = m_PhysicsTiming.totalSteps;
		return sampled;
	}

    RenderPresentationInput _Scene::ObserveRenderPresentation(entt::entity handle) const
    {
        const auto& transform = m_Registry.get<Transform3DComponent>(handle);
        const auto* link = m_Registry.try_get<RelationshipComponent>(handle);
        const UUID parent = link ? link->ParentHandle : UUID{0};
        RenderPresentationInput input;
        input.translation = transform.Translation;
        input.rotation = transform.QuatRotation;
        input.scale = transform.Scale;
        const auto* pose = m_Registry.try_get<RuntimePhysicsPose>(handle);
        const auto* rigidBody = m_Registry.try_get<RigidBody3DComponent>(handle);
        // Lifetime/readiness is tested freshly, before any body dereference.
        if (pose && rigidBody && ValidPhysicsPose(m_PhysicsSystem->GetPhysicsWorld(), *rigidBody, *pose))
        {
            const bool authoredEdit = transform.Translation != pose->translation || transform.QuatRotation != pose->rotation;
            if (!authoredEdit)
            {
                const auto body = m_PhysicsSystem->ReadBodyPose(pose->handle);
                if (!body) return input;
                input.translation = body->position;
                input.rotation = body->orientation;
                if (m_RenderInterpolationEnabled && !m_IsPaused && transform.Scale == pose->scale
                    && parent == pose->parent && input.translation == pose->currentTranslation
                    && input.rotation == pose->currentRotation)
                {
                    input.interpolate = true;
                    input.alpha = static_cast<float>(GetRenderInterpolationAlpha());
                    input.previousTranslation = pose->previousTranslation;
                    input.currentTranslation = pose->currentTranslation;
                    input.previousRotation = pose->previousRotation;
                    input.currentRotation = pose->currentRotation;
                }
            }
        }
        return input;
    }
    SceneBodyPose _Scene::EvaluateRenderPose(const RenderPresentationInput& input)
    {
        auto translation = input.translation;
        auto rotation = input.rotation;
        if (input.interpolate)
        {
            translation = glm::mix(input.previousTranslation, input.currentTranslation, input.alpha);
            rotation = glm::normalize(glm::slerp(input.previousRotation, input.currentRotation, input.alpha));
        }
        return {translation, glm::normalize(rotation)};
    }
    Mat4 _Scene::EvaluateRenderPresentation(const RenderPresentationInput& input)
    {
        const auto pose = EvaluateRenderPose(input);
        return glm::translate(Mat4(1.0f), pose.translation) * glm::toMat4(pose.rotation)
            * glm::scale(Mat4(1.0f), input.scale);
    }
    Mat4 _Scene::SampleRenderMatrix(entt::entity handle) const
    {
        return EvaluateRenderPresentation(ObserveRenderPresentation(handle));
    }

    std::expected<EntityRenderId, TransformMutationError>
    _Scene::ValidateTransformMutation(const _Entity& entity)
    {
        using Error = TransformMutationErrorCode;
        if (m_RenderData.IsExtracting())
            return std::unexpected(TransformMutationError{Error::ExtractionActive});
        if (m_ApplyingTransform)
            return std::unexpected(TransformMutationError{Error::MutationActive});
        if (!entity.GetSceneContext())
            return std::unexpected(TransformMutationError{Error::InvalidEntity});
        if (entity.GetSceneContext() != this)
            return std::unexpected(TransformMutationError{Error::ForeignEntity});
        if (!entity.HasAllComponents<IDComponent>())
            return std::unexpected(TransformMutationError{Error::InvalidEntity});
        const auto id = entity.GetUUID();
        if (id == 0)
            return std::unexpected(TransformMutationError{Error::InvalidEntity});
        if (!entity.HasAllComponents<Transform3DComponent>())
            return std::unexpected(TransformMutationError{Error::MissingTransform, id});
        auto identity = m_RenderData.Identify(static_cast<entt::entity>(entity));
        if (!identity)
            return std::unexpected(TransformMutationError{Error::IdentityExhausted, id});
        return *identity;
    }

    AuthoritativeTransformMutation
    _Scene::PublishAuthoritativeTransformChange(const _Entity& entity, EntityRenderId identity,
                                                AuthoritativeTransformChangeKind kind)
    {
        const auto& transform = entity.GetComponent<Transform3DComponent>();
        const AuthoritativeTransformChange change{identity,
                                                  entity.GetParentUUID(),
                                                  kind,
                                                  transform.Translation,
                                                  transform.EulerRotation,
                                                  transform.Scale,
                                                  transform.QuatRotation};
        // Only a value snapshot crosses dispatch: callbacks may perform another semantic
        // mutation. Each notification describes its own commit, not a later nested commit.
        return {true, m_AuthoritativeTransformChanges.Dispatch(change)};
    }

    std::expected<AuthoritativeTransformMutation, TransformMutationError>
    _Scene::ApplyLocalPose(const _Entity& entity, Vec3f translation, Quat rotation,
                           Vec3f eulerRotation)
    {
        using Error = TransformMutationErrorCode;
        auto identity = ValidateTransformMutation(entity);
        if (!identity)
            return std::unexpected(identity.error());
        const auto id = entity.GetUUID();
        const auto lengthSquared = glm::dot(rotation, rotation);
        if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0f)
            return std::unexpected(TransformMutationError{Error::InvalidRotation, id});
        auto& transform = m_Registry.get<Transform3DComponent>(static_cast<entt::entity>(entity));
        if (!Math::IsFinite(translation) || !Math::IsFinite(eulerRotation) ||
            !Math::IsFinite(transform.Scale))
            return std::unexpected(TransformMutationError{Error::NonFiniteTransform, id});
        if (transform.Translation == translation && transform.QuatRotation == rotation &&
            transform.EulerRotation == eulerRotation)
            return AuthoritativeTransformMutation{};

        const auto handle = static_cast<entt::entity>(entity);
        auto* pose = m_Registry.try_get<RuntimePhysicsPose>(handle);
        const auto* rigidBody = m_Registry.try_get<RigidBody3DComponent>(handle);
        const bool live = pose && rigidBody &&
                          ValidPhysicsPose(m_PhysicsSystem->GetPhysicsWorld(), *rigidBody, *pose);
        if ((pose || (rigidBody && rigidBody->RuntimeBody)) && !live)
            return std::unexpected(TransformMutationError{Error::PhysicsPoseRejected, id});
        bool bodyChanged = false;
        if (live)
        {
            const auto changed = m_PhysicsSystem->SetBodyPose(pose->handle, translation, rotation);
            if (!changed)
                return std::unexpected(TransformMutationError{Error::PhysicsPoseRejected, id, UUID{0}, changed.error()});
            bodyChanged = *changed;
        }
        transform.SetTranslation(translation);
        const auto acceptedRotation = live ? m_PhysicsSystem->ReadBodyPose(pose->handle)->orientation : rotation;
        transform.SetRotation(acceptedRotation);
        transform.EulerRotation = eulerRotation;
        if (live)
        {
            // Record the authoring teleport now so Update does not replay it as an edit.
            pose->translation = transform.Translation;
            pose->rotation = transform.QuatRotation;
            pose->authored = {transform.Translation, transform.QuatRotation};
            if (bodyChanged)
                ResetPhysicsHistory(*pose, transform, *m_PhysicsSystem->ReadBodyPose(pose->handle), entity.GetParentUUID());
        }
        return PublishAuthoritativeTransformChange(entity, *identity,
                                                   AuthoritativeTransformChangeKind::Pose);
    }

    std::expected<AuthoritativeTransformMutation, TransformMutationError>
    _Scene::SetLocalPose(const _Entity& entity, const Vec3f& translation, const Quat& rotation)
    {
        return ApplyLocalPose(entity, translation, rotation, glm::eulerAngles(rotation));
    }

    std::expected<AuthoritativeTransformMutation, TransformMutationError>
    _Scene::SetLocalTranslation(const _Entity& entity, const Vec3f& translation)
    {
        if (auto valid = ValidateTransformMutation(entity); !valid)
            return std::unexpected(valid.error());
        const auto& transform = entity.GetComponent<Transform3DComponent>();
        return ApplyLocalPose(entity, translation, transform.QuatRotation, transform.EulerRotation);
    }

    std::expected<AuthoritativeTransformMutation, TransformMutationError>
    _Scene::SetLocalRotation(const _Entity& entity, const Vec3f& eulerRotation)
    {
        if (auto valid = ValidateTransformMutation(entity); !valid)
            return std::unexpected(valid.error());
        return ApplyLocalPose(entity, entity.GetComponent<Transform3DComponent>().Translation,
                              Quat(eulerRotation), eulerRotation);
    }

    std::expected<AuthoritativeTransformMutation, TransformMutationError>
    _Scene::SetLocalScale(const _Entity& entity, const Vec3f& scale)
    {
        using Error = TransformMutationErrorCode;
        auto identity = ValidateTransformMutation(entity);
        if (!identity)
            return std::unexpected(identity.error());
        auto& transform = m_Registry.get<Transform3DComponent>(static_cast<entt::entity>(entity));
        const auto lengthSquared = glm::dot(transform.QuatRotation, transform.QuatRotation);
        if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0f)
            return std::unexpected(
                TransformMutationError{Error::InvalidRotation, entity.GetUUID()});
        if (!Math::IsFinite(scale) || !Math::IsFinite(transform.Translation))
            return std::unexpected(
                TransformMutationError{Error::NonFiniteTransform, entity.GetUUID()});
        if (transform.Scale == scale)
            return AuthoritativeTransformMutation{};
        const auto e = static_cast<entt::entity>(entity);
        const auto* pose = m_Registry.try_get<RuntimePhysicsPose>(e);
        const auto* binding = m_Registry.try_get<RigidBody3DComponent>(e);
        PhysicsBodyHandle liveHandle{};
        if (pose || (binding && binding->RuntimeBody))
        {
            auto body = GetBodyBinding(entity);
            if (!body) return std::unexpected(body.error());
            liveHandle = *body;
            // Shape allocation, validation and body-space representability checks
            // complete before either live shape or authoritative Transform commits.
            auto scaled = m_PhysicsSystem->SetBodyScale(liveHandle, scale);
            if (!scaled)
                return std::unexpected(TransformMutationError{Error::PhysicsScaleRejected,
                    entity.GetUUID(), UUID{0}, scaled.error()});
        }
        {
            struct ApplyingTransform
            {
                bool& active;
                PhysicsBodyHandle& committing;
                explicit ApplyingTransform(bool& value, PhysicsBodyHandle& target, PhysicsBodyHandle body)
                    : active(value), committing(target)
                {
                    active = true;
                    committing = body;
                }
                ~ApplyingTransform()
                {
                    committing = {};
                    active = false;
                }
            } applying(m_ApplyingTransform, m_CommittingScaleBody, liveHandle);
            transform.SetScale(scale);
        }
        (void)ResetRenderInterpolation(entity); // Validated member; changed authoring scale only.
        return PublishAuthoritativeTransformChange(entity, *identity,
                                                   AuthoritativeTransformChangeKind::Scale);
    }

    std::expected<PhysicsBodyHandle, TransformMutationError>
    _Scene::GetBodyBinding(const _Entity& entity) const
    {
        using Error = TransformMutationErrorCode;
        (void)m_RenderData.IsExtracting();
        if (entity.GetSceneContext() != this)
            return std::unexpected(TransformMutationError{Error::ForeignEntity, {0}});
        if (!entity.HasAllComponents<IDComponent, Transform3DComponent>())
            return std::unexpected(TransformMutationError{Error::InvalidEntity, {0}});
        const auto e = static_cast<entt::entity>(entity);
        const auto* binding = m_Registry.try_get<RigidBody3DComponent>(e);
        const auto* pose = m_Registry.try_get<RuntimePhysicsPose>(e);
        if (!binding || !pose)
            return std::unexpected(TransformMutationError{Error::MissingPhysicsBody, entity.GetUUID()});
        if (!ValidPhysicsPose(m_PhysicsSystem->GetPhysicsWorld(), *binding, *pose))
            return std::unexpected(TransformMutationError{Error::PhysicsPoseRejected, entity.GetUUID()});
        return pose->handle;
    }

    std::expected<SceneBodyTransform, TransformMutationError>
    _Scene::ReadBodyTransform(const _Entity& entity) const
    {
        auto binding = GetBodyBinding(entity);
        if (!binding) return std::unexpected(binding.error());
        const auto e = static_cast<entt::entity>(entity);
        const auto& pose = m_Registry.get<RuntimePhysicsPose>(e);
        const auto body = m_PhysicsSystem->ReadBodyPose(*binding);
        if (!body) return std::unexpected(TransformMutationError{
            TransformMutationErrorCode::PhysicsPoseRejected, entity.GetUUID(), UUID{0}, body.error()});
        return SceneBodyTransform{*binding, body->type, pose.authored,
            {pose.previousTranslation, pose.previousRotation}, {body->position, body->orientation},
            EvaluateRenderPose(ObserveRenderPresentation(e))};
    }

    std::expected<void, TransformMutationError>
    _Scene::ReplaceBodyCollider(const _Entity& entity, const CollisionShapeDesc& desc)
    {
        if (auto mutableEntity = ValidateTransformMutation(entity); !mutableEntity)
            return std::unexpected(mutableEntity.error());
        auto binding = GetBodyBinding(entity);
        if (!binding) return std::unexpected(binding.error());
        if (auto replaced = m_PhysicsSystem->ReplaceBodyCollider(*binding, desc); !replaced)
            return std::unexpected(TransformMutationError{TransformMutationErrorCode::PhysicsScaleRejected,
                entity.GetUUID(), UUID{0}, replaced.error()});
        (void)ResetRenderInterpolation(entity);
        return {};
    }

    std::expected<AuthoritativeTransformMutation, TransformMutationError>
    _Scene::SetParent(const _Entity& entity, const _Entity& parent)
    {
        using Error = TransformMutationErrorCode;
        auto identity = ValidateTransformMutation(entity);
        if (!identity)
            return std::unexpected(identity.error());
        const auto id = entity.GetUUID();
        const bool detach = static_cast<entt::entity>(parent) == entt::null;
        if (!detach && parent.GetSceneContext() != this)
            return std::unexpected(TransformMutationError{Error::ForeignEntity, id});
        if (!detach && !parent.HasAllComponents<IDComponent, Transform3DComponent>())
            return std::unexpected(TransformMutationError{Error::InvalidParent, id});
        std::unordered_set<UUID> visited;
        for (auto ancestor = detach ? _Entity{} : parent; ancestor;)
        {
            if (ancestor == entity || !visited.insert(ancestor.GetUUID()).second)
                return std::unexpected(TransformMutationError{Error::Cycle, id, parent.GetUUID()});
            const auto next = ancestor.GetParentUUID();
            ancestor = ancestor.GetParent();
            if (next != 0 && !ancestor)
                return std::unexpected(TransformMutationError{Error::InvalidParent, id, next});
        }
        const UUID parentId = detach ? UUID(0) : parent.GetUUID();
        EntityRenderId parentIdentity{};
        if (!detach)
        {
            auto identified = m_RenderData.Identify(static_cast<entt::entity>(parent));
            if (!identified)
                return std::unexpected(
                    TransformMutationError{Error::IdentityExhausted, id, parentId});
            parentIdentity = *identified;
        }
        if (entity.GetParentUUID() == parentId && (detach || entity.GetParent() == parent))
            return AuthoritativeTransformMutation{};
        const auto handle = static_cast<entt::entity>(entity);
        const auto currentParent = entity.GetParent();
        (void)m_Registry.get_or_emplace<RelationshipComponent>(handle);
        if (!detach)
        {
            auto& children =
                m_Registry.get_or_emplace<RelationshipComponent>(static_cast<entt::entity>(parent))
                    .Children;
            if (std::find(children.begin(), children.end(), id) == children.end())
                children.push_back(id);
        }
        if (currentParent)
            if (auto* link = m_Registry.try_get<RelationshipComponent>(
                    static_cast<entt::entity>(currentParent)))
                std::erase(link->Children, id);
        // Relationship insertion may relocate its storage; reacquire before publication.
        auto& relationship = m_Registry.get<RelationshipComponent>(handle);
        relationship.ParentHandle = parentId;
        relationship.ParentIdentity = parentIdentity;
        (void)ResetRenderInterpolation(entity);
        return PublishAuthoritativeTransformChange(entity, *identity,
                                                   AuthoritativeTransformChangeKind::Parent);
    }

	std::expected<void, TransformError> _Scene::ResetRenderInterpolation(const _Entity& entity)
	{
		m_RenderData.RequireMutable();
		if (entity.GetSceneContext() != this || !entity)
			return std::unexpected(TransformError{TransformErrorCode::InvalidEntity});
		const auto handle = static_cast<entt::entity>(entity);
		auto* pose = m_Registry.try_get<RuntimePhysicsPose>(handle);
		const auto* rigidBody = m_Registry.try_get<RigidBody3DComponent>(handle);
		const auto* transform = m_Registry.try_get<Transform3DComponent>(handle);
		if (pose && rigidBody && transform && ValidPhysicsPose(m_PhysicsSystem->GetPhysicsWorld(), *rigidBody, *pose))
			if (auto body = m_PhysicsSystem->ReadBodyPose(pose->handle))
                ResetPhysicsHistory(*pose, *transform, *body, entity.GetParentUUID());
		return {};
	}

	std::expected<WorldTransformUpdate, TransformError> _Scene::UpdateWorldTransforms()
	{
		m_RenderData.RequireMutable();
        auto* previousGraph = m_Registry.ctx().find<C3WorldGraph>();
        bool sameGraph = previousGraph != nullptr;
        std::size_t ordinal = 0;
        const auto observe = [&](entt::entity e) {
            C3TopologyInput input{e, m_Registry.get<IDComponent>(e).ID};
            if (const auto* parent = m_Registry.try_get<RelationshipComponent>(e)) {
                input.relationship = true; input.parent = parent->ParentHandle; input.parentIdentity = parent->ParentIdentity;
            }
            input.transform = m_Registry.all_of<Transform3DComponent>(e);
            input.anchor = m_Registry.all_of<RigidBody3DComponent>(e);
            return input;
        };
        for (auto e : m_Registry.view<IDComponent>())
        {
            auto input = observe(e);
            if (sameGraph)
            {
                if (ordinal >= previousGraph->observed.size()) sameGraph = false;
                else
                {
                    const auto& old = previousGraph->observed[ordinal];
                    const auto resolved = m_RenderData.Resolve(old.identity);
                    input.identity = old.identity;
                    if (!resolved || *resolved != e || input != old) sameGraph = false;
                }
            }
            ++ordinal;
        }
        sameGraph = sameGraph && ordinal == previousGraph->observed.size();
        C3WorldGraph nextGraph;
        const C3WorldGraph* graph = previousGraph;
        if (!sameGraph)
        {
        using Node = C3WorldNode;
		std::vector<Node> nodes;
		for (auto e : m_Registry.view<IDComponent>())
			nodes.push_back({e, m_Registry.get<IDComponent>(e).ID});
		std::sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) { return a.uuid < b.uuid; });
		std::unordered_map<entt::entity, std::size_t> indices;
		for (std::size_t i = 0; i != nodes.size(); ++i)
		{
			auto& node = nodes[i];
			if (node.uuid == 0 || (i && node.uuid == nodes[i - 1].uuid))
				return std::unexpected(TransformError{TransformErrorCode::InvalidEntity, node.uuid});
			if (!m_Registry.all_of<Transform3DComponent>(node.handle))
				return std::unexpected(TransformError{TransformErrorCode::MissingTransform, node.uuid});
			auto identity = m_RenderData.Identify(node.handle);
			if (!identity) return std::unexpected(TransformError{TransformErrorCode::IdentityExhausted, node.uuid});
			node.identity = *identity;
			indices.emplace(node.handle, i);
		}
		for (auto& node : nodes)
		{
			const auto* link = m_Registry.try_get<RelationshipComponent>(node.handle);
			if (!link || link->ParentHandle == 0) continue;
			auto parent = m_RenderData.Resolve(link->ParentIdentity);
			if (!parent || !indices.contains(*parent)
				|| nodes[indices.at(*parent)].uuid != link->ParentHandle)
				return std::unexpected(TransformError{TransformErrorCode::InvalidParent, node.uuid, link->ParentHandle});
			node.parent = indices.at(*parent);
			node.parentIdentity = link->ParentIdentity;
		}

		// Iterative tri-color traversal: depth does not consume the C++ call stack.
		// UUID-sorted starting points make both traversal and first-error selection stable.
		std::vector<unsigned char> state(nodes.size());
		std::vector<std::size_t> order, chain;
		order.reserve(nodes.size());
		for (std::size_t start = 0; start != nodes.size(); ++start)
		{
			if (state[start] == 2) continue;
			chain.clear();
			auto cursor = start;
			while (cursor != SIZE_MAX && state[cursor] == 0)
			{
				state[cursor] = 1;
				chain.push_back(cursor);
				cursor = nodes[cursor].parent;
			}
			if (cursor != SIZE_MAX && state[cursor] == 1)
				return std::unexpected(TransformError{TransformErrorCode::Cycle, nodes[cursor].uuid,
					nodes[nodes[cursor].parent].uuid});
			for (auto it = chain.rbegin(); it != chain.rend(); ++it)
			{
				state[*it] = 2;
				order.push_back(*it);
			}
		}

            nextGraph.nodes = std::move(nodes);
            nextGraph.order = std::move(order);
            nextGraph.observed.reserve(ordinal);
            for (auto e : m_Registry.view<IDComponent>())
            {
                auto input = observe(e);
                input.identity = m_RenderData.Identify(e).value(); // Full validator already identified each node.
                nextGraph.observed.push_back(input);
            }
            graph = &nextGraph;

        }
        const auto& nodes = graph->nodes;
        const auto& order = graph->order;

		WorldTransformUpdate result;
		result.transforms.reserve(nodes.size());
		std::vector<CachedWorldTransform> candidate(nodes.size());
        std::vector<bool> modified(nodes.size());
		for (auto index : order)
		{
			const auto& node = nodes[index];
			const auto& local = m_Registry.get<Transform3DComponent>(node.handle);
			auto& cache = candidate[index];
			if (const auto* previous = m_Registry.try_get<CachedWorldTransform>(node.handle)) cache = *previous;
			const bool localChanged = !cache.revision || cache.translation != local.Translation
				|| cache.rotation != local.QuatRotation || cache.scale != local.Scale;
			if (localChanged)
			{
				const auto lengthSquared = glm::dot(local.QuatRotation, local.QuatRotation);
				if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0f)
					return std::unexpected(TransformError{TransformErrorCode::InvalidRotation, node.uuid});
				cache.local = glm::translate(Mat4(1.0f), local.Translation)
					* glm::toMat4(glm::normalize(local.QuatRotation)) * glm::scale(Mat4(1.0f), local.Scale);
				if (!FiniteMatrix(cache.local))
					return std::unexpected(TransformError{TransformErrorCode::NonFiniteTransform, node.uuid});
				cache.translation = local.Translation;
				cache.rotation = local.QuatRotation;
				cache.scale = local.Scale;
			}
			const bool anchor = m_Registry.all_of<RigidBody3DComponent>(node.handle);
			const bool compose = node.parent != SIZE_MAX && !anchor;
			const auto parentRevision = compose ? candidate[node.parent].revision : 0;
			if (localChanged || cache.parent != node.parentIdentity || cache.parentRevision != parentRevision
				|| cache.worldAnchor != anchor)
			{
                modified[index] = true;
				const Mat4 world = compose ? candidate[node.parent].world * cache.local : cache.local;
				if (!FiniteMatrix(world))
					return std::unexpected(TransformError{TransformErrorCode::NonFiniteTransform, node.uuid});
				++result.recomputed;
				if (!cache.revision || cache.world != world)
				{
					Asset::AssetDetail::RequireInvariant(cache.revision != UINT64_MAX);
					++cache.revision;
					cache.world = world;
				}
				cache.parent = node.parentIdentity;
				cache.parentRevision = parentRevision;
				cache.worldAnchor = anchor;
			}
			result.transforms.push_back({node.identity, cache.world, cache.revision});
		}
		// Publish caches only after the whole graph, including composed matrices, is valid.
		for (std::size_t i = 0; i != nodes.size(); ++i)
            if (modified[i]) m_Registry.emplace_or_replace<CachedWorldTransform>(nodes[i].handle, candidate[i]);
        if (!sameGraph)
        {
            if (previousGraph) *previousGraph = std::move(nextGraph);
            else m_Registry.ctx().emplace<C3WorldGraph>(std::move(nextGraph));
        }

#ifdef GENGINE_RENDER_WORLD_DIAGNOSTICS
        const auto& retained=*m_Registry.ctx().find<C3WorldGraph>();
        RenderCpu::counts.worldCpuBytes=sizeof(C3WorldGraph)+retained.nodes.capacity()*sizeof(C3WorldNode)
            +retained.order.capacity()*sizeof(std::size_t)+retained.observed.capacity()*sizeof(C3TopologyInput)
            +m_Registry.storage<CachedWorldTransform>().capacity()*sizeof(CachedWorldTransform);
#endif
        return result;
    }


    void _Scene::SetPaused(bool paused)
	{
		m_RenderData.RequireMutable();
		if (m_IsPaused == paused) return;
		m_IsPaused = paused;
		// Pause/resume snaps to current state; resuming without a tick cannot rewind.
		for (auto e : m_Registry.view<RuntimePhysicsPose>())
			(void)ResetRenderInterpolation(_Entity(e, this)); // Live member of this scene.
	}

    std::expected<void, PhysicsShapeError> _Scene::OnRuntimeStart()
    {
        m_RenderData.RequireMutable();
        m_IsRunning = true;
        auto started = OnPhysics3DStart();
        if (!started) m_IsRunning = false;
        return started;
    }

	void _Scene::OnRuntimeStop()
	{
		m_RenderData.RequireMutable();
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
		m_RenderData.RequireMutable();
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

    std::expected<SceneAssignmentChange, SceneAssignmentError> _Scene::AssignRenderable(
        _Entity entity, const Component::MeshRendererComponent& value,
        const RenderStateResources& resources)
    {
        if (m_RenderData.IsExtracting())
            return std::unexpected(SceneAssignmentError::ExtractionActive);
        if (!entity.GetSceneContext())
            return std::unexpected(SceneAssignmentError::InvalidEntity);
        if (entity.GetSceneContext() != this)
            return std::unexpected(SceneAssignmentError::ForeignEntity);
        if (!m_Registry.valid(entity))
            return std::unexpected(SceneAssignmentError::InvalidEntity);
        if (!m_Registry.all_of<IDComponent>(entity))
            return std::unexpected(SceneAssignmentError::MissingIdentity);

        // Range validation reads CPU metadata paired with the published identity.
        // It does not acquire a GpuMesh merely to inspect authoring facts.
        auto mesh = resources.meshes.ReadMetadata(value.mesh);
        if (!mesh)
            return std::unexpected(SceneAssignmentError::InvalidMesh);
        auto material = resources.materials.Acquire(resources.access, value.material);
        if (!material)
            return std::unexpected(SceneAssignmentError::InvalidMaterial);
        if (value.submesh >= mesh->submeshCount)
            return std::unexpected(SceneAssignmentError::InvalidSubmesh);
        const auto* previous = m_Registry.try_get<Component::MeshRendererComponent>(entity);
        if (previous && *previous == value)
            return SceneAssignmentChange{false, m_RenderAssignmentRevision};
        if (m_RenderAssignmentRevision == UINT64_MAX)
            return std::unexpected(SceneAssignmentError::RevisionExhausted);
        if (!PrepareGeometryReceipt(entity))
            return std::unexpected(SceneAssignmentError::AuthoringAllocation);
        if (!SceneRenderResources::CanAddGeometryOwner(value.mesh))
            return std::unexpected(SceneAssignmentError::AuthoringOwnershipExhausted);
        auto identity = m_RenderData.Identify(entity);
        if (!identity)
            return std::unexpected(SceneAssignmentError::IdentityExhausted);

        m_Registry.emplace_or_replace<Component::MeshRendererComponent>(entity, value);
        return SceneAssignmentChange{true, ++m_RenderAssignmentRevision};
    }

    std::expected<_Entity, SceneError> _Scene::DuplicateEntity(_Entity entity)
    {
        m_RenderData.RequireMutable();
        if (entity.GetSceneContext() != this)
            return std::unexpected(SceneError{SceneErrorCode::ForeignEntity, "_Scene::DuplicateEntity",
                "Duplicate requires a live entity in this scene"});
        if (!m_Registry.valid(entity))
            return std::unexpected(SceneError{SceneErrorCode::InvalidIdentity,
                "_Scene::DuplicateEntity", "Duplicate requires a live entity"});
        if (!entity.HasAllComponents<IDComponent>())
            return std::unexpected(SceneError{SceneErrorCode::MissingIdentity, "_Scene::DuplicateEntity",
                "Duplicate requires a live entity in this scene"});
        // Copy name before component storage may move.
        std::string name = entity.GetName();
        auto created = CreateEntity(name);
        if (!created) return std::unexpected(created.error());
        _Entity newEntity = *created;
        struct Rollback
        {
            _Scene& scene;
            _Entity entity;
            UUID id;
            bool complete = false;
            ~Rollback()
            {
                if (complete) return;
                if (const auto* link = scene.m_Registry.try_get<RelationshipComponent>(entity))
                    if (auto parent = scene.GetEntityByUUID(link->ParentHandle); parent)
                        if (auto* relationship = scene.m_Registry.try_get<RelationshipComponent>(parent))
                            std::erase(relationship->Children, id);
                scene.RemoveFromRenderLists(entity);
                scene.m_Registry.destroy(entity);
                scene.m_EntityMap.erase(id);
            }
        } rollback{*this, newEntity, newEntity.GetUUID()};
        if (!PrepareGeometryReceipt(newEntity))
            return std::unexpected(SceneError{SceneErrorCode::InvalidScene, "_Scene::DuplicateEntity",
                "Duplicate could not reserve geometry-owner accounting"});
        if (const auto* renderer = m_Registry.try_get<MeshRendererComponent>(entity);
            renderer && !SceneRenderResources::CanAddGeometryOwner(renderer->mesh))
            return std::unexpected(SceneError{SceneErrorCode::InvalidScene, "_Scene::DuplicateEntity",
                "Duplicate would exhaust geometry-owner accounting"});
        CopyComponentIfExists(AllComponents{}, newEntity, entity);
        ClearCopiedRuntimeBindings(m_Registry, static_cast<entt::entity>(newEntity),
            m_Registry.try_get<RuntimePhysicsPose>(static_cast<entt::entity>(entity)));
        // A single-entity duplicate is a sibling; its source's children are not copied.
        if (newEntity.HasAllComponents<RelationshipComponent>())
            newEntity.GetComponent<RelationshipComponent>() = RelationshipComponent{};
        if (auto parented = newEntity.SetParent(entity.GetParent()); !parented)
            return std::unexpected(SceneError{SceneErrorCode::Parenting, "_Scene::DuplicateEntity",
                "Duplicate could not preserve the source parent", entity.GetUUID(), parented.error()});
        if (auto published = PushToRenderList(newEntity); !published)
            return std::unexpected(published.error());
        auto identity = m_RenderData.Identify(newEntity);
        if (!identity)
            return std::unexpected(SceneError{SceneErrorCode::InvalidIdentity,
                "_Scene::DuplicateEntity", "Duplicate could not allocate a render identity"});
        rollback.complete = true;
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

    std::expected<entt::entity, SceneCameraError>
    _Scene::ResolveSceneCamera(EntityRenderId id) const
    {
        if (m_RenderData.IsExtracting())
            return std::unexpected(SceneCameraError{SceneCameraErrorCode::ExtractionActive, id});
        auto entity = m_RenderData.Resolve(id);
        if (!entity) return std::unexpected(CameraIdentityError(id, entity.error()));
        // Compatibility component writes cannot change a transient view's lifetime role.
        if (m_Registry.any_of<TransientEditorView>(*entity) || !m_Registry.all_of<IDComponent>(*entity))
            return std::unexpected(SceneCameraError{SceneCameraErrorCode::MissingIdentity, id});
        if (!m_Registry.all_of<RenderCameraComponent>(*entity))
            return std::unexpected(SceneCameraError{SceneCameraErrorCode::MissingCamera, id});
        if (!m_Registry.all_of<Transform3DComponent>(*entity))
            return std::unexpected(SceneCameraError{SceneCameraErrorCode::MissingTransform, id});
        return *entity;
    }

    std::expected<EntityRenderId, SceneCameraError> _Scene::CreateEditorViewIdentity()
    {
        if (m_RenderData.IsExtracting())
            return std::unexpected(SceneCameraError{SceneCameraErrorCode::ExtractionActive});
        const auto entity = m_Registry.create();
        auto id = m_RenderData.Identify(entity);
        if (!id)
        {
            m_Registry.destroy(entity);
            return std::unexpected(CameraIdentityError({}, id.error()));
        }
        m_Registry.emplace<TransientEditorView>(entity);
        return *id;
    }

    std::expected<void, SceneCameraError>
    _Scene::ValidateEditorViewIdentity(EntityRenderId id) const
    {
        auto entity = m_RenderData.Resolve(id);
        if (!entity) return std::unexpected(CameraIdentityError(id, entity.error()));
        if (!m_Registry.all_of<TransientEditorView>(*entity) ||
            m_Registry.any_of<IDComponent, Transform3DComponent, RenderCameraComponent,
                              MeshRendererComponent, RigidBody3DComponent>(*entity))
            return std::unexpected(SceneCameraError{SceneCameraErrorCode::NotEditorView, id});
        return {};
    }

    std::expected<void, SceneCameraError> _Scene::RetireEditorViewIdentity(EntityRenderId id)
    {
        if (m_RenderData.IsExtracting())
            return std::unexpected(SceneCameraError{SceneCameraErrorCode::ExtractionActive, id});
        auto entity = m_RenderData.Resolve(id);
        if (!entity) return std::unexpected(CameraIdentityError(id, entity.error()));
        // A compatibility projection value invalidates rendering, not the owned
        // transient lifetime. Authored state and resource owners still fail closed.
        if (!m_Registry.all_of<TransientEditorView>(*entity) ||
            m_Registry.any_of<IDComponent, Transform3DComponent,
                              MeshRendererComponent, RigidBody3DComponent>(*entity))
            return std::unexpected(SceneCameraError{SceneCameraErrorCode::NotEditorView, id});
        m_Registry.destroy(*entity);
        return {};
    }

    std::expected<std::optional<EntityRenderId>, SceneCameraError> _Scene::ActiveSceneCamera() const
    {
        if (m_ActiveSceneCamera)
            if (auto valid = ResolveSceneCamera(*m_ActiveSceneCamera); !valid)
                return std::unexpected(valid.error());
        return m_ActiveSceneCamera;
    }

    std::expected<void, SceneCameraError> _Scene::ClearActiveSceneCamera()
    {
        if (m_RenderData.IsExtracting())
            return std::unexpected(SceneCameraError{SceneCameraErrorCode::ExtractionActive});
        m_ActiveSceneCamera.reset();
        return {};
    }

    std::expected<CameraSnapshot, SceneCameraError>
    _Scene::SnapshotSceneCamera(EntityRenderId id, const RenderStateResources& resources,
                                const CameraViewport& viewport)
    {
        auto entity = ResolveSceneCamera(id);
        if (!entity) return std::unexpected(entity.error());
        const auto& intent = m_Registry.get<RenderCameraComponent>(*entity);
        if (!std::isfinite(intent.verticalFovRadians) || !std::isfinite(intent.orthographicHeight) ||
            !std::isfinite(intent.nearPlane) || !std::isfinite(intent.farPlane))
            return std::unexpected(SceneCameraError{SceneCameraErrorCode::InvalidProjection, id});
        auto sampled = UpdateRenderState(resources);
        if (!sampled)
            return std::unexpected(SceneCameraError{
                sampled.error().code == TransformErrorCode::NonFiniteTransform ||
                sampled.error().code == TransformErrorCode::InvalidRotation
                    ? SceneCameraErrorCode::InvalidPose : SceneCameraErrorCode::TransformUnavailable,
                id, {}, {}, sampled.error()});
        for (const auto row : sampled->entities)
        {
            if (row.entity != id) continue;
            const glm::dmat3 basis(row.world);
            const auto residual = glm::transpose(basis) * basis - glm::dmat3(1);
            double maximum = 0;
            for (int c = 0; c != 3; ++c)
                for (int r = 0; r != 3; ++r)
                    maximum = (std::max)(maximum, std::abs(residual[c][r]));
            if (!FiniteMatrix(row.world) || maximum > 2e-4 ||
                std::abs(glm::determinant(basis) - 1.) > 2e-4)
                return std::unexpected(SceneCameraError{SceneCameraErrorCode::InvalidPose, id});
            const auto& projection = *row.cpu.cameraIntent;
            CameraSnapshotDesc desc;
            desc.position = glm::dvec3(row.world[3]);
            desc.orientation = glm::normalize(glm::quat_cast(basis));
            desc.projection = projection.projection;
            desc.verticalFovRadians = projection.verticalFovRadians;
            desc.orthographicHeight = projection.orthographicHeight;
            desc.nearPlane = projection.nearPlane;
            desc.farPlane = projection.farPlane;
            desc.visibleLayers = projection.visibleLayers;
            desc.viewport = viewport;
            auto snapshot = CameraSnapshot::Create(desc);
            if (!snapshot)
            {
                const auto code = snapshot.error().code == CameraErrorCode::EmptyViewport
                    ? SceneCameraErrorCode::EmptyViewport
                    : snapshot.error().code == CameraErrorCode::InvalidViewport
                        ? SceneCameraErrorCode::InvalidViewport : SceneCameraErrorCode::InvalidProjection;
                return std::unexpected(SceneCameraError{code, id, {}, snapshot.error()});
            }
            return *snapshot;
        }
        return std::unexpected(SceneCameraError{SceneCameraErrorCode::MissingTransform, id});
    }

    std::expected<void, SceneCameraError>
    _Scene::SetActiveSceneCamera(EntityRenderId id, const RenderStateResources& resources)
    {
        if (auto valid = SnapshotSceneCamera(id, resources, {}); !valid)
            return std::unexpected(valid.error());
        m_ActiveSceneCamera = id;
        return {};
    }

    std::expected<void, SceneCameraError> _Scene::DeleteSceneCamera(EntityRenderId id)
    {
        auto entity = ResolveSceneCamera(id);
        if (!entity) return std::unexpected(entity.error());
        if (auto retired = DestroyEntity(_Entity{*entity, this}); !retired)
            return std::unexpected(SceneCameraError{SceneCameraErrorCode::RetirementFailed,
                id, {}, {}, {}, retired.error()});
        return {};
    }

    std::expected<void, SceneCameraError>
    _Scene::ReplaceSceneCamera(EntityRenderId old, EntityRenderId replacement,
                               const RenderStateResources& resources)
    {
        auto source = ResolveSceneCamera(old);
        if (!source) return std::unexpected(source.error());
        auto target = ResolveSceneCamera(replacement);
        if (!target) return std::unexpected(target.error());
        if (old == replacement)
            return std::unexpected(SceneCameraError{SceneCameraErrorCode::InvalidReplacement, old});
        if (auto valid = SnapshotSceneCamera(replacement, resources, {}); !valid)
            return std::unexpected(valid.error());
        // Destruction includes descendants: a replacement must survive that retirement.
        for (auto parent = _Entity{*target, this}.GetParent(); parent; parent = parent.GetParent())
            if (static_cast<entt::entity>(parent) == *source)
                return std::unexpected(SceneCameraError{SceneCameraErrorCode::InvalidReplacement, replacement});
        const bool transfer = m_ActiveSceneCamera == old;
        if (auto retired = DeleteSceneCamera(old); !retired) return retired;
        if (transfer) m_ActiveSceneCamera = replacement;
        return {};
    }

	std::expected<_Entity, SceneError> _Scene::CreateEntity(const std::string& tag)
	{
		return CreateEntityWithUUID(UUID(), tag);
	}

	std::expected<_Entity, SceneError> _Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		m_RenderData.RequireMutable();
		if (uuid == 0 || GetEntityByUUID(uuid))
            return std::unexpected(SceneError{SceneErrorCode::InvalidIdentity, "_Scene::CreateEntityWithUUID",
                "Entity UUID must be nonzero and unique in its scene", uuid});
        _Entity entity = { m_Registry.create(), this };
        struct Rollback
        {
            entt::registry& registry;
            entt::entity entity;
            bool complete = false;
            ~Rollback() { if (!complete) registry.destroy(entity); }
        } rollback{m_Registry, entity};
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<Transform3DComponent>();
		auto& tag = entity.AddComponent<TagComponent>();
		tag.Name = name.empty() ? "Entity" : name;

		m_EntityMap[uuid] = entity;
        rollback.complete = true;

		return entity;
	}

	std::expected<void, SceneError> _Scene::DestroyEntity(_Entity entity, bool excludeChildren, bool /*first*/)
	{
		m_RenderData.RequireMutable();
		if ((entt::entity)entity == entt::null)
			return {};
		if (entity.GetSceneContext() != this)
			return std::unexpected(SceneError{SceneErrorCode::ForeignEntity, "_Scene::DestroyEntity",
                "Destroy entity does not belong to this scene",
                0});
		if (!entity)
			return {};
		if (!entity.HasAllComponents<IDComponent>())
			return std::unexpected(SceneError{SceneErrorCode::MissingIdentity, "_Scene::DestroyEntity",
                "Destroy requires a scene entity with an ID", 0});

		// Gather without mutation; validate all body bindings before detaching anything.
		// The explicit worklist also makes destruction safe for very deep hierarchies.
		std::vector<_Entity> pending{entity}, retiring;
		std::unordered_set<entt::entity> visited;
		while (!pending.empty())
		{
			auto current = pending.back();
			pending.pop_back();
			if (!visited.insert(static_cast<entt::entity>(current)).second) continue;
			const auto children = std::as_const(current).Children();
			for (const auto childId : children)
			{
				auto child = GetEntityByUUID(childId);
				if (child && child.GetParent() == current)
				{
                    if (!excludeChildren) pending.push_back(child);
				}
			}
			retiring.push_back(current);
		}
        auto* retirementWorld = m_PhysicsSystem->GetPhysicsWorld();
        for (const auto current : retiring)
        {
            const auto e = static_cast<entt::entity>(current);
            const auto* binding = m_Registry.try_get<RigidBody3DComponent>(e);
            const auto* pose = m_Registry.try_get<RuntimePhysicsPose>(e);
            const auto handle = binding ? binding->RuntimeBody : PhysicsBodyHandle{};
            if (!handle && !pose) continue;
            if (!retirementWorld || !pose || !handle)
                return std::unexpected(SceneError{SceneErrorCode::InvalidIdentity, "DestroyEntity",
                    "Physics runtime binding is incomplete", std::uint64_t(current.GetUUID())});
            if (handle != pose->handle || !retirementWorld->IsBodyValid(handle))
                return std::unexpected(SceneError{SceneErrorCode::InvalidIdentity, "DestroyEntity",
                    "Physics body does not match its scene binding", std::uint64_t(current.GetUUID())});
        }
        for (auto current : retiring)
        {
            const auto children = std::as_const(current).Children();
            (void)current.SetParent({}); // Live ID and null parent: cannot fail.
            if (excludeChildren)
                for (const auto childId : children)
                    if (auto child = GetEntityByUUID(childId); child && child.GetParent() == current)
                        (void)child.SetParent({});
        }
		for (auto it = retiring.rbegin(); it != retiring.rend(); ++it)
		{
			const auto current = *it;
			const auto id = current.GetUUID();
			if (m_ActiveSceneCamera)
                if (auto active = m_RenderData.Resolve(*m_ActiveSceneCamera);
                    active && *active == static_cast<entt::entity>(current))
                    m_ActiveSceneCamera.reset();
			if (current.HasAllComponents<RigidBody3DComponent>())
			{
				const auto body = current.GetComponent<RigidBody3DComponent>().RuntimeBody;
				auto* world = m_PhysicsSystem->GetPhysicsWorld();
				if (world && body)
                    if (auto retired = world->DestroyBody(body); !retired)
                        return std::unexpected(SceneError{SceneErrorCode::InvalidIdentity,
                            "DestroyEntity", "Physics body handle could not be retired", std::uint64_t(id)});
			}
			RemoveFromRenderLists(current);
			m_Registry.destroy(static_cast<entt::entity>(current));
			m_EntityMap.erase(id);
		}

        return {};
	}

	std::expected<void, SceneError> _Scene::DestroyEntity(UUID entityID, bool excludeChildren, bool first)
	{
		auto it = m_EntityMap.find(entityID);
		if (it == m_EntityMap.end())
			return {};
		return DestroyEntity({ it->second, this }, excludeChildren, first);
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

	std::expected<void, SceneError> _Scene::PushToRenderList(_Entity entity)
	{
		m_RenderData.RequireMutable();
		if (entity.GetSceneContext() != this || !m_Registry.valid(entity))
            return std::unexpected(SceneError{SceneErrorCode::ForeignEntity, "_Scene::PushToRenderList",
                "Render-list entity does not belong to this scene"});
		if (!entity.HasAllComponents<RenderComponent>())
			return {};

		const auto* shader = entity.GetComponent<RenderComponent>().Shader;
		if (!shader || Asset::ShaderBackendAccess::Program(*shader) == 0)
            return std::unexpected(SceneError{SceneErrorCode::InvalidProgram, "_Scene::PushToRenderList",
                "Render-list entity requires a nonzero shader program", entity.HasAllComponents<IDComponent>()
                    ? static_cast<std::uint64_t>(entity.GetUUID()) : 0});
		const auto program = static_cast<unsigned int>(Asset::ShaderBackendAccess::Program(*shader));
		auto& groups = entity.HasAnyComponents<DirectionalLightComponent, PointLightComponent, SpotLightComponent>()
			? m_LightEntities : m_GroupEntities;
		const auto found = groups.find(program);
		if (found != groups.end() && std::find(found->second.begin(), found->second.end(), entity) != found->second.end())
			return {};

		// Re-publishing after a shader change moves the entity from its old group.
		RemoveFromRenderLists(entity);
		groups[program].push_back(entity);
        return {};
	}


	void _Scene::Step(int frames)
	{
		m_RenderData.RequireMutable();
		m_StepFrames = frames;
	}

    std::expected<void, PhysicsShapeError> _Scene::OnPhysics3DStart()
    {
        OnPhysics3DStop();
        struct Rollback
        {
            _Scene& scene;
            bool complete = false;
            ~Rollback() { if (!complete) { scene.m_IsRunning = false; scene.OnPhysics3DStop(); } }
        } rollback{*this};
        auto* world = new (std::nothrow) PhysicsWorld{};
        if (!world)
            return std::unexpected(PhysicsShapeError{PhysicsShapeErrorCode::Allocation,
                "_Scene::OnPhysics3DStart", "Physics world allocation failed"});
        m_PhysicsSystem->SetPhysicsWorld(world);
        for (auto e : m_Registry.view<RigidBody3DComponent>())
        {
            _Entity entity{e, this};
            auto& transform = entity.GetComponent<Transform3DComponent>();
            auto& rigid = entity.GetComponent<RigidBody3DComponent>();
            const Fixture3DProperty* property = nullptr;
            std::unique_ptr<PhysicalShape> shape;
            auto failure = [&](PhysicsShapeError error) -> std::expected<void, PhysicsShapeError>
            { error.entity = entity.GetUUID(); return std::unexpected(error); };
            if (entity.HasAllComponents<SphereFixture3DComponent>())
            {
                const auto& fixture = entity.GetComponent<SphereFixture3DComponent>();
                property = &fixture.Property;
                auto created = CreateCollisionShape(SphereShapeDesc{fixture.Radius});
                if (!created) return failure(created.error());
                shape = std::move(*created);
            }
            else if (entity.HasAllComponents<BoxFixture3DComponent>())
            {
                const auto& fixture = entity.GetComponent<BoxFixture3DComponent>();
                property = &fixture.Property;
                auto created = CreateCollisionShape(fixture.Shape, transform.Scale);
                if (!created) return failure(created.error());
                shape = std::move(*created);
            }
            else if (entity.HasAllComponents<ConvexFixture3DComponent>())
            {
                const auto& fixture = entity.GetComponent<ConvexFixture3DComponent>();
                property = &fixture.Property;
                // Preserve the existing convex startup convention: explicit local
                // points are unscaled until a connected scale mutation occurs.
                auto created = CreateCollisionShape(fixture.Shape);
                if (!created) return failure(created.error());
                shape = std::move(*created);
            }
            else if (entity.HasAllComponents<CapsuleFixture3DComponent>())
            {
                const auto& fixture = entity.GetComponent<CapsuleFixture3DComponent>();
                property = &fixture.Property;
                auto created = CreateCollisionShape(fixture.Shape, transform.Scale);
                if (!created) return failure(created.error());
                shape = std::move(*created);
            }
            else continue;
            if (!shape) return failure(PhysicsShapeError{PhysicsShapeErrorCode::Allocation,
                "_Scene::OnPhysics3DStart", "Physics shape allocation failed"});
            if (!Math::IsFinite(property->m_InvMass) || property->m_InvMass < 0)
            {
                GENGINE_CORE_ERROR("Physics body input rejected: invalid inverse mass entity={}", std::uint64_t(entity.GetUUID()));
                continue;
            }
            PhysicsBodyDesc desc;
            desc.type = rigid.Type;
            desc.position = transform.Translation; desc.orientation = transform.QuatRotation;
            desc.linearVelocity = property->m_LinearVelocity; desc.angularVelocity = property->m_AngularVelocity;
            desc.mass = rigid.Type == BodyType::Dynamic && property->m_InvMass > 0 ? 1.f / property->m_InvMass : 0.f;
            desc.material = {property->m_Friction, property->m_Elasticity};
            desc.collisionLayer = rigid.CollisionLayer; desc.collisionMask = rigid.CollisionMask;
            auto created = world->CreateBody(desc, shape);
            if (!created)
            {
                if (created.error().code == PhysicsBodyErrorCode::Allocation ||
                    created.error().code == PhysicsBodyErrorCode::IdentityExhausted)
                    return failure(PhysicsShapeError{PhysicsShapeErrorCode::Allocation,
                        "_Scene::OnPhysics3DStart", "Physics body allocation or identity capacity exhausted"});
                // Retain the existing authored-input rejection policy: no invalid body is published.
                GENGINE_CORE_ERROR("Physics body input rejected: operation={} code={} entity={}",
                    created.error().operation, unsigned(created.error().code), std::uint64_t(entity.GetUUID()));
                continue;
            }
            const auto handle = *created;
            rigid.RuntimeBody = handle;
            auto* body = *world->Resolve(handle, "Scene startup");
            // Preserve the exact existing authored inverse-mass bits (including inactive static storage).
            body->m_InvMass = property->m_InvMass;
            auto& pose = m_Registry.emplace_or_replace<RuntimePhysicsPose>(e);
            pose.handle = handle;
            const auto initial = *m_PhysicsSystem->ReadBodyPose(handle);
            PublishPhysicsPose(transform, pose, initial);
            pose.authored = {transform.Translation, transform.QuatRotation};
            ResetPhysicsHistory(pose, transform, initial, entity.GetParentUUID());
            auto connected = transform.OnScaleChanged.ConnectScoped([this, world, handle](const Vec3f& scale) {
                if (m_CommittingScaleBody != handle && m_PhysicsSystem->GetPhysicsWorld() == world)
                    m_PhysicsSystem->ApplyLegacyBodyScale(handle, scale);
            });
            if (!connected) return failure(PhysicsShapeError{PhysicsShapeErrorCode::Allocation,
                "_Scene::OnPhysics3DStart", "Scale subscription could not be established"});
            pose.scaleConnection = std::move(*connected);
            world->SetSceneRetirement(handle, [this, e, handle] {
                if (!m_Registry.valid(e)) return;
                auto* binding = m_Registry.try_get<RigidBody3DComponent>(e);
                if (binding && binding->RuntimeBody == handle) binding->RuntimeBody = {};
                // The private pose owns the connection even if an authored component
                // handle was replaced or cleared. Never retain a callback into this world.
                const auto* retiringPose = m_Registry.try_get<RuntimePhysicsPose>(e);
                if (retiringPose && retiringPose->handle == handle)
                    m_Registry.remove<RuntimePhysicsPose>(e);
            });
        }
        rollback.complete = true;
        return {};
    }

	void _Scene::OnPhysics3DStop()
	{
        // Clearing runtime poses releases scoped callbacks before body/shape teardown.
		m_PhysicsTiming = {};
		m_TimingWorld = nullptr;
		m_Registry.clear<RuntimePhysicsPose>();
		for (auto& e : m_Registry.view<RigidBody3DComponent>())
		{
			m_Registry.get<RigidBody3DComponent>(e).RuntimeBody = {};
		}

		m_PhysicsSystem->OnExit();
	}
}
