#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aNormal;

//out vec2 TexCoords;

out VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
} vs_out;

uniform mat4 u_projection;
uniform mat4 u_view;
uniform mat4 u_model;

uniform bool reverse_normals;

void main()
{
    vs_out.FragPos = vec3(u_model * vec4(aPos, 1.0));
    if (reverse_normals) 
    {
        vs_out.Normal = -transpose(inverse(mat3(u_model))) * aNormal;
    }
    else
    {
        vs_out.Normal = transpose(inverse(mat3(u_model))) * aNormal;
    }
    vs_out.TexCoords = aTexCoords;
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
}