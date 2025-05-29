#include "gepch.h"

#include "Core/Entity.h"
//#include"Material/Material.h"
#include"Geometry/Geometry.h"
#include"Camera/Camera.h"
#include <Core/Renderer.h>

namespace GEngine
{
	Entity::Entity(): Actor()
	{

	}

	Entity::Entity(Geometry* geometry, const RefPtr<Material>& material) : Actor(), m_Geometry(geometry), m_Material(material)
	{
		//glGenVertexArrays(1, &m_Vao);
		//glBindVertexArray(m_Vao);
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

	Entity::Entity(Entity&& other) noexcept
	{
		m_Geometry = std::exchange(other.m_Geometry, nullptr);
		m_Material = std::exchange(other.m_Material, nullptr);
		//m_Vao = other.m_Vao;
		//other.m_Vao = 0;
		m_Visible = other.m_Visible;
	}

	void Entity::UpdateRenderSettings() const
	{
		m_Material->UpdateRenderSettings();
	}

	void Entity::SetRenderMode(RenderMode mode)
	{
		m_Material->SetRenderMode(mode);
	}

	Entity::~Entity()
	{
	/*	if (m_Vao != 0) {
			glDeleteVertexArrays(1, &m_Vao);
			m_Vao = 0;
		}*/
	}

	void Entity::ArraysDraw() const
	{
		glDrawArrays((int)m_Material->m_RenderSetting.m_Mode, 0, m_Geometry->m_VertexCount);
		Renderer::GetRenderStats().m_ArrayDrawCall++;
	}


	void Entity::ElementsDraw() const
	{
		glDrawElements((int)m_Material->m_RenderSetting.m_Mode, m_Geometry->m_IndicesCount, GL_UNSIGNED_INT, nullptr);
		Renderer::GetRenderStats().m_ElementsDrawCall++;
	}

	/*void Entity::ElementsInstancedDraw() const
	{
		glDrawElementsInstanced(m_Material->m_Setting.m_Mode, m_Geometry->m_IndicesCount, GL_UNSIGNED_INT, nullptr, );
	}*/

	//void Entity::ArraysInstancedDraw() const
	//{
	//	glDrawArraysInstanced(m_Material->m_Setting.m_Mode, 0, m_Geometry->m_VertexCount, );
	//}

	void Entity::BindTextures() const
	{
		m_Material->BindTextureUniforms();
	}

	void Entity::UseShaderProgram() const
	{
		m_Material->UseProgram();
	}

	void Entity::BindVAO() const
	{
		m_Geometry->BindVAO();
	}

	void Entity::UnBindVAO() const
	{
		m_Geometry->UnBindVAO();
	}


	void Entity::Render(CameraBase* camera)
	{

		auto is_visible = bool_variant(m_Visible);
		auto is_ArraysDraw = bool_variant(m_Material->m_RenderSetting.m_RenderMode == RenderMode::Arrays);
		auto is_ElementDraw = bool_variant(m_Material->m_RenderSetting.m_RenderMode == RenderMode::Elements);
	

		std::visit([&](auto b_visible, auto b_ArrayDraw, auto b_ElementDraw)
		{
			if constexpr (b_visible)
			{
				UseShaderProgram();

				BindVAO();

				SetUniforms<Mat4>({ {"u_model", GetWorldTransform()} ,
								{"u_view", camera->m_View},
								{"u_projection", camera->m_Projection} });

				SetUniforms<Vec3f>({ {"u_cameraPosition", camera->GetWorldPosition()} });
				//SetUniforms<bool>({ {"u_useVertexColor", false } });
				//SetUniforms<Vec4f>({ {"u_baseColor", {1.f, 1.f, 1.f, 1.f} } });
				SetUniforms<Vec2f>({ {"u_offset", GetTextureOffset()} });
				//BindTexture(m_Material->GetTextureTarget());
				GetMaterial()->BindTextureUniforms();
			

				GetMaterial()->UploadUniforms();

				UpdateRenderSettings();

				if constexpr (b_ArrayDraw) ArraysDraw();
				else if constexpr (b_ElementDraw) ElementsDraw();
				
			}

		}, is_visible, is_ArraysDraw, is_ElementDraw);


	}


}


	

