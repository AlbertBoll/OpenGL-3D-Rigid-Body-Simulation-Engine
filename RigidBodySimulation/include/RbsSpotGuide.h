#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <optional>

namespace Rbs::Editor
{
    // A world-space Light frame, never a camera/billboard frame. Keep the same
    // local -Z ray as RenderExtraction. Remove scale without discarding roll.
    struct SpotGuideFrame { glm::vec3 position, direction, right, up; };
    inline std::optional<SpotGuideFrame> MakeSpotGuideFrame(const glm::mat4& world)
    {
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(world[c][r]))return {};
        const auto unit=[](glm::vec3 v)->std::optional<glm::vec3> {
            const float length=std::hypot(v.x,v.y,v.z);
            if(!std::isfinite(length)||length<=0)return {};
            return v/length;
        };
        const auto direction=unit(-glm::vec3(world[2]));if(!direction)return {};
        const auto x=glm::vec3(world[0]);
        const auto right=unit(x-*direction*glm::dot(x,*direction));if(!right)return {};
        return SpotGuideFrame{glm::vec3(world[3]),*direction,*right,glm::cross(*right,*direction)};
    }
    enum class SpotGuidePart { Inner, Outer, Range };
    struct SpotGuideVisibility { bool inner,outer,range; };

    // Emit bounded, allocation-free world segments. Camera and screen coordinates
    // deliberately are not inputs. Range is radial, matching the existing PBR cutoff.
    template<class Emit>
    void EmitSpotGuide(const SpotGuideFrame& f,float range,float inner,float outer,
                       SpotGuideVisibility visible,Emit&& line)
    {
        const auto cone=[&](float angle,SpotGuidePart part) {
            const auto tip=f.position+f.direction*(range*std::cos(angle));
            const float radius=range*std::sin(angle);
            for(int i=0;i<48;++i) {
                const float a=float(i)*glm::two_pi<float>()/48,b=float(i+1)*glm::two_pi<float>()/48;
                const auto p=tip+(f.right*std::cos(a)+f.up*std::sin(a))*radius;
                line(p,tip+(f.right*std::cos(b)+f.up*std::sin(b))*radius,part);
                if(i%12==0)line(f.position,p,part);
            }
        };
        if(visible.inner)cone(inner,SpotGuidePart::Inner);
        if(visible.outer)cone(outer,SpotGuidePart::Outer);
        if(visible.range) {
            line(f.position,f.position+f.direction*range,SpotGuidePart::Range);
            for(int axis=0;axis<2;++axis)for(int i=0;i<32;++i) {
                const float a=-outer+2*outer*float(i)/32,b=-outer+2*outer*float(i+1)/32;
                const auto radial=axis?f.up:f.right;
                line(f.position+(f.direction*std::cos(a)+radial*std::sin(a))*range,
                     f.position+(f.direction*std::cos(b)+radial*std::sin(b))*range,SpotGuidePart::Range);
            }
        }
    }
}
