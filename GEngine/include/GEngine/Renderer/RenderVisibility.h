#pragma once

#include "Renderer/RenderFrame.h"
#include "Scene/RenderState.h"
#include <array>

namespace GEngine
{
    enum class BoundsVisibility { Inside, Intersecting, Outside, Conservative };

    // Engine column-vector clip convention: -w <= x,y,z <= w. Invalid or
    // degenerate cameras/bounds keep candidates; touching planes never reject.
    class CameraFrustum final
    {
    public:
        static CameraFrustum FromCamera(const FrameCamera&) noexcept;
        BoundsVisibility Test(const WorldBounds&) const noexcept;
    private:
        std::array<glm::dvec4, 6> m_Planes{};
        bool m_Valid = false;
    };

    struct VisibilityStats
    {
        std::size_t inputDraws{}, emptyDraws{}, layerRejected{};
        // tested = visible + culled; visible includes conservative fallbacks.
        // inputDraws = emptyDraws + layerRejected + tested.
        std::size_t tested{}, visible{}, culled{}, conservative{};
    };
    enum class VisibilityErrorCode { InvalidCamera, InvalidConservativeDraw, CapacityOverflow, AllocationFailed };
    struct VisibilityError { VisibilityErrorCode code; std::size_t element{}; };

    // CPU-only relevance stage over a finalized frame, safe for concurrent const
    // readers. Bounds derive from its retained mesh version and presentation pose;
    // classification derives from its retained pipeline. No ECS/registry lookup,
    // GL calls, asset copies, occlusion, sorting or frame mutation occurs.
    //
    // Lists contain frame-local draw ordinals, NOT resource/entity identities.
    // Consume them with the same originating frame, retained through submission.
    // Each list preserves source order, including transparent draws; compositing
    // order is a later ordering-stage responsibility. Main is the ordered union
    // of Opaque/Masked/Transparent. Picking intersects Main with pickable.
    // Shadows contains every nonempty castShadows draw, independently of camera
    // frustum AND camera layer filtering (even a zero draw layer mask). Disabled
    // ECS entries were already omitted by extraction; material shadow policy was
    // already intersected by the frame builder. Light/cascade culling is later.
    // conservativeDraws is an optional strictly increasing list of draw ordinals
    // whose mesh bounds do not describe their shader output (e.g. camera-relative
    // sky or vertex deformation). Callers identify these from their pass contract;
    // they bypass geometry rejection, still obey layers, and count as conservative.
    //
    // The result owns only index storage, no frame/resource borrows. Its const
    // spans expire on move/replacement/destruction. Moved-from lists are empty.
    class RenderVisibility final
    {
    public:
        [[nodiscard]] static std::expected<RenderVisibility, VisibilityError> Build(
            const RenderFrame&, std::size_t camera = 0,
            std::span<const std::size_t> conservativeDraws = {}) noexcept;
        RenderVisibility(const RenderVisibility&) = delete;
        RenderVisibility& operator=(const RenderVisibility&) = delete;
        RenderVisibility(RenderVisibility&&) noexcept;
        RenderVisibility& operator=(RenderVisibility&&) noexcept;
        std::span<const std::size_t> Main() const & noexcept { return List(0); }
        std::span<const std::size_t> Opaque() const & noexcept { return List(1); }
        std::span<const std::size_t> Masked() const & noexcept { return List(2); }
        std::span<const std::size_t> Transparent() const & noexcept { return List(3); }
        std::span<const std::size_t> Shadows() const & noexcept { return List(4); }
        std::span<const std::size_t> Picking() const & noexcept { return List(5); }
        std::span<const std::size_t> Main() const && = delete;
        std::span<const std::size_t> Opaque() const && = delete;
        std::span<const std::size_t> Masked() const && = delete;
        std::span<const std::size_t> Transparent() const && = delete;
        std::span<const std::size_t> Shadows() const && = delete;
        std::span<const std::size_t> Picking() const && = delete;
        VisibilityStats Stats() const noexcept { return m_Stats; }
        std::size_t CameraIndex() const noexcept { return m_Camera; }
        std::size_t StorageBytes() const noexcept { return m_Capacity * 6 * sizeof(std::size_t); }
    private:
        RenderVisibility() = default;
        void Swap(RenderVisibility&) noexcept;
        std::span<const std::size_t> List(std::size_t index) const noexcept;
        void Append(std::size_t list, std::size_t draw) noexcept;
        std::unique_ptr<std::size_t[]> m_Indices;
        std::array<std::size_t, 6> m_Counts{};
        std::size_t m_Capacity{}, m_Camera{};
        VisibilityStats m_Stats;
    };
}
