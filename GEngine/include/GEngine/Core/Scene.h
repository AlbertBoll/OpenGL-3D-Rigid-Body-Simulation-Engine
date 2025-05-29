#pragma once
#include "Actor.h"
#include "Scene.h"
#include "Entity.h"
#include "Group.h"

namespace GEngine
{
	//class CameraBase;
	class LightEntity;

	class Scene : public Actor
	{
		friend class SceneHiarchyPanel;

	public:
		Scene(): Actor(){}
		~Scene()override;
		Scene(const std::string& name): Actor(name){}

		void Push(Group<Entity>* group_entity)
		{
			
			m_ProgramIDLookUp[group_entity->GetProgramID()].push_back(group_entity);

		}

		void Push(LightEntity* entity);
	

		void Push(const std::vector<Group<Entity>*>& group)
		{
			for (auto& p : group)
			{
				Push(p);
			}
		}
		
		void Push(const std::vector<LightEntity*>& lights)
		{
			for (auto p : lights)
			{
				Push(p);
			}
		}

		void Render(CameraBase* camera) override;
		
		
	private:
		void UploadLightsUniform(Material* mat)const;
		

	
	private:
		std::unordered_map<unsigned int, std::vector<Group<Entity>*>> m_ProgramIDLookUp;
		std::vector<LightEntity*> m_Lights;

	};
}
