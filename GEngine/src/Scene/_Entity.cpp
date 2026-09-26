#include "gepch.h"
#include "Scene/_Entity.h"
#include <Scene/_Scene.h>
#include <algorithm>
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

	std::expected<void, TransformError> _Entity::SetParent(_Entity parent)
	{
		if (!HasAllComponents<IDComponent>())
			return std::unexpected(TransformError{TransformErrorCode::InvalidEntity});
		const auto id = GetUUID();
		const bool detach = parent.m_EntityHandle == entt::null;
		if (!detach && parent.m_Scene != m_Scene)
			return std::unexpected(TransformError{TransformErrorCode::ForeignEntity, id});
		if (!detach && !parent.HasAllComponents<IDComponent>())
			return std::unexpected(TransformError{TransformErrorCode::InvalidParent, id});

		// Validate the complete parent chain before changing either side of the link.
		std::unordered_set<UUID> visited;
		for (auto ancestor = detach ? _Entity{} : parent; ancestor;)
		{
			if (ancestor == *this || !visited.insert(ancestor.GetUUID()).second)
				return std::unexpected(TransformError{TransformErrorCode::Cycle, id, parent.GetUUID()});
			const auto next = ancestor.GetParentUUID();
			ancestor = ancestor.GetParent();
			if (next != 0 && !ancestor)
				return std::unexpected(TransformError{TransformErrorCode::InvalidParent, id, next});
		}

		const UUID parentId = detach ? UUID(0) : parent.GetUUID();
		EntityRenderId parentIdentity{};
		if (!detach)
		{
			auto identity = m_Scene->RenderData().Identify(parent.m_EntityHandle);
			if (!identity) return std::unexpected(TransformError{TransformErrorCode::IdentityExhausted, id, parentId});
			parentIdentity = *identity;
		}
		if (GetParentUUID() == parentId && (detach || GetParent() == parent)) return {};
		auto currentParent = GetParent();
		(void)m_Scene->Reg().get_or_emplace<RelationshipComponent>(m_EntityHandle);
		if (!detach)
		{
			auto& children = m_Scene->Reg().get_or_emplace<RelationshipComponent>(parent.m_EntityHandle).Children;
			if (std::find(children.begin(), children.end(), id) == children.end())
				children.push_back(id);
		}
		if (currentParent)
			if (auto* link = m_Scene->Reg().try_get<RelationshipComponent>(currentParent.m_EntityHandle))
				std::erase(link->Children, id);
		// Component insertion can relocate storage: reacquire after parent.Children().
		auto& relationship = GetComponent<RelationshipComponent>();
		relationship.ParentHandle = parentId;
		relationship.ParentIdentity = parentIdentity;
		return m_Scene->ResetRenderInterpolation(*this);
	}

	std::expected<void, TransformError> _Entity::SetParentUUID(UUID parent)
	{
		if (!HasAllComponents<IDComponent>())
			return std::unexpected(TransformError{TransformErrorCode::InvalidEntity});
		auto resolved = m_Scene->GetEntityByUUID(parent);
		if (parent != 0 && !resolved)
			return std::unexpected(TransformError{TransformErrorCode::InvalidParent, GetUUID(), parent});
		return SetParent(resolved);
	}

	std::expected<std::reference_wrapper<std::vector<UUID>>, EntityChildrenError> _Entity::Children()
	{
		if (!*this)
			return std::unexpected(EntityChildrenError{});
		return std::ref(m_Scene->Reg().get_or_emplace<RelationshipComponent>(m_EntityHandle).Children);
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
		auto& children = GetComponent<RelationshipComponent>().Children; // Existing precheck proves the component.
		if (std::find(children.begin(), children.end(), child.GetUUID()) == children.end())
			return false;
		if (child.GetParentUUID() == GetUUID())
			(void)child.SetParent({}); // Validated live child, null parent: cannot fail.
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
