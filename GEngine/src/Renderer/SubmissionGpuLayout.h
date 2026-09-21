#pragma once

// Private ABI for the existing immutable-frame submitter and its shader adapters.
#include "Renderer/SceneRenderResources.h"
#include <algorithm>
#include <cstddef>
#include <regex>
#include <string>

namespace GEngine::RenderBackend
{
    using MatrixWords = std::array<float,16>;
    using FloatLane = std::array<float,4>;
    struct alignas(16) PackedInstance
    {
        MatrixWords model{};
        // Full shared identity: slot, generation low/high, domain low/high.
        // The separately encoded signed picking pixel is not an entity identity.
        // identity1.zw hold directional/point shadow layer masks, not identities.
        std::array<std::uint32_t,4> identity0{}, identity1{};
    };
    static_assert(sizeof(PackedInstance)==96 && offsetof(PackedInstance,identity0)==64
        && offsetof(PackedInstance,identity1)==80 && std::is_standard_layout_v<PackedInstance>);
    inline constexpr const char* InstanceBlock=R"(
struct GEngineInstance { mat4 model; uvec4 identity0; uvec4 identity1; };
layout(std430,binding=1) readonly buffer GEngineInstances { GEngineInstance geInstances[]; };
uniform bool geInstanced;
uniform uint geInstanceBase;
)";
    struct alignas(16) PackedFrame
    {
        MatrixWords view{}, projection{}, skyView{};
        std::array<MatrixWords,16> cascades{};
        std::array<MatrixWords,6> pointMatrices{};
        FloatLane viewPosition{}, lightDirection{}, lightPosition{}, pointColor{}, directionalColor{}, planes{};
        std::array<std::uint32_t,4> lights{};
        std::array<FloatLane,16> splits{};
        // Appended lanes preserve directional/point offsets. Position.w is range
        // (zero disables); direction.w/color.w are inner/outer cone cosines.
        FloatLane spotPosition{}, spotDirection{}, spotColor{};
    };
    static_assert(std::is_standard_layout_v<PackedFrame> && alignof(PackedFrame)==16);
    static_assert(sizeof(MatrixWords)==64 && sizeof(FloatLane)==16);
    static_assert(offsetof(PackedFrame,cascades)==192 && offsetof(PackedFrame,pointMatrices)==1216);
    static_assert(offsetof(PackedFrame,viewPosition)==1600 && offsetof(PackedFrame,lights)==1696);
    static_assert(offsetof(PackedFrame,splits)==1712 && offsetof(PackedFrame,spotPosition)==1968
        && offsetof(PackedFrame,spotDirection)==1984 && offsetof(PackedFrame,spotColor)==2000 && sizeof(PackedFrame)==2016);

    inline constexpr const char* FrameBlock=R"(
layout(std140,binding=1) uniform GEngineFrame {
    mat4 geView, geProjection, geSkyView;
    mat4 geCascades[16], gePointMatrices[6];
    vec4 geViewPosition, geLightDirection, geLightPosition, gePointColor, geDirectionalColor, gePlanes;
    uvec4 geLights;
    vec4 geSplits[16];
    vec4 geSpotPosition, geSpotDirection, geSpotColor;
};
uniform bool geReceiveShadows;
)";
    inline constexpr const char* MaterialBlock=R"(
layout(std430,binding=0) readonly buffer GEngineMaterials { uvec4 geMaterialWords[]; };
uniform uint geMaterialOffset;
#define frameAlphaMode int(geMaterialWords[geMaterialOffset].x)
#define frameAlphaCutoff uintBitsToFloat(geMaterialWords[geMaterialOffset].y)
#define frameOpacity uintBitsToFloat(geMaterialWords[geMaterialOffset].z)
#define framePremultiplied (geMaterialWords[geMaterialOffset].w != 0u)
#define frameMasked (frameAlphaMode == 1)
#define frameTiling uintBitsToFloat(geMaterialWords[geMaterialOffset+1u].xy)
)";
    inline void ReplaceAll(std::string& text,std::string_view from,std::string_view to)
    {
        std::size_t at=0;
        while((at=text.find(from,at))!=std::string::npos) { text.replace(at,from.size(),to);at+=to.size(); }
    }
    // Only the bounded scene/coverage adapter uses this ABI. Legacy shader files
    // and all other shader consumers retain their original default uniforms.
    inline std::expected<std::string,SceneResourceError> PackedStage(std::string source,bool sky,
        std::span<const MaterialParameterDecl> parameters={}, Asset::ShaderStage stage=Asset::ShaderStage::Fragment)
    {
        source.replace(0,source.find('\n'),"#version 450 core");
        // Split the adapter's known combined declarations before replacing names.
        ReplaceAll(source,"uniform mat4 u_projection, u_view, u_model;","uniform mat4 u_projection;\nuniform mat4 u_view;\nuniform mat4 u_model;");
        ReplaceAll(source,"uniform mat4 u_model, u_view, u_projection;","uniform mat4 u_model;\nuniform mat4 u_view;\nuniform mat4 u_projection;");
        ReplaceAll(source,"uniform float frameAlphaCutoff, frameOpacity;","uniform float frameAlphaCutoff;\nuniform float frameOpacity;");
        source=std::regex_replace(source,std::regex(R"(layout\s*\(std140(?:,binding=0)?\)\s*uniform LightSpaceMatrices\s*\{\s*mat4 lightSpaceMatrices\[16\];\s*\};)"),"");
        const std::pair<const char*,const char*> frameNames[]{
            {"u_view",sky?"geSkyView":"geView"},{"u_projection","geProjection"},{"viewPos","geViewPosition.xyz"},
            {"lightDir","geLightDirection.xyz"},{"lightPos","geLightPosition.xyz"},
            {"pointlightColor","gePointColor.xyz"},{"directionallightColor","geDirectionalColor.xyz"},
            {"farPlane","gePlanes.x"},{"pointShadowfarPlane","gePlanes.y"},{"far_plane","gePlanes.y"},
            {"frameLights","true"},{"frameDirectional","(geLights.x != 0u)"},{"framePoint","(geLights.y != 0u)"},
            {"frameDirectionalShadows","(geLights.z != 0u && geReceiveShadows)"},
            {"framePointShadows","(geLights.w != 0u && geReceiveShadows)"},
            {"frameSpot","(geSpotPosition.w > 0.0)"},{"spotPosition","geSpotPosition.xyz"},
            {"spotRange","geSpotPosition.w"},{"spotDirection","geSpotDirection.xyz"},
            {"spotInnerCos","geSpotDirection.w"},{"spotColor","geSpotColor.xyz"},{"spotOuterCos","geSpotColor.w"},
            {"reverse_normals","false"},{"cascadeCount","5"},{"shadowMatrices","gePointMatrices"},
            {"frameAlphaMode",nullptr},{"frameAlphaCutoff",nullptr},{"frameOpacity",nullptr},
            {"framePremultiplied",nullptr},{"frameMasked",nullptr},{"frameTiling",nullptr}};
        for(const auto& [name,value]:frameNames) {
            const std::regex declaration(std::string(R"(uniform\s+\w+\s+)")+name+R"((?:\[\d+\])?\s*;)");
            source=std::regex_replace(source,declaration,value?std::string("\n#define ")+name+" ("+value+")\n":"");
        }
        source=std::regex_replace(source,std::regex(R"(uniform float cascadePlaneDistances\[16\];)"),"");
        source=std::regex_replace(source,std::regex(R"(cascadePlaneDistances\[([^\]]+)\])"),"geSplits[$1].x");
        ReplaceAll(source,"lightSpaceMatrices","geCascades");
        // u_tiling has a frame-path default of (1,1) and is shared by coverage.
        ReplaceAll(source,"uniform vec2 u_tiling;","\n#define u_tiling frameTiling\n");
        std::vector<MaterialParameterDecl> sorted(parameters.begin(),parameters.end());
        std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.name<b.name;});
        std::uint32_t lane=2;
        constexpr const char* types[]{"bool","int","uint","float","vec2","vec3","vec4","mat4"};
        for(const auto& parameter:sorted) {
            const auto type=static_cast<unsigned>(parameter.type);
            if(type>=std::size(types) || !std::regex_match(parameter.name,std::regex("[A-Za-z_][A-Za-z_0-9]*")))
                return std::unexpected(SceneResourceError{"packed material declaration",SceneResourceCode::InvalidMaterial});
            if(parameter.name=="u_tiling") {
                if(parameter.type!=MaterialParameterType::Float2)
                    return std::unexpected(SceneResourceError{"packed material tiling type",SceneResourceCode::InvalidMaterial});
            } else {
                // Frame/draw values are renderer-owned, not material storage.
                if(parameter.name=="u_model" || parameter.name=="u_EntityID" || parameter.name.starts_with("ge")
                    || std::any_of(std::begin(frameNames),std::end(frameNames),[&](const auto& n){return parameter.name==n.first;}))
                    return std::unexpected(SceneResourceError{"reserved frame material parameter",SceneResourceCode::InvalidMaterial});
                // An authored parameter overrides its GLSL initializer; unbound parameters keep that default.
                const std::regex declaration(std::string(R"(uniform\s+(\w+)\s+)")+parameter.name+R"(\s*(?:=\s*[^;]+)?;)");
                std::smatch match;
                if(std::regex_search(source,match,declaration)) {
                    if(match[1].str()!=types[type])
                        return std::unexpected(SceneResourceError{"packed material shader type",SceneResourceCode::InvalidMaterial});
                    const auto word="geMaterialWords[geMaterialOffset+"+std::to_string(lane)+"u]";
                    std::string value;
                    switch(parameter.type) {
                    case MaterialParameterType::Boolean:value="("+word+".x != 0u)";break;
                    case MaterialParameterType::Integer:value="int("+word+".x)";break;
                    case MaterialParameterType::UnsignedInteger:value=word+".x";break;
                    case MaterialParameterType::Float:value="uintBitsToFloat("+word+".x)";break;
                    case MaterialParameterType::Float2:value="uintBitsToFloat("+word+".xy)";break;
                    case MaterialParameterType::Float3:value="uintBitsToFloat("+word+".xyz)";break;
                    case MaterialParameterType::Float4:value="uintBitsToFloat("+word+")";break;
                    case MaterialParameterType::Matrix4:
                        value="mat4(";
                        for(unsigned i=0;i<4;++i) { if(i) value+=",";value+="uintBitsToFloat(geMaterialWords[geMaterialOffset+"+std::to_string(lane+i)+"u])"; }
                        value+=")";break;
                    }
                    source=std::regex_replace(source,declaration,"\n#define "+parameter.name+" ("+value+")\n");
                }
            }
            lane+=parameter.type==MaterialParameterType::Matrix4?4:1;
        }
        if(stage==Asset::ShaderStage::Vertex) {
            ReplaceAll(source,"u_model","geModelTransform()");
            ReplaceAll(source,"uniform mat4 geModelTransform();",R"(uniform mat4 u_model;
mat4 geModelTransform() { return geInstanced ? geInstances[geInstanceBase+uint(gl_InstanceID)].model : u_model; }
)");
            source.insert(source.find('\n')+1,InstanceBlock);
        }
        source.insert(source.find('\n')+1,std::string(FrameBlock)+MaterialBlock);
        return source;
    }
}
