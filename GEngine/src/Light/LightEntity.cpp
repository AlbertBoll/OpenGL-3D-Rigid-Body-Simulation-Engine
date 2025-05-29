#include "gepch.h"
#include "Light/LightEntity.h"
#include "Core/Entity.h"

namespace GEngine
{
	//LightEntity::LightEntity(Asset::Shader* shader, const LightComponent& light): Actor(), m_Light{light}, m_Shader(shader)
	//{
		

	//}

	LightEntity::LightEntity() :Actor()
	{
		static int Index = 0;
		m_LightIndex = Index;
		Index++;
	}

	LightEntity::LightEntity(const LightComponent& light) : Actor(), m_Light{ light }
	{
		static int Index = 0;
		m_LightIndex = Index;
		SetPos(light.UniformName)->
		SetColor(light.Color.Name, light.Color.Data.x, light.Color.Data.y, light.Color.Data.z)->
		SetAttenuation(light.Attenuation.Name, light.Attenuation.Data.x, light.Attenuation.Data.y, light.Attenuation.Data.z);
		

		Index++;
	}

	void LightEntity::UploadLightUniform(Material* mat)
	{
		mat->SetUniforms<Vec3f>({ {m_Light.UniformName, GetWorldPosition()},
								  {m_Light.Color.Name, m_Light.Color.Data},
								  {m_Light.Attenuation.Name, m_Light.Attenuation.Data} });
	}


	/*void LightEntity::UploadLightUniform()
	{
		auto mat = m_Owner->GetMaterial();
		mat->SetUniforms<Vec3f>({ {m_Light.UniformName, GetWorldPosition()},
								  {m_Light.Color.Name, m_Light.Color.Data},
								  {m_Light.Attenuation.Name, m_Light.Attenuation.Data} });
	} */

}
