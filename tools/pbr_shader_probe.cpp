#ifndef PBR_APPLICATION_PROBE
// Driver fixture: private native access is intentional; no consumer API is added.
#include "Core/GEngine.h"
#include "Core/Window.h"
#include "Core/RuntimeAssets.h"
#include "../GEngine/src/Assets/ShaderBackend.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <print>
#include <string>

namespace
{
    using namespace GEngine;
    using namespace ::GEngine::Asset;
    using Pixel = std::array<float,4>;
    int checks{};
    void Check(bool value, const char* message)
    {
        ++checks;
        if (!value) { std::println(stderr,"[FAIL] {}",message); std::exit(1); }
    }
    std::string Read(const std::string& path)
    {
        std::ifstream input(path,std::ios::binary);
        Check(bool(input),"shader file opened");
        std::string result((std::istreambuf_iterator<char>(input)),{});
        Check(!input.bad(),"shader file read");
        return result;
    }
    constexpr const char* vertex = R"(#version 450 core
out VS_OUT { vec3 FragPos; vec3 Normal; vec2 TexCoords; } vs_out;
uniform vec2 fixtureOffset, fixtureExtent;
uniform bool fixtureRotate;
void main() {
    vec2 p=vec2((gl_VertexID << 1) & 2,gl_VertexID & 2)*2.0-1.0;
    vec2 uv=(p+1.0)*0.5;
    if(fixtureRotate) uv=vec2(-uv.y,uv.x);
    vs_out.FragPos=vec3(p,0); vs_out.Normal=vec3(0,0,1);
    vs_out.TexCoords=fixtureOffset+uv*fixtureExtent;
    gl_Position=vec4(p,0,1);
})";
    Shader Program(const std::string& fragment)
    {
        const ShaderSource stages[]{{VERTEX,vertex,"pbr fixture vertex"},{FRAGMENT,fragment,"production fragment"}};
        auto result=Shader::Create({stages});
        if(!result) std::println(stderr,"shader error: {}",result.error().log);
        Check(bool(result),"production fragment compiled and linked");
        return std::move(*result);
    }
    GLuint Name(const Shader& shader) { return ShaderBackendAccess::Program(shader); }
    void Int(const Shader& shader,const char* name,int value) { glUniform1i(glGetUniformLocation(Name(shader),name),value); }
    void Vec2(const Shader& shader,const char* name,float x,float y) { glUniform2f(glGetUniformLocation(Name(shader),name),x,y); }
    void Vec3(const Shader& shader,const char* name,float x,float y,float z) { glUniform3f(glGetUniformLocation(Name(shader),name),x,y,z); }
    constexpr std::array<Pixel,5> defaults{{{.45f,.3f,.2f,1},{.5f,.5f,1,1},{.2f,0,0,1},{.6f,0,0,1},{.8f,0,0,1}}};
    struct Fixture
    {
        GLuint vao{}, fbo{}, color{}, entity{}, picking{};
        std::array<GLuint,5> maps{};
        std::array<GLuint,2> shadows{};
        Fixture()
        {
            glCreateVertexArrays(1,&vao); glBindVertexArray(vao);
            glCreateFramebuffers(1,&fbo);
            glCreateTextures(GL_TEXTURE_2D,1,&color);glTextureStorage2D(color,1,GL_RGBA32F,64,64);
            glCreateTextures(GL_TEXTURE_2D,1,&entity);glTextureStorage2D(entity,1,GL_R32I,64,64);
            glCreateTextures(GL_TEXTURE_2D,1,&picking);glTextureStorage2D(picking,1,GL_R32I,64,64);
            glNamedFramebufferTexture(fbo,GL_COLOR_ATTACHMENT0,color,0);
            glNamedFramebufferTexture(fbo,GL_COLOR_ATTACHMENT1,entity,0);
            glNamedFramebufferDrawBuffer(fbo,GL_COLOR_ATTACHMENT0);
            Check(glCheckNamedFramebufferStatus(fbo,GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"mixed float/integer fixture framebuffer complete");
            glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,64,64);
            glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
            glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_DITHER);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            glCreateTextures(GL_TEXTURE_2D,5,maps.data());
            for(int i=0;i<5;++i) {
                glTextureStorage2D(maps[i],1,GL_RGBA32F,2,2);
                glTextureParameteri(maps[i],GL_TEXTURE_MIN_FILTER,GL_NEAREST);
                glTextureParameteri(maps[i],GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                glTextureParameteri(maps[i],GL_TEXTURE_WRAP_S,GL_REPEAT);
                glTextureParameteri(maps[i],GL_TEXTURE_WRAP_T,GL_REPEAT);
                glBindTextureUnit(i,maps[i]);glBindSampler(i,0);
            }
            glCreateTextures(GL_TEXTURE_2D_ARRAY,1,&shadows[0]);
            glTextureStorage3D(shadows[0],1,GL_DEPTH_COMPONENT32F,1,1,5);
            glCreateTextures(GL_TEXTURE_CUBE_MAP,1,&shadows[1]);
            glTextureStorage2D(shadows[1],1,GL_DEPTH_COMPONENT32F,1,1);
            const float depth=1;
            for(int i=0;i<2;++i) {
                glTextureParameteri(shadows[i],GL_TEXTURE_MIN_FILTER,GL_NEAREST);
                glTextureParameteri(shadows[i],GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                glClearTexImage(shadows[i],0,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);
                glBindTextureUnit(5+i,shadows[i]);glBindSampler(5+i,0);
            }
            Reset();
        }
        ~Fixture()
        {
            glUseProgram(0);glBindVertexArray(0);glBindFramebuffer(GL_FRAMEBUFFER,0);
            glDeleteTextures(2,shadows.data());glDeleteTextures(5,maps.data());glDeleteTextures(1,&color);glDeleteTextures(1,&entity);glDeleteTextures(1,&picking);
            glDeleteFramebuffers(1,&fbo);glDeleteVertexArrays(1,&vao);
        }
        void Map(int index,const std::array<Pixel,4>& values) { glTextureSubImage2D(maps[index],0,0,0,2,2,GL_RGBA,GL_FLOAT,values.data()); }
        void Constant(int index,Pixel value) { Map(index,{value,value,value,value}); }
        void Reset() { for(int i=0;i<5;++i) Constant(i,defaults[i]); }
        void Bind(const Shader& shader,float tx=1,float ty=1,bool rotate=false)
        {
            shader.Bind();
            const char* names[]{"albedoMap","normalMap","metallicMap","roughnessMap","aoMap"};
            for(int i=0;i<5;++i) Int(shader,names[i],i);
            // Distinct sampler types must not alias even with shadow branches disabled.
            Int(shader,"shadowMap",5);Int(shader,"pointShadowDepthMap",6);
            Int(shader,"frameLights",1);Int(shader,"frameDirectional",1);Int(shader,"framePoint",0);
            Int(shader,"frameDirectionalShadows",0);Int(shader,"framePointShadows",0);
            Vec2(shader,"u_tiling",tx,ty);Vec2(shader,"fixtureOffset",.125f,.125f);Vec2(shader,"fixtureExtent",.25f,.25f);
            Int(shader,"fixtureRotate",rotate?1:0);
            Vec3(shader,"viewPos",0,0,3);Vec3(shader,"lightPos",2,3,4);
            Vec3(shader,"lightDir",.3f,.4f,std::sqrt(.75f));Vec3(shader,"directionallightColor",.9f,.8f,.7f);
            Vec3(shader,"pointlightColor",0,0,0);Vec3(shader,"metalness",.04f,.04f,.04f);
        }
        Pixel Draw()
        {
            glDrawArrays(GL_TRIANGLES,0,3);
            Pixel pixel{};glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(32,32,1,1,GL_RGBA,GL_FLOAT,pixel.data());
            for(float value:pixel) Check(std::isfinite(value),"finite shader result");
            Check(glGetError()==GL_NO_ERROR,"draw/readback has no GL error");
            return pixel;
        }
    };
    float Difference(const Pixel& a,const Pixel& b)
    {
        float largest=0;for(int i=0;i<3;++i) largest=std::max(largest,std::abs(a[i]-b[i]));return largest;
    }
    void Normals(Fixture& fixture,const std::string& source)
    {
        std::string observed=source;
        observed.insert(observed.find('\n')+1,"#define main originalLightingMain\n");
        observed+="\n#undef main\nvoid main(){ FragColor=vec4(getNormalFromMap(fs_in.TexCoords*u_tiling),1); }\n";
        auto shader=Program(observed);
        const Pixel encoded[]{{.5f,.5f,1,1},{.8f,.5f,.9f,1},{.2f,.5f,.9f,1},{.5f,.8f,.9f,1},{.5f,.2f,.9f,1}};
        const Pixel expected[]{{0,0,1,1},{.6f,0,.8f,1},{-.6f,0,.8f,1},{0,-.6f,.8f,1},{0,.6f,.8f,1}};
        for(bool rotate:{false,true}) for(int i=0;i<5;++i) {
            fixture.Constant(1,encoded[i]);fixture.Bind(shader,3,2,rotate);
            auto reference=expected[i];if(rotate) {reference[0]=expected[i][1];reference[1]=-expected[i][0];}
            const auto actual=fixture.Draw();
            std::println("normal rotate={} index={} actual=({},{},{})",rotate,i,actual[0],actual[1],actual[2]);
            Check(Difference(actual,reference)<2e-4f,"flat/signed tangent normal direction");
        }
        fixture.Reset();
        std::println("[PASS] flat-normal and signed/rotated normal-direction fixtures");
    }
    void BoundedFrames(Fixture& fixture,const std::string& source)
    {
        const auto replace=[](std::string& text,const std::string& from,const std::string& to) {
            const auto position=text.find(from);Check(position!=std::string::npos,"frame diagnostic source marker");
            text.replace(position,from.size(),to);
        };
        std::string observed=source;
        observed.insert(observed.find('\n')+1,"#define main originalLightingMain\n");
        observed+="\n#undef main\nvoid main(){ FragColor=vec4(getNormalFromMap(fs_in.TexCoords*u_tiling),1); }\n";
        auto base=observed;
        replace(base,"dFdx(uvs)","dFdx(fs_in.TexCoords)");
        replace(base,"dFdy(uvs)","dFdy(fs_in.TexCoords)");
        auto cotangent=observed;
        replace(cotangent,"vec3 T = normalize(Q1*st2.t - Q2*st1.t);",
            "vec3 T = normalize(cross(Q2,N)*st1.x + cross(N,Q1)*st2.x);");
        replace(cotangent,"vec3 B = -normalize(cross(N, T));",
            "vec3 B = -normalize(cross(Q2,N)*st1.y + cross(N,Q1)*st2.y);");
        auto tiledProgram=Program(observed),baseProgram=Program(base),cotangentProgram=Program(cotangent);
        const std::array<std::array<float,2>,4> tiles{{{1,1},{2,2},{2,.2f},{3,2}}};
        // Independent UV-gradient construction, bounded to the orthogonal charts
        // and positive scales in the scene. This does not certify arbitrary skew,
        // mirrored charts, smooth-surface projection or degenerate UVs.
        for(bool rotate:{false,true}) for(const auto& tile:tiles) {
            fixture.Constant(1,{.7f,.8f,.9f,1});
            fixture.Bind(tiledProgram,tile[0],tile[1],rotate);const auto actual=fixture.Draw();
            fixture.Bind(baseProgram,tile[0],tile[1],rotate);const auto untiledFrame=fixture.Draw();
            fixture.Bind(cotangentProgram,tile[0],tile[1],rotate);const auto dualFrame=fixture.Draw();
            Check(Difference(actual,untiledFrame)<2e-4f,"base/tiled derivative frames agree for positive authored scales");
            Check(Difference(actual,dualFrame)<2e-4f,"independent cotangent axes agree on bounded orthogonal chart");
            std::println("frame rotate={} tiling=({},{}) baseDelta={} cotangentDelta={}",rotate,tile[0],tile[1],Difference(actual,untiledFrame),Difference(actual,dualFrame));
        }
        fixture.Reset();
        std::println("[PASS] bounded derivative/cotangent frame comparison; single negative bitangent convention");
    }
    void Tiling(Fixture& fixture,const Shader& shader)
    {
        const std::array<std::array<Pixel,4>,5> patterns{{
            {{{.1f,.2f,.3f,1},{.8f,.2f,.1f,1},{.2f,.7f,.2f,1},{.6f,.5f,.8f,1}}},
            {{{.5f,.5f,1,1},{.8f,.5f,.9f,1},{.5f,.8f,.9f,1},{.2f,.5f,.9f,1}}},
            {{{.05f,0,0,1},{.8f,0,0,1},{.4f,0,0,1},{.95f,0,0,1}}},
            {{{.2f,0,0,1},{.8f,0,0,1},{.5f,0,0,1},{.95f,0,0,1}}},
            {{{.05f,0,0,1},{.9f,0,0,1},{.4f,0,0,1},{.7f,0,0,1}}}
        }};
        // At pixel (32,32), UV=(.251953125,.251953125). Integer tiling
        // selects all four texels, including repeat beyond one UV interval.
        const std::array<std::array<float,2>,4> tiles{{{1,1},{3,1},{1,3},{7,7}}};
        const char* channels[]{"albedo","normal","metallic","roughness","ao"};
        for(int channel=0;channel<5;++channel) for(int cell=0;cell<4;++cell) {
            fixture.Reset();fixture.Map(channel,patterns[channel]);
            fixture.Bind(shader,tiles[cell][0],tiles[cell][1]);const auto actual=fixture.Draw();
            fixture.Constant(channel,patterns[channel][cell]);const auto expected=fixture.Draw();
            std::println("tiled channel={} texel={} error={}",channels[channel],cell,Difference(actual,expected));
            Check(Difference(actual,expected)<5e-4f,"tiled PBR output equals independently selected constant texel");
            if(cell) {
                fixture.Constant(channel,patterns[channel][0]);const auto untiled=fixture.Draw();
                Check(Difference(expected,untiled)>1e-3f,"fixture distinguishes tiled from untiled channel");
            }
        }
        fixture.Reset();
        std::println("[PASS] tiled-PBR channels: independent full-lighting pixel oracle, X/Y/repeat");
    }
    void Roughness(Fixture& fixture,const std::string& source)
    {
        auto shader=Program(source);fixture.Reset();fixture.Bind(shader);
        const auto scale=glGetUniformLocation(Name(shader),"roughnessScale");
        // First compare the complete production BRDF with independently scaled linear data.
        fixture.Constant(3,{.28332952f,0,0,1});glUniform1f(scale,.5f);const auto actual=fixture.Draw();
        glUniform1f(scale,1.f);fixture.Constant(3,{.14166476f,0,0,1});const auto expected=fixture.Draw();
        Check(Difference(actual,expected)<5e-4f,"authored roughness scale equals linear map scaling");
        Check(scale>=0,"roughness material factor is active");
        auto observed=source;observed.insert(observed.find('\n')+1,"#define main originalLightingMain\n");
        observed+=R"(
#undef main
uniform float fixtureAngle;
void main() {
    float r=texture(roughnessMap,fs_in.TexCoords*u_tiling).r*roughnessScale;
    vec3 n=vec3(0,0,1),h=vec3(sin(fixtureAngle),0,cos(fixtureAngle));
    FragColor=vec4(vec3(DistributionGGX(n,h,r)/DistributionGGX(n,n,r)),1);
}
)";
        auto distribution=Program(observed);fixture.Bind(distribution);fixture.Constant(3,{.28332952f,0,0,1});
        float widths[2]{};
        for(int index=0;index<2;++index) {
            glUniform1f(glGetUniformLocation(Name(distribution),"roughnessScale"),index?.5f:1.f);
            for(int step=0;step<=160;++step) {
                const float angle=step*.0005f;
                glUniform1f(glGetUniformLocation(Name(distribution),"fixtureAngle"),angle);
                const auto pixel=fixture.Draw();
                if(!step) Check(std::abs(pixel[0]-1.f)<1e-4f,"GGX peak normalizes to unity");
                if(pixel[0]<=.5f) { widths[index]=angle;break; }
            }
            Check(widths[index]>0,"GGX half maximum is inside measured angular range");
        }
        Check(widths[0]>.050f && widths[0]<.054f && widths[1]>.012f && widths[1]<.014f,
            "floor GGX half width agrees with linear roughness and its authored factor");
        std::println("[PASS] floor roughness: GGX half-width radians {} -> {}; unchanged normal/gamma/light equations",widths[0],widths[1]);
        // Representative low/median/high roughness of the measured metallic patches.
        for(float raw:{74.f/255,78.f/255,95.f/255}) {
            const float legacy=std::pow((raw+.055f)/1.055f,2.4f);
            const float factors[]{legacy/raw,.9f,.28f};float metalWidths[3]{};
            fixture.Constant(3,{raw,0,0,1});
            for(int variant=0;variant<3;++variant) {
                glUniform1f(glGetUniformLocation(Name(distribution),"roughnessScale"),factors[variant]);
                for(int step=0;step<=500;++step) {
                    const float angle=step*.00025f;
                    glUniform1f(glGetUniformLocation(Name(distribution),"fixtureAngle"),angle);
                    if(fixture.Draw()[0]<=.5f) {metalWidths[variant]=angle;break;}
                }
                Check(metalWidths[variant]>0,"sphere GGX half width measured inside angular range");
            }
            Check(std::abs(metalWidths[2]-metalWidths[0])<.002f &&
                std::abs(metalWidths[2]-metalWidths[0])<.1f*std::abs(metalWidths[1]-metalWidths[0]),
                "authored sphere factor recovers measured legacy metal-patch lobe width");
            std::println("[PASS] sphere roughness raw={} half-width legacy={} recovered={} authored={}",raw,metalWidths[0],metalWidths[1],metalWidths[2]);
        }
        fixture.Reset();
    }
    void ColorOutput(Fixture& fixture,const Shader& shader)
    {
        GLint outputs{};glGetProgramInterfaceiv(Name(shader),GL_PROGRAM_OUTPUT,GL_ACTIVE_RESOURCES,&outputs);
        Check(outputs==1 && glGetFragDataLocation(Name(shader),"FragColor")==0,"color program has exactly one output at zero");
        Check(glGetFragDataLocation(Name(shader),"o_EntityID")==-1,"color program has no entity output");
        Check(glGetUniformLocation(Name(shader),"u_EntityID")==-1 && glGetUniformLocation(Name(shader),"u_entityID")==-1,"no stale entity uniform");
        const int sentinel=-12345;glClearTexImage(fixture.entity,0,GL_RED_INTEGER,GL_INT,&sentinel);
        fixture.Bind(shader);fixture.Draw();
        std::array<int,64*64> pixels{};
        glGetTextureImage(fixture.entity,0,GL_RED_INTEGER,GL_INT,sizeof(pixels),pixels.data());
        for(int value:pixels) Check(value==sentinel,"color-only draw routing preserves attached integer image");
    }
    void Entity(Fixture& fixture,const Shader& pbr,const std::string& helperSource,const std::string& pickSource)
    {
        auto helper=Program(helperSource);ColorOutput(fixture,pbr);ColorOutput(fixture,helper);
        auto pick=Program(pickSource);
        Check(glGetFragDataLocation(Name(pick),"o_EntityID")==0 && glGetUniformLocation(Name(pick),"u_EntityID")>=0,"dedicated picking has explicit integer output and ID uniform");
        glNamedFramebufferTexture(fixture.fbo,GL_COLOR_ATTACHMENT0,fixture.picking,0);
        Check(glCheckNamedFramebufferStatus(fixture.fbo,GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"integer picking framebuffer complete");
        pick.Bind();glEnable(GL_SCISSOR_TEST);glScissor(0,0,32,64);
        for(int id:{7,123456789,2147483646}) {
            const int background=-1;glClearTexImage(fixture.picking,0,GL_RED_INTEGER,GL_INT,&background);
            Int(pick,"u_EntityID",id);glDrawArrays(GL_TRIANGLES,0,3);
            std::array<int,64*64> pixels{};glGetTextureImage(fixture.picking,0,GL_RED_INTEGER,GL_INT,sizeof(pixels),pixels.data());
            for(int y=0;y<64;++y) for(int x=0;x<64;++x) Check(pixels[y*64+x]==(x<32?id:-1),"explicit picked ID and background survive integer readback");
        }
        glDisable(GL_SCISSOR_TEST);glNamedFramebufferTexture(fixture.fbo,GL_COLOR_ATTACHMENT0,fixture.color,0);
        std::println("[PASS] entity-attachment/output fixture: color-only routing, dynamic picking IDs/background");
    }
}
int main(int argc,char** argv)
{
    Check(argc==2 || argc==3,"usage: pbr-shader-probe SHADER_DIRECTORY [normals|tiling|entity]");
    const std::string directory=argv[1], mode=argc==3?argv[2]:"all";
    const auto source=Read(directory+"/pbr_cascade_shadow.frag");
    const auto helper=Read(directory+"/point_light_sphere_visual.frag");
    const auto pick=Read(directory+"/mouse_pick.frag");
    RuntimeAssets::Initialize("RigidBodySimulation");
    for(int cycle=0;cycle<2;++cycle) {
        EngineContext root;WindowProperties properties;properties.m_Title="Phase 61 hidden PBR validation";
        properties.flag={WindowFlags::INVISIBLE};properties.m_Width=properties.m_Height=64;
        properties.m_MinWidth=properties.m_MinHeight=64;properties.m_IsVsync=false;
        auto initialized=root.Initialize({properties});Check(bool(initialized),"hidden owning context initialized");
        std::println("GPU={} GL={} GLSL={} cycle={} target=64x64 RGBA32F linear/R32I camera=(0,0,3) normalTolerance=0.0002 colorTolerance=0.0005",
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)),cycle);
        {
            Fixture fixture;auto shader=Program(source);
            if(mode=="all" || mode=="normals") { Normals(fixture,source); BoundedFrames(fixture,source); }
            if(mode=="all" || mode=="tiling") Tiling(fixture,shader);
            if(mode=="all" || mode=="roughness") Roughness(fixture,source);
            if(mode=="all" || mode=="entity") Entity(fixture,shader,helper,pick);
        }
        Check(glGetError()==GL_NO_ERROR,"fixture resources retired before context teardown without driver errors");
    }
    std::println("[PASS] pbr-shader checks={} two context lifetimes complete",checks);
}

#else
// Runs the actual application material authoring/load path with flat PNG normals.
// All native observation is test-only and restores the submitted draw's bindings.
#include "RigidBodySimulation.h"
#include "EntryPoint.h"
#include "../GEngine/src/Assets/ShaderBackend.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <print>
#include <set>

namespace {
using namespace ::GEngine;
using namespace ::GEngine::Asset;
std::set<GLuint> observedNormals,observedPrograms;
bool observedFloorScale{},observedSphereScale{};
PFNGLDRAWARRAYSPROC originalArrays{};
PFNGLDRAWELEMENTSPROC originalElements{};
PFNGLDRAWARRAYSINSTANCEDPROC originalArrayInstances{};
PFNGLDRAWELEMENTSINSTANCEDPROC originalElementInstances{};
void Require(bool value,const char* message) {
    if(!value) {std::println(stderr,"[FAIL] {}",message);std::exit(1);}
}
void ObserveNormal() {
    GLint program{};glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    const auto location=glGetUniformLocation(program,"normalMap");
    if(location<0) return;
    GLint normalUnit{},active{},normalTexture{};
    glGetUniformiv(program,location,&normalUnit);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    glActiveTexture(GL_TEXTURE0+normalUnit);glGetIntegerv(GL_TEXTURE_BINDING_2D,&normalTexture);glActiveTexture(active);
    if(observedPrograms.insert(program).second) {
        const auto scale=glGetUniformLocation(program,"roughnessScale");
        if(scale>=0) {
            float value{};glGetUniformfv(program,scale,&value);
            Require(value==1.f,"unmodified materials retain default roughness scale");
        } else {
            GLint storage{},offset{};glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,0,&storage);
            const auto materialOffset=glGetUniformLocation(program,"geMaterialOffset");
            Require(storage && materialOffset>=0,"floor roughness parameter uses packed material storage");
            glGetUniformiv(program,materialOffset,&offset);
            std::array<float,12> words{};glGetNamedBufferSubData(storage,(offset+1)*16,sizeof(words),words.data());
            if(words[4]==.8f) {
                Require(words[0]==1.f && words[1]==1.f && words[8]==.28f,
                    "sphere roughness scale/adjacent packed material lanes");
                observedSphereScale=true;
            } else if(words[1]==.2f) {
                // The recovered owner wall material also authors a factor.
                Require(words[0]==2.f && words[4]==.08f && words[8]==.5f,
                    "wall roughness scale/adjacent packed material lanes");
            } else {
                Require(words[0]==2.f && words[1]==2.f && words[4]==.08f && words[8]==.5f,
                    "floor roughness scale/adjacent packed material lanes");
                observedFloorScale=true;
            }
            std::println("production packed tiling=({},{}) metalness={} roughnessScale={}",words[0],words[1],words[4],words[8]);
        }
    }
    if(!observedNormals.insert(normalTexture).second) return;
    // Observe the submitted texture and sampler, before and after signed decode.
    constexpr const char* sample=R"(#version 450 core
layout(local_size_x=1) in;
layout(binding=7) uniform sampler2D imageInput;
layout(std430,binding=7) buffer Observation { vec4 rawValue; vec4 decodedValue; };
void main() { rawValue=textureLod(imageInput,vec2(0.5),0); decodedValue=vec4(rawValue.xyz*2.0-1.0,1); }
)";
    const ShaderSource source{ShaderStage::Compute,sample,"application PBR sampling observation"};
    auto shader=Shader::Create({{&source,1}});
    Require(bool(shader),"application PBR observation shader compilation");
    GLint savedTexture{},savedSampler{},savedBuffer{},savedGeneric{};
    glActiveTexture(GL_TEXTURE7);glGetIntegerv(GL_TEXTURE_BINDING_2D,&savedTexture);glActiveTexture(active);
    glGetIntegeri_v(GL_SAMPLER_BINDING,7,&savedSampler);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,7,&savedBuffer);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&savedGeneric);
    GLuint buffer{};glCreateBuffers(1,&buffer);glNamedBufferData(buffer,sizeof(float)*8,nullptr,GL_STREAM_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,buffer);
    const char* names[]{"normalMap","metallicMap","roughnessMap","aoMap","albedoMap"};
    for(int channel=0;channel<5;++channel) {
        GLint unit{},texture{},format{},sampler{};
        const auto input=glGetUniformLocation(program,names[channel]);
        Require(input>=0,"production PBR channel is active");
        glGetUniformiv(program,input,&unit);
        glActiveTexture(GL_TEXTURE0+unit);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glActiveTexture(active);
        glGetIntegeri_v(GL_SAMPLER_BINDING,unit,&sampler);
        glGetTextureLevelParameteriv(texture,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
        glBindTextureUnit(7,texture);glBindSampler(7,sampler);
        shader->Bind();glDispatchCompute(1,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        std::array<float,8> values{};glGetNamedBufferSubData(buffer,0,sizeof(values),values.data());
        GLint swizzle[4]{};glGetTextureParameteriv(texture,GL_TEXTURE_SWIZZLE_RGBA,swizzle);
        std::println("production {} unit={} internal_format=0x{:x} raw=({},{},{}) signed=({},{},{}) swizzle=({:x},{:x},{:x},{:x})",
            names[channel],unit,format,values[0],values[1],values[2],values[4],values[5],values[6],swizzle[0],swizzle[1],swizzle[2],swizzle[3]);
        Require(swizzle[0]==GL_RED && swizzle[1]==GL_GREEN && swizzle[2]==GL_BLUE && swizzle[3]==GL_ALPHA,"PBR texture has identity RGBA swizzle");
        const bool linear=format==GL_RGBA8 || format==GL_RGB8 || format==GL_R8;
        if(channel==0) Require(linear && std::abs(values[0]-128.f/255)<.0001f
            && std::abs(values[1]-128.f/255)<.0001f && std::abs(values[4])<.005f
            && std::abs(values[5])<.005f && values[6]>.999f,
            "production normal map sampled as non-linear vector data");
        else if(channel<4) {
            Require(linear, "production scalar map sampled as non-linear data");
            Require(std::abs(values[0]-(64.f*channel)/255)<.0001f,
                "production scalar channel bound to the wrong material map");
        }
        else Require(format==GL_SRGB8_ALPHA8 || format==GL_SRGB8,
            "production albedo lost sRGB color sampling");
    }
    glUseProgram(program);glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,savedTexture);glActiveTexture(active);glBindSampler(7,savedSampler);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,savedBuffer);glBindBuffer(GL_SHADER_STORAGE_BUFFER,savedGeneric);glDeleteBuffers(1,&buffer);
    Require(glGetError()==GL_NO_ERROR,"application PBR observation leaves no GL error");
}
void APIENTRY Arrays(GLenum mode,GLint first,GLsizei count) {ObserveNormal();originalArrays(mode,first,count);}
void APIENTRY Elements(GLenum mode,GLsizei count,GLenum type,const void* indices) {ObserveNormal();originalElements(mode,count,type,indices);}
void APIENTRY ArrayInstances(GLenum mode,GLint first,GLsizei count,GLsizei instances) {ObserveNormal();originalArrayInstances(mode,first,count,instances);}
void APIENTRY ElementInstances(GLenum mode,GLsizei count,GLenum type,const void* indices,GLsizei instances) {ObserveNormal();originalElementInstances(mode,count,type,indices,instances);}
class ApplicationNormalProbe final:public RigidBodySimulationApp {
    unsigned updates{};bool installed{};
public:
    ~ApplicationNormalProbe() override {
        if(installed) {glad_glDrawArrays=originalArrays;glad_glDrawElements=originalElements;
            glad_glDrawArraysInstanced=originalArrayInstances;glad_glDrawElementsInstanced=originalElementInstances;}
    }
    void Update(Timestep) override {++updates;RigidBodySimulationApp::Update(Timestep(0));}
    void Render() override {
        if(!installed) {
            originalArrays=glad_glDrawArrays;glad_glDrawArrays=Arrays;
            originalElements=glad_glDrawElements;glad_glDrawElements=Elements;
            originalArrayInstances=glad_glDrawArraysInstanced;glad_glDrawArraysInstanced=ArrayInstances;
            originalElementInstances=glad_glDrawElementsInstanced;glad_glDrawElementsInstanced=ElementInstances;installed=true;
        }
        RigidBodySimulationApp::Render();
        if(updates>=90) {
            Require(observedFloorScale,"floor roughness override reached the GPU instead of the GLSL default");
            Require(observedSphereScale,"sphere roughness override reached the GPU instead of the GLSL default");
            Require(observedNormals.size()==2,"actual sphere and tile normal material paths both exercised");
            std::println("[PASS] production normal-map contract: two application materials preserve normal/scalar linear data and sRGB albedo");
            m_Running=false;
        }
    }
};
}
BaseApp* CreateApp() { return new ApplicationNormalProbe; }
#endif
