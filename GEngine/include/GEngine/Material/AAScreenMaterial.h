#pragma once
#include "Core/RuntimeAssets.h"
#include "Material.h"

namespace GEngine
{
	class AAScreenMaterial: public Material
	{
	public:
		AAScreenMaterial(unsigned int screenTextureID, const std::string& vertexFileName = RuntimeAssets::File("Shaders/aa_post.vert"),
			             const std::string& fragFileName = RuntimeAssets::File("Shaders/aa_post.frag"));

		void UpdateRenderSettings() override;
	};

}