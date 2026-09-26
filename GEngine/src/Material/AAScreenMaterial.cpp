#include "gepch.h"
#include "Material/AAScreenMaterial.h"
#include "MaterialBackend.h"
#include "../Assets/TextureBackend.h"

namespace GEngine
{
	using namespace Math;

    AAScreenMaterial::CreationResult AAScreenMaterial::Create(const Asset::TextureView& screen,
        const std::string& vertexFileName, const std::string& fragFileName)
    {
        Construction construction;
        auto candidate = std::shared_ptr<AAScreenMaterial>(new AAScreenMaterial(construction, vertexFileName, fragFileName));
        if (!construction.result) return std::unexpected(CreationError{std::move(construction.result.error())});
        auto name = Asset::AssetDetail::TextureBackend::Name(screen);
        if (!name) return std::unexpected(CreationError{Asset::SamplingError{std::move(name.error())}});
        if (Asset::AssetDetail::TextureBackend::Target(screen) != GL_TEXTURE_2D)
            return std::unexpected(CreationError{Asset::SamplingError{Asset::TextureError{
                Asset::TextureErrorCode::InvalidDescription, "AAScreenMaterial::Create",
                "Screen material requires a resolved two-dimensional texture view"}}});
        if (auto bound = candidate->SetTextureBinding("uScreenTexture", screen, 1); !bound)
            return std::unexpected(CreationError{std::move(bound.error())});
        candidate->UpdateRenderSettings();
        return candidate;
    }

    AAScreenMaterial::AAScreenMaterial(Construction& construction, const std::string& vertexFileName,
        const std::string& fragFileName) : Material(construction, vertexFileName, fragFileName)
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
    }



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
		MaterialDetail::BackendAccess::SetTextureUniforms(*this, { {"uScreenTexture", {m_RenderSetting.m_TexTarget, {screenTextureID, 1}} } });
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

