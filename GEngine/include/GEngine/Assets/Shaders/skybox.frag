#version 460

in vec3 uv;

out vec4 out_color;

uniform samplerCube u_SkyBoxDay;
uniform samplerCube u_SkyBoxNight;
uniform float u_BlendFactor;
uniform vec3 u_FogColor;


uniform float u_LowerLimit;
uniform float u_UpperLimit;

void main()
{
	vec4 texture_day = texture(u_SkyBoxDay, uv);
	vec4 texture_night = texture(u_SkyBoxNight, uv);
	vec4 finalColor = mix(texture_day, texture_night, u_BlendFactor);
	float factor = (uv.y - u_LowerLimit)/(u_UpperLimit-u_LowerLimit);
	factor = clamp(factor, 0.f, 1.0f);


	out_color = mix(vec4(u_FogColor, 1.0f), finalColor, factor);

}