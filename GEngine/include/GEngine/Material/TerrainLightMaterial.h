#pragma once
#include "Material.h"
#include <Component/Component.h>

namespace GEngine
{
    namespace Asset
    {
        class Texture;
    }

    class TerrainLightMaterial: public Material
    {
    public:
        TerrainLightMaterial(const Asset::Texture& texture,
            const std::string& vertexFileName = base_shader_dir + "terrain.vert",
            const std::string& fragFileName = base_shader_dir + "terrain.frag");

        void UpdateRenderSettings() override;

        void UploadUniforms() override;

        TerrainLightMaterial(std::vector<Asset::Texture*> textures,
            const std::string& vertexFileName = base_shader_dir + "terrain.vert",
            const std::string& fragFileName = base_shader_dir + "terrain.frag");

        TerrainLightMaterial& SetLightComponent(const Component::LightComponents& light)
        {
            m_LightComps = light;
            return *this;
        }

        TerrainLightMaterial& SetFogComponent(const Component::FogComponent& fog)
        {
            m_FogComp = fog;
            return *this;
        }

        void SetIsBlinn(bool blinn)
        {
            m_IsBlinn = blinn;
        }


        Component::FogComponent& GetFogComponent() { return m_FogComp; }


    private:
        Component::LightComponents m_LightComps;
        Component::FogComponent m_FogComp;

        bool m_IsBlinn = { true };
        

    };

}