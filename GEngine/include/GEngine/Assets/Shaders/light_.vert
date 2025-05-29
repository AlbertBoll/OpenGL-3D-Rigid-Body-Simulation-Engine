#version 460 core

layout(location=0) in vec3 position;
layout(location=2) in vec2 uv;
layout(location=3) in vec3 normal;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

void main()
{
    FragPos = vec3(u_model * vec4(position, 1.0));
    Normal = mat3(transpose(inverse(u_model))) * normal;  
    TexCoords = uv;
    
    gl_Position = u_projection * u_view * vec4(FragPos, 1.0);
}