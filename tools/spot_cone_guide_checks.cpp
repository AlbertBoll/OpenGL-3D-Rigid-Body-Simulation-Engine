#include "RbsSpotGuide.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace Rbs::Editor;
struct Segment {glm::vec3 a,b;SpotGuidePart part;};
struct Guide {std::array<Segment,169> segments{};std::size_t count{};};
unsigned checks{};
void Check(bool value,const char* label) {++checks;if(!value){std::printf("FAIL %s\n",label);std::exit(1);}}
bool Near(glm::vec3 a,glm::vec3 b,float tolerance=2e-4f){return glm::length(a-b)<tolerance;}
Guide Build(const glm::mat4& world,SpotGuideVisibility visibility={true,true,true}) {
    const auto frame=MakeSpotGuideFrame(world);Check(bool(frame),"valid Light world frame");
    Guide result;
    EmitSpotGuide(*frame,50.f,.4f,.6f,visibility,[&](auto a,auto b,auto part){
        Check(result.count<result.segments.size(),"bounded segment storage");
        result.segments[result.count++]={a,b,part};
    });
    return result;
}
glm::vec3 LegacyRight(const glm::mat4& world) {
    auto d=glm::normalize(-glm::vec3(world[2]));
    return glm::normalize(glm::cross(d,std::abs(d.y)>.99f?glm::vec3(1,0,0):glm::vec3(0,1,0)));
}
int main() {
    const auto identity=glm::mat4(1);
    const auto local=Build(identity);
    Check(local.count==169,"unchanged 52 inner + 52 outer + 65 range segments");
    const auto authored=glm::rotation(glm::vec3(0,0,-1),-glm::normalize(glm::vec3(20,50,20)));
    const auto owner=glm::translate(identity,glm::vec3(0,15,0))*glm::mat4_cast(authored);
    const auto ownerGuide=Build(owner);
    const auto ownerFrame=*MakeSpotGuideFrame(owner);
    Check(Near(ownerFrame.direction,-glm::normalize(glm::vec3(20,50,20))),"exact authored render light axis");
    float maximumProjectionError=0;
    for(const auto rotation:{authored,glm::quat(glm::radians(glm::vec3(25,40,65))),glm::angleAxis(1.2f,glm::vec3(0,0,1))}) {
        const auto world=glm::translate(identity,glm::vec3(0,15,0))*glm::mat4_cast(rotation);
        const auto guide=Build(world);
        for(std::size_t i=0;i<guide.count;++i) {
            Check(Near(guide.segments[i].a,glm::vec3(world*glm::vec4(local.segments[i].a,1))),"guide endpoint follows full Light orientation");
            Check(Near(guide.segments[i].b,glm::vec3(world*glm::vec4(local.segments[i].b,1))),"guide endpoint follows translation/roll");
        }
    }
    const auto rolled=owner*glm::mat4_cast(glm::angleAxis(glm::half_pi<float>(),glm::vec3(0,0,1)));
    Check(Near(LegacyRight(owner),LegacyRight(rolled)),"reproduce legacy roll omission");
    Check(std::abs(glm::dot(MakeSpotGuideFrame(owner)->right,MakeSpotGuideFrame(rolled)->right))<1e-5f,"new guide follows local roll");
    const float pole=std::asin(.99f);
    const auto poleA=glm::rotate(identity,pole-1e-4f,glm::vec3(1,0,0));
    const auto poleB=glm::rotate(identity,pole+1e-4f,glm::vec3(1,0,0));
    Check(std::abs(glm::dot(LegacyRight(poleA),LegacyRight(poleB)))<.01f,"reproduce legacy world-up seam jump");
    Check(glm::dot(MakeSpotGuideFrame(poleA)->right,MakeSpotGuideFrame(poleB)->right)>.999f,"continuous Light-local frame across former pole threshold");
    for(int flags=0;flags<8;++flags) {
        const auto guide=Build(owner,{bool(flags&1),bool(flags&2),bool(flags&4)});
        Check(guide.count==std::size_t(52*bool(flags&1)+52*bool(flags&2)+65*bool(flags&4)),"independent visibility options");
        for(std::size_t i=0;i<guide.count;++i)for(const auto p:{guide.segments[i].a,guide.segments[i].b}) {
            const float distance=glm::length(p-ownerFrame.position);
            Check(distance<1e-4f || std::abs(distance-50.f)<2e-4f,"radial range remains exactly 50");
        }
    }
    // The same world geometry is projected through orbit/pan/dolly, perspective
    // and independent orthographic views. Inverse projection recovers the Light
    // geometry, not a camera-facing approximation. No scene mutation is involved.
    const std::array<glm::dvec3,5> eyes{{{0,30,70},{45,25,45},{-40,55,25},{0,20,40},{10,40,80}}};
    for(int view=0;view<int(eyes.size());++view)for(double scale:{1.,1.25,1.5,2.}) {
        const auto camera=glm::lookAt(eyes[view],glm::dvec3(0,0,0),glm::dvec3(0,1,0));
        const auto projection=view==4?glm::ortho(-70.,70.,-50.,50.,.1,500.):glm::perspective(glm::radians(45.),1068./760.,.1,500.);
        const auto vp=projection*camera;
        const auto guide=Build(owner);
        for(std::size_t i=0;i<guide.count;++i)for(int end=0;end<2;++end) {
            const auto p=end?guide.segments[i].b:guide.segments[i].a;
            const auto original=end?ownerGuide.segments[i].b:ownerGuide.segments[i].a;
            Check(p==original,"camera motion does not mutate world endpoints");
            const auto clip=vp*glm::dvec4(p,1),ndc=clip/clip.w;
            const glm::dvec2 pixel{(ndc.x*.5+.5)*1068*scale,(.5-ndc.y*.5)*760*scale};
            auto restored=glm::inverse(vp)*glm::dvec4(pixel.x/(1068*scale)*2-1,1-pixel.y/(760*scale)*2,ndc.z,1);
            restored/=restored.w;
            const auto error=glm::length(glm::vec3(restored)-p);
            maximumProjectionError=std::max(maximumProjectionError,error);
            Check(error<2e-4f,"perspective/DPI appearance changes preserve world orientation");
        }
    }
    const auto scaled=*MakeSpotGuideFrame(owner*glm::scale(identity,glm::vec3(2,3,4)));
    Check(Near(scaled.direction,ownerFrame.direction)&&Near(scaled.right,ownerFrame.right),"remove nonuniform local scale without scaling range");
    auto invalid=identity;invalid[2]=glm::vec4(0);
    Check(!MakeSpotGuideFrame(invalid),"reject degenerate axis");
    invalid=identity;invalid[0][0]=std::numeric_limits<float>::quiet_NaN();
    Check(!MakeSpotGuideFrame(invalid),"reject nonfinite transform");
    std::printf("PASS checks=%u projection_max_error=%g\n",checks,maximumProjectionError);
    std::puts("Legacy: world endpoints were already camera-independent; roll omission and near-vertical frame discontinuity reproduced. No camera-facing cone axis established.");
}
