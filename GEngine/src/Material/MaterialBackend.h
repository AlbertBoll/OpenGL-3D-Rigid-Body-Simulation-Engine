#pragma once
// Backend-private: normal application/Actor headers never include native shader access.
#include "Material/Material.h"
#include "../Assets/ShaderBackend.h"
namespace GEngine::MaterialDetail
{
    struct BackendAccess
    {
        static GLuint Program(const Material& material)
        { return Asset::ShaderBackendAccess::Program(*material.m_Shader); }
        static auto& LegacyTextures(Material& material) { return material.m_TextureList; }
        static void SetTextureUniforms(Material& material,
            const std::map<std::string, std::pair<unsigned, std::pair<unsigned, unsigned>>>& uniforms)
        {
            material.UseProgram();
            for (const auto& [name, binding] : uniforms)
            {
                Asset::ShaderBackendAccess::BindTexture(*material.m_Shader, name.c_str(), binding.first, binding.second.first, binding.second.second);
                material.m_TextureList.emplace(binding);
            }
        }
    };
}
