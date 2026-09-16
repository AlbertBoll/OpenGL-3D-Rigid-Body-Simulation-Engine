#pragma once
#include "Core/RuntimeAssets.h"
#include <unordered_map>
#include <memory>
#include <map>
#include <tuple>
#include <Assets/Textures/Texture.h>
#include <Assets/Fonts/Font.h>
#include <Core/RenderTarget.h>




namespace GEngine { class EngineContext; }

namespace GEngine::Manager
{

	enum class ImageFormat
	{
		PNG,
		JPG,
		JPEG
	};

	struct EnumClassHash
	{
		template <typename T>
		std::size_t operator()(T t) const
		{
			return static_cast<std::size_t>(t);
		}
	};

	enum class FrameBufferMapType
	{
		CascadedShadowMap,
		PointShadowMap
	};

	class AssetsManager
	{
		

		using TextureOwner = std::unique_ptr<Asset::Texture, void(*)(Asset::Texture*)>;
		using TextKey = std::tuple<std::string, std::string, int, float, float, float, std::string>;

	public:
        ~AssetsManager();
        NONCOPYMOVABLE(AssetsManager);
        // Returned pointers borrow this EngineContext's lifetime. Only the root
        // can create/retire the manager; failed loads never publish cache entries.
		static Asset::Texture* GetTexture(const std::string& texture_name="",
								   const std::string& uniform_name = "u_texture",
								   const std::string& extension = ".png", 
								   const Asset::TextureInfo& info = Asset::TextureInfo{});
		static Asset::Texture* GetCascadedFrameBufferTexture(const CascadeShadowFrameBuffer& fb, const std::string& uniform_name = "");
		static Asset::Texture* GetPointShadowFrameBufferTexture(const PointShadowFrameBuffer& fb, const std::string& uniform_name = "");
		static Asset::Texture* GetTextTexture(const std::string& str,
									   const std::string& font_file = RuntimeAssets::File("Fonts/Carlito-Regular.ttf"),
									   int pointSize = 24,
									   const glm::vec3& font_color = { 0.0f, 0.0f, 1.0f },
									   const std::string& uniform_name = "");


		static Asset::Font* GetFont(const std::string& font_file);

	private:
        friend class ::GEngine::EngineContext;
        AssetsManager() = default;
        static AssetsManager& Current();
        std::unordered_map<std::string, TextureOwner> m_TextureMap;
        std::map<TextKey, TextureOwner> m_TextTextures;
        std::unordered_map<std::string, std::unique_ptr<Asset::Font>> m_FontMap;
        // Each call returns a stable wrapper snapshot. Names are borrowed data,
        // never cache identities; the framebuffer must outlive its use.
        std::vector<std::unique_ptr<Asset::Texture>> m_FrameBufferTextures;
	};
	

}
