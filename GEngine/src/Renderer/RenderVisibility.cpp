#include "gepch.h"
#include "Renderer/RenderVisibility.h"
#include "RenderCpuBacking.h"
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace GEngine
{
    CameraFrustum CameraFrustum::FromCamera(const FrameCamera& camera) noexcept
    {
        CameraFrustum result;
        const glm::dmat4 clip = glm::dmat4(camera.projection) * glm::dmat4(camera.view);
        const double determinant = glm::determinant(clip);
        if (!std::isfinite(determinant) || determinant == 0) return result;
        const auto row = [&](int i) { return glm::dvec4(clip[0][i], clip[1][i], clip[2][i], clip[3][i]); };
        for (int axis = 0; axis < 3; ++axis)
            for (int side = 0; side < 2; ++side)
            {
                auto plane = row(3) + (side ? -row(axis) : row(axis));
                const double length = std::hypot(plane.x, plane.y, plane.z);
                if (!std::isfinite(length) || length == 0 || !std::isfinite(plane.w)) return result;
                plane /= length;
                if (!std::isfinite(plane.w)) return result;
                result.m_Planes[axis * 2 + side] = plane;
            }
        result.m_Valid = true;
        return result;
    }

    BoundsVisibility CameraFrustum::Test(const WorldBounds& bounds) const noexcept
    {
        if (!m_Valid || !bounds.CanCull()) return BoundsVisibility::Conservative;
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis])
                || bounds.minimum[axis] > bounds.maximum[axis]) return BoundsVisibility::Conservative;
        bool outside = false, intersects = false;
        for (const auto& plane : m_Planes)
        {
            double lower = plane.w, upper = plane.w, magnitude = std::abs(plane.w);
            for (int axis = 0; axis < 3; ++axis)
            {
                const double a = plane[axis] * bounds.minimum[axis], b = plane[axis] * bounds.maximum[axis];
                lower += (std::min)(a, b); upper += (std::max)(a, b);
                magnitude += (std::max)(std::abs(a), std::abs(b));
            }
            // Cover float camera/vertex arithmetic at submission as well as the
            // double plane test. Uncertain boundary cases are always retained.
            const double tolerance = 32 * std::numeric_limits<float>::epsilon() * (1 + magnitude);
            if (!std::isfinite(lower) || !std::isfinite(upper) || !std::isfinite(tolerance))
                return BoundsVisibility::Conservative;
            outside |= upper < -tolerance;
            intersects |= lower <= tolerance;
        }
        return outside ? BoundsVisibility::Outside
            : intersects ? BoundsVisibility::Intersecting : BoundsVisibility::Inside;
    }

    RenderVisibility::~RenderVisibility()
    { auto owner=RenderCpu::CpuOwner<const RenderCpu::VisibilitySnapshot>::Adopt(std::exchange(m_Snapshot,nullptr)); }
    void RenderVisibility::Swap(RenderVisibility& other) noexcept
    {
        using std::swap;
        swap(m_Snapshot,other.m_Snapshot);swap(m_Counts,other.m_Counts);
        swap(m_Capacity,other.m_Capacity);swap(m_Camera,other.m_Camera);swap(m_Stats,other.m_Stats);
    }
    RenderVisibility::RenderVisibility(RenderVisibility&& other) noexcept { Swap(other); }
    RenderVisibility& RenderVisibility::operator=(RenderVisibility&& other) noexcept
    {
        if(this!=&other){RenderVisibility old(std::move(other));Swap(old);}return *this;
    }
    std::span<const std::size_t> RenderVisibility::List(std::size_t index) const noexcept
    {
        const auto* values=m_Snapshot&&m_Snapshot->lists[index]?m_Snapshot->lists[index]->values.get():nullptr;
        return {values,m_Counts[index]};
    }
    namespace {
        using RenderCpu::Contribution;
        bool SameCamera(const FrameCamera&a,const FrameCamera&b) noexcept {
            if(a.entity!=b.entity||!RenderCpu::SameMatrix(a.view,b.view)||!RenderCpu::SameMatrix(a.projection,b.projection)
                ||a.viewportX!=b.viewportX||a.viewportY!=b.viewportY||a.viewportWidth!=b.viewportWidth
                ||a.viewportHeight!=b.viewportHeight||a.visibleLayers!=b.visibleLayers)return false;
            for(int i=0;i<3;++i)if(!RenderCpu::SameFloat(a.worldPosition[i],b.worldPosition[i]))return false;
            return true;
        }
        void AddContribution(VisibilityStats& stats,const Contribution& c,bool add) noexcept {
#define RW_STAT(field) if(add)stats.field+=std::size_t(c.field);else stats.field-=std::size_t(c.field)
            RW_STAT(emptyDraws);RW_STAT(layerRejected);RW_STAT(tested);RW_STAT(visible);RW_STAT(culled);RW_STAT(conservative);
#undef RW_STAT
        }
        Contribution Classify(const RenderFrame& frame,const RenderCpu::DrawSnapshot* cpu,std::size_t index,
            const FrameCamera& camera,const CameraFrustum& frustum,bool conservative,
            const Contribution* reuse=nullptr) noexcept
        {
            Contribution result;const auto& draw=frame.Draws()[index];
            if(!draw.submesh.elementCount){result.emptyDraws=true;return result;}
            if(draw.castShadows)result.lists|=1u<<4;
            if(!(draw.layers&camera.visibleLayers)){result.layerRejected=true;return result;}
            result.tested=true;
            if(reuse)result.geometry=reuse->geometry;
            else if(conservative)result.geometry=BoundsVisibility::Conservative;
            else {
                const auto bounds=cpu?cpu->inputs->values[index].bounds
                    :TransformBounds(frame.Resources()[draw.resources].Mesh()->Bounds(),draw.worldTransform);
                result.geometry=frustum.Test(bounds);RW_COUNT(visibilityTests,1);
            }
            if(result.geometry==BoundsVisibility::Outside){result.culled=true;return result;}
            result.visible=true;result.conservative=result.geometry==BoundsVisibility::Conservative;
            result.lists|=1u;
            const auto alpha=cpu?cpu->inputs->values[index].alpha:frame.Resources()[draw.resources].Material().Pipeline().Alpha();
            switch(alpha) {
                case AlphaMode::Opaque:result.lists|=1u<<1;break;
                case AlphaMode::Masked:result.lists|=1u<<2;break;
                case AlphaMode::Transparent:result.lists|=1u<<3;break;
            }
            if(draw.pickable)result.lists|=1u<<5;
            return result;
        }
    }

    std::expected<RenderVisibility,VisibilityError> RenderVisibility::Build(const RenderFrame& frame,
        std::size_t cameraIndex,std::span<const std::size_t> conservativeDraws) noexcept
    {
        if(cameraIndex>=frame.Cameras().size())
            return std::unexpected(VisibilityError{VisibilityErrorCode::InvalidCamera,cameraIndex});
        const auto count=frame.Draws().size();
        for(std::size_t i=0;i<conservativeDraws.size();++i)
            if(conservativeDraws[i]>=count||(i&&conservativeDraws[i]<=conservativeDraws[i-1]))
                return std::unexpected(VisibilityError{VisibilityErrorCode::InvalidConservativeDraw,i});
        if(count>static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)())/(6*sizeof(std::size_t)))
            return std::unexpected(VisibilityError{VisibilityErrorCode::CapacityOverflow,count});
        const auto& camera=frame.Cameras()[cameraIndex];
        const auto* storage=RenderCpu::FrameAccess::Storage(frame);
        const auto* shared=storage?std::get_if<RenderCpu::CpuOwner<const RenderCpu::DrawSnapshot>>(&storage->rows):nullptr;
        const auto* cpu=shared?shared->get():nullptr;
        RenderCpu::CpuOwner<const RenderCpu::VisibilitySnapshot> prior=cpu?storage->seed:nullptr;
        std::shared_ptr<RenderCpu::Domain> domain;
        if(cpu&&storage->owner==std::this_thread::get_id()&&storage->publicationEpoch) {
            auto owner=storage->domain.lock();
            if(owner&&owner->owner==storage->owner&&owner->publicationEpoch==storage->publicationEpoch) {
                domain=std::move(owner);prior=domain->visibility;
            }
        }
        auto Return=[&](RenderCpu::CpuOwner<const RenderCpu::VisibilitySnapshot> value) {
            RenderVisibility result;result.m_Snapshot=value.Detach();result.m_Capacity=count;result.m_Camera=cameraIndex;
            result.m_Stats=result.m_Snapshot->stats;
            for(std::size_t i=0;i<6;++i)result.m_Counts[i]=result.m_Snapshot->lists[i]?result.m_Snapshot->lists[i]->count:0;
            return result;
        };
        const bool compatible=cpu&&prior&&prior->orderEpoch==cpu->orderEpoch&&prior->contributions.size()==count;
        const bool sameCamera=compatible&&prior->cameraIndex==cameraIndex&&SameCamera(prior->camera,camera)&&prior->target==cpu->target;
        const bool sameMask=prior&&std::equal(conservativeDraws.begin(),conservativeDraws.end(),
            prior->conservativeDraws.begin(),prior->conservativeDraws.end());
        if(sameCamera&&sameMask&&prior->visibilityEpoch==cpu->visibilityEpoch) {
            RW_COUNT(visibilityHits,1);return Return(prior);
        }
        auto Failure=[&](RenderCpu::StorageFailure code) {
            return std::unexpected(VisibilityError{code==RenderCpu::StorageFailure::Capacity
                ?VisibilityErrorCode::CapacityOverflow:VisibilityErrorCode::AllocationFailed,count});
        };
        auto created=RenderCpu::CpuOwner<RenderCpu::VisibilitySnapshot>::Create();
        if(!created)return Failure(created.error());
        auto next=std::move(*created);
        next->camera=camera;next->cameraIndex=cameraIndex;
        if(cpu){next->target=cpu->target;next->orderEpoch=cpu->orderEpoch;next->visibilityEpoch=cpu->visibilityEpoch;}
        if(auto assigned=next->conservativeDraws.Assign(conservativeDraws);!assigned)return Failure(assigned.error());
        next->stats.inputDraws=count;
        if(auto sized=next->inputEpochs.Resize(count);!sized)return Failure(sized.error());
        if(auto sized=next->geometryEpochs.Resize(count);!sized)return Failure(sized.error());
        if(auto sized=next->contributions.Resize(count);!sized)return Failure(sized.error());
        const auto frustum=CameraFrustum::FromCamera(camera);
        RenderCpu::CpuArray<std::size_t> changes;
        const bool partial=sameCamera;
        if(partial) {
            RW_COUNT(visibilityPartial,1);
            if(auto copied=next->contributions.Assign({prior->contributions.data(),prior->contributions.size()});!copied)return Failure(copied.error());
            if(auto copied=next->inputEpochs.Assign({prior->inputEpochs.data(),prior->inputEpochs.size()});!copied)return Failure(copied.error());
            if(auto copied=next->geometryEpochs.Assign({prior->geometryEpochs.data(),prior->geometryEpochs.size()});!copied)return Failure(copied.error());
            next->stats=prior->stats;
            if(prior->visibilityEpoch==cpu->predecessorVisibilityEpoch) {
                if(auto copied=changes.Assign({cpu->changed.data(),cpu->changed.size()});!copied)return Failure(copied.error());
            } else for(std::size_t i=0;i<count;++i)
                if(prior->inputEpochs[i]!=cpu->inputs->values[i].visibilityEpoch)
                    if(auto appended=changes.Append(i);!appended)return Failure(appended.error());
            if(!sameMask) {
                // The same ordered symmetric difference, with explicit fallible append.
                std::size_t oldIndex{},newIndex{};
                const auto& oldMask=prior->conservativeDraws;
                while(oldIndex<oldMask.size()||newIndex<conservativeDraws.size()) {
                    if(oldIndex<oldMask.size()&&newIndex<conservativeDraws.size()&&oldMask[oldIndex]==conservativeDraws[newIndex])
                        {++oldIndex;++newIndex;continue;}
                    const auto ordinal=newIndex==conservativeDraws.size()
                        ||(oldIndex<oldMask.size()&&oldMask[oldIndex]<conservativeDraws[newIndex])
                        ?oldMask[oldIndex++]:conservativeDraws[newIndex++];
                    if(auto appended=changes.Append(ordinal);!appended)return Failure(appended.error());
                }
            }
            if(!changes.empty()) {
                std::sort(changes.begin(),changes.end());
                changes.Shrink(std::size_t(std::unique(changes.begin(),changes.end())-changes.begin()));
            }
        } else {
            RW_COUNT(visibilityFull,1);
            if(auto sized=changes.Resize(count);!sized)return Failure(sized.error());
            for(std::size_t i=0;i<count;++i)changes[i]=i;
        }
        std::array<bool,6> changedLists{};
        if(!compatible)changedLists.fill(true);
        for(const auto i:changes) {
            const bool conservative=std::binary_search(conservativeDraws.begin(),conservativeDraws.end(),i);
            const bool sameGeometry=partial&&prior->contributions[i].tested
                &&prior->geometryEpochs[i]==cpu->inputs->values[i].geometryEpoch
                &&conservative==std::binary_search(prior->conservativeDraws.begin(),prior->conservativeDraws.end(),i);
            const auto value=Classify(frame,cpu,i,camera,frustum,conservative,
                sameGeometry?&prior->contributions[i]:nullptr);
            if(partial)AddContribution(next->stats,next->contributions[i],false);
            AddContribution(next->stats,value,true);
            if(compatible)for(std::size_t list=0;list<6;++list)
                if((value.lists^(prior->contributions[i].lists))&(1u<<list))changedLists[list]=true;
            next->contributions[i]=value;
            if(cpu){next->inputEpochs[i]=cpu->inputs->values[i].visibilityEpoch;
                next->geometryEpochs[i]=cpu->inputs->values[i].geometryEpoch;}
        }
        for(std::size_t list=0;list<6;++list) {
            if(compatible&&!changedLists[list]){next->lists[list]=prior->lists[list];continue;}
            auto createdList=RenderCpu::CpuOwner<RenderCpu::IndexArray>::Create();
            if(!createdList)return Failure(createdList.error());
            auto values=std::move(*createdList);values->capacity=count;
            if(count) {
                values->values.reset(new(std::nothrow)std::size_t[count]);RW_COUNT(listAllocations,1);
                if(!values->values)return std::unexpected(VisibilityError{VisibilityErrorCode::AllocationFailed,count});
            }
            if(partial&&prior->lists[list]) {
                const auto& old=*prior->lists[list];std::size_t cursor{};
                for(const auto index:changes) {
                    const auto* begin=old.values.get()+cursor;
                    const std::size_t* end=old.values.get()+old.count;
                    const auto* found=std::lower_bound(begin,end,index);
                    const auto run=std::size_t(found-begin);
                    std::copy_n(begin,run,values->values.get()+values->count);
                    values->count+=run;cursor+=run;RW_COUNT(listBytesCopied,run*sizeof(std::size_t));
                    if(cursor<old.count&&old.values[cursor]==index)++cursor;
                    if(next->contributions[index].lists&(1u<<list)){values->values[values->count++]=index;RW_COUNT(listWrites,1);}
                }
                const auto tail=old.count-cursor;
                if(tail)std::copy_n(old.values.get()+cursor,tail,values->values.get()+values->count);
                values->count+=tail;RW_COUNT(listBytesCopied,tail*sizeof(std::size_t));
            } else {
                for(std::size_t i=0;i<count;++i)if(next->contributions[i].lists&(1u<<list))
                    {values->values[values->count++]=i;RW_COUNT(listWrites,1);}
            }
            next->lists[list]=std::move(values);
        }
        if(domain)domain->visibility=next;
        return Return(std::move(next));
    }
}
