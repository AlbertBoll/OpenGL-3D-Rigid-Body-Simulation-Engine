#include "gepch.h"
#include "Mesh/IndexBuffer.h"
#include <utility>

namespace GEngine::Buffer
{

	IndexBuffer::IndexBuffer(IndexBuffer&& other) noexcept
		: m_IndexBufferRef(std::exchange(other.m_IndexBufferRef, 0)),
		  m_Data(std::move(other.m_Data))
	{
	}

	IndexBuffer& IndexBuffer::operator=(IndexBuffer&& other) noexcept
	{
		if (this != &other)
		{
			if (m_IndexBufferRef != 0)
			{
				glDeleteBuffers(1, &m_IndexBufferRef);
			}

			m_IndexBufferRef = std::exchange(other.m_IndexBufferRef, 0);
			m_Data = std::move(other.m_Data);
		}

		return *this;
	}

	IndexBuffer::IndexBuffer(const std::vector<unsigned int>& data) : m_Data(data)
	{
		glGenBuffers(1, &m_IndexBufferRef);
		ASSERT(m_IndexBufferRef != 0);
		LoadIndex();
	}

	void IndexBuffer::AddIndexData(const std::vector<unsigned int>& data)
	{
		ASSERT(m_Data.empty());
		m_Data = data;
		LoadIndex();

	}

	void IndexBuffer::LoadIndex()
	{

		//ASSERT(m_IndexBufferRef != 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IndexBufferRef);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_Data.size() * sizeof(unsigned int), m_Data.data(), GL_STATIC_DRAW);
	}

	void IndexBuffer::Bind()
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IndexBufferRef);
	}

	void IndexBuffer::Unbind()
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	IndexBuffer::~IndexBuffer()
	{
		if(m_IndexBufferRef != 0)
			glDeleteBuffers(1, &m_IndexBufferRef);
	}


}
