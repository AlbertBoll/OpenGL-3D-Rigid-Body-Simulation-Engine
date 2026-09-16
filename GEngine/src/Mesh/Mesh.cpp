#include "gepch.h"

#include <Mesh/IndexBuffer.h>
#include <Mesh/VertexBuffer.h>
#include "Mesh/Mesh.h"
#include <utility>

namespace GEngine//::Buffer
{
	using namespace Buffer;
	static GLenum FromAttributeDataTypeToGLType(AttributeType type)
	{
		switch (type)
		{
		case AttributeType::Bool:   return GL_BOOL;
		case AttributeType::Float:  return GL_FLOAT;
		case AttributeType::Vec2f:  return GL_FLOAT;
		case AttributeType::Vec3f:  return GL_FLOAT;
		case AttributeType::Vec4f:  return GL_FLOAT;
		case AttributeType::Int:    return GL_INT;
		case AttributeType::Vec2i:  return GL_INT;
		case AttributeType::Vec3i:  return GL_INT;
		case AttributeType::Vec4i:  return GL_INT;
		case AttributeType::Mat2:   return GL_FLOAT;
		case AttributeType::Mat3:   return GL_FLOAT;
		case AttributeType::Mat4:   return GL_FLOAT;
		}

		ASSERT(false, "Unknown attribute type");
		return 0;
	}

	Mesh::Mesh()
	{
		glGenVertexArrays(1, &m_VertexArrayRef);
		ASSERT(0 != m_VertexArrayRef);
		//glBindVertexArray(m_VertexArrayRef);

	}


	Mesh::Mesh(Mesh&& other) noexcept
		: m_VertexArrayRef(std::exchange(other.m_VertexArrayRef, 0)),
		  m_VertexBufferIndex(std::exchange(other.m_VertexBufferIndex, 0)),
		  m_VertexBuffers(std::move(other.m_VertexBuffers)),
		  m_IndexBuffer(std::move(other.m_IndexBuffer))
	{
	}

	Mesh& Mesh::operator=(Mesh&& other) noexcept
	{
		if (this != &other)
		{
			if (m_VertexArrayRef != 0)
				glDeleteVertexArrays(1, &m_VertexArrayRef);
			m_VertexArrayRef = std::exchange(other.m_VertexArrayRef, 0);
			m_VertexBufferIndex = std::exchange(other.m_VertexBufferIndex, 0);
			m_VertexBuffers = std::move(other.m_VertexBuffers);
			m_IndexBuffer = std::move(other.m_IndexBuffer);
		}
		return *this;
	}

	Mesh::~Mesh()
	{
		if (m_VertexArrayRef != 0)
			glDeleteVertexArrays(1, &m_VertexArrayRef);
	}


	void Mesh::SetIndexBuffer(const RefPtr<IndexBuffer>& indexBuffer)
	{
		glBindVertexArray(m_VertexArrayRef);
		indexBuffer->Bind();
		m_IndexBuffer = indexBuffer;
	}


	void Mesh::AddVertexBuffer(const RefPtr<VertexBuffer>& vertexBuffer)
	{
		ASSERT(!vertexBuffer->GetBufferLayout().GetAttributes().empty(),
			"Buffer has no layout");

		glBindVertexArray(m_VertexArrayRef);
		vertexBuffer->Bind();

		const auto& layout = vertexBuffer->GetBufferLayout();

		for (auto& att : layout)
		{
			switch (att.m_Type)
			{
				case AttributeType::Float:
				case AttributeType::Vec2f:
				case AttributeType::Vec3f:
				case AttributeType::Vec4f:
				{
					glEnableVertexAttribArray(m_VertexBufferIndex);
					glVertexAttribPointer(m_VertexBufferIndex, att.GetComponentCount(), 
						FromAttributeDataTypeToGLType(att.m_Type),
						att.m_Normalized ? GL_TRUE: GL_FALSE, layout.GetStride(), 
						reinterpret_cast<const void*>(att.m_Offset));
					m_VertexBufferIndex++;
					break;
				}

				case AttributeType::Int:
				case AttributeType::Vec2i:
				case AttributeType::Vec3i:
				case AttributeType::Vec4i:
				case AttributeType::Bool:
				{
					glEnableVertexAttribArray(m_VertexBufferIndex);
					glVertexAttribIPointer(m_VertexBufferIndex, att.GetComponentCount(),
						FromAttributeDataTypeToGLType(att.m_Type), 
						layout.GetStride(), reinterpret_cast<const void*>(att.m_Offset));
					m_VertexBufferIndex++;
					break;
				}

				case AttributeType::Mat2:
				case AttributeType::Mat3:
				case AttributeType::Mat4:
				{
					uint8_t count = att.GetComponentCount();
					for (uint8_t i = 0; i < count; i++)
					{
						glEnableVertexAttribArray(m_VertexBufferIndex);
						glVertexAttribPointer(m_VertexBufferIndex,
							count,
							FromAttributeDataTypeToGLType(att.m_Type),
							att.m_Normalized ? GL_TRUE : GL_FALSE,
							layout.GetStride(),
							(const void*)(att.m_Offset + sizeof(float) * count * i));
						glVertexAttribDivisor(m_VertexBufferIndex, 1);
						m_VertexBufferIndex++;
					}
					break;
				}
				
			}
		}





		m_VertexBuffers.push_back(vertexBuffer);
	}


	void Mesh::Bind()
	{
		glBindVertexArray(m_VertexArrayRef);
	}


	void Mesh::Unbind()
	{
		glBindVertexArray(0);
	}

	





	

}

