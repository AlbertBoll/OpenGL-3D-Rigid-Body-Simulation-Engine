#include "gepch.h"
#include "Material/TextureMaterial.h"
#include"Assets/Textures/Texture.h"

namespace GEngine
{
	TextureMaterial::TextureMaterial(const Asset::Texture& texture, 
									 const std::string& vertexFileName, 
									 const std::string& fragFileName): Material(vertexFileName, fragFileName)
	{
		RenderSetting setting;
		setting.m_Mode = DrawMode::TRIANGLES;
		setting.m_RenderMode = RenderMode::Arrays;
		setting.m_PrimitivesSetting.surfaceSetting.lineWidth = 1.0f;
		setting.m_PrimitivesSetting.surfaceSetting.bDoubleSide = true;
		setting.m_PrimitivesSetting.surfaceSetting.bWireFrame = false;
		SetRenderSettings(setting);

		UseProgram();
		SetUniforms<std::pair<GLuint, std::pair<GLuint, GLuint>>>({ {"u_texture", {m_RenderSetting.m_TexTarget, {texture.GetTextureID(), 1}} } });
		SetUniforms<Vec3f>({ {"u_baseColor", {1.0f, 1.0f, 1.0f}} });
		
		UpdateRenderSettings();

	}

	void TextureMaterial::UpdateRenderSettings()
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


}