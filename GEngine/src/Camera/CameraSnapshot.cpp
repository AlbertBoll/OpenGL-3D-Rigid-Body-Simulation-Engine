#include "gepch.h"
#include "Camera/CameraSnapshot.h"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace GEngine
{
    namespace
    {
        using Code = CameraErrorCode;

        std::unexpected<CameraError> Error(Code code, std::string_view operation) noexcept
        {
            return std::unexpected(CameraError{code, operation});
        }

        template <glm::length_t N, typename T, glm::qualifier Q>
        bool Finite(const glm::vec<N, T, Q>& value) noexcept
        {
            for (glm::length_t i = 0; i < N; ++i)
                if (!std::isfinite(value[i]))
                    return false;
            return true;
        }

        template <typename T> bool Finite(const glm::mat<4, 4, T>& matrix) noexcept
        {
            for (int i = 0; i < 4; ++i)
                if (!Finite(matrix[i]))
                    return false;
            return true;
        }

        std::expected<glm::dvec3, CameraError> Divide(glm::dvec4 value) noexcept
        {
            if (!Finite(value) || value.w == 0)
                return Error(Code::NonInvertible, "homogeneous division");
            const glm::dvec3 result = glm::dvec3(value) / value.w;
            if (!Finite(result))
                return Error(Code::NonFinite, "homogeneous result");
            return result;
        }

        std::expected<glm::dmat4, CameraError> Invert(const glm::dmat4& value) noexcept
        {
            const double determinant = glm::determinant(value);
            if (!std::isfinite(determinant) || determinant == 0)
                return Error(Code::NonInvertible, "camera matrix determinant");
            const auto inverse = glm::inverse(value);
            if (!Finite(inverse))
                return Error(Code::NonInvertible, "camera matrix inverse");
            return inverse;
        }
    }

    std::expected<CameraSnapshot, CameraError>
    CameraSnapshot::Create(const CameraSnapshotDesc& desc) noexcept
    {
        const auto& viewport = desc.viewport;
        if (!Finite(viewport.logicalOrigin) || !Finite(viewport.logicalSize) ||
            viewport.logicalSize.x < 0 || viewport.logicalSize.y < 0 ||
            viewport.pixelOrigin.x > 8192 || viewport.pixelOrigin.y > 8192 ||
            viewport.pixelSize.x > 8192 - viewport.pixelOrigin.x ||
            viewport.pixelSize.y > 8192 - viewport.pixelOrigin.y)
            return Error(Code::InvalidViewport, "camera viewport");
        if (viewport.logicalSize.x == 0 || viewport.logicalSize.y == 0 ||
            viewport.pixelSize.x == 0 || viewport.pixelSize.y == 0)
            return Error(Code::EmptyViewport, "inactive camera viewport");
        if (!Finite(desc.position) || !std::isfinite(desc.orientation.w) ||
            !std::isfinite(desc.orientation.x) || !std::isfinite(desc.orientation.y) ||
            !std::isfinite(desc.orientation.z))
            return Error(Code::InvalidPose, "camera pose");
        const auto q = desc.orientation;
        const double scale =
            (std::max)({std::abs(q.w), std::abs(q.x), std::abs(q.y), std::abs(q.z)});
        if (scale == 0)
            return Error(Code::InvalidPose, "zero camera orientation");
        const auto normalized = glm::normalize(q / scale);
        if (!std::isfinite(desc.nearPlane) || !std::isfinite(desc.farPlane) ||
            desc.nearPlane >= desc.farPlane)
            return Error(Code::InvalidProjection, "camera clip interval");
        const double aspect = double(viewport.pixelSize.x) / viewport.pixelSize.y;
        glm::dmat4 projection;
        if (desc.projection == Component::CameraProjection::Perspective)
        {
            if (!std::isfinite(desc.verticalFovRadians) || desc.verticalFovRadians <= 0 ||
                desc.verticalFovRadians >= std::numbers::pi || desc.nearPlane <= 0)
                return Error(Code::InvalidProjection, "perspective camera");
            projection = glm::perspectiveRH_NO(desc.verticalFovRadians, aspect, desc.nearPlane,
                                               desc.farPlane);
        }
        else if (desc.projection == Component::CameraProjection::Orthographic)
        {
            if (!std::isfinite(desc.orthographicHeight) || desc.orthographicHeight <= 0)
                return Error(Code::InvalidProjection, "orthographic camera");
            const double halfHeight = desc.orthographicHeight * .5;
            projection = glm::orthoRH_NO(-halfHeight * aspect, halfHeight * aspect, -halfHeight,
                                         halfHeight, desc.nearPlane, desc.farPlane);
        }
        else
            return Error(Code::InvalidProjection, "unknown camera projection");

        CameraSnapshot result;
        result.m_Desc = desc;
        result.m_Desc.orientation = normalized;
        result.m_PreciseView = glm::mat4_cast(glm::conjugate(normalized)) *
                               glm::translate(glm::dmat4(1), -desc.position);
        result.m_View = glm::mat4(result.m_PreciseView);
        result.m_Projection = glm::mat4(projection);
        if (desc.projection == Component::CameraProjection::Perspective)
        {
            // The far plane solves (A + 1) * z + B = 0. Rounding A and B
            // independently amplifies their error through cancellation in A + 1.
            // Derive B from the rounded A and the requested endpoint in double
            // precision, then round once for the renderer representation.
            result.m_Projection[3][2] =
                static_cast<float>((double(result.m_Projection[2][2]) + 1) * desc.farPlane);
        }
        if (!Finite(result.m_View) || !Finite(result.m_Projection) ||
            !Finite(glm::vec3(desc.position)))
            return Error(Code::NonFinite, "renderer camera representation");
        // Keep conversion math in double precision. The renderer matrices above are
        // derived representations of this same descriptor, never independent inputs.
        result.m_ViewProjection = projection * result.m_PreciseView;
        auto inverseView = Invert(result.m_PreciseView);
        auto inverseProjection = Invert(projection);
        auto inverseViewProjection = Invert(result.m_ViewProjection);
        if (!inverseView)
            return std::unexpected(inverseView.error());
        if (!inverseProjection)
            return std::unexpected(inverseProjection.error());
        if (!inverseViewProjection)
            return std::unexpected(inverseViewProjection.error());
        result.m_InverseView = *inverseView;
        result.m_InverseProjection = *inverseProjection;
        result.m_InverseViewProjection = *inverseViewProjection;

        const auto row = [&](int i)
        {
            const auto& m = result.m_ViewProjection;
            return glm::dvec4(m[0][i], m[1][i], m[2][i], m[3][i]);
        };
        for (int axis = 0; axis < 3; ++axis)
            for (int side = 0; side < 2; ++side)
            {
                auto plane = row(3) + (side ? -row(axis) : row(axis));
                const double length = std::hypot(plane.x, plane.y, plane.z);
                if (!std::isfinite(length) || length == 0)
                    return Error(Code::NonInvertible, "camera frustum plane");
                plane /= length;
                if (!Finite(plane))
                    return Error(Code::NonFinite, "camera frustum plane");
                result.m_Planes[axis * 2 + side] = plane;
            }
        if (!Invert(glm::dmat4(result.m_View)) || !Invert(glm::dmat4(result.m_Projection)) ||
            (desc.projection == Component::CameraProjection::Perspective &&
             result.m_Projection[2][2] == -1))
            return Error(Code::NonInvertible, "renderer camera precision");
        // Reject descriptors whose derived matrices cannot represent a finite frustum.
        for (int x = -1; x <= 1; x += 2)
            for (int y = -1; y <= 1; y += 2)
                for (int z = -1; z <= 1; z += 2)
                    if (auto corner = result.NdcToWorld({x, y, z}); !corner)
                        return std::unexpected(corner.error());
        return result;
    }

    std::expected<glm::dvec3, CameraError>
    CameraSnapshot::WorldToView(glm::dvec3 world) const noexcept
    {
        if (!Finite(world))
            return Error(Code::NonFinite, "world-to-view input");
        return Divide(m_PreciseView * glm::dvec4(world, 1));
    }

    std::expected<CameraNdcPoint, CameraError>
    CameraSnapshot::WorldToNdc(glm::dvec3 world) const noexcept
    {
        if (!Finite(world))
            return Error(Code::NonFinite, "world-to-NDC input");
        const auto clip = m_ViewProjection * glm::dvec4(world, 1);
        if (m_Desc.projection == Component::CameraProjection::Perspective && clip.w <= 0)
            return Error(Code::BehindCamera, "world-to-NDC behind camera");
        auto value = Divide(clip);
        if (!value)
            return std::unexpected(value.error());
        const bool inside = value->x >= -1 && value->x <= 1 && value->y >= -1 && value->y <= 1 &&
                            value->z >= -1 && value->z <= 1;
        return CameraNdcPoint{*value, inside};
    }

    std::expected<CameraPixelPoint, CameraError>
    CameraSnapshot::WorldToPixel(glm::dvec3 world) const noexcept
    {
        auto ndc = WorldToNdc(world);
        if (!ndc)
            return std::unexpected(ndc.error());
        const auto size = glm::dvec2(m_Desc.viewport.pixelSize);
        return CameraPixelPoint{
            {(ndc->value.x + 1) * .5 * size.x, (1 - ndc->value.y) * .5 * size.y},
            ndc->value.z,
            ndc->insideFrustum};
    }

    std::expected<glm::dvec3, CameraError> CameraSnapshot::NdcToWorld(glm::dvec3 ndc) const noexcept
    {
        if (!Finite(ndc))
            return Error(Code::NonFinite, "NDC-to-world input");
        return Divide(m_InverseViewProjection * glm::dvec4(ndc, 1));
    }

    std::expected<glm::dvec2, CameraError>
    CameraSnapshot::LogicalToPixel(glm::dvec2 logical) const noexcept
    {
        const auto& viewport = m_Desc.viewport;
        const auto relative = logical - viewport.logicalOrigin;
        if (!Finite(relative) || relative.x < 0 || relative.y < 0 ||
            relative.x >= viewport.logicalSize.x || relative.y >= viewport.logicalSize.y)
            return Error(Code::OutsideViewport, "logical camera position");
        return relative * glm::dvec2(viewport.pixelSize) / viewport.logicalSize;
    }

    std::expected<glm::uvec2, CameraError>
    CameraSnapshot::ReadbackPixelAt(glm::dvec2 logical) const noexcept
    {
        auto pixel = LogicalToPixel(logical);
        if (!pixel)
            return std::unexpected(pixel.error());
        const auto& viewport = m_Desc.viewport;
        // Check the converted value as well: never clamp a rounded-outside boundary.
        if (pixel->x >= viewport.pixelSize.x || pixel->y >= viewport.pixelSize.y)
            return Error(Code::OutsideViewport, "readback camera position");
        return viewport.pixelOrigin +
               glm::uvec2(static_cast<std::uint32_t>(std::floor(pixel->x)),
                          viewport.pixelSize.y - 1 -
                              static_cast<std::uint32_t>(std::floor(pixel->y)));
    }

    std::expected<CameraRay, CameraError>
    CameraSnapshot::PixelToRay(glm::dvec2 pixel) const noexcept
    {
        const auto size = glm::dvec2(m_Desc.viewport.pixelSize);
        if (!Finite(pixel) || pixel.x < 0 || pixel.y < 0 || pixel.x >= size.x || pixel.y >= size.y)
            return Error(Code::OutsideViewport, "camera ray pixel");
        const glm::dvec2 xy(2 * pixel.x / size.x - 1, 1 - 2 * pixel.y / size.y);
        auto nearPoint = NdcToWorld({xy, -1});
        auto farPoint = NdcToWorld({xy, 1});
        if (!nearPoint)
            return std::unexpected(nearPoint.error());
        if (!farPoint)
            return std::unexpected(farPoint.error());
        CameraRay ray;
        const bool perspective = m_Desc.projection == Component::CameraProjection::Perspective;
        ray.origin = perspective ? m_Desc.position : *nearPoint;
        const auto delta = *farPoint - ray.origin;
        const double length = std::hypot(delta.x, delta.y, delta.z);
        if (!std::isfinite(length) || length == 0)
            return Error(Code::NonInvertible, "camera ray direction");
        ray.direction = delta / length;
        ray.tNear = perspective ? glm::dot(*nearPoint - ray.origin, ray.direction) : 0;
        ray.tFar = glm::dot(*farPoint - ray.origin, ray.direction);
        if (!std::isfinite(ray.tNear) || !std::isfinite(ray.tFar) || ray.tNear < 0 ||
            ray.tFar <= ray.tNear)
            return Error(Code::NonInvertible, "camera ray interval");
        return ray;
    }

    std::expected<CameraRay, CameraError>
    CameraSnapshot::PixelCenterToRay(glm::uvec2 raster) const noexcept
    {
        return PixelToRay(glm::dvec2(raster) + .5);
    }

    std::expected<std::array<glm::dvec3, 8>, CameraError>
    CameraSnapshot::SliceCorners(double nearDepth, double farDepth) const noexcept
    {
        if (!std::isfinite(nearDepth) || !std::isfinite(farDepth) || nearDepth >= farDepth ||
            nearDepth < m_Desc.nearPlane || farDepth > m_Desc.farPlane)
            return Error(Code::InvalidSlice, "camera frustum slice interval");
        std::array<glm::dvec3, 8> corners;
        std::size_t index = 0;
        for (int x = -1; x <= 1; x += 2)
            for (int y = -1; y <= 1; y += 2)
            {
                auto nearPoint = NdcToWorld({x, y, -1});
                auto farPoint = NdcToWorld({x, y, 1});
                if (!nearPoint)
                    return std::unexpected(nearPoint.error());
                if (!farPoint)
                    return std::unexpected(farPoint.error());
                const double start = -(m_PreciseView * glm::dvec4(*nearPoint, 1)).z;
                const double end = -(m_PreciseView * glm::dvec4(*farPoint, 1)).z;
                if (!std::isfinite(start) || !std::isfinite(end) || end <= start)
                    return Error(Code::NonInvertible, "camera frustum slice depth");
                for (double depth : {nearDepth, farDepth})
                {
                    const auto point =
                        *nearPoint + (*farPoint - *nearPoint) * ((depth - start) / (end - start));
                    if (!Finite(point))
                        return Error(Code::NonFinite, "camera frustum slice corner");
                    corners[index++] = point;
                }
            }
        return corners;
    }
}
