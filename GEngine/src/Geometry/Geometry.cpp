#include "gepch.h"
#include "Geometry/Geometry.h"

namespace GEngine
{
	Geometry::Geometry()
	{
		glGenVertexArrays(1, &m_Vao);
		glBindVertexArray(m_Vao);
	}

	Geometry::~Geometry()
	{
		
		if (!m_Buffers.empty())
		{
			glDeleteBuffers((int)m_Buffers.size(), m_Buffers.data());
		}

		if (m_Vao != 0) {
			glDeleteVertexArrays(1, &m_Vao);
			m_Vao = 0;
		}
	}

	void Geometry::CountVertices()
	{
		if(!m_Attributes.empty())
			std::visit([&](auto&& T)
				{
					m_VertexCount = (int)T.m_Data.size();
				}, m_Attributes.begin()->second);

		else
		{
			m_VertexCount = 0;
		}
	}

	void Geometry::BindVAO() const
	{
		glBindVertexArray(m_Vao);
	}

	void Geometry::UnBindVAO() const
	{
		glBindVertexArray(0);
	}

	void Geometry::AddIndices(const std::vector<unsigned int>& data)
	{
		m_IndexBuffer = IndexBuffer(data);
		m_Buffers.push_back(m_IndexBuffer.GetBufferRef());
		m_IndicesCount = (int)data.size();
		b_UseIndexBuffer = true;
	}

	void Geometry::AddEntityID(int entityID)
	{
		std::vector<int> entityIDData(m_VertexCount, entityID);
		BindVAO();
		AddAttributes(entityIDData);
		UnBindVAO();

	}

	void Geometry::ApplyTransform(const Mat4& transform, unsigned int location, bool bNormal)
	{
		if (auto it = m_Attributes.find(location); it != m_Attributes.end())
		{
			std::visit(overloaded{
				[&](auto&& arg) {},
				[&](Attribute<Vec3f>& arg)
				{
					if (auto& vertex_data = arg.m_Data; bNormal)
					{
						const auto rotationMatrix = glm::mat3(transform);

						for (auto& vertex : vertex_data)
						{
							vertex = rotationMatrix * vertex;
						}
					}

					else
					{
						for (auto& vertex : vertex_data)
						{
							Vec4f temp_vertex = Vec4f{ vertex, 1.0f };
							temp_vertex = transform * temp_vertex;
							vertex = Vec3f(temp_vertex);
						}
					}

					m_Attributes[location] = arg.m_Data;
				},

				}, it->second);

			
		}
	}


	
	void Geometry::Merge(Geometry* otherGeo)
	{
		for (auto& [ele, attribute] : m_Attributes)
		{
			std::visit(overloaded{
				[&](auto&& arg) 
				{
					auto& des = arg.m_Data;
					auto& src = std::get<std::decay_t<decltype(arg)>>(otherGeo->m_Attributes[ele]).m_Data;
					des.insert(des.end(), src.begin(), src.end()); },
				},
				/*[&](Attribute<Vec4f>& arg) {auto& des = arg.m_Data;
						auto& src = std::get<Attribute<Vec4f>>(otherGeo->m_Attributes[ele]).m_Data;
						des.insert(des.end(), src.begin(), src.end());},
				[&](Attribute<Vec3f>& arg) {auto& des = arg.m_Data;
						auto& src = std::get<Attribute<Vec3f>>(otherGeo->m_Attributes[ele]).m_Data;
						des.insert(des.end(), src.begin(), src.end()); },
				[&](Attribute<Vec2f>& arg) {auto& des = arg.m_Data;
						auto& src = std::get<Attribute<Vec2f>>(otherGeo->m_Attributes[ele]).m_Data;
						des.insert(des.end(), src.begin(), src.end()); },
				}*/
				attribute);
		}

		CountVertices();
	}

	//void Geometry::BuildBounds(const std::vector<Vec3f>& pts)
	//{
	//
	//	for (auto& pt: pts) {
	//		m_Bounds.Expand(pt);
	//	}
	//}

	Bounds Geometry::BuildBounds(const std::vector<Vec3f>& pts)
	{
		 Bounds bounds;
		 for (auto& pt : pts)
		 {
			 bounds.Expand(pt);
		 }

		 return bounds;
	}

	std::vector<Vec3f> Geometry::GetPoints(const Vec3f& scale)
	{
		auto pts = std::get<Attribute<Vec3f>>(m_Attributes[0]).m_Data;
		for (auto& pt : pts)
		{
			pt *= scale;
		}
		return pts;

	}

	std::vector<Vec3f> Geometry::GetUniquePoints(const Vec3f& scale)
	{
		std::vector<Vec3f> temp = m_UniquePoints;
		for (auto& pt : temp)
		{
			pt *= scale;
		}
		return temp;
	}

	//std::vector<Vec3f> Geometry::GetPoints()
	//{
	//	Attribute<Vec3f> positionAttr = std::get<Attribute<Vec3f>>(m_Attributes[0]);
	//	return positionAttr.m_Data;
	//}

}