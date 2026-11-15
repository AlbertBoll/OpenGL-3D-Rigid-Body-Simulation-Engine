#pragma once
#include <cmath>
#include <optional>
#include <limits>
#include <glm/glm.hpp>

namespace Rbs::Editor
{
    struct SnapPreferences {
        bool visible{},grid{},surface{},align{},resolve{};
        float increment{1.f},offset{},distance{5.f};
    };
    inline SnapPreferences Snap;
    inline std::optional<glm::vec3> SnapGrid(const glm::vec3& position,float increment)
    {
        if(!std::isfinite(increment)||increment<=0)return {};
        glm::vec3 result;
        for(int i=0;i<3;++i) {
            const double v=std::round(double(position[i])/increment)*increment;
            if(!std::isfinite(v)||std::abs(v)>std::numeric_limits<float>::max())return {};
            result[i]=float(v);
        }
        return result;
    }
}
