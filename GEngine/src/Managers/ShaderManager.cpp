#include "gepch.h"
#include "Managers/ShaderManager.h"

namespace GEngine::Manager
{
	Shader* ShaderManager::GetShaderProgram(const Files& shader_file)
	{
		{
			if (auto it = m_ShaderMap.find(shader_file); it != m_ShaderMap.end())
			{
				return it->second;
			}

			Shader* new_shader = new(std::nothrow) Shader;
			ASSERT(new_shader);

			for (auto& str : shader_file.keys_filepath)
			{
				new_shader->CompileShader(str.c_str());
			}

			new_shader->Link();
			new_shader->Validate();
			new_shader->FindUniformLocations();

			m_ShaderMap.emplace(shader_file, new_shader);
			//m_ShaderMap.insert({ shader_file, new_shader });

			return new_shader;



		}
	}

	void ShaderManager::FreeShader()
	{
		
		for (auto& ele : m_ShaderMap)
		{
			if(ele.second) delete ele.second;
		}
		m_ShaderMap.clear();
	}
}
