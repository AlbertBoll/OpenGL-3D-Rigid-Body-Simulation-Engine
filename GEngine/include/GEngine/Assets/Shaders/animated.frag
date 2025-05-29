#version 460 core

out vec4 FragColor;
const int maxLightNumber = 6;

in VS_OUT
{
	vec3 FragPos;
	vec2 TexCoords;
	vec3 TangentLightPos[maxLightNumber];
    vec3 TangentViewPos;
    vec3 TangentFragPos;
} fs_in;


//in vec3 toLightVector[maxLightNumber];
//in vec3 toCameraVector;
in float visibility;

uniform sampler2D u_diffuseTexture;
uniform sampler2D u_normalTexture;

uniform vec3      u_lightColor[maxLightNumber];
uniform vec3      u_lightAttuentation[maxLightNumber];

uniform float     u_shineDamper;
uniform float     u_reflectivity;
uniform vec3      u_skyColor;
uniform bool      u_blinn;

void main(void)
{
    vec3 unitNormal = texture(u_normalTexture, fs_in.TexCoords).rgb;
    unitNormal = normalize(unitNormal * 2.0f - 1.0f);
    
    vec3 viewDir = normalize(fs_in.TangentViewPos - fs_in.TangentFragPos);

    vec3 totalDiffuse = vec3(0.0f);
    vec3 totalSpecular = vec3(0.0f);

    vec3 lightDir;

    for(int i = 0; i < maxLightNumber; i++)
    {
        lightDir = fs_in.TangentLightPos[i] - fs_in.TangentFragPos;
        float distance =  length(lightDir);

        float attenuationFac = u_lightAttuentation[i].x + u_lightAttuentation[i].y * distance + u_lightAttuentation[i].z * distance * distance; 

        lightDir = normalize(lightDir);
        float nDotl = dot(unitNormal, lightDir);
        float brightness = max(nDotl, 0.f);
        totalDiffuse += brightness * u_lightColor[i]/attenuationFac;
        
        float specularFac;

        if(u_blinn)
        {
            vec3 halfwayDir = normalize(lightDir + viewDir);  
            specularFac = max(dot(unitNormal, halfwayDir), 0.f);
        }
       
        else
        {
            vec3 reflectedLightVector = reflect(-lightDir, unitNormal);
            specularFac = max(dot(reflectedLightVector, viewDir), 0.f);
        }
       

        //float specularFac = mix(phong_specularFac, blinn_specularFac, u_blinn);
        //vec3 reflectedLightVector = reflect(-lightDir, unitNormal);
        //float specularFac = max(dot(reflectedLightVector, viewDir), 0.f);
        totalSpecular += pow(specularFac, u_shineDamper) * u_reflectivity * u_lightColor[i]/attenuationFac;

    }

    totalDiffuse = max(totalDiffuse, 0.2f);

    vec4 textureColor = texture(u_diffuseTexture, fs_in.TexCoords);

    if(textureColor.a < 0.5)
    {
        discard;
    }

    FragColor = textureColor * vec4(totalDiffuse, 1.0f) + vec4(totalSpecular, 1.0f);

    FragColor = mix(vec4(u_skyColor, 1.f), FragColor, visibility);

}