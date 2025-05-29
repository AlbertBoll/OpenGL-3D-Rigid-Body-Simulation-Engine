#include "gepch.h"
#include "Scene/_Entity.h"
#include <Scene/_Scene.h>

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

}