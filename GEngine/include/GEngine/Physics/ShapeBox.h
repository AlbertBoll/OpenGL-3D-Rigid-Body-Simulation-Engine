#pragma once
#include "Shape.h"

#include <array>
#include <stdexcept>

namespace GEngine
{
	// Local axis/sign IDs survive input point reordering and valid geometry rebuilds.
	// Cached geometry must additionally match the shape identity and revision.
	enum class BoxFaceId : std::uint8_t
	{
		NegativeX, PositiveX, NegativeY, PositiveY, NegativeZ, PositiveZ, Invalid
	};

	inline constexpr float BoxFaceAlignmentTolerance = 1.0e-5f;

	struct BoxFaceFeature
	{
		BoxFaceId id = BoxFaceId::Invalid;
		std::uint64_t shapeRevision{};
		Vec3f normal{ 0.0f }; // Outward unit world normal.
		std::array<Vec3f, 4> vertices{}; // World vertices, CCW when viewed from outside.
		float alignment{}; // Dot of normal with the normalized selection direction.
	};


	class ShapeBox : public PhysicalShape
	{
	public:
		ShapeBox() = delete;
		explicit ShapeBox(const std::vector<Vec3f>& pts): PhysicalShape(pts) {
			Build(pts);
			m_ShapeType = ShapeType::Box;
			if (!IsValid()) {
				throw std::invalid_argument("ShapeBox requires finite points with non-zero extents");
			}
		}
		void Build(const std::vector<Vec3f>& pts);
		static bool IsValidPointSet(const std::vector<Vec3f>& pts);
		bool IsValid() const override;

		// Select the face whose outward normal best aligns with a finite nonzero world direction.
		// Within the dimensionless alignment tolerance, prefer local X, then Y, then Z.
		// Pose orientation must be unit length (squared-length tolerance 1e-4).
		// Returns false without changing output for invalid or unrepresentable geometry/pose.
		bool GetContactFace(const Vec3f& direction, const Vec3f& position,
			const Quat& orientation, BoxFaceFeature& output) const;

		Vec3f Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const override;
		//void HandleScaleChanged(const Vec3f& new_scale) override;
		Mat3 InertiaTensor() const override;

		Bounds GetBounds(const Vec3f& pos, const Quat& orient) const override;
		Bounds GetBounds() const override { return m_bounds; }

		float FastestLinearSpeed(const Vec3f& angularVelocity, const Vec3f& dir) const override;

	private:
		std::vector<Vec3f> m_points;
		Bounds m_bounds;
	};

}

