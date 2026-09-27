#pragma once
#include "Core/RuntimeAssets.h"
#include "Geometry/Geometry.h"



namespace GEngine
{
	
	class Terrain: public Geometry
	{


	public:
		[[nodiscard]] static std::expected<std::unique_ptr<Terrain>, PlatformError> Create(int GridX, int GridZ, int size = 800, const std::string& heightMap = "heightmap");
		[[nodiscard]] static std::expected<std::unique_ptr<Terrain>, PlatformError> Create(int size = 800, const std::string& heightMap = "heightmap");
		float GetTerrainHeight(float WorldX, float WorldZ);

	private:
		float GetHeight(int x, int z);
		Terrain(int size, std::vector<unsigned char> pixels, int width, int height, int bpp);
		Vec3f CalculateNormal(int x, int z);

		Terrain& SetX(float x)
		{
			m_X = x;
			return *this;
		}

		float GetX()const { return m_X; }

		float GetZ()const { return m_Z; }

		Terrain& SetZ(float z)
		{
			m_Z = z;
			return *this;
		}

		int GetSize()const { return m_Size; }
		


	private:
		std::vector<unsigned char> m_Data;
		int m_Width, m_Height, m_Bpp, m_Size;
		std::vector<float> m_Heights;
		float m_X = 0, m_Z = 0;

	};
}


