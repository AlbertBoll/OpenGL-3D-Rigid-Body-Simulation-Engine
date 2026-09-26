#pragma once
#include"Math/Math.h"
#include <vector>
#include <string>
#include <expected>
#include <string_view>
#include <utility>

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


    enum class BoneKeyTrack { Position, Rotation, Scale };
    enum class BoneKeyErrorCode { MissingInterval };
    struct BoneKeyError
    {
        BoneKeyErrorCode code;
        BoneKeyTrack track;
        std::string boneName;
        int boneId;
        float animationTime;
        std::size_t keyCount;
        std::string_view operation, message;
    };
    using BoneUpdateResult = std::expected<void, BoneKeyError>;

	class Bone
	{

	public:
        Bone(const std::string& name, int ID, std::vector<KeyPosition> positions,
            std::vector<KeyRotation> rotations, std::vector<KeyScale> scales)
            : m_Positions(std::move(positions)), m_Rotations(std::move(rotations)), m_Scales(std::move(scales)),
              m_LocalTransform(1.0f), m_Name(name), m_ID(ID) {}

        [[nodiscard]] BoneUpdateResult Update(float animationTime)
        {
            auto translation = InterpolatePosition(animationTime);
            if (!translation) return std::unexpected(translation.error());
            auto rotation = InterpolateRotation(animationTime);
            if (!rotation) return std::unexpected(rotation.error());
            auto scale = InterpolateScaling(animationTime);
            if (!scale) return std::unexpected(scale.error());
            m_LocalTransform = *translation * *rotation * *scale;
            return {};
        }

		Mat4 GetLocalTransform() { return m_LocalTransform; }
		std::string GetBoneName() const { return m_Name; }
		int GetBoneID() { return m_ID; }

		[[nodiscard]] std::expected<std::size_t, BoneKeyError> GetPositionIndex(float animationTime)
		{
			for (std::size_t index = 0; index + 1 < m_Positions.size(); ++index)
			{
				if (animationTime < m_Positions[index + 1].timeStamp)
					return index;
			}

            return std::unexpected(BoneKeyError{BoneKeyErrorCode::MissingInterval, BoneKeyTrack::Position,
                m_Name, m_ID, animationTime, m_Positions.size(), "Bone::GetPositionIndex",
                "Animation time has no position key interval"});
		}

		[[nodiscard]] std::expected<std::size_t, BoneKeyError> GetRotationIndex(float animationTime)
		{
			for (std::size_t index = 0; index + 1 < m_Rotations.size(); ++index)
			{
				if (animationTime < m_Rotations[index + 1].timeStamp)
					return index;
			}

            return std::unexpected(BoneKeyError{BoneKeyErrorCode::MissingInterval, BoneKeyTrack::Rotation,
                m_Name, m_ID, animationTime, m_Rotations.size(), "Bone::GetRotationIndex",
                "Animation time has no rotation key interval"});
		}

		[[nodiscard]] std::expected<std::size_t, BoneKeyError> GetScaleIndex(float animationTime)
		{
			for (std::size_t index = 0; index + 1 < m_Scales.size(); ++index)
			{
				if (animationTime < m_Scales[index + 1].timeStamp)
					return index;
			}

            return std::unexpected(BoneKeyError{BoneKeyErrorCode::MissingInterval, BoneKeyTrack::Scale,
                m_Name, m_ID, animationTime, m_Scales.size(), "Bone::GetScaleIndex",
                "Animation time has no scale key interval"});
		}


	private:
		float GetScaleFactor(float lastTimeStamp, float nextTimeStamp, float animationTime)
		{
			
			float midWayLength = animationTime - lastTimeStamp;
			float framesDiff = nextTimeStamp - lastTimeStamp;
			return midWayLength / framesDiff;
			
		}


		std::expected<Mat4, BoneKeyError> InterpolatePosition(float animationTime)
		{
			if (1 == m_Positions.size())
				return glm::translate(Mat4(1.0f), m_Positions[0].position);

			auto interval = GetPositionIndex(animationTime);
            if (!interval) return std::unexpected(interval.error());
            const auto p0Index = *interval;
            const auto p1Index = p0Index + 1;
			float scaleFactor = GetScaleFactor(m_Positions[p0Index].timeStamp,
				m_Positions[p1Index].timeStamp, animationTime);
			Vec3f finalPosition = glm::mix(m_Positions[p0Index].position, m_Positions[p1Index].position
				, scaleFactor);
			return glm::translate(Mat4(1.0f), finalPosition);
		}


		std::expected<Mat4, BoneKeyError> InterpolateRotation(float animationTime)
		{
			if (1 == m_Rotations.size())
			{
				auto rotation = glm::normalize(m_Rotations[0].orientation);
				return glm::toMat4(rotation);
			}

			auto interval = GetRotationIndex(animationTime);
            if (!interval) return std::unexpected(interval.error());
            const auto p0Index = *interval;
            const auto p1Index = p0Index + 1;
			float scaleFactor = GetScaleFactor(m_Rotations[p0Index].timeStamp,
				m_Rotations[p1Index].timeStamp, animationTime);
			Quat finalRotation = glm::slerp(m_Rotations[p0Index].orientation, m_Rotations[p1Index].orientation
				, scaleFactor);
			finalRotation = glm::normalize(finalRotation);
			return glm::toMat4(finalRotation);

		}


		std::expected<Mat4, BoneKeyError> InterpolateScaling(float animationTime)
		{
			if (1 == m_Scales.size())
				return glm::scale(Mat4(1.0f), m_Scales[0].scale);

			auto interval = GetScaleIndex(animationTime);
            if (!interval) return std::unexpected(interval.error());
            const auto p0Index = *interval;
            const auto p1Index = p0Index + 1;
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
		int m_ID;
	};

}
