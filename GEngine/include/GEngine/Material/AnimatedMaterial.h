#pragma once
#include "Material.h"

namespace GEngine
{
    class AnimationSystem;

    namespace Asset
    {
        class Texture;
    }

    class AnimatedMaterial : public Material
    {
    public:
        AnimatedMaterial(const std::vector<Asset::Texture*>& textures, const std::string& shaderName);

        void UpdateRenderSettings() override;

        void UploadUniforms() override;

    /*    AnimatedMaterial& SetLightComponent(const Component::LightComponents& light)
        {
            m_LightComps = light;
            return *this;
        }*/

        AnimatedMaterial& SetFogComponent(const Component::FogComponent& fog)
        {
            m_FogComp = fog;
            return *this;
        }


        AnimatedMaterial& SetTextureComponent(const Component::TextureComponent& tex)
        {
            m_TextureComp = tex;
            return *this;
        }

        void SetIsBlinn(bool blinn)
        {
            m_IsBlinn = blinn;
        }

        AnimatedMaterial& SetAnimationSystem(AnimationSystem* sys)
        {
            m_AnimationSystem = sys;
            return *this;
        }

    private:
        //std::vector<Component::LightComponent> m_LightComp;
        //Component::LightComponents m_LightComps;
        AnimationSystem* m_AnimationSystem{};
        Component::FogComponent m_FogComp;
        Component::TextureComponent m_TextureComp;
        bool m_IsBlinn{ true };

    };

}