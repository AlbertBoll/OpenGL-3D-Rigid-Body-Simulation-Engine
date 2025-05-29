#pragma once
#include <Math/Math.h>
//#include <Material/Material.h>
#include "Core/Utility.h"




namespace GEngine
{


    class GameObject2D
    {
        struct GameObjectParam2D
        {
            Math::Vec2f   m_Position{ 0.f };
            Math::Vec2f   m_Size{ 1.0f };
            Math::Vec2f   m_Velocity{ 0.f };
            Math::Vec3f   m_Color{ 1.f };
            float   m_Rotation{ 0.f };
            bool    m_IsSolid{ false };
            bool    m_Destroyed{ false };
        };


        class SpriteGeometry;
        class SpriteMaterial;

    public:
        GameObject2D() = default;
        GameObject2D(SpriteGeometry* geo, RefPtr<SpriteMaterial> spriteMaterial);
        void SetGameObjectParams(const GameObjectParam2D& param)
        {
            m_Param2D = param;
        }


    public:

        RefPtr<SpriteMaterial> m_SpriteMaterial{};
        SpriteGeometry* m_SpriteGeo{};
        GameObjectParam2D m_Param2D{};
     
    };


}