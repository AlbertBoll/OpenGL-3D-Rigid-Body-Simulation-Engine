#version 460

in vec3 uv;

out vec4 out_color;

uniform samplerCube u_skyBoxDay;

void main()
{

	out_color =  texture(u_skyBoxDay, uv);

}