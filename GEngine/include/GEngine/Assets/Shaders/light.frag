#version 460 core

in vec2 in_uv;

const int maxLightNumber = 6;

in vec3 surfaceNormal;
in vec3 toLightVector[maxLightNumber];
in vec3 toCameraVector;
in float visibility;

out vec4 out_Color;

uniform sampler2D u_texture;

uniform vec3      u_lightColor[maxLightNumber];
uniform vec3      u_lightAttuentation[maxLightNumber];

uniform float     u_shineDamper;
uniform float     u_reflectivity;
uniform vec3      u_skyColor;
uniform bool      u_blinn;

void main(void)
{
    vec3 unitNormal = normalize(surfaceNormal);
    vec3 unitVectorToCamera = normalize(toCameraVector);

    vec3 totalDiffuse = vec3(0.0f);
    vec3 totalSpecular = vec3(0.0f);


    for(int i = 0; i < maxLightNumber; i++)
    {
        float distance =  length(toLightVector[i]);

        float attenuationFac = u_lightAttuentation[i].x + u_lightAttuentation[i].y * distance + u_lightAttuentation[i].z * distance * distance; 

        vec3 unitLightVector = normalize(toLightVector[i]);
        float nDotl = dot(unitNormal, unitLightVector);
        float brightness = max(nDotl, 0.f);
        totalDiffuse += brightness * u_lightColor[i]/attenuationFac;
        //vec3 reflectedLightVector = reflect(-unitLightVector, unitNormal);

        float specularFac;

        if(u_blinn)
        {
            vec3 halfwayDir = normalize(unitLightVector + unitVectorToCamera);  
            specularFac = max(dot(unitNormal, halfwayDir), 0.f);
        }
       
        else
        {
            vec3 reflectedLightVector = reflect(-unitLightVector, unitNormal);
            specularFac = max(dot(reflectedLightVector, unitVectorToCamera), 0.f);
        }

        //float specularFac = max(dot(reflectedLightVector, unitVectorToCamera), 0.f);
        totalSpecular += pow(specularFac, u_shineDamper) * u_reflectivity * u_lightColor[i]/attenuationFac;

    }

    totalDiffuse = max(totalDiffuse, 0.2f);

    vec4 textureColor = texture(u_texture, in_uv);

    if(textureColor.a < 0.5)
    {
        discard;
    }

    out_Color = textureColor * vec4(totalDiffuse, 1.0f) + vec4(totalSpecular, 1.0f);

    out_Color = mix(vec4(u_skyColor, 1.f), out_Color, visibility);

}