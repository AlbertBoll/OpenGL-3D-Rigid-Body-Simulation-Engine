#pragma once

#include "Assets/AsyncUploadQueue.h"
#include "Assets/Textures/Texture.h"
#include <variant>

namespace GEngine::Asset
{
    using AsyncTextureError = std::variant<TextureError, UploadError>;
    struct AsyncTextureStatus
    {
        AsyncAssetState state = AsyncAssetState::Requested;
        TextureHandle image{}; // Present only when Ready. Keep the caller's fallback otherwise.
        TextureDesc description;
        std::optional<AsyncTextureError> error;
    };
    struct AsyncTextureLimits
    {
        // Covers encoded input, decoder scratch and retained pixels together.
        std::size_t requestBytes = 64 * 1024 * 1024;
        UploadLimits queue{64, 2, 4, 256 * 1024 * 1024, 8, 128 * 1024 * 1024,
            64 * 1024 * 1024, std::chrono::milliseconds(2)};
    };
    struct AsyncTextureStats
    {
        std::size_t fileReads{}, decodes{}, uploads{}, peakRequestBytes{};
    };
    std::string DescribeAsyncTextureError(const AsyncTextureError&);

    // Owner-thread facade over Phase 49's queue. No file access occurs in Request.
    // File canonicalization/open/read/decode happen on workers; ready images are
    // created and published only by the scheduler's UpdateFrameResources drain.
    // This phase selects 2D R8/RGB8/RGBA8 files. Cube/float/depth/attachment paths
    // keep their existing synchronous API. Output is tightly packed, explicitly
    // top- or bottom-left, with the requested color space and mip intent unchanged.
    //
    // Exact lexical requests reuse a ticket. Canonical aliases (including hard
    // links and Windows file-name aliases) coalesce on workers by open-file identity
    // PLUS the complete resource descriptor. Aliases share one decode/upload and
    // its terminal diagnostics. Cancel cancels that shared operation; cancellation
    // of an unresolved spelling cancels only that spelling. Ready/Uploading is Busy.
    //
    // Cache metadata/tickets are bounded by queue.requests for this loader lifetime;
    // exhaustion is a typed Capacity error, never unbounded retention. Registry
    // images remain registry-owned. Shut down the loader before closing its registry
    // or context. Registry and publication domain must be the same caller-owned pair.
    class AsyncTextureLoader final
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<AsyncTextureLoader>, AsyncTextureError> Create(
            AssetPublication&, TextureRegistry&, std::filesystem::path imageRoot, AsyncTextureLimits = {});
        ~AsyncTextureLoader();
        AsyncTextureLoader(const AsyncTextureLoader&) = delete;
        AsyncTextureLoader& operator=(const AsyncTextureLoader&) = delete;
        [[nodiscard]] std::expected<UploadTicket, AsyncTextureError> Request(
            const std::filesystem::path&, const TextureDesc& = {}, const std::string& extension = ".png");
        [[nodiscard]] std::expected<AsyncTextureStatus, UploadError> Status(UploadTicket) const;
        UploadResult Cancel(UploadTicket);
        void Shutdown();
        AsyncUploadQueue& Queue() noexcept;
        AsyncTextureStats Stats() const;
    private:
        struct Impl;
        explicit AsyncTextureLoader(std::unique_ptr<Impl>);
        std::unique_ptr<Impl> m_Impl;
    };
}
