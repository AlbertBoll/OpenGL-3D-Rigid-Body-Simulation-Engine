#pragma once
#include <cstdint>
#include <expected>
#include <string_view>

namespace GEngine
{
	enum class ImageFormat
	{
		None = 0,
		RGBA,
		RGBA32F
	};

    enum class ImageErrorCode
    {
        InvalidExtent, CameraMismatch, InvalidFormat, InvalidData,
        Allocation, Context, Backend, Unavailable
    };
    struct ImageError
    {
        ImageErrorCode code;
        std::string_view operation;
        std::string_view message;
        std::uint32_t width{}, height{};
        ImageFormat format = ImageFormat::None;
        std::uint32_t sourceWidth{}, sourceHeight{};
        std::uint64_t expectedElements{}, actualElements{};
        std::uint32_t backendCode{};
    };
    using ImageResult = std::expected<void, ImageError>;
}
