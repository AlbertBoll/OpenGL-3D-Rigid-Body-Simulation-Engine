#include "gepch.h"
#include "Animation/AnimatedModel.h"
#include <assimp/postprocess.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include "Geometry/Geometry.h"
#include "Extras/AssimpGLMHelpers.h"
#include <limits>
#include <new>

namespace GEngine
{
    namespace
    {
        struct AnimatedImportState
        {
            std::unordered_map<std::string, BoneInfo> bones;
            std::vector<std::unique_ptr<Geometry>> geometries;
            int boneCount = 0;
        };
        ModelImportError AnimatedImportError(ModelImportErrorCode code, const std::string& path, std::string message)
        { return {code, "AnimatedModel::Create", path, std::move(message)}; }

        std::expected<void, ModelImportError> ReadBoneWeights(std::vector<Vec4i>& boneIds,
            std::vector<Vec4f>& weights, const aiMesh& mesh, AnimatedImportState& state, const std::string& path)
        {
            if (mesh.mNumBones && !mesh.mBones)
                return std::unexpected(AnimatedImportError(ModelImportErrorCode::InvalidData, path, "Model mesh has no bone data"));
            for (unsigned boneIndex = 0; boneIndex < mesh.mNumBones; ++boneIndex)
            {
                const auto* bone = mesh.mBones[boneIndex];
                if (!bone || (bone->mNumWeights && !bone->mWeights))
                    return std::unexpected(AnimatedImportError(ModelImportErrorCode::InvalidData, path, "Model bone has incomplete weight data"));
                const std::string boneName = bone->mName.C_Str();
                int boneID;
                const auto found = state.bones.find(boneName);
                if (found == state.bones.end())
                {
                    if (state.boneCount == (std::numeric_limits<int>::max)())
                        return std::unexpected(AnimatedImportError(ModelImportErrorCode::InvalidData, path, "Model bone identifier exceeds its supported range"));
                    BoneInfo info;
                    info.id = state.boneCount;
                    info.offset = AssimpGLMHelpers::ConvertMatrixToGLMFormat(bone->mOffsetMatrix);
                    state.bones.emplace(boneName, info);
                    boneID = state.boneCount++;
                }
                else boneID = found->second.id;
                if (boneID < 0)
                    return std::unexpected(AnimatedImportError(ModelImportErrorCode::InvalidData, path, "Model bone has an invalid identifier: " + boneName));
                for (unsigned weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex)
                {
                    const auto vertexID = bone->mWeights[weightIndex].mVertexId;
                    if (vertexID >= boneIds.size())
                        return std::unexpected(AnimatedImportError(ModelImportErrorCode::InvalidData, path,
                            "Model bone weight vertex is outside its mesh: bone=" + boneName
                            + " vertex=" + std::to_string(vertexID) + " vertices=" + std::to_string(boneIds.size())));
                    // Preserve the existing first-four-influence selection and weight values.
                    for (int slot = 0; slot < MAX_BONE_INFLUENCE; ++slot)
                        if (boneIds[vertexID][slot] < 0)
                        {
                            weights[vertexID][slot] = bone->mWeights[weightIndex].mWeight;
                            boneIds[vertexID][slot] = boneID;
                            break;
                        }
                }
            }
            return {};
        }

        std::expected<std::unique_ptr<Geometry>, ModelImportError> ReadAnimatedMesh(
            const aiMesh& source, AnimatedImportState& state, const std::string& path)
        {
            const auto* mesh = &source;
            if (!mesh->mNumVertices || !mesh->mVertices || !mesh->mNormals
                || (mesh->mNumFaces && !mesh->mFaces)
                || (mesh->mTextureCoords[0] && (!mesh->mTangents || !mesh->mBitangents)))
                return std::unexpected(AnimatedImportError(ModelImportErrorCode::InvalidData, path,
                    "Model mesh has incomplete vertex, normal, face or tangent data"));
            for (unsigned i = 0; i < mesh->mNumFaces; ++i)
            {
                const auto& face = mesh->mFaces[i];
                if (face.mNumIndices && !face.mIndices)
                    return std::unexpected(AnimatedImportError(ModelImportErrorCode::InvalidData, path, "Model face has no index data"));
                for (unsigned j = 0; j < face.mNumIndices; ++j)
                    if (face.mIndices[j] >= mesh->mNumVertices)
                        return std::unexpected(AnimatedImportError(ModelImportErrorCode::InvalidData, path, "Model face index is outside its vertex data"));
            }
            std::unique_ptr<Geometry> ModelGeometry(new (std::nothrow) Geometry);
            if (!ModelGeometry)
                return std::unexpected(AnimatedImportError(ModelImportErrorCode::Allocation, path, "Unable to allocate model geometry"));
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

		if (auto bones = ReadBoneWeights(boneIds, weights, *mesh, state, path); !bones)
			return std::unexpected(bones.error());
		ModelGeometry->AddAttributes(vertexPosition, vertexUV, vertexNormal, vertexTangent, vertexBiTangent, boneIds, weights);
		ModelGeometry->AddIndices(vertexIndices);
		return ModelGeometry;

        }

        std::expected<void, ModelImportError> ReadAnimatedNode(const aiNode& node, const aiScene& scene,
            AnimatedImportState& state, const std::string& path)
        {
            if ((node.mNumMeshes && !node.mMeshes) || (node.mNumChildren && !node.mChildren))
                return std::unexpected(AnimatedImportError(ModelImportErrorCode::InvalidData, path, "Model node has incomplete mesh or child data"));
            for (unsigned i = 0; i < node.mNumMeshes; ++i)
            {
                const auto index = node.mMeshes[i];
                if (index >= scene.mNumMeshes || !scene.mMeshes || !scene.mMeshes[index])
                    return std::unexpected(AnimatedImportError(ModelImportErrorCode::InvalidData, path, "Model node references an invalid mesh"));
                auto mesh = ReadAnimatedMesh(*scene.mMeshes[index], state, path);
                if (!mesh) return std::unexpected(mesh.error());
                state.geometries.push_back(std::move(*mesh));
            }
            for (unsigned i = 0; i < node.mNumChildren; ++i)
            {
                if (!node.mChildren[i])
                    return std::unexpected(AnimatedImportError(ModelImportErrorCode::InvalidData, path, "Model node references a missing child"));
                if (auto child = ReadAnimatedNode(*node.mChildren[i], scene, state, path); !child) return child;
            }
            return {};
        }

        std::expected<AnimatedImportState, ModelImportError> BuildAnimatedModel(const aiScene& scene, const std::string& path)
        {
            if ((scene.mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene.mRootNode)
                return std::unexpected(AnimatedImportError(ModelImportErrorCode::InvalidScene, path, "ERROR::ASSIMP:: Model scene is incomplete or has no root node"));
            AnimatedImportState state;
            if (auto nodes = ReadAnimatedNode(*scene.mRootNode, scene, state, path); !nodes) return std::unexpected(nodes.error());
            if (state.geometries.empty())
                return std::unexpected(AnimatedImportError(ModelImportErrorCode::NoGeometry, path, "Model contains no geometry: " + path));
            return state;
        }
    }

    std::expected<AnimatedModel, ModelImportError> AnimatedModel::Create(const std::string& path)
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
            return std::unexpected(AnimatedImportError(!scene ? ModelImportErrorCode::ImportFailed : ModelImportErrorCode::InvalidScene,
                path, std::string("ERROR::ASSIMP:: ") + importer.GetErrorString()));
        auto state = BuildAnimatedModel(*scene, path);
        if (!state) return std::unexpected(state.error());
        AnimatedModel model;
        model.m_BoneInfo = std::move(state->bones);
        model.m_BoneCounter = state->boneCount;
        model.m_Geometries = std::move(state->geometries);
        return model;
    }
    AnimatedModel::AnimatedModel(AnimatedModel&&) = default;
    AnimatedModel& AnimatedModel::operator=(AnimatedModel&&) = default;
    AnimatedModel::~AnimatedModel() = default;
    std::vector<std::unique_ptr<Geometry>> AnimatedModel::TakeGeometries() && { return std::move(m_Geometries); }
}
