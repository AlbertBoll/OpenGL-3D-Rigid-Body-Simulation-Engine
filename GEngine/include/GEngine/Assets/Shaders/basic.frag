#version 460

uniform vec4 u_baseColor;
uniform bool u_useVertexColor;

in vec3 color;
out vec4 fragColor;


void main()
{
	vec4 tempColor = u_baseColor; //vec4(uBaseColor, 1.0f);
	if (u_useVertexColor)
	{
		tempColor *= vec4(color, 1.0f);
	};

	fragColor = tempColor;
}