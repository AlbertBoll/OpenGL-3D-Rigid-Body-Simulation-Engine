#pragma once
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include "Bounds.h"
#include <Component/Component.h>



namespace GEngine
{
	//using namespace Component;
	enum class ShapeType
	{
		Sphere = 0,
		Box = 1,
		Convex = 2
	};

	class PhysicalShape
	{
	public:
		PhysicalShape() = default;
		PhysicalShape(const std::vector<Vec3f>& MeshPoints) : m_MeshPoints(MeshPoints) {};
		virtual Mat3 InertiaTensor() const = 0;
		virtual Bounds GetBounds(const Vec3f& pos, const glm::quat& orient) const = 0;
		virtual Bounds GetBounds() const = 0;
		virtual void Build(const std::vector<Vec3f>& pts) {};
		virtual void HandleScaleChanged(const Vec3f& new_scale);
		//virtual void HandleScaleChanged(float new_scale) {};
		virtual Vec3f GetCenterOfMass() const { return m_CenterOfMass; }
		virtual Vec3f Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const = 0;
		virtual float FastestLinearSpeed(const Vec3f& angularVelocity, const Vec3f& dir) const { return 0.0f; }

		void SetShapeType(ShapeType type) { m_ShapeType = type; }
		ShapeType GetShapeType() const { return m_ShapeType; }


	protected:
		Vec3f m_CenterOfMass;
		std::vector<Vec3f> m_MeshPoints;
		ShapeType m_ShapeType;

	};
}