#include "gepch.h"
#include "Shape.h"
#include "Math/Math.h"
#include "ShapeSphere.h"
#include "ShapeBox.h"
#include "ShapeConvex.h"
#include "ShapeCapsule.h"
#include <new>
#include <type_traits>

namespace GEngine
{
	using namespace Math;
    namespace
    {
        using ShapeOwner = std::expected<std::unique_ptr<PhysicalShape>, PhysicsShapeError>;
        auto ShapeFailure(PhysicsShapeErrorCode code, std::string_view message)
        { return std::unexpected(PhysicsShapeError{code, "CreateCollisionShape", message}); }

        template<class T> ShapeOwner OwnShape(std::expected<T, PhysicsShapeError> shape)
        {
            if (!shape) return std::unexpected(shape.error());
            std::unique_ptr<PhysicalShape> owner(new (std::nothrow) T(std::move(*shape)));
            if (!owner) return ShapeFailure(PhysicsShapeErrorCode::Allocation, "Shape allocation failed");
            return owner;
        }

        bool UsableInertia(const PhysicalShape& shape)
        {
            const auto tensor = shape.InertiaTensor();
            for (int column = 0; column < 3; ++column)
                if (!Math::IsFinite(tensor[column])) return false;
            // Positive definite, finite normalized-mass tensor; do not alter the
            // solver's existing float determinant/cache policy or mass scaling.
            const glm::dmat3 wide(tensor);
            const double minor = wide[0][0]*wide[1][1] - wide[0][1]*wide[1][0];
            const double determinant = glm::determinant(wide);
            if (wide[0][0] <= 0 || minor <= 0 || !std::isfinite(determinant) || determinant <= 0)
                return false;
            const auto inverse = glm::inverse(wide);
            for (int column = 0; column < 3; ++column)
                if (!Math::IsFinite(Vec3f(inverse[column]))) return false;
            return true;
        }
    }

    std::expected<std::unique_ptr<PhysicalShape>, PhysicsShapeError>
    CreateCollisionShape(const CollisionShapeDesc& description, const Vec3f& scale)
    {
        if (!Math::IsFinite(scale))
            return ShapeFailure(PhysicsShapeErrorCode::InvalidScale, "Shape scale must be finite");
        if (description.valueless_by_exception())
            return ShapeFailure(PhysicsShapeErrorCode::InvalidDimensions, "Missing shape description");
        auto result = std::visit([&](const auto& desc) -> ShapeOwner {
            using T = std::decay_t<decltype(desc)>;
            if constexpr (std::is_same_v<T, BoxShapeDesc>)
            {
                if (!Math::IsFinite(desc.halfExtents) || !Math::IsFinite(desc.center) ||
                    glm::any(glm::lessThanEqual(desc.halfExtents, Vec3f(0))))
                    return ShapeFailure(PhysicsShapeErrorCode::InvalidDimensions,
                                        "Box requires finite center and positive half extents");
                std::vector<Vec3f> points;
                points.reserve(8);
                for (unsigned i = 0; i < 8; ++i)
                    points.push_back((desc.center + desc.halfExtents *
                        Vec3f(i&1 ? 1.f : -1.f, i&2 ? 1.f : -1.f, i&4 ? 1.f : -1.f)) * scale);
                return OwnShape(ShapeBox::Create(points));
            }
            else if constexpr (std::is_same_v<T, SphereShapeDesc>)
            {
                if (!std::isfinite(desc.radius) || desc.radius <= 0)
                    return ShapeFailure(PhysicsShapeErrorCode::InvalidRadius,
                                        "Sphere requires a finite positive radius");
                return OwnShape(ShapeSphere::Create(desc.radius * scale.x));
            }
            else if constexpr (std::is_same_v<T, CapsuleShapeDesc>)
            {
                auto capsule = ShapeCapsule::Create(desc.radius, desc.halfLength);
                if (!capsule) return std::unexpected(capsule.error());
                if (auto scaled = capsule->SetScale(scale); !scaled)
                    return std::unexpected(scaled.error());
                return OwnShape(std::move(capsule));
            }
            else
            {
                auto points = desc.points;
                for (auto& point : points) point *= scale;
                std::unique_ptr<PhysicalShape> shape(new (std::nothrow) ShapeConvex(points));
                if (!shape) return ShapeFailure(PhysicsShapeErrorCode::Allocation, "Shape allocation failed");
                if (!shape->IsValid())
                    return ShapeFailure(PhysicsShapeErrorCode::InvalidPointSet,
                                        "ConvexHull requires a finite nondegenerate three-dimensional point set");
                return shape;
            }
        }, description);
        if (!result) return result;
        const auto bounds = (*result)->GetBounds();
        if (!Math::IsFinite(bounds.mins) || !Math::IsFinite(bounds.maxs) ||
            !Math::IsFinite((*result)->GetCenterOfMass()) || !UsableInertia(**result))
            return ShapeFailure(PhysicsShapeErrorCode::InvalidInertia,
                                "Shape bounds, center or usable inertia are not representable");
        return result;
    }

	void PhysicalShape::HandleScaleChanged(const Vec3f& new_scale)
	{
		if (!Math::IsFinite(new_scale)) {
			return;
		}
		auto pts = m_MeshPoints;
		for (auto& pt : pts)
		{
			pt *= new_scale;
			if (!Math::IsFinite(pt)) {
				return;
			}
		}
		// Build installs its input as the source for explicit geometry replacement.
		// A scale rebuild retains the previous unscaled source on every exit.
		std::vector<Vec3f> basePoints;
		basePoints.swap(m_MeshPoints);
		struct RestoreSource
		{
			std::vector<Vec3f>& current;
			std::vector<Vec3f>& original;
			~RestoreSource() { current.swap(original); }
		} restore{m_MeshPoints, basePoints};
		Build(pts);
	}

}
