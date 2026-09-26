#pragma once

#include <cmath>
#include <expected>
#include <limits>
#include <string_view>

namespace GEngine::Shape
{
    enum class PointLightSegmentErrorCode { NotRepresentable };
    enum class PointLightSegmentAxis { Radius, Height };
    struct PointLightSegmentError
    {
        PointLightSegmentErrorCode code;
        PointLightSegmentAxis axis;
        float value;
        std::string_view operation;
        std::string_view message;
    };

    // Validated conversion only: preserve the retained helper's int domain and
    // truncation toward zero. Geometry/tessellation policy is unchanged.
    class PointLightSegments final
    {
    public:
        constexpr PointLightSegments() noexcept = default;
        [[nodiscard]] static std::expected<PointLightSegments, PointLightSegmentError>
            Create(float radius = 32.f, float height = 32.f) noexcept
        {
            if (!Representable(radius)) return Reject(PointLightSegmentAxis::Radius, radius);
            if (!Representable(height)) return Reject(PointLightSegmentAxis::Height, height);
            return PointLightSegments(static_cast<int>(radius), static_cast<int>(height));
        }
        constexpr int Radius() const noexcept { return m_Radius; }
        constexpr int Height() const noexcept { return m_Height; }

    private:
        constexpr PointLightSegments(int radius, int height) noexcept : m_Radius(radius), m_Height(height) {}
        static bool Representable(float value) noexcept
        {
            return std::isfinite(value)
                && static_cast<double>(value) >= (std::numeric_limits<int>::min)()
                && static_cast<double>(value) <= (std::numeric_limits<int>::max)();
        }
        static std::unexpected<PointLightSegmentError> Reject(PointLightSegmentAxis axis, float value) noexcept
        {
            return std::unexpected(PointLightSegmentError{PointLightSegmentErrorCode::NotRepresentable,
                axis, value, "PointLightSegments::Create", "Sphere segment count is not representable as int"});
        }
        int m_Radius = 32;
        int m_Height = 32;
    };
}
