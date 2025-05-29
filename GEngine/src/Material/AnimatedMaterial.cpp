#include "gepch.h"
#include "Material/AnimatedMaterial.h"
#include"Assets/Textures/Texture.h"
#include"Animation/AnimationSystem.h"

namespace GEngine
{
    AnimatedMaterial::AnimatedMaterial(const std::vector<Asset::Texture*>& textures, const std::string& shaderName)
        : Material(base_shader_dir + shaderName + ".vert", base_shader_dir + shaderName + ".frag")
    {
		RenderSetting setting;
		setting.m_Mode = DrawMode::TRIANGLES;
		setting.m_RenderMode = RenderMode::Elements;
		setting.m_PrimitivesSetting.surfaceSetting.lineWidth = 1.0f;
		setting.m_PrimitivesSetting.surfaceSetting.bDoubleSide = true;
		setting.m_PrimitivesSetting.surfaceSetting.bWireFrame = false;
		SetRenderSettings(setting);

		UseProgram();

		static unsigned int i = 0;
		for (auto& tex : textures)
		{
			SetUniforms<std::pair<GLuint, std::pair<GLuint, GLuint>>>({ {tex->GetUniformName(), {m_RenderSetting.m_TexTarget, {tex->GetTextureID(), i++}}} });
		}

		UpdateRenderSettings();
    }

	void AnimatedMaterial::UpdateRenderSettings()
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


	void AnimatedMaterial::UploadUniforms()
	{
		SetUniforms<Vec3f>({ { "u_skyColor", {0.5f, 0.5f, 0.5f}} });


		SetUniforms<bool>({ { "u_blinn", m_IsBlinn} });

		SetUniforms<float>({ {"u_reflectivity", m_MaterialProp.Reflectivity},
									{"u_shineDamper", m_MaterialProp.ShineDamper},
									{m_FogComp.Density.Name, m_FogComp.Density.Data},
									{m_FogComp.Gradient.Name, m_FogComp.Gradient.Data} });

		SetUniforms<Mat4>("u_finalBonesMatrices", m_AnimationSystem->GetFinalBoneMatrices());
		SetUniforms<int>({ {m_TextureComp.NumOfRows.Name, m_TextureComp.NumOfRows.Data} });
	}
}