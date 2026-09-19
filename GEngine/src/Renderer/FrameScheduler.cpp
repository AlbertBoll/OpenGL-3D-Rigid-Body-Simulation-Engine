#include "gepch.h"
#include "Renderer/FrameScheduler.h"
#include "Core/RenderTarget.h"
#include "Core/GLContextThread.h"
#include "Managers/WindowManager.h"
#include <format>

namespace GEngine
{
    std::string DescribeScheduleError(const ScheduleError& error)
    {
        return std::format("frame stage={}: ", int(error.stage)) + std::visit([](const auto& cause)->std::string {
            using T = std::decay_t<decltype(cause)>;
            if constexpr (std::same_as<T, ScheduleCode>) return std::format("scheduler code={}", int(cause));
            else if constexpr (std::same_as<T, SubmissionError>) return DescribeSubmissionError(cause);
            else if constexpr (std::same_as<T, SceneResourceError>) return DescribeSceneResourceError(cause);
            else if constexpr (std::same_as<T, PlatformError>) return std::format("platform code={} operation={} message={}", int(cause.code), cause.operation, cause.message);
            else if constexpr (std::same_as<T, FramebufferError>) return std::format("framebuffer code={} size={}x{} samples={} message={}",int(cause.code),cause.width,cause.height,cause.samples,cause.message);
            else return std::format("extraction entity={}/{}/{} cause-domain={} ",cause.entity.index,cause.entity.generation,cause.entity.registry,cause.cause.index()) +
                std::visit([](const auto& detail)->std::string {
                    using D = std::decay_t<decltype(detail)>;
                    if constexpr (std::is_enum_v<D>) return std::format("code={}",int(detail));
                    else if constexpr (std::same_as<D, MaterialBindingError>) return std::format("material code={} binding={} message={} registry={} fallback={}",int(detail.code),detail.binding,detail.message,int(detail.registry),detail.cause?int(*detail.cause):-1);
                    else if constexpr (std::same_as<D, FrameError>) return std::format("frame code={} section={} element={}",int(detail.code),int(detail.section),detail.element);
                    else if constexpr (std::same_as<D, RenderWorkError>) {
                        auto message=std::format("task code={} element={} boundary={} cause-domain={}",int(detail.code),detail.element,detail.boundary,detail.cause.index());
                        if(const auto* system=std::get_if<std::error_code>(&detail.cause)) message+=std::format(" system={} category={} message={}",system->value(),system->category().name(),system->message());
                        return message;
                    }
                    else return std::format("transform code={} entity={} parent={}",int(detail.code),static_cast<std::uint64_t>(detail.entity),static_cast<std::uint64_t>(detail.parent));
                },cause.cause);
        }, error.cause);
    }

    namespace
    {
        // Context operations belong to this concrete backend, not its callbacks.
        void ExternalState()
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
            glDisable(GL_RASTERIZER_DISCARD); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
            glDisable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            glStencilMask(0); glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
            glActiveTexture(GL_TEXTURE0); glBindSampler(0,0); glUseProgram(0); glBindVertexArray(0);
        }
    }
    RenderPassDesc FrameScheduler::DescribeBoundary(RenderPass pass,const RenderContext& context)
    {
        RenderPassDesc result;
        result.pass=pass;
        const auto size=context.window.GetFramebufferPixelSize();
        result.viewport={0,0,size.Width,size.Height};
        result.output=PassTarget::Window;
        if(pass==RenderPass::Resolve) {
            result.input=PassTarget::SceneColor; result.output=PassTarget::ResolvedColor;
            if(context.color) result.viewport={0,0,context.color->Description().Storage.Width,context.color->Description().Storage.Height};
        }
        else if(pass==RenderPass::EditorUI) {
            result.input=context.color?PassTarget::ResolvedColor:PassTarget::None;
            result.blend=true; result.boundary=PassBoundary::EstablishBeforeUI;
        }
        else if(pass==RenderPass::Present) {
            result.input=PassTarget::Window; result.colorWrite=false; result.boundary=PassBoundary::Presentation;
        }
        else {
            result.output=context.color?PassTarget::SceneColor:PassTarget::Window;
            result.boundary=PassBoundary::RestoreAfterLegacy;
        }
        return result;
    }
    std::expected<ScheduledFrameStats, ScheduleError> FrameScheduler::Render(
        const RenderContext& context, const FrameSceneInput* scene)
    {
        if (!GLContextThread::IsCurrentOwner() || !context.window.IsCurrent())
            return std::unexpected(ScheduleError{FrameStage::BeginFrame,ScheduleCode::Context});
        static thread_local bool active=false;
        if(active) return std::unexpected(ScheduleError{FrameStage::BeginFrame,ScheduleCode::FrameActive});
        if ((scene && context.legacyScene.invoke) || (scene && context.color != &scene->targets.color))
            return std::unexpected(ScheduleError{FrameStage::BeginFrame,ScheduleCode::InvalidInput});
        if(scene && !scene->resources.Publication().CanPublish())
            return std::unexpected(ScheduleError{FrameStage::UpdateFrameResources,ScheduleCode::FrameActive});
        active=true;
        struct EndFrame { bool& active; ~EndFrame() { active=false; } } end{active};
        ScheduledFrameStats stats;
        stats.trace.Stage(FrameStage::BeginFrame);
        stats.trace.Stage(FrameStage::UpdateFrameResources);
        if (auto updated=context.updateResources(); !updated) return std::unexpected(updated.error());
        if (context.visible && scene)
        {
            if (!scene->resources.Publication().CanPublish())
                return std::unexpected(ScheduleError{FrameStage::FreezeFrameInputs,ScheduleCode::FrameActive});
            stats.trace.Stage(FrameStage::FreezeFrameInputs);
            auto access=scene->resources.Publication().BeginFrame();
            stats.trace.Stage(FrameStage::BuildRenderFrame);
            // This entry prepares and freezes ECS before any builder work; its
            // serial and optional worker paths share the pinned resource domain.
            auto frame=ExtractRenderFrame(scene->scene,scene->resources.ForFrame(access),stats.extraction,scene->cameras,{},scene->extraction);
            if (!frame) return std::unexpected(ScheduleError{FrameStage::BuildRenderFrame,frame.error()});
            auto targets=scene->targets;
            // A synchronous replacement can grow the role table. Borrow its
            // current storage only after publication has completed.
            targets.pipelines=scene->resources.Pipelines();
            auto submitted=scene->submission.Submit(*frame,targets,scene->picks);
            if (!submitted) return std::unexpected(ScheduleError{FrameStage::Pass,submitted.error()});
            stats.submission=std::move(*submitted);
            for (auto event:stats.submission.trace.Events()) stats.trace.Pass(event.contract);
        }
        else if (context.visible && context.legacyScene.invoke)
        {
            stats.trace.Pass(DescribeBoundary(RenderPass::LegacyScene,context));
            if(auto legacy=context.legacyScene(); !legacy) { ExternalState(); return std::unexpected(legacy.error()); }
            ExternalState();
        }
        if (context.visible && scene && scene->targets.pickingEnabled)
            if (auto picked=scene->pickingReadback(); !picked) return std::unexpected(picked.error());
        if (context.visible && context.color)
        {
            stats.trace.Pass(DescribeBoundary(RenderPass::Resolve,context));
            if(context.color->IsMultiSampled())
                if(auto result=context.color->BindAndBlitToScreen(); !result)
                    return std::unexpected(ScheduleError{FrameStage::Pass,result.error()});
            context.color->UnBind();
        }
        // The entire scene is finished before entering any external UI backend.
        // Window IDs give deterministic traversal of the legacy unordered owner map.
        auto& windows=context.windows.GetWindows();
        auto nextWindow=[&](std::uint32_t previous)->Window* {
            Window* next=nullptr;
            for(auto& [id,window]:windows) if(id>previous && (!next || id<next->GetWindowID())) next=window.get();
            return next;
        };
        if(context.editorUI.invoke)
        {
            stats.trace.Pass(DescribeBoundary(RenderPass::EditorUI,context));
            for(auto* window=nextWindow(0);window;window=nextWindow(window->GetWindowID()))
            {
                if(auto current=window->BeginRender(); !current) return std::unexpected(ScheduleError{FrameStage::Pass,current.error()});
                ExternalState();
                const auto size=window->GetFramebufferPixelSize(); glViewport(0,0,size.Width,size.Height);
                if(auto ui=window->BeginUI(); !ui) return std::unexpected(ScheduleError{FrameStage::Pass,ui.error()});
                auto authored=context.editorUI();
                auto finished=window->EndUI(); // Balance a successful begin even if authoring fails.
                if(!authored) return std::unexpected(authored.error());
                if(!finished) return std::unexpected(ScheduleError{FrameStage::Pass,finished.error()});
            }
        }
        stats.trace.Pass(DescribeBoundary(RenderPass::Present,context));
        for(auto* window=nextWindow(0);window;window=nextWindow(window->GetWindowID()))
        {
            if(auto current=window->BeginRender(); !current) return std::unexpected(ScheduleError{FrameStage::Pass,current.error()});
            window->SwapBuffer();
        }
        if(auto current=context.window.BeginRender(); !current) return std::unexpected(ScheduleError{FrameStage::Pass,current.error()});
        return stats;
    }
}
