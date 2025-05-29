#version 460

layout(location=0) in vec2 position;

uniform mat4 u_projection;
uniform mat4 u_modelView;

void main(void){

	gl_Position = u_projection *  u_modelView * vec4(position, 0.0, 1.0);

}