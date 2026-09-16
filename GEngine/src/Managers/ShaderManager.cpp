#include "gepch.h"
#include "Core/GEngine.h"
#include "Managers/ShaderManager.h"
#include <stdexcept>

namespace GEngine::Manager
{
    ShaderManager& ShaderManager::Current() { return EngineContext::Current().Shaders(); }
    ShaderManager::~ShaderManager() = default;

    Shader* ShaderManager::GetShaderProgram(const Files& files)
    {
        auto& cache = Current().m_ShaderMap;
        if (auto it = cache.find(files); it != cache.end()) return it->second.get();
        if (files.keys_filepath.empty()) throw std::invalid_argument("Shader program requires source files");
        auto shader = std::make_unique<Shader>();
        for (const auto& file : files.keys_filepath) shader->CompileShader(file.c_str());
        shader->Link();
        // Validate depends on current sampler/pipeline state, not successful linking.
        // Preserve its existing diagnostic behavior; compilation/link failures throw.
        shader->Validate();
        shader->FindUniformLocations();
        auto* result = shader.get();
        cache.emplace(files, std::move(shader));
        return result;
    }
}
