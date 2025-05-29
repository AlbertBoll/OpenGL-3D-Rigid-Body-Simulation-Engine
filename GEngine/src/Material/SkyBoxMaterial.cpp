#include "gepch.h"
#include "Material/SkyBoxMaterial.h"
#include "Assets/Textures/Texture.h"

namespace GEngine
{
	SkyBoxMaterial::SkyBoxMaterial(const Asset::Texture& texture,
		const std::string& vertexFileName, 
		const std::string& fragFileName): Material(vertexFileName, fragFileName)
	{
		RenderSetting setting;
		setting.m_Mode = DrawMode::TRIANGLES;
		setting.m_RenderMode = RenderMode::Arrays;
		setting.m_PrimitivesSetting.surfaceSetting.lineWidth = 1.0f;
		setting.m_PrimitivesSetting.surfaceSetting.bDoubleSide = true;
		setting.m_PrimitivesSetting.surfaceSetting.bWireFrame = false;
		setting.m_TexTarget = GL_TEXTURE_CUBE_MAP;
		SetRenderSettings(setting);

		UseProgram();
		SetUniforms<std::pair<unsigned int, std::pair<unsigned int, unsigned int>>>({ {texture.GetUniformName(), {m_RenderSetting.m_TexTarget, {texture.GetTextureID(), 1}} } });
	}

	SkyBoxMaterial::SkyBoxMaterial(const std::vector<Asset::Texture*>& textures,
		const std::string& vertexFileName,
		const std::string& fragFileName): Material(vertexFileName, fragFileName)
	{
		RenderSetting setting;
		setting.m_Mode = DrawMode::TRIANGLES;
		setting.m_RenderMode = RenderMode::Arrays;
		setting.m_PrimitivesSetting.surfaceSetting.lineWidth = 1.0f;
		setting.m_PrimitivesSetting.surfaceSetting.bDoubleSide = true;
		setting.m_PrimitivesSetting.surfaceSetting.bWireFrame = false;
		setting.m_TexTarget = GL_TEXTURE_CUBE_MAP;
		SetRenderSettings(setting);

		UseProgram();
		static int i = 1;
		for(auto& texture: textures)
			SetUniforms<std::pair<unsigned int, std::pair<unsigned int, unsigned int>>>({ {(*texture).GetUniformName(), {m_RenderSetting.m_TexTarget, {(*texture).GetTextureID(), i++}}}});

	}

	void SkyBoxMaterial::UpdateRenderSettings()
	{
		if (m_RenderSetting.m_PrimitivesSetting.surfaceSetting.bDoubleSide)
		{
			glDisable(GL_CULL_FACE);
		}

		else
		{
			glEnable(GL_CULL_FACE);
		}

		if (m_RenderSetting.m_PrimitivesSetting.surfaceSetting.bWireFrame)
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}

		else
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}

		glLineWidth(m_RenderSetting.m_PrimitivesSetting.surfaceSetting.lineWidth);
	}

	void SkyBoxMaterial::UploadUniforms()
	{
		SetUniforms<Vec3f>({{m_SkyBoxComp.FogAmbientColor.Name, m_SkyBoxComp.FogAmbientColor.Data} });
		SetUniforms<float>({{m_SkyBoxComp.LowerLimit.Name,      m_SkyBoxComp.LowerLimit.Data},
							{m_SkyBoxComp.UpperLimit.Name,      m_SkyBoxComp.UpperLimit.Data},
						    {m_SkyBoxComp.BlendFactor.Name,     m_SkyBoxComp.BlendFactor.Data} });
						  
	}
	
}
