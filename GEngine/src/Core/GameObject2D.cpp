#include "gepch.h"
#include "Core/GameObject2D.h"


namespace GEngine
{
	GameObject2D::GameObject2D(SpriteGeometry* geo, RefPtr<SpriteMaterial> spriteMaterial)
		: m_SpriteGeo(geo), m_SpriteMaterial(spriteMaterial)
	{
		
	}
}