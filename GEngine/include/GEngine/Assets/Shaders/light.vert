#version 460 core

layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec3 normal;

out vec2 in_uv;


const int maxLightNumber = 6;
out vec3 surfaceNormal;
out vec3 toLightVector[maxLightNumber];
out vec3 toCameraVector;
out float visibility;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform vec3 u_lightPosition[maxLightNumber];
uniform vec3 u_cameraPosition;
uniform bool u_useFakeLighting;
uniform float u_density;
uniform float u_gradient;

uniform int u_numberOfRows;
uniform vec2 u_offset;

void main(void)
{
    vec4 worldPosition = u_model * vec4(position, 1.0f);

    vec4 positionRelativeToCamera = u_view * worldPosition;

    gl_Position = u_projection * positionRelativeToCamera;

    in_uv = (uv / u_numberOfRows) + u_offset;

    //vec3 actualNormal = normal;

    vec3 actualNormal = mix(normal, vec3(0, 1, 0), u_useFakeLighting);


    surfaceNormal = (u_model * vec4(actualNormal, 0.0f)).xyz;

    for(int i = 0; i < maxLightNumber; i++)
    {
        toLightVector[i] = u_lightPosition[i] - worldPosition.xyz;
    }
   // toLightVector = u_lightPosition - worldPosition.xyz;
    toCameraVector = u_cameraPosition - worldPosition.xyz;
    float distance = length(positionRelativeToCamera.xyz);
    visibility = clamp(exp(-pow((distance * u_density), u_gradient)), 0.f, 1.f);
}