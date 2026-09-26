#pragma once
#include <span>
#include <vector>
#include "Core/Utility.h"

namespace GEngine { class GpuMesh; namespace IndexBufferDetail { struct BackendAccess; } }

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

		void AddIndexData(const std::vector<unsigned int>& data);

		void LoadIndex();

        // Semantic CPU export; no GPU readback or buffer-name access.
        std::span<const unsigned int> Indices() const noexcept { return m_Data; }

		void Bind();
		void Unbind();

		~IndexBuffer();

	private:
		friend struct ::GEngine::IndexBufferDetail::BackendAccess;
		unsigned int GetBufferRef()const { return m_IndexBufferRef; }
		friend class ::GEngine::GpuMesh;
		unsigned int m_IndexBufferRef{};
		std::vector<unsigned int> m_Data;

	};
	
}
