#include "gepch.h"
#include "Material/LightTextureMaterial.h"
#include"Assets/Textures/Texture.h"


static int MaxNumber = 4;

namespace GEngine
{
	LightTextureMaterial::LightTextureMaterial(const Asset::Texture& texture,
		const std::string& vertexFileName, 
		const std::string& fragFileName): Material(vertexFileName, fragFileName)
	{
		RenderSetting setting;
		setting.m_Mode = DrawMode::TRIANGLES;
		setting.m_RenderMode = RenderMode::Elements;
		setting.m_PrimitivesSetting.surfaceSetting.lineWidth = 1.0f;
		setting.m_PrimitivesSetting.surfaceSetting.bDoubleSide = true;
		setting.m_PrimitivesSetting.surfaceSetting.bWireFrame = false;
		SetRenderSettings(setting);

		UseProgram();
		SetUniforms<std::pair<GLuint, std::pair<GLuint, GLuint>>>({ {"u_texture", {m_RenderSetting.m_TexTarget, {texture.GetTextureID(), 1}} } });

		UpdateRenderSettings();

	}

	void LightTextureMaterial::UpdateRenderSettings()
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

	void LightTextureMaterial::UploadUniforms()
	{
		

		/*SetUniforms<Vec3f>({ {m_LightComps.Positions.Name, m_LightComps.Positions.Data},
									{m_LightComps.Colors.Name, m_LightComps.Colors.Data},
									{m_LightComps.Attenuations.Name, m_LightComps.Attenuations.Data} });*/

		SetUniforms<Vec3f>({ { "u_skyColor", {0.5f, 0.5f, 0.5f}} });
	


		SetUniforms<bool>({ {"u_useFakeLighting", IsFakeLighting()}});

		SetUniforms<bool>({ { "u_blinn", m_IsBlinn} });

		SetUniforms<float>({ {"u_reflectivity", m_MaterialProp.Reflectivity},
									{"u_shineDamper", m_MaterialProp.ShineDamper},
									{m_FogComp.Density.Name, m_FogComp.Density.Data},
									{m_FogComp.Gradient.Name, m_FogComp.Gradient.Data} });


		SetUniforms<int>({ {m_TextureComp.NumOfRows.Name, m_TextureComp.NumOfRows.Data} });


	
	}



}
