#include "gepch.h"
#include "Scene/BoxEntity.h"
#include "Camera/Camera.h"

namespace GEngine::SceneObjects
{
	BoxEntity::BoxEntity(Geometry* geometry, const RefPtr<Material>& material): Entity(geometry, material)
	{

	}

	void BoxEntity::Render(CameraBase* camera)
	{
		UseShaderProgram();

		BindVAO();

		SetUniforms<Mat4>({ {"uModel", GetWorldTransform()},
							{"uView", camera->GetView()},
							{"uProjection", camera->GetProjection()} });

		SetUniforms<Vec4f>({ {"uBaseColor", m_Color} });

		SetUniforms<bool>({ {"uUseVertexColor", true} });

		ArraysDraw();
	}
	
}
