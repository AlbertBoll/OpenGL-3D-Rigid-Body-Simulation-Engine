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
			RigidBodyIdentity identity;
			RigidBody3D* body{};
            std::optional<Delegate<void(const Vec3f&)>> scaleConnection;
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
            if (auto* body = destination.try_get<Component::RigidBody3DComponent>(entity)) body->RuntimeBody = nullptr;
            if (sourceRuntime && sourceRuntime->scaleConnection)
                if (auto* transform = destination.try_get<Component::Transform3DComponent>(entity))
                    transform->OnScaleChanged.Disconnect(*sourceRuntime->scaleConnection);
        }

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

	std::expected<RefPtr<_Scene>, SceneError> _Scene::Copy(RefPtr<_Scene> other)
	{
        if (!other) return std::unexpected(SceneError{SceneErrorCode::InvalidScene, "_Scene::Copy",
            "Scene copy requires a source scene"});
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
            auto created = newScene->CreateEntityWithUUID(uuid, name);
            if (!created) return std::unexpected(created.error());
            enttMap[uuid] = static_cast<entt::entity>(*created);
		}

		// Copy components (except IDComponent and TagComponent)
		CopyComponent(AllComponents{}, dstSceneRegistry, srcSceneRegistry, enttMap);
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
                const auto* body = rigidBody->RuntimeBody;
                input.translation = body->m_Position;
                input.rotation = body->m_Orientation;
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
    Mat4 _Scene::EvaluateRenderPresentation(const RenderPresentationInput& input)
    {
        auto translation = input.translation;
        auto rotation = input.rotation;
        if (input.interpolate)
        {
            translation = glm::mix(input.previousTranslation, input.currentTranslation, input.alpha);
            rotation = glm::normalize(glm::slerp(input.previousRotation, input.currentRotation, input.alpha));
        }
        return glm::translate(Mat4(1.0f), translation) * glm::toMat4(glm::normalize(rotation))
            * glm::scale(Mat4(1.0f), input.scale);
    }
    Mat4 _Scene::SampleRenderMatrix(entt::entity handle) const
    {
        return EvaluateRenderPresentation(ObserveRenderPresentation(handle));
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
			ResetPhysicsHistory(*pose, *transform, *rigidBody->RuntimeBody, entity.GetParentUUID());
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

    std::expected<_Entity, SceneError> _Scene::DuplicateEntity(_Entity entity)
    {
        m_RenderData.RequireMutable();
        if (entity.GetSceneContext() != this || !m_Registry.valid(entity))
            return std::unexpected(SceneError{SceneErrorCode::ForeignEntity, "_Scene::DuplicateEntity",
                "Duplicate requires a live entity in this scene"});
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

		// Detach while parents are still alive, then retire descendants before parents.
		// The explicit worklist also makes destruction safe for very deep hierarchies.
		std::vector<_Entity> pending{entity}, retiring;
		std::unordered_set<entt::entity> visited;
		while (!pending.empty())
		{
			auto current = pending.back();
			pending.pop_back();
			if (!visited.insert(static_cast<entt::entity>(current)).second) continue;
			const auto children = std::as_const(current).Children();
			(void)current.SetParent({}); // Live ID and null parent: cannot fail.
			for (const auto childId : children)
			{
				auto child = GetEntityByUUID(childId);
				if (child && child.GetParent() == current)
				{
					if (excludeChildren)
						(void)child.SetParent({});
					else
						pending.push_back(child);
				}
			}
			retiring.push_back(current);
		}
		for (auto it = retiring.rbegin(); it != retiring.rend(); ++it)
		{
			const auto current = *it;
			const auto id = current.GetUUID();
			RemoveFromRenderLists(current);
			if (current.HasAllComponents<RigidBody3DComponent>())
			{
				auto* body = current.GetComponent<RigidBody3DComponent>().RuntimeBody;
				auto* world = m_PhysicsSystem->GetPhysicsWorld();
				if (world && body) world->RemoveRigidBody3D(body);
			}
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
        struct PendingShape
        {
            entt::entity entity;
            std::unique_ptr<PhysicalShape> shape;
            std::optional<Delegate<void(const Vec3f&)>> connection;
        };
        std::vector<PendingShape> pending;
        struct Rollback
        {
            _Scene& scene;
            std::vector<PendingShape>& pending;
            bool complete = false;
            ~Rollback()
            {
                if (complete) return;
                // Startup does not remove authoring entities; only these new slots are ours.
                for (auto& owner : pending)
                    if (owner.connection)
                        scene.m_Registry.get<Transform3DComponent>(owner.entity).OnScaleChanged.Disconnect(*owner.connection);
                scene.m_IsRunning = false;
                scene.OnPhysics3DStop(); // Body borrowers retire before pending shape owners.
            }
        } rollback{*this, pending};
        auto m_PhysicsWorld = new (std::nothrow) PhysicsWorld{};
        if (!m_PhysicsWorld)
            return std::unexpected(PhysicsShapeError{PhysicsShapeErrorCode::Allocation,
                "_Scene::OnPhysics3DStart", "Physics world allocation failed"});
		m_PhysicsSystem->SetPhysicsWorld(m_PhysicsWorld);
		//auto view = m_Registry.view<RigidBody3DComponent>();
		for (auto& e : m_Registry.view<RigidBody3DComponent>())
		{
			_Entity entity{ e, this };
			auto& transform = entity.GetComponent<Transform3DComponent>();
			auto& rigid_body = entity.GetComponent<RigidBody3DComponent>();
            std::unique_ptr<PhysicalShape> shape;
            auto failure = [&](PhysicsShapeError error) -> std::expected<void, PhysicsShapeError>
            { error.entity = entity.GetUUID(); return std::unexpected(error); };

			if (entity.HasAllComponents<SphereFixture3DComponent>())
			{
				auto& sphere_fixure = entity.GetComponent<SphereFixture3DComponent>();
                auto created = ShapeSphere::Create(sphere_fixure.Radius);
                if (!created) return failure(created.error());
                shape.reset(new (std::nothrow) ShapeSphere(std::move(*created)));
                if (!shape) return failure(PhysicsShapeError{PhysicsShapeErrorCode::Allocation,
                    "_Scene::OnPhysics3DStart", "Sphere shape allocation failed", sphere_fixure.Radius});
				RigidBody3D* body = m_PhysicsWorld->CreateRigidBody3D();

				body->m_LinearVelocity = sphere_fixure.Property.m_LinearVelocity;//rigid_body.Property.m_LinearVelocity;
				body->m_AngularVelocity = sphere_fixure.Property.m_AngularVelocity;
				body->m_InvMass = sphere_fixure.Property.m_InvMass;
				body->m_Elasticity = sphere_fixure.Property.m_Elasticity;
				body->m_Friction = sphere_fixure.Property.m_Friction;
				body->Type = rigid_body.Type;
				body->m_CollisionLayer = rigid_body.CollisionLayer;
				body->m_CollisionMask = rigid_body.CollisionMask;
				body->m_Shape = shape.get();

				
				
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
                auto created = ShapeBox::Create(pts);
                if (!created) return failure(created.error());
                shape.reset(new (std::nothrow) ShapeBox(std::move(*created)));
                if (!shape) return failure(PhysicsShapeError{PhysicsShapeErrorCode::Allocation,
                    "_Scene::OnPhysics3DStart", "Box shape allocation failed", 0.0f, pts.size()});
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
				body->m_Shape = shape.get();


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
                shape.reset(new (std::nothrow) ShapeConvex(pts));
                if (!shape) return failure(PhysicsShapeError{PhysicsShapeErrorCode::Allocation,
                    "_Scene::OnPhysics3DStart", "Convex shape allocation failed", 0.0f, pts.size()});
                body->m_Shape = shape.get();

				
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
					body->m_Shape = nullptr; // Local owner retires the rejected shape.
					m_PhysicsWorld->RemoveRigidBody3D(body);
					rigid_body.RuntimeBody = nullptr;
					continue;
				}
				auto& pose = m_Registry.emplace_or_replace<RuntimePhysicsPose>(e);
				pose.identity = body->GetIdentity();
				pose.body = body;
				PublishPhysicsPose(transform, pose, *body);
				ResetPhysicsHistory(pose, transform, *body, entity.GetParentUUID());
                pending.push_back(PendingShape{e, std::move(shape), {}});
                pending.back().connection.emplace(Connection(transform, OnScaleChanged, *body->m_Shape, &PhysicalShape::HandleScaleChanged));
                pose.scaleConnection = pending.back().connection;
			}
		}
        for (auto& owner : pending) owner.shape.release(); // Existing successful caller handoff.
        rollback.complete = true;
        return {};
    }

	void _Scene::OnPhysics3DStop()
	{
        for (auto entity : m_Registry.view<RuntimePhysicsPose, Transform3DComponent>())
            if (const auto& connection = m_Registry.get<RuntimePhysicsPose>(entity).scaleConnection; connection)
                m_Registry.get<Transform3DComponent>(entity).OnScaleChanged.Disconnect(*connection);
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
