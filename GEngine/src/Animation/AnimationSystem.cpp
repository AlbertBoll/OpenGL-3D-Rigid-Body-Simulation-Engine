#include "gepch.h"
#include "Animation/AnimationSystem.h"
#include "Animation/Animation.h"

namespace GEngine
{
	AnimationSystem::AnimationSystem(Animation* animation)
	{
		m_CurrentTime = 0.0;
		m_CurrentAnimation = animation;

		m_FinalBoneMatrices.reserve(100);

		for (int i = 0; i < 100; i++)
			m_FinalBoneMatrices.push_back(glm::mat4(1.0f));
	}

	AnimationSystem::~AnimationSystem()
	{
		delete m_CurrentAnimation; 
	}

	BoneUpdateResult AnimationSystem::UpdateAnimation(float dt)
	{
		{
			m_DeltaTime = dt;
			if (m_CurrentAnimation)
			{
				m_CurrentTime += m_CurrentAnimation->GetTicksPerSecond() * dt;
				m_CurrentTime = fmod(m_CurrentTime, m_CurrentAnimation->GetDuration());
				if (auto result = CalculateBoneTransform(&m_CurrentAnimation->GetRootNode(), glm::mat4(1.0f)); !result)
                    return result;
			}
		}
        return {};
	}

	BoneUpdateResult AnimationSystem::CalculateBoneTransform(const AssimpNodeData* node, const Mat4& parentTransform)
	{
		std::string nodeName = node->name;
		Mat4 nodeTransform = node->transformation;

		Bone* Bone = m_CurrentAnimation->FindBone(nodeName);

		if (Bone)
		{
			if (auto result = Bone->Update(m_CurrentTime); !result) return result;
			nodeTransform = Bone->GetLocalTransform();
		}

		Mat4 globalTransformation = parentTransform * nodeTransform;

		auto boneInfoMap = m_CurrentAnimation->GetBoneIDMap();
		if (boneInfoMap.find(nodeName) != boneInfoMap.end())
		{
			int index = boneInfoMap[nodeName].id;
			Mat4 offset = boneInfoMap[nodeName].offset;
			m_FinalBoneMatrices[index] = globalTransformation * offset;
		}

        for (int i = 0; i < node->childrenCount; i++)
            if (auto result = CalculateBoneTransform(&node->children[i], globalTransformation); !result)
                return result;
        return {};
	}
}
