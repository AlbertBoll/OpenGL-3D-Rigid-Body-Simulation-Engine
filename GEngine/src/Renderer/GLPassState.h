#pragma once

// Concrete GL pass setup. This header stays outside consumer dependency closures.
#include "GLStateCache.h"
#include "Renderer/FrameSubmission.h"

namespace GEngine::RenderBackend
{
    inline GLenum DepthFunction(DepthCompare value)
    {
        switch(value) {
        case DepthCompare::Never:return GL_NEVER; case DepthCompare::Less:return GL_LESS;
        case DepthCompare::Equal:return GL_EQUAL; case DepthCompare::LessEqual:return GL_LEQUAL;
        case DepthCompare::Greater:return GL_GREATER; case DepthCompare::NotEqual:return GL_NOTEQUAL;
        case DepthCompare::GreaterEqual:return GL_GEQUAL; case DepthCompare::Always:return GL_ALWAYS;
        }
        Asset::AssetDetail::RequireInvariant(false); return GL_LESS;
    }

    // Every draw/clear starts from this fixed raster baseline plus its semantic
    // descriptor. Material overrides occur afterwards. Disabled features' stored
    // parameters are irrelevant; a pass enabling one must declare those parameters.
    inline void ApplyPassState(GLStateCache& state, const RenderPassDesc& pass)
    {
        state.Viewport(pass.viewport.x,pass.viewport.y,pass.viewport.width,pass.viewport.height);
        state.Toggle(GL_SCISSOR_TEST,pass.scissor);
        state.Scissor(pass.viewport.x,pass.viewport.y,pass.viewport.width,pass.viewport.height);
        state.Toggle(GL_DEPTH_TEST,pass.depthTest); state.DepthMask(pass.depthWrite);
        state.DepthFunc(DepthFunction(pass.depthCompare));
        state.ColorMask(pass.colorWrite,pass.colorWrite,pass.colorWrite,pass.colorWrite);
        state.Toggle(GL_STENCIL_TEST,false); state.StencilMask(pass.stencilWrite?~0u:0u);
        state.Toggle(GL_BLEND,pass.blend); state.BlendEquation(GL_FUNC_ADD,GL_FUNC_ADD);
        state.BlendFunction(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        state.Toggle(GL_CULL_FACE,pass.cull!=CullMode::None);
        state.CullFace(pass.cull==CullMode::Front?GL_FRONT:GL_BACK);
        state.FrontFace(GL_CCW); state.Polygon(GL_FILL); state.LineWidth(1);
        for(const auto capability:{GL_RASTERIZER_DISCARD,GL_POLYGON_OFFSET_FILL,GL_POLYGON_OFFSET_LINE,
            GL_POLYGON_OFFSET_POINT,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE,GL_SAMPLE_MASK,
            GL_LINE_SMOOTH,GL_POLYGON_SMOOTH,GL_DEPTH_CLAMP,GL_FRAMEBUFFER_SRGB,GL_COLOR_LOGIC_OP,
            GL_PRIMITIVE_RESTART,GL_PRIMITIVE_RESTART_FIXED_INDEX,GL_PROGRAM_POINT_SIZE,GL_SAMPLE_SHADING,GL_SAMPLE_ALPHA_TO_ONE})
            state.Toggle(capability,false);
        for(GLint i=0;i<state.clipDistances;++i) state.Toggle(GL_CLIP_DISTANCE0+i,false);
        state.Toggle(GL_DITHER,pass.pass!=RenderPass::Picking);
        state.Toggle(GL_MULTISAMPLE,true); state.Toggle(GL_TEXTURE_CUBE_MAP_SEAMLESS,true);
        state.ClipControl(GL_LOWER_LEFT,GL_NEGATIVE_ONE_TO_ONE);
        state.DepthRange(0,1); state.ClearDepth(1); state.ClearColor(.1f,.1f,.1f,1.f);
    }

    // The cache ends before an external client or a native-context transition.
    // Draw/read routing belongs to each FBO, including each window's default FBO.
    inline void EstablishWindowState(const RenderPassDesc& pass)
    {
        GLStateCache state;
        ApplyPassState(state,pass);
        state.Framebuffer(GL_FRAMEBUFFER,0); state.DrawBuffer(GL_BACK); state.ReadBuffer(GL_BACK);
        state.Program(0); state.VertexArray(0);
        state.Texture(0,GL_TEXTURE_2D,0); state.Sampler(0,0);
    }
    inline void EstablishUIState(unsigned width, unsigned height)
    {
        RenderPassDesc pass;
        pass.pass=RenderPass::EditorUI; pass.viewport={0,0,width,height}; pass.blend=true;
        EstablishWindowState(pass);
    }
}
