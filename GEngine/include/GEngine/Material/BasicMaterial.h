#pragma once
#include "Core/RuntimeAssets.h"
#include "Material.h"

namespace GEngine
{
    using namespace Math;
    class BasicMaterial: public Material
    {
    public:
        BasicMaterial(Construction& construction, const std::string& vertexFileName = "Shaders/basic.vert",
                        const std::string& fragFileName = "Shaders/basic.frag"): Material(construction, vertexFileName, fragFileName)
        {
        if (!construction) return;
            //UseProgram();
            //SetUniforms<Vec4f>({ {"uBaseColor", {1.0f, 1.0f, 1.0f, 1.0f}} });
            //SetUniforms<bool>({ {"uUseVertexColor", false} });
        }
    };

}