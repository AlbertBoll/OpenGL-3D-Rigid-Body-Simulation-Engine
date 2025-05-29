#pragma once
#include <Math/Math.h>
#include "BoneInfo.h"
#include "Bone.h"

struct aiNode;
struct aiAnimation;


namespace GEngine
{
	class AnimatedModel;
	using namespace Math;

	struct AssimpNodeData
	{
		Mat4 transformation;
		std::string name;
		int childrenCount;
		std::vector<AssimpNodeData> children;
	};

	class Animation
	{
	public:
		Animation(const std::string& animationPath, AnimatedModel* model);
		Animation() = default;
		~Animation() {}
		inline float GetTicksPerSecond() { return m_TicksPerSecond; }

		inline float GetDuration() { return m_Duration; }

		inline const AssimpNodeData& GetRootNode() { return m_RootNode; }

		inline const std::unordered_map<std::string, BoneInfo>& GetBoneIDMap()
		{
			return m_BoneInfoMap;
		}

		Bone* FindBone(const std::string& name);

	private:
		void ReadMissingBones(const aiAnimation* animation, AnimatedModel& model);

		void ReadHeirarchyData(AssimpNodeData& dest, const aiNode* src);

	private:
		float m_Duration{};
		float m_TicksPerSecond{};
		std::vector<Bone> m_Bones;
		AssimpNodeData m_RootNode;
		std::unordered_map<std::string, BoneInfo> m_BoneInfoMap;
	};

}