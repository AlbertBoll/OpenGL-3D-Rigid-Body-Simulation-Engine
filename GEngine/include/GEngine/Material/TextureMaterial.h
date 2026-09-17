#pragma once
#include "Core/RuntimeAssets.h"
#include "Material.h"



namespace GEngine
{
	namespace Asset
	{
		class Texture;
	}

	using namespace Math;
	class TextureMaterial : public Material
	{
		

	public:
		TextureMaterial(Construction& construction, const Asset::Texture& texture, const std::string& vertexFileName = "Shaders/texture.vert",
			const std::string& fragFileName =  "Shaders/texture.frag");

		void UpdateRenderSettings() override;

	};


}
