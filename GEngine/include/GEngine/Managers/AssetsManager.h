#pragma once
#include "Assets/Textures/Texture.h"
#include "Assets/Textures/AsyncTexture.h"
#include "Assets/Samplers/Sampler.h"
#include "Assets/Fonts/Font.h"
#include "Core/RuntimeAssets.h"
#include "Math/Math.h"
#include "Material/MaterialBinding.h"
#include <map>
#include <tuple>

namespace GEngine { class EngineContext; class CascadeShadowFrameBuffer; class PointShadowFrameBuffer; }
namespace GEngine::Manager
{
    class AssetsManager final
    {
    public:
        ~AssetsManager();
        AssetsManager(const AssetsManager&) = delete;
        AssetsManager& operator=(const AssetsManager&) = delete;
        // Root-owned, lazy async loader. Caller supplies Queue() to FrameScheduler
        // and retains FallbackTexture until Status reports a ready handle.
        static std::expected<Asset::AsyncTextureLoader*, Asset::AsyncTextureError> AsyncTextures();
        static std::expected<Asset::TextureHandle, Asset::TextureError> LoadTexture(const std::string& path,
            const Asset::TextureDesc& desc = {}, const std::string& extension = ".png");
        static std::expected<Asset::TextureView, Asset::TextureError> ResolveTexture(Asset::TextureHandle);
        static std::expected<Asset::SamplerHandle, Asset::SamplingError> GetSampler(const Asset::SamplerDesc&);
        static std::expected<Asset::SamplerView, Asset::SamplingError> ResolveSampler(Asset::SamplerHandle);
        // Borrow stable root-owned registries before extraction. Their acquisitions
        // use the same caller-held frame scope as mesh/material resolution.
        static std::expected<MaterialBindingResources, Asset::TextureError> FrameBindings(const ShaderProgramRegistry&);
        static std::expected<Asset::SampledTextureBinding, Asset::SamplingError> SampleTexture(const Asset::TextureView&);
        static std::expected<Asset::SampledTextureBinding, Asset::SamplingError> SampleTexture(
            const Asset::TextureView&, const Asset::SamplerDesc&);
        static std::expected<Asset::TextureHandle, Asset::TextureError> FallbackTexture(const Asset::TextureDesc& = {});
        static std::expected<Asset::Texture*, Asset::TextureError> GetTexture(const std::string& path = {},
            const std::string& uniform = "u_texture", const std::string& extension = ".png", const Asset::TextureDesc& = {});
        // Explicit presentation recovery. Failed sources never enter the image cache.
        static std::expected<Asset::Texture*, Asset::TextureError> GetTextureOrFallback(const std::string& path = {},
            const std::string& uniform = "u_texture", const std::string& extension = ".png", const Asset::TextureDesc& = {});
        static std::expected<Asset::Texture*, Asset::TextureError> GetCascadedFrameBufferTexture(const CascadeShadowFrameBuffer&, const std::string& uniform = {});
        static std::expected<Asset::Texture*, Asset::TextureError> GetPointShadowFrameBufferTexture(const PointShadowFrameBuffer&, const std::string& uniform = {});
        static std::expected<Asset::Texture*, Asset::TextureError> GetTextTexture(const std::string& text,
            const std::string& font = {}, int pointSize = 24,
            const glm::vec3& color = {0,0,1}, const std::string& uniform = {});
        static std::expected<Asset::Font*, Asset::TextureError> GetFont(const std::string& path);
    private:
        friend class ::GEngine::EngineContext;
        AssetsManager(Asset::AssetPublication&, std::filesystem::path imageRoot);
        static std::expected<AssetsManager*, Asset::TextureError> Current();
        std::expected<Asset::TextureHandle, Asset::TextureError> Publish(Asset::TextureResource image);
        std::expected<Asset::TextureView, Asset::TextureError> Resolve(Asset::TextureHandle);
        std::expected<Asset::Texture*, Asset::TextureError> Binding(Asset::TextureHandle, const std::string&);
        Asset::AssetPublication& m_Publication;
        Asset::TextureRegistry m_Images;
        Asset::SamplerCache m_Samplers;
        std::filesystem::path m_ImageRoot;
        using ImageKey = std::tuple<std::string, Asset::TextureDesc, std::string>;
        std::map<ImageKey, Asset::TextureHandle> m_ImageCache;
        std::map<Asset::TextureDesc, Asset::TextureHandle> m_Fallbacks;
        std::map<std::pair<Asset::TextureHandle, std::string>, std::unique_ptr<Asset::Texture>> m_Bindings;
        std::vector<std::unique_ptr<Asset::Texture>> m_Attachments;
        using TextKey = std::tuple<std::string, std::string, int, float, float, float>;
        std::map<TextKey, Asset::TextureHandle> m_TextCache;
        std::map<std::string, std::unique_ptr<Asset::Font>> m_Fonts;
        std::unique_ptr<Asset::AsyncTextureLoader> m_AsyncTextures;
    };
}
