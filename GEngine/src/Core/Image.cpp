#include "gepch.h"
#include "Core/Image.h"
#include <stb_image/stb_image.h>

namespace GEngine
{

	namespace Utils
	{
		static uint32_t BytesPerPixel(ImageFormat format)
		{
			switch (format)
			{
			case ImageFormat::RGBA:    return 4;
			case ImageFormat::RGBA32F: return 16;
			}
			return 0;
		}

	}

	Image::Image(std::string_view path)
	{

	}

	Image::Image(uint32_t width, uint32_t height, ImageFormat format, const void* data):
		m_Width(width), m_Height(height), m_Format(format)
	{
		if (data)
		{
			SetData(data);
		}
	}

	Image::~Image()
	{
		GENGINE_CORE_INFO("Image destructor was called");
		glDeleteTextures(1, &m_TexID);
	}

	void Image::ReAllocateData(const void* data)
	{
		SetData(data);
	}

	void Image::SetData(const void* data)
	{
		if (m_TexID == 0)
		{
			glGenTextures(1, &m_TexID);
			glBindTexture(GL_TEXTURE_2D, m_TexID);
		}

		GLenum internal_format{};
		GLenum format{};

		switch (m_Format)
		{
		case ImageFormat::RGBA:
		{

			internal_format = GL_RGBA8;
			format = GL_RGBA;
			break;
		}

		case ImageFormat::RGBA32F:
		{
			internal_format = GL_RGBA32F;
			format = GL_RGBA;
			break;
		}
		}

		glTexImage2D(GL_TEXTURE_2D, 0, internal_format, m_Width, m_Height, 0, format, GL_UNSIGNED_BYTE, data);
		//glGenerateMipmap(GL_TEXTURE_2D);
		//glTexSubImage2D
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		//check if hardware support anisotropic filtering
		//if (GLAD_GL_EXT_texture_filter_anisotropic)
		//{
		//	GLfloat largest;
		//	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largest);

		//	//activate anisotropic filtering
		//	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, largest);
		//}



			//stbi_image_free((unsigned char*)data);
		//}
	}

	void Image::UpdateData(const void* data)
	{
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height, GL_RGBA, GL_UNSIGNED_BYTE, data);
	}

	void Image::Resize(uint32_t width, uint32_t height)
	{
		/*if (m_Width == width && m_Height == height)
			return;*/
		
		m_Width = width;
		m_Height = height;



	}
};
