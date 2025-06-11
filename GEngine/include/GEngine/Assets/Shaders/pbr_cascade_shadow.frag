#version 450 core
//out vec4 FragColor;

layout(location = 0) out vec4 FragColor;
//layout(location = 1) out int o_EntityID;

in VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
} fs_in;

uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D metallicMap;
uniform sampler2D roughnessMap;
uniform sampler2D aoMap;

uniform sampler2D diffuseTexture;
uniform sampler2DArray shadowMap;
uniform samplerCube pointShadowDepthMap;

uniform vec3 lightDir;

uniform vec3 lightPos; // position of the light source
uniform vec3 viewPos;
uniform float farPlane;

uniform vec3 pointlightColor;
uniform vec3 directionallightColor;

uniform float pointShadowfarPlane;
uniform bool shadows;

uniform vec3 metalness;

const float PI = 3.14159265359;

vec3 getNormalFromMap(vec2 uvs)
{
    vec3 tangentNormal = texture(normalMap, fs_in.TexCoords).xyz;
    //vec3 tangentNormal = texture(normalMap, fs_in.TexCoords).xyz * 2.0 - 1.0;
    //tangentNormal.y = -tangentNormal.y; // flip y to match OpenGL's coordinate system
    //vec3 tangentNormal = texture(normalMap, uvs).xyz;// * 2.0 - 1.0;
    vec3 Q1  = dFdx(fs_in.FragPos);
    vec3 Q2  = dFdy(fs_in.FragPos);


    //vec2 st1 = dFdx(uvs);
    //vec2 st2 = dFdy(uvs);
    vec2 st1 = dFdx(fs_in.TexCoords);
    vec2 st2 = dFdy(fs_in.TexCoords);

//    vec3 dp2perp = cross(Q2, tangentNormal);
//    vec3 dp1perp = cross(tangentNormal, Q1);
//
//    vec3 T = dp2perp * st1.x + dp1perp * st2.x;
//    vec3 B = dp2perp * st1.y + dp1perp * st2.y;
//
//    float invMax = inversesqrt(max(dot(T, T), dot(B, B)));
//    mat3 TBN = mat3(T * invMax, B * invMax, tangentNormal);


//
    vec3 N   = normalize(fs_in.Normal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    //mat3 TBN = mat3(T, N, B);
    //mat3 TBN = mat3(N, T, B);
    //mat3 TBN = mat3(N, B, T);
    //mat3 TBN = mat3(B, T, N);
    //mat3 TBN = mat3(B, N, T);

    return normalize(TBN * tangentNormal);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
// ----------------------------------------------------------------------------
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

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
    vec3 albedo     = texture(albedoMap, uvs).rgb;
    float metallic  = texture(metallicMap, uvs).r;
    float roughness = texture(roughnessMap, uvs).r;
    float ao        = texture(aoMap, uvs).r;
//    float metallic  = texture(metallicMap, fs_in.TexCoords).r;
//    float roughness = texture(roughnessMap, fs_in.TexCoords).r;
//    float ao        = texture(aoMap, fs_in.TexCoords).r;

    vec3 N = getNormalFromMap(uvs);
    //vec3 N = normalize(fs_in.Normal);
    vec3 V = normalize(viewPos - fs_in.FragPos);

    // calculate reflectance at normal incidence; if dia-electric (like plastic) use F0 
    // of 0.04 and if it's a metal, use the albedo color as F0 (metallic workflow)    
    //vec3 F0 = metalness; 
    //vec3 F0 = vec3(1); 
    vec3 F0 = mix(metalness, albedo, metallic);


    // reflectance equation
    vec3 point_light_Lo = vec3(0.0);
    vec3 directional_light_Lo = vec3(0.0);

    // calculate per-light radiance
    vec3 L = normalize(lightPos - fs_in.FragPos);
    vec3 H = normalize(V + L);
    float distance = length(lightPos - fs_in.FragPos);
    //float attenuation = 800.0 / (distance * distance);
    float attenuation = 80.0 / distance;
    vec3 radiance = pointlightColor * attenuation;

    // calculate directional light radiance
    vec3 H_ = normalize(V + lightDir);
    vec3 radiance_ = directionallightColor;




    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);   
    float NDF_ = DistributionGGX(N, H_, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);  
    float G_  = GeometrySmith(N, V, lightDir, roughness);
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 F_   = fresnelSchlick(max(dot(H_,V), 0.0), F0);
           
    vec3 numerator    = NDF * G * F; 
    vec3 numerator_   = NDF_ * G_ * F_;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // + 0.0001 to prevent divide by zero
    float denominator_ = 4.0 * max(dot(N, V), 0.0) * max(dot(N, lightDir), 0.0) + 0.0001; // + 0.0001 to prevent divide by zero
    vec3 specular = numerator / denominator;
    vec3 specular_ = numerator_ / denominator_;
        
    // kS is equal to Fresnel
    vec3 kS = F;
    vec3 kS_ = F_;
    // for energy conservation, the diffuse and specular light can't
    // be above 1.0 (unless the surface emits light); to preserve this
    // relationship the diffuse component (kD) should equal 1.0 - kS.
    vec3 kD = vec3(1.0) - kS;
    vec3 kD_ = vec3(1.0) - kS_;
    // multiply kD by the inverse metalness such that only non-metals 
    // have diffuse lighting, or a linear blend if partly metal (pure metals
    // have no diffuse light).
    kD *= 1.0 - metallic;	
    kD_ *= 1.0 - metallic;

    // scale light by NdotL
    float NdotL = max(dot(N, L), 0.0);     
    float NdotL_ = max(dot(N, lightDir), 0.0);

    // add to outgoing radiance Lo
    point_light_Lo += (kD * albedo / PI + specular) * radiance * NdotL;  // note that we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again
    directional_light_Lo += (kD_ * albedo / PI + specular_) * radiance_ * NdotL_;  // note that we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again


    // ambient lighting (note that the next IBL tutorial will replace 
    // this ambient lighting with environment lighting).
    vec3 ambient = vec3(0.05) * albedo * ao;

     // calculate shadow
    float point_light_shadow = ShadowCalculation(fs_in.FragPos);
    float cascade_shadow = CascadeShadowCalculation(fs_in.FragPos); 
    
    vec3 color = ambient + (1 - point_light_shadow) * point_light_Lo + (1 - cascade_shadow) * directional_light_Lo;
    //vec3 color = ambient + point_light_Lo ;//+ directional_light_Lo;
    // HDR tonemapping
    //color = color / (color + vec3(1.0));

    // gamma correct
    color = pow(color, vec3(1.0/1.8));
    
    FragColor = vec4(color, 1.0);



   /* vec2 uvs = vec2(fs_in.TexCoords.x * u_tiling.x, fs_in.TexCoords.y * u_tiling.y);
    vec3 color = texture(diffuseTexture, uvs).rgb;
    vec3 normal = normalize(fs_in.Normal);
    vec3 ambientlightColor = vec3(0.2);
    // ambient
    vec3 ambient = 0.2 * color;

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
     // HDR tonemapping
    //lighting = lighting / (lighting + vec3(1.0));
    lighting = pow(lighting, vec3(1.0/1.7)); // Convert to linear space
    FragColor = vec4(lighting, 1.0);
//    //o_EntityID = u_entityID;
*/
  
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

