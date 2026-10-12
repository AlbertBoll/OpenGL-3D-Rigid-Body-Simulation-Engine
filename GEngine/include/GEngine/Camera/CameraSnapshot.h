#pragma once

#include "Component/RenderComponents.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <cstdint>
#include <expected>
#include <string_view>

namespace GEngine
{
    enum class CameraErrorCode
    {
        EmptyViewport,
        InvalidViewport,
        InvalidProjection,
        InvalidPose,
        NonInvertible,
        NonFinite,
        OutsideViewport,
        BehindCamera,
        InvalidSlice
    };

    struct CameraError
    {
        CameraErrorCode code;
        std::string_view operation;
    };

    struct CameraViewport
    {
        // Origin and input positions share the caller's top-left logical coordinate space.
        // Pixel extent is actual allocated storage, including during a failed resize.
        glm::dvec2 logicalOrigin{};
        glm::dvec2 logicalSize{1280, 720};
        glm::uvec2 pixelOrigin{};
        glm::uvec2 pixelSize{1280, 720};
    };

    struct CameraSnapshotDesc
    {
        Component::CameraProjection projection = Component::CameraProjection::Perspective;
        double verticalFovRadians = 0.7853981633974483;
        double orthographicHeight = 10;
        double nearPlane = .1, farPlane = 1000;
        glm::dvec3 position{};
        // Camera-local-to-world orientation: local forward -Z, up +Y.
        glm::dquat orientation{1, 0, 0, 0};
        CameraViewport viewport;
        std::uint32_t visibleLayers = ~std::uint32_t{0};
    };

    struct CameraNdcPoint
    {
        glm::dvec3 value{};
        bool insideFrustum = false;
    };

    struct CameraPixelPoint
    {
        glm::dvec2 value{}; // Continuous, top-left, relative to the viewport.
        double ndcDepth{};
        bool insideFrustum = false;
    };

    struct CameraRay
    {
        // P(t) = origin + t * direction; direction is a normalized world vector.
        // Perspective intervals intersect clip planes (not scalar view depths off axis).
        // Orthographic origin is on the near plane, tNear=0, tFar reaches the far plane.
        glm::dvec3 origin{}, direction{};
        double tNear{}, tFar{};
    };

    // One validated value snapshot, independent of ECS, native windows and GPU state.
    // No setters: replacement creates a new value; retained frames keep their old values.
    class CameraSnapshot final
    {
    public:
        [[nodiscard]] static std::expected<CameraSnapshot, CameraError>
        Create(const CameraSnapshotDesc&) noexcept;

        const CameraSnapshotDesc& Description() const noexcept
        {
            return m_Desc;
        }
        // Float renderer representations; conversion/inversion math retains double
        // precision from the same validated descriptor. Neither is independently settable.
        const glm::mat4& View() const noexcept
        {
            return m_View;
        }
        const glm::mat4& Projection() const noexcept
        {
            return m_Projection;
        }
        const glm::dmat4& ViewProjection() const noexcept
        {
            return m_ViewProjection;
        }
        const glm::dmat4& InverseView() const noexcept
        {
            return m_InverseView;
        }
        const glm::dmat4& InverseProjection() const noexcept
        {
            return m_InverseProjection;
        }
        const glm::dmat4& InverseViewProjection() const noexcept
        {
            return m_InverseViewProjection;
        }
        const std::array<glm::dvec4, 6>& FrustumPlanes() const noexcept
        {
            return m_Planes;
        }

        [[nodiscard]] std::expected<glm::dvec3, CameraError>
        WorldToView(glm::dvec3 world) const noexcept;
        [[nodiscard]] std::expected<CameraNdcPoint, CameraError>
        WorldToNdc(glm::dvec3 world) const noexcept;
        [[nodiscard]] std::expected<CameraPixelPoint, CameraError>
        WorldToPixel(glm::dvec3 world) const noexcept;
        [[nodiscard]] std::expected<glm::dvec3, CameraError>
        NdcToWorld(glm::dvec3 ndc) const noexcept;
        [[nodiscard]] std::expected<glm::dvec2, CameraError>
        LogicalToPixel(glm::dvec2 logical) const noexcept;
        [[nodiscard]] std::expected<glm::uvec2, CameraError>
        ReadbackPixelAt(glm::dvec2 logical) const noexcept;
        [[nodiscard]] std::expected<CameraRay, CameraError>
        PixelToRay(glm::dvec2 continuousPixel) const noexcept;
        [[nodiscard]] std::expected<CameraRay, CameraError>
        PixelCenterToRay(glm::uvec2 rasterPixel) const noexcept;
        // Eight world corners, in x/y/z order, at the requested signed view depths.
        [[nodiscard]] std::expected<std::array<glm::dvec3, 8>, CameraError>
        SliceCorners(double nearDepth, double farDepth) const noexcept;

    private:
        CameraSnapshot() = default;
        CameraSnapshotDesc m_Desc;
        glm::mat4 m_View{1}, m_Projection{1};
        glm::dmat4 m_PreciseView{1}, m_ViewProjection{1}, m_InverseView{1}, m_InverseProjection{1};
        glm::dmat4 m_InverseViewProjection{1};
        std::array<glm::dvec4, 6> m_Planes{};
    };
}
