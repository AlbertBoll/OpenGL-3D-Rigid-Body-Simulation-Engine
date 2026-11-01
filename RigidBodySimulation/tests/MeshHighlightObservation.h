#pragma once
// Isolated validation only. Same-frame observation follows the existing Phase24
// GL dispatch interception technique; it is inactive in all timed samples.
namespace Rbs::MeshHighlight
{
    inline PFNGLDRAWARRAYSPROC nativeArrays{};
    inline PFNGLDRAWELEMENTSINSTANCEDPROC nativeInstanced{};
    inline unsigned secondaryInstances{};
    inline void APIENTRY ObserveInstanced(GLenum mode,GLsizei count,GLenum type,const void* indices,GLsizei instances)
    {
        if(tick==40 && glIsEnabled(GL_STENCIL_TEST)) {
            GLint reference{};glGetIntegerv(GL_STENCIL_REF,&reference);
            std::fprintf(highlightLog,"INSTANCED\t%u\t%d\t%d\n",stage,reference,instances);
            if(reference==2)secondaryInstances+=unsigned(instances);
        }
        nativeInstanced(mode,count,type,indices,instances);
    }
    inline unsigned capturedStage=~0u;
    inline void Save(const std::string& suffix,const void* data,std::size_t size)
    {
        FILE* file{}; const auto path=directory+"/same-frame-"+std::to_string(stage)+suffix;
        if(fopen_s(&file,path.c_str(),"wb")||!file){Check(false,"same-frame evidence open");return;}
        Check(std::fwrite(data,1,size,file)==size,"same-frame evidence write");std::fclose(file);
    }
    inline void APIENTRY ObserveArrays(GLenum mode,GLint first,GLsizei count)
    {
        GLint program{};
        if(tick==86 && capturedStage!=stage && mode==GL_TRIANGLES && count==3)
            glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        const GLint selected=program?glGetUniformLocation(program,"selectedMask"):-1;
        if(selected<0){nativeArrays(mode,first,count);return;}
        capturedStage=stage;
        GLint fbo{},origin[2]{},size[2]{},active{},mask{},width{},height{},format{},stencilBits{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
        glGetUniformiv(program,glGetUniformLocation(program,"regionOrigin"),origin);
        glGetUniformiv(program,glGetUniformLocation(program,"regionSize"),size);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&mask);glActiveTexture(active);
        glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_WIDTH,&width);
        glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_HEIGHT,&height);
        glGetTextureLevelParameteriv(mask,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
        glGetNamedFramebufferAttachmentParameteriv(fbo,GL_STENCIL_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE,&stencilBits);
        std::vector<float> coverage(std::size_t(width)*height*4);
        glGetTextureImage(mask,0,GL_RGBA,GL_FLOAT,GLsizei(coverage.size()*sizeof(float)),coverage.data());
        Save("-mask.f32",coverage.data(),coverage.size()*sizeof(float));
        GLuint texture{},resolved{};glCreateTextures(GL_TEXTURE_2D,1,&texture);
        glTextureStorage2D(texture,1,GL_RGBA8,size[0],size[1]);glCreateFramebuffers(1,&resolved);
        glNamedFramebufferTexture(resolved,GL_COLOR_ATTACHMENT0,texture,0);
        std::vector<unsigned char> before(std::size_t(size[0])*size[1]*4),after(before.size());
        const bool scissor=glIsEnabled(GL_SCISSOR_TEST);
        const auto read=[&](auto& pixels){
            glDisable(GL_SCISSOR_TEST);
            glBlitNamedFramebuffer(fbo,resolved,origin[0],origin[1],origin[0]+size[0],origin[1]+size[1],0,0,size[0],size[1],GL_COLOR_BUFFER_BIT,GL_NEAREST);
            glGetTextureImage(texture,0,GL_RGBA,GL_UNSIGNED_BYTE,GLsizei(pixels.size()),pixels.data());
            if(scissor)glEnable(GL_SCISSOR_TEST);
        };
        read(before);nativeArrays(mode,first,count);read(after);
        Save("-before.rgba",before.data(),before.size());Save("-after.rgba",after.data(),after.size());
        glDeleteFramebuffers(1,&resolved);glDeleteTextures(1,&texture);
        GLint samples{};glGetUniformiv(program,glGetUniformLocation(program,"sampleCount"),&samples);
        std::fprintf(highlightLog,"MSAA\t%u\t%d\n",stage,samples);
        GLfloat dpi[2]{};glGetUniformfv(program,glGetUniformLocation(program,"pixelsPerDip"),dpi);
        std::fprintf(highlightLog,"SAME_FRAME\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.3f\t%.3f\n",
            stage,origin[0],origin[1],size[0],size[1],width,height,format,stencilBits,dpi[0],dpi[1]);
        Check(glGetError()==GL_NO_ERROR,"same-frame color/mask observation preserves GL state");
    }
}
