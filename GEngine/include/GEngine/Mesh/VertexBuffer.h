#pragma once
#include"BufferLayout.h"
#include <cstddef>
#include <span>

namespace GEngine::Buffer
{

	class VertexBuffer
	{

	public:

		NONCOPYABLE(VertexBuffer);
		VertexBuffer(VertexBuffer&& other) noexcept;
		VertexBuffer& operator=(VertexBuffer&& other) noexcept;
		// Allocation sizes and upload offsets are bytes; payload sizes come from the span.
		explicit VertexBuffer(std::size_t capacityBytes);
		explicit VertexBuffer(std::span<const float> data);

		~VertexBuffer();

		void Bind();
		void Unbind();

		const BufferLayout& GetBufferLayout() const { return m_BufferLayout; }

		void SetLayout(const BufferLayout& layout)
		{
			m_BufferLayout = layout;
		}

		// Throws std::out_of_range before GL work when the upload exceeds capacity.
		// Empty input is a no-op at any offset through capacity (inclusive).
		void SetData(std::span<const float> data, std::size_t offsetBytes = 0);


	private:
		unsigned int m_VertexBufferRef{};
		std::size_t m_CapacityBytes{};
		BufferLayout m_BufferLayout;
	};

}
