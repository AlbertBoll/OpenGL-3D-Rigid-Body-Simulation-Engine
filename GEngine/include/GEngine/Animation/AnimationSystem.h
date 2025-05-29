#pragma once
#include <Math/Math.h>

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

		void UpdateAnimation(float dt);
	

		void PlayAnimation(Animation* pAnimation)
		{
			m_CurrentAnimation = pAnimation;
			m_CurrentTime = 0.0f;
		}

		void CalculateBoneTransform(const AssimpNodeData* node, const Mat4& parentTransform);
		

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