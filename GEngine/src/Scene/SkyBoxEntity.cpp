#include "gepch.h"
#include "Scene/SkyBoxEntity.h"
#include "Managers/AssetsManager.h"
#include "Material/SkyBoxMaterial.h"
#include "Camera/Camera.h"

namespace GEngine
{
    static std::string image_base_dir = "../GEngine/include/GEngine/Assets/Images/SkyBox";

    RefPtr<Material> SkyBoxEntity::GetSkyBoxMaterial(const SkyBoxComponent& comp)
    {
        Asset::TextureInfo info;
        info.m_TextureSpec.m_MagFilter = GL_LINEAR;
        info.m_TextureSpec.m_MinFilter = GL_LINEAR;
        info.m_TextureSpec.m_WrapR = GL_CLAMP_TO_EDGE;
        info.m_TextureSpec.m_WrapS = GL_CLAMP_TO_EDGE;
        info.m_TextureSpec.m_WrapT = GL_CLAMP_TO_EDGE;
        info.b_CubeMap = true;
        info.b_HDR = false;
        info.b_GammaCorrection = false;
        //auto tex = Manager::AssetsManager::GetTexture("SkyBox", "u_SkyBoxDay", ".png", info);
        Asset::Texture* tex1 = Manager::AssetsManager::GetTexture("SkyBox/Day/", "u_SkyBoxDay", ".png", info);
        Asset::Texture* tex2 = Manager::AssetsManager::GetTexture("SkyBox/Night/", "u_SkyBoxNight", ".png", info);
        std::vector<Asset::Texture*> texs = { tex1, tex2 };
        auto skyBoxMaterial = CreateRefPtr<SkyBoxMaterial>(texs);
        //auto skyBoxMaterial = CreateRefPtr<SkyBoxMaterial>(*tex1);
        skyBoxMaterial->SetSkyBoxComponent(comp);
        return skyBoxMaterial;
    }

    SkyBoxEntity::SkyBoxEntity(const SkyBoxComponent& comp, Geometry* geometry, const RefPtr<Material>& material): Entity(geometry, material)
    {
        auto material_ = GetSkyBoxMaterial(comp);
        SetMaterial(material_);
    }

    void SkyBoxEntity::Render(CameraBase* camera)
    {

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
        auto skyBox_material = dynamic_cast<SkyBoxMaterial*>(material);
        
        return skyBox_material->GetSkyBoxComponent();
        

     
                

    }

}