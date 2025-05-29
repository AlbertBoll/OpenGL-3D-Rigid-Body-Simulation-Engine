#include "gepch.h"
#include "Core/BaseApp.h"
#include "Core/Renderer.h"
#include "Core/RenderTarget.h"
#include "Windows/SDLWindow.h"
#include "Camera/PerspectiveCamera.h"
#include "Camera/OrthographicCamera.h"
#include <imgui/imgui.h>


namespace GEngine
{
	void Renderer::ImGuiRender()
	{
		
	}


	void Renderer::Initialize(const Math::Vec3f& clearColor)
	{

		GENGINE_CORE_INFO("OpenGL Info:\n  \t\t\t\t  Vendor: {}\n  \t\t\t        Renderer: {}\n  \t\t\t         Version: {}", (const char*)glGetString(GL_VENDOR), 
																															  (const char*)glGetString(GL_RENDERER), 
																															  (const char*)glGetString(GL_VERSION));
		//enable depth test
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
		//enable blending

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		//Enable multi-sampling
		glEnable(GL_MULTISAMPLE);
	

		glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
		
	
	}

	void Renderer::Set(const RenderParam& param)
	{
		auto color = param.ClearColor;
		glClearColor(color.r, color.g, color.b, color.a);

		if(param.bClearColorBit)   glClear(GL_COLOR_BUFFER_BIT);
		if(param.bClearDepthBit)   glClear(GL_DEPTH_BUFFER_BIT);
		if(param.bClearStencilBit) glClear(GL_STENCIL_BUFFER_BIT);
		if(param.bEnableDepthTest) glEnable(GL_DEPTH_TEST);
		else glDisable(GL_DEPTH_TEST);
	}


	void Renderer::Clear()
	{
		glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}


	/*void Renderer::RenderBegin(CameraBase* camera, Window* window, bool bClearColor, bool bClearDepth, RenderTarget* target)
	{
		if (!target)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, window->GetScreenWidth(), window->GetScreenHeight());
			
		}

		else
		{
			glBindFramebuffer(GL_FRAMEBUFFER, target->GetFrameBufferID());
			glViewport(0, 0, target->GetWidth(), target->GetHeight());
		}

		if (bClearColor) glClear(GL_COLOR_BUFFER_BIT);
		if (bClearDepth) glClear(GL_DEPTH_BUFFER_BIT);

		camera->OnUpdateView();
	}*/

	void Renderer::RenderBegin(CameraBase* camera, RenderTarget* target)
	{
		if (!target)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, m_WindowWidth, m_WindowHeight);
		}

		else
		{
			glBindFramebuffer(GL_FRAMEBUFFER, target->GetFrameBufferID());
			glViewport(0, 0, target->GetWidth(), target->GetHeight());
		}

		if(camera)
			camera->OnUpdateView();
	
	}

	void Renderer::RenderBegin(CameraBase* camera, bool bClearColor, bool bClearDepth, RenderTarget* target)
	{
		if (!target)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, m_WindowWidth, m_WindowHeight);
		}

		else
		{
			glBindFramebuffer(GL_FRAMEBUFFER, target->GetFrameBufferID());
			glViewport(0, 0, target->GetWidth(), target->GetHeight());
		}

		if (bClearColor) glClear(GL_COLOR_BUFFER_BIT);
		if (bClearDepth) glClear(GL_DEPTH_BUFFER_BIT);

		if (camera)
			camera->OnUpdateView();
	}

	void Renderer::RenderScene(Actor* scene, CameraBase* camera)
	{
		Render(scene, camera);
		glDepthFunc(GL_LESS);
	}

	void Renderer::RenderBeginHUD(CameraBase* camera)
	{
		glClear(GL_DEPTH_BUFFER_BIT);
		camera->OnUpdateView();
	}

	void Renderer::Render(Actor* scene, CameraBase* camera)
	{
		/*auto children = root->GetChildrenRef();
		if (children.empty())return;
		for (auto& child : children)
		{
			child->Render(camera);

			Render(child, camera);
		}*/

		if(scene && camera)
			scene->Render(camera);
	}

	void Renderer::RenderHelper(Actor* entity, CameraBase* camera)
	{
		auto children = entity->GetChildrenRef();
		//if (children.empty())return;
		for (auto& child : children)
		{
			child->Render(camera);

			RenderHelper(child, camera);
		}
	}
	

}
