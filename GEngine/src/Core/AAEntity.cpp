#include "gepch.h"
#include "Core/AAEntity.h"


namespace GEngine
{
	void AAEntity::Render(CameraBase* camera)
	{

		UseShaderProgram();

		BindVAO();

		BindTextures();

		UpdateRenderSettings();

		ArraysDraw();
	}
}