#include "gepch.h"
#include "Core/GEngine.h"
#include "Managers/AssetsManager.h"
#include "Assets/Textures/TextTexture.h"
#include "../Assets/TextureBackend.h"
#include "Core/RenderTarget.h"
#include <cmath>
#include <cstring>

namespace GEngine::Manager
{
    using namespace Asset;
    namespace
    {
        template<std::totally_ordered Key>
        class TextureCacheEntry
        {
            std::map<Key, TextureHandle>& cache;
            typename std::map<Key, TextureHandle>::iterator entry;
        public:
            TextureCacheEntry(std::map<Key, TextureHandle>& values, const Key& key) : cache(values)
            {
                auto [position, inserted] = cache.try_emplace(key);
                AssetDetail::RequireInvariant(inserted);
                entry = position;
            }
            ~TextureCacheEntry() { if (!entry->second) cache.erase(entry); }
            void Commit(TextureHandle handle) noexcept { entry->second = handle; }
        };
        TextureError RegistryFailure(RegistryError code)
        { return {TextureErrorCode::Registry, {}, "Texture registry operation failed", {}, code}; }
        std::expected<std::filesystem::path, TextureError> Canonical(const std::filesystem::path& source)
        {
            std::error_code error;
            auto path = std::filesystem::canonical(source, error);
            if (error) return std::unexpected(TextureError{TextureErrorCode::FileSystem, source.string(), error.message(), error});
            return path;
        }
    }
    AssetsManager::AssetsManager(AssetPublication& publication, std::filesystem::path root)
        : m_Publication(publication), m_Images(publication), m_ImageRoot(std::move(root)) {}
    AssetsManager::~AssetsManager()
    {
        m_Attachments.clear();
        m_Bindings.clear();
        auto access = m_Publication.BeginPublication();
        // Escaped render leases are an ownership invariant: never retire the context under them.
        AssetDetail::RequireInvariant(bool(m_Images.Close(access)));
    }
    std::expected<AssetsManager*, TextureError> AssetsManager::Current()
    {
        auto* root = EngineContext::TryGet();
        if (!root) return std::unexpected(TextureError{TextureErrorCode::ContextUnavailable, {}, "No live EngineContext"});
        return root->TryAssets();
    }
    std::expected<TextureHandle, TextureError> AssetsManager::Publish(TextureResource image)
    {
        auto access = m_Publication.BeginPublication();
        auto result = m_Images.Create(access, std::move(image));
        if (!result) return std::unexpected(RegistryFailure(result.error()));
        return *result;
    }
    std::expected<TextureView, TextureError> AssetsManager::Resolve(TextureHandle handle)
    {
        auto access = m_Publication.BeginFrame();
        auto result = m_Images.Acquire(access, handle);
        if (!result) return std::unexpected(RegistryFailure(result.error()));
        return TextureView(std::move(*result));
    }
    std::expected<TextureView, TextureError> AssetsManager::ResolveTexture(TextureHandle handle)
    {
        auto manager = Current();
        if (!manager) return std::unexpected(manager.error());
        return (*manager)->Resolve(handle);
    }
    std::expected<Texture*, TextureError> AssetsManager::Binding(TextureHandle handle, const std::string& uniform)
    {
        const auto key = std::pair{handle, uniform};
        if (auto found = m_Bindings.find(key); found != m_Bindings.end()) return found->second.get();
        auto view = Resolve(handle);
        if (!view) return std::unexpected(view.error());
        auto binding = std::make_unique<Texture>(std::move(*view), uniform);
        auto* result = binding.get();
        m_Bindings.emplace(key, std::move(binding));
        return result;
    }
    std::expected<TextureHandle, TextureError> AssetsManager::LoadTexture(const std::string& source,
        const TextureDesc& desc, const std::string& extension)
    {
        auto manager = Current();
        if (!manager) return std::unexpected(manager.error());
        auto& self = **manager;
        if (source.empty() || desc.width != 0 || desc.height != 0)
            return std::unexpected(TextureError{TextureErrorCode::InvalidDescription, source, "File loads need a source and decoded dimensions"});
        auto path = std::filesystem::path(source);
        if (!path.is_absolute()) path = self.m_ImageRoot / path;
        if (desc.kind == TextureKind::Image2D && !path.has_extension()) path += extension;
        auto canonical = Canonical(path);
        if (!canonical) return std::unexpected(canonical.error());
        const auto cubeExtension = desc.kind == TextureKind::Cube ? extension : std::string{};
        const ImageKey key{canonical->string(), desc, cubeExtension};
        if (auto found = self.m_ImageCache.find(key); found != self.m_ImageCache.end()) return found->second;
        // Filesystem equivalence also respects Windows case-sensitive directories and hard links.
        for (const auto& [cached, handle] : self.m_ImageCache)
        {
            if (std::get<1>(cached) != desc || std::get<2>(cached) != cubeExtension) continue;
            std::error_code error;
            if (std::filesystem::equivalent(std::get<0>(cached), *canonical, error) && !error) return handle;
        }
        auto image = TextureResource::Load(*canonical, desc, extension);
        if (!image) return std::unexpected(image.error());
        TextureCacheEntry reservation(self.m_ImageCache, key);
        auto published = self.Publish(std::move(*image));
        if (!published) return std::unexpected(published.error());
        reservation.Commit(*published);
        return *published;
    }
    std::expected<TextureHandle, TextureError> AssetsManager::FallbackTexture(const TextureDesc& requested)
    {
        auto manager = Current();
        if (!manager) return std::unexpected(manager.error());
        auto& self = **manager;
        if (auto found = self.m_Fallbacks.find(requested); found != self.m_Fallbacks.end()) return found->second;
        if (requested.format == TextureFormat::Depth32Float)
            return std::unexpected(TextureError{TextureErrorCode::InvalidDescription, {}, "Color fallback cannot replace a depth attachment"});
        auto desc = requested;
        desc.width = desc.height = 4;
        const bool floating = desc.format == TextureFormat::RGB16Float || desc.format == TextureFormat::RGBA16Float;
        const std::size_t channels = desc.format == TextureFormat::R8 ? 1
            : desc.format == TextureFormat::RGB8 || desc.format == TextureFormat::RGB16Float ? 3 : 4;
        const std::size_t faces = desc.kind == TextureKind::Cube ? 6 : 1;
        std::vector<std::byte> pixels(16 * faces * channels * (floating ? 4 : 1));
        for (std::size_t i = 0; i < 16 * faces; ++i)
            for (std::size_t channel = 0; channel < channels; ++channel)
            {
                const float value = channel == 3 || ((i % 4 + i / 4) % 2) ? 1.f : 0.f;
                const auto offset = (i * channels + channel) * (floating ? 4 : 1);
                if (floating) std::memcpy(pixels.data() + offset, &value, sizeof(value));
                else pixels[offset] = std::byte(value > 0 ? 255 : 0);
            }
        auto image = TextureResource::Create(desc, {pixels, 0});
        if (!image) return std::unexpected(image.error());
        TextureCacheEntry reservation(self.m_Fallbacks, requested);
        auto handle = self.Publish(std::move(*image));
        if (!handle) return std::unexpected(handle.error());
        reservation.Commit(*handle);
        return *handle;
    }
    std::expected<Texture*, TextureError> AssetsManager::GetTexture(const std::string& path,
        const std::string& uniform, const std::string& extension, const TextureDesc& desc)
    {
        auto handle = LoadTexture(path, desc, extension);
        if (!handle) return std::unexpected(handle.error());
        auto manager = Current();
        if (!manager) return std::unexpected(manager.error());
        return (*manager)->Binding(*handle, uniform);
    }
    std::expected<Texture*, TextureError> AssetsManager::GetTextureOrFallback(const std::string& path,
        const std::string& uniform, const std::string& extension, const TextureDesc& desc)
    {
        if (!path.empty())
        {
            auto loaded = GetTexture(path, uniform, extension, desc);
            if (loaded) return loaded;
            if (loaded.error().code != TextureErrorCode::Decode && loaded.error().code != TextureErrorCode::FileSystem)
                return loaded;
            GENGINE_CORE_WARN("Texture {}: {}; selecting the explicit fallback image", loaded.error().source, loaded.error().message);
        }
        auto fallback = FallbackTexture(desc);
        if (!fallback) return std::unexpected(fallback.error());
        auto manager = Current();
        if (!manager) return std::unexpected(manager.error());
        return (*manager)->Binding(*fallback, uniform);
    }
    std::expected<Texture*, TextureError> AssetsManager::GetCascadedFrameBufferTexture(
        const CascadeShadowFrameBuffer& framebuffer, const std::string& uniform)
    {
        auto manager = Current();
        if (!manager) return std::unexpected(manager.error());
        auto attachment = AssetDetail::TextureBackend::Borrow([&framebuffer] { return framebuffer.GetLightDepthMaps(); }, GL_TEXTURE_2D_ARRAY);
        auto binding = std::make_unique<Texture>(TextureView(std::move(attachment)), uniform);
        auto* result = binding.get(); (*manager)->m_Attachments.push_back(std::move(binding)); return result;
    }
    std::expected<Texture*, TextureError> AssetsManager::GetPointShadowFrameBufferTexture(
        const PointShadowFrameBuffer& framebuffer, const std::string& uniform)
    {
        auto manager = Current();
        if (!manager) return std::unexpected(manager.error());
        auto attachment = AssetDetail::TextureBackend::Borrow([&framebuffer] { return framebuffer.GetDepthCubeMaps(); }, GL_TEXTURE_CUBE_MAP);
        auto binding = std::make_unique<Texture>(TextureView(std::move(attachment)), uniform);
        auto* result = binding.get(); (*manager)->m_Attachments.push_back(std::move(binding)); return result;
    }
    std::expected<Font*, TextureError> AssetsManager::GetFont(const std::string& source)
    {
        auto manager = Current();
        if (!manager) return std::unexpected(manager.error());
        auto& self = **manager;
        const auto file = source.empty() ? self.m_ImageRoot.parent_path() / "Fonts/Carlito-Regular.ttf" : std::filesystem::path(source);
        auto path = Canonical(file);
        if (!path) return std::unexpected(path.error());
        const auto key = path->string();
        if (auto found = self.m_Fonts.find(key); found != self.m_Fonts.end()) return found->second.get();
        auto font = Font::Load(key);
        if (!font) return std::unexpected(font.error());
        auto* result = font->get(); self.m_Fonts.emplace(key, std::move(*font)); return result;
    }
    std::expected<Texture*, TextureError> AssetsManager::GetTextTexture(const std::string& text,
        const std::string& source, int size, const glm::vec3& color, const std::string& uniform)
    {
        if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z)
            || color.x < 0 || color.x > 1 || color.y < 0 || color.y > 1 || color.z < 0 || color.z > 1)
            return std::unexpected(TextureError{TextureErrorCode::Font, source, "Text color must be finite in [0,1]"});
        auto manager = Current();
        if (!manager) return std::unexpected(manager.error());
        auto& self = **manager;
        auto canonical = Canonical(source.empty() ? self.m_ImageRoot.parent_path() / "Fonts/Carlito-Regular.ttf" : std::filesystem::path(source));
        if (!canonical) return std::unexpected(canonical.error());
        const TextKey key{canonical->string(), text, size, color.x, color.y, color.z};
        if (auto found = self.m_TextCache.find(key); found != self.m_TextCache.end()) return self.Binding(found->second, uniform);
        auto font = GetFont(canonical->string());
        if (!font) return std::unexpected(font.error());
        auto image = TextTexture::Create(**font, text, size, {color.x, color.y, color.z});
        if (!image) return std::unexpected(image.error());
        TextureCacheEntry reservation(self.m_TextCache, key);
        auto handle = self.Publish(std::move(*image));
        if (!handle) return std::unexpected(handle.error());
        reservation.Commit(*handle);
        return self.Binding(*handle, uniform);
    }
}
