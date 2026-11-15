#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <optional>

namespace GEngine
{
    // One existing Spot slot. Bias is normalized projected depth; radius 0/1/2
    // means hard / 3x3 / 5x5 PCF. Allocation is transactional on the GL owner.
    struct SpotShadowSettings
    {
        int resolution=1024;
        float bias=.001f;
        int pcfRadius=1;
        bool operator==(const SpotShadowSettings&) const = default;
    };
    inline const char* ValidateSpotShadow(const SpotShadowSettings& value) noexcept
    {
        if(value.resolution<1 || value.resolution>8192)return "Shadow resolution must be 1 to 8192 pixels (subject to GPU allocation).";
        if(!std::isfinite(value.bias) || value.bias<0 || value.bias>1)return "Shadow bias must be finite, between 0 and 1 in projected depth.";
        if(value.pcfRadius<0 || value.pcfRadius>2)return "Spot filtering supports hard, 3x3 or 5x5 PCF.";
        return nullptr;
    }
    inline std::optional<glm::mat4> SpotShadowMatrix(glm::vec3 position,glm::vec3 direction,float range,float outer) noexcept
    {
        if(!std::isfinite(range)||range<=0 || !std::isfinite(outer)||outer<=0||outer>=1.5707963f)return {};
        const float nearPlane=std::min(.1f,range*.01f);
        if(nearPlane<=0)return {};
        const auto up=std::abs(direction.y)>.99f?glm::vec3(1,0,0):glm::vec3(0,1,0);
        // Construct orientation at the origin to avoid losing the unit direction
        // when adding it to a large finite world position.
        const auto result=glm::perspective(2.f*outer,1.f,nearPlane,range)
            *glm::lookAt(glm::vec3(0),direction,up)*glm::translate(glm::mat4(1),-position);
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(result[c][r]))return {};
        if(!std::isfinite(glm::determinant(result)) || glm::determinant(result)==0)return {};
        return result;
    }
}
