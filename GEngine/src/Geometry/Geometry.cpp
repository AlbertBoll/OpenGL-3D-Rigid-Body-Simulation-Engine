#include "gepch.h"
#include "Geometry/Geometry.h"
#include <new>
#include <cstddef>

namespace GEngine
{
    std::expected<MeshAsset, MeshError> Geometry::ExportCpuMesh() const
    {
        struct Vertex { Vec3f position{}, color{}; Vec2f uv{}; Vec3f normal{}; };
        const auto position = m_Attributes.find(0);
        if (position == m_Attributes.end() || !std::holds_alternative<Attribute<Vec3f>>(position->second))
            return std::unexpected(MeshError{MeshErrorCode::MissingPosition});
        const auto& positions = std::get<Attribute<Vec3f>>(position->second).m_Data;
        if (positions.size() > (std::numeric_limits<std::size_t>::max)() / sizeof(Vertex))
            return std::unexpected(MeshError{MeshErrorCode::SizeOverflow, positions.size()});
        std::unique_ptr<Vertex[]> vertices(new (std::nothrow) Vertex[positions.size()]);
        if (!vertices && !positions.empty())
            return std::unexpected(MeshError{MeshErrorCode::AllocationFailed, positions.size()});
        std::array<VertexAttribute, 4> attributes{};
        std::size_t count = 0;
        for (const auto& [slot, attribute] : m_Attributes)
            if (slot >= 4) return std::unexpected(MeshError{MeshErrorCode::UnsupportedAttribute, slot});
        for (unsigned slot = 0; slot != 4; ++slot)
        {
            const auto found = m_Attributes.find(slot);
            if (found == m_Attributes.end()) continue;
            const bool valid = slot == 2 ? std::holds_alternative<Attribute<Vec2f>>(found->second)
                                         : std::holds_alternative<Attribute<Vec3f>>(found->second);
            if (!valid) return std::unexpected(MeshError{MeshErrorCode::UnsupportedAttribute, slot});
            const auto size = std::visit([](const auto& a) { return a.m_Data.size(); }, found->second);
            // Helpers author empty UV/normal arrays. They mean an absent optional
            // semantic, not vertex records or dynamic GPU scratch storage.
            if (slot != 0 && size == 0) continue;
            if (size != positions.size())
                return std::unexpected(MeshError{MeshErrorCode::VertexPayloadMismatch, slot});
            const VertexSemantic semantics[]{VertexSemantic::Position, VertexSemantic::Color0,
                VertexSemantic::TexCoord0, VertexSemantic::Normal};
            const std::size_t offsets[]{offsetof(Vertex, position), offsetof(Vertex, color),
                offsetof(Vertex, uv), offsetof(Vertex, normal)};
            attributes[count++] = {semantics[slot], AttributeSlot(semantics[slot]), VertexScalarFormat::Float32,
                static_cast<std::uint8_t>(slot == 2 ? 2 : 3), VertexInterpretation::Floating, offsets[slot]};
            for (std::size_t i = 0; i < positions.size(); ++i)
            {
                if (slot == 0) vertices[i].position = positions[i];
                else if (slot == 1) vertices[i].color = std::get<Attribute<Vec3f>>(found->second).m_Data[i];
                else if (slot == 2) vertices[i].uv = std::get<Attribute<Vec2f>>(found->second).m_Data[i];
                else vertices[i].normal = std::get<Attribute<Vec3f>>(found->second).m_Data[i];
            }
        }
        auto source = MeshSourceData::FromVertices<Vertex>({vertices.get(), positions.size()}, {attributes.data(), count});
        const auto indices = m_IndexBuffer.Indices();
        static_assert(sizeof(unsigned int) == sizeof(std::uint32_t));
        source.indexFormat = b_UseIndexBuffer ? MeshIndexFormat::UInt32 : MeshIndexFormat::None;
        source.indices = b_UseIndexBuffer ? std::as_bytes(indices) : std::span<const std::byte>{};
        source.indexCount = b_UseIndexBuffer ? indices.size() : 0;
        const SubmeshRange range{0, b_UseIndexBuffer ? indices.size() : positions.size(), 0};
        source.submeshes = {&range, 1};
        return MeshAsset::Create(source);
    }
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

	void Geometry::BindVBO() const
	{
		for(auto& buff: m_Buffers)
		glBindBuffer(GL_ARRAY_BUFFER, buff);
	}

	void Geometry::ReSizeVBO(unsigned int size) const
	{
		BindVBO();
		for (auto& buff : m_Buffers)
		{
			glBufferData(GL_ARRAY_BUFFER, size * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
		}
	}

	void Geometry::AddIndices(const std::vector<unsigned int>& data)
	{
		m_IndexBuffer = IndexBuffer(data);
		// The EBO belongs only to m_IndexBuffer; m_Buffers owns attribute VBOs.
		m_IndicesCount = (int)data.size();
		b_UseIndexBuffer = true;
	}

	void Geometry::LoadSubDataDynamically(const Bounds& bounds)
	{
		auto mins = bounds.mins + Vec3f{ 0.02f, 0.02f, 0.02f };
		auto maxs = bounds.maxs - Vec3f{ 0.02f, 0.02f, 0.02f };
		Vec3f vertices[24] = {
			{mins.x, mins.y, mins.z}, {maxs.x, mins.y, mins.z},
			{maxs.x, maxs.y, mins.z}, {mins.x, maxs.y, mins.z},
			{mins.x, mins.y, maxs.z}, {maxs.x, mins.y, maxs.z},
			{maxs.x, maxs.y, maxs.z}, {mins.x, maxs.y, maxs.z},
			{mins.x, mins.y, mins.z}, {mins.x, maxs.y, mins.z},
			{maxs.x, mins.y, mins.z}, {maxs.x, maxs.y, mins.z},
			{mins.x, mins.y, maxs.z}, {mins.x, maxs.y, maxs.z},
			{maxs.x, mins.y, maxs.z}, {maxs.x, maxs.y, maxs.z},
			{mins.x, mins.y, mins.z}, {mins.x, mins.y, maxs.z},
			{maxs.x, mins.y, mins.z}, {maxs.x, mins.y, maxs.z},
			{mins.x, maxs.y, mins.z}, {mins.x, maxs.y, maxs.z},
			{maxs.x, maxs.y, mins.z}, {maxs.x, maxs.y, maxs.z}
		};
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
	}

	void Geometry::LoadKDTreeVisualizerDynamically(const std::vector<Vec3f>& data)
	{
		glBufferSubData(GL_ARRAY_BUFFER, 0, data.size() * sizeof(Vec3f), data.data());
	}

	template<typename T>
	void Geometry::AddAttribute()
	{
		Attribute attribute = Attribute<T>();
	}

	template void Geometry::AddAttribute<Vec3f>();

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
