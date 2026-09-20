#pragma once

// Backend-private whole-store replacement. GL retains storage needed by already
// queued commands; no unsynchronized mapping or CPU overwrite of in-flight bytes.
#include "SubmissionGpuLayout.h"
#include "Renderer/FrameSubmission.h"
#include <bit>
#include <cstring>
#include <format>
#include <limits>
#include <new>

namespace GEngine::RenderBackend
{
    inline auto UploadError(const char* operation,SubmissionCode code)
    { return std::unexpected(SubmissionError{operation,code}); }

    struct MaterialBatch
    {
        std::unique_ptr<std::uint32_t[]> words, offsets;
        std::size_t wordCount{};
        static std::expected<MaterialBatch,SubmissionError> Pack(const RenderFrame& frame,
            std::span<const ScenePipeline> roles,std::size_t limit)
        {
            if(limit<32) return UploadError("material batch minimum storage",SubmissionCode::InvalidDraw);
            MaterialBatch result;
            const auto resources=frame.Resources();
            result.offsets.reset(new(std::nothrow) std::uint32_t[resources.size()]);
            if(!resources.empty() && !result.offsets) return UploadError("material offsets",SubmissionCode::Allocation);
            // Count with full-width arithmetic before any allocation or narrowing.
            for(std::size_t i=0;i<resources.size();++i) {
                const auto& material=resources[i].Material();
                std::size_t shared=i;
                for(std::size_t j=0;j<i;++j) if(resources[j].Material().Instance()==material.Instance()
                    && resources[j].Material().PublicationRevision()==material.PublicationRevision()
                    && resources[j].Material().Revision()==material.Revision()) { shared=j;break; }
                if(shared<i) {result.offsets[i]=result.offsets[shared];continue;}
                const auto words=material.PackedWords().size();
                const auto maximum=(std::min)(limit/sizeof(std::uint32_t),std::size_t((std::numeric_limits<std::uint32_t>::max)()));
                if(words%4 || words>maximum || maximum-words<8 || result.wordCount>maximum-words-8)
                    return UploadError("material batch byte limit",SubmissionCode::InvalidDraw);
                result.offsets[i]=static_cast<std::uint32_t>(result.wordCount/4);
                result.wordCount+=8+words;
            }
            // A nonempty backing store is required even for an empty frame.
            if(!result.wordCount) result.wordCount=8;
            result.words.reset(new(std::nothrow) std::uint32_t[result.wordCount]{});
            if(!result.words) return UploadError("material batch allocation",SubmissionCode::Allocation);
            for(std::size_t i=0;i<resources.size();++i) {
                const auto& material=resources[i].Material();
                const ScenePipeline* role=nullptr;
                for(const auto& entry:roles) if(entry.pipeline==material.Source()->Declaration()->Pipeline()) {role=&entry;break;}
                if(!role) return UploadError("material batch pipeline",SubmissionCode::UnsupportedPipeline);
                auto* destination=result.words.get()+std::size_t(result.offsets[i])*4;
                const auto& pipeline=material.Pipeline().Description();
                destination[0]=static_cast<std::uint32_t>(pipeline.alpha);
                destination[1]=std::bit_cast<std::uint32_t>(pipeline.alphaCutoff);
                destination[2]=std::bit_cast<std::uint32_t>(role->opacity);
                destination[3]=pipeline.transparentBlend==TransparentBlend::PremultipliedAlpha;
                destination[4]=destination[5]=std::bit_cast<std::uint32_t>(1.f);
                const auto parameters=material.Source()->Declaration()->Parameters();
                for(std::size_t j=0;j<parameters.size();++j) if(parameters[j].declaration.name=="u_tiling") {
                    if(material.Parameters()[j].type!=MaterialParameterType::Float2)
                        return UploadError("material tiling layout",SubmissionCode::UnsupportedPipeline);
                    const auto at=material.Parameters()[j].wordOffset;
                    destination[4]=material.PackedWords()[at];destination[5]=material.PackedWords()[at+1];
                }
                std::copy(material.PackedWords().begin(),material.PackedWords().end(),destination+8);
            }
            return result;
        }
        std::span<const std::byte> Bytes() const
        { return std::as_bytes(std::span(words.get(),wordCount)); }
    };

    class UploadBuffer final
    {
        GLuint name{};
        std::unique_ptr<std::byte[]> previous;
        std::size_t size{};
    public:
        UploadBuffer()=default;
        UploadBuffer(const UploadBuffer&)=delete;
        UploadBuffer& operator=(const UploadBuffer&)=delete;
        ~UploadBuffer() { if(name) glDeleteBuffers(1,&name); }
        GLuint Name() const { return name; }
        std::expected<void,SubmissionError> Update(std::span<const std::byte> bytes,GLenum usage)
        {
            if(previous && size==bytes.size() && std::memcmp(previous.get(),bytes.data(),size)==0) return {};
            if(bytes.empty() || bytes.size()>std::size_t((std::numeric_limits<GLsizeiptr>::max)()))
                return UploadError("upload byte count",SubmissionCode::InvalidDraw);
            std::unique_ptr<std::byte[]> candidate(new(std::nothrow) std::byte[bytes.size()]);
            if(!candidate) return UploadError("upload comparison snapshot",SubmissionCode::Allocation);
            std::memcpy(candidate.get(),bytes.data(),bytes.size());
            if(!name) glCreateBuffers(1,&name);
            if(!name) return UploadError("upload buffer allocation",SubmissionCode::Allocation);
            glNamedBufferData(name,static_cast<GLsizeiptr>(bytes.size()),bytes.data(),usage);
            // A failed replacement never makes its CPU comparison snapshot valid.
            if(const auto error=glGetError();error!=GL_NO_ERROR) {
                previous.reset();size=0;
                return std::unexpected(SubmissionError{std::format("packed upload: driver diagnostic 0x{:x}",error),SubmissionCode::Driver});
            }
            previous=std::move(candidate);size=bytes.size();return {};
        }
    };
    // Preserve exact indexed ranges as well as generic binding state. This scope
    // ends on every Submit return, before UI/legacy/context-switch boundaries.
    struct UploadBindings
    {
        struct Binding { GLint name{};GLint64 start{},size{}; } frame,material;
        GLint uniform{},storage{};
        static Binding Capture(GLenum binding,GLenum start,GLenum size,GLuint index)
        {
            Binding result;glGetIntegeri_v(binding,index,&result.name);
            glGetInteger64i_v(start,index,&result.start);glGetInteger64i_v(size,index,&result.size);return result;
        }
        UploadBindings()
        {
            glGetIntegerv(GL_UNIFORM_BUFFER_BINDING,&uniform);glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&storage);
            frame=Capture(GL_UNIFORM_BUFFER_BINDING,GL_UNIFORM_BUFFER_START,GL_UNIFORM_BUFFER_SIZE,1);
            material=Capture(GL_SHADER_STORAGE_BUFFER_BINDING,GL_SHADER_STORAGE_BUFFER_START,GL_SHADER_STORAGE_BUFFER_SIZE,0);
        }
        static void Restore(GLenum target,GLuint index,const Binding& value)
        {
            if(value.name && value.size) glBindBufferRange(target,index,value.name,value.start,value.size);
            else glBindBufferBase(target,index,value.name);
        }
        ~UploadBindings()
        {
            Restore(GL_UNIFORM_BUFFER,1,frame);Restore(GL_SHADER_STORAGE_BUFFER,0,material);
            glBindBuffer(GL_UNIFORM_BUFFER,uniform);glBindBuffer(GL_SHADER_STORAGE_BUFFER,storage);
        }
    };
}
