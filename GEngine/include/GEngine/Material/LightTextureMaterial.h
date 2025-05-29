#pragma once
#include "Material.h"
#include <Component/Component.h>

namespace GEngine
{
    namespace Asset
    {
        class Texture;
    }

    class LightTextureMaterial: public Material
    {
    public:
        LightTextureMaterial(const Asset::Texture& texture, 
            const std::string& vertexFileName = base_shader_dir + "light.vert",
            const std::string& fragFileName = base_shader_dir + "light.frag");


        void UpdateRenderSettings() override;


        LightTextureMaterial& SetLightComponent(const Component::LightComponents& light)
        {
            m_LightComps = light;
            return *this;
        }

        LightTextureMaterial& SetFogComponent(const Component::FogComponent& fog)
        {
            m_FogComp = fog;
            return *this;
        }


        LightTextureMaterial& SetTextureComponent(const Component::TextureComponent& tex)
        {
            m_TextureComp = tex;
            return *this;
        }
        

        void UploadUniforms() override;


        void SetIsBlinn(bool blinn)
        {
            m_IsBlinn = blinn;
        }



    private:
        //std::vector<Component::LightComponent> m_LightComp;
        Component::LightComponents m_LightComps;
        Component::FogComponent m_FogComp;
        Component::TextureComponent m_TextureComp;
        bool m_IsBlinn = { true };

        //int m_NumOfRows = 1;
        //int m_TextureIndex = 0;
    

    protected:
        
    };

}
