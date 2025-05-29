#pragma once
#include "Material.h"

namespace GEngine
{

    namespace Asset
    {
        class Texture;
    }

    class NormalLightTextureMaterial: public Material
    {
    public:
     /*   NormalLightTextureMaterial(const std::vector<Asset::Texture*>& textures,
            const std::string& vertexFileName = base_shader_dir + "normal.vert",
            const std::string& fragFileName = base_shader_dir + "normal.frag");*/

     

        NormalLightTextureMaterial(const std::vector<Asset::Texture*>& textures, const std::string& shaderName);

        void UpdateRenderSettings() override;

        void UploadUniforms() override;

        NormalLightTextureMaterial& SetLightComponent(const Component::LightComponents& light)
        {
            m_LightComps = light;
            return *this;
        }

        NormalLightTextureMaterial& SetFogComponent(const Component::FogComponent& fog)
        {
            m_FogComp = fog;
            return *this;
        }


        NormalLightTextureMaterial& SetTextureComponent(const Component::TextureComponent& tex)
        {
            m_TextureComp = tex;
            return *this;
        }

        void SetIsBlinn(bool blinn)
        {
            m_IsBlinn = blinn;
        }

    private:
        //std::vector<Component::LightComponent> m_LightComp;
        Component::LightComponents m_LightComps;
        Component::FogComponent m_FogComp;
        Component::TextureComponent m_TextureComp;
        bool m_IsBlinn{ true };

    };

}