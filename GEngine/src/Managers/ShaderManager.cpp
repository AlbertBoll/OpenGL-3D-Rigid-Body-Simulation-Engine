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
        auto creation = CreateShaderProgramFromFiles(files.keys_filepath);
        if (const auto* error = std::get_if<ShaderCreationError>(&creation))
            throw ShaderCreationException(*error);
        auto shader = std::make_unique<Shader>(std::move(std::get<Shader>(creation)));
        // Validate depends on current sampler/pipeline state, not successful linking.
        // Preserve its existing diagnostic behavior; compilation/link failures throw.
        shader->Validate();
        auto* result = shader.get();
        cache.emplace(files, std::move(shader));
        return result;
    }
}
