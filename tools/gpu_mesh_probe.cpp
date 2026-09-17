#include "gepch.h"
#include "Mesh/GpuMesh.h"
#include "Core/GLContextThread.h"
#include <new>
#include <print>
#include <cstring>
#include <cmath>
#include <unordered_set>

static int denyAllocation = -1;
void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
    if (denyAllocation == 0) { denyAllocation = -1; return nullptr; }
    if (denyAllocation > 0) --denyAllocation;
    return std::malloc(n ? n : 1);
}
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void* operator new[](std::size_t n, const std::nothrow_t& tag) noexcept { return ::operator new(n, tag); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace
{
    using namespace GEngine;
    using namespace GEngine::Asset;
    using E = GpuMeshErrorCode;
    int checks = 0, debugErrors = 0;
    void InstallExpectedTermination()
    { std::set_terminate([]{std::println(stderr,"[EXPECTED] gpu-mesh ownership invariant");std::_Exit(86);}); }
    template<class T> void Check(const T& value, const char* label)
    {
        ++checks;
        if (!static_cast<bool>(value)) { std::println(stderr, "[FAIL] {}", label); std::exit(1); }
    }
    template<class T> void Reject(const T& result, E expected)
    { Check(!result && result.error().code == expected, "Wrong expected GPU mesh error"); }
    void APIENTRY DebugMessage(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*)
    {
        if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH)
        { ++debugErrors; std::println(stderr, "[GL ERROR] {}", message); }
    }
    struct Observer
    {
        inline static PFNGLGENBUFFERSPROC genBuffer;
        inline static PFNGLDELETEBUFFERSPROC deleteBuffer;
        inline static PFNGLGENVERTEXARRAYSPROC genArray;
        inline static PFNGLDELETEVERTEXARRAYSPROC deleteArray;
        inline static PFNGLBUFFERDATAPROC bufferData;
        inline static PFNGLBUFFERSUBDATAPROC bufferSubData;
        inline static PFNGLVERTEXATTRIBPOINTERPROC pointer;
        inline static PFNGLGETINTEGERVPROC getInteger;
        inline static PFNGLGETERRORPROC getError;
        inline static std::unordered_set<GLuint> buffers, arrays;
        inline static SDL_GLContext context;
        inline static std::thread::id thread;
        inline static int failName = -1, failUpload = -1;
        inline static bool failAttribute = false, failUpdate = false, errorPending = false, tinyStride = false, tinySlots = false;
        inline static bool valid = true;
        inline static int vertexUploads = 0, indexUploads = 0, updates = 0;
        inline static GLuint lastVertex = 0;
        inline static GLenum vertexUsage = 0;
        inline static GLintptr updateOffset = 0;
        inline static GLsizeiptr updateBytes = 0;
        static bool Deny(int& count)
        { if (count < 0) return false; if (count-- == 0) { count = -1; return true; } return false; }
        static void Owner() { valid &= SDL_GL_GetCurrentContext() == context && std::this_thread::get_id() == thread; }
        static void APIENTRY GenBuffer(GLsizei n, GLuint* names)
        {
            Owner(); if (Deny(failName)) { std::fill_n(names, n, 0); return; }
            genBuffer(n, names); for (int i=0;i<n;++i) valid &= names[i] && buffers.insert(names[i]).second;
        }
        static void APIENTRY GenArray(GLsizei n, GLuint* names)
        {
            Owner(); if (Deny(failName)) { std::fill_n(names, n, 0); return; }
            genArray(n, names); for (int i=0;i<n;++i) valid &= names[i] && arrays.insert(names[i]).second;
        }
        static void APIENTRY DeleteBuffer(GLsizei n, const GLuint* names)
        { Owner(); for(int i=0;i<n;++i) valid &= names[i] && buffers.erase(names[i])==1; deleteBuffer(n,names); }
        static void APIENTRY DeleteArray(GLsizei n, const GLuint* names)
        { Owner(); for(int i=0;i<n;++i) valid &= names[i] && arrays.erase(names[i])==1; deleteArray(n,names); }
        static void APIENTRY Data(GLenum target, GLsizeiptr bytes, const void* data, GLenum usage)
        {
            Owner();
            if (target == GL_ARRAY_BUFFER)
            { ++vertexUploads; vertexUsage=usage; GLint name=0; getInteger(GL_ARRAY_BUFFER_BINDING,&name); lastVertex=name; }
            if (target == GL_ELEMENT_ARRAY_BUFFER) ++indexUploads;
            if (!Deny(failUpload)) bufferData(target,bytes,data,usage);
        }
        static void APIENTRY SubData(GLenum target, GLintptr offset, GLsizeiptr bytes, const void* data)
        {
            Owner(); ++updates; updateOffset=offset; updateBytes=bytes;
            if (std::exchange(failUpdate,false)) { errorPending=true; return; }
            bufferSubData(target,offset,bytes,data);
        }
        static void APIENTRY Pointer(GLuint slot, GLint count, GLenum type, GLboolean normalized, GLsizei stride, const void* offset)
        { if (!std::exchange(failAttribute,false)) pointer(slot,count,type,normalized,stride,offset); }
        static void APIENTRY Integer(GLenum key, GLint* value)
        {
            getInteger(key,value);
            if (tinyStride && key==GL_MAX_VERTEX_ATTRIB_STRIDE) *value=4;
            if (tinySlots && key==GL_MAX_VERTEX_ATTRIBS) *value=1;
        }
        static GLenum APIENTRY Error() { if(std::exchange(errorPending,false)) return GL_OUT_OF_MEMORY; return getError(); }
        Observer()
        {
            context=SDL_GL_GetCurrentContext(); thread=std::this_thread::get_id(); valid=true;
            buffers.clear(); arrays.clear(); vertexUploads=indexUploads=updates=0;
            genBuffer=glad_glGenBuffers; glad_glGenBuffers=GenBuffer;
            deleteBuffer=glad_glDeleteBuffers; glad_glDeleteBuffers=DeleteBuffer;
            genArray=glad_glGenVertexArrays; glad_glGenVertexArrays=GenArray;
            deleteArray=glad_glDeleteVertexArrays; glad_glDeleteVertexArrays=DeleteArray;
            bufferData=glad_glBufferData; glad_glBufferData=Data;
            bufferSubData=glad_glBufferSubData; glad_glBufferSubData=SubData;
            pointer=glad_glVertexAttribPointer; glad_glVertexAttribPointer=Pointer;
            getInteger=glad_glGetIntegerv; glad_glGetIntegerv=Integer;
            getError=glad_glGetError; glad_glGetError=Error;
        }
        ~Observer()
        {
            glad_glGenBuffers=genBuffer; glad_glDeleteBuffers=deleteBuffer;
            glad_glGenVertexArrays=genArray; glad_glDeleteVertexArrays=deleteArray;
            glad_glBufferData=bufferData; glad_glBufferSubData=bufferSubData;
            glad_glVertexAttribPointer=pointer; glad_glGetIntegerv=getInteger; glad_glGetError=getError;
        }
    };

    struct P { float position[3]; };
    struct PN { float position[3], normal[3]; };
    struct PNUV { float position[3], normal[3], uv[2]; };
#pragma warning(push)
#pragma warning(disable:4324) // Intentional padding fixture.
    struct alignas(32) Padded
    { std::uint8_t prefix; alignas(16) float position[3]; alignas(16) float normal[3]; float uv[2]; };
#pragma warning(pop)
    VertexAttribute Attribute(VertexSemantic semantic, std::size_t offset, std::uint8_t count,
        VertexScalarFormat scalar=VertexScalarFormat::Float32, VertexInterpretation interpretation=VertexInterpretation::Floating)
    { return {semantic,AttributeSlot(semantic),scalar,count,interpretation,offset}; }
    const std::array paddedLayout{Attribute(VertexSemantic::TexCoord0,offsetof(Padded,uv),2),
        Attribute(VertexSemantic::Position,offsetof(Padded,position),3), Attribute(VertexSemantic::Normal,offsetof(Padded,normal),3)};
    const std::array ranges{SubmeshRange{0,3,1},SubmeshRange{1,2,0},SubmeshRange{3,0,1}};
    template<InterleavedVertexRecord T, std::size_t N>
    MeshAsset Source(const std::array<T,N>& vertices, std::span<const VertexAttribute> attributes,
        MeshUpdateIntent intent=MeshUpdateIntent::Dynamic, MeshIndexFormat format=MeshIndexFormat::None,
        std::span<const std::byte> indices={})
    {
        auto source=MeshSourceData::FromVertices<T>(vertices,attributes);
        source.updateIntent=intent; source.submeshes=ranges; source.materialSlotCount=2;
        source.indexFormat=format; source.indices=indices; source.indexCount=indices.size()/(format==MeshIndexFormat::UInt16?2:4);
        auto result=MeshAsset::Create(source); Check(result,"CPU source validation"); return std::move(*result);
    }
    std::array<Padded,3> Records()
    {
        std::array<Padded,3> records{};
        for(std::size_t i=0;i<records.size();++i)
        {
            for(std::size_t j=0;j<3;++j) { records[i].position[j]=float(10*i+j+1); records[i].normal[j]=float(20*i+j+4); }
            records[i].uv[0]=float(i)+.25f; records[i].uv[1]=float(i)+.75f;
        }
        return records;
    }
    std::vector<std::byte> ReadBuffer(GLuint buffer, std::size_t bytes)
    {
        GLint previous=0; glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&previous);
        glBindBuffer(GL_ARRAY_BUFFER,buffer); std::vector<std::byte> data(bytes);
        glGetBufferSubData(GL_ARRAY_BUFFER,0,static_cast<GLsizeiptr>(bytes),data.data());
        glBindBuffer(GL_ARRAY_BUFFER,previous); return data;
    }
    // Transform feedback observes what the shader actually reads from the VAO.
    struct Capture
    {
        GLuint program=0;
        Capture()
        {
            const char* code=R"(#version 460 core
layout(location=0) in vec3 position; layout(location=1) in vec2 uv;
layout(location=2) in vec3 normal; layout(location=5) in ivec4 joints;
layout(location=8) in vec4 color;
out vec3 p; out vec3 n; out vec2 t; out vec4 c; out vec4 j;
void main(){p=position;n=normal;t=uv;c=color;j=vec4(joints);gl_Position=vec4(position,1);})";
            GLuint shader=glCreateShader(GL_VERTEX_SHADER); glShaderSource(shader,1,&code,nullptr); glCompileShader(shader);
            GLint okay=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&okay); Check(okay,"Capture shader compile");
            program=glCreateProgram(); glAttachShader(program,shader);
            const char* varyings[]{"p","n","t","c","j"}; glTransformFeedbackVaryings(program,5,varyings,GL_INTERLEAVED_ATTRIBS);
            glLinkProgram(program); glGetProgramiv(program,GL_LINK_STATUS,&okay); Check(okay,"Capture program link"); glDeleteShader(shader);
        }
        ~Capture(){glUseProgram(0);glDeleteProgram(program);}
        std::vector<float> Read(const GpuMesh& mesh, std::size_t submesh, std::size_t count)
        {
            GLuint buffer=0; glGenBuffers(1,&buffer); glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffer);
            glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,static_cast<GLsizeiptr>((std::max)(count,std::size_t(1))*16*sizeof(float)),nullptr,GL_STREAM_READ);
            glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffer); glUseProgram(program); glEnable(GL_RASTERIZER_DISCARD);
            glBeginTransformFeedback(GL_POINTS); Check(mesh.DrawSubmesh(submesh,MeshPrimitive::Points),"Submesh draw"); glEndTransformFeedback();
            glDisable(GL_RASTERIZER_DISCARD); std::vector<float> data(count*16);
            if(count) glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,static_cast<GLsizeiptr>(data.size()*sizeof(float)),data.data());
            glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0); glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,0); glDeleteBuffers(1,&buffer); return data;
        }
    };
    void Match(const std::vector<float>& data, const std::array<Padded,3>& records, std::span<const std::size_t> order)
    {
        Check(data.size()==order.size()*16,"Capture size");
        for(std::size_t i=0;i<order.size();++i)
        {
            const auto& r=records[order[i]];
            for(std::size_t c=0;c<3;++c) { Check(data[i*16+c]==r.position[c],"Shader position/stride"); Check(data[i*16+3+c]==r.normal[c],"Shader normal/stride"); }
            for(std::size_t c=0;c<2;++c) Check(data[i*16+6+c]==r.uv[c],"Shader UV/offset");
        }
    }
    void LayoutsAndUpdates(Capture& capture)
    {
        auto records=Records();
        const auto beforeVertex=Observer::vertexUploads, beforeIndex=Observer::indexUploads;
        auto source=Source(records,paddedLayout);
        auto gpu=GpuMesh::Create(source); Check(gpu,"Padded creation"); const GLuint buffer=Observer::lastVertex;
        Check(Observer::vertexUploads==beforeVertex+1 && Observer::indexUploads==beforeIndex,"Exactly one interleaved vertex batch");
        Check(Observer::vertexUsage==GL_DYNAMIC_DRAW && gpu->Layout().strideBytes==sizeof(Padded),"Dynamic usage and padded stride");
        Check(gpu->Submeshes().size()==3 && gpu->Submeshes()[0].materialSlot==1 && gpu->MaterialSlotCount()==2,"Submesh/material metadata");
        Match(capture.Read(*gpu,0,3),records,std::array<std::size_t,3>{0,1,2});
        Match(capture.Read(*gpu,1,2),records,std::array<std::size_t,2>{1,2});
        Check(capture.Read(*gpu,2,0).empty(),"Empty range");
        Reject(gpu->DrawSubmesh(3),E::InvalidRange); Reject(gpu->DrawSubmesh(0,static_cast<MeshPrimitive>(-1)),E::InvalidPrimitive);
        const auto original=ReadBuffer(buffer,sizeof(records));
        records[1].position[0]=99; records[1].position[1]=-123;
        auto update=VertexRecordUpdate{gpu->Layout(),1,1,std::as_bytes(std::span(records).subspan(1,1))};
        Check(gpu->UpdateVertices(update),"Nonzero first-vertex update");
        Check(Observer::updateOffset==sizeof(Padded) && Observer::updateBytes==sizeof(Padded),"Update byte units");
        Match(capture.Read(*gpu,0,3),records,std::array<std::size_t,3>{0,1,2});
        const auto changed=ReadBuffer(buffer,sizeof(records));
        Check(std::memcmp(changed.data(),original.data(),sizeof(Padded))==0
            && std::memcmp(changed.data()+2*sizeof(Padded),original.data()+2*sizeof(Padded),sizeof(Padded))==0,"Untouched records byte-identical");
        for(auto& record:records) record.position[2]+=100;
        Check(gpu->UpdateVertices({gpu->Layout(),0,3,std::as_bytes(std::span(records))}),"Full update");
        Match(capture.Read(*gpu,0,3),records,std::array<std::size_t,3>{0,1,2});
        const auto validBytes=ReadBuffer(buffer,sizeof(records));
        const auto writes=Observer::updates;
        Check(gpu->UpdateVertices({gpu->Layout(),3,0,{}}),"Empty end update");
        Reject(gpu->UpdateVertices({gpu->Layout(),4,0,{}}),E::InvalidRange);
        Reject(gpu->UpdateVertices({gpu->Layout(),1,SIZE_MAX,{}}),E::InvalidRange);
        Reject(gpu->UpdateVertices({gpu->Layout(),SIZE_MAX,1,{}}),E::InvalidRange);
        Reject(gpu->UpdateVertices({gpu->Layout(),1,1,std::as_bytes(std::span(records[1].position))}),E::PayloadMismatch);
        auto wrong=gpu->Layout(); ++wrong.strideBytes; Reject(gpu->UpdateVertices({wrong,0,0,{}}),E::IncompatibleLayout);
        auto attributes=paddedLayout; attributes[0].offsetBytes=0;
        wrong=gpu->Layout(); wrong.attributes=attributes; Reject(gpu->UpdateVertices({wrong,0,0,{}}),E::IncompatibleLayout);
        records[1].position[0]=std::numeric_limits<float>::infinity(); Reject(gpu->UpdateVertices(update),E::NonFinitePosition); records[1].position[0]=99;
        Check(Observer::updates==writes && ReadBuffer(buffer,sizeof(records))==validBytes,"Rejected/empty updates perform no writes");
        Observer::failUpdate=true; Reject(gpu->UpdateVertices(update),E::Driver);
        Check(ReadBuffer(buffer,sizeof(records))==validBytes,"Injected failed upload preserved storage");

        std::array<P,3> p{}; std::array<PN,3> pn{}; std::array<PNUV,3> pnuv{};
        for(std::size_t i=0;i<3;++i)
        {
            std::copy_n(records[i].position,3,p[i].position); std::copy_n(records[i].position,3,pn[i].position); std::copy_n(records[i].normal,3,pn[i].normal);
            std::copy_n(records[i].position,3,pnuv[i].position); std::copy_n(records[i].normal,3,pnuv[i].normal); std::copy_n(records[i].uv,2,pnuv[i].uv);
        }
        const std::array pa{Attribute(VertexSemantic::Position,offsetof(P,position),3)};
        const std::array pna{Attribute(VertexSemantic::Position,offsetof(PN,position),3),Attribute(VertexSemantic::Normal,offsetof(PN,normal),3)};
        const std::array pnuva{Attribute(VertexSemantic::Position,offsetof(PNUV,position),3),Attribute(VertexSemantic::Normal,offsetof(PNUV,normal),3),Attribute(VertexSemantic::TexCoord0,offsetof(PNUV,uv),2)};
        auto positions=GpuMesh::Create(Source(p,pa)); auto normals=GpuMesh::Create(Source(pn,pna)); auto uv=GpuMesh::Create(Source(pnuv,pnuva));
        Check(positions && normals && uv,"Minimal optional layouts");
        auto po=capture.Read(*positions,0,3), no=capture.Read(*normals,0,3);
        for(std::size_t i=0;i<3;++i) for(std::size_t c=0;c<3;++c)
        { Check(po[i*16+c]==p[i].position[c],"Position-only fields"); Check(no[i*16+c]==pn[i].position[c] && no[i*16+3+c]==pn[i].normal[c],"Position/normal fields"); }
        Match(capture.Read(*uv,0,3),records,std::array<std::size_t,3>{0,1,2});
        auto fixed=GpuMesh::Create(Source(records,paddedLayout,MeshUpdateIntent::Static)); Check(fixed,"Static mesh");
        Check(Observer::vertexUsage==GL_STATIC_DRAW,"Static upload policy");
        Reject(fixed->UpdateVertices({fixed->Layout(),0,0,{}}),E::Immutable);
        Reject(fixed->UpdateVertices({fixed->Layout(),0,3,std::as_bytes(std::span(records))}),E::Immutable);
        for(auto format:{MeshIndexFormat::UInt16,MeshIndexFormat::UInt32})
        {
            const std::array<std::uint16_t,3> shorts{2,0,1}; const std::array<std::uint32_t,3> ints{2,0,1};
            const auto bytes=format==MeshIndexFormat::UInt16?std::as_bytes(std::span<const std::uint16_t>(shorts)):std::as_bytes(std::span<const std::uint32_t>(ints));
            auto indexed=GpuMesh::Create(Source(records,paddedLayout,MeshUpdateIntent::Static,format,bytes)); Check(indexed,"Indexed creation");
            Check(indexed->IndexCount()==3 && indexed->IndexFormat()==format,"Index metadata");
            Match(capture.Read(*indexed,1,2),records,std::array<std::size_t,2>{0,1});
        }
    }

    void NumericInputs(Capture& capture)
    {
        struct Numeric {float p[3];std::int32_t joints[4];std::uint8_t color[4];};
        const std::array data{Numeric{{1,2,3},{-1,2,3,4},{0,64,128,255}},Numeric{{4,5,6},{8,9,10,11},{255,128,64,0}},Numeric{{7,8,9},{-7,-8,-9,-10},{51,102,153,204}}};
        const std::array attrs{Attribute(VertexSemantic::Position,offsetof(Numeric,p),3),
            Attribute(VertexSemantic::JointIndices,offsetof(Numeric,joints),4,VertexScalarFormat::Int32,VertexInterpretation::Integer),
            Attribute(VertexSemantic::Color0,offsetof(Numeric,color),4,VertexScalarFormat::UInt8,VertexInterpretation::Normalized)};
        auto gpu=GpuMesh::Create(Source(data,attrs)); Check(gpu,"Mixed integer/normalized creation"); auto output=capture.Read(*gpu,0,3);
        for(std::size_t i=0;i<3;++i) for(std::size_t c=0;c<4;++c)
        { Check(std::abs(output[i*16+8+c]-data[i].color[c]/255.f)<1e-6f,"Normalized color input"); Check(output[i*16+12+c]==float(data[i].joints[c]),"Integer shader input"); }
    }

    void FailuresAndPublication(Capture& capture)
    {
        auto records=Records(); const std::array<std::uint16_t,3> indices{0,1,2};
        auto source=Source(records,paddedLayout,MeshUpdateIntent::Dynamic,MeshIndexFormat::UInt16,std::as_bytes(std::span(indices)));
        AssetPublication publication; MeshRegistry registry(publication,{1});
        const auto beforeBuffers=Observer::buffers.size(),beforeArrays=Observer::arrays.size();
        auto noLeak=[&]{Check(registry.Size()==0 && Observer::buffers.size()==beforeBuffers && Observer::arrays.size()==beforeArrays,"Failed creation published/leaked");};
        for(int i=0;i<3;++i) {Observer::failName=i; auto p=publication.BeginPublication(); Reject(PublishMesh(registry,p,source),E::Allocation); noLeak();}
        for(int i=0;i<2;++i) {Observer::failUpload=i; auto p=publication.BeginPublication(); Reject(PublishMesh(registry,p,source),E::Driver); noLeak();}
        Observer::failAttribute=true; {auto p=publication.BeginPublication(); Reject(PublishMesh(registry,p,source),E::Driver);noLeak();}
        for(int i=0;i<2;++i) {denyAllocation=i;auto p=publication.BeginPublication();Reject(PublishMesh(registry,p,source),E::Allocation);noLeak();}
        Observer::tinyStride=true; Reject(GpuMesh::Create(source),E::DeviceLimit);Observer::tinyStride=false;noLeak();
        Observer::tinySlots=true; Reject(GpuMesh::Create(source),E::DeviceLimit);Observer::tinySlots=false;noLeak();
        MeshHandle handle;
        {auto p=publication.BeginPublication();auto h=PublishMesh(registry,p,source);Check(h,"Successful publication");handle=*h;
            auto full=PublishMesh(registry,p,source);Reject(full,E::Registry);Check(full.error().registry==RegistryError::SlotsExhausted,"Capacity error retained");}
        source=Source(records,paddedLayout); // Releasing the uploaded CPU payload cannot affect rendering.
        MeshView view;
        {auto f=publication.BeginFrame();view=registry.Acquire(f,handle).value();Match(capture.Read(*view,0,3),records,std::array<std::size_t,3>{0,1,2});}
        VertexRecordUpdate update{view->Layout(),1,1,std::as_bytes(std::span(records).subspan(1,1))};
        std::array<VertexAttribute,3> layout=paddedLayout;update.layout.attributes=layout;
        {auto p=publication.BeginPublication();auto busy=UpdateMesh(registry,p,handle,update);Reject(busy,E::Registry);Check(busy.error().registry==RegistryError::Busy,"Live lease update rejected");}
        view={}; records[1].position[0]=70;
        {auto p=publication.BeginPublication();Check(UpdateMesh(registry,p,handle,update),"Published dynamic update");}
        {auto f=publication.BeginFrame();view=registry.Acquire(f,handle).value();Check(view.Revision()==2,"Update advances revision");Match(capture.Read(*view,0,3),records,std::array<std::size_t,3>{0,1,2});}
        view={};
        {auto p=publication.BeginPublication();auto bad=update;bad.vertexCount=99;Reject(UpdateMesh(registry,p,handle,bad),E::InvalidRange);}
        {auto f=publication.BeginFrame();view=registry.Acquire(f,handle).value();Check(view.Revision()==2,"Failed update preserves revision");}
        struct Fence final:AssetRetirementFence {bool* done;explicit Fence(bool& d):done(&d){}bool IsComplete()const noexcept override{return *done;}};
        bool completed=false;
        {auto f=publication.BeginFrame();Check(registry.ProtectGpuUse(f,view,std::make_unique<Fence>(completed)),"Fence retention");}view={};
        {auto p=publication.BeginPublication();auto busy=UpdateMesh(registry,p,handle,update);Reject(busy,E::Registry);Check(busy.error().registry==RegistryError::Busy,"In-flight fence update rejected");}
        completed=true;
        {auto p=publication.BeginPublication();Check(UpdateMesh(registry,p,handle,update),"Completed fence permits update");}
        {auto f=publication.BeginFrame();view=registry.Acquire(f,handle).value();}
        {auto p=publication.BeginPublication();Check(registry.Destroy(p,handle),"Destroy published handle");Check(registry.Collect(p)==0,"Lease retains retired resource");}
        std::thread release([lease=std::move(view)]()mutable{lease={};});release.join();
        {auto p=publication.BeginPublication();Check(registry.Collect(p)==1,"Owner retires after worker lease release");Check(registry.Close(p),"Registry closes");Reject(PublishMesh(registry,p,source),E::Registry);}
        noLeak();
        {auto f=publication.BeginFrame();Check(!registry.Acquire(f,handle),"Stale handle rejected");}
        MeshRegistry limited(publication,{1,UINT64_MAX,1});
        {auto p=publication.BeginPublication();auto h=PublishMesh(limited,p,source).value();auto exhausted=UpdateMesh(limited,p,h,update);Reject(exhausted,E::Registry);Check(exhausted.error().registry==RegistryError::RevisionExhausted,"Revision exhaustion");Check(limited.Close(p),"Limited registry closes");}
    }

    void EmptyAndMoves()
    {
        static_assert(!std::is_copy_constructible_v<GpuMesh> && std::is_nothrow_move_constructible_v<GpuMesh>
            && std::is_nothrow_move_assignable_v<GpuMesh> && std::is_nothrow_destructible_v<GpuMesh>);
        const std::array attributes{Attribute(VertexSemantic::Position,offsetof(P,position),3)};
        MeshSourceData source;source.layout=VertexLayout::For<P>(attributes);source.updateIntent=MeshUpdateIntent::Dynamic;
        auto empty=MeshAsset::Create(source).value();auto gpu=GpuMesh::Create(empty);Check(gpu && gpu->VertexCount()==0,"Empty validated mesh");
        Check(gpu->UpdateVertices({gpu->Layout(),0,0,{}}),"Empty mesh update");
        Reject(gpu->DrawSubmesh(0),E::InvalidRange);
        auto a=GpuMesh::Create(Source(Records(),paddedLayout)).value();auto b=GpuMesh::Create(Source(Records(),paddedLayout)).value();
        b=std::move(a);Check(!a && b.VertexCount()==3,"Occupied move resets source");
        b=std::move(b);Check(b,"Self move");std::vector<GpuMesh> moved;moved.push_back(std::move(b));moved.reserve(30);
        Check(!b && moved[0].VertexCount()==3,"Relocation preserves ownership");Reject(b.UpdateVertices({}),E::InvalidMesh);Reject(b.DrawSubmesh(0),E::InvalidMesh);
        auto other=std::move(empty);Reject(GpuMesh::Create(empty),E::InvalidAsset);
    }
}

int main(int argc,char** argv)
{
    using namespace GEngine;
    Check(SDL_Init(SDL_INIT_VIDEO)==0,"SDL init");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,SDL_GL_CONTEXT_DEBUG_FLAG);
    for(int cycle=0;cycle<2;++cycle)
    {
        auto* window=SDL_CreateWindow("GPU mesh validation",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);Check(window,"Hidden window");
        auto context=SDL_GL_CreateContext(window);Check(context,"Owned context");Check(SDL_GL_MakeCurrent(window,context)==0,"Make current");
        Check(gladLoadGLLoader(SDL_GL_GetProcAddress),"GL loader");
        glEnable(GL_DEBUG_OUTPUT);glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);glDebugMessageCallback(DebugMessage,nullptr);
        std::println("[GL] version={} renderer={}",reinterpret_cast<const char*>(glGetString(GL_VERSION)),reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        if(argc>1)
        {
            InstallExpectedTermination();
            auto gpu=GpuMesh::Create(Source(Records(),paddedLayout)).value();const std::string_view mode=argv[1];
            if(mode=="--reject-worker") {std::thread worker([owner=std::move(gpu)]()mutable{InstallExpectedTermination();owner={};});worker.join();}
            if(mode=="--reject-worker-create") {auto cpu=Source(Records(),paddedLayout);std::thread worker([&]{InstallExpectedTermination();(void)GpuMesh::Create(cpu);});worker.join();}
            if(mode=="--reject-context") {auto second=SDL_GL_CreateContext(window);Check(second,"Second context");gpu={};}
            return 89;
        }
        {
            Observer observer;
            GLuint sentinelArray=0,sentinelBuffer=0;glGenVertexArrays(1,&sentinelArray);glGenBuffers(1,&sentinelBuffer);
            glBindVertexArray(sentinelArray);glBindBuffer(GL_ARRAY_BUFFER,sentinelBuffer);
            {
                Capture capture;LayoutsAndUpdates(capture);NumericInputs(capture);FailuresAndPublication(capture);EmptyAndMoves();
            }
            GLint vao=0,buffer=0;glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
            Check(vao==static_cast<GLint>(sentinelArray)&&buffer==static_cast<GLint>(sentinelBuffer),"Caller bindings preserved");
            glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);glDeleteVertexArrays(1,&sentinelArray);glDeleteBuffers(1,&sentinelBuffer);
            Check(Observer::valid&&Observer::buffers.empty()&&Observer::arrays.empty(),"Exactly-once owner-thread retirement");
            Check(debugErrors==0&&glGetError()==GL_NO_ERROR,"No unexpected GL errors");
        }
        SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);
    }
    SDL_Quit();std::println("[PASS] gpu-mesh cycles=2 checks={}",checks);
}
