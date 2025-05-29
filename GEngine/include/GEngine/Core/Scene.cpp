#include "gepch.h"
#include "Scene.h"
#include "Renderer.h"
#include "Light/LightEntity.h"

GEngine::Scene::~Scene()
{
	for (auto& [id, groups] : m_ProgramIDLookUp)
	{
		for (auto group : groups)
		{
			delete group;
		}
	}

	//for (auto& light : m_Lights)
	//{
	//	if (light)
	//	{
	//		delete light;
	//		light = nullptr;
	//	}
	//}
}

void GEngine::Scene::Push(LightEntity* entity)
{
	m_Lights.push_back(entity);
}

void GEngine::Scene::Render(CameraBase* camera)
{
	//first render groups
	for (auto& [id, groups] : m_ProgramIDLookUp)
	{

		glUseProgram(id);

		auto groupMaterial = groups[0]->GetMaterial();

		UploadLightsUniform(groupMaterial);

		groupMaterial->SetUniforms<Mat4>({ {"u_view", camera->GetView()},
										   {"u_projection", camera->GetProjection()} });

		groupMaterial->SetUniforms<Vec3f>({ {"u_cameraPosition", camera->GetWorldPosition()} });
	
		for (auto& group : groups)
		{
			group->Render(camera);
		}

	}

	//render the single entity;
	Renderer::RenderHelper(this, camera);
}

void GEngine::Scene::UploadLightsUniform(Material* mat) const
{
	for (auto light : m_Lights)
	{
		light->UploadLightUniform(mat);
	}
}

//void GEngine::Scene::UploadLightsUniform() const
//{
//	for (auto light : m_Lights)
//	{
//		light->UploadLightUniform();
//	}
//}
