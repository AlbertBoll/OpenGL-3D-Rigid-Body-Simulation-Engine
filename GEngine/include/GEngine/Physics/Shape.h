#pragma once
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include "Bounds.h"
#include "PhysicsShapeError.h"
#include <cstdint>
#include <expected>
#include <memory>
#include <variant>



namespace GEngine
{
	//using namespace Component;
	enum class ShapeType
	{
		Invalid = -1,
		Sphere = 0,
		Box = 1,
		Convex = 2,
        Capsule = 3
	};

    // CPU values own their inputs. Visual meshes and GPU resources are unrelated.
    struct BoxShapeDesc { Vec3f halfExtents{.5f}; Vec3f center{}; };
    struct SphereShapeDesc { float radius{1}; };
    // Local Y axis; halfLength excludes the two hemispherical end caps.
    struct CapsuleShapeDesc { float radius{1}; float halfLength{.5f}; };
    struct ConvexHullShapeDesc { std::vector<Vec3f> points; };
    using CollisionShapeDesc = std::variant<BoxShapeDesc, SphereShapeDesc,
                                           CapsuleShapeDesc, ConvexHullShapeDesc>;

	class PhysicalShape
	{
	public:
		PhysicalShape() = default;
        // Concrete shape payloads retire through the existing polymorphic owner boundary.
        virtual ~PhysicalShape() = default;
        PhysicalShape(const PhysicalShape&) = default;
        PhysicalShape& operator=(const PhysicalShape&) = default;
        PhysicalShape(PhysicalShape&&) noexcept = default;
        PhysicalShape& operator=(PhysicalShape&&) noexcept = default;
		PhysicalShape(const std::vector<Vec3f>& MeshPoints) : m_MeshPoints(MeshPoints) {};
		virtual Mat3 InertiaTensor() const = 0;
		virtual Bounds GetBounds(const Vec3f& pos, const glm::quat& orient) const = 0;
		virtual Bounds GetBounds() const = 0;
		// A successful explicit Build replaces the unscaled source geometry.
		virtual void Build(const std::vector<Vec3f>& pts) {};
		// Rebuild at an absolute component-wise scale without replacing that source.
		virtual void HandleScaleChanged(const Vec3f& new_scale);
		//virtual void HandleScaleChanged(float new_scale) {};
		virtual Vec3f GetCenterOfMass() const { return m_CenterOfMass; }
		virtual Vec3f Support(const Vec3f& dir, const Vec3f& pos, const Quat& orient, const float bias) const = 0;
		// Angular velocity and unit search direction are both in model space, about the shape COM.
		virtual float FastestLinearSpeed(const Vec3f& angularVelocity, const Vec3f& dir) const { return 0.0f; }
		virtual bool IsValid() const { return m_ShapeType != ShapeType::Invalid; }

		void SetShapeType(ShapeType type) { m_ShapeType = type; }
		ShapeType GetShapeType() const { return m_ShapeType; }
		std::uint64_t GetRevision() const { return m_Revision; }


	protected:
		void MarkGeometryChanged() { ++m_Revision; }

		Vec3f m_CenterOfMass{ 0.0f };
		std::vector<Vec3f> m_MeshPoints; // Owned base points, preserved across scale callbacks.
		ShapeType m_ShapeType = ShapeType::Invalid;
		std::uint64_t m_Revision{};

	};

    // Box/hull scale is component-wise; Sphere retains its existing X-axis policy.
    // Capsule accepts positive uniform scale only. A rejected description publishes
    // no shape. PhysicsWorld::CreateBody adopts the returned owner only on success.
    [[nodiscard]] std::expected<std::unique_ptr<PhysicalShape>, PhysicsShapeError>
    CreateCollisionShape(const CollisionShapeDesc&, const Vec3f& scale = Vec3f{1});
}
