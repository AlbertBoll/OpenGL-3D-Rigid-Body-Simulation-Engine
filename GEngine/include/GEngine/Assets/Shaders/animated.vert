#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitagent;
layout(location = 5) in ivec4 boneIds; 
layout(location = 6) in vec4 weights;

const int maxLightNumber = 6;

out VS_OUT
{
	vec3 FragPos;
	vec2 TexCoords;
	vec3 TangentLightPos[maxLightNumber];
    vec3 TangentViewPos;
    vec3 TangentFragPos;
} vs_out;


out float visibility;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform vec3 u_lightPosition[maxLightNumber];
uniform vec3 u_cameraPosition;

uniform float u_density;
uniform float u_gradient;

uniform int u_numberOfRows;
uniform vec2 u_offset;

const int MAX_BONES = 100;
const int MAX_BONE_INFLUENCE = 4;
uniform mat4 u_finalBonesMatrices[MAX_BONES];

void main()
{
    vec4 totalLocalPosition = vec4(0.0f);
    //mat4 BoneTransform = mat4(1.0f);
    vec4 totalNormal = vec4(0.0f);
    vec4 totalTangent = vec4(0.0f);

    for(int i = 0 ; i < MAX_BONE_INFLUENCE ; i++)
    {
        mat4 boneTransform = u_finalBonesMatrices[boneIds[i]];
        if(boneIds[i] == -1) 
            continue;

        if(boneIds[i] >=MAX_BONES) 
        {
           totalLocalPosition = vec4(position,1.0f);
           totalNormal = vec4(normal, 0.0f);
           totalTangent = vec4(tangent, 0.0f);
           break;
        }

        vec4 localPosition = boneTransform * vec4(position,1.0f);
        vec4 worldNormal = boneTransform * vec4(normal,0.0f);
        vec4 worldTangent = boneTransform * vec4(tangent,0.0f);
        totalLocalPosition += localPosition * weights[i];
        totalNormal += worldNormal * weights[i];
        totalTangent += worldTangent * weights[i];
    }

	vec4 worldPosition = u_model * totalLocalPosition;
    vec4 positionRelativeToCamera = u_view * worldPosition;

    gl_Position = u_projection * positionRelativeToCamera;

   

    mat3 normalMatrix = transpose(inverse(mat3(u_model)));

    //vec3 T = normalize(mat3(u_model) * tangent);
    //vec3 N = normalize(normalMatrix * normal);
    vec3 T = normalize(mat3(u_model) * vec3(totalTangent));
    vec3 N = normalize(normalMatrix * vec3(totalNormal));
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    
    mat3 TBN = transpose(mat3(T, B, N));

    for(int i = 0; i < maxLightNumber; ++i)
    {
        vs_out.TangentLightPos[i] = TBN * u_lightPosition[i];
    }

    vs_out.FragPos = vec3(worldPosition);   
    vs_out.TexCoords = (uv / u_numberOfRows) + u_offset;
    vs_out.TangentViewPos  = TBN * u_cameraPosition;
    vs_out.TangentFragPos  = TBN * vs_out.FragPos;

    //toCameraVector =   normalize(vs_out.TangentViewPos - vs_out.TangentFragPos);

    //toCameraVector = u_cameraPosition - worldPosition.xyz;
    float distance = length(positionRelativeToCamera.xyz);
    visibility = clamp(exp(-pow((distance * u_density), u_gradient)), 0.f, 1.f);

}