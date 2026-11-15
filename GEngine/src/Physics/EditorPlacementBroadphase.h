#pragma once
#include "Physics/Bounds.h"
#include "Scene/EditorTransformBudget.h"
#include <algorithm>
#include <numeric>
#include <span>
#include <limits>
namespace GEngine::EditorPlacementBroadphase
{
    // Conservative AABB sweep of the complete candidate geometry. No contact is
    // accepted by this filter: intersecting bounds still need the exact query.
    template<class Pair> bool Visit(std::span<const Bounds> bounds,std::vector<std::size_t>& order,Pair&& pair) {
        order.resize(bounds.size());std::iota(order.begin(),order.end(),0);
        if(order.empty())return true;
        Vec3f lo=bounds[0].mins,hi=bounds[0].maxs;
        for(const auto& b:bounds){lo=glm::min(lo,b.mins);hi=glm::max(hi,b.maxs);}
        const auto extent=hi-lo;int axis=0;for(int a=1;a<3;++a)if(extent[a]>extent[axis])axis=a;
        std::sort(order.begin(),order.end(),[&](auto a,auto b){return bounds[a].mins[axis]!=bounds[b].mins[axis]?bounds[a].mins[axis]<bounds[b].mins[axis]:a<b;});
        std::size_t work=0;
        for(std::size_t a=0;a<order.size();++a)for(std::size_t b=a+1;b<order.size();++b) {
            const auto i=order[a],j=order[b];const auto& x=bounds[i];const auto& y=bounds[j];
            if(y.mins[axis]>x.maxs[axis])break;
            if(++work>EditorTransformBudget::BroadphasePairs)return false;
            if(x.mins.x>y.maxs.x || y.mins.x>x.maxs.x || x.mins.y>y.maxs.y || y.mins.y>x.maxs.y || x.mins.z>y.maxs.z || y.mins.z>x.maxs.z)continue;
            if(!pair(i,j))return false;
        }
        return true;
    }
    inline Bounds Inflate(Bounds b,float tolerance) {
        // Cover float transform rounding in addition to the unchanged query tolerance.
        const auto e=Vec3f(tolerance)+8*std::numeric_limits<float>::epsilon()*(Vec3f(1)+glm::max(glm::abs(b.mins),glm::abs(b.maxs)));
        b.mins-=e;b.maxs+=e;return b;
    }
}
