#pragma once
#include "Core/Entity.h"

namespace GEngine::SceneObjects
{
	class BoxEntity : public Entity
	{
	public:
		BoxEntity(Geometry* geometry, const RefPtr<Material>& material);

		Vec4f& GetColor() { return m_Color; }

		void Render(CameraBase* camera);

	private:
		Vec4f m_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
	};

}

