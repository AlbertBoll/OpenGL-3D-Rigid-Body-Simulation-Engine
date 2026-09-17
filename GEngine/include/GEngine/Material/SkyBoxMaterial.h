#pragma once
#include "Material.h"
#include <Component/Component.h>

namespace GEngine
{
    namespace Asset
    {
        class Texture;
    }
    class SkyBoxMaterial: public Material
    {
    public:
        SkyBoxMaterial(Construction& construction, const Asset::Texture& texture,
            const std::string& vertexFileName = base_shader_dir + "skybox.vert",
            const std::string& fragFileName = base_shader_dir + "skybox.frag");


        SkyBoxMaterial(Construction& construction, const std::vector<Asset::Texture*>& textures,
            const std::string& vertexFileName = base_shader_dir + "skybox.vert",
            const std::string& fragFileName = base_shader_dir + "skybox.frag");

        void UpdateRenderSettings() override;

        void UploadUniforms() override;

        SkyBoxMaterial& SetSkyBoxComponent(const Component::SkyBoxComponent& skyComp)
        {
            m_SkyBoxComp = skyComp;
            return *this;
        }

        Component::SkyBoxComponent& GetSkyBoxComponent() { return m_SkyBoxComp; }

    private:
        Component::SkyBoxComponent m_SkyBoxComp;

    };
}
