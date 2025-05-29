#version 460

layout(location=0) in vec3 position;

out vec3 uv;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;


void main()
{
	uv = position;
	vec4 pos = u_projection * u_view * u_model * vec4(position, 1.0f);;
	gl_Position = pos.xyww;
	

}


