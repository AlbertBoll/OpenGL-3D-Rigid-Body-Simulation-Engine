#version 450 core

layout(location = 0) out int o_EntityID;

uniform int u_EntityID;


void main()
{
	o_EntityID = u_EntityID;
}