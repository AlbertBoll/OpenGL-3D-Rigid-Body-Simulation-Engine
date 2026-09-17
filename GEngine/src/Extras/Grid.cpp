#include "gepch.h"
#include "Extras/Grid.h"
#include "Geometry/Geometry.h"
#include "Material/LineBasicMaterial.h"
#include "Camera/Camera.h"

namespace GEngine
{

	Geometry* Grid::CreateGeometry(float size, int divisions, const Vec3f grid_color, const Vec3f& center_color_x, const Vec3f& center_color_z)
	{
		auto geometry = new Geometry;

		std::vector<Vec3f> positionData;

		std::vector<Vec3f> colorData;
		std::vector<Vec2f> uvData;
		std::vector<Vec3f> vertexNormalData;
		//std::vector<Vec3f> faceNormalData;

		positionData.reserve(4 * (divisions + 1));

		colorData.reserve(4 * (divisions + 1));

		float delta_size = size / static_cast<float>(divisions);

		std::vector<float> range_values(divisions + 1, 0);
		std::generate(range_values.begin(), range_values.end(), [n = 0, &size, &delta_size]() mutable{return -size / 2.0f + n++ * delta_size; });

		for (auto& x : range_values)
		{
			positionData.emplace_back(x, -size / 2.0f, 0);
			positionData.emplace_back(x, size / 2.0f, 0);

			if (x == 0.f)
			{
				colorData.emplace_back(center_color_x);
				colorData.emplace_back(center_color_x);
			}

			else
			{

				colorData.emplace_back(grid_color);
				colorData.emplace_back(grid_color);
			}
		}

		for (auto& y : range_values)
		{
			positionData.emplace_back(-size / 2.0f, y, 0);
			positionData.emplace_back(size / 2.0f, y, 0);

			if (y == 0.f)
			{
				colorData.emplace_back(center_color_z);
				colorData.emplace_back(center_color_z);
			}

			else
			{

				colorData.emplace_back(grid_color);
				colorData.emplace_back(grid_color);
			}
		}

		geometry->AddAttributes(positionData, colorData, uvData, vertexNormalData);
		geometry->UnBindVAO();
		return geometry;
	}


	Grid::Grid(RefPtr<Material> material, float size, int divisions, float line_width,
		const Vec3f& grid_color, 
		const Vec3f& center_color_x, 
		const Vec3f& center_color_z): Entity(CreateGeometry(size, divisions, grid_color, center_color_x, center_color_z), nullptr)
	{

		//auto material = new LineBasicMaterial;
		// The factory has already completed checked shader/material construction.
		material->UseProgram();
		BindVAO();
		material->SetUniforms<Vec4f>({ {"uBaseColor", Vec4f{1.0f, 1.0f, 1.0f, 1.0f}} });
		material->SetUniforms<bool>({ {"uUseVertexColor", false} });
		RenderSetting setting;
		
		setting.m_PrimitivesSetting.lineSetting.lineType = LineType::Segments;
		setting.m_PrimitivesSetting.lineSetting.lineWidth = line_width;
		material->SetRenderSettings(setting);
		material->UpdateRenderSettings();
		SetMaterial(material);

	}

    std::expected<std::unique_ptr<Grid>, Asset::ShaderError> Grid::Create(float size, int divisions, float line_width,
        const Vec3f& grid_color, const Vec3f& center_color_x, const Vec3f& center_color_z)
    {
        auto material = Material::Create<LineBasicMaterial>();
        if (!material) return std::unexpected(material.error());
        return std::unique_ptr<Grid>(new Grid(*material, size, divisions, line_width, grid_color, center_color_x, center_color_z));
    }

	Grid::~Grid()
	{
		auto it = GetGeometry();
		delete it;
	}

	void Grid::Render(CameraBase* camera)
	{
		UseShaderProgram();

		BindVAO();

		SetUniforms<Mat4>({ {"uModel", GetWorldTransform()},
							{"uView", camera->GetView()},
							{"uProjection", camera->GetProjection()} });

		SetUniforms<Vec4f>({ {"uBaseColor", Vec4f{1.0f, 1.0f, 1.0f, 1.0f}} });

		SetUniforms<bool>({ {"uUseVertexColor", true} });

		ArraysDraw();
	}
}
