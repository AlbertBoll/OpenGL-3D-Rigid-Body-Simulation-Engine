#pragma once
#include <span>

namespace GEngine { class GpuMesh; }

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

        // Semantic CPU export; no GPU readback or buffer-name access.
        std::span<const unsigned int> Indices() const noexcept { return m_Data; }

		void Bind();
		void Unbind();

		~IndexBuffer();

	private:
		friend class ::GEngine::GpuMesh;
		unsigned int m_IndexBufferRef{};
		std::vector<unsigned int> m_Data;

	};
	
}
