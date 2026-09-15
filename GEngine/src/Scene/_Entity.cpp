#include "gepch.h"
#include "Scene/_Entity.h"
#include <Scene/_Scene.h>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace GEngine
{

	_Entity::~_Entity()
	{
	/*	while (!m_ChildEntities.empty())
		{
			for (auto& child : m_ChildEntities)
			{

			}
		}*/
	}


	_Entity::_Entity(entt::entity handle, _Scene* scene) : m_EntityHandle(handle), m_Scene(scene)
	{

	}

	void _Entity::SetParent(_Entity parent)
	{
		if (!HasAllComponents<IDComponent>())
			throw std::invalid_argument("Parenting requires a live scene entity");
		const bool detach = parent.m_EntityHandle == entt::null;
		if (!detach && (parent.m_Scene != m_Scene || !parent.HasAllComponents<IDComponent>()))
			throw std::invalid_argument("Parent must be a live entity in the same scene");

		// Validate the complete parent chain before changing either side of the link.
		std::unordered_set<UUID> visited;
		for (auto ancestor = detach ? _Entity{} : parent; ancestor;)
		{
			if (ancestor == *this || !visited.insert(ancestor.GetUUID()).second)
				throw std::invalid_argument("Entity parenting cannot create or join a cycle");
			const auto next = ancestor.GetParentUUID();
			ancestor = ancestor.GetParent();
			if (next != 0 && !ancestor)
				throw std::invalid_argument("Parent chain contains an invalid entity");
		}

		const UUID parentId = detach ? UUID(0) : parent.GetUUID();
		if (GetParentUUID() == parentId)
			return;
		const auto id = GetUUID();
		auto currentParent = GetParent();
		(void)m_Scene->Reg().get_or_emplace<RelationshipComponent>(m_EntityHandle);
		if (!detach)
		{
			auto& children = parent.Children();
			if (std::find(children.begin(), children.end(), id) == children.end())
				children.push_back(id);
		}
		if (currentParent)
			std::erase(currentParent.Children(), id);
		// Component insertion can relocate storage: reacquire after parent.Children().
		GetComponent<RelationshipComponent>().ParentHandle = parentId;
	}

	void _Entity::SetParentUUID(UUID parent)
	{
		if (!*this)
			throw std::invalid_argument("Parenting requires a live scene entity");
		auto resolved = m_Scene->GetEntityByUUID(parent);
		if (parent != 0 && !resolved)
			throw std::invalid_argument("Parent UUID is not a live scene entity");
		SetParent(resolved);
	}

	std::vector<UUID>& _Entity::Children()
	{
		if (!*this)
			throw std::invalid_argument("Children requires a live scene entity");
		return m_Scene->Reg().get_or_emplace<RelationshipComponent>(m_EntityHandle).Children;
	}

	const std::vector<UUID>& _Entity::Children() const
	{
		static const std::vector<UUID> empty;
		return HasAllComponents<RelationshipComponent>() ? GetComponent<RelationshipComponent>().Children : empty;
	}

	bool _Entity::RemoveChild(_Entity child)
	{
		if (!HasAllComponents<IDComponent, RelationshipComponent>() || child.m_Scene != m_Scene
			|| !child.HasAllComponents<IDComponent>())
			return false;
		auto& children = Children();
		if (std::find(children.begin(), children.end(), child.GetUUID()) == children.end())
			return false;
		if (child.GetParentUUID() == GetUUID())
			child.SetParent({});
		else
			std::erase(children, child.GetUUID());
		return true;
	}

	bool _Entity::IsAncesterOf(_Entity entity) const
	{
		if (!*this || !entity || m_Scene != entity.m_Scene || *this == entity)
			return false;
		std::unordered_set<UUID> visited;
		for (auto parent = entity.GetParent(); parent; parent = parent.GetParent())
		{
			if (parent == *this)
				return true;
			if (!visited.insert(parent.GetUUID()).second)
				break;
		}
		return false;
	}

}
