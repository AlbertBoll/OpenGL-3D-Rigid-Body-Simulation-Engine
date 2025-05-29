#pragma once
#include <Mesh/IndexBuffer.h>
#include <Mesh/Attribute.h>
#include <unordered_map>
#include <variant>
#include "Core/Utility.h"
#include <Physics/Bounds.h>


namespace GEngine
{
	using namespace Buffer;
	class Geometry
	{

	private:

		friend class Entity;
		friend class SpriteEntity;
		friend struct MeshComponent;

		std::unordered_map<unsigned int, std::variant<Attribute<Vec4f>,
			Attribute<Vec3f>,
			Attribute<Vec2f>,
			Attribute<Vec1f>,
			Attribute<Vec1i>,
			Attribute<Vec2i>,
			Attribute<Vec3i>,
			Attribute<Vec4i>,
			Attribute<Mat2>,
			Attribute<Mat3>,
			Attribute<Mat4>>> m_Attributes;

		IndexBuffer m_IndexBuffer; // Hold index buffer

		std::vector<unsigned int> m_Buffers; //keep reference of active BufferRef 

		unsigned int m_Vao{};

		int m_IndicesCount{};
		int m_VertexCount{};
		int m_Binding{ 0 };
		bool b_HasRecursive = false;
		bool b_UseIndexBuffer = false;

	protected:
		std::vector<Vec3f> m_UniquePoints;
		

		//Bounds m_Bounds;

	public:

		Geometry();

		//virtual ~Geometry() = default;

		virtual ~Geometry();

		int GetIndicesCount()const { return m_IndicesCount; }
		int GetVerticesCount()const { return m_VertexCount; }
		bool IsUsingIndexBuffer()const { return b_UseIndexBuffer; }
		void CountVertices();
		unsigned int GetVAO()const { return m_Vao; }
		void BindVAO()const;
		void UnBindVAO()const;

		void AddIndices(const std::vector<unsigned int>& data);

		auto& GetAttributes() { return m_Attributes; }


		template<typename Attrib>
		void AddAttributes(const Attrib& data);

		template<typename Attrib, typename... Attribs>
		void AddAttributes(const Attrib& data, const Attribs&... rest);
		void AddEntityID(int entityID);
	
		void ApplyTransform(const Mat4& transform, unsigned int location = 0, bool bNormal = false);

		void Merge(Geometry* otherGeo);

		//std::vector<Vec3f> GetPoints()
		Bounds BuildBounds(const std::vector<Vec3f>& pts);
		//Bounds GetBounds()const { return m_Bounds; }
		std::vector<Vec3f> GetPoints(const Vec3f& scale = {1, 1, 1});
		std::vector<Vec3f> GetUniquePoints(const Vec3f& scale = { 1, 1, 1 });
	};


	template<typename Attrib>
	inline void Geometry::AddAttributes(const Attrib& data)
	{
		//if (!data.empty())
		//{
		Attribute attribute = Attribute(data);
		m_Buffers.push_back(attribute.GetBufferRef());
		m_Attributes.emplace(m_Binding++, attribute);

		//m_Attributes.insert({ m_Binding++, Attribute(data) });

		if (!b_HasRecursive)
		{
			CountVertices();
			b_HasRecursive = true;
		}

		//}
	}
	template<typename Attrib, typename ...Attribs>
	void Geometry::AddAttributes(const Attrib& data, const Attribs & ...rest)
	{

		AddAttributes(data);
		AddAttributes(rest...);

	}


}
