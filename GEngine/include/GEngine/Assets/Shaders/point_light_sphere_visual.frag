#version 330 core
layout (location = 0) out vec4 FragColor;
layout(location = 1) out int o_EntityID;
//layout (location = 1) out vec4 BrightColor;

in VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
} fs_in;

uniform vec3 pointlightColor;

uniform int u_EntityID;

void main()
{         
    // ambient
    
    vec3 color = pow(pointlightColor, vec3(1.0/0.9)); // Convert to linear space
    
    FragColor = vec4(color, 1.0);
    o_EntityID = 50;
    //o_EntityID = u_EntityID;
//    float brightness = dot(FragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
//    if(brightness > 1.0)
//        BrightColor = vec4(FragColor.rgb, 1.0);
//	else
//		BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
}