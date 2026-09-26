#pragma once
#include "Core/RuntimeAssets.h"
#include "Material.h"
#include <variant>

namespace GEngine
{
	class AAScreenMaterial: public Material
	{
    public:
        using CreationError = std::variant<Asset::ShaderError, Asset::SamplingError>;
        using CreationResult = std::expected<std::shared_ptr<AAScreenMaterial>, CreationError>;
        [[nodiscard]] static CreationResult Create(const Asset::TextureView& screen,
            const std::string& vertexFileName = "Shaders/aa_post.vert",
            const std::string& fragFileName = "Shaders/aa_post.frag");
        void UpdateRenderSettings() override;
    private:
        AAScreenMaterial(Construction&, const std::string& vertexFileName, const std::string& fragFileName);
        // Retained native compatibility implementation, unavailable to normal consumers.
		AAScreenMaterial(Construction& construction, unsigned int screenTextureID, const std::string& vertexFileName = "Shaders/aa_post.vert",
			             const std::string& fragFileName = "Shaders/aa_post.frag");

	};

}