#pragma once

namespace GEngine::Buffer
{

	class IndexBuffer
	{

	public:
		NONCOPYABLE(IndexBuffer);
		IndexBuffer() = default;

		IndexBuffer(IndexBuffer&& other) noexcept;

		IndexBuffer& operator = (IndexBuffer&& other) noexcept;

		IndexBuffer(const std::vector<unsigned int>& data);
		unsigned int GetBufferRef()const { return m_IndexBufferRef; }

		void AddIndexData(const std::vector<unsigned int>& data);

		void LoadIndex();

		void Bind();
		void Unbind();

		~IndexBuffer();

	private:
		unsigned int m_IndexBufferRef{};
		std::vector<unsigned int> m_Data;

	};
	
}
