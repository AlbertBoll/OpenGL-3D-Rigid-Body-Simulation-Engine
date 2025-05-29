#pragma once
#include"Geometry/Geometry.h"

namespace GEngine::Shape
{
	class SmoothSphere : public Geometry
	{
	public:
        SmoothSphere(float radius = 0.5f, int u_resolution = 64, int v_resolution = 64) : Geometry()
        {

            std::vector<Vec3f> positions;
            std::vector<Vec3f> colors;
            std::vector<Vec2f> uv;
            std::vector<Vec3f> normals;
            std::vector<unsigned int> indices;

            const unsigned int X_SEGMENTS = u_resolution;
            const unsigned int Y_SEGMENTS = v_resolution;
           
            for (unsigned int y = 0; y <= Y_SEGMENTS; ++y)
            {
                for (unsigned int x = 0; x <= X_SEGMENTS; ++x)
                {
                    
                    float xSegment = static_cast<float>(x) / static_cast<float>(X_SEGMENTS);
                    float ySegment = static_cast<float>(y) / static_cast<float>(Y_SEGMENTS);
                    float xPos = radius * std::cos(xSegment * TwoPi) * std::sin(ySegment * Pi);
                    float yPos = radius * std::cos(ySegment * Pi);
                    float zPos = radius * std::sin(xSegment * TwoPi) * std::sin(ySegment * Pi);

                    positions.emplace_back(xPos, yPos, zPos);
                    uv.emplace_back(xSegment, ySegment);
                    normals.emplace_back(xPos, yPos, zPos);
                }
            }

            bool oddRow = false;
            for (unsigned int y = 0; y < Y_SEGMENTS; ++y)
            {
                if (!oddRow) // even rows: y == 0, y == 2; and so on
                {
                    for (unsigned int x = 0; x <= X_SEGMENTS; ++x)
                    {
                        indices.push_back(y * (X_SEGMENTS + 1) + x);
                        indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
                    }
                }
                else
                {
                    for (int x = X_SEGMENTS; x >= 0; --x)
                    {
                        indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
                        indices.push_back(y * (X_SEGMENTS + 1) + x);
                    }
                }
                oddRow = !oddRow;
            }
            colors.resize(positions.size(), { 1.f, 1.f, 1.f });

            AddAttributes(positions, colors, uv, normals);
            AddIndices(indices);
            //UnBindVAO();
        }
    
	};
}
