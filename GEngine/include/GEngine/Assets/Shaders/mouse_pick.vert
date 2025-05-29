#version 450 core
layout (location = 0) in vec3 aPos;
//layout (location = 4) in int a_EntityID;

uniform mat4 u_projection;
uniform mat4 u_view;
uniform mat4 u_model;

//layout (location = 0) out flat int v_EntityID;

void main()
{
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
    //v_EntityID = a_EntityID;
}