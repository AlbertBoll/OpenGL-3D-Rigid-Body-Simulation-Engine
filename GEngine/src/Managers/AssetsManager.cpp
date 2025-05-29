#include "gepch.h"
#include "Managers/AssetsManager.h"
#include "Assets/Textures/TextTexture.h"


namespace GEngine::Manager
{
	static std::string image_base_dir = "../GEngine/include/GEngine/Assets/Images/";
	static std::string image_extension = ".png";

	Asset::Texture* AssetsManager::GetTexture(const std::string& imageFilePath, 
									   const std::string& uniform_name,
									   const std::string& extension, 
									   const Asset::TextureInfo& info)
	{
	/*	if (texture_file.empty())
		{
			return nullptr;
		}*/

		std::string image_dir = !info.b_CubeMap? image_base_dir + imageFilePath + extension: image_base_dir + imageFilePath;

		//std::string image_dir = image_base_dir + imageFilePath + image_extension;

		if (auto it = m_TextureMap.find(image_dir); it != m_TextureMap.end())
		{
			return it->second;
		}

		Asset::Texture* new_Texture = new(std::nothrow) Asset::Texture(image_dir, extension, info);
		ASSERT(new_Texture);

		if (!uniform_name.empty()) new_Texture->SetUniformName(uniform_name);

		m_TextureMap.emplace(imageFilePath, new_Texture);

		return new_Texture;



	}

	Asset::Texture* AssetsManager::GetCascadedFrameBufferTexture(const CascadeShadowFrameBuffer& fb, const std::string& uniform_name)
	{
		if (auto it = m_FrameBufferTextures.find(FrameBufferMapType::CascadedShadowMap); it != m_FrameBufferTextures.end())
		{
			return it->second;
		}

		Asset::TextureInfo info;
		info.m_TextureSpec.m_TexTarget = GL_TEXTURE_2D_ARRAY;
		auto* new_texture = new(std::nothrow) Asset::Texture(fb.GetLightDepthMaps(), info, uniform_name);
		ASSERT(new_texture);
		m_FrameBufferTextures.emplace(FrameBufferMapType::CascadedShadowMap, new_texture);
		return new_texture;
	}

	Asset::Texture* AssetsManager::GetTextTexture(const std::string& str,
										   const std::string& font_file, 
										   int pointSize, 
										   const glm::vec3& font_color, 
										   const std::string& uniform_name)
	{
		if (auto it = m_TextureMap.find(font_file); it != m_TextureMap.end())
		{
			return it->second;
		}

		auto* new_texture = new(std::nothrow) Asset::TextTexture(str, font_file, pointSize, font_color);
		ASSERT(new_texture);

		if (!uniform_name.empty()) new_texture->SetUniformName(uniform_name);
		m_TextureMap.emplace(font_file, new_texture);

		return new_texture;

	}




	Asset::Font* AssetsManager::GetFont(const std::string& font_file)
	{
		if (auto it = m_FontMap.find(font_file); it != m_FontMap.end())
		{
			return  it->second;
		}

		auto* new_font = new(std::nothrow) Asset::Font;

		ASSERT(new_font);
		new_font->LoadFont(font_file);

		m_FontMap.emplace(font_file,  new_font);
		return new_font;

	}

	void AssetsManager::FreeTextureResource()
	{
		
		for (auto& ele : m_TextureMap)
		{
			if(ele.second) delete ele.second;
		}
		
	}

	void AssetsManager::FreeFontResource()
	{
		for (auto& ele : m_FontMap)
		{
			if(ele.second) delete ele.second;
		}
	}

	void AssetsManager::FreeAllResources()
	{
		FreeFontResource();
		FreeTextureResource();
	}
}
