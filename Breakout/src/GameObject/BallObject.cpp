#include "GameObject/BallObject.h"

BallEntity::BallEntity(Geometry* geometry, const RefPtr<Material>& material)
	:SpriteEntity(geometry, material)
{

}

Vec2f BallEntity::Move(Timestep dt, unsigned int window_width)
{
    auto position = GetPosition();
    if (!m_IsStuck)
    {
        // move the ball
        position += Vec3f(m_SpriteComp.Velocity, 0.f) * (float)dt;
        // then check if outside window bounds and if so, reverse velocity and restore at correct position
        if (position.x <= 0.0f)
        {
            m_SpriteComp.Velocity.x *= -1.f;
            position.x = 0.0f;
        }
        else if (position.x + m_SpriteComp.Size.x >= window_width)
        {
            m_SpriteComp.Velocity.x *= -1.f;
            position.x = window_width - m_SpriteComp.Size.x;
        }
        if (position.y <= 0.0f)
        {
            m_SpriteComp.Velocity.y *= -1.0f;
            position.y = 0.0f;
        }
    }

    SetPosition(position);
    return position;
}

void BallEntity::Reset(const Vec2f& position, const Vec2f& velocity)
{
	SetPosition(Vec3f(position, 0.f));
	m_SpriteComp.Velocity = velocity;
}