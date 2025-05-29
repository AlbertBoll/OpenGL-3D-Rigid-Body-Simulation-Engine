#pragma once
#include <string_view>
namespace GEngine
{
	enum class ImageFormat
	{
		None = 0,
		RGBA,
		RGBA32F
	};

	class Image
	{
	public:
		Image(std::string_view path);
		Image(uint32_t width, uint32_t height, ImageFormat format, const void* data = nullptr);
		~Image();
		void ReAllocateData(const void* data);
		void SetData(const void* data);
		void UpdateData(const void* data);
		void Resize(uint32_t width, uint32_t height);

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		uint32_t GetTexID()const { return m_TexID; }

	private:
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		ImageFormat m_Format = ImageFormat::None;
		uint32_t m_TexID{};
	};

}
