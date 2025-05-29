#version 460 core

in vec3 position;
in vec2 uv;
in vec3 normal;

out vec2 pass_textureCoordinates;

const int maxLightNumber = 6;
out vec3 surfaceNormal;
out vec3 toLightVector[maxLightNumber];
out vec3 toCameraVector;
out float visibility;

uniform mat4 u_model;
uniform mat4 u_projection;
uniform mat4 u_view;
uniform vec3 u_lightPosition[maxLightNumber];
uniform vec3 u_cameraPosition;

uniform float u_density;
uniform float u_gradient;

void main(void){

    vec4 worldPosition = u_model * vec4(position,1.0);

    vec4 positionRelativeToCamera = u_view * worldPosition;

    gl_Position = u_projection * positionRelativeToCamera;
    pass_textureCoordinates = uv;

    surfaceNormal = (u_model * vec4(normal,0.0)).xyz;


    for(int i = 0; i < maxLightNumber; i++)
    {
        toLightVector[i] = u_lightPosition[i] - worldPosition.xyz;
    }

    toCameraVector = u_cameraPosition - worldPosition.xyz;

    float distance = length(positionRelativeToCamera.xyz);
    visibility = exp(-pow((distance * u_density), u_gradient));
    visibility = clamp(visibility, 0.0, 1.0);
}

