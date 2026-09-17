#include "Material/MaterialBinding.h"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <print>
#include <thread>

#ifdef MATERIAL_INSTANCE_GL_PROBE
#include "../GEngine/src/Assets/ShaderBackend.h"
#include "../GEngine/src/Assets/TextureBackend.h"
#include "Core/GLContextThread.h"
#else
#if defined(GL_VERSION_1_0) || defined(GLAD_GL_H_) || defined(SDL_MAJOR_VERSION)
#error Backend declarations leaked into the material consumer
#endif
#endif

namespace
{
    using namespace GEngine;
    using namespace GEngine::Asset;
    int checks{};
    template<class T> void Check(const T& condition, const char* message)
    { ++checks; if (!static_cast<bool>(condition)) { std::println(stderr, "[FAIL] {}", message); std::exit(1); } }
    template<class T, class E> void Reject(const T& result, E code)
    { Check(!result && result.error().code == code, "Expected typed failure"); }
    std::array<float,16> Matrix()
    { std::array<float,16> value{}; for (int i=0;i<16;++i) value[i]=static_cast<float>(i+1); return value; }
    std::vector<MaterialParameterDecl> Parameters()
    {
        return {{"h_matrix", MaterialParameterType::Matrix4, Matrix()},
            {"g_vec4", MaterialParameterType::Float4, std::array<float,4>{1,2,3,4}},
            {"f_vec3", MaterialParameterType::Float3, std::array<float,3>{5,6,7}},
            {"e_vec2", MaterialParameterType::Float2, std::array<float,2>{8,9}},
            {"d_float", MaterialParameterType::Float, -0.f},
            {"c_uint", MaterialParameterType::UnsignedInteger, std::uint32_t(0xfedcba98)},
            {"b_int", MaterialParameterType::Integer, std::int32_t(-7)},
            {"a_bool", MaterialParameterType::Boolean, true}};
    }
    MaterialTemplateView PublishTemplate(AssetPublication& publication, PipelineRegistry& pipelines,
        MaterialTemplateRegistry& templates, ShaderProgramHandle program,
        std::span<const MaterialTextureSlotDecl> textures, std::uint64_t revision=1)
    {
        PipelineDesc desc; desc.program=program; desc.programRevision=revision;
        PipelineHandle pipeline;
        { auto p=publication.BeginPublication(); pipeline=pipelines.Create(p, PipelineState::Create(desc).value()).value(); }
        PipelineView view;
        { auto f=publication.BeginFrame(); view=pipelines.Acquire(f,pipeline).value(); }
        auto parameters=Parameters();
        auto material=MaterialTemplate::Create({view,parameters,textures}).value();
        MaterialTemplateHandle handle;
        { auto p=publication.BeginPublication(); handle=templates.Create(p,std::move(material)).value(); }
        auto f=publication.BeginFrame(); return templates.Acquire(f,handle).value();
    }
    MaterialInstanceView PublishInstance(AssetPublication& publication, MaterialInstanceRegistry& registry, const MaterialInstance& value)
    {
        MaterialInstanceHandle handle;
        { auto p=publication.BeginPublication(); handle=registry.Create(p,value).value(); }
        auto f=publication.BeginFrame(); return registry.Acquire(f,handle).value();
    }
    void Authoring()
    {
        Reject(MaterialInstance::Create({}),MaterialInstanceCode::InvalidTemplate);
        AssetPublication publication;
        PipelineRegistry pipelines(publication);
        MaterialTemplateRegistry templates(publication);
        MaterialInstanceRegistry instances(publication);
        {
            MaterialTextureValue defaults{{1,2,3},{4,5,6}}, replacement{{7,8,9},{10,11,12}};
            const MaterialTextureSlotDecl textures[]{{"z_required",true,{}},{"a_default",true,defaults},{"m_optional",false,{}}};
            auto declaration=PublishTemplate(publication,pipelines,templates,{3,4,5},textures);
            auto first=MaterialInstance::Create(declaration).value();
            auto second=MaterialInstance::Create(declaration).value();
            auto clone=first;
            Check(first.Template()==second.Template() && first.TemplateRevision()==1 && first.Revision()==1,"Shared immutable template");
            Check(first.SetParameter("d_float",0.f) && first.Revision()==1,"Equivalent default and signed zero preserve revision");
            Check(first.SetParameter("d_float",.5f) && first.Revision()==2,"Effective parameter edit increments once");
            Check(first.SetParameter("d_float",.5f) && first.Revision()==2,"Repeated edit is stable");
            Check(std::get<float>(second.Values()[3])==0 && std::get<float>(clone.Values()[3])==0,"Overrides are independent");
            Reject(first.SetParameter("d_float",std::int32_t(1)),MaterialInstanceCode::InvalidParameter);
            Reject(first.SetParameter("d_float",std::numeric_limits<float>::quiet_NaN()),MaterialInstanceCode::InvalidParameter);
            Reject(first.SetParameter("e_vec2",std::array<float,2>{1,std::numeric_limits<float>::infinity()}),MaterialInstanceCode::InvalidParameter);
            Reject(first.SetParameter("missing",1.f),MaterialInstanceCode::UnknownParameter);
            Reject(first.ResetParameter("missing"),MaterialInstanceCode::UnknownParameter);
            Check(first.Revision()==2 && std::get<float>(first.Values()[3])==.5f,"Failed edits are transactional");
            Check(first.ResetParameter("d_float") && first.Revision()==3,"Reset to default increments");
            Check(first.ResetParameter("d_float") && first.Revision()==3,"Repeated reset stable");
            Check(first.SetTexture("a_default",defaults) && first.Revision()==3,"Default texture assignment is stable");
            Check(first.SetTexture("a_default",replacement) && first.Revision()==4,"Replace texture/sampler pair");
            Check(first.SetTexture("a_default",replacement) && first.Revision()==4,"Repeated pair is stable");
            auto samplerOnly=replacement; samplerOnly.sampler=defaults.sampler;
            Check(first.SetTexture("a_default",samplerOnly) && first.Revision()==5,"Sampler-only edit is effective");
            Reject(first.SetTexture("a_default",{replacement.texture,{}}),MaterialInstanceCode::InvalidTexture);
            Reject(first.SetTexture("unknown",replacement),MaterialInstanceCode::UnknownTexture);
            Reject(first.ResetTexture("unknown"),MaterialInstanceCode::UnknownTexture);
            Check(first.Revision()==5 && second.Textures()[0]==defaults,"Failed binding edit and independent defaults");
            Check(first.ResetTexture("a_default") && first.Revision()==6,"Restore template pair");
            Check(first.ResetTexture("m_optional") && first.Revision()==6,"Unbound reset is stable");
            Check(first.SetTexture("m_optional",replacement) && first.Revision()==7,"Optional override");
            Check(first.ResetTexture("m_optional") && first.Revision()==8 && !first.Textures()[1],"Optional reset removes override");
            auto shared=PublishInstance(publication,instances,first);
            auto sharedAlias=shared;
            auto unique=PublishInstance(publication,instances,first);
            Check(shared.Identity()==sharedAlias.Identity() && shared.Get()==sharedAlias.Get() && unique.Identity()!=shared.Identity(),"Shared versus unique handles");
            Check(first.SetParameter("d_float",.25f),"Author replacement");
            {auto p=publication.BeginPublication();Check(instances.Replace(p,shared.Identity(),first),"Publish shared replacement");}
            {auto f=publication.BeginFrame();auto current=instances.Acquire(f,shared.Identity()).value();
                Check(current.Revision()==2 && current->Revision()==9 && shared->Revision()==8 && unique->Revision()==8,"Publication preserves pinned shared/unique snapshots");}
            auto moved=std::move(clone);
            Reject(clone.SetParameter("d_float",1.f),MaterialInstanceCode::InvalidTemplate);
            Check(moved.Template()==declaration.Identity(),"Move retains template");
            bool edited=false;
            std::thread worker([copy=second,&edited]() mutable { edited=bool(copy.SetParameter("d_float",1.f)) && copy.Revision()==2; });
            worker.join();Check(edited && second.Revision()==1,"Independent CPU authoring on worker");
        }
        auto p=publication.BeginPublication();
        Check(instances.Close(p) && templates.Close(p) && pipelines.Close(p),"Authoring leases close in dependency order");
    }

#ifdef MATERIAL_INSTANCE_GL_PROBE
    Shader Program()
    {
        const ShaderSource sources[]{{VERTEX,"#version 460 core\nvoid main(){gl_Position=vec4(0,0,0,1);}","material-vs"},
            {FRAGMENT,"#version 460 core\nout vec4 color;void main(){color=vec4(1);}","material-fs"}};
        auto result=Shader::Create({sources});Check(result,"Create program");return std::move(*result);
    }
    TextureResource Image()
    {
        TextureDesc desc;desc.width=desc.height=1;desc.colorSpace=TextureColorSpace::Linear;desc.mips=TextureMipIntent::None;
        const std::array<std::byte,4> pixels{std::byte{20},std::byte{40},std::byte{60},std::byte{255}};
        auto image=TextureResource::Create(desc,{pixels});Check(image,"Create image");return std::move(*image);
    }
    void Bindings(SDL_Window* window, SDL_GLContext context)
    {
        AssetPublication publication;
        ShaderProgramRegistry programs(publication);
        PipelineRegistry pipelines(publication);
        MaterialTemplateRegistry templates(publication);
        MaterialInstanceRegistry instances(publication);
        TextureRegistry textures(publication);
        SamplerRegistry samplers(publication);
        const MaterialBindingResources resources{programs,textures,samplers};
        ShaderProgramHandle program;
        TextureHandle texture, fallbackTexture;
        SamplerHandle sampler;
        { auto p=publication.BeginPublication();
            program=programs.Create(p,Program()).value();texture=textures.Create(p,Image()).value();fallbackTexture=textures.Create(p,Image()).value();
            sampler=samplers.Create(p,GpuSampler::Create({}).value()).value(); }
        std::optional<PreparedMaterialBinding> retained;
        GLuint oldProgram{},oldTexture{};
        {
            const MaterialTextureSlotDecl slots[]{{"z_optional",false,{}},{"a_image",true,MaterialTextureValue{texture,sampler}}};
            auto declaration=PublishTemplate(publication,pipelines,templates,program,slots);
            auto author=MaterialInstance::Create(declaration).value();
            auto view=PublishInstance(publication,instances,author);
            Check(SDL_GL_MakeCurrent(window,nullptr)==0,"Detach context for CPU-only preparation");
            {
                auto frame=publication.BeginFrame();
                Reject(PreparedMaterialBinding::Prepare({},frame,resources),MaterialBindingCode::InvalidInstance);
                auto packet=PreparedMaterialBinding::Prepare(view,frame,resources);Check(packet,"Prepare defaults without any current GL context");
                Check(packet->Fallbacks().empty() && packet->Textures().size()==2 && !packet->Textures()[1].texture,"Optional unbound slot is explicit");
                Check(packet->Instance()==view.Identity() && packet->Revision()==1 && packet->PublicationRevision()==1,"Packet identities");
                Check(packet->Program().Identity()==program && packet->Program().Revision()==1 && packet->Pipeline().Alpha()==AlphaMode::Opaque,"Resolved program and immutable pipeline");
                const auto words=packet->PackedWords();
                Check(words.size()==44 && packet->Parameters().size()==8,"Packed lane sizes");
                Check(words[0]==1 && words[4]==std::bit_cast<std::uint32_t>(std::int32_t(-7)) && words[8]==0xfedcba98 && words[12]==0,"Boolean/integer/unsigned/float representation");
                Check(words[16]==std::bit_cast<std::uint32_t>(8.f) && words[17]==std::bit_cast<std::uint32_t>(9.f),"Float2 representation");
                Check(words[20]==std::bit_cast<std::uint32_t>(5.f) && words[22]==std::bit_cast<std::uint32_t>(7.f),"Float3 representation");
                Check(words[24]==std::bit_cast<std::uint32_t>(1.f) && words[27]==std::bit_cast<std::uint32_t>(4.f),"Float4 representation");
                for(std::size_t i=0;i<16;++i)Check(words[28+i]==std::bit_cast<std::uint32_t>(Matrix()[i]),"Column-major matrix representation");
                for(auto offset:{1,2,3,5,6,7,9,10,11,13,14,15,18,19,23})Check(words[offset]==0,"Packed padding is deterministic zero");
                for(std::size_t i=0;i<8;++i)Check(packet->Parameters()[i].wordOffset==4*i,"GPU offsets independent of CPU offsets");
                retained=std::move(*packet);
            }
            Check(SDL_GL_MakeCurrent(window,context)==0,"Restore context");
            oldProgram=ShaderBackendAccess::Program(*retained->Program());
            oldTexture=AssetDetail::TextureBackend::Name(TextureView(retained->Textures()[0].texture)).value();
            GLuint buffer{};glGenBuffers(1,&buffer);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
            const auto words=retained->PackedWords();
            glBufferData(GL_SHADER_STORAGE_BUFFER,static_cast<GLsizeiptr>(words.size_bytes()),words.data(),GL_STATIC_DRAW);
            std::vector<std::uint32_t> readback(words.size());glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,static_cast<GLsizeiptr>(words.size_bytes()),readback.data());
            Check(std::equal(words.begin(),words.end(),readback.begin()),"Packed bytes survive GPU upload/readback");glDeleteBuffers(1,&buffer);
            {auto p=publication.BeginPublication();Check(textures.Replace(p,texture,Image()),"Texture replacement");
                SamplerDesc desc;desc.wrapU=SamplerWrap::ClampToEdge;Check(samplers.Replace(p,sampler,GpuSampler::Create(desc).value()),"Sampler replacement");}
            {auto f=publication.BeginFrame();auto updated=PreparedMaterialBinding::Prepare(view,f,resources).value();
                Check(updated.Revision()==1 && updated.Textures()[0].texture.Revision()==2 && updated.Textures()[0].sampler.Revision()==2,"Reprepare resolves new versions without authored revision change");
                Check(retained->Textures()[0].texture.Revision()==1 && retained->Textures()[0].sampler.Revision()==1,"Earlier packet pins earlier versions");}
            auto missing=author;Check(missing.SetTexture("a_image",{{99,99,texture.registry},sampler}),"Author stale texture");
            auto missingView=PublishInstance(publication,instances,missing);
            {auto f=publication.BeginFrame();
                Reject(PreparedMaterialBinding::Prepare(missingView,f,resources),MaterialBindingCode::InvalidTexture);
                auto substituted=PreparedMaterialBinding::Prepare(missingView,f,resources,MaterialBindingFallback{{fallbackTexture,sampler}});
                Check(substituted && substituted->Fallbacks().size()==2 && substituted->Fallbacks()[0].reason.code==MaterialBindingCode::InvalidTexture
                    && substituted->Fallbacks()[1].reason.code==MaterialBindingCode::MissingTexture && substituted->Textures()[0].texture.Identity()==fallbackTexture,"Explicit fallback records every cause");
                auto bad=PreparedMaterialBinding::Prepare(missingView,f,resources,MaterialBindingFallback{{{},sampler}});
                Reject(bad,MaterialBindingCode::InvalidFallback);Check(bad.error().cause==MaterialBindingCode::InvalidTexture,"Fallback retains resolution cause");}
            Check(missing.SetTexture("a_image",{texture,{99,99,sampler.registry}}),"Author stale sampler");
            auto badSampler=PublishInstance(publication,instances,missing);
            {auto f=publication.BeginFrame();Reject(PreparedMaterialBinding::Prepare(badSampler,f,resources),MaterialBindingCode::InvalidSampler);}
            const MaterialTextureSlotDecl required[]{{"required",true,{}}};
            auto requiredDeclaration=PublishTemplate(publication,pipelines,templates,program,required);
            auto requiredView=PublishInstance(publication,instances,MaterialInstance::Create(requiredDeclaration).value());
            {auto f=publication.BeginFrame();Reject(PreparedMaterialBinding::Prepare(requiredView,f,resources),MaterialBindingCode::MissingTexture);
                Check(PreparedMaterialBinding::Prepare(requiredView,f,resources,MaterialBindingFallback{{fallbackTexture,sampler}}),"Explicit required-binding fallback");}
            auto foreignDeclaration=PublishTemplate(publication,pipelines,templates,{99,99,99},{});
            auto foreignView=PublishInstance(publication,instances,MaterialInstance::Create(foreignDeclaration).value());
            {auto f=publication.BeginFrame();Reject(PreparedMaterialBinding::Prepare(foreignView,f,resources),MaterialBindingCode::InvalidProgram);}
            {auto p=publication.BeginPublication();Check(programs.Replace(p,program,Program()),"Replace program");}
            {auto f=publication.BeginFrame();Reject(PreparedMaterialBinding::Prepare(view,f,resources,MaterialBindingFallback{{fallbackTexture,sampler}}),MaterialBindingCode::ProgramRevisionMismatch);}
            Check(glIsProgram(oldProgram) && glIsTexture(oldTexture),"Prepared packet retains old GPU owners");
            auto refreshed=PublishTemplate(publication,pipelines,templates,program,slots,2);
            auto refreshedView=PublishInstance(publication,instances,MaterialInstance::Create(refreshed).value());
            {auto f=publication.BeginFrame();Check(PreparedMaterialBinding::Prepare(refreshedView,f,resources),"Explicit template rebase resolves replacement program");}
            {auto p=publication.BeginPublication();Check(textures.Destroy(p,texture) && samplers.Destroy(p,sampler) && programs.Destroy(p,program),"Destroy identities while packet is live");}
            {auto f=publication.BeginFrame();Reject(PreparedMaterialBinding::Prepare(view,f,resources),MaterialBindingCode::InvalidProgram);}
        }
        {auto p=publication.BeginPublication();Check(!instances.Close(p) && !textures.Close(p) && !samplers.Close(p) && !programs.Close(p),"Escaped packet blocks dependency teardown");}
        std::thread worker([packet=std::move(retained)]() mutable {packet.reset();});worker.join();retained.reset();
        Check(glIsProgram(oldProgram) && glIsTexture(oldTexture),"Worker release defers GPU retirement");
        {auto p=publication.BeginPublication();Check(instances.Close(p) && templates.Close(p) && pipelines.Close(p)
            && textures.Close(p) && samplers.Close(p) && programs.Close(p),"Dependency-order retirement");}
        Check(!glIsProgram(oldProgram) && !glIsTexture(oldTexture),"GPU owners retire before context teardown");
        Check(glGetError()==GL_NO_ERROR,"No GL errors");
    }
#endif
}
int main()
{
    Authoring();
#ifdef MATERIAL_INSTANCE_GL_PROBE
    Check(SDL_Init(SDL_INIT_VIDEO)==0,"SDL init");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    for(int cycle=0;cycle<2;++cycle)
    {
        auto* window=SDL_CreateWindow("Material instance validation",0,0,32,32,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);Check(window,"Hidden window");
        auto context=SDL_GL_CreateContext(window);Check(context,"Context");Check(gladLoadGLLoader(SDL_GL_GetProcAddress),"GL loader");
        std::println("[GL] version={} renderer={}",reinterpret_cast<const char*>(glGetString(GL_VERSION)),reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        Bindings(window,context);SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);
    }
    SDL_Quit();
#endif
    std::println("[PASS] material-instance checks={}",checks);
}
