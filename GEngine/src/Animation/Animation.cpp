#include "gepch.h"
#include "Animation/Animation.h"
#include "Animation/AnimatedModel.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include "Extras/AssimpGLMHelpers.h"
#include <limits>
#include <new>

namespace GEngine
{
    namespace
    {
        ModelImportError ClipImportError(ModelImportErrorCode code, const std::string& path, std::string message)
        { return {code, "Animation::Create", path, std::move(message)}; }

        struct ClipImportState
        {
            float duration{}, ticksPerSecond{};
            AssimpNodeData root;
            std::vector<Bone> bones;
            std::unordered_map<std::string, BoneInfo> boneInfo;
            int boneCount{};
        };

        std::expected<AssimpNodeData, ModelImportError> ReadClipHierarchy(const aiNode* source, const std::string& path)
        {
            if (!source || (source->mNumChildren && !source->mChildren)
                || source->mNumChildren > static_cast<unsigned>((std::numeric_limits<int>::max)()))
                return std::unexpected(ClipImportError(ModelImportErrorCode::InvalidData, path,
                    "Animation hierarchy has missing or unsupported child data"));
            AssimpNodeData node;
            node.name = source->mName.C_Str();
            node.transformation = AssimpGLMHelpers::ConvertMatrixToGLMFormat(source->mTransformation);
            node.childrenCount = static_cast<int>(source->mNumChildren);
            for (unsigned i = 0; i < source->mNumChildren; ++i)
            {
                auto child = ReadClipHierarchy(source->mChildren[i], path);
                if (!child) return std::unexpected(child.error());
                node.children.push_back(std::move(*child));
            }
            return node;
        }

        std::expected<ClipImportState, ModelImportError> BuildAnimation(
            const aiScene& scene, AnimatedModel& model, const std::string& path)
        {
            if ((scene.mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene.mRootNode)
                return std::unexpected(ClipImportError(ModelImportErrorCode::InvalidScene, path,
                    "Animation scene is incomplete or has no root node"));
            if (!scene.mNumAnimations || !scene.mAnimations || !scene.mAnimations[0])
                return std::unexpected(ClipImportError(ModelImportErrorCode::InvalidScene, path,
                    "Model contains no animation clip: " + path));
            const auto& animation = *scene.mAnimations[0];
            if (animation.mNumChannels && !animation.mChannels)
                return std::unexpected(ClipImportError(ModelImportErrorCode::InvalidData, path,
                    "Animation clip has no channel data"));
            auto root = ReadClipHierarchy(scene.mRootNode, path);
            if (!root) return std::unexpected(root.error());
            ClipImportState state;
            state.duration = static_cast<float>(animation.mDuration);
            state.ticksPerSecond = static_cast<float>(animation.mTicksPerSecond);
            state.root = std::move(*root);
            state.boneInfo = model.GetBoneInfoMap();
            state.boneCount = model.GetBoneCount();
            if (state.boneCount < 0)
                return std::unexpected(ClipImportError(ModelImportErrorCode::InvalidData, path,
                    "Animation model has an invalid bone count"));
            for (unsigned i = 0; i < animation.mNumChannels; ++i)
            {
                const auto* channel = animation.mChannels[i];
                if (!channel || (channel->mNumPositionKeys && !channel->mPositionKeys)
                    || (channel->mNumRotationKeys && !channel->mRotationKeys)
                    || (channel->mNumScalingKeys && !channel->mScalingKeys))
                    return std::unexpected(ClipImportError(ModelImportErrorCode::InvalidData, path,
                        "Animation channel has incomplete key data: channel=" + std::to_string(i)));
                const std::string boneName = channel->mNodeName.C_Str();
                if (!state.boneInfo.contains(boneName))
                {
                    if (state.boneCount == (std::numeric_limits<int>::max)())
                        return std::unexpected(ClipImportError(ModelImportErrorCode::InvalidData, path,
                            "Animation bone identifier exceeds its supported range"));
                    state.boneInfo[boneName].id = state.boneCount++;
                }
                std::vector<KeyPosition> positions;
                std::vector<KeyRotation> rotations;
                std::vector<KeyScale> scales;
                for (unsigned key = 0; key < channel->mNumPositionKeys; ++key)
                    positions.push_back({AssimpGLMHelpers::GetGLMVec(channel->mPositionKeys[key].mValue),
                        static_cast<float>(channel->mPositionKeys[key].mTime)});
                for (unsigned key = 0; key < channel->mNumRotationKeys; ++key)
                    rotations.push_back({AssimpGLMHelpers::GetGLMQuat(channel->mRotationKeys[key].mValue),
                        static_cast<float>(channel->mRotationKeys[key].mTime)});
                for (unsigned key = 0; key < channel->mNumScalingKeys; ++key)
                    scales.push_back({AssimpGLMHelpers::GetGLMVec(channel->mScalingKeys[key].mValue),
                        static_cast<float>(channel->mScalingKeys[key].mTime)});
                state.bones.emplace_back(boneName, state.boneInfo.find(boneName)->second.id,
                    std::move(positions), std::move(rotations), std::move(scales));
            }
            return state;
        }
    }

    std::expected<std::unique_ptr<Animation>, ModelImportError> Animation::Create(
        const std::string& path, AnimatedModel& model)
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate);
        if (!scene)
            return std::unexpected(ClipImportError(ModelImportErrorCode::ImportFailed, path,
                std::string("ERROR::ASSIMP:: ") + importer.GetErrorString()));
        auto state = BuildAnimation(*scene, model, path);
        if (!state) return std::unexpected(state.error());
        std::unique_ptr<Animation> result(new (std::nothrow) Animation);
        if (!result)
            return std::unexpected(ClipImportError(ModelImportErrorCode::Allocation, path, "Unable to allocate animation clip"));
        result->m_Duration = state->duration;
        result->m_TicksPerSecond = state->ticksPerSecond;
        result->m_RootNode = std::move(state->root);
        result->m_Bones = std::move(state->bones);
        result->m_BoneInfoMap = state->boneInfo;
        // Publish missing-bone metadata only after a complete clip exists.
        model.GetBoneInfoMap() = std::move(state->boneInfo);
        model.GetBoneCount() = state->boneCount;
        return result;
    }

	Bone* Animation::FindBone(const std::string& name)
	{
		auto iter = std::find_if(m_Bones.begin(), m_Bones.end(),
			[&](const Bone& Bone)
			{
				return Bone.GetBoneName() == name;
			}
		);
		if (iter == m_Bones.end()) return nullptr;
		else return &(*iter);
	}

}
