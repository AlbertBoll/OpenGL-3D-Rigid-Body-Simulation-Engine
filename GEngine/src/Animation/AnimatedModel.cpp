#include "gepch.h"
#include "Animation/AnimatedModel.h"
#include <assimp/postprocess.h>
#include <assimp/Importer.hpp>
#include <Assimp/scene.h>
#include "Geometry/Geometry.h"
#include "Extras/AssimpGLMHelpers.h"

namespace GEngine
{
	AnimatedModel::AnimatedModel(const std::string& path)
	{
		LoadModel(path);
	}
	void AnimatedModel::LoadModel(const std::string& path)
	{

		// read file via ASSIMP
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
		ASSERT(!(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode), std::string("ERROR::ASSIMP:: ") + importer.GetErrorString());

		ProcessNode(scene->mRootNode, scene);
	}

	void AnimatedModel::ProcessNode(aiNode* node, const aiScene* scene)
	{
		for (unsigned int i = 0; i < node->mNumMeshes; ++i)
		{
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			m_Geometries.push_back(ProcessMesh(mesh, scene));
		}

		// after we've processed all of the meshes (if any) we then recursively process each of the children nodes
		for (unsigned int i = 0; i < node->mNumChildren; i++)
		{
			ProcessNode(node->mChildren[i], scene);
		}
	}

	Geometry* AnimatedModel::ProcessMesh(aiMesh* mesh, const aiScene* scene)
	{
		auto ModelGeometry = new Geometry;

		auto size = mesh->mNumVertices;

		std::vector<Vec3f> vertexPosition;

		std::vector<Vec2f> vertexUV;
		std::vector<Vec3f> vertexNormal;
		std::vector<Vec3f> vertexTangent;
		std::vector<Vec3f> vertexBiTangent;
		std::vector<Vec4i> boneIds(size, { -1, -1, -1, -1 });
		std::vector<Vec4f> weights(size, { 0.f, 0.f, 0.f, 0.f });

		vertexPosition.reserve(size);
		vertexUV.reserve(size);
		vertexNormal.reserve(size);
		vertexTangent.reserve(size);
		vertexBiTangent.reserve(size);




		std::vector<unsigned int> vertexIndices;

		for (unsigned int i = 0; i < size; ++i)
		{
			vertexPosition.emplace_back(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
			vertexNormal.emplace_back(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
			// texture coordinates
			if (mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
			{
				vertexUV.emplace_back(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
				vertexTangent.emplace_back(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z);
				vertexBiTangent.emplace_back(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z);
			}
		}

		for (unsigned int i = 0; i < mesh->mNumFaces; i++)
		{
			aiFace face = mesh->mFaces[i];
			for (unsigned int j = 0; j < face.mNumIndices; j++)
				vertexIndices.push_back(face.mIndices[j]);
		}

		ExtractBoneWeightForVertices(boneIds, weights, mesh);
		ModelGeometry->AddAttributes(vertexPosition, vertexUV, vertexNormal, vertexTangent, vertexBiTangent, boneIds, weights);
		ModelGeometry->AddIndices(vertexIndices);
		return ModelGeometry;

	}

	void AnimatedModel::ExtractBoneWeightForVertices(std::vector<Vec4i>& bone_ids, std::vector<Vec4f>& weights, aiMesh* mesh)
	{
		auto& boneInfoMap = m_BoneInfo;
		int& boneCount = m_BoneCounter;

		for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
		{
			int boneID = -1;
			std::string boneName = mesh->mBones[boneIndex]->mName.C_Str();
			if (boneInfoMap.find(boneName) == boneInfoMap.end())
			{
				BoneInfo newBoneInfo;
				newBoneInfo.id = boneCount;
				newBoneInfo.offset = AssimpGLMHelpers::ConvertMatrixToGLMFormat(mesh->mBones[boneIndex]->mOffsetMatrix);
				boneInfoMap[boneName] = newBoneInfo;
				boneID = boneCount++;
				//boneCount++;
			}

			else
			{
				boneID = boneInfoMap[boneName].id;
			}

			ASSERT(boneID != -1);

			auto _weights = mesh->mBones[boneIndex]->mWeights;
			int numWeights = mesh->mBones[boneIndex]->mNumWeights;

			for (int weightIndex = 0; weightIndex < numWeights; ++weightIndex)
			{
				int vertexId = _weights[weightIndex].mVertexId;
				float weight = _weights[weightIndex].mWeight;
				ASSERT(vertexId <= bone_ids.size());

				//auto& boneID = bone_ids[vertexId];
				//auto& weight = weights[vertexId];
				SetVertexBoneData(bone_ids, weights, vertexId, boneID, weight);


			}
		}


	}

	void GEngine::AnimatedModel::SetVertexBoneData(std::vector<Vec4i>& bone_ids, std::vector<Vec4f>& weights, int vertexID, int boneID, float weight)
	{
		for (int i = 0; i < MAX_BONE_INFLUENCE; ++i)
		{
			if (bone_ids[vertexID][i] < 0)
			{
				weights[vertexID][i] = weight;
				bone_ids[vertexID][i] = boneID;
				break;
			}
		}
	}

}
