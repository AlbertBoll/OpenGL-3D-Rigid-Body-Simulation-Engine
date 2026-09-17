#include "gepch.h"
#include "Scene/SkyBoxEntity.h"
#include "Managers/AssetsManager.h"
#include "Material/SkyBoxMaterial.h"
#include "Camera/Camera.h"

namespace GEngine
{
    static std::string image_base_dir = "../GEngine/include/GEngine/Assets/Images/SkyBox";

    std::expected<RefPtr<Material>, ApplicationInitializationError> SkyBoxEntity::GetSkyBoxMaterial(const SkyBoxComponent& comp)
    {
        Asset::TextureDesc info;
        info.kind = Asset::TextureKind::Cube;
        info.colorSpace = Asset::TextureColorSpace::Linear;
        info.mips = Asset::TextureMipIntent::None;
        info.orientation = Asset::ImageOrientation::TopLeft;
        //auto tex = Manager::AssetsManager::GetTexture("SkyBox", "u_SkyBoxDay", ".png", info);
        auto tex1Result = Manager::AssetsManager::GetTextureOrFallback("SkyBox/Day/", "u_SkyBoxDay", ".png", info);
        if (!tex1Result) return std::unexpected(tex1Result.error());
        auto* tex1 = *tex1Result;
        auto tex2Result = Manager::AssetsManager::GetTextureOrFallback("SkyBox/Night/", "u_SkyBoxNight", ".png", info);
        if (!tex2Result) return std::unexpected(tex2Result.error());
        auto* tex2 = *tex2Result;
        std::vector<Asset::Texture*> texs = { tex1, tex2 };
        auto created = Material::Create<SkyBoxMaterial>(texs);
        if (!created) return std::unexpected(created.error());
        auto skyBoxMaterial = std::move(*created);
        //auto skyBoxMaterial = CreateRefPtr<SkyBoxMaterial>(*tex1);
        skyBoxMaterial->SetSkyBoxComponent(comp);
        return skyBoxMaterial;
    }

    SkyBoxEntity::SkyBoxEntity(const SkyBoxComponent& comp, Geometry* geometry, const RefPtr<Material>& material): Entity(geometry, material)
    {
        m_Description = comp;
    }

    std::expected<std::unique_ptr<SkyBoxEntity>, ApplicationInitializationError> SkyBoxEntity::Create(
        const SkyBoxComponent& comp, Geometry* geometry)
    {
        auto material = GetSkyBoxMaterial(comp);
        if (!material) return std::unexpected(material.error());
        return std::unique_ptr<SkyBoxEntity>(new SkyBoxEntity(comp, geometry, *material));
    }

    void SkyBoxEntity::Render(CameraBase* camera)
    {
        if (!GetMaterial()) return;

        auto view = Mat4(Mat3(camera->GetView()));

        glDepthFunc(GL_LEQUAL);
        UseShaderProgram();

        BindVAO();

        SetUniforms<Mat4>({{"u_model", GetWorldTransform()},
            {"u_view", Mat4(Mat3(camera->GetView()))}, 
                                  {"u_projection", camera->GetProjection()} });

        GetMaterial()->BindTextureUniforms();
        GetMaterial()->UploadUniforms();
        UpdateRenderSettings();
        
        ArraysDraw();
        //glDepthRange(0.f, 1.f);
        //glDepthMask(GL_TRUE);


    }

    void SkyBoxEntity::SetSkyBoxComponent(const SkyBoxComponent& comp)
    {

    }

    SkyBoxComponent& SkyBoxEntity::GetSkyBoxComponent()
    {
        auto material = GetMaterial();
        if (!material) return m_Description;
        auto skyBox_material = dynamic_cast<SkyBoxMaterial*>(material);
        
        return skyBox_material->GetSkyBoxComponent();
        

     
                

    }

}
