#pragma once
#include "Core/Actor.h"

namespace GEngine
{

	//class Entity;
	class Material;

	class LightEntity: public Actor
	{

	public:
		LightEntity();
		//LightEntity(Entity* owner);
		LightEntity(const LightComponent& light);

		
		LightEntity* SetColor(const std::string& name, float r, float g, float b)
		{
			m_Light.Color.Name = name + "[" + std::to_string(m_LightIndex) + "]";
			m_Light.Color.Data = { r, g, b };
			return this;
		}

	
		LightEntity* SetColor(const std::string& name, const Vec3f& color)
		{
			return SetColor(name, color.r, color.g, color.b);
		}


	
		LightEntity* SetAttenuation(const std::string& name, float a, float b, float c)
		{
			m_Light.Attenuation.Name = name + "["+std::to_string(m_LightIndex)+"]";
			m_Light.Attenuation.Data = { a, b, c };
			return this;
		}

	
		LightEntity* SetAttenuation(const std::string& name, const Vec3f& attenuation)
		{
			return SetAttenuation(name, attenuation.x, attenuation.y, attenuation.z);
		}

		
		LightEntity* SetPos(const std::string& name)
		{
			m_Light.UniformName = name + "[" + std::to_string(m_LightIndex) + "]";;
		
			return this;
		}


		void UploadLightUniform(Material* mat);

	private:
		LightComponent m_Light;
		//Entity* m_Owner{};
		int m_LightIndex{};
		//std::vector<Asset::Shader> m_Shaders;
	};
}


