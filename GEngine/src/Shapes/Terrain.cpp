#include "gepch.h"
#include "Shapes/Terrain.h"
#include <stb_image/stb_image.h>


namespace GEngine
{
	static constexpr float MAX_HEIGHT = 40;
	static constexpr float MAX_PIXEL_COLOR = 256 * 256 * 256;

	Terrain::Terrain(int GridX, int GridZ, int size, const std::string& heightMap): Terrain(size, heightMap)
	{
		m_X = (float)GridX * size;
		m_Z = (float)GridZ * size;
	}

	Terrain::Terrain(int size, const std::string& heightMap): Geometry(), m_Size(size)
	{
		auto dir = Image_Dir + heightMap + ".png";

		//stbi_set_flip_vertically_on_load(true);
		bool success = load_image(m_Data, dir, m_Width, m_Height, m_Bpp);
		ASSERT(success);

		int VertexCount = m_Height;

		int count = VertexCount * VertexCount;
		m_Heights.resize(count);

		std::vector<Vec3f> vertices(count);
		std::vector<Vec2f> uvs(count);
		std::vector<Vec3f> normals(count);

		std::vector<unsigned int> indices(6 * (VertexCount - 1) * (VertexCount - 1));

		int vertexPointer = 0;
		float height;
		for (int i = 0; i < VertexCount; i++) {
			for (int j = 0; j < VertexCount; j++) {
				height = GetHeight(j, i);
				m_Heights[j + i * m_Width] = height;
				Vec3f position = { (float)j / ((float)VertexCount - 1) * size, height, (float)i / ((float)VertexCount - 1) * size};
				Vec2f uv = { (float)j / ((float)VertexCount - 1) , (float)i / ((float)VertexCount - 1) };
				Vec3f normal = CalculateNormal(j, i);
				//std::cout << "("<<j<<", "<<i<<")"<<" = "<<GetHeight(j, i) << std::endl;

				vertices[vertexPointer] = position;
				uvs[vertexPointer] = uv;
				normals[vertexPointer] = normal;
				vertexPointer++;

			}
		}

		int pointer = 0;
		for (int gz = 0; gz < VertexCount - 1; gz++) {
			for (int gx = 0; gx < VertexCount - 1; gx++) {
				int topLeft = (gz * VertexCount) + gx;
				int topRight = topLeft + 1;
				int bottomLeft = ((gz + 1) * VertexCount) + gx;
				int bottomRight = bottomLeft + 1;
				indices[pointer++] = topLeft;
				indices[pointer++] = bottomLeft;
				indices[pointer++] = topRight;
				indices[pointer++] = topRight;
				indices[pointer++] = bottomLeft;
				indices[pointer++] = bottomRight;
			}
		}

		AddAttributes(vertices, uvs, normals);
		AddIndices(indices);

	}




	float Terrain::GetHeight(int x, int z)
	{
		if (x < 0 || x >= m_Height || z < 0 || z >= m_Height)
		{
			return 0;
		}

		size_t index = 4 * (x * m_Width + z);
		uint8_t r = m_Data[index + 0];
		uint8_t g = m_Data[index + 1];
		uint8_t b = m_Data[index + 2];
		uint8_t a = m_Data[index + 3];
		//std::cout << (int)a << std::endl;
		//float height = (r << 24) | (g << 16) | (b << 8);
		float height = (float)(((int)a << 24) | ((int)r << 16) | ((int)g << 8)| ((int)b << 0));
		height += MAX_PIXEL_COLOR/2 ;
		height /= MAX_PIXEL_COLOR/2 ;
		//height -= 1;
		height *= MAX_HEIGHT;

		return height;

	}

	bool Terrain::load_image(std::vector<unsigned char>& image, const std::string& filename, int& x, int& y, int& bbp)
	{
	
		unsigned char* data = stbi_load(filename.c_str(), &x, &y, &bbp, 4);
		if (data != nullptr)
		{
			image = std::vector<unsigned char>(data, data + x * y * 4);
		}
		stbi_image_free(data);
		return (data != nullptr);
	}

	Vec3f Terrain::CalculateNormal(int x, int z)
	{
		float heightL = GetHeight(x - 1, z);
		float heightR = GetHeight(x + 1, z);
		float heightD = GetHeight(x, z - 1);
		float heightU = GetHeight(x, z + 1);

		Vec3f normal = { heightL - heightR, 2.f, heightD - heightU };
		return glm::normalize(normal);
	}

	float Terrain::GetTerrainHeight(float WorldX, float WorldZ)
	{
		float terrainX = WorldX - m_X;
		float terrainZ = WorldZ - m_Z;
		float gridSquareSize = m_Size / ((float)m_Height - 1.f);
		int gridX = (int)std::floor(terrainX / gridSquareSize);
		int gridZ = (int)std::floor(terrainZ / gridSquareSize);
		if (gridX >= m_Height - 1 || gridX < 0 || gridZ >= m_Height - 1 || gridZ < 0)
		{
			return 0.f;
		}

		float xCoord = std::fmod(terrainX, gridSquareSize) / gridSquareSize;
		float zCoord = std::fmod(terrainZ, gridSquareSize) / gridSquareSize;
		
		float answer;

		if (xCoord <= 1 - zCoord)
		{
		
			answer = Math::BarryCentric(Vec3f(0, m_Heights[gridX + m_Width * gridZ], 0), Vec3f(1,
				m_Heights[gridX + 1 + m_Width * gridZ], 0), Vec3f(0,
					m_Heights[gridX+ m_Width*(gridZ + 1)], 1), Vec2f(xCoord, zCoord));
		}

		else
		{
		
			answer = Math::BarryCentric(Vec3f(1, m_Heights[gridX + 1 + m_Width * gridZ], 0), Vec3f(1,
				m_Heights[gridX + 1 + m_Width * (gridZ + 1)], 1), Vec3f(0,
					m_Heights[gridX + m_Width * (gridZ + 1)], 1), Vec2f(xCoord, zCoord));

		}

		return answer;
	}



}

