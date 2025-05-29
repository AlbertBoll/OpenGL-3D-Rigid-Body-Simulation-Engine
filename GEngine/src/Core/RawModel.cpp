#include "gepch.h"
#include "Core/RawModel.h"
#include "assimp/Importer.hpp"
#include <Assimp/scene.h>
#include <Assimp/postprocess.h>
#include "Geometry/Geometry.h"

namespace GEngine
{


	RawModel::RawModel(const std::string& path)
	{
		LoadModelGeometry(path);
	}

	void RawModel::LoadModelGeometry(const std::string& path)
	{
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
		// check for errors
		ASSERT(!(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode), std::string("ERROR::ASSIMP:: ") + importer.GetErrorString());
		//auto node = scene->mRootNode;
		//aiMesh* mesh = scene->mMeshes[node->mMeshes[0]];
		//return ProcessMesh(mesh, scene);

			// retrieve the directory path of the filepath
			//m_Directory = path.substr(0, path.find_last_of('/'));
			//ProcessNode(scene->mRootNode, scene);
		ProcessNode(scene->mRootNode, scene);
	};


	Geometry* RawModel::ProcessMesh(aiMesh* mesh, const aiScene* scene)
	{

		auto ModelGeometry = new Geometry;
		std::vector<Vec3f> vertexPosition;
		std::vector<Vec2f> vertexUV;
		std::vector<Vec3f> vertexNormal;
		std::vector<Vec3f> vertexTangent;
		std::vector<Vec3f> vertexBiTangent;
		std::vector<unsigned int> vertexIndices;

		for (unsigned int i = 0; i < mesh->mNumVertices; i++)
		{
			//glm::vec3 vertex;
			Vec3f vector; // we declare a placeholder vector since assimp uses its own vector class that doesn't directly convert to glm's vec3 class so we transfer the data to this placeholder glm::vec3 first.

			// positions
			vector.x = mesh->mVertices[i].x;
			vector.y = mesh->mVertices[i].y;
			vector.z = mesh->mVertices[i].z;
			vertexPosition.emplace_back(vector);

			// normals
			if (mesh->HasNormals())
			{
				vector.x = mesh->mNormals[i].x;
				vector.y = mesh->mNormals[i].y;
				vector.z = mesh->mNormals[i].z;
				vertexNormal.emplace_back(vector);
			}

			// texture coordinates
			if (mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
			{
				glm::vec2 vec{};
				// a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
				// use models where a vertex can have multiple texture coordinates so we always take the first set (0).
				vec.x = mesh->mTextureCoords[0][i].x;
				vec.y = mesh->mTextureCoords[0][i].y;
				vertexUV.emplace_back(vec);

				// tangent
				vector.x = mesh->mTangents[i].x;
				vector.y = mesh->mTangents[i].y;
				vector.z = mesh->mTangents[i].z;
				vertexTangent.emplace_back(vector);

				// bitangent
				vector.x = mesh->mBitangents[i].x;
				vector.y = mesh->mBitangents[i].y;
				vector.z = mesh->mBitangents[i].z;
				vertexBiTangent.emplace_back(vector);
			}
			else
				vertexUV.emplace_back(0.0f, 0.0f);
		}

		// now walk through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
		for (unsigned int i = 0; i < mesh->mNumFaces; i++)
		{
			const aiFace face = mesh->mFaces[i];
			// retrieve all indices of the face and store them in the indices vector
			for (unsigned int j = 0; j < face.mNumIndices; j++)
				vertexIndices.push_back(face.mIndices[j]);
		}

		ModelGeometry->AddAttributes(vertexPosition, vertexUV, vertexNormal, vertexTangent, vertexBiTangent);
		ModelGeometry->AddIndices(vertexIndices);
		return ModelGeometry;

	}

	void RawModel::ProcessNode(aiNode* node, const aiScene* scene)
	{
		// process each mesh located at the current node
		for (unsigned int i = 0; i < node->mNumMeshes; i++)
		{
			// the node object only contains indices to index the actual objects in the scene. 
			// the scene contains all the data, node is just to keep stuff organized (like relations between nodes).
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			m_Geometries.push_back(ProcessMesh(mesh, scene));
		}

		// after we've processed all of the meshes (if any) we then recursively process each of the children nodes
		for (unsigned int i = 0; i < node->mNumChildren; i++)
		{
			ProcessNode(node->mChildren[i], scene);
		}
	}

	Geometry* RawModel::GetGeometry(int index)
	{
		return m_Geometries[index];
	}

	//void RawModel::ProcessNode(aiNode* node, const aiScene* scene)
	//{
		// process each mesh located at the current node
		//for (unsigned int i = 0; i < node->mNumMeshes; i++)
		//{
			// the node object only contains indices to index the actual objects in the scene. 
			// the scene contains all the data, node is just to keep stuff organized (like relations between nodes).
			//aiMesh* mesh = scene->mMeshes[node->mMeshes[0]];
			//return ProcessMesh(mesh, scene)
			//m_Geometries.push_back(ProcessMesh(mesh, scene));
		//}

		// after we've processed all of the meshes (if any) we then recursively process each of the children nodes
		//for (unsigned int i = 0; i < node->mNumChildren; i++)
		//{
			//ProcessNode(node->mChildren[i], scene);
		//}
	//}

	



}