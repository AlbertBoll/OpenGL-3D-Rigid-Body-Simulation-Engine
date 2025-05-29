#pragma once
#include <string>

//using namespace GEngine;
//class Window;

struct SDL_Surface;

namespace GEngine
{
	class Window;

	namespace Asset
	{

		enum class TextureTarget
		{
			TEXTURE2D,
			TEXTURE2D_HDR,
			TEXTURE3D,
			TEXTURE3D_HDR
		};

		struct TextureDataFormat
		{
			unsigned int m_InternalFormat = 0x8058;
			unsigned int m_DataFormat = 0x1908;
			unsigned int m_DataType = 0x1401;
		};


		struct TextureParam
		{
			int m_TexTarget = 0x0DE1;
			int m_MagFilter = 0x2601;
			int m_MinFilter = 0x2703;
			int m_WrapS = 0x2901;
			int m_WrapT = 0x2901;
			int m_WrapR = 0x2901;



		};

		struct TextureInfo
		{
			TextureParam m_TextureSpec = {};
			TextureDataFormat m_TextureFormat{};
			int m_Width{ 0 };
			int m_Height{ 0 };
			int m_Channels{ 4 };
			bool b_CubeMap{ false };
			bool b_HDR{ false };
			bool b_GenerateMipmap{ false };
			bool b_GammaCorrection{ false };



		};

		class Texture
		{
		public:
			NONCOPYMOVABLE(Texture);
			Texture(unsigned int id, const TextureInfo& texInfo, const std::string& uniformName = "");
			Texture() = default;
			Texture(const TextureInfo& texInfo, const std::string& uniformName = "");
			Texture(const std::string& fileName, const std::string& extension = ".png", const TextureInfo& info = TextureInfo{});
			void LoadTexture(const std::string& fileName);

			void LoadHdrTexture(const std::string& fileName);
			void LoadCubeMap(const std::string& baseName, const std::string& extension);
			void LoadHdrCubeMap(const std::string& baseName);

			void SetTextureInfo(const TextureInfo& texInfo) { m_TextureInfo = texInfo; }

			void SetTextureID(unsigned int id) { m_TexID = id; }
			void Bind() const;
			std::string& GetUniformName() { return m_TexUniformName; }
			[[nodiscard]] std::string GetUniformName() const { return m_TexUniformName; }
			[[nodiscard]] unsigned int GetTextureID()const { return m_TexID; }
			[[nodiscard]] TextureInfo GetTextureInfo()const { return m_TextureInfo; }
			TextureInfo& GetTextureInfo(){ return m_TextureInfo; }
			Texture* SetUniformName(const std::string& name) { m_TexUniformName = name; return this; }

			void CreateFromSurface(SDL_Surface* surface);

			void CreateForRendering(const TextureInfo& texInfo);

			//std::string GetUniformName()const { return m_TexUniformName; }

		private:
			void WritePixels(Window* focusWindow, const std::string& fileName, int channels);
			unsigned char* LoadPixels(const std::string& fileName, int& width, int& height, bool flip = true);
			void DeletePixels(unsigned char*);

		private:
			TextureInfo m_TextureInfo{};
			std::string m_TexUniformName;
			unsigned int m_TexID{};

		};

	}

}