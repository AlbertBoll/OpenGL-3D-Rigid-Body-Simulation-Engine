#pragma once
#include <Animation/BoneInfo.h>
#include <string>
#include <unordered_map>
#include "Math/Math.h"

constexpr int MAX_BONE_INFLUENCE = 4;

struct aiMesh;
struct aiScene;
struct aiNode;

namespace GEngine
{
	using namespace Math;
	class Geometry;

	class AnimatedModel
	{
	public:
		AnimatedModel(const std::string& path);
		auto& GetBoneInfoMap() { return m_BoneInfo; }
		int& GetBoneCount() { return m_BoneCounter; }

		auto& GetGeometries() { return m_Geometries; }


	private:
		void LoadModel(const std::string& path);
		void ProcessNode(aiNode* node, const aiScene* scene);
		Geometry* ProcessMesh(aiMesh* mesh, const aiScene* scene);
		void ExtractBoneWeightForVertices(std::vector<Vec4i>& bone_ids, std::vector<Vec4f>& weights, aiMesh* mesh);
		void SetVertexBoneData(std::vector<Vec4i>& bone_ids, std::vector<Vec4f>& weights, int vertexID, int boneID, float weight);

	private:
		std::unordered_map<std::string, BoneInfo> m_BoneInfo;
		std::vector<Geometry*> m_Geometries;
		int m_BoneCounter = 0;
	};

}