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
		TextureMaterial(const Asset::Texture& texture, const std::string& vertexFileName = RuntimeAssets::File("Shaders/texture.vert"),
			const std::string& fragFileName =  RuntimeAssets::File("Shaders/texture.frag"));

		void UpdateRenderSettings() override;

	};


}
