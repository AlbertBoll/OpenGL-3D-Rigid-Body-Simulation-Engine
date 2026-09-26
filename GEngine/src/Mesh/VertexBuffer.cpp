#include "gepch.h"
#include "Mesh/VertexBuffer.h"
#include <limits>
#include <string_view>
#include <utility>

namespace GEngine::Buffer
{
    namespace
    {
        VertexBufferResult DriverStatus(std::string_view operation, std::size_t capacity,
            std::size_t offset = 0, std::size_t elements = 0)
        {
            VertexBufferError error{VertexBufferErrorCode::Driver, std::string(operation), {}, capacity, offset, elements};
            for (auto code = glGetError(); code != GL_NO_ERROR; code = glGetError())
            {
                if (!error.message.empty()) error.message += "; ";
                error.message += "OpenGL error " + std::to_string(code);
            }
            if (!error.message.empty()) return std::unexpected(std::move(error));
            return {};
        }
    }

    std::expected<VertexBuffer, VertexBufferError> VertexBuffer::Create(std::size_t capacityBytes)
    {
        if (capacityBytes > static_cast<std::size_t>((std::numeric_limits<GLsizeiptr>::max)()))
            return std::unexpected(VertexBufferError{VertexBufferErrorCode::CapacityTooLarge,
                "create capacity", "VertexBuffer capacity exceeds the GL byte-size limit", capacityBytes});
        return CreateStorage(capacityBytes, {}, true);
    }

    std::expected<VertexBuffer, VertexBufferError> VertexBuffer::Create(std::span<const float> data)
    {
        if (data.size() > static_cast<std::size_t>((std::numeric_limits<GLsizeiptr>::max)()) / sizeof(float))
            return std::unexpected(VertexBufferError{VertexBufferErrorCode::PayloadTooLarge,
                "create payload", "VertexBuffer payload exceeds the GL byte-size limit", 0, 0, data.size()});
        return CreateStorage(data.size_bytes(), data, false);
    }

    std::expected<VertexBuffer, VertexBufferError> VertexBuffer::CreateStorage(
        std::size_t capacityBytes, std::span<const float> data, bool dynamic)
    {
        if (auto prior = DriverStatus("before vertex buffer creation", capacityBytes, 0, data.size()); !prior)
            return std::unexpected(prior.error());
        GLint previous = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
        VertexBuffer buffer;
        glGenBuffers(1, &buffer.m_VertexBufferRef);
        if (auto generated = DriverStatus("generate vertex buffer", capacityBytes, 0, data.size()); !generated)
            return std::unexpected(generated.error());
        if (!buffer.m_VertexBufferRef)
            return std::unexpected(VertexBufferError{VertexBufferErrorCode::Allocation,
                "generate vertex buffer", "Driver did not allocate a vertex buffer", capacityBytes, 0, data.size()});
        glBindBuffer(GL_ARRAY_BUFFER, buffer.m_VertexBufferRef);
        if (auto bound = DriverStatus("bind vertex buffer", capacityBytes, 0, data.size()); !bound)
        {
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previous));
            return std::unexpected(bound.error());
        }
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacityBytes),
            data.empty() ? nullptr : data.data(), dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        GLint64 actualBytes = -1;
        glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &actualBytes);
        auto storage = DriverStatus("allocate vertex buffer", capacityBytes, 0, data.size());
        if (!storage || actualBytes != static_cast<GLint64>(capacityBytes))
        {
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previous));
            if (!storage) return std::unexpected(storage.error());
            return std::unexpected(VertexBufferError{VertexBufferErrorCode::Driver,
                "allocate vertex buffer", "Driver storage size " + std::to_string(actualBytes) +
                " differs from requested " + std::to_string(capacityBytes), capacityBytes, 0, data.size()});
        }
        buffer.m_CapacityBytes = capacityBytes;
        return buffer;
    }

	VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept
		: m_VertexBufferRef(std::exchange(other.m_VertexBufferRef, 0)),
		  m_CapacityBytes(std::exchange(other.m_CapacityBytes, 0)),
		  m_BufferLayout(std::move(other.m_BufferLayout))
	{
	}

	VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other) noexcept
	{
		if (this != &other)
		{
			if (m_VertexBufferRef != 0)
				glDeleteBuffers(1, &m_VertexBufferRef);
			m_VertexBufferRef = std::exchange(other.m_VertexBufferRef, 0);
			m_CapacityBytes = std::exchange(other.m_CapacityBytes, 0);
			m_BufferLayout = std::move(other.m_BufferLayout);
		}
		return *this;
	}

	VertexBuffer::~VertexBuffer()
	{
		if (m_VertexBufferRef != 0)
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


	VertexBufferResult VertexBuffer::SetData(std::span<const float> data, std::size_t offsetBytes)
	{
		// Subtract before comparing to avoid offset + size overflow. Compare elements
		// before multiplying so even an unrepresentable payload is rejected safely.
		if (offsetBytes > m_CapacityBytes || data.size() > (m_CapacityBytes - offsetBytes) / sizeof(float))
            return std::unexpected(VertexBufferError{VertexBufferErrorCode::UploadOutOfRange,
                "upload vertex buffer", "VertexBuffer upload exceeds allocated byte capacity",
                m_CapacityBytes, offsetBytes, data.size()});
        if (data.empty()) return {};
        if (auto prior = DriverStatus("before vertex buffer upload", m_CapacityBytes, offsetBytes, data.size()); !prior)
            return prior;
        GLint previous = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
        glBindBuffer(GL_ARRAY_BUFFER, m_VertexBufferRef);
        if (auto bound = DriverStatus("bind vertex buffer for upload", m_CapacityBytes, offsetBytes, data.size()); !bound)
        {
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previous));
            return bound;
        }
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offsetBytes),
            static_cast<GLsizeiptr>(data.size_bytes()), data.data());
        auto uploaded = DriverStatus("upload vertex buffer", m_CapacityBytes, offsetBytes, data.size());
        if (!uploaded) glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previous));
        return uploaded;
	}

}

