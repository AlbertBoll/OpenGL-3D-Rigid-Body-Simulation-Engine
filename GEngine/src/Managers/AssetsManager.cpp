#include "gepch.h"
#include "Core/GEngine.h"
#include "Core/RuntimeAssets.h"
#include "Managers/AssetsManager.h"
#include "Assets/Textures/TextTexture.h"
#include <cmath>
#include <stdexcept>

namespace GEngine::Manager
{
    namespace
    {
        template<class T> void DeleteOwnedTexture(Asset::Texture* texture)
        {
            const auto name = texture->GetTextureID();
            if (name) glDeleteTextures(1, &name);
            // Texture has no virtual destructor; preserve the allocation type.
            delete static_cast<T*>(texture);
        }
    }

    AssetsManager& AssetsManager::Current() { return EngineContext::Current().Assets(); }
    AssetsManager::~AssetsManager() = default;

    Asset::Texture* AssetsManager::GetTexture(const std::string& imageFilePath,
        const std::string& uniform_name, const std::string& extension, const Asset::TextureInfo& info)
    {
        auto& cache = Current().m_TextureMap;
        std::filesystem::path path(imageFilePath);
        if (!path.is_absolute()) path = RuntimeAssets::Directory{"Images/"} + imageFilePath;
        if (!info.b_CubeMap && !path.has_extension()) path += extension;
        const auto key = path.lexically_normal().string();
        if (auto it = cache.find(key); it != cache.end()) return it->second.get();

        TextureOwner texture(new Asset::Texture, DeleteOwnedTexture<Asset::Texture>);
        texture->SetTextureInfo(info);
        // Own the wrapper before loading, including any partially allocated GL name.
        if (info.b_CubeMap && info.b_HDR) texture->LoadHdrCubeMap(key);
        else if (info.b_CubeMap) texture->LoadCubeMap(key, extension);
        else if (info.b_HDR) texture->LoadHdrTexture(key);
        else texture->LoadTexture(key);
        // Ordinary missing images retain the existing checkerboard fallback.
        // Loaders that return no GL name cannot publish a usable cache entry.
        if (!texture->GetTextureID()) throw std::runtime_error("Texture initialization failed: " + key);
        if (!uniform_name.empty()) texture->SetUniformName(uniform_name);
        auto* result = texture.get();
        cache.emplace(key, std::move(texture));
        return result;
    }

    Asset::Texture* AssetsManager::GetCascadedFrameBufferTexture(const CascadeShadowFrameBuffer& fb,
        const std::string& uniform_name)
    {
        auto& wrappers = Current().m_FrameBufferTextures;
        Asset::TextureInfo info;
        info.m_TextureSpec.m_TexTarget = GL_TEXTURE_2D_ARRAY;
        auto texture = std::make_unique<Asset::Texture>(fb.GetLightDepthMaps(), info, uniform_name);
        auto* result = texture.get();
        wrappers.push_back(std::move(texture));
        return result;
    }

    Asset::Texture* AssetsManager::GetPointShadowFrameBufferTexture(const PointShadowFrameBuffer& fb,
        const std::string& uniform_name)
    {
        auto& wrappers = Current().m_FrameBufferTextures;
        Asset::TextureInfo info;
        info.m_TextureSpec.m_TexTarget = GL_TEXTURE_CUBE_MAP;
        auto texture = std::make_unique<Asset::Texture>(fb.GetDepthCubeMaps(), info, uniform_name);
        auto* result = texture.get();
        wrappers.push_back(std::move(texture));
        return result;
    }

    Asset::Texture* AssetsManager::GetTextTexture(const std::string& text, const std::string& font_file,
        int pointSize, const glm::vec3& color, const std::string& uniform_name)
    {
        auto& cache = Current().m_TextTextures;
        if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z)
            || color.x < 0.f || color.x > 1.f || color.y < 0.f || color.y > 1.f || color.z < 0.f || color.z > 1.f)
            throw std::invalid_argument("Text color must be finite and within [0, 1]");
        const TextKey key{font_file, text, pointSize, color.x, color.y, color.z, uniform_name};
        if (auto it = cache.find(key); it != cache.end()) return it->second.get();
        TextureOwner texture(new Asset::TextTexture(text, font_file, pointSize, color),
            DeleteOwnedTexture<Asset::TextTexture>);
        if (!uniform_name.empty()) texture->SetUniformName(uniform_name);
        auto* result = texture.get();
        cache.emplace(key, std::move(texture));
        return result;
    }

    Asset::Font* AssetsManager::GetFont(const std::string& font_file)
    {
        auto& cache = Current().m_FontMap;
        if (auto it = cache.find(font_file); it != cache.end()) return it->second.get();
        auto font = std::make_unique<Asset::Font>();
        font->LoadFont(font_file);
        auto* result = font.get();
        cache.emplace(font_file, std::move(font));
        return result;
    }
}
