#include"gepch.h"
#include"Component/Component.h"
#include <Assets/Shaders/Shader.h>
#include "Assets/Textures/Texture.h"
#include <Core/RenderSystem.h>
#include "Geometry/Geometry.h"
#include "Shapes/AABBBoundingBox.h"
#include "Managers/ShapeManager.h"

namespace GEngine
{

	using namespace Shape;
	using namespace Asset;
	namespace Component
	{

		void TexturesComponent::BindTextures(Shader* shader)
		{

			for (auto& ele : TextureList)
			{
				shader->BindTextureUniform(ele.first, ele.second.second, ele.second.first);
			}

		}

		void TexturesComponent::PreBindTextures(Shader* shader)
		{
			shader->Bind();
			unsigned int i = 0;
			for (auto& texture : Textures)
			{
				auto& str = texture->GetUniformName();
				TextureList[texture->GetTextureID()] = { texture->GetTextureInfo().m_TextureSpec.m_TexTarget, i };
				shader->SetUniform(str.c_str(), std::pair<unsigned int, std::pair<unsigned, unsigned>>{texture->GetTextureInfo().m_TextureSpec.m_TexTarget, { texture->GetTextureID(), i++ }});
			}
			shader->UnBind();
		}

		void TexturesComponent::LoadUniforms(Asset::Shader* shader)const
		{
			shader->SetUniform(Tiling.Name.c_str(), Tiling.Data);

		}


		MeshComponent::MeshComponent(Geometry* geo) :m_Geometry(geo)
		{

			RenderSystem::GetRenderStats().m_IndicesCount += m_Geometry->GetIndicesCount();
			RenderSystem::GetRenderStats().m_VerticeCount += m_Geometry->GetVerticesCount();

			m_Geometry->BindVAO();

			for (auto& [index, ele] : m_Geometry->GetAttributes())
			{
				std::visit([&](auto&& arg)
					{
						arg.AssociateSlot(index);
					}, ele);
			}

			// Bind index buffer if used
			if (m_Geometry->IsUsingIndexBuffer())
				m_Geometry->m_IndexBuffer.LoadIndex();

			m_Geometry->UnBindVAO();
		}

		/*MeshComponent::MeshComponent(Shape::AABBBoundingBox* bound_volume): m_Geometry(bound_volume)
		{

		}*/

		void DirectionalLightComponent::LoadUniforms(Asset::Shader* shader) const
		{
			/*shader->SetUniform(ambient.Name.c_str(), ambient.Data);
			shader->SetUniform(diffuse.Name.c_str(), diffuse.Data);
			shader->SetUniform(specular.Name.c_str(), specular.Data);*/
			shader->SetUniform(ambient.Name.c_str(), ambient.Data);
			shader->SetUniform(direction.Name.c_str(), direction.Data);
		}

		void PointLightComponent::LoadUniforms(Asset::Shader* shader) const
		{
			shader->SetUniform(position.Name.c_str(), position.Data);
			shader->SetUniform(ambient.Name.c_str(), ambient.Data);
			/*shader->SetUniform(diffuse.Name.c_str(), diffuse.Data);
			shader->SetUniform(specular.Name.c_str(), specular.Data);
			shader->SetUniform(constant.Name.c_str(), constant.Data);
			shader->SetUniform(linear.Name.c_str(), linear.Data);
			shader->SetUniform(quadratic.Name.c_str(), quadratic.Data);*/
		}

		void SpotLightComponent::LoadUniforms(Asset::Shader* shader) const
		{
			/*shader->SetUniform(ambient.Name.c_str(), ambient.Data);
			shader->SetUniform(diffuse.Name.c_str(), diffuse.Data);
			shader->SetUniform(specular.Name.c_str(), specular.Data);
			shader->SetUniform(constant.Name.c_str(), constant.Data);
			shader->SetUniform(linear.Name.c_str(), linear.Data);
			shader->SetUniform(quadratic.Name.c_str(), quadratic.Data);
			shader->SetUniform(cutOff.Name.c_str(), cutOff.Data);
			shader->SetUniform(outerCutOff.Name.c_str(), outerCutOff.Data);*/
			shader->SetUniform(direction.Name.c_str(), direction.Data);
		}

		void MaterialComponent::LoadUniforms(Asset::Shader* shader) const
		{
			shader->SetUniform(Metalness.Name.c_str(), Metalness.Data);
			//shader->SetUniform(Reflectivity.Name.c_str(), Reflectivity.Data);
			//shader->SetUniform(ShineDamper.Name.c_str(), ShineDamper.Data);
		}



		DebugAABBBoundingBoxComponent::DebugAABBBoundingBoxComponent()
		{
			//Color = { "u_baseColor", {1.0f, 0.0f, 0.0f} };
			SetUpBuffers();
		}

		DebugAABBBoundingBoxComponent::~DebugAABBBoundingBoxComponent()
		{
			if (m_VAO != 0)
			{
				glDeleteVertexArrays(1, &m_VAO);
				m_VAO = 0;
			}
			if (m_VBO != 0)
			{
				glDeleteBuffers(1, &m_VBO);
				m_VBO = 0;
			}
		}

		void DebugAABBBoundingBoxComponent::LoadUniforms(Asset::Shader* shader) const
		{
			shader->SetUniform(Color.Name.c_str(), Color.Data);
			shader->SetUniform("u_useVertexColor", false);

		}

		void DebugAABBBoundingBoxComponent::LoadDynamicalBuffer(const Bounds& bounds)
		{
			//glBindVertexArray(m_VAO);
			//glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
			auto mins = bounds.mins + Vec3f{ 0.02f, 0.02f, 0.02f };
			auto maxs = bounds.maxs - Vec3f{ 0.02f, 0.02f, 0.02f };
			Vec3f vertices[24] = {
				{mins.x, mins.y, mins.z}, {maxs.x, mins.y, mins.z},
				{maxs.x, maxs.y, mins.z}, {mins.x, maxs.y, mins.z},
				{mins.x, mins.y, maxs.z}, {maxs.x, mins.y, maxs.z},
				{maxs.x, maxs.y, maxs.z}, {mins.x, maxs.y, maxs.z},
				{mins.x, mins.y, mins.z}, {mins.x, maxs.y, mins.z},
				{maxs.x, mins.y, mins.z}, {maxs.x, maxs.y, mins.z},
				{mins.x, mins.y, maxs.z}, {mins.x, maxs.y, maxs.z},
				{maxs.x, mins.y, maxs.z}, {maxs.x, maxs.y, maxs.z},
				{mins.x, mins.y, mins.z}, {mins.x, mins.y, maxs.z},
				{maxs.x, mins.y, mins.z}, {maxs.x, mins.y, maxs.z},
				{mins.x, maxs.y, mins.z}, {mins.x, maxs.y, maxs.z},
				{maxs.x, maxs.y, mins.z}, {maxs.x, maxs.y, maxs.z}
			};
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

		}

		void DebugAABBBoundingBoxComponent::BindVAO()
		{
			glBindVertexArray(m_VAO);
		}

		void DebugAABBBoundingBoxComponent::BindVBO()
		{
			glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
		}

		void DebugAABBBoundingBoxComponent::SetUpBuffers()
		{
			glGenVertexArrays(1, &m_VAO);
			glGenBuffers(1, &m_VBO);

			glBindVertexArray(m_VAO);
			glBindBuffer(GL_ARRAY_BUFFER, m_VBO);

			glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(Vec3f), nullptr, GL_DYNAMIC_DRAW);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
			glBindVertexArray(0);

		}



		/*DebugAABBBoundingBoxMeshComponent::DebugAABBBoundingBoxMeshComponent(const RefPtr<Attribute<Vec3f>>& Attribute)
		{
			using namespace Manager;
			m_Attribute = Attribute;
			m_AABB = ShapeManager::GetShape("AABBBoundingBox");
		}*/

		void AABBBoundingBoxComponent::LoadUniforms(Asset::Shader* shader) const
		{
			shader->SetUniform(Color.Name.c_str(), Color.Data);
			shader->SetUniform("u_useVertexColor", false);
		}

		DebugAABBBoundingBoxMeshComponent::DebugAABBBoundingBoxMeshComponent()
		{
			using namespace Manager;
			m_AABB = ShapeManager::GetShape("AABBBoundingBox");
			m_AABB->BindVAO();

			for (auto& [index, ele] : m_AABB->GetAttributes())
			{
				std::visit([&](auto&& arg)
					{
						arg.AssociateSlot(index);
					}, ele);
			}

			// Bind index buffer if used
			if (m_AABB->IsUsingIndexBuffer())
				m_AABB->m_IndexBuffer.LoadIndex();

			m_AABB->UnBindVAO();
		}

		DebugKDTreeVisualizer::DebugKDTreeVisualizer()
		{
			using namespace Manager;
			m_KDTree = ShapeManager::GetShape("KDTreeVisualizer");
			m_KDTree->BindVAO();

			for (auto& [index, ele] : m_KDTree->GetAttributes())
			{
				std::visit([&](auto&& arg)
					{
						arg.AssociateSlot(index);
					}, ele);
			}

			// Bind index buffer if used
			if (m_KDTree->IsUsingIndexBuffer())
				m_KDTree->m_IndexBuffer.LoadIndex();

			m_KDTree->UnBindVAO();
		}

		void DebugKDTreeVisualizer::LoadUniforms(Asset::Shader* shader) const
		{
			shader->SetUniform(Color.Name.c_str(), Color.Data);
			shader->SetUniform("u_useVertexColor", false);
		}
	}

}

