#include "gepch.h"
#include "Material/AAScreenMaterial.h"

namespace GEngine
{
	using namespace Math;

	AAScreenMaterial::AAScreenMaterial(Construction& construction, unsigned int screenTextureID, const std::string& vertexFileName,
		const std::string& fragFileName): Material(construction, vertexFileName, fragFileName)
	{
        if (!construction) return;
		RenderSetting setting;
	
		setting.m_Mode = DrawMode::TRIANGLES;
		setting.m_RenderMode = RenderMode::Arrays;
		setting.m_PrimitivesSetting.surfaceSetting.lineWidth = 1.0f;
		setting.m_PrimitivesSetting.surfaceSetting.bDoubleSide = true;
		setting.m_PrimitivesSetting.surfaceSetting.bWireFrame = false;
		SetRenderSettings(setting);

		UseProgram();
		SetUniforms<std::pair<GLuint, std::pair<GLuint, GLuint>>>({ {"uScreenTexture", {m_RenderSetting.m_TexTarget, {screenTextureID, 1}} } });
		UpdateRenderSettings();
	}

	void AAScreenMaterial::UpdateRenderSettings()
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

