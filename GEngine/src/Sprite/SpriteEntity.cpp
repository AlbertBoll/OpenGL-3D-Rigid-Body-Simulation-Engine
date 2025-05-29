#include "gepch.h"
#include "Sprite/SpriteEntity.h"
#include "Geometry/Geometry.h"
#include "Core/Renderer.h"
#include "Camera/Camera.h"



namespace GEngine
{



	SpriteEntity::SpriteEntity(): Actor()
	{

	}

	SpriteEntity::SpriteEntity(Geometry* geometry, const RefPtr<Material>& material): Actor(), m_Geometry(geometry), m_Material(material)
	{
		Renderer::GetRenderStats().m_IndicesCount += geometry->m_IndicesCount;
		Renderer::GetRenderStats().m_VerticeCount += geometry->m_VertexCount;

		BindVAO();

		for (auto& [index, ele] : m_Geometry->m_Attributes)
		{
			std::visit([&](auto&& arg)
				{
					arg.AssociateSlot(index);
				}, ele);
		}

		// Bind index buffer if used
		if (m_Geometry->b_UseIndexBuffer)
			m_Geometry->m_IndexBuffer.LoadIndex();

		//glBindVertexArray(0);
		UnBindVAO();
	}

	void SpriteEntity::BindVAO() const
	{
		m_Geometry->BindVAO();
	}

	void SpriteEntity::Set2DTransform(const Vec2f& pos, const Vec2f& size, float rotation)
	{
		SetScale(Vec3f(size, 0.f), false);
		Translate(-0.5f * size.x, -0.5f * size.y, 0.f, false);
		RotateZ(Math::ToRadians(rotation));
		Translate(0.5f * size.x, 0.5f * size.y, 0.f, false);
		Translate(pos.x, pos.y, 0.f, false);
	}

	void SpriteEntity::UnBindVAO() const
	{
		m_Geometry->UnBindVAO();
	}

	void SpriteEntity::UseShaderProgram() const
	{
		m_Material->UseProgram();
	}

	void SpriteEntity::UpdateRenderSettings() const
	{
		m_Material->UpdateRenderSettings();
	}

	void SpriteEntity::ArraysDraw() const
	{
		glDrawArrays((int)m_Material->m_RenderSetting.m_Mode, 0, m_Geometry->m_VertexCount);
		Renderer::GetRenderStats().m_ArrayDrawCall++;
	}

	void SpriteEntity::Render(CameraBase* camera)
	{
		auto is_visible = bool_variant(m_Visible);
		std::visit([&](auto b_visible)
			{
				if constexpr (b_visible)
				{
					UseShaderProgram();

					BindVAO();

					SetUniforms<Mat4>({ {"u_model", GetWorldTransform()} ,
										{"u_projection", camera->m_Projection} });
					SetUniforms<Vec3f>({ { m_SpriteComp.SpriteColor.Name, m_SpriteComp.SpriteColor.Data } });

					m_Material->BindTextureUniforms();
					SetUniforms<Mat4>({ {"u_model", GetWorldTransform()} });
					ArraysDraw();

				}
			}, is_visible);

		
	

	}

}
