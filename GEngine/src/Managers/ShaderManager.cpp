#include "gepch.h"
#include "Core/GEngine.h"
#include "Managers/ShaderManager.h"

namespace GEngine::Manager
{
    std::expected<ShaderManager*, ShaderError> ShaderManager::Current()
    {
        auto* root = EngineContext::TryGet();
        if (!root) return std::unexpected(ShaderError{ShaderErrorCode::ContextUnavailable, {}, {}, "No live shader services"});
        return root->Shaders();
    }
    ShaderManager::~ShaderManager() = default;

    std::expected<Shader*, ShaderError> ShaderManager::GetShaderProgram(const Files& files)
    {
        auto manager = Current();
        if (!manager) return std::unexpected(manager.error());
        auto& cache = (*manager)->m_ShaderMap;
        if (auto it = cache.find(files); it != cache.end()) return it->second.get();
        auto creation = CreateShaderProgramFromFiles(files.keys_filepath);
        if (!creation) return std::unexpected(std::move(creation.error()));
        auto shader = std::make_unique<Shader>(std::move(*creation));
        // Linking publishes a program; state-dependent validation belongs to its use.
        auto* result = shader.get();
        cache.emplace(files, std::move(shader));
        return result;
    }
}
