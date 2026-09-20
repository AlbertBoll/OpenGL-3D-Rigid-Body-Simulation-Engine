#include "gepch.h"
#include "Renderer/ShadowQuality.h"
#include "Core/GLContextThread.h"
#include "../Core/FramebufferBackend.h"
#include <charconv>
#include <new>

namespace GEngine
{
    std::string_view ShadowQualityLabel(ShadowQuality quality) noexcept
    {
        switch(quality) {
        case ShadowQuality::Low:return "Low";
        case ShadowQuality::Medium:return "Medium";
        case ShadowQuality::High:return "High";
        case ShadowQuality::Custom:return "Custom";
        }
        return "Invalid";
    }
    std::expected<ShadowQualityPlan,FramebufferError> PlanShadowQuality(const ShadowQualityDesc& desc)
    {
        unsigned resolution{};
        switch(desc.quality) {
        case ShadowQuality::Low:resolution=1024;break;
        case ShadowQuality::Medium:resolution=2048;break;
        case ShadowQuality::High:resolution=4096;break;
        case ShadowQuality::Custom:resolution=desc.customResolution;break;
        default:return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidDescription,"Unknown shadow quality tier"});
        }
        if(!resolution || resolution>8192 || !desc.byteBudget
            || (desc.fallback!=ShadowFallback::None && desc.fallback!=ShadowFallback::LowerTiers))
            return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidDescription,
                "Shadow resolution must be 1..8192 with a positive byte budget and valid fallback policy"});
        return ShadowQualityPlan{resolution,12ull*resolution*resolution*4};
    }
    std::expected<ShadowQualityDesc,FramebufferError> ParseShadowQuality(const char* tier,const char* resolution)
    {
        ShadowQualityDesc desc;
        if(tier) {
            const std::string_view value(tier);
            if(value=="Low" || value=="low") desc.quality=ShadowQuality::Low;
            else if(value=="Medium" || value=="medium") desc.quality=ShadowQuality::Medium;
            else if(value=="High" || value=="high") desc.quality=ShadowQuality::High;
            else if(value=="Custom" || value=="custom") desc.quality=ShadowQuality::Custom;
            else return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidDescription,
                "GENGINE_SHADOW_QUALITY must be Low, Medium, High or Custom"});
        }
        if(resolution) {
            const std::string_view value(resolution);
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),desc.customResolution);
            if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size())
                return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidDescription,
                    "GENGINE_SHADOW_RESOLUTION must be an integer from 1 to 8192"});
            desc.quality=ShadowQuality::Custom; // Existing explicit resolution has priority.
        } else if(desc.quality==ShadowQuality::Custom)
            return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidDescription,
                "Custom shadow quality requires GENGINE_SHADOW_RESOLUTION"});
        if(auto plan=PlanShadowQuality(desc);!plan) return std::unexpected(plan.error());
        return desc;
    }
    std::expected<ShadowQualityDesc,FramebufferError> ShadowQualityFromEnvironment()
    { return ParseShadowQuality(SDL_getenv("GENGINE_SHADOW_QUALITY"),SDL_getenv("GENGINE_SHADOW_RESOLUTION")); }
    namespace
    {
        std::expected<std::uint64_t,FramebufferError> AllocatedDepth(const FrameBuffer& target)
        {
            const auto name=FramebufferDetail::Backend::Depth(target);
            const auto& desc=target.Description();
            GLint width{},height{},depth{},bits{},format{};
            glGetTextureLevelParameteriv(name,0,GL_TEXTURE_WIDTH,&width);
            glGetTextureLevelParameteriv(name,0,GL_TEXTURE_HEIGHT,&height);
            glGetTextureLevelParameteriv(name,0,GL_TEXTURE_DEPTH,&depth);
            glGetTextureLevelParameteriv(name,0,GL_TEXTURE_DEPTH_SIZE,&bits);
            glGetTextureLevelParameteriv(name,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
            if(glGetError()!=GL_NO_ERROR || width!=static_cast<int>(desc.Width) || height!=static_cast<int>(desc.Height)
                || bits!=32 || format!=GL_DEPTH_COMPONENT32F || (desc.Kind==FramebufferKind::Array && depth!=6))
                return std::unexpected(FramebufferError{FramebufferErrorCode::Storage,
                    "Shadow depth storage query does not match the six-image 32-bit allocation",desc.Width,desc.Height});
            return std::uint64_t(width)*height*6*static_cast<unsigned>(bits)/8;
        }
        std::expected<ShadowTargets,FramebufferError> Allocate(const ShadowQualityDesc& requested,ShadowQuality quality,unsigned resolution)
        {
            const auto estimate=12ull*resolution*resolution*4;
            if(estimate>requested.byteBudget)
                return std::unexpected(FramebufferError{FramebufferErrorCode::Unsupported,
                    "Shadow depth payload exceeds the configured byte budget",resolution,resolution});
            auto cascade=CascadeShadowFrameBuffer::Create(resolution,resolution,5);
            const auto cascadeError=glGetError();
            if(!cascade) return std::unexpected(cascade.error());
            if(cascadeError!=GL_NO_ERROR) return std::unexpected(FramebufferError{FramebufferErrorCode::Storage,
                "Driver rejected cascade depth storage",resolution,resolution});
            auto point=PointShadowFrameBuffer::Create(resolution,resolution);
            const auto pointError=glGetError();
            if(!point) return std::unexpected(point.error());
            if(pointError!=GL_NO_ERROR) return std::unexpected(FramebufferError{FramebufferErrorCode::Storage,
                "Driver rejected point depth storage",resolution,resolution});
            auto cascadeBytes=AllocatedDepth(cascade->Buffer());if(!cascadeBytes) return std::unexpected(cascadeBytes.error());
            auto pointBytes=AllocatedDepth(point->Buffer());if(!pointBytes) return std::unexpected(pointBytes.error());
            ShadowTargets result;
            result.cascade.reset(new (std::nothrow) CascadeShadowFrameBuffer(std::move(*cascade)));
            result.point.reset(new (std::nothrow) PointShadowFrameBuffer(std::move(*point)));
            if(!result.cascade || !result.point) return std::unexpected(FramebufferError{FramebufferErrorCode::Allocation,
                "Shadow target owner allocation failed",resolution,resolution});
            result.state.requested=requested;result.state.effective=quality;result.state.resolution=resolution;
            result.state.memory={estimate,*cascadeBytes+*pointBytes,*cascadeBytes,*pointBytes,std::nullopt};
            return result;
        }
        bool CanFallback(FramebufferErrorCode code)
        { return code==FramebufferErrorCode::Unsupported || code==FramebufferErrorCode::Allocation
            || code==FramebufferErrorCode::Storage || code==FramebufferErrorCode::Incomplete; }
    }
    std::expected<ShadowTargets,FramebufferError> CreateShadowTargets(const ShadowQualityDesc& desc)
    {
        auto plan=PlanShadowQuality(desc);if(!plan) return std::unexpected(plan.error());
        if(!GLContextThread::IsCurrentOwner()) return std::unexpected(FramebufferError{FramebufferErrorCode::ContextUnavailable,
            "Shadow allocation requires the owning current context"});
        if(glGetError()!=GL_NO_ERROR) return std::unexpected(FramebufferError{FramebufferErrorCode::InvalidOperation,
            "Resolve the pending driver error before changing shadow quality"});
        auto result=Allocate(desc,desc.quality,plan->resolution);
        if(result || desc.fallback==ShadowFallback::None || !CanFallback(result.error().code)) return result;
        const auto original=result.error();
        for(const auto tier:{ShadowQuality::High,ShadowQuality::Medium,ShadowQuality::Low}) {
            auto lower=desc;lower.quality=tier;const auto candidate=PlanShadowQuality(lower)->resolution;
            if(candidate>=plan->resolution) continue;
            result=Allocate(desc,tier,candidate);
            if(result) {result->state.fallbackReason=original;return result;}
            GENGINE_CORE_WARN("Shadow fallback {} failed: code={}, {}",ShadowQualityLabel(tier),int(result.error().code),result.error().message);
            if(!CanFallback(result.error().code)) break;
        }
        return std::unexpected(original);
    }
}
