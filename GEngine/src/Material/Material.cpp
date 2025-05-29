#include "gepch.h"
#include <Assets/Shaders/Shader.h>
#include "Material/Material.h"
#include "Managers/ShaderManager.h"
#include <Core/Actor.h>


namespace GEngine
{
	using namespace Manager;
	Material::Material(const std::string& vertexFileName, const std::string& fragFileName)
	{
		m_Shader = ShaderManager::GetShaderProgram({ vertexFileName, fragFileName });

		RenderSetting setting;

		SetRenderSettings(setting);

	}

	unsigned int Material::GetShaderRef() const
	{
		return m_Shader->GetHandle();
	}

	void Material::UseProgram() const
	{
		m_Shader->Bind();
	}

	void Material::BindTextureUniforms(int TexTarget)
	{
		for (auto& ele : m_TextureList)
		{
			m_Shader->BindTextureUniform(ele.second.first, ele.second.second, TexTarget);
			
		}
	}

	void Material::BindTextureUniforms()
	{
		for (auto& ele : m_TextureList)
		{
			m_Shader->BindTextureUniform(ele.second.first, ele.second.second, ele.first);
		}
	}

	Material::Material(Material&& other)noexcept
	{
		m_RenderSetting = other.m_RenderSetting;
		m_Shader = std::move(other.m_Shader);
		
		other.m_Shader = nullptr;
	}
	Material& Material::operator=(Material&& other) noexcept
	{
		if (this != &other)
		{
			m_Shader = other.m_Shader;
			other.m_Shader = nullptr;
			m_RenderSetting = other.m_RenderSetting;
		}

		return *this;
	}



}