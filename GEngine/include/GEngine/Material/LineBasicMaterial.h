#pragma once
#include "BasicMaterial.h"


namespace GEngine
{
	class LineBasicMaterial : public BasicMaterial
	{

	public:
		LineBasicMaterial(Construction& construction) : BasicMaterial(construction)
		{
            if (!construction) return;
			RenderSetting setting;
			setting.m_PrimitivesSetting.lineSetting.lineType = LineType::Connected;
			setting.m_PrimitivesSetting.lineSetting.lineWidth = 1.f;
			UpdateRenderSettings();
			SetRenderSettings(setting);
		}

		void UpdateRenderSettings()override;
		
		
	};
}
