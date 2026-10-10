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

    std::expected<SceneAssignmentChange, SceneAssignmentError> _Entity::AssignRenderable(
        const Component::MeshRendererComponent& value, const RenderStateResources& resources)
    {
        if (!m_Scene)
            return std::unexpected(SceneAssignmentError::InvalidEntity);
        return m_Scene->AssignRenderable(*this, value, resources);
    }

    std::expected<_Entity, SceneError> _Entity::Duplicate() const
    {
        if (!m_Scene)
            return std::unexpected(SceneError{SceneErrorCode::InvalidScene, "_Entity::Duplicate",
                "Duplicate requires a scene"});
        return m_Scene->DuplicateEntity(*this);
    }

    std::expected<void, TransformError> _Entity::SetParent(_Entity parent)
    {
        if (!m_Scene)
            return std::unexpected(TransformError{TransformErrorCode::InvalidEntity});
        auto changed = m_Scene->SetParent(*this, parent);
        if (!changed)
        {
            // Preserve the legacy hierarchy error vocabulary; the Scene API additionally
            // reports extraction/reentrant mutation rejection explicitly.
            auto code = TransformErrorCode::InvalidEntity;
            switch (changed.error().code)
            {
            case TransformMutationErrorCode::ForeignEntity:
                code = TransformErrorCode::ForeignEntity;
                break;
            case TransformMutationErrorCode::MissingTransform:
                code = TransformErrorCode::MissingTransform;
                break;
            case TransformMutationErrorCode::InvalidParent:
                code = TransformErrorCode::InvalidParent;
                break;
            case TransformMutationErrorCode::Cycle:
                code = TransformErrorCode::Cycle;
                break;
            case TransformMutationErrorCode::IdentityExhausted:
                code = TransformErrorCode::IdentityExhausted;
                break;
            default:
                break;
            }
            return std::unexpected(
                TransformError{code, changed.error().entity, changed.error().parent});
        }
        // This compatibility signature cannot carry post-commit delivery diagnostics.
        if (!changed->notification)
            ReportSubscriptionError(changed->notification.error());
        return {};
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
