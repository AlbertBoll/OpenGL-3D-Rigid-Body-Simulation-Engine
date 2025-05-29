#pragma once
#include <entt/entt.hpp>
#include <Scene/_Scene.h>
#include <Component/Component.h>
#include <Core/Assert.h>

namespace GEngine
{
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
			return m_Scene->Reg().all_of<Args...>(m_EntityHandle);
		}

		template<typename... Args>
		bool HasAnyComponents() const
		{
			return m_Scene->Reg().any_of<Args...>(m_EntityHandle);
		}

		template<typename T, typename ... Args>
		T& AddComponent(Args&&...args)
		{
			return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
		}

		template<typename T>
		T& GetComponent()
		{
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		template<typename T>
		T GetComponent()const
		{
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		template<typename ... Component>
		void RemoveComponents()
		{
			(m_Scene->m_Registry.remove<Component>(m_EntityHandle), ...);
		}

		template<typename Component>
		void RemoveComponent()
		{
			m_Scene->m_Registry.remove<Component>(m_EntityHandle);
		}

		template<typename T, typename ... Args>
		T& AddOrReplaceComponent(Args&& ... args)
		{
			T& component = m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
			//m_Scene->OnComponentAdded<T>(*this, component);
			return component;
		}


		UUID GetUUID() 
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

		operator bool(){ return m_EntityHandle != entt::null; }

		operator entt::entity() { return m_EntityHandle; }

		operator uint32_t() const { return (uint32_t)m_EntityHandle; }

		operator entt::entity() const { return m_EntityHandle; }

		_Entity GetParent() const
		{
			return m_Scene->GetEntityByUUID(GetParentUUID());
		}

		void SetParent(_Entity parent)
		{
			_Entity currentParent = GetParent();
			if (currentParent == parent)
				return;

			// If changing parent, remove child from existing parent
			if (currentParent)
				currentParent.RemoveChild(*this);

			// Setting to null is okay
			SetParentUUID(parent.GetUUID());

			if (parent)
			{
				auto& parentChildren = parent.Children();
				UUID uuid = GetUUID();
				if (std::find(parentChildren.begin(), parentChildren.end(), uuid) == parentChildren.end())
					parentChildren.emplace_back(GetUUID());
			}
		}


		void SetParentUUID(UUID parent) { GetComponent<RelationshipComponent>().ParentHandle = parent; }

		UUID GetParentUUID() const
		{ 
			return GetComponent<RelationshipComponent>().ParentHandle; 
		}

		std::vector<UUID>& Children() { return GetComponent<RelationshipComponent>().Children; }

		const std::vector<UUID>& Children() const { return GetComponent<RelationshipComponent>().Children; }

		operator bool() const { return  m_EntityHandle != entt::null; }
		_Scene* GetSceneContext()const { return m_Scene; }

		bool RemoveChild(_Entity child)
		{
			UUID childId = child.GetUUID();
			std::vector<UUID>& children = Children();
			auto it = std::find(children.begin(), children.end(), childId);
			if (it != children.end())
			{
				children.erase(it);
				return true;
			}

			return false;
		}

		bool IsAncesterOf(_Entity entity) const
		{
			const auto& children = Children();

			if (children.empty())
				return false;

			for (UUID child : children)
			{
				if (child == entity.GetUUID())
					return true;
			}

			for (UUID child : children)
			{
				if (m_Scene->GetEntityByUUID(child).IsAncesterOf(entity))
					return true;
			}

			return false;
		}

		bool IsDescendantOf(_Entity entity) const { return entity.IsAncesterOf(*this); }


		Transform3DComponent& Transform() { return m_Scene->m_Registry.get<Transform3DComponent>(m_EntityHandle); }
		const Mat4& Transform() const { return m_Scene->m_Registry.get<TransformComponent>(m_EntityHandle).GetTransform(); }

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
