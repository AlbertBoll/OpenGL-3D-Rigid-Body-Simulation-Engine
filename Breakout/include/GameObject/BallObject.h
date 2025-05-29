#include"Sprite/SpriteEntity.h"

#include <Material/Material.h>


#include"Core/Utility.h"

using namespace GEngine;

class BallEntity: public SpriteEntity
{
	

public:
	BallEntity(): SpriteEntity(){}
	BallEntity(Geometry* geometry, const RefPtr<Material>& material);
	Vec2f Move(Timestep dt, unsigned int window_width);
	void Reset(const Vec2f& position, const Vec2f& velocity);

	BallEntity& SetParams(const Vec2f& pos, float radius, float rotation = 0.f)
	{
		m_Radius = radius;
		Set2DTransform(pos, { 2.f * radius, 2.f * radius }, rotation);
		return *this;
	}

	bool& GetIsStucked() { return m_IsStuck; }

public:
	float m_Radius{12.5f};
	bool m_IsStuck{true};


private:

};