#pragma once
#include "Shape.h"

#include <stdexcept>

namespace GEngine
{

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

