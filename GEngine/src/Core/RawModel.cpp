#include "gepch.h"
#include "Core/RawModel.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "Geometry/Geometry.h"
#include <new>

namespace GEngine
{
    namespace
    {
        ModelImportError ImportError(ModelImportErrorCode code, const std::string& path, std::string message)
        { return {code, "RawModel::Create", path, std::move(message)}; }

        std::expected<std::unique_ptr<Geometry>, ModelImportError> ProcessMesh(
            const aiMesh& source, const std::string& path)
        {
            const auto* mesh = &source;
            if (!mesh->mNumVertices || !mesh->mVertices
                || (mesh->mNumFaces && !mesh->mFaces)
                || (mesh->mTextureCoords[0] && (!mesh->mTangents || !mesh->mBitangents)))
                return std::unexpected(ImportError(ModelImportErrorCode::InvalidData, path,
                    "Model mesh has incomplete vertex, face or tangent data"));
            for (unsigned i = 0; i < mesh->mNumFaces; ++i)
            {
                const auto& face = mesh->mFaces[i];
                if (face.mNumIndices && !face.mIndices)
                    return std::unexpected(ImportError(ModelImportErrorCode::InvalidData, path,
                        "Model face has no index data"));
                for (unsigned j = 0; j < face.mNumIndices; ++j)
                    if (face.mIndices[j] >= mesh->mNumVertices)
                        return std::unexpected(ImportError(ModelImportErrorCode::InvalidData, path,
                            "Model face index is outside its vertex data"));
            }
            std::unique_ptr<Geometry> ModelGeometry(new (std::nothrow) Geometry);
            if (!ModelGeometry)
                return std::unexpected(ImportError(ModelImportErrorCode::Allocation, path,
                    "Unable to allocate model geometry"));
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

        std::expected<void, ModelImportError> ProcessNode(const aiNode& node, const aiScene& scene,
            const std::string& path, std::vector<std::unique_ptr<Geometry>>& output)
        {
            if ((node.mNumMeshes && !node.mMeshes) || (node.mNumChildren && !node.mChildren))
                return std::unexpected(ImportError(ModelImportErrorCode::InvalidData, path,
                    "Model node has incomplete mesh or child data"));
            for (unsigned i = 0; i < node.mNumMeshes; ++i)
            {
                const auto index = node.mMeshes[i];
                if (index >= scene.mNumMeshes || !scene.mMeshes || !scene.mMeshes[index])
                    return std::unexpected(ImportError(ModelImportErrorCode::InvalidData, path,
                        "Model node references an invalid mesh"));
                auto mesh = ProcessMesh(*scene.mMeshes[index], path);
                if (!mesh) return std::unexpected(mesh.error());
                output.push_back(std::move(*mesh));
            }
            for (unsigned i = 0; i < node.mNumChildren; ++i)
            {
                if (!node.mChildren[i])
                    return std::unexpected(ImportError(ModelImportErrorCode::InvalidData, path,
                        "Model node references a missing child"));
                if (auto child = ProcessNode(*node.mChildren[i], scene, path, output); !child) return child;
            }
            return {};
        }

        std::expected<std::vector<std::unique_ptr<Geometry>>, ModelImportError> BuildModel(
            const aiScene& scene, const std::string& path)
        {
            if ((scene.mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene.mRootNode)
                return std::unexpected(ImportError(ModelImportErrorCode::InvalidScene, path,
                    "ERROR::ASSIMP:: Model scene is incomplete or has no root node"));
            std::vector<std::unique_ptr<Geometry>> geometries;
            if (auto nodes = ProcessNode(*scene.mRootNode, scene, path, geometries); !nodes)
                return std::unexpected(nodes.error());
            if (geometries.empty())
                return std::unexpected(ImportError(ModelImportErrorCode::NoGeometry, path,
                    "Model contains no geometry: " + path));
            return geometries;
        }
    }

    std::expected<RawModel, ModelImportError> RawModel::Create(const std::string& path)
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
            return std::unexpected(ImportError(!scene ? ModelImportErrorCode::ImportFailed : ModelImportErrorCode::InvalidScene,
                path, std::string("ERROR::ASSIMP:: ") + importer.GetErrorString()));
        auto geometries = BuildModel(*scene, path);
        if (!geometries) return std::unexpected(geometries.error());
        RawModel model;
        model.m_Geometries = std::move(*geometries);
        return model;
    }

    RawModel::RawModel(RawModel&&) noexcept = default;
    RawModel& RawModel::operator=(RawModel&&) noexcept = default;
    RawModel::~RawModel() = default;
    std::vector<std::unique_ptr<Geometry>> RawModel::TakeGeometries() && { return std::move(m_Geometries); }
}
