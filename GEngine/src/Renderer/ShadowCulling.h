#pragma once

#include "Renderer/FrameSubmission.h"
#include <cmath>
#include <limits>
#include <new>

namespace GEngine::RenderDetail
{
    inline bool CullableShadowBounds(const WorldBounds& bounds) noexcept
    {
        if(!bounds.CanCull()) return false;
        for(int axis=0;axis<3;++axis)
            if(!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis])
                || bounds.minimum[axis]>bounds.maximum[axis]) return false;
        return true;
    }
    struct ShadowPointRange { glm::vec3 position{}; float radius{}; };
    inline BoundsVisibility TestShadowRange(const WorldBounds& bounds,ShadowPointRange range) noexcept
    {
        if(!CullableShadowBounds(bounds) || !std::isfinite(range.radius) || range.radius<=0)
            return BoundsVisibility::Conservative;
        std::array<double,3> distance{};
        double magnitude=range.radius;
        for(int axis=0;axis<3;++axis) {
            const double p=range.position[axis];
            if(!std::isfinite(p)) return BoundsVisibility::Conservative;
            distance[axis]=(std::max)({bounds.minimum[axis]-p,p-bounds.maximum[axis],0.0});
            magnitude+=(std::max)({std::abs(p),std::abs(bounds.minimum[axis]),std::abs(bounds.maximum[axis])});
        }
        const double nearest=std::hypot(distance[0],distance[1],distance[2]);
        const double tolerance=32*std::numeric_limits<float>::epsilon()*(1+magnitude);
        if(!std::isfinite(nearest) || !std::isfinite(tolerance)) return BoundsVisibility::Conservative;
        return nearest>double(range.radius)+tolerance ? BoundsVisibility::Outside : BoundsVisibility::Intersecting;
    }

    inline BoundsVisibility TestShadowVolume(const WorldBounds& bounds,const CameraFrustum& frustum,
        BoundsVisibility range) noexcept
    {
        if(!CullableShadowBounds(bounds) || range==BoundsVisibility::Conservative) return BoundsVisibility::Conservative;
        return range==BoundsVisibility::Outside ? BoundsVisibility::Outside : frustum.Test(bounds);
    }

    // CPU-only source-ordered lists over retained frame bounds. Layer volumes
    // are the exact submitted light matrices (including cascade depth expansion),
    // never the main-camera slice: off-camera upstream casters remain eligible.
    // A union list submits each caster once; layer membership becomes a geometry
    // shader mask, including for individual instances in one compatible group.
    class ShadowCasterLists final
    {
    public:
        static std::expected<ShadowCasterLists,VisibilityError> Build(const RenderFrame& frame,
            std::span<const std::size_t> candidates,std::span<const glm::mat4> matrices,
            std::optional<ShadowPointRange> range,bool enabled) noexcept
        {
            Asset::AssetDetail::RequireInvariant(matrices.size()<=6);
            ShadowCasterLists result;
            result.m_Layers=matrices.size(); result.m_Capacity=candidates.size();
            const auto lists=matrices.size()+1;
            if(candidates.size()>std::size_t((std::numeric_limits<std::ptrdiff_t>::max)())/sizeof(std::size_t)/lists)
                return std::unexpected(VisibilityError{VisibilityErrorCode::CapacityOverflow,candidates.size()});
            if(!candidates.empty()) {
                result.m_Indices.reset(new(std::nothrow) std::size_t[candidates.size()*lists]);
                if(!result.m_Indices) return std::unexpected(VisibilityError{VisibilityErrorCode::AllocationFailed,candidates.size()});
            }
            std::array<CameraFrustum,6> frusta;
            for(std::size_t layer=0;layer<matrices.size();++layer) {
                FrameCamera camera; camera.view=glm::mat4(1); camera.projection=matrices[layer];
                frusta[layer]=CameraFrustum::FromCamera(camera);
            }
            for(const auto index:candidates) {
                const auto& draw=frame.Draws()[index];
                const auto bounds=TransformBounds(frame.Resources()[draw.resources].Mesh()->Bounds(),draw.worldTransform);
                const auto relevance=range ? TestShadowRange(bounds,*range) : BoundsVisibility::Intersecting;
                bool any=false, fallback=false;
                for(std::size_t layer=0;layer<matrices.size();++layer) {
                    const auto test=!enabled ? BoundsVisibility::Inside
                        : TestShadowVolume(bounds,frusta[layer],relevance);
                    const bool keep=test!=BoundsVisibility::Outside, conservative=test==BoundsVisibility::Conservative;
                    Count(result.stats.layers[layer],keep,conservative);
                    if(keep) result.Append(layer,index);
                    any|=keep; fallback|=conservative;
                }
                Count(result.stats.total,any,fallback);
                if(any) result.Append(matrices.size(),index);
            }
            result.stats.layerCount=matrices.size();
            return result;
        }
        std::span<const std::size_t> Layer(std::size_t layer) const noexcept
        { return {m_Indices ? m_Indices.get()+layer*m_Capacity : nullptr,m_Counts[layer]}; }
        std::span<const std::size_t> Union() const noexcept { return Layer(m_Layers); }
        ShadowVisibilityStats stats;
    private:
        static void Count(ShadowCasterStats& stats,bool keep,bool conservative) noexcept
        { ++stats.candidates; if(keep) ++stats.visible; else ++stats.culled; if(conservative) ++stats.conservative; }
        void Append(std::size_t layer,std::size_t index) noexcept
        { m_Indices[layer*m_Capacity+m_Counts[layer]++]=index; }
        std::unique_ptr<std::size_t[]> m_Indices;
        std::array<std::size_t,7> m_Counts{};
        std::size_t m_Capacity{},m_Layers{};
    };
}
