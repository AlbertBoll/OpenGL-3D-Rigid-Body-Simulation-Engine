#version 460

layout(location=0) in vec3 vertexPosition;
layout(location=2) in vec2 vertexUV;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec2 UV;

void main()
{

	gl_Position = u_projection * u_view * u_model * vec4(vertexPosition, 1.0f);
	UV = vertexUV;

}
