#version 460

in vec3 uv;

out vec4 out_color;

uniform samplerCube u_skyBoxDay;

void main()
{
	vec3 color = pow(texture(u_skyBoxDay, uv).rgb, vec3(1/2.2f));


	//out_color =  texture(u_skyBoxDay, uv);
	out_color =  vec4(color, 1.0f);

}