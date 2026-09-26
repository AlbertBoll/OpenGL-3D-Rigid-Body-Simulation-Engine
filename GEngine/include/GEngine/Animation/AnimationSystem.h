#pragma once
#include <Math/Math.h>
#include "Animation/Bone.h"

namespace GEngine
{
	class Animation;
	struct AssimpNodeData;

	using namespace Math;

	class AnimationSystem
	{
	public:
		AnimationSystem(Animation* animation);
		~AnimationSystem();

		[[nodiscard]] BoneUpdateResult UpdateAnimation(float dt);
	

		void PlayAnimation(Animation* pAnimation)
		{
			m_CurrentAnimation = pAnimation;
			m_CurrentTime = 0.0f;
		}

		[[nodiscard]] BoneUpdateResult CalculateBoneTransform(const AssimpNodeData* node, const Mat4& parentTransform);
		

		std::vector<Mat4> GetFinalBoneMatrices()
		{
			return m_FinalBoneMatrices;
		}

	private:
		std::vector<Mat4> m_FinalBoneMatrices;
		Animation* m_CurrentAnimation;
		float m_CurrentTime;
		float m_DeltaTime;

	};
}