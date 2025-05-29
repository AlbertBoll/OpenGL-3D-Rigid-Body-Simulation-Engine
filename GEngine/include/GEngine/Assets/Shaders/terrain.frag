#version 460 core

in vec2 pass_textureCoordinates;

const int maxLightNumber = 6;
in vec3 surfaceNormal;


in vec3 toLightVector[maxLightNumber];
in vec3 toCameraVector;
in float visibility;

out vec4 out_Color;

uniform sampler2D u_backgroundTexture;
uniform sampler2D u_rTexture;
uniform sampler2D u_gTexture;
uniform sampler2D u_bTexture;
uniform sampler2D u_blendMap;


uniform float u_shineDamper;
uniform float u_reflectivity;
uniform vec3  u_skyColor;
uniform vec3  u_lightColor[maxLightNumber];
uniform vec3  u_lightAttuentation[maxLightNumber];
uniform bool  u_blinn;

void main(void){

    vec3 totalDiffuse = vec3(0.0f);
    vec3 totalSpecular = vec3(0.0f);

    vec4 blendMapColor = texture(u_blendMap, pass_textureCoordinates);
    float backTextureAmount = 1 - (blendMapColor.r + blendMapColor.g + blendMapColor.b);
    vec2 tiledCoords = pass_textureCoordinates * 80.f;
    vec4 backgroundTextureColor = texture(u_backgroundTexture, tiledCoords) * backTextureAmount;
    vec4 rTextureColor = texture(u_rTexture, tiledCoords) * blendMapColor.r;
    vec4 gTextureColor = texture(u_gTexture, tiledCoords) * blendMapColor.g;
    vec4 bTextureColor = texture(u_bTexture, tiledCoords) * blendMapColor.b;

    vec4 totalColor = backgroundTextureColor + rTextureColor + gTextureColor + bTextureColor;
    vec3 unitNormal = normalize(surfaceNormal);

    for(int i = 0; i < maxLightNumber; i++)
    {

        float distance =  length(toLightVector[i]);
        float attenuationFac = u_lightAttuentation[i].x + u_lightAttuentation[i].y * distance + u_lightAttuentation[i].z * distance * distance; 

        vec3 unitLightVector = normalize(toLightVector[i]);

        float nDotl = dot(unitNormal,unitLightVector);
        float brightness = max(nDotl, 0.0f);
        totalDiffuse += brightness * u_lightColor[i]/attenuationFac;

        vec3 unitVectorToCamera = normalize(toCameraVector);
        vec3 lightDirection = -unitLightVector;
        vec3 reflectedLightDirection = reflect(lightDirection,unitNormal);

        //float specularFactor = dot(reflectedLightDirection, unitVectorToCamera);

        float specularFactor;

        if(u_blinn)
        {
            vec3 halfwayDir = normalize(unitLightVector + unitVectorToCamera);  
            specularFactor = max(dot(unitNormal, halfwayDir), 0.f);
        }
       
        else
        {
            vec3 reflectedLightVector = reflect(-unitLightVector, unitNormal);
            specularFactor = max(dot(reflectedLightVector, unitVectorToCamera), 0.f);
        }


        //specularFactor = max(specularFactor, 0.0);
        float dampedFactor = pow(specularFactor, u_shineDamper);
        totalSpecular += dampedFactor * u_reflectivity * u_lightColor[i]/attenuationFac;

    }

    totalDiffuse = max(totalDiffuse, 0.2f);


    out_Color =  vec4(totalDiffuse, 1.0) * totalColor + vec4(totalSpecular, 1.0);
    out_Color = mix(vec4(u_skyColor, 1.0), out_Color, visibility);
}