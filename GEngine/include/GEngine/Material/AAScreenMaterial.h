#pragma once
#include "Core/RuntimeAssets.h"
#include "Material.h"

namespace GEngine
{
	class AAScreenMaterial: public Material
	{
	public:
		AAScreenMaterial(Construction& construction, unsigned int screenTextureID, const std::string& vertexFileName = "Shaders/aa_post.vert",
			             const std::string& fragFileName = "Shaders/aa_post.frag");

		void UpdateRenderSettings() override;
	};

}