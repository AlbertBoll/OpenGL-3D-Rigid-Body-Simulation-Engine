#pragma once
#include "Animation/BoneInfo.h"
#include "Assets/ModelImportError.h"
#include <expected>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

constexpr int MAX_BONE_INFLUENCE = 4;

namespace GEngine
{
    class Geometry;
    class AnimatedModel
    {
    public:
        [[nodiscard]] static std::expected<AnimatedModel, ModelImportError> Create(const std::string& path);
        AnimatedModel(const AnimatedModel&) = delete;
        AnimatedModel& operator=(const AnimatedModel&) = delete;
        AnimatedModel(AnimatedModel&&);
        AnimatedModel& operator=(AnimatedModel&&);
        ~AnimatedModel();
        auto& GetBoneInfoMap() { return m_BoneInfo; }
        int& GetBoneCount() { return m_BoneCounter; }
        [[nodiscard]] std::vector<std::unique_ptr<Geometry>> TakeGeometries() &&;
    private:
        AnimatedModel() = default;
        std::unordered_map<std::string, BoneInfo> m_BoneInfo;
        std::vector<std::unique_ptr<Geometry>> m_Geometries;
        int m_BoneCounter = 0;
    };
}
