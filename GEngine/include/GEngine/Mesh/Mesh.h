#pragma once


using namespace GEngine::Buffer;
class GEngine::Buffer::VertexBuffer;
class GEngine::Buffer::IndexBuffer;

namespace GEngine
{
	

	enum class BufferTrait
	{
		SeparateBuffer,
		InterleaveBuffer
	};

	enum class DrawType
	{
		DrawArrays,
		DrawElements,
		DrawInstances
	};

	class Mesh
	{
	
	public:
		NONCOPYABLE(Mesh);
		Mesh(Mesh&& other) noexcept;
		Mesh& operator=(Mesh&& other) noexcept;
		Mesh();
		~Mesh();

		void SetIndexBuffer(const RefPtr<IndexBuffer>& indexBuffer);
		void AddVertexBuffer(const RefPtr<VertexBuffer>& vertexBuffer);

		const std::vector<RefPtr<VertexBuffer>>& GetVertexBuffers() const 
		{ 
			return m_VertexBuffers;
		}

		const RefPtr<IndexBuffer>& GetIndexBuffer() const
		{ 
			return m_IndexBuffer; 
		}

		void Bind();
		void Unbind();

	private:
		unsigned int m_VertexArrayRef{};
		unsigned int m_VertexBufferIndex{};
		std::vector<RefPtr<VertexBuffer>>m_VertexBuffers;
		RefPtr<IndexBuffer> m_IndexBuffer;
	};

}
