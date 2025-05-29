#pragma once
#include <unordered_map>
#include <Assets/Textures/Texture.h>
#include <Assets/Fonts/Font.h>
#include <Core/RenderTarget.h>




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
		CascadedShadowMap
	};

	class AssetsManager
	{
		

		typedef std::unordered_map<std::string, Asset::Texture*> TextureHashMap;
		typedef std::unordered_map<FrameBufferMapType, Asset::Texture*, EnumClassHash> FrameBufferTextures;
		typedef std::unordered_map<std::string, Asset::Font*> FontHashMap;

	public:
		static Asset::Texture* GetTexture(const std::string& texture_name="",
								   const std::string& uniform_name = "u_texture",
								   const std::string& extension = ".png", 
								   const Asset::TextureInfo& info = Asset::TextureInfo{});
		static Asset::Texture* GetCascadedFrameBufferTexture(const CascadeShadowFrameBuffer& fb, const std::string& uniform_name = "");

		static Asset::Texture* GetTextTexture(const std::string& str,
									   const std::string& font_file = "../Assets/Fonts/Carlito-Regular.ttf",
									   int pointSize = 24,
									   const glm::vec3& font_color = { 0.0f, 0.0f, 1.0f },
									   const std::string& uniform_name = "");


		static Asset::Font* GetFont(const std::string& font_file);

		static void FreeTextureResource();

		static void FreeFontResource();

		static void FreeAllResources();
	
	


	private:
		inline static TextureHashMap m_TextureMap;
		inline static FontHashMap m_FontMap;
		inline static FrameBufferTextures m_FrameBufferTextures;
	};
	

}
