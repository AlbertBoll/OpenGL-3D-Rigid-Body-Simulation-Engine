#version 460

uniform vec3 u_baseColor;

uniform sampler2D u_texture;
//uniform sampler2DMS uTexture;

in vec2 UV;
out vec4 fragColor;

void main()
{
	vec4 color = vec4(u_baseColor, 1.f);
	
	fragColor = texture2D(u_texture, UV) ;//* vec4(uBaseColor, 1.0f);
	//fragColor = color * texture2D(uTexture, UV);
}