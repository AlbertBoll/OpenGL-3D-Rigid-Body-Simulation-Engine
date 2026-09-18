#include "gepch.h"
#include "Renderer/RenderVisibility.h"
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace GEngine
{
    CameraFrustum CameraFrustum::FromCamera(const FrameCamera& camera) noexcept
    {
        CameraFrustum result;
        const glm::dmat4 clip = glm::dmat4(camera.projection) * glm::dmat4(camera.view);
        const double determinant = glm::determinant(clip);
        if (!std::isfinite(determinant) || determinant == 0) return result;
        const auto row = [&](int i) { return glm::dvec4(clip[0][i], clip[1][i], clip[2][i], clip[3][i]); };
        for (int axis = 0; axis < 3; ++axis)
            for (int side = 0; side < 2; ++side)
            {
                auto plane = row(3) + (side ? -row(axis) : row(axis));
                const double length = std::hypot(plane.x, plane.y, plane.z);
                if (!std::isfinite(length) || length == 0 || !std::isfinite(plane.w)) return result;
                plane /= length;
                if (!std::isfinite(plane.w)) return result;
                result.m_Planes[axis * 2 + side] = plane;
            }
        result.m_Valid = true;
        return result;
    }

    BoundsVisibility CameraFrustum::Test(const WorldBounds& bounds) const noexcept
    {
        if (!m_Valid || !bounds.CanCull()) return BoundsVisibility::Conservative;
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis])
                || bounds.minimum[axis] > bounds.maximum[axis]) return BoundsVisibility::Conservative;
        bool outside = false, intersects = false;
        for (const auto& plane : m_Planes)
        {
            double lower = plane.w, upper = plane.w, magnitude = std::abs(plane.w);
            for (int axis = 0; axis < 3; ++axis)
            {
                const double a = plane[axis] * bounds.minimum[axis], b = plane[axis] * bounds.maximum[axis];
                lower += (std::min)(a, b); upper += (std::max)(a, b);
                magnitude += (std::max)(std::abs(a), std::abs(b));
            }
            // Cover float camera/vertex arithmetic at submission as well as the
            // double plane test. Uncertain boundary cases are always retained.
            const double tolerance = 32 * std::numeric_limits<float>::epsilon() * (1 + magnitude);
            if (!std::isfinite(lower) || !std::isfinite(upper) || !std::isfinite(tolerance))
                return BoundsVisibility::Conservative;
            outside |= upper < -tolerance;
            intersects |= lower <= tolerance;
        }
        return outside ? BoundsVisibility::Outside
            : intersects ? BoundsVisibility::Intersecting : BoundsVisibility::Inside;
    }

    void RenderVisibility::Swap(RenderVisibility& other) noexcept
    {
        using std::swap;
        swap(m_Indices, other.m_Indices); swap(m_Counts, other.m_Counts);
        swap(m_Capacity, other.m_Capacity); swap(m_Camera, other.m_Camera); swap(m_Stats, other.m_Stats);
    }
    RenderVisibility::RenderVisibility(RenderVisibility&& other) noexcept { Swap(other); }
    RenderVisibility& RenderVisibility::operator=(RenderVisibility&& other) noexcept
    {
        if (this != &other) { RenderVisibility old(std::move(other)); Swap(old); }
        return *this;
    }
    std::span<const std::size_t> RenderVisibility::List(std::size_t index) const noexcept
    { return {m_Indices ? m_Indices.get() + index * m_Capacity : nullptr, m_Counts[index]}; }
    void RenderVisibility::Append(std::size_t list, std::size_t draw) noexcept
    { m_Indices[list * m_Capacity + m_Counts[list]++] = draw; }

    std::expected<RenderVisibility, VisibilityError> RenderVisibility::Build(
        const RenderFrame& frame, std::size_t cameraIndex, std::span<const std::size_t> conservativeDraws) noexcept
    {
        if (cameraIndex >= frame.Cameras().size())
            return std::unexpected(VisibilityError{VisibilityErrorCode::InvalidCamera, cameraIndex});
        RenderVisibility result;
        const auto count = frame.Draws().size();
        for (std::size_t i = 0; i < conservativeDraws.size(); ++i)
            if (conservativeDraws[i] >= count || (i && conservativeDraws[i] <= conservativeDraws[i-1]))
                return std::unexpected(VisibilityError{VisibilityErrorCode::InvalidConservativeDraw, i});
        if (count > static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)()) / (6 * sizeof(std::size_t)))
            return std::unexpected(VisibilityError{VisibilityErrorCode::CapacityOverflow, count});
        if (count)
        {
            result.m_Indices.reset(new (std::nothrow) std::size_t[count * 6]);
            if (!result.m_Indices) return std::unexpected(VisibilityError{VisibilityErrorCode::AllocationFailed, count});
        }
        result.m_Capacity = count; result.m_Camera = cameraIndex; result.m_Stats.inputDraws = count;
        const auto& camera = frame.Cameras()[cameraIndex];
        const auto frustum = CameraFrustum::FromCamera(camera);
        std::size_t conservativeIndex = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& draw = frame.Draws()[i];
            const bool conservative = conservativeIndex < conservativeDraws.size() && conservativeDraws[conservativeIndex] == i;
            if (conservative) ++conservativeIndex;
            if (!draw.submesh.elementCount) { ++result.m_Stats.emptyDraws; continue; }
            if (draw.castShadows) result.Append(4, i);
            if (!(draw.layers & camera.visibleLayers)) { ++result.m_Stats.layerRejected; continue; }
            ++result.m_Stats.tested;
            const auto& resources = frame.Resources()[draw.resources];
            const auto visibility = conservative ? BoundsVisibility::Conservative
                : frustum.Test(TransformBounds(resources.Mesh()->Bounds(), draw.worldTransform));
            if (visibility == BoundsVisibility::Outside) { ++result.m_Stats.culled; continue; }
            ++result.m_Stats.visible;
            if (visibility == BoundsVisibility::Conservative) ++result.m_Stats.conservative;
            result.Append(0, i);
            switch (resources.Material().Pipeline().Alpha())
            {
            case AlphaMode::Opaque: result.Append(1, i); break;
            case AlphaMode::Masked: result.Append(2, i); break;
            case AlphaMode::Transparent: result.Append(3, i); break;
            }
            if (draw.pickable) result.Append(5, i);
        }
        return result;
    }
}
