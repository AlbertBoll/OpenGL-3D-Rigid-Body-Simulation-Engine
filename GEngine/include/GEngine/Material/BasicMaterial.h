#pragma once
#include "Core/RuntimeAssets.h"
#include "Material.h"

namespace GEngine
{
    using namespace Math;
    class BasicMaterial: public Material
    {
    public:
        BasicMaterial(const std::string& vertexFileName = RuntimeAssets::File("Shaders/basic.vert"),
                        const std::string& fragFileName = RuntimeAssets::File("Shaders/basic.frag")): Material(vertexFileName, fragFileName)
        {
            //UseProgram();
            //SetUniforms<Vec4f>({ {"uBaseColor", {1.0f, 1.0f, 1.0f, 1.0f}} });
            //SetUniforms<bool>({ {"uUseVertexColor", false} });
        }
    };

}