#pragma once
#include "Assets/Textures/Texture.h"
#include "Math/Math.h"
#include <initializer_list>

namespace GEngine::Asset { class Shader; }
namespace GEngine::Component
{
        struct TexturesComponent
        {
            TexturesComponent() = default;
            TexturesComponent(std::initializer_list<Asset::Texture*> textures)
            { for (const auto* texture : textures) Textures.push_back(*texture); }
            TexturesComponent(const std::vector<Asset::Texture*>& textures)
            { for (const auto* texture : textures) Textures.push_back(*texture); }
            void BindTextures(Asset::Shader* shader);
            void PreBindTextures(Asset::Shader* shader);
            void LoadUniforms(Asset::Shader* shader) const;
            // Small immutable binding descriptors/leases, never GPU owners or native identities.
            std::vector<Asset::Texture> Textures;
            struct TilingValue { std::string Name = "u_tiling"; Math::Vec2f Data{1.f, 1.f}; } Tiling;
        };

}
