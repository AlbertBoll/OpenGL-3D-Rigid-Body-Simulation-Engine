#pragma once
#include"Math/Math.h"
#include <vector>
#include <string>
#include <assimp/anim.h>
#include <Extras/AssimpGLMHelpers.h>
#include <Core/Assert.h>

namespace GEngine
{
	using namespace Math;

	struct KeyPosition
	{
		Vec3f position;
		float timeStamp;
	};

	struct KeyRotation
	{
		Quat orientation;
		float timeStamp;
	};

	struct KeyScale
	{
		Vec3f scale;
		float timeStamp;
	};


	class Bone
	{

	public:
		Bone(const std::string& name, int ID, const aiNodeAnim* channel)
			:m_Name(name), m_ID(ID), m_LocalTransform(1.0f)
		{
			m_NumPositions = channel->mNumPositionKeys;

			for (int positionIndex = 0; positionIndex < m_NumPositions; ++positionIndex)
			{
				aiVector3D aiPosition = channel->mPositionKeys[positionIndex].mValue;
				float timeStamp = (float)channel->mPositionKeys[positionIndex].mTime;
				KeyPosition data;
				data.position = AssimpGLMHelpers::GetGLMVec(aiPosition);
				data.timeStamp = timeStamp;
				m_Positions.push_back(data);
			}

			m_NumRotations = channel->mNumRotationKeys;
			for (int rotationIndex = 0; rotationIndex < m_NumRotations; ++rotationIndex)
			{
				aiQuaternion aiOrientation = channel->mRotationKeys[rotationIndex].mValue;
				float timeStamp = (float)channel->mRotationKeys[rotationIndex].mTime;
				KeyRotation data;
				data.orientation = AssimpGLMHelpers::GetGLMQuat(aiOrientation);
				data.timeStamp = timeStamp;
				m_Rotations.push_back(data);
			}

			m_NumScalings = channel->mNumScalingKeys;
			for (int keyIndex = 0; keyIndex < m_NumScalings; ++keyIndex)
			{
				aiVector3D scale = channel->mScalingKeys[keyIndex].mValue;
				float timeStamp = (float)channel->mScalingKeys[keyIndex].mTime;
				KeyScale data;
				data.scale = AssimpGLMHelpers::GetGLMVec(scale);
				data.timeStamp = timeStamp;
				m_Scales.push_back(data);
			}
		}

		void Update(float animationTime)
		{
			Mat4 translation = InterpolatePosition(animationTime);
			Mat4 rotation = InterpolateRotation(animationTime);
			Mat4 scale = InterpolateScaling(animationTime);
			m_LocalTransform = translation * rotation * scale;
		}

		Mat4 GetLocalTransform() { return m_LocalTransform; }
		std::string GetBoneName() const { return m_Name; }
		int GetBoneID() { return m_ID; }

		int GetPositionIndex(float animationTime)
		{
			for (int index = 0; index < m_NumPositions - 1; ++index)
			{
				if (animationTime < m_Positions[index + 1].timeStamp)
					return index;
			}

			ASSERT(false);
		}

		int GetRotationIndex(float animationTime)
		{
			for (int index = 0; index < m_NumRotations - 1; ++index)
			{
				if (animationTime < m_Rotations[index + 1].timeStamp)
					return index;
			}

			ASSERT(false);
		}

		int GetScaleIndex(float animationTime)
		{
			for (int index = 0; index < m_NumScalings - 1; ++index)
			{
				if (animationTime < m_Scales[index + 1].timeStamp)
					return index;
			}

			ASSERT(false);
		}


	private:
		float GetScaleFactor(float lastTimeStamp, float nextTimeStamp, float animationTime)
		{
			
			float midWayLength = animationTime - lastTimeStamp;
			float framesDiff = nextTimeStamp - lastTimeStamp;
			return midWayLength / framesDiff;
			
		}


		Mat4 InterpolatePosition(float animationTime)
		{
			if (1 == m_NumPositions)
				return glm::translate(Mat4(1.0f), m_Positions[0].position);

			int p0Index = GetPositionIndex(animationTime);
			int p1Index = p0Index + 1;
			float scaleFactor = GetScaleFactor(m_Positions[p0Index].timeStamp,
				m_Positions[p1Index].timeStamp, animationTime);
			Vec3f finalPosition = glm::mix(m_Positions[p0Index].position, m_Positions[p1Index].position
				, scaleFactor);
			return glm::translate(Mat4(1.0f), finalPosition);
		}


		Mat4 InterpolateRotation(float animationTime)
		{
			if (1 == m_NumRotations)
			{
				auto rotation = glm::normalize(m_Rotations[0].orientation);
				return glm::toMat4(rotation);
			}

			int p0Index = GetRotationIndex(animationTime);
			int p1Index = p0Index + 1;
			float scaleFactor = GetScaleFactor(m_Rotations[p0Index].timeStamp,
				m_Rotations[p1Index].timeStamp, animationTime);
			Quat finalRotation = glm::slerp(m_Rotations[p0Index].orientation, m_Rotations[p1Index].orientation
				, scaleFactor);
			finalRotation = glm::normalize(finalRotation);
			return glm::toMat4(finalRotation);

		}


		Mat4 InterpolateScaling(float animationTime)
		{
			if (1 == m_NumScalings)
				return glm::scale(Mat4(1.0f), m_Scales[0].scale);

			int p0Index = GetScaleIndex(animationTime);
			int p1Index = p0Index + 1;
			float scaleFactor = GetScaleFactor(m_Scales[p0Index].timeStamp,
				m_Scales[p1Index].timeStamp, animationTime);
			Vec3f finalScale = glm::mix(m_Scales[p0Index].scale, m_Scales[p1Index].scale
				, scaleFactor);
			return glm::scale(Mat4(1.0f), finalScale);
		}


	private:
		std::vector<KeyPosition> m_Positions;
		std::vector<KeyRotation> m_Rotations;
		std::vector<KeyScale> m_Scales;
		Mat4 m_LocalTransform;
		std::string m_Name;
		int m_NumPositions;
		int m_NumRotations;
		int m_NumScalings;
		int m_ID;
	};

}