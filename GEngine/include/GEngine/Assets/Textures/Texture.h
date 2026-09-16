#pragma once

#include "Assets/AssetRegistry.h"
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>

namespace GEngine::Asset
{
    enum class TextureKind { Image2D, Cube };
    enum class TextureFormat { R8, RGB8, RGBA8, RGB16Float, RGBA16Float, Depth32Float };
    enum class TextureColorSpace { Linear, SRGB };
    enum class TextureMipIntent { None, Generate };
    enum class ImageOrientation { TopLeft, BottomLeft };
    enum class TextureUsage { Sampled, Attachment };
    struct TextureDesc
    {
        TextureKind kind = TextureKind::Image2D;
        TextureFormat format = TextureFormat::RGBA8;
        TextureColorSpace colorSpace = TextureColorSpace::SRGB;
        TextureMipIntent mips = TextureMipIntent::Generate;
        ImageOrientation orientation = ImageOrientation::BottomLeft;
        TextureUsage usage = TextureUsage::Sampled;
        int width = 0, height = 0;
        auto operator<=>(const TextureDesc&) const = default;
    };
    enum class TextureErrorCode { InvalidDescription, InvalidPixels, FileSystem, Decode, Allocation,
        Storage, Registry, ContextUnavailable, InvalidView, InvalidUnit, Font };
    struct TextureError
    {
        TextureErrorCode code;
        std::string source;
        std::string message;
        std::error_code system;
        RegistryError registry = RegistryError::InvalidHandle;
    };
    struct TexturePixels
    {
        std::span<const std::byte> bytes;
        std::size_t rowStride = 0; // 0 means tightly packed; rows have the descriptor's orientation.
    };
    namespace AssetDetail { struct TextureBackend; struct AttachmentState; }

    // Sole GPU image owner. Views and the legacy Texture binding below never own a name.
    class TextureResource final
    {
    public:
        TextureResource() noexcept;
        ~TextureResource();
        TextureResource(const TextureResource&) = delete;
        TextureResource& operator=(const TextureResource&) = delete;
        TextureResource(TextureResource&&) noexcept;
        TextureResource& operator=(TextureResource&&) noexcept;
        static std::expected<TextureResource, TextureError> Create(const TextureDesc&, TexturePixels = {});
        static std::expected<TextureResource, TextureError> Load(const std::filesystem::path&,
            const TextureDesc& = {}, const std::string& cubeExtension = ".png");
        const TextureDesc& Description() const noexcept { return m_Desc; }
        explicit operator bool() const noexcept;
    private:
        friend struct AssetDetail::TextureBackend;
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
        TextureDesc m_Desc;
    };
    using TextureRegistry = AssetRegistry<TextureHandle, TextureResource>;

    // Non-owning attachment observer. Its framebuffer must outlive all uses.
    // The backend queries the framebuffer at use time, so resize never caches a stale name.
    class AttachmentView final
    {
    public:
        AttachmentView() = default;
        explicit operator bool() const noexcept { return bool(m_State); }
    private:
        friend struct AssetDetail::TextureBackend;
        std::shared_ptr<const AssetDetail::AttachmentState> m_State;
    };
    class TextureView final
    {
    public:
        TextureView() = default;
        explicit TextureView(TextureRegistry::Lease lease) : m_Image(std::move(lease)) {}
        explicit TextureView(AttachmentView attachment) : m_Attachment(std::move(attachment)) {}
        TextureHandle Identity() const noexcept { return m_Image.Identity(); }
        std::uint64_t Revision() const noexcept { return m_Image.Revision(); }
        bool IsAttachment() const noexcept { return bool(m_Attachment); }
        explicit operator bool() const noexcept { return bool(m_Image) || bool(m_Attachment); }
        std::expected<void, TextureError> Bind(std::uint32_t unit) const;
    private:
        friend struct AssetDetail::TextureBackend;
        TextureRegistry::Lease m_Image;
        AttachmentView m_Attachment;
    };
    // Compatibility binding descriptor for existing pointer-facing material/UI callers.
    // Copying a descriptor retains a registry version; it never copies or deletes a GPU image.
    class Texture final
    {
    public:
        Texture(TextureView view, std::string uniformName) : m_View(std::move(view)), m_Name(std::move(uniformName)) {}
        const TextureView& View() const noexcept { return m_View; }
        const std::string& GetUniformName() const noexcept { return m_Name; }
    private:
        TextureView m_View;
        std::string m_Name;
    };
}
