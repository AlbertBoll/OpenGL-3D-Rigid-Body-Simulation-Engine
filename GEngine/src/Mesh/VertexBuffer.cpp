#include "gepch.h"
#include "Mesh/VertexBuffer.h"
#include <limits>
#include <stdexcept>

namespace GEngine::Buffer
{
	namespace
	{
		std::size_t CheckedCapacityBytes(std::size_t capacityBytes)
		{
			if (capacityBytes > static_cast<std::size_t>((std::numeric_limits<GLsizeiptr>::max)()))
				throw std::length_error("VertexBuffer capacity exceeds the GL byte-size limit");
			return capacityBytes;
		}

		std::size_t CheckedDataBytes(std::span<const float> data)
		{
			if (data.size() > static_cast<std::size_t>((std::numeric_limits<GLsizeiptr>::max)()) / sizeof(float))
				throw std::length_error("VertexBuffer payload exceeds the GL byte-size limit");
			return data.size_bytes();
		}
	}

	VertexBuffer::VertexBuffer(std::size_t capacityBytes)
		: m_CapacityBytes(CheckedCapacityBytes(capacityBytes))
	{
		glGenBuffers(1, &m_VertexBufferRef);
		ASSERT(m_VertexBufferRef != 0);
		glBindBuffer(GL_ARRAY_BUFFER, m_VertexBufferRef);
		glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_CapacityBytes), nullptr, GL_DYNAMIC_DRAW);
	}


	VertexBuffer::VertexBuffer(std::span<const float> data)
		: m_CapacityBytes(CheckedDataBytes(data))
	{
		glGenBuffers(1, &m_VertexBufferRef);
		ASSERT(m_VertexBufferRef != 0);
		glBindBuffer(GL_ARRAY_BUFFER, m_VertexBufferRef);
		glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_CapacityBytes),
			data.empty() ? nullptr : data.data(), GL_STATIC_DRAW);
	}


	VertexBuffer::~VertexBuffer()
	{
		glDeleteBuffers(1, &m_VertexBufferRef);
	}


	void VertexBuffer::Bind()
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_VertexBufferRef);

	}

	void VertexBuffer::Unbind()
	{
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}


	void VertexBuffer::SetData(std::span<const float> data, std::size_t offsetBytes)
	{
		// Subtract before comparing to avoid offset + size overflow. Compare elements
		// before multiplying so even an unrepresentable payload is rejected safely.
		if (offsetBytes > m_CapacityBytes || data.size() > (m_CapacityBytes - offsetBytes) / sizeof(float))
			throw std::out_of_range("VertexBuffer upload exceeds allocated byte capacity");
		if (data.empty()) return;

		glBindBuffer(GL_ARRAY_BUFFER, m_VertexBufferRef);
		glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offsetBytes),
			static_cast<GLsizeiptr>(data.size_bytes()), data.data());
	}

}

