#pragma once
#include <entt/entt.hpp>
#include <Scene/_Scene.h>
#include <Component/Component.h>
#include <Core/Assert.h>
#include <expected>
#include <functional>
#include <string_view>

namespace GEngine
{
    struct EntityChildrenError
    {
        TransformErrorCode code = TransformErrorCode::InvalidEntity;
        std::string_view operation = "_Entity::Children";
        std::string_view message = "Children requires a live scene entity";
    };

	class _Entity
	{
	public:
		_Entity() = default;
		_Entity(entt::entity handle, _Scene* scene);
		~_Entity();
		_Entity(const _Entity& other) = default;

		template<typename... Args>
		bool HasAllComponents() const
		{
			return *this && m_Scene->Reg().all_of<Args...>(m_EntityHandle);
		}

		template<typename... Args>
		bool HasAnyComponents() const
		{
			return *this && m_Scene->Reg().any_of<Args...>(m_EntityHandle);
		}

		template<typename T, typename ... Args>
		T& AddComponent(Args&&...args)
		{
			m_Scene->RenderData().RequireMutable();
			return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
		}

		template<typename T>
		T& GetComponent()
		{
			m_Scene->RenderData().RequireMutable();
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		template<typename T>
		const T& GetComponent()const
		{
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		template<typename ... Component>
		void RemoveComponents()
		{
			m_Scene->RenderData().RequireMutable();
			(m_Scene->m_Registry.remove<Component>(m_EntityHandle), ...);
		}

		template<typename Component>
		void RemoveComponent()
		{
			m_Scene->RenderData().RequireMutable();
			m_Scene->m_Registry.remove<Component>(m_EntityHandle);
		}

		template<typename T, typename ... Args>
		T& AddOrReplaceComponent(Args&& ... args)
		{
			m_Scene->RenderData().RequireMutable();
			T& component = m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
			//m_Scene->OnComponentAdded<T>(*this, component);
			return component;
		}


		UUID GetUUID() const
		{
			return GetComponent<Component::IDComponent>().ID; 
		}

		bool operator!=(const _Entity& other) const
		{
			return !(*this == other);
		}

		bool operator==(const _Entity& other) const
		{
			return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
		}

		const std::string& GetName() { return GetComponent<Component::TagComponent>().Name; }

		operator entt::entity() { return m_EntityHandle; }

		operator uint32_t() const { return (uint32_t)m_EntityHandle; }

		operator entt::entity() const { return m_EntityHandle; }

		_Entity GetParent() const
		{
			if (!HasAllComponents<RelationshipComponent>()) return {};
			const auto& link = GetComponent<RelationshipComponent>();
			auto parent = m_Scene->RenderData().Resolve(link.ParentIdentity);
			if (!parent) return {};
			_Entity result(*parent, m_Scene);
			return result.HasAllComponents<IDComponent>() && result.GetUUID() == link.ParentHandle ? result : _Entity{};
		}

		// Null detaches. Retains authored TRS; validates the full parent chain before
		// relinking. Typed failures leave both sides unchanged.
		std::expected<void, TransformError> SetParent(_Entity parent);
		std::expected<void, TransformError> SetParentUUID(UUID parent);

		UUID GetParentUUID() const
		{ 
			return HasAllComponents<RelationshipComponent>() ? GetComponent<RelationshipComponent>().ParentHandle : UUID(0);
		}

		// The successful reference borrows the existing Scene-owned relationship vector.
		[[nodiscard]] std::expected<std::reference_wrapper<std::vector<UUID>>, EntityChildrenError> Children();

		const std::vector<UUID>& Children() const;

		// Handles borrow their scene and must not outlive it. EnTT checks generation reuse.
		operator bool() const { return m_Scene && m_Scene->Reg().valid(m_EntityHandle); }
		_Scene* GetSceneContext()const { return m_Scene; }

		bool RemoveChild(_Entity child);
		bool IsAncesterOf(_Entity entity) const;

		bool IsDescendantOf(_Entity entity) const { return entity.IsAncesterOf(*this); }


		Transform3DComponent& Transform() { m_Scene->RenderData().RequireMutable(); return m_Scene->m_Registry.get<Transform3DComponent>(m_EntityHandle); }
		Mat4 Transform() const { return GetComponent<Transform3DComponent>().GetTransform(); }

		std::string& Name() { return HasAllComponents<TagComponent>() ? GetComponent<TagComponent>().Name : NoName; }
		const std::string& Name() const { return HasAllComponents<TagComponent>() ? GetComponent<TagComponent>().Name : NoName; }


		/*void SetParent(_Entity* parent)
		{
			m_ParentEntity = parent;
		}*/
		//_Entity* GetParent() { return m_ParentEntity; }

		//auto& GetChildrenEntitiesList() { return m_ChildEntities; }
		//bool IsChildListEmpty()const { return m_ChildEntities.empty(); }
		//bool IsRoot()const { return m_ParentEntity == nullptr; }


	private:
		entt::entity m_EntityHandle{entt::null};
		_Scene* m_Scene{};
		inline static std::string NoName = "Unnamed";
		//std::vector<_Entity>m_ChildEntities;
		//_Entity* m_ParentEntity{};
	};

}
