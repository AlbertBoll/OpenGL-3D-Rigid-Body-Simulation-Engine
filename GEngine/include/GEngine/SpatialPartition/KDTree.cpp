#include "gepch.h"
#include "KDTree.h"
#include <algorithm>

namespace GEngine
{
	
    KDTree::KDTree(const std::vector<Point3D<float>>& points)
    {
		//m_Root = Build(points, 0);
    }


    KDTree::~KDTree()
    {
        // Implement a destructor to free the KDTree nodes
        // This is a simple recursive deletion
		Destroy(m_Root);
        
	}

    
    void KDTree::ConstructKDTree(std::vector<Point3D<float>>& points)
    {
        if (points.empty()) {
            return;
        }
        /*if(m_Root)
            Destroy(m_Root);*/
        Vec3f minBounds, maxBounds;
        ComputeBounds(points, minBounds, maxBounds);
        m_Root = Build(points, minBounds, maxBounds, 0);
    }

    void KDTree::ClearNode()
    {
		Destroy(m_Root);
		m_Root = nullptr;
    }

   
    void KDTree::CollectBoxes(std::vector<Vec3f>& boxes)
    {
        //boxes.clear();
		//boxes.resize(0);
		CollectBoxesImpl(m_Root, boxes);
       
      
    }

 
    void KDTree::Destroy(KDNode* node)
    {
        if (node)
        {
            Destroy(node->m_Left);
            Destroy(node->m_Right);
            delete node;
			//node = nullptr;
        }
    }


    KDNode* KDTree::BuildRecursive(const std::vector<Point3D<float>>& points, int depth, const Vec3f& minBounds, const Vec3f& maxBounds)
    {
        return nullptr;
    }


    void KDTree::ComputeBounds(const std::vector<Point3D<float>>& points, glm::vec3& minBounds, glm::vec3& maxBounds)
    {
      /*  if (points.empty()) {
            return;
        }*/

        minBounds = maxBounds = points[0];
        for (size_t i = 1; i < points.size(); ++i) {
            minBounds.x = std::min(minBounds.x, Vec3f(points[i]).x);
            minBounds.y = std::min(minBounds.y, Vec3f(points[i]).y);
            minBounds.z = std::min(minBounds.z, Vec3f(points[i]).z);
            maxBounds.x = std::max(maxBounds.x, Vec3f(points[i]).x);
            maxBounds.y = std::max(maxBounds.y, Vec3f(points[i]).y);
            maxBounds.z = std::max(maxBounds.z, Vec3f(points[i]).z);
        }
    }

    KDNode* KDTree::Build(std::vector<Point3D<float>>& points, const Vec3f& min_bound, const Vec3f& max_bound, int depth)
    {
        if( points.empty())
			return nullptr;

        //Vec3f minBounds;
		//Vec3f maxBounds;

        //ComputeBounds(points, minBounds, maxBounds);
        //m_Root = BuildRecursive(points, 0, minBounds, maxBounds);


        // Sort points by the current dimension
        int axis = depth % 3;
		size_t mid = points.size() / 2;
        std::nth_element(points.begin(), points.begin() + mid, points.end(), [axis](const Point3D<float>& a, const Point3D<float>& b) {
            return a[axis] < b[axis];
        });
       /* std::sort(points.begin(), points.end(), [axis](const Point3D<float>& a, const Point3D<float>& b) {
            return a[axis] < b[axis];
            });*/

        // Find the median point
        size_t medianIndex = points.size() / 2;
        KDNode* node = new KDNode(points[medianIndex], axis, min_bound, max_bound );
        //node->m_Point = points[medianIndex];
        
        // Recursively build the left and right subtrees
        std::vector<Point3D<float>> leftPoints(points.begin(), points.begin() + medianIndex);
        std::vector<Point3D<float>> rightPoints(points.begin() + medianIndex + 1, points.end());
        Vec3f leftMax = max_bound; leftMax[axis] = node->m_Point[axis];
        Vec3f rightMin = min_bound; rightMin[axis] = node->m_Point[axis];
        node->m_Left = Build(leftPoints, min_bound, leftMax, depth + 1);
        node->m_Right = Build(rightPoints, rightMin, max_bound, depth + 1);
		return node;
    }

   
    void KDTree::CollectBoxesImpl(KDNode* n, std::vector<Vec3f>& boxes)
    {
       /* if (!n) {
            return;
        }

        Vec3f min = n->m_MinBounds, max = n->m_MaxBounds;
        Vec3f c[8] = {
            {min.x,min.y,min.z}, {max.x,min.y,min.z}, {max.x,max.y,min.z}, {min.x,max.y,min.z},
            {min.x,min.y,max.z}, {max.x,min.y,max.z}, {max.x,max.y,max.z}, {min.x,max.y,max.z}
        };
        int e[24] = { 0,1,1,2,2,3,3,0,4,5,5,6,6,7,7,4,0,4,1,5,2,6,3,7 };
        for (int i = 0; i < 24; i += 2)
            boxes.insert(boxes.end(), { Vec3f{c[e[i]].x,c[e[i]].y,c[e[i]].z}, Vec3f{c[e[i + 1]].x,c[e[i + 1]].y,c[e[i + 1]].z } });
        CollectBoxesImpl(n->m_Left, boxes);
        CollectBoxesImpl(n->m_Right, boxes);*/

        if (!n) return;
        int axis = n->m_Axis;
      
        Vec3f min = n->m_MinBounds;
        Vec3f max = n->m_MaxBounds;

        Vec3f corners[4];
        if (axis == 0) {
            corners[0] = Vec3f(n->m_Point[axis], min.y, min.z);
            corners[1] = Vec3f(n->m_Point[axis], max.y, min.z);
            corners[2] = Vec3f(n->m_Point[axis], max.y, max.z);
            corners[3] = Vec3f(n->m_Point[axis], min.y, max.z);
        }
        else if (axis == 1) {
            corners[0] = Vec3f(min.x, n->m_Point[axis], min.z);
            corners[1] = Vec3f(max.x, n->m_Point[axis], min.z);
            corners[2] = Vec3f(max.x, n->m_Point[axis], max.z);
            corners[3] = Vec3f(min.x, n->m_Point[axis], max.z);
        }
        else {
            corners[0] = Vec3f(min.x, min.y, n->m_Point[axis]);
            corners[1] = Vec3f(max.x, min.y, n->m_Point[axis]);
            corners[2] = Vec3f(max.x, max.y, n->m_Point[axis]);
            corners[3] = Vec3f(min.x, max.y, n->m_Point[axis]);
        }

        // Rectangle edges
        for (int i = 0; i < 4; ++i) {
            Vec3f a = corners[i];
            Vec3f b = corners[(i + 1) % 4];
            boxes.insert(boxes.end(), { a, b });
            //boxes.insert(boxes.end(), { b.x, b.y, b.z });
        }

        CollectBoxesImpl(n->m_Left, boxes);
        CollectBoxesImpl(n->m_Right, boxes);

    }

	//template class KDTree<float>;
	//template class KDTree<double>;
}
