#pragma once
#include <type_traits>
#include <Math/Math.h>

namespace GEngine
{
	using namespace Math;
	template<typename T>
	concept FloatOrDouble = std::is_same_v<T, float> || std::is_same_v<T, double>;

	template<FloatOrDouble T>
	struct Point3D
	{
		Vec3f m_Position{};
		//int id = -1; // -1 means not assigned

		Point3D() = default;
		Point3D(const Vec3f& position) : m_Position(position){}

		float operator[](int index) const
		{
			switch (index)
			{
			case 0: return m_Position.x;
			case 1: return m_Position.y;
			case 2: return m_Position.z;
			default: throw std::out_of_range("Index out of range for Point3D");
			}
		}

		float LengthSquared() const
		{
			return glm::length2(m_Position);
		}

		operator Vec3f() const
		{
			return m_Position;
		}


	};

	struct KDNode
	{
		Point3D<float> m_Point; // Point in 3D space
		int m_Axis{ 0 }; // Axis along which the node is split (0: x, 1: y, 2: z)
		//float m_SplitValue{ 0.0f }; // Value at which the node is split

		KDNode* m_Left{ nullptr };
		KDNode* m_Right{ nullptr };

		Vec3f m_MinBounds; // Bounding box min corner
		Vec3f m_MaxBounds; // Bounding box max corner
		
		KDNode() = default;
		KDNode(const Point3D<float>& point) : m_Point(point) {}
		KDNode(const Point3D<float>& point, int axis, const Vec3f& minBounds, const Vec3f& maxBounds)
			: m_Point(point), m_Axis(axis), m_MinBounds(minBounds), m_MaxBounds(maxBounds) {
		}

	};

	//template<FloatOrDouble T>
	class KDTree
	{
	public:
		KDTree() = default;
		KDTree(const std::vector<Point3D<float>>& points);
		~KDTree();
		//std::vector<Point3D<float>> Search(const Vec3f& target, float radius) const;
		void ConstructKDTree(std::vector<Point3D<float>>& points);
		void ClearNode();

		void CollectBoxes(std::vector<Vec3f>& boxes);

	private:
		KDNode* m_Root{ nullptr };

		void Destroy(KDNode* node);
		KDNode* BuildRecursive(const std::vector<Point3D<float>>& points, int depth, const Vec3f& minBounds, const Vec3f& maxBounds);
		// Helper to compute bounding box
		void ComputeBounds(const std::vector<Point3D<float>>& points, glm::vec3& minBounds, glm::vec3& maxBounds);
		KDNode* Build(std::vector<Point3D<float>>& points, const Vec3f& min_bound, const Vec3f& max_bound, int depth);
		void CollectBoxesImpl(KDNode* n, std::vector<Vec3f>& boxes);


	};




}
