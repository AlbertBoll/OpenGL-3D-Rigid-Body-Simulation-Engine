#version 450 core
//out vec4 FragColor;

layout(location = 0) out vec4 FragColor;
//layout(location = 1) out int o_EntityID;

in VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
} fs_in;

uniform sampler2D diffuseTexture;
uniform sampler2DArray shadowMap;
uniform samplerCube pointShadowDepthMap;

uniform vec3 lightDir;

uniform vec3 lightPos; // position of the light source
uniform vec3 viewPos;
uniform float farPlane;

uniform vec3 pointlightColor;

uniform float pointShadowfarPlane;
uniform bool shadows;


// array of offset direction for sampling
vec3 gridSamplingDisk[20] = vec3[]
(
   vec3(1, 1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1, 1,  1), 
   vec3(1, 1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1, 1, -1),
   vec3(1, 1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1, 1,  0),
   vec3(1, 0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1, 0, -1),
   vec3(0, 1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0, 1, -1)
);

uniform mat4 u_view;
uniform vec2 u_tiling;
uniform int u_entityID;

layout (std140) uniform LightSpaceMatrices
{
    mat4 lightSpaceMatrices[16];
};

uniform float cascadePlaneDistances[16];
uniform int cascadeCount;   // number of frusta - 1


// calculate shadow for point lights
float ShadowCalculation(vec3 fragPosWorldSpace);



float CascadeShadowCalculation(vec3 fragPosWorldSpace)
{
    // select cascade layer
    vec4 fragPosViewSpace = u_view * vec4(fragPosWorldSpace, 1.0);
    float depthValue = abs(fragPosViewSpace.z);

    int layer = -1;
    for (int i = 0; i < cascadeCount; ++i)
    {
        if (depthValue < cascadePlaneDistances[i])
        {
            layer = i;
            break;
        }
    }
    if (layer == -1)
    {
        layer = cascadeCount;
    }

    vec4 fragPosLightSpace = lightSpaceMatrices[layer] * vec4(fragPosWorldSpace, 1.0);
    // perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    // transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;

    // keep the shadow at 0.0 when outside the far_plane region of the light's frustum.
    if (currentDepth > 1.0)
    {
        return 0.0;
    }
    // calculate bias (based on depth map resolution and slope)
    vec3 normal = normalize(fs_in.Normal);
    float bias = max(0.05 * (1.0 - dot(normal, lightDir)), 0.005);
    const float biasModifier = 0.5f;
    if (layer == cascadeCount)
    {
        bias *= 1 / (farPlane * biasModifier);
    }
    else
    {
        bias *= 1 / (cascadePlaneDistances[layer] * biasModifier);
    }

    // PCF
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, layer)).r;
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;
        
    return shadow;
}

void main()
{          
    vec2 uvs = vec2(fs_in.TexCoords.x * u_tiling.x, fs_in.TexCoords.y * u_tiling.y);
    vec3 color = texture(diffuseTexture, uvs).rgb;
    vec3 normal = normalize(fs_in.Normal);
    vec3 ambientlightColor = vec3(0.3);
    // ambient
    vec3 ambient = 0.3 * color;

    // directional diffuse
    float directional_diff = max(dot(lightDir, normal), 0.0);
    vec3 directional_diffuse = directional_diff * ambientlightColor;

    // directional specular
    vec3 viewDir = normalize(viewPos - fs_in.FragPos);
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = 0.0;
    vec3 halfwayDir = normalize(lightDir + viewDir);  
    spec = pow(max(dot(normal, halfwayDir), 0.0), 64.0);
    vec3 directional_specular = spec * ambientlightColor;    

    // point light diffuse
    float distance_ = length(lightPos - fs_in.FragPos);
    //float attenuation = 1.0;
    float attenuation = 20.0 / distance_;
    vec3 point_light_dir = normalize(lightPos - fs_in.FragPos);
    float point_light_diff = max(dot(point_light_dir, normal), 0.0);
    vec3 point_light_diffuse = (point_light_diff * pointlightColor) * attenuation;

    // point light specular
    vec3 point_light_reflect_dir = reflect(-point_light_dir, normal);
    float point_light_spec = 0.0;
    vec3 point_light_halfway_dir = normalize(point_light_dir + viewDir);
    point_light_spec = pow(max(dot(normal, point_light_halfway_dir), 0.0), 64.0);
    vec3 point_light_specular = (point_light_spec * pointlightColor) * attenuation;


    // calculate shadow
    float point_light_shadow = ShadowCalculation(fs_in.FragPos);
    float cascade_shadow = CascadeShadowCalculation(fs_in.FragPos); 
   
    //vec3 lighting = ambient + (diffuse + specular) * color;
    //vec3 lighting = (ambient + (1.0 - cascade_shadow) * (directional_diffuse + directional_specular)) * color; 
    vec3 lighting = ambient * color + (1.0 - cascade_shadow) * (directional_diffuse + directional_specular) * color + (1 - point_light_shadow) * (point_light_diffuse + point_light_specular) * color;  
    //vec3 lighting = ambient + (1 - point_light_shadow) * (point_light_diffuse + point_light_specular);  
    //vec3 lighting = ambient * color + (point_light_diffuse + point_light_specular) * color;
    lighting = pow(lighting, vec3(1.0/0.9)); // Convert to linear space
    FragColor = vec4(lighting, 1.0);
    //o_EntityID = u_entityID;
  
}


float ShadowCalculation(vec3 fragPosWorldSpace)
{

     // get vector between fragment position and light position
    vec3 fragToLight = fragPosWorldSpace - lightPos;
    // use the fragment to light vector to sample from the depth map    
    // float closestDepth = texture(depthMap, fragToLight).r;
    // it is currently in linear range between [0,1], let's re-transform it back to original depth value
    // closestDepth *= far_plane;
    // now get current linear depth as the length between the fragment and light position
    float currentDepth = length(fragToLight);
   
    float shadow = 0.0;
    float bias = 0.15;
    int samples = 20;
    float viewDistance = length(viewPos - fragPosWorldSpace);
    float diskRadius = (1.0 + (viewDistance / pointShadowfarPlane)) / 25.0;
    for(int i = 0; i < samples; ++i)
    {
        //float closestDepth = 0.01f;
        float closestDepth = texture(pointShadowDepthMap, fragToLight + gridSamplingDisk[i] * diskRadius).r;
        closestDepth *= pointShadowfarPlane;   // undo mapping [0;1]
        if(currentDepth - bias > closestDepth)
        {
            shadow += 1.0;
        }
    }
    shadow /= float(samples);
        
    return shadow;
}

