#include "gepch.h"
#include "Core/Renderer2D.h"
#include "../Assets/ShaderBackend.h"
#include <Assets/Textures/Texture.h>
#include "Sprite/SpriteEntity.h"
#include "Core/RenderTarget.h"
#include "Camera/Camera.h"

namespace GEngine
{
    void Renderer2D::Groups::Set(const Material& material, std::vector<std::vector<SpriteEntity*>> groups)
    {
        m_Groups[Asset::ShaderBackendAccess::Program(*material.m_Shader)] = std::move(groups);
    }

	void Renderer2D::Render(SpriteEntity* entity, CameraBase* camera)
	{
		entity->Render(camera);
	}

	void Renderer2D::Render(const std::vector<SpriteEntity*>& entities, CameraBase* camera)
	{
		for(auto& entity: entities)
			entity->Render(camera);
	}

	void Renderer2D::RenderBegin(CameraBase* camera, RenderTarget* target)
	{
		if (!target)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, m_WindowWidth, m_WindowHeight);
		}

		else
		{
			target->Bind();
			glViewport(0, 0, target->GetWidth(), target->GetHeight());
		}

		//camera->OnUpdateView();
	}

	void Renderer2D::RenderSetup(const Render2DParam& param)
	{
		auto color = param.ClearColor;
		glClearColor(color.r, color.g, color.b, color.a);

		if (param.bClearColorBit)   glClear(GL_COLOR_BUFFER_BIT);
		if (param.bClearDepthBit)   glClear(GL_DEPTH_BUFFER_BIT);
		if (param.bClearStencilBit) glClear(GL_STENCIL_BUFFER_BIT);
		if (param.bEnableDepthTest) glEnable(GL_DEPTH_TEST);
		else glDisable(GL_DEPTH_TEST);
	}




	void Renderer2D::Initialize(const Vec2f& windowSize)
	{
		m_WindowWidth = (int)windowSize.x;
		m_WindowHeight = (int)windowSize.y;
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}


	void Renderer2D::Render(const Groups& groupsLookUp, CameraBase* camera)
	{
		SpriteComponent m_SpriteComp;
		for (auto& [id, groups] : groupsLookUp.m_Groups)
		{
			glUseProgram(id);
			groups[0][0]->BindVAO();

			groups[0][0]->GetMaterial()->SetUniforms<Mat4>({ {"u_projection", camera->m_Projection} });
			for (auto& group : groups)
			{
				group[0]->GetMaterial()->BindTextureUniforms();

				for (auto& entity : group)
				{
					entity->Render(camera);
				}
			}
		}
	}
}
