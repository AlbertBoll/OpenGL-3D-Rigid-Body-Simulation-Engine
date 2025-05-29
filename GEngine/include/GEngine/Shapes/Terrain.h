#pragma once
#include "Geometry/Geometry.h"



namespace GEngine
{
	
	class Terrain: public Geometry
	{
		inline static std::string Image_Dir = "../GEngine/include/GEngine/Assets/Images/";

	public:
		Terrain(int GridX, int GridZ, int size = 800, const std::string& heightMap = "heightmap");
		Terrain(int size = 800, const std::string& heightMap="heightmap");
		float GetTerrainHeight(float WorldX, float WorldZ);

	private:
		float GetHeight(int x, int z);
		bool load_image(std::vector<unsigned char>& image, const std::string& filename, int& x, int& y, int& bbp);
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
		float m_X, m_Z;

	};
}


