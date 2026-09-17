#pragma once
#include "Core/Entity.h"

namespace GEngine
{
	class Axes : public Entity
	{
		static Geometry* BuildGeometry(float axis_length = 1.0f);
		
		

		static std::expected<RefPtr<Material>, Asset::ShaderError> BuildMaterial(float line_width = 2.0f);

	public:
		[[nodiscard]] static std::expected<std::unique_ptr<Axes>, Asset::ShaderError> Create(float axis_length = 1.0f, float line_width = 2.0f);
    private:
        Axes(float axis_length, RefPtr<Material> material);
	};

}