#pragma once
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>
#include "Core/Base.h"
#include "BufferLayout.h"

namespace GEngine { class GpuMesh; }

namespace GEngine::Buffer
{
    enum class VertexBufferErrorCode { CapacityTooLarge, PayloadTooLarge, UploadOutOfRange, Allocation, Driver };
    struct VertexBufferError
    {
        VertexBufferErrorCode code;
        std::string operation;
        std::string message;
        std::size_t capacityBytes{};
        std::size_t offsetBytes{};
        std::size_t elementCount{};
    };
    using VertexBufferResult = std::expected<void, VertexBufferError>;


	class VertexBuffer
	{

	public:

		NONCOPYABLE(VertexBuffer);
		VertexBuffer(VertexBuffer&& other) noexcept;
		VertexBuffer& operator=(VertexBuffer&& other) noexcept;
		// Allocation sizes and upload offsets are bytes; payload sizes come from the span.
		[[nodiscard]] static std::expected<VertexBuffer, VertexBufferError> Create(std::size_t capacityBytes);
		[[nodiscard]] static std::expected<VertexBuffer, VertexBufferError> Create(std::span<const float> data);

		~VertexBuffer();

		void Bind();
		void Unbind();

		const BufferLayout& GetBufferLayout() const { return m_BufferLayout; }

		void SetLayout(const BufferLayout& layout)
		{
			m_BufferLayout = layout;
		}

		// Rejects out-of-range input before GL work; successful uploads retain binding.
		// Empty input is a no-op at any offset through capacity (inclusive).
		[[nodiscard]] VertexBufferResult SetData(std::span<const float> data, std::size_t offsetBytes = 0);


	private:
		friend class ::GEngine::GpuMesh;
		// Backend-only empty owner; GpuMesh performs typed creation and upload.
        VertexBuffer() = default;
        static std::expected<VertexBuffer, VertexBufferError> CreateStorage(
            std::size_t capacityBytes, std::span<const float> data, bool dynamic);
		unsigned int m_VertexBufferRef{};
		std::size_t m_CapacityBytes{};
		BufferLayout m_BufferLayout;
	};

}
