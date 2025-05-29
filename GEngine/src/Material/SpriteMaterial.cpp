#include "gepch.h"
#include "Material/SpriteMaterial.h"
#include "Assets/Textures/Texture.h"

namespace GEngine
{
	SpriteMaterial::SpriteMaterial(Asset::Texture* texture, const std::string& shaderName)
		: Material(base_shader_dir + shaderName + ".vert", base_shader_dir + shaderName + ".frag")
	{
		RenderSetting setting;
		setting.m_Mode = DrawMode::TRIANGLES;
		setting.m_RenderMode = RenderMode::Arrays;
		setting.m_PrimitivesSetting.surfaceSetting.lineWidth = 1.0f;
		setting.m_PrimitivesSetting.surfaceSetting.bDoubleSide = true;
		setting.m_PrimitivesSetting.surfaceSetting.bWireFrame = false;
		SetRenderSettings(setting);
		SetUniforms<std::pair<GLuint, std::pair<GLuint, GLuint>>>({ {"u_texture", {m_RenderSetting.m_TexTarget, {texture->GetTextureID(), 1}} } });
		UseProgram();
		UpdateRenderSettings();
	}

	void SpriteMaterial::UpdateRenderSettings()
	{
		if (m_RenderSetting.m_PrimitivesSetting.surfaceSetting.bDoubleSide && GetMaterialProperty().HasTransparency)
		{
			glDisable(GL_CULL_FACE);
		}

		else
		{
			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
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
