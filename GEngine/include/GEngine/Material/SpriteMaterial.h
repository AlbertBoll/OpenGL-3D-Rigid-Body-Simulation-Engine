#pragma once
#include "Material.h"
#include <Component/Component.h>

namespace GEngine
{

	namespace Asset
	{
		class Texture;
	}

	class SpriteMaterial: public Material
	{
	public:
		SpriteMaterial(Asset::Texture* textures, const std::string& shaderName = "sprite");

		void UpdateRenderSettings();
		

	};

}