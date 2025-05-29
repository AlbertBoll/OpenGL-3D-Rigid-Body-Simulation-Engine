#include"gepch.h"
#include"Component/Component.h"
#include <Assets/Shaders/Shader.h>
#include "Assets/Textures/Texture.h"
#include <Core/RenderSystem.h>
#include "Geometry/Geometry.h"

namespace GEngine
{
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
				TextureList[texture->GetTextureID()] = {texture->GetTextureInfo().m_TextureSpec.m_TexTarget, i};
				shader->SetUniform(str.c_str(), std::pair<unsigned int, std::pair<unsigned, unsigned>>{texture->GetTextureInfo().m_TextureSpec.m_TexTarget, {texture->GetTextureID(), i++}});
			}
			//shader->UnBind();
		}

		void TexturesComponent::LoadUniforms(Asset::Shader* shader)const
		{
			shader->SetUniform(Tiling.Name.c_str(), Tiling.Data);

		}

		MeshComponent::MeshComponent(Geometry* geo):m_Geometry(geo)
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
	
		void DirectionalLightComponent::LoadUniforms(Asset::Shader* shader) const
		{
			/*shader->SetUniform(ambient.Name.c_str(), ambient.Data);
			shader->SetUniform(diffuse.Name.c_str(), diffuse.Data);
			shader->SetUniform(specular.Name.c_str(), specular.Data);*/
			shader->SetUniform(direction.Name.c_str(), direction.Data);
		}

		void PointLightComponent::LoadUniforms(Asset::Shader* shader) const
		{
			shader->SetUniform(ambient.Name.c_str(), ambient.Data);
			shader->SetUniform(diffuse.Name.c_str(), diffuse.Data);
			shader->SetUniform(specular.Name.c_str(), specular.Data);
			shader->SetUniform(constant.Name.c_str(), constant.Data);
			shader->SetUniform(linear.Name.c_str(), linear.Data);
			shader->SetUniform(quadratic.Name.c_str(), quadratic.Data);
		}

		void SpotLightComponent::LoadUniforms(Asset::Shader* shader) const
		{
			shader->SetUniform(ambient.Name.c_str(), ambient.Data);
			shader->SetUniform(diffuse.Name.c_str(), diffuse.Data);
			shader->SetUniform(specular.Name.c_str(), specular.Data);
			shader->SetUniform(constant.Name.c_str(), constant.Data);
			shader->SetUniform(linear.Name.c_str(), linear.Data);
			shader->SetUniform(quadratic.Name.c_str(), quadratic.Data);
			shader->SetUniform(cutOff.Name.c_str(), cutOff.Data);
			shader->SetUniform(outerCutOff.Name.c_str(), outerCutOff.Data);
			shader->SetUniform(direction.Name.c_str(), direction.Data);
		}

		



	}
}
